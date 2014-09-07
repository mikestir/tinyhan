/*
 * mac-tinyhan.c
 *
 *  Created on: 17 Aug 2014
 *      Author: mike
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "phy.h"
#include "timer.h"
#include "tinymac.h"

typedef enum {
	tinymacState_Unregistered = 0,
	tinymacState_BeaconRequest,
	tinymacState_Registering,
	tinymacState_Registered,
	tinymacState_SendPending,
	tinymacState_WaitAck,
} tinymac_state_t;

static const char *tinymac_states[] = {
		"Unregistered",
		"BeaconRequest",
		"Registering",
		"Registered",
		"SendPending",
		"WaitAck",
};

#define TINYMAC_MAX_NODES				32
#define TINYMAC_MAX_PAYLOAD				128
#define TINYMAC_MAX_RETRIES				3
#define TINYMAC_ACK_TIMEOUT				250
#define TINYMAC_BEACON_REQUEST_TIMEOUT	5000
#define TINYMAC_REGISTRATION_TIMEOUT	2000

typedef struct {
	tinymac_state_t		state;
	uint64_t			uuid;			/*< Unit identifier */
	timer_handle_t		timer;			/*< Timer for ack timeout, etc. */
	uint32_t			last_heard;		/*< Last heard time (ticks) */
	uint16_t			flags;			/*< Node flags (from registration) */
	uint8_t				retries;		/*< Number of tx tries remaining */
	uint8_t				addr;			/*< Short address */

	tinymac_header_t	pending_header;	/*< Header for pending packet */
	size_t				pending_size;	/*< Size of pending outbound payload */
	char				pending[TINYMAC_MAX_PAYLOAD];	/*< Pending outbound packet payload */
} tinymac_node_t;

typedef struct {
	tinymac_params_t	params;			/*< MAC configuration */

	uint8_t				net_id;			/*< Current network ID (if registered) */
	uint8_t				addr;			/*< Current device short address (if registered) */
	uint8_t				dseq;			/*< Current data sequence number */
	uint8_t				bseq;			/*< Current beacon serial number (if registered) */
	boolean_t			permit_attach;	/*< Whether or not we are accepting registration requests */
	uint16_t			slot;			/*< Current beacon slot */

	unsigned int		phy_mtu;		/*< MTU from PHY driver */
	tinymac_recv_cb_t	rx_cb;			/*< MAC data receive callback */

	tinymac_node_t		nodes[TINYMAC_MAX_NODES];	/*< Registered nodes (if we are a coordinator) */
	tinymac_node_t		coord;			/*< Coordinator (if we are a node) */
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

	printf("******************************************************************\n");
	printf("| Addr | UUID             | Flags | State           | Last Heard |\n");
	printf("******************************************************************\n");
	for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
		if (node->uuid) {
			printf("|  %02X  | %016llX | %02X    | %15s | %10u |\n",
				node->addr, node->uuid, node->flags, tinymac_states[node->state], node->last_heard);
		}
	}
	printf("******************************************************************\n\n");

}

static tinymac_node_t* tinymac_get_node_by_addr(uint8_t addr)
{
	tinymac_node_t *node = tinymac_ctx->nodes;
	unsigned int n;

	if (!tinymac_ctx->params.coordinator) {
		/* If we are not coordinator then the only node we can talk to
		 * is the coordinator with which we are registered */
		return (tinymac_ctx->coord.state != tinymacState_Unregistered && tinymac_ctx->coord.addr == addr) ? &tinymac_ctx->coord : NULL;
	}

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
			return node;
		}
	}
	return NULL;
}

static tinymac_node_t* tinymac_get_free_node(void)
{
	tinymac_node_t *node = tinymac_ctx->nodes;
	tinymac_node_t *fallback = NULL;
	unsigned int n;

	for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
		if (node->uuid == 0) {
			/* Prefer a previously unallocated slot */
			return node;
		}
		if (!fallback && node->state == tinymacState_Unregistered) {
			fallback = node;
		}
	}
	return fallback ? fallback : NULL;
}

static void tinymac_ack_timer(void *arg)
{
	tinymac_node_t *node = (tinymac_node_t*)arg;

	INFO("Ack timeout for node %02X\n", node->addr);
	if (node->retries--) {
		phy_buf_t bufs[] = {
				{ (char*)&node->pending_header, sizeof(tinymac_header_t) },
				{ (char*)node->pending, node->pending_size },
		};

		/* Re-send and schedule another timer */
		TRACE("OUT (retry): %04X %02X %02X %02X %02X (%u)\n",
				node->pending_header.flags,
				node->pending_header.net_id,
				node->pending_header.dest_addr,
				node->pending_header.src_addr,
				node->pending_header.seq,
				node->pending_size);
		node->timer = timer_request_callback(tinymac_ack_timer, node, TIMER_MILLIS(TINYMAC_ACK_TIMEOUT), TIMER_ONE_SHOT);
		phy_send(bufs, ARRAY_SIZE(bufs));
	} else {
		/* Give up */
		ERROR("Node %02X has gone away\n", node->addr);
		node->state = tinymacState_Unregistered;

		/* FIXME: Blurgh */
		if (node == &tinymac_ctx->coord) {
			tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
			tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
		}
	}
}

static void tinymac_timeout_timer(void *arg)
{
	if (tinymac_ctx->coord.state == tinymacState_BeaconRequest || tinymac_ctx->coord.state == tinymacState_Registering) {
		TRACE("Beacon request/registration timeout\n");
		tinymac_ctx->coord.state = tinymacState_Unregistered;
	} else {
		TRACE("Timeout callback skipped\n");
	}
}

static int tinymac_tx_packet(tinymac_node_t *dest, uint8_t flags_type, const char *buf, size_t size)
{
	tinymac_header_t hdr;
	phy_buf_t bufs[] = {
			{ (char*)&hdr, sizeof(hdr) },
			{ (char*)buf, size },
	};

	/* Check size against PHY MTU */
	if (size > TINYMAC_MAX_PAYLOAD || size > tinymac_ctx->phy_mtu) {
		ERROR("Packet too large\n");
		return -1;
	}

	if (dest && dest != &tinymac_ctx->coord) {
		/* Only one message may be in flight at a time to a given node */
		if (dest->state != tinymacState_Registered) {
			ERROR("Node %02X is %s\n", dest->addr,
					dest->state == tinymacState_Unregistered ? "not registered" : "busy");
			return -1;
		}

		/* If destination node is sleepy then copy the packet and defer the transmission */
		if (dest->flags & TINYMAC_ATTACH_FLAGS_SLEEPY) {
			TRACE("Pending transmission for node %02X\n", dest->addr);
			memcpy(dest->pending, buf, size);
			dest->pending_size = size;
			dest->state = tinymacState_SendPending;
			return 0;
		}
	}

	/* Build header and send now */
	hdr.flags = TINYMAC_FLAGS_VERSION | flags_type;
	hdr.net_id = tinymac_ctx->net_id;
	hdr.src_addr = tinymac_ctx->addr;
	hdr.dest_addr = dest ? dest->addr : TINYMAC_ADDR_BROADCAST;
	hdr.seq = ++tinymac_ctx->dseq;
	if (dest && (flags_type & TINYMAC_FLAGS_ACK_REQUEST)) {
		/* Start a timer and prepare for a retransmission if we don't get an ACK */
		TRACE("Waiting for ack from node %02X\n", dest->addr);
		memcpy(dest->pending, buf, size);
		memcpy(&dest->pending_header, &hdr, sizeof(hdr));
		dest->retries = TINYMAC_MAX_RETRIES;
		dest->pending_size = size;
		dest->state = tinymacState_WaitAck;
		dest->timer = timer_request_callback(tinymac_ack_timer, dest, TIMER_MILLIS(TINYMAC_ACK_TIMEOUT), TIMER_ONE_SHOT);
	}
	TRACE("OUT: %04X %02X %02X %02X %02X (%u)\n", hdr.flags, hdr.net_id, hdr.dest_addr, hdr.src_addr, hdr.seq, size);
	return phy_send(bufs, ARRAY_SIZE(bufs));
}

static int tinymac_tx_ack(tinymac_node_t *node, uint8_t seq)
{
	tinymac_header_t hdr;
	phy_buf_t bufs[] = {
			{ (char*)&hdr, sizeof(hdr) },
	};

	/* Build header and send now */
	hdr.flags = TINYMAC_FLAGS_VERSION | tinymacType_Ack;
	hdr.net_id = tinymac_ctx->net_id;
	hdr.src_addr = tinymac_ctx->addr;
	hdr.dest_addr = node->addr;
	hdr.seq = seq;
	TRACE("ACK: %04X %02X %02X %02X %02X\n", hdr.flags, hdr.net_id, hdr.dest_addr, hdr.src_addr, hdr.seq);
	return phy_send(bufs, ARRAY_SIZE(bufs));
}

static int tinymac_tx_beacon(boolean_t periodic)
{
	tinymac_header_t hdr;
	tinymac_beacon_t beacon;
	uint8_t addrlist[TINYMAC_MAX_NODES];
	size_t npending = 0;
	unsigned int n;
	phy_buf_t bufs[] = {
			{ (char*)&hdr, sizeof(hdr) },
			{ (char*)&beacon, sizeof(beacon) },
			{ (char*)addrlist, 0 },
	};

	if (!tinymac_ctx->params.coordinator) {
		/* Ignore if not a coordinator */
		return 0;
	}

	if (periodic) {
		tinymac_node_t *node;

		/* Build list of nodes with data pending */
		node = tinymac_ctx->nodes;
		for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
			if (node->state == tinymacState_SendPending) {
				addrlist[npending++] = node->addr;
			}
		}
	}
	bufs[2].size = npending;

	/* Build beacon */
	beacon.uuid = tinymac_ctx->params.uuid;
	beacon.timestamp = tinymac_ctx->slot;
	beacon.beacon_interval = TINYMAC_BEACON_INTERVAL_NO_BEACON;
	beacon.flags =
			(periodic ? TINYMAC_BEACON_FLAGS_SYNC : 0) |
			(tinymac_ctx->permit_attach ? TINYMAC_BEACON_FLAGS_PERMIT_ATTACH : 0);

	/* Build header and send */
	hdr.flags = TINYMAC_FLAGS_VERSION | tinymacType_Beacon;
	hdr.net_id = tinymac_ctx->net_id;
	hdr.src_addr = tinymac_ctx->addr;
	hdr.dest_addr = TINYMAC_ADDR_BROADCAST;
	hdr.seq = ++tinymac_ctx->bseq;
	TRACE("BEACON: %04X %02X %02X %02X %02X\n", hdr.flags, hdr.net_id, hdr.dest_addr, hdr.src_addr, hdr.seq);
	return phy_send(bufs, ARRAY_SIZE(bufs));
}

static void tinymac_rx_beacon(tinymac_header_t *hdr, size_t size)
{
	tinymac_beacon_t *beacon = (tinymac_beacon_t*)hdr->payload;

	if (tinymac_ctx->params.coordinator) {
		/* Ignore beacons if we are a coordinator */
		return;
	}

	TRACE("BEACON from %016llX %s\n", beacon->uuid, (beacon->flags & TINYMAC_BEACON_FLAGS_SYNC) ? "(SYNC)" : "(ADV)");

	/* Cancel beacon request timer */
	if (tinymac_ctx->coord.state == tinymacState_BeaconRequest) {
		TRACE("Canceling beacon request timer\n");
		timer_cancel_callback(tinymac_ctx->coord.timer);
	}

	if (beacon->flags & TINYMAC_BEACON_FLAGS_SYNC) {
		/* FIXME: Sync clock */
	}

	switch (tinymac_ctx->coord.state) {
	case tinymacState_Unregistered:
	case tinymacState_BeaconRequest:
		if (beacon->flags & TINYMAC_BEACON_FLAGS_PERMIT_ATTACH) {
			tinymac_registration_request_t attach;

			/* Temporarily bind with this network and send an attach request */
			tinymac_ctx->net_id = hdr->net_id;

			/* Build node descriptor for prospective coordinator */
			tinymac_ctx->coord.state = tinymacState_Registering;
			tinymac_ctx->coord.addr = hdr->src_addr;

			attach.uuid = tinymac_ctx->params.uuid;
			attach.flags = tinymac_ctx->params.flags;
			tinymac_tx_packet(&tinymac_ctx->coord, (uint16_t)tinymacType_RegistrationRequest,
					(const char*)&attach, sizeof(attach));

			/* Start callback timer */
			tinymac_ctx->coord.timer = timer_request_callback(tinymac_timeout_timer, NULL, TIMER_MILLIS(TINYMAC_REGISTRATION_TIMEOUT), TIMER_ONE_SHOT);
		}
		break;
	case tinymacState_Registered:
		/* FIXME: Check if we are in the address list */
		break;
	default:
		break;
	}
}

static void tinymac_rx_registration_response(tinymac_header_t *hdr, size_t size)
{
	tinymac_registration_response_t *addr = (tinymac_registration_response_t*)hdr->payload;

	if (tinymac_ctx->params.coordinator) {
		/* Ignore reg response if we are a coordinator */
		return;
	}

	TRACE("REG RESPONSE for %016llX %02X\n", addr->uuid, addr->addr);

	/* Check and ignore if UUID doesn't match our own */
	if (addr->uuid != tinymac_ctx->params.uuid) {
		return;
	}

	/* Cancel registration timer */
	if (tinymac_ctx->coord.state == tinymacState_Registering) {
		TRACE("Canceling registration timer\n");
		timer_cancel_callback(tinymac_ctx->coord.timer);
	}

	if (addr->status != tinymacRegistrationStatus_Success) {
		/* Coordinator rejected */
		ERROR("registration error %d\n", addr->status);
		tinymac_ctx->coord.state = tinymacState_Unregistered;
		return;
	}

	/* Update registration */
	if (addr->addr == TINYMAC_ADDR_UNASSIGNED) {
		/* Detachment */
		tinymac_ctx->coord.addr = TINYMAC_ADDR_UNASSIGNED;
		tinymac_ctx->coord.state = tinymacState_Unregistered;
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
	} else if (tinymac_ctx->coord.state == tinymacState_Registering) {
		/* Attachment - only if we are expecting it */
		tinymac_ctx->coord.addr = hdr->src_addr;
		tinymac_ctx->coord.state = tinymacState_Registered;
		tinymac_ctx->net_id = hdr->net_id;
	}
	tinymac_ctx->addr = addr->addr;

	INFO("New address %02X %02X\n", hdr->net_id, addr->addr);
}

static void tinymac_rx_registration_request(tinymac_header_t *hdr, size_t size)
{
	tinymac_registration_request_t *attach = (tinymac_registration_request_t*)hdr->payload;
	tinymac_registration_response_t resp;
	tinymac_node_t *node;

	if (!tinymac_ctx->params.coordinator) {
		/* Ignore if not a coordinator */
		return;
	}

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
		node->flags = attach->flags;
		node->last_heard = timer_get_tick_count();

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
	tinymac_tx_packet(NULL, (uint16_t)tinymacType_RegistrationResponse,
			(const char*)&resp, sizeof(resp));
}

static void tinymac_rx_deregistration_request(tinymac_header_t *hdr, size_t size)
{
	tinymac_deregistration_request_t *detach = (tinymac_deregistration_request_t*)hdr->payload;
	tinymac_registration_response_t resp;
	tinymac_node_t *node;

	if (!tinymac_ctx->params.coordinator) {
		/* Ignore if not a coordinator */
		return;
	}

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
	tinymac_tx_packet(node, (uint16_t)tinymacType_RegistrationResponse,
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
		node->last_heard = timer_get_tick_count();

		/* Check for ack request */
		if (hdr->flags & TINYMAC_FLAGS_ACK_REQUEST) {
			tinymac_tx_ack(node, hdr->seq);
		}
		/* FIXME: Check data pending (not applicable for coordinator which is always-on rx anyway) */
	}

	switch (hdr->flags & TINYMAC_FLAGS_TYPE_MASK) {
	case tinymacType_Beacon:
		/* Beacon */
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_beacon_t)) {
			ERROR("Discarding short packet\n");
			return;
		}
		tinymac_rx_beacon(hdr, size);
		break;
	case tinymacType_Ack:
		/* Acknowledgement */
		TRACE("RX ACK\n");

		if (node && node->state == tinymacState_WaitAck) {
			if (hdr->seq == node->pending_header.seq) {
				/* Ack ok, cancel timer */
				TRACE("Valid ack received from %02X for %02X\n", node->addr, hdr->seq);
				node->state = tinymacState_Registered;
				timer_cancel_callback(node->timer);
			} else {
				ERROR("Bad ack received from %02X\n", node->addr);
			}
		} else {
			ERROR("Unexpected ACK\n");
		}
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
	case tinymacType_RegistrationResponse:
		/* Attach/detach response message */
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_registration_response_t)) {
			ERROR("Discarding short packet\n");
			return;
		}
		tinymac_rx_registration_response(hdr, size);
		break;
	default:
		ERROR("Unsupported packet type\n");
	}
}

int tinymac_init(const tinymac_params_t *params)
{
	unsigned int n;

	memcpy(&tinymac_ctx->params, params, sizeof(tinymac_params_t));

	tinymac_ctx->dseq = rand();
	tinymac_ctx->bseq = rand();
	tinymac_ctx->permit_attach = FALSE;
	if (tinymac_ctx->params.coordinator) {
		tinymac_ctx->net_id = rand();
		tinymac_ctx->addr = 0x00;
	} else {
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
		tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
	}

	for (n = 0; n < TINYMAC_MAX_NODES; n++) {
		tinymac_ctx->nodes[n].state = tinymacState_Unregistered;
		tinymac_ctx->nodes[n].addr = n + 1;
	}
	tinymac_ctx->coord.state = tinymacState_Unregistered;

	/* Register PHY receive callback */
	phy_register_recv_cb(tinymac_recv_cb);
	tinymac_ctx->phy_mtu = phy_get_mtu();

	return 0;
}

void tinymac_register_recv_cb(tinymac_recv_cb_t cb)
{
	tinymac_ctx->rx_cb = cb;
}

void tinymac_recv_handler(void)
{
	/* Execute PHY function - this may call us back in the receive handler and cause
	 * a state change */
	phy_recv_handler();
}

void tinymac_tick_handler(void *arg)
{
	unsigned int n;
	tinymac_node_t *node;
	uint32_t now = timer_get_tick_count();

	if (tinymac_ctx->params.coordinator) {
		/* This is called once per beacon slot (250 ms) - check if a beacon is due in this
		 * slot and increment the counter */
		if (((++tinymac_ctx->slot) & ((1 << tinymac_ctx->params.beacon_interval) - 1)) == tinymac_ctx->params.beacon_offset) {
			/* Beacon due */
			TRACE("sync beacon\n");
			tinymac_tx_beacon(TRUE);
		}

		/* Search for lost nodes */
		node = tinymac_ctx->nodes;
		for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
			if (node->state == tinymacState_Registered &&
					(int32_t)((node->last_heard + TIMER_SECONDS(TINYMAC_LAST_HEARD_TIMEOUT)) - now) <= 0) {
				/* Send a ping */
				INFO("Pinging node %02X\n", node->addr);
				tinymac_tx_packet(node, TINYMAC_FLAGS_ACK_REQUEST | (uint16_t)tinymacType_Ping, NULL, 0);
			}
		}
	} else {
		/* Unregistered nodes may request a beacon */
		if (tinymac_ctx->coord.state == tinymacState_Unregistered) {
			tinymac_ctx->coord.state = tinymacState_BeaconRequest;
			tinymac_tx_packet(NULL, (uint16_t)tinymacType_BeaconRequest,
					NULL, 0);

			/* Set a timer for beacon request timeout */
			tinymac_ctx->coord.timer = timer_request_callback(tinymac_timeout_timer, NULL, TIMER_MILLIS(TINYMAC_BEACON_REQUEST_TIMEOUT), TIMER_ONE_SHOT);
		}

		/* Search for lost coordinator */
		if (tinymac_ctx->coord.state == tinymacState_Registered &&
				(int32_t)((tinymac_ctx->coord.last_heard + TIMER_SECONDS(TINYMAC_LAST_HEARD_TIMEOUT)) - now) <= 0) {
			/* Send a ping */
			INFO("Pinging node %02X\n", tinymac_ctx->coord.addr);
			tinymac_tx_packet(&tinymac_ctx->coord, TINYMAC_FLAGS_ACK_REQUEST | (uint16_t)tinymacType_Ping, NULL, 0);
		}
	}
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

	/* FIXME: How to request an ack? */
	return tinymac_tx_packet(node, (uint16_t)tinymacType_Data, buf, size);
}
