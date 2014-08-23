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
	tinymacState_BeaconRequest,
	tinymacState_Registering,
	tinymacState_Registered,
} tinymac_state_t;

#if DEBUG
static const char *tinymac_states[] = {
		"Unregistered",
		"BeaconRequest",
		"Registering",
		"Registered",
		"WaitAck",
		"Ack",
		"Nak",
};
#endif

static uint32_t get_timestamp(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint32_t)tv.tv_sec;
}

#if TINYMAC_COORDINATOR_SUPPORT
#define TINYMAC_MAX_REGISTRATIONS		32
#define TINYMAC_MAX_PAYLOAD				64

typedef struct {
	uint64_t			uuid;			/*< Unit identifier */
	uint32_t			last_heard;		/*< Last heard time */
	uint16_t			flags;			/*< Node flags (from registration) */
	uint8_t				addr;			/*< Address slot */
	size_t				pending_size;	/*< Size of pending outbound packet */
	char				pending[TINYMAC_MAX_PAYLOAD];	/*< Pending outbound packet */
} tinymac_registry_t;
#endif

typedef struct {
	uint64_t			uuid;			/*< Assigned unit identifier */
	uint8_t				net_id;			/*< Current network ID (if registered) */
	uint8_t				addr;			/*< Current device short address (if registered) */
	uint8_t				dseq;			/*< Current data sequence number */

	uint8_t				coord_addr;		/*< Short address of coordinator (if registered) */

	tinymac_state_t		state;			/*< Current MAC engine state */
	tinymac_state_t		next_state;		/*< Scheduled state change */
	uint32_t			timer;			/*< For timed state changes */

#if TINYMAC_COORDINATOR_SUPPORT
	uint8_t				bseq;			/*< Current beacon serial number (if registered) */
	boolean_t			coord;			/*< Whether or not we are a coordinator */
	boolean_t			permit_attach;	/*< Whether or not we are accepting registration requests */

	tinymac_registry_t	slot[TINYMAC_MAX_REGISTRATIONS];
#endif

	unsigned int		phy_mtu;		/*< MTU from PHY driver */
	tinymac_recv_cb_t	rx_cb;			/*< MAC data receive callback */
} tinymac_t;

static tinymac_t tinymac_ctx_;
static tinymac_t *tinymac_ctx = &tinymac_ctx_;

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

#if TINYMAC_COORDINATOR_SUPPORT
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
#endif

static void tinymac_rx_beacon(tinymac_header_t *hdr, size_t size)
{
	tinymac_beacon_t *beacon = (tinymac_beacon_t*)hdr->payload;

	TRACE("BEACON from %016llX %s\n", beacon->uuid, (beacon->flags & TINYMAC_BEACON_FLAGS_SYNC) ? "(SYNC)" : "(ADV)");

	if (beacon->flags & TINYMAC_BEACON_FLAGS_SYNC) {
		/* FIXME: Sync clock */
	}

	switch (tinymac_ctx->state) {
	case tinymacState_Unregistered:
		if (beacon->flags & TINYMAC_BEACON_FLAGS_PERMIT_ATTACH) {
			tinymac_registration_request_t attach;

			/* Temporarily bind with this network and send an attach request */
			tinymac_ctx->net_id = hdr->net_id;

			attach.uuid = tinymac_ctx->uuid;
			attach.flags = 0; /* FIXME: Node flags */
			tinymac_tx_packet((uint16_t)tinymacType_RegistrationRequest,
					hdr->src_addr, ++tinymac_ctx->dseq,
					(const char*)&attach, sizeof(attach));

			tinymac_ctx->state = tinymacState_Registering;
			tinymac_ctx->next_state = tinymacState_Unregistered;
			tinymac_ctx->timer = get_timestamp() + TINYMAC_RETRY_INTERVAL;
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

	TRACE("REG RESPONSE for %016llX %02X\n", addr->uuid, addr->addr);

	/* Check and ignore if UUID doesn't match our own */
	if (addr->uuid != tinymac_ctx->uuid) {
		return;
	}

	if (addr->status != tinymacRegistrationStatus_Success) {
		/* Coordinator rejected */
		ERROR("registration error %d\n", addr->status);
		tinymac_ctx->state = tinymacState_Unregistered;
		return;
	}

	/* Update registration */
	if (addr->addr == TINYMAC_ADDR_UNASSIGNED) {
		/* Detachment */
		tinymac_ctx->coord_addr = TINYMAC_ADDR_UNASSIGNED;
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
		tinymac_ctx->state = tinymacState_Unregistered;
	} else if (tinymac_ctx->state == tinymacState_Registering) {
		/* Attachment - only if we are expecting it */
		tinymac_ctx->coord_addr = hdr->src_addr;
		tinymac_ctx->net_id = hdr->net_id;
		tinymac_ctx->state = tinymacState_Registered;
	}
	tinymac_ctx->addr = addr->addr;

	/* Cancel timed state change */
	tinymac_ctx->timer = 0;

	INFO("New address %02X %02X\n", hdr->net_id, addr->addr);
}

#if TINYMAC_COORDINATOR_SUPPORT
static void tinymac_rx_registration_request(tinymac_header_t *hdr, size_t size)
{
	tinymac_registration_request_t *attach = (tinymac_registration_request_t*)hdr->payload;
	tinymac_registration_response_t addr;

	if (!tinymac_ctx->coord) {
		return;
	}

	TRACE("REG REQUEST\n");

	/* FIXME: Update coordinator's database */

	/* Send address update */
	addr.uuid = attach->uuid;
	addr.addr = rand();
	addr.status = tinymacRegistrationStatus_Success;
	tinymac_tx_packet((uint16_t)tinymacType_RegistrationResponse,
			hdr->src_addr, ++tinymac_ctx->dseq,
			(const char*)&addr, sizeof(addr));
}

static void tinymac_rx_deregistration_request(tinymac_header_t *hdr, size_t size)
{
	tinymac_deregistration_request_t *attach = (tinymac_deregistration_request_t*)hdr->payload;
	tinymac_registration_response_t addr;

	if (!tinymac_ctx->coord) {
		return;
	}

	TRACE("DEREG REQUEST\n");

	/* FIXME: Update coordinator's database */

	/* Send address update */
	addr.uuid = attach->uuid;
	addr.addr = TINYMAC_ADDR_UNASSIGNED;
	addr.status = tinymacRegistrationStatus_Success;
	tinymac_tx_packet((uint16_t)tinymacType_RegistrationResponse,
			hdr->src_addr, ++tinymac_ctx->dseq,
			(const char*)&addr, sizeof(addr));
}
#endif

static void tinymac_recv_cb(const char *buf, size_t size)
{
	tinymac_header_t *hdr = (tinymac_header_t*)buf;

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

	/* Data pending and Ack Request bits only checked on unicast packets */
	if (hdr->net_id != TINYMAC_NETWORK_ANY && hdr->dest_addr != TINYMAC_ADDR_BROADCAST) {
		if (hdr->flags & TINYMAC_FLAGS_ACK_REQUEST) {
			tinymac_tx_packet((uint16_t)tinymacType_Ack,
					hdr->src_addr, hdr->seq,
					NULL, 0);
		}
		/* FIXME: Check data pending */
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
	case tinymacType_RegistrationResponse:
		/* Attach/detach response message */
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_registration_response_t)) {
			ERROR("Discarding short packet\n");
			return;
		}
		tinymac_rx_registration_response(hdr, size);
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

#if TINYMAC_COORDINATOR_SUPPORT
	case tinymacType_BeaconRequest:
		/* This solicits an extra beacon */
		TRACE("BEACON REQUEST\n");
		if (tinymac_ctx->coord) {
			tinymac_tx_beacon(FALSE);
		}
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
#endif
	default:
		ERROR("Unsupported packet type\n");
	}
}

int tinymac_init(uint64_t uuid, boolean_t coord)
{
	tinymac_ctx->uuid = uuid;
	tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
	tinymac_ctx->coord_addr = TINYMAC_ADDR_UNASSIGNED;
	tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
	tinymac_ctx->dseq = rand();
	tinymac_ctx->timer = 0;
	tinymac_ctx->state = tinymacState_BeaconRequest;
	tinymac_ctx->next_state = tinymacState_Unregistered;

#if TINYMAC_COORDINATOR_SUPPORT
	tinymac_ctx->bseq = rand();
	tinymac_ctx->coord = coord;
	tinymac_ctx->permit_attach = FALSE;
	if (coord) {
		tinymac_ctx->net_id = rand();
		tinymac_ctx->addr = 0;
		tinymac_ctx->state = tinymacState_Registered;
	}
#endif

	/* Register PHY receive callback */
	phy_register_recv_cb(tinymac_recv_cb);
	tinymac_ctx->phy_mtu = phy_get_mtu();

	return 0;
}

void tinymac_process(void)
{
	uint32_t now = get_timestamp();

	/* Execute scheduled state changes */
	if (tinymac_ctx->timer && (int32_t)(tinymac_ctx->timer - now) <= 0) {
		TRACE("Timed state change => %s\n", tinymac_states[tinymac_ctx->next_state]);
		tinymac_ctx->state = tinymac_ctx->next_state;
		tinymac_ctx->timer = 0;
	}

	/* Execute PHY function - this may call us back in the receive handler and cause
	 * a state change */
	phy_process();

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
}

void tinymac_register_recv_cb(tinymac_recv_cb_t cb)
{
	tinymac_ctx->rx_cb = cb;
}

void tinymac_permit_attach(boolean_t permit)
{
#if TINYMAC_COORDINATOR_SUPPORT
	TRACE("permit_attach=%d\n", permit);

	tinymac_ctx->permit_attach = permit;
#endif
}

int tinymac_send(uint8_t dest, const char *buf, size_t size)
{
	if (tinymac_ctx->state != tinymacState_Registered) {
		ERROR("Busy or not registered\n");
		return -1;
	}

	return tinymac_tx_packet((uint16_t)tinymacType_Data,
			dest, ++tinymac_ctx->dseq,
			buf, size);
}
