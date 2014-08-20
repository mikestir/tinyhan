/*
 * mac-tinyhan.c
 *
 *  Created on: 17 Aug 2014
 *      Author: mike
 */

#include <stdint.h>

#include "common.h"
#include "phy.h"
#include "tinymac.h"

/* Get seconds, no support for sub-second beacons yet (FIXME:) */
#include <sys/time.h>

static uint32_t get_timestamp(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint32_t)tv.tv_sec;
}

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
static int tinymac_tx_beacon(void)
{
	tinymac_beacon_t beacon;

	beacon.uuid = tinymac_ctx->uuid;
	beacon.timestamp = get_timestamp();
	beacon.beacon_interval = TINYMAC_BEACON_INTERVAL_NO_BEACON;
	beacon.flags = tinymac_ctx->permit_attach ? TINYMAC_BEACON_FLAGS_PERMIT_ATTACH : 0;

	return tinymac_tx_packet((uint16_t)tinymacType_SolicitedBeacon,
			TINYMAC_ADDR_BROADCAST, ++tinymac_ctx->bseq,
			(const char*)&beacon, sizeof(beacon) /* + size of address list */);
}
#endif

static void tinymac_rx_beacon(tinymac_header_t *hdr)
{
	tinymac_beacon_t *beacon = (tinymac_beacon_t*)hdr->payload;

	TRACE("COORDINATOR: %016llX\n", beacon->uuid);

	if (tinymac_ctx->addr == TINYMAC_ADDR_UNASSIGNED && (beacon->flags & TINYMAC_BEACON_FLAGS_PERMIT_ATTACH)) {
		/* We can try to attach to this coordinator */
		tinymac_ctx->state = tinymacState_Attach;
		tinymac_ctx->coord_addr = hdr->src_addr;
		tinymac_ctx->net_id = hdr->net_id; /* Temporarily bind to the network ID we are trying to attach to */
		tinymac_ctx->next_event = get_timestamp();
	}

	/* FIXME: Check if we're in the address list */
}

static void tinymac_rx_address_response(tinymac_header_t *hdr)
{
	tinymac_address_response_t *addr = (tinymac_address_response_t*)hdr->payload;

	/* Check UUID */
	if (addr->uuid != tinymac_ctx->uuid) {
		return;
	}

	/* Update our address */
	if (addr->status != tinymacAddressStatus_Success) {
		ERROR("attach error %d\n", addr->status);
		tinymac_ctx->state = tinymacState_Idle;
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
		return;
	}
	tinymac_ctx->addr = addr->addr;
	tinymac_ctx->net_id = (addr->addr == TINYMAC_ADDR_UNASSIGNED) ? TINYMAC_NETWORK_ANY : hdr->net_id;
	INFO("New address %02X %02X\n", hdr->net_id, addr->addr);
	tinymac_ctx->state = tinymacState_Idle;
}

#if TINYMAC_COORDINATOR_SUPPORT
static void tinymac_rx_attach_request(tinymac_header_t *hdr)
{
	tinymac_attach_request_t *attach = (tinymac_attach_request_t*)hdr->payload;
	tinymac_address_response_t addr;

	/* FIXME: Update coordinator's database */

	/* Send address update */
	addr.uuid = attach->uuid;
	addr.addr = rand();
	addr.status = tinymacAddressStatus_Success;
	tinymac_tx_packet((uint16_t)tinymacType_AddressResponse,
			hdr->src_addr, ++tinymac_ctx->dseq,
			(const char*)&addr, sizeof(addr));
}

static void tinymac_rx_detach_request(tinymac_header_t *hdr)
{
	tinymac_detach_request_t *attach = (tinymac_detach_request_t*)hdr->payload;
	tinymac_address_response_t addr;

	/* FIXME: Update coordinator's database */

	/* Send address update */
	addr.uuid = attach->uuid;
	addr.addr = TINYMAC_ADDR_UNASSIGNED;
	addr.status = tinymacAddressStatus_Success;
	tinymac_tx_packet((uint16_t)tinymacType_AddressResponse,
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

	TRACE("IN: %04X %02X %02X %02X %02X (%u)\n", hdr->flags, hdr->net_id, hdr->dest_addr, hdr->src_addr, hdr->seq, size - sizeof(tinymac_header_t));

	if (tinymac_ctx->net_id != TINYMAC_NETWORK_ANY &&
			hdr->net_id != tinymac_ctx->net_id && hdr->net_id != TINYMAC_NETWORK_ANY) {
		TRACE("Not my network\n");
		return;
	}

	if (hdr->src_addr == tinymac_ctx->addr) {
		/* Ignore loopbacks */
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
	case tinymacType_PeriodicBeacon:
	case tinymacType_SolicitedBeacon:
		/* Beacon */
		TRACE("BEACON\n");
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_beacon_t)) {
			ERROR("Discarding short packet\n");
			return;
		}
		tinymac_rx_beacon(hdr);
		break;
	case tinymacType_Ack:
		/* Acknowledgement */
		TRACE("RX ACK\n");
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
		break;
	case tinymacType_AddressResponse:
		/* Attach/detach response message */
		TRACE("ADDRESS RESPONSE\n");
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_address_response_t)) {
			ERROR("Discarding short packet\n");
			return;
		}
		tinymac_rx_address_response(hdr);
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
		if (!tinymac_ctx->coord) {
			break;
		}

		/* This solicits an extra beacon */
		TRACE("BEACON REQUEST\n");
		tinymac_tx_beacon();
		break;
	case tinymacType_AttachRequest:
		if (!tinymac_ctx->coord) {
			break;
		}

		TRACE("ATTACH REQUEST\n");
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_attach_request_t)) {
			ERROR("Discarding short packet\n");
			return;
		}
		tinymac_rx_attach_request(hdr);
		break;
	case tinymacType_DetachRequest:
		if (!tinymac_ctx->coord) {
			break;
		}

		TRACE("DETACH REQUEST\n");
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_detach_request_t)) {
			ERROR("Discarding short packet\n");
			return;
		}
		tinymac_rx_detach_request(hdr);
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
	tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
	tinymac_ctx->dseq = rand();
	tinymac_ctx->next_event = get_timestamp();
	tinymac_ctx->state = tinymacState_Idle;

#if TINYMAC_COORDINATOR_SUPPORT
	tinymac_ctx->bseq = rand();
	tinymac_ctx->coord = coord;
	tinymac_ctx->permit_attach = FALSE;
	if (coord) {
		tinymac_ctx->net_id = rand();
		tinymac_ctx->addr = 0;
	}
#endif

	/* Register PHY receive callback */
	phy_register_recv_cb(tinymac_recv_cb);
	tinymac_ctx->phy_mtu = phy_get_mtu();

	return 0;
}

void tinymac_register_recv_cb(tinymac_recv_cb_t cb)
{
	tinymac_ctx->rx_cb = cb;
}

void tinymac_process(void)
{
	uint32_t now = get_timestamp();

	phy_process();

	/* Check for periodic events */
	if ((int32_t)(tinymac_ctx->next_event - now) <= 0) {
		switch (tinymac_ctx->state) {
		case tinymacState_Idle:
			if (tinymac_ctx->addr == TINYMAC_ADDR_UNASSIGNED) {
				/* Send beacon requests so we can try to attach */
				tinymac_tx_packet((uint16_t)tinymacType_BeaconRequest,
						TINYMAC_ADDR_BROADCAST, ++tinymac_ctx->dseq,
						NULL, 0);
			}
			break;
		case tinymacState_Attach: {
			tinymac_attach_request_t attach;

			/* Attempt to attach to the coordinator from the last beacon */
			attach.uuid = tinymac_ctx->uuid;
			attach.flags = 0;
			tinymac_tx_packet((uint16_t)tinymacType_AttachRequest,
					tinymac_ctx->coord_addr, ++tinymac_ctx->dseq,
					(const char*)&attach, sizeof(attach));
		} break;
		}
		tinymac_ctx->next_event = now + TINYMAC_RETRY_INTERVAL;
	}

#if TINYMAC_COORDINATOR_SUPPORT
	if (tinymac_ctx->coord) {
		/* Send periodic beacons */
	}
#endif
}

void tinymac_permit_attach(boolean_t permit)
{
#if TINYMAC_COORDINATOR_SUPPORT
	TRACE("permit_attach=%d\n", permit);

	if (tinymac_ctx->coord) {
		tinymac_ctx->permit_attach = permit;
	}
#endif
}

int tinymac_send(uint8_t dest, const char *buf, size_t size)
{
	if (tinymac_ctx->addr == TINYMAC_ADDR_UNASSIGNED) {
		ERROR("Not attached\n");
		return -1;
	}

	return tinymac_tx_packet((uint16_t)tinymacType_Data /* FIXME: ack request? */,
			dest, ++tinymac_ctx->dseq,
			buf, size);
}
