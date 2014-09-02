/*
 * tinymac-node.c
 *
 * TinyHAN MAC layer (slave node only)
 *
 *  Created on: 17 Aug 2014
 *      Author: mike
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "phy.h"
#include "tinymac.h"

/* Get seconds, no support for sub-second beacons yet (FIXME:) */
#include <sys/time.h>

typedef enum {
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
};
#endif

static uint32_t get_timestamp(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint32_t)tv.tv_sec;
}

typedef struct {
	tinymac_params_t	params;			/*< MAC configuration */
	uint8_t				net_id;			/*< Current network ID (if registered) */
	uint8_t				addr;			/*< Current device short address (if registered) */
	uint8_t				dseq;			/*< Current data sequence number */

	uint8_t				hub_addr;		/*< Short address of hub (if registered) */

	tinymac_state_t		state;			/*< Current MAC engine state */
	tinymac_state_t		next_state;		/*< Scheduled state change */
	uint32_t			timer;			/*< For timed state changes */

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

			attach.uuid = tinymac_ctx->params.uuid;
			attach.flags = tinymac_ctx->params.flags;
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
	if (addr->uuid != tinymac_ctx->params.uuid) {
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
		tinymac_ctx->hub_addr = TINYMAC_ADDR_UNASSIGNED;
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
		tinymac_ctx->state = tinymacState_Unregistered;
	} else if (tinymac_ctx->state == tinymacState_Registering) {
		/* Attachment - only if we are expecting it */
		tinymac_ctx->hub_addr = hdr->src_addr;
		tinymac_ctx->net_id = hdr->net_id;
		tinymac_ctx->state = tinymacState_Registered;
	}
	tinymac_ctx->addr = addr->addr;

	/* Cancel timed state change */
	tinymac_ctx->timer = 0;

	INFO("New address %02X %02X\n", hdr->net_id, addr->addr);
}

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

	default:
		ERROR("Unsupported packet type\n");
	}
}

int tinymac_init(const tinymac_params_t *params)
{
	memcpy(&tinymac_ctx->params, params, sizeof(tinymac_params_t));

	tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
	tinymac_ctx->hub_addr = TINYMAC_ADDR_UNASSIGNED;
	tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
	tinymac_ctx->dseq = rand();
	tinymac_ctx->timer = 0;
	tinymac_ctx->state = tinymacState_BeaconRequest;
	tinymac_ctx->next_state = tinymacState_Unregistered;

	/* Register PHY receive callback */
	phy_register_recv_cb(tinymac_recv_cb);
	tinymac_ctx->phy_mtu = phy_get_mtu();

	return 0;
}

void tinymac_recv_handler(void)
{
	/* Execute PHY function - this may call us back in the receive handler and cause
	 * a state change */
	phy_recv_handler();
}

void tinymac_tick_handler(void *arg)
{
	uint32_t now = get_timestamp();

	/* Execute scheduled state changes */
	if (tinymac_ctx->timer && (int32_t)(tinymac_ctx->timer - now) <= 0) {
		TRACE("Timed state change => %s\n", tinymac_states[tinymac_ctx->next_state]);
		tinymac_ctx->state = tinymac_ctx->next_state;
		tinymac_ctx->timer = 0;
	}

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
