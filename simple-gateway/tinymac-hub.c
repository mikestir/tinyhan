/*
 * mac-tinyhan.c
 *
 *  Created on: 17 Aug 2014
 *      Author: mike
 */

#include <stdint.h>
#include <stdlib.h>

#include "common.h"
#include "phy.h"
#include "tinymac.h"

/* Get seconds, no support for sub-second beacons yet (FIXME:) */
#include <sys/time.h>

typedef enum {
	/* Client states */
	tinymacState_Unregistered = 0,
//	tinymacState_BeaconRequest,	/* not used by coordinator */
//	tinymacState_Registering, /* not used by coordinator */
	tinymacState_Registered,
	tinymacState_SendPending,
	tinymacState_WaitAck,
} tinymac_state_t;

static const char *tinymac_states[] = {
		"Unregistered",
//		"BeaconRequest",
//		"Registering",
		"Registered",
		"SendPending",
		"WaitAck",
};

static uint32_t get_timestamp(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint32_t)tv.tv_sec;
}

#define TINYMAC_MAX_NODES				32
#define TINYMAC_MAX_PAYLOAD				128

typedef struct {
	tinymac_state_t		state;
	uint64_t			uuid;			/*< Unit identifier */
	uint32_t			timer;			/*< Timer for state machine periodic events */
	uint32_t			last_heard;		/*< Last heard time */
	uint16_t			flags;			/*< Node flags (from registration) */
	uint8_t				addr;			/*< Short address */
	size_t				pending_size;	/*< Size of pending outbound packet */
	char				pending[TINYMAC_MAX_PAYLOAD];	/*< Pending outbound packet */
} tinymac_node_t;

typedef struct {
	uint64_t			uuid;			/*< Assigned unit identifier */
	uint8_t				net_id;			/*< Current network ID (if registered) */
	uint8_t				addr;			/*< Current device short address (if registered) */
	uint8_t				dseq;			/*< Current data sequence number */
	uint8_t				bseq;			/*< Current beacon serial number (if registered) */
	boolean_t			permit_attach;	/*< Whether or not we are accepting registration requests */

	tinymac_node_t		nodes[TINYMAC_MAX_NODES];

	unsigned int		phy_mtu;		/*< MTU from PHY driver */
	tinymac_recv_cb_t	rx_cb;			/*< MAC data receive callback */
} tinymac_t;

static tinymac_t tinymac_ctx_;
static tinymac_t *tinymac_ctx = &tinymac_ctx_;

static void tinymac_dump_nodes(void)
{
	tinymac_node_t *node = tinymac_ctx->nodes;
	unsigned int n;

	printf("Network %02X\n", tinymac_ctx->net_id);
	printf("Permit attach: %s\n\n", tinymac_ctx->permit_attach ? "Yes" : "No");
	printf("Registered nodes:\n\n");

	printf("**********************************************************\n");
	printf("| Addr | UUID             | State           | Last Heard |\n");
	printf("**********************************************************\n");
	for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
		if (node->uuid) {
			printf("|  %02X  | %016llX | %15s | %10u |\n",
				node->addr, node->uuid, tinymac_states[node->state], node->last_heard);
		}
	}
}

static tinymac_node_t* tinymac_get_node_by_addr(uint8_t addr)
{
	tinymac_node_t *node = tinymac_ctx->nodes;
	unsigned int n;

	for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
		if (node->state > tinymacState_Unregistered && node->addr == addr) {
			break;
		}
	}
	return (n == TINYMAC_MAX_NODES) ? NULL : node;
}

static tinymac_node_t* tinymac_get_node_by_uuid(uint64_t uuid)
{
	tinymac_node_t *node = tinymac_ctx->nodes;
	unsigned int n;

	for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
		if (node->uuid == uuid) {
			break;
		}
	}
	return (n == TINYMAC_MAX_NODES) ? NULL : node;
}

static tinymac_node_t* tinymac_get_free_node(void)
{
	tinymac_node_t *node = tinymac_ctx->nodes;
	unsigned int n;

	for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
		if (node->state == tinymacState_Unregistered) {
			break;
		}
	}
	return (n == TINYMAC_MAX_NODES) ? NULL : node;
}

static int tinymac_tx_packet(uint8_t flags_type, uint8_t dest, uint8_t seq,
		const char *buf, size_t size)
{
	tinymac_header_t hdr;
	phy_buf_t bufs[] = {
			{ (char*)&hdr, sizeof(hdr) },
			{ (char*)buf, size },
	};

	/* Check size against PHY MTU */
	if (size > tinymac_ctx->phy_mtu) {
		ERROR("Packet too large\n");
		return -1;
	}

	/* Build header and send */
	hdr.flags = TINYMAC_FLAGS_VERSION | flags_type;
	hdr.net_id = tinymac_ctx->net_id;
	hdr.src_addr = tinymac_ctx->addr;
	hdr.dest_addr = dest;
	hdr.seq = seq;
	TRACE("OUT: %04X %02X %02X %02X %02X (%u)\n", hdr.flags, hdr.net_id, hdr.dest_addr, hdr.src_addr, hdr.seq, size);
	return phy_send(bufs, ARRAY_SIZE(bufs));
}

static int tinymac_tx_beacon(boolean_t periodic)
{
	tinymac_beacon_t beacon;

	beacon.uuid = tinymac_ctx->uuid;
	beacon.timestamp = get_timestamp();
	beacon.beacon_interval = TINYMAC_BEACON_INTERVAL_NO_BEACON;
	beacon.flags =
			(periodic ? TINYMAC_BEACON_FLAGS_SYNC : 0) |
			(tinymac_ctx->permit_attach ? TINYMAC_BEACON_FLAGS_PERMIT_ATTACH : 0);

	return tinymac_tx_packet((uint16_t)tinymacType_Beacon,
			TINYMAC_ADDR_BROADCAST, ++tinymac_ctx->bseq,
			(const char*)&beacon, sizeof(beacon) /* + size of address list */);
}

static void tinymac_rx_registration_request(tinymac_header_t *hdr, size_t size)
{
	tinymac_registration_request_t *attach = (tinymac_registration_request_t*)hdr->payload;
	tinymac_registration_response_t resp;
	tinymac_node_t *node;

	TRACE("REG REQUEST\n");

	/* Search for existing registration */
	node = tinymac_get_node_by_uuid(attach->uuid);
	if (!node) {
		/* Try to allocated new slot */
		node = tinymac_get_free_node();
	}

	if (node) {
		INFO("Registered node %02X for %016llX with flags %04X\n", node->addr, attach->uuid, attach->flags);
		node->state = tinymacState_Registered;
		node->uuid = attach->uuid;
		node->last_heard = get_timestamp();

		resp.uuid = attach->uuid;
		resp.addr = node->addr;
		resp.status = tinymacRegistrationStatus_Success;
	} else {
		ERROR("Network full\n");
		resp.uuid = attach->uuid;
		resp.addr = TINYMAC_ADDR_UNASSIGNED;
		resp.status = tinymacRegistrationStatus_NetworkFull;
	}

	tinymac_dump_nodes();

	/* Send response */
	tinymac_tx_packet((uint16_t)tinymacType_RegistrationResponse,
			hdr->src_addr, ++tinymac_ctx->dseq,
			(const char*)&resp, sizeof(resp));
}

static void tinymac_rx_deregistration_request(tinymac_header_t *hdr, size_t size)
{
	tinymac_deregistration_request_t *detach = (tinymac_deregistration_request_t*)hdr->payload;
	tinymac_registration_response_t resp;
	tinymac_node_t *node;

	TRACE("DEREG REQUEST\n");

	node = tinymac_get_node_by_addr(hdr->src_addr);
	if (!node || node->uuid != detach->uuid) {
		/* Ignore if source address not known or UUID doesn't match */
		ERROR("Bad deregistration request from %016llX\n", detach->uuid);
		return;
	}

	/* Free the slot */
	INFO("De-registered node %02X for %016llX reason %u\n", node->addr, node->uuid, detach->reason);
	node->state = tinymacState_Unregistered;

	tinymac_dump_nodes();

	/* Send response */
	resp.uuid = detach->uuid;
	resp.addr = TINYMAC_ADDR_UNASSIGNED;
	resp.status = tinymacRegistrationStatus_Success;
	tinymac_tx_packet((uint16_t)tinymacType_RegistrationResponse,
			hdr->src_addr, ++tinymac_ctx->dseq,
			(const char*)&resp, sizeof(resp));
}

static void tinymac_recv_cb(const char *buf, size_t size)
{
	tinymac_header_t *hdr = (tinymac_header_t*)buf;
	tinymac_node_t *node;

	if (size < sizeof(tinymac_header_t)) {
		ERROR("Discarding short packet\n");
		return;
	}

	if (hdr->src_addr == tinymac_ctx->addr) {
		/* Quietly ignore loopbacks */
		return;
	}

	TRACE("IN: %04X %02X %02X %02X %02X (%u)\n", hdr->flags, hdr->net_id, hdr->dest_addr, hdr->src_addr, hdr->seq, size - sizeof(tinymac_header_t));

	if (tinymac_ctx->net_id != TINYMAC_NETWORK_ANY &&
			hdr->net_id != tinymac_ctx->net_id && hdr->net_id != TINYMAC_NETWORK_ANY) {
		TRACE("Not my network\n");
		return;
	}

	if (hdr->dest_addr != tinymac_ctx->addr && hdr->dest_addr != TINYMAC_ADDR_BROADCAST) {
		TRACE("Not my addr\n");
		return;
	}

	/* For unicast packets we expect the source node to be registered */
	if (hdr->net_id != TINYMAC_NETWORK_ANY && hdr->src_addr != TINYMAC_ADDR_UNASSIGNED &&
			hdr->dest_addr != TINYMAC_ADDR_BROADCAST) {
		/* Check registration - we must not ACK packets from unregistered nodes */
		node = tinymac_get_node_by_addr(hdr->src_addr);
		if (!node) {
			ERROR("Source node is not registered\n");
			return;
		}
		node->last_heard = get_timestamp();

		/* Check for ack request */
		if (hdr->flags & TINYMAC_FLAGS_ACK_REQUEST) {
			tinymac_tx_packet((uint16_t)tinymacType_Ack,
					hdr->src_addr, hdr->seq,
					NULL, 0);
		}
		/* FIXME: Check data pending (not applicable for coordinator which is always-on rx anyway) */
	}

	switch (hdr->flags & TINYMAC_FLAGS_TYPE_MASK) {
	case tinymacType_Ack:
		/* Acknowledgement */
		TRACE("RX ACK\n");
#if 0 /* FIXME: Not supported yet */
		if (tinymac_ctx->state == tinymacState_WaitAck) {
			if (hdr->seq == tinymac_ctx->dseq) {
				tinymac_ctx->state = tinymacState_Ack;
			} else {
				ERROR("Out of sequence ACK\n");
				tinymac_ctx->state = tinymacState_Nak;
			}
		} else {
			ERROR("Unexpected ACK\n");
		}
#endif
		break;
	case tinymacType_Ping:
		/* This just solicits an ack, which happens above */
		TRACE("PING\n");
		break;
	case tinymacType_Data:
		/* Forward to upper layer */
		TRACE("RX DATA\n");
		if (tinymac_ctx->rx_cb) {
			tinymac_ctx->rx_cb(hdr->src_addr, hdr->payload, size - sizeof(tinymac_header_t));
		}
		break;

	case tinymacType_BeaconRequest:
		/* This solicits an extra beacon */
		TRACE("BEACON REQUEST\n");
		tinymac_tx_beacon(FALSE);
		break;
	case tinymacType_RegistrationRequest:
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_registration_request_t)) {
			ERROR("Discarding short packet\n");
			return;
		}
		tinymac_rx_registration_request(hdr, size);
		break;
	case tinymacType_DeregistrationRequest:
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_deregistration_request_t)) {
			ERROR("Discarding short packet\n");
			return;
		}
		tinymac_rx_deregistration_request(hdr, size);
		break;
	default:
		ERROR("Unsupported packet type\n");
	}
}

int tinymac_init(uint64_t uuid)
{
	unsigned int n;

	tinymac_ctx->uuid = uuid;
	tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
	tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
	tinymac_ctx->dseq = rand();
	tinymac_ctx->bseq = rand();
	tinymac_ctx->permit_attach = FALSE;
	tinymac_ctx->net_id = rand();
	tinymac_ctx->addr = 0x00;

	for (n = 0; n < TINYMAC_MAX_NODES; n++) {
		tinymac_ctx->nodes[n].state = tinymacState_Unregistered;
		tinymac_ctx->nodes[n].addr = n + 1;
	}

	/* Register PHY receive callback */
	phy_register_recv_cb(tinymac_recv_cb);
	tinymac_ctx->phy_mtu = phy_get_mtu();

	return 0;
}

void tinymac_process(void)
{
	uint32_t now = get_timestamp();

#if 0
	/* Execute scheduled state changes */
	if (tinymac_ctx->timer && (int32_t)(tinymac_ctx->timer - now) <= 0) {
		TRACE("Timed state change => %s\n", tinymac_states[tinymac_ctx->next_state]);
		tinymac_ctx->state = tinymac_ctx->next_state;
		tinymac_ctx->timer = 0;
	}
#endif

	/* Execute PHY function - this may call us back in the receive handler and cause
	 * a state change */
	phy_process();

#if 0
	/* MAC state machine */
	switch (tinymac_ctx->state) {
	case tinymacState_Unregistered:
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;

		/* Schedule beacon request */
		if (!tinymac_ctx->timer) {
			tinymac_ctx->next_state = tinymacState_BeaconRequest;
			tinymac_ctx->timer = now + TINYMAC_RETRY_INTERVAL;
		}
		break;
	case tinymacState_BeaconRequest:
		/* Send beacon request and return to idle */
		tinymac_tx_packet((uint16_t)tinymacType_BeaconRequest,
				TINYMAC_ADDR_BROADCAST, ++tinymac_ctx->dseq,
				NULL, 0);
		tinymac_ctx->state = tinymacState_Unregistered;
		break;
	}
#endif
}

void tinymac_register_recv_cb(tinymac_recv_cb_t cb)
{
	tinymac_ctx->rx_cb = cb;
}

void tinymac_permit_attach(boolean_t permit)
{
	TRACE("permit_attach=%d\n", permit);

	tinymac_ctx->permit_attach = permit;
}

int tinymac_send(uint8_t dest, const char *buf, size_t size)
{
	tinymac_node_t *node;

	node = tinymac_get_node_by_addr(dest);
	if (!node) {
		ERROR("Node %02X not registered\n", dest);
		return -1;
	}
	if (node->state != tinymacState_Registered) {
		ERROR("Node %02X busy\n");
		return -1;
	}

	if (node->flags & TINYMAC_ATTACH_FLAGS_SLEEPY) {
		/* FIXME: Defer tx and go to tinymacState_SendPending */
		return -1;
	} else {
		/* Send now */
		/* FIXME: What about ACK request? */
		return tinymac_tx_packet((uint16_t)tinymacType_Data,
				dest, ++tinymac_ctx->dseq,
				buf, size);
	}
}
