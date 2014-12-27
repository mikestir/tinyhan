/*
 * Copyright 2013-2014 Mike Stirling
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of the Tiny Home Area Network stack.
 *
 * http://www.tinyhan.co.uk/
 *
 * tinymac.c
 *
 * TinyHAN MAC layer
 *
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#undef DEBUG

#include "common.h"
#include "phy.h"
#include "tinymac.h"

typedef enum {
	tinymacClientState_Unregistered = 0,
	tinymacClientState_BeaconRequest,
	tinymacClientState_Registering,
	tinymacClientState_Registered,
} tinymac_client_state_t;

#if WITH_TINYMAC_COORDINATOR
static const char *tinymac_node_states[] = {
		"Unregistered",
		"Registered",
		"SendPending",
		"WaitAck",
};
#endif

typedef struct {
	/**********/
	/* Common */
	/**********/

	tinymac_params_t		params;			/*< MAC configuration */
	unsigned int			phy_mtu;		/*< MTU from PHY driver */
	tinymac_recv_cb_t		rx_cb;			/*< MAC data receive callback */
	uint32_t				tick_count;		/*< Tick counter */

	/* net_id and addr are assigned by the coordinator upon registration, or by
	 * software if this node is the coordinator */
	uint8_t					net_id;			/*< Current network ID (if assigned) */
	uint8_t					addr;			/*< Current device short address (if assigned) */
	uint8_t					dseq;			/*< Current outbound data sequence number */
	uint16_t				slot;			/*< Current beacon slot */

	/**********/
	/* Client */
	/**********/

	tinymac_client_state_t	state;			/*< Current client state for this node */
	tinymac_node_t			coord;			/*< Client: Associated coordinator */
	tinymac_timer_t			timer;			/*< Timer for registration/beacon requests */

#if WITH_TINYMAC_COORDINATOR
	/***************/
	/* Coordinator */
	/***************/

	uint8_t					bseq;			/*< Current outbound beacon serial number (coordinator) */
	tinymac_node_t	nodes[TINYMAC_MAX_NODES];	/*< List of known nodes */
	boolean_t				permit_attach;	/*< Whether or not we are acceptng registration requests */
	tinymac_reg_cb_t		reg_cb;			/*< Node registration callback */
	tinymac_reg_cb_t		dereg_cb;		/*< Node deregistration callback */
#endif
} tinymac_t;

static tinymac_t tinymac_ctx_;
static tinymac_t *tinymac_ctx = &tinymac_ctx_;

static int tinymac_phy_send(phy_buf_t *bufs, unsigned int nbufs, uint8_t flags);


/****************************/
/* Callback timer functions */
/****************************/

static inline void tinymac_set_timer(tinymac_timer_t *timer, tinymac_timer_cb_t cb, void *arg, uint32_t delay)
{
	timer->callback = cb;
	timer->arg = arg;
	timer->expiry = tinymac_ctx->tick_count + delay;
}

static inline void tinymac_cancel_timer(tinymac_timer_t *timer)
{
	timer->callback = NULL;
}

static void tinymac_despatch_timer(tinymac_timer_t *timer)
{
	if (timer->callback && ((int32_t)(tinymac_ctx->tick_count - timer->expiry) >= 0)) {
		tinymac_timer_cb_t callback = timer->callback;

		/* Callback may set a new timer, so we need to clear the current one first */
		timer->callback = NULL;
		callback(timer->arg);
	}
}

/*****************/
/* Node registry */
/*****************/

#if WITH_TINYMAC_COORDINATOR
static tinymac_node_t* tinymac_get_node_by_addr(uint8_t addr)
{
	tinymac_node_t *node = tinymac_ctx->nodes;
	unsigned int n;

	if (tinymac_ctx->params.coordinator) {
		for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
			if (node->addr == addr && node->state != tinymacNodeState_Unregistered) {
				return node;
			}
		}
	} else {
		/* Clients can only communicate with the coordinator/hub */
		return (tinymac_ctx->coord.state != tinymacNodeState_Unregistered &&
				tinymac_ctx->coord.addr == addr) ? &tinymac_ctx->coord : NULL;
	}

	return NULL;
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
		if (!fallback && node->state == tinymacNodeState_Unregistered) {
			fallback = node;
		}
	}
	return fallback ? fallback : NULL;
}
#else
static tinymac_node_t* tinymac_get_node_by_addr(uint8_t addr)
{
	/* Clients can only communicate with the coordinator/hub */
	return (tinymac_ctx->coord.state != tinymacNodeState_Unregistered &&
			tinymac_ctx->coord.addr == addr) ? &tinymac_ctx->coord : NULL;
}
#endif

static void tinymac_deregister_node(tinymac_node_t *node)
{
	ERROR("Node %02X has gone away\n", node->addr);

	node->state = tinymacNodeState_Unregistered;
	if (node == &tinymac_ctx->coord) {
		/* This node was our coordinator, so we are now unregistered */
		tinymac_ctx->state = tinymacClientState_Unregistered;
		tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
	}

#if WITH_TINYMAC_COORDINATOR
	/* Invoke callback */
	if (tinymac_ctx->dereg_cb) {
		tinymac_ctx->dereg_cb((const tinymac_node_t*)node);
	}

	tinymac_dump_nodes();
#endif
}

/*****************************/
/* Timeout handler callbacks */
/*****************************/

/*!
 * Timer callback invoked when a beacon request or registration request are
 * not answered in time.
 * Invalidates the client's temporary coordinator association
 */
static void tinymac_request_timeout(void *arg)
{
	if (tinymac_ctx->state == tinymacClientState_BeaconRequest || tinymac_ctx->state == tinymacClientState_Registering) {
		/* Coordinator has gone away */
		TRACE("Beacon request/registration timeout\n");
		tinymac_deregister_node(&tinymac_ctx->coord);
	} else {
		TRACE("Timeout callback skipped\n");
	}
}

/*!
 * Timer callback invoked when a pending send is not completed within the
 * packet's validity period.
 */
static void tinymac_validity_timeout(void *arg)
{
	tinymac_node_t *node = (tinymac_node_t*)arg;

	ERROR("Validity expired for pending send to node %02X\n", node->addr);

	/* Clear pending packet */
	node->state = tinymacNodeState_Registered;
	tinymac_cancel_timer(&node->ack_timer);

	/* Invoke callback for failure */
	if (node->send_cb) {
		node->send_cb(-1);
	}
}

/*!
 * Timer callback invoked when a packet has been sent with AR set and no
 * acknowledgement was received
 */
static void tinymac_ack_timeout(void *arg)
{
	tinymac_node_t *node = (tinymac_node_t*)arg;

	INFO("Ack timeout for node %02X\n", node->addr);
	if (node->retries--) {
		phy_buf_t bufs[] = {
				{ (char*)&node->pending_header, sizeof(tinymac_header_t) },
				{ (char*)node->pending, node->pending_size },
		};

		if (node->flags & TINYMAC_ATTACH_FLAGS_SLEEPY) {
			/* Defer re-send to sleepy node */
			TRACE("(pending)\n");
			node->state = tinymacNodeState_SendPending;
		} else {
			/* Re-send immediately and schedule another timeout */
			TRACE("OUT (retry): %04X %02X %02X %02X %02X (%zu)\n",
					node->pending_header.flags,
					node->pending_header.net_id,
					node->pending_header.dest_addr,
					node->pending_header.src_addr,
					node->pending_header.seq,
					node->pending_size);
			tinymac_set_timer(&node->ack_timer, tinymac_ack_timeout, node, TINYMAC_MILLIS(TINYMAC_ACK_TIMEOUT));
			tinymac_phy_send(bufs, ARRAY_SIZE(bufs), 0);
		}
	} else {
		/* Invoke callback for failure */
		tinymac_cancel_timer(&node->validity_timer);
		if (node->send_cb) {
			node->send_cb(-1);
		}

		/* Give up - other end is unreachable */
		tinymac_deregister_node(node);
	}
}

/**********************/
/* Transmit functions */
/**********************/

/*! Wrapper around phy_send to handle power state transition */
static int tinymac_phy_send(phy_buf_t *bufs, unsigned int nbufs, uint8_t flags)
{
	int rc;

	rc = phy_send(bufs, nbufs, flags);
	if (tinymac_ctx->params.flags & TINYMAC_ATTACH_FLAGS_SLEEPY) {
		if (rc < 0) {
			/* Error - standby immediately */
			phy_standby();
		} else {
			/* OK - standby after listen period */
			phy_delayed_standby(TINYMAC_LISTEN_PERIOD_US);
		}
	}
	return rc;
}

static int tinymac_tx_packet(tinymac_node_t *dest, uint8_t flags_type, const char *buf, size_t size,
		uint16_t validity, tinymac_send_cb_t cb)
{
	tinymac_header_t hdr;
	phy_buf_t bufs[] = {
			{ (char*)&hdr, sizeof(hdr) },
			{ (char*)buf, size },
	};

	/* Check size against PHY MTU */
	if (size > TINYMAC_MAX_PAYLOAD || (size + sizeof(hdr)) > tinymac_ctx->phy_mtu) {
		ERROR("Packet too large\n");
		return -1;
	}

	/* Build header */
	hdr.flags = TINYMAC_FLAGS_VERSION | flags_type;
	hdr.net_id = tinymac_ctx->net_id;
	hdr.src_addr = tinymac_ctx->addr;
	hdr.dest_addr = dest ? dest->addr : TINYMAC_ADDR_BROADCAST;
	hdr.seq = ++tinymac_ctx->dseq;

	/* For unicast packets... */
	if (dest) {
		/* Only one message may be in flight at a time to a given node */
		if (dest->state != tinymacNodeState_Registered) {
			/* Destination is busy */
			ERROR("Node %02X is %s\n", dest->addr,
					dest->state == tinymacNodeState_Unregistered ? "not registered" : "busy");
			return -1;
		}

		if ((flags_type & TINYMAC_FLAGS_ACK_REQUEST) || (dest->flags & TINYMAC_ATTACH_FLAGS_SLEEPY)) {
			/* Copy packet for (re-)transmission */
			memcpy(dest->pending, buf, size);
			memcpy(&dest->pending_header, &hdr, sizeof(hdr));
			dest->pending_size = size;
			dest->send_cb = cb;
			dest->retries = TINYMAC_MAX_RETRIES;
		}

		/* If destination node is sleepy then defer the transmission (packet contents are
		 * copied above) */
		if (dest->flags & TINYMAC_ATTACH_FLAGS_SLEEPY) {
			TRACE("Pending transmission for node %02X\n", dest->addr);

			/* Start validity period timer.  The node must call in before this
			 * expires otherwise the send will fail */
			tinymac_set_timer(&dest->validity_timer, tinymac_validity_timeout, dest, TINYMAC_SECONDS(validity));
			dest->state = tinymacNodeState_SendPending;
			return 0;
		}

		/* We are going to send this packet immediately - if an ack is requested then
		 * start the timer now */
		if (flags_type & TINYMAC_FLAGS_ACK_REQUEST) {
			TRACE("Waiting for ack from node %02X\n", dest->addr);

			/* Start a timer for ack receipt and reset retry counter */
			tinymac_set_timer(&dest->ack_timer, tinymac_ack_timeout, dest, TINYMAC_MILLIS(TINYMAC_ACK_TIMEOUT));
			dest->state = tinymacNodeState_WaitAck;
		}
	}

	/* Send now */
	/* FIXME: Invoke callback on successful immediate send? */
	TRACE("OUT: %04X %02X %02X %02X %02X (%zu)\n", hdr.flags, hdr.net_id, hdr.dest_addr, hdr.src_addr, hdr.seq, size);
	return tinymac_phy_send(bufs, ARRAY_SIZE(bufs), 0);
}

static int tinymac_tx_pending(tinymac_node_t *node)
{
	tinymac_header_t hdr;
	phy_buf_t bufs[] = {
			{ (char*)&hdr, sizeof(hdr) },
			{ NULL, 0 },
	};

	/* Send pending packet if any */
	if (node->state == tinymacNodeState_SendPending) {
		node->state = tinymacNodeState_Registered;

		memcpy(&hdr, &node->pending_header, sizeof(hdr));
		bufs[1].buf = node->pending;
		bufs[1].size = node->pending_size;
		if (hdr.flags & TINYMAC_FLAGS_ACK_REQUEST) {
			/* Start a timer and prepare for a retransmission if we don't get an ACK */
			TRACE("Waiting for ack from node %02X\n", node->addr);
			node->state = tinymacNodeState_WaitAck;
			tinymac_set_timer(&node->ack_timer, tinymac_ack_timeout, node, TINYMAC_MILLIS(TINYMAC_ACK_TIMEOUT));
		}
		TRACE("PENDING OUT: %04X %02X %02X %02X %02X (%zu)\n", hdr.flags, hdr.net_id, hdr.dest_addr, hdr.src_addr, hdr.seq, node->pending_size);
		tinymac_phy_send(bufs, ARRAY_SIZE(bufs), 0);
		if (!(hdr.flags & TINYMAC_FLAGS_ACK_REQUEST)) {
			/* No ack - required so we assume success */
			tinymac_cancel_timer(&node->validity_timer);
			if (node->send_cb) {
				node->send_cb(0);
			}
		}
	}
	return 0;
}

static int tinymac_tx_ack(tinymac_node_t *node, uint8_t seq)
{
	tinymac_header_t hdr;
	phy_buf_t bufs[] = {
			{ (char*)&hdr, sizeof(hdr) },
			{ NULL, 0 },
	};
	int rc;

	/* Build header and send now */
	hdr.flags = TINYMAC_FLAGS_VERSION | tinymacType_Ack;
	hdr.net_id = tinymac_ctx->net_id;
	hdr.src_addr = tinymac_ctx->addr;
	hdr.dest_addr = node->addr;
	hdr.seq = seq;
	if (node->state == tinymacNodeState_SendPending) {
		hdr.flags |= TINYMAC_FLAGS_DATA_PENDING;
	}
	TRACE("ACK: %04X %02X %02X %02X %02X\n", hdr.flags, hdr.net_id, hdr.dest_addr, hdr.src_addr, hdr.seq);
	rc = tinymac_phy_send(bufs, 1, 0);
	if (rc < 0) {
		/* Send failed */
		return rc;
	}
	/* Send any pending packet immediately after sending the ack */
	return tinymac_tx_pending(node);
}

#if WITH_TINYMAC_COORDINATOR
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

		/* Append list of nodes with data pending */
		node = tinymac_ctx->nodes;
		for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
			if (node->state == tinymacNodeState_SendPending) {
				addrlist[npending++] = node->addr;
			}
		}
	}
	bufs[2].size = npending;

	/* Build beacon header */
	beacon.uuid = tinymac_ctx->params.uuid;
	beacon.timestamp = tinymac_ctx->slot;
	beacon.beacon_interval = TINYMAC_BEACON_INTERVAL_NO_BEACON;
	beacon.flags =
			(periodic ? TINYMAC_BEACON_FLAGS_SYNC : 0) |
			(tinymac_ctx->permit_attach ? TINYMAC_BEACON_FLAGS_PERMIT_ATTACH : 0);

	/* Build packet header and send */
	hdr.flags = TINYMAC_FLAGS_VERSION | tinymacType_Beacon;
	hdr.net_id = tinymac_ctx->net_id;
	hdr.src_addr = tinymac_ctx->addr;
	hdr.dest_addr = TINYMAC_ADDR_BROADCAST;
	hdr.seq = ++tinymac_ctx->bseq;
	TRACE("BEACON: %04X %02X %02X %02X %02X\n", hdr.flags, hdr.net_id, hdr.dest_addr, hdr.src_addr, hdr.seq);
	return tinymac_phy_send(bufs, ARRAY_SIZE(bufs), periodic ? PHY_FLAG_IMMEDIATE : 0);
}
#endif

/********************/
/* Receive handlers */
/********************/

static void tinymac_rx_beacon(tinymac_header_t *hdr, size_t size)
{
	tinymac_beacon_t *beacon = (tinymac_beacon_t*)hdr->payload;

	if (tinymac_ctx->params.coordinator) {
		/* Ignore beacons if we are a coordinator */
		return;
	}

#ifdef PRIX64
	TRACE("BEACON from %016" PRIX64 " %s\n", beacon->uuid, (beacon->flags & TINYMAC_BEACON_FLAGS_SYNC) ? "(SYNC)" : "(ADV)");
#else
	/* For platforms that don't support printing uint64s we just show the lower 32-bit word */
	TRACE("BEACON from %08" PRIX32 " %s\n", beacon->uuid, (beacon->flags & TINYMAC_BEACON_FLAGS_SYNC) ? "(SYNC)" : "(ADV)");
#endif

	/* Cancel beacon request timer */
	if (tinymac_ctx->state == tinymacClientState_BeaconRequest) {
		TRACE("Canceling beacon request timer\n");
		tinymac_cancel_timer(&tinymac_ctx->timer);
	}

	if (beacon->flags & TINYMAC_BEACON_FLAGS_SYNC) {
		/* FIXME: Sync clock */
	}

	switch (tinymac_ctx->state) {
	case tinymacClientState_Unregistered:
	case tinymacClientState_BeaconRequest:
		if (beacon->flags & TINYMAC_BEACON_FLAGS_PERMIT_ATTACH) {
			tinymac_registration_request_t attach;

			INFO("Attempting registration with %02X:%02X\n", hdr->net_id, hdr->src_addr);

			/* Temporarily bind with this network and send an attachment request */
			tinymac_ctx->state = tinymacClientState_Registering;
			tinymac_ctx->net_id = hdr->net_id;

			attach.uuid = tinymac_ctx->params.uuid;
			attach.flags = tinymac_ctx->params.flags;

			/* "register" this node as our coordinator */
			tinymac_ctx->coord.state = tinymacNodeState_Registered;
			tinymac_ctx->coord.addr = hdr->src_addr;
			tinymac_ctx->coord.uuid = beacon->uuid;
			tinymac_ctx->coord.flags = 0;
			tinymac_ctx->coord.last_heard = tinymac_ctx->tick_count;

			tinymac_tx_packet(&tinymac_ctx->coord, (uint16_t)tinymacType_RegistrationRequest,
					(const char*)&attach, sizeof(attach), 0, NULL);

			/* Start callback timer */
			tinymac_set_timer(&tinymac_ctx->timer, tinymac_request_timeout, NULL, TINYMAC_MILLIS(TINYMAC_REGISTRATION_TIMEOUT));
		}
		break;
	case tinymacClientState_Registered: {
		unsigned int n;

		/* Check if we are in the address list */
		for (n = 0; n < size - sizeof(tinymac_header_t) - sizeof(tinymac_beacon_t); n++) {
			if (beacon->address_list[n] == tinymac_ctx->addr) {
				INFO("Polling coordinator for pending data\n");
				tinymac_tx_packet(&tinymac_ctx->coord, (uint16_t)tinymacType_Poll, NULL, 0, 0, NULL);
				break;
			}
		}
	} break;
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

#ifdef PRIX64
	TRACE("REG RESPONSE for %016" PRIX64 " %02X\n", addr->uuid, addr->addr);
#else
	/* For platforms that don't support printing uint64s we just show the lower 32-bit word */
	TRACE("REG RESPONSE for %08" PRIX32 " %02X\n", addr->uuid, addr->addr);
#endif

	/* UUID must match our own */
	if (addr->uuid != tinymac_ctx->params.uuid) {
		if (hdr->dest_addr == TINYMAC_ADDR_BROADCAST) {
			/* This is a registration broadcast intended for another device - just ignore */
			return;
		} else {
			/* This was unicast to us - we have an address clash or we are being deregistered */
			ERROR("Address %02X clash (%u) - deregistering!\n", hdr->dest_addr);
			tinymac_ctx->state = tinymacClientState_Unregistered;
			tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
			tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
			return;
		}
	}

	/* Cancel registration timer */
	if (tinymac_ctx->state == tinymacClientState_Registering) {
		TRACE("Canceling registration timer\n");
		tinymac_cancel_timer(&tinymac_ctx->timer);
	}

	/* Update registration */
	if (addr->addr == TINYMAC_ADDR_UNASSIGNED || addr->status != tinymacRegistrationStatus_Success) {
		/* Detachment */
		ERROR("Network detachment, status = %u\n", addr->status);
		tinymac_ctx->state = tinymacClientState_Unregistered;
		tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
	} else if (tinymac_ctx->state == tinymacClientState_Registering) {
		/* Attachment - only if we are expecting it */
		INFO("Accepting new address %02X:%02X\n", hdr->net_id, addr->addr);
		tinymac_ctx->state = tinymacClientState_Registered;
		tinymac_ctx->addr = addr->addr;
		tinymac_ctx->net_id = hdr->net_id;
	}
}

#if WITH_TINYMAC_COORDINATOR
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
		INFO("Registered node %02X for %016" PRIX64 " with flags %04X\n", node->addr, attach->uuid, attach->flags);
		node->state = tinymacNodeState_Registered;
		node->uuid = attach->uuid;
		node->flags = attach->flags;
		node->last_heard = tinymac_ctx->tick_count;

		resp.uuid = attach->uuid;
		resp.addr = node->addr;
		resp.status = tinymacRegistrationStatus_Success;
	} else {
		ERROR("Network full\n");
		resp.uuid = attach->uuid;
		resp.addr = TINYMAC_ADDR_UNASSIGNED;
		resp.status = tinymacRegistrationStatus_NetworkFull;
	}

	/* Send response */
	tinymac_tx_packet(NULL, (uint16_t)tinymacType_RegistrationResponse,
			(const char*)&resp, sizeof(resp), 0, NULL);

	/* Invoke callback */
	if (tinymac_ctx->reg_cb) {
		tinymac_ctx->reg_cb((const tinymac_node_t*)node);
	}
	tinymac_dump_nodes();
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
		ERROR("Bad deregistration request from %016" PRIX64 "\n", detach->uuid);
		return;
	}

	/* Send response */
	resp.uuid = detach->uuid;
	resp.addr = TINYMAC_ADDR_UNASSIGNED;
	resp.status = tinymacRegistrationStatus_Success;
	tinymac_tx_packet(node, (uint16_t)tinymacType_RegistrationResponse,
			(const char*)&resp, sizeof(resp), 0, NULL);

	/* Free the slot */
	INFO("De-registered node %02X for %016" PRIX64 " reason %u\n", hdr->src_addr, node->uuid, detach->reason);
	tinymac_deregister_node(node);
}
#endif

static void tinymac_recv_cb(const char *buf, size_t size, int rssi)
{
	tinymac_header_t *hdr = (tinymac_header_t*)buf;
	tinymac_node_t *node = NULL;

	if (size < sizeof(tinymac_header_t)) {
		ERROR("Discarding short packet\n");
		return;
	}

	if (hdr->src_addr == tinymac_ctx->addr) {
		/* Quietly ignore loopbacks */
		return;
	}

	TRACE("IN: %04X %02X %02X %02X %02X (%zu)\n", hdr->flags, hdr->net_id, hdr->dest_addr, hdr->src_addr, hdr->seq, size - sizeof(tinymac_header_t));

	/*
	 * Accept packets addressed to the following destinations only:
	 * a) our own network and short address
	 * b) our own network and broadcast address
	 * c) wildcard network and broadcast address
	 * d) any network and broadcast address only if we are not registered
	 */
	if (!(	(hdr->net_id == tinymac_ctx->net_id &&
			(hdr->dest_addr == tinymac_ctx->addr || hdr->dest_addr == TINYMAC_ADDR_BROADCAST)) ||
			(hdr->net_id == TINYMAC_NETWORK_ANY && hdr->dest_addr == TINYMAC_ADDR_BROADCAST) ||
			(tinymac_ctx->net_id == TINYMAC_NETWORK_ANY && hdr->dest_addr == TINYMAC_ADDR_BROADCAST)) ) {
		TRACE("Ignoring packet with destination %02X:%02X\n", hdr->net_id, hdr->dest_addr);
		return;
	}

	/* Broadcasts to all networks, and packets with an unassigned source address
	 * are ignored except by a coordinator in
	 * "permit attach" state */
	if (hdr->net_id == TINYMAC_NETWORK_ANY || hdr->src_addr == TINYMAC_ADDR_UNASSIGNED) {
#if WITH_TINYMAC_COORDINATOR
		if (!tinymac_ctx->params.coordinator || !tinymac_ctx->permit_attach) {

#endif
			TRACE("Ignoring all-networks/unassigned source\n");
			return;
#if WITH_TINYMAC_COORDINATOR
		}
#endif
	}

	/* Sleepy nodes - Turn the receiver off unless data pending */
	if (tinymac_ctx->params.flags & TINYMAC_ATTACH_FLAGS_SLEEPY) {
		if (hdr->flags & TINYMAC_FLAGS_DATA_PENDING) {
			phy_delayed_standby(TINYMAC_LISTEN_PERIOD_US);
		} else {
			phy_standby();
		}
	}

	/* Source address, if assigned, must be known and live*/
	if (tinymac_ctx->addr != TINYMAC_ADDR_UNASSIGNED && hdr->src_addr != TINYMAC_ADDR_UNASSIGNED) {
		node = tinymac_get_node_by_addr(hdr->src_addr);
		if (node) {
			/* Update last heard */
			INFO("Updated last_heard for node %02X\n", hdr->src_addr);
			node->last_heard = tinymac_ctx->tick_count;
			node->rssi = (int8_t)rssi;

			if (hdr->flags & TINYMAC_FLAGS_ACK_REQUEST) {
				/* Acknowledgement requested */
				tinymac_tx_ack(node, hdr->seq);
			}
			/* Send any pending packet */
			tinymac_tx_pending(node);
		} else {
			/* Address not registered */
			ERROR("Ignoring unknown source node %02X\n", hdr->src_addr);
#if WITH_TINYMAC_COORDINATOR
			if (tinymac_ctx->params.coordinator) {
				/* Fake destination */
				tinymac_registration_response_t resp;
				tinymac_node_t dummy = {
					.addr = hdr->src_addr,
					.state = tinymacNodeState_Registered,
				};

				/* Force deregistration */
				ERROR("Forcing deregistration for unregistered address %02X\n", hdr->src_addr);
				resp.uuid = 0; /* Forces all nodes using this short address to re-register */
				resp.addr = TINYMAC_ADDR_UNASSIGNED;
				resp.status = tinymacRegistrationStatus_AddressInvalid;
				tinymac_tx_packet(&dummy, (uint16_t)tinymacType_RegistrationResponse,
					(const char*)&resp, sizeof(resp), 0, NULL);
			}
#endif
			return;
		}
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

		if (node && node->state == tinymacNodeState_WaitAck) {
			if (hdr->seq == node->pending_header.seq) {
				/* Ack ok, cancel timers */
				TRACE("Valid ack received from %02X for %02X\n", hdr->src_addr, hdr->seq);
				node->state = tinymacNodeState_Registered;
				tinymac_cancel_timer(&node->ack_timer);
				tinymac_cancel_timer(&node->validity_timer);

				/* Callback success */
				if (node->send_cb) {
					node->send_cb(0);
				}
			} else {
				ERROR("Bad ack received from %02X\n", hdr->src_addr);
			}
		} else {
			ERROR("Unexpected ACK\n");
		}
		break;
	case tinymacType_Poll:
		/* This just solicits an ack, which happens above */
		TRACE("POLL\n");
		break;
	case tinymacType_Data:
		/* Forward to upper layer */
		TRACE("RX DATA\n");
		if (tinymac_ctx->rx_cb) {
			tinymac_ctx->rx_cb((const tinymac_node_t*)node, hdr->payload, size - sizeof(tinymac_header_t));
		}
		break;

#if WITH_TINYMAC_COORDINATOR
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
#endif
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

/********************/
/* Public functions */
/********************/

int tinymac_init(const tinymac_params_t *params)
{
	memcpy(&tinymac_ctx->params, params, sizeof(tinymac_params_t));

#if WITH_TINYMAC_COORDINATOR
	/* Clear registrations and assign addresses to slots */
	{
		int n;
		for (n = 0; n < TINYMAC_MAX_NODES; n++) {
			tinymac_ctx->nodes[n].state = tinymacNodeState_Unregistered;
			tinymac_ctx->nodes[n].addr = n + 1;
		}
	}
	tinymac_ctx->bseq = rand();
	tinymac_ctx->permit_attach = FALSE;
#endif
	tinymac_ctx->dseq = rand();
	tinymac_ctx->coord.state = tinymacNodeState_Unregistered;

#if WITH_TINYMAC_COORDINATOR
	if (tinymac_ctx->params.coordinator) {
		tinymac_ctx->state = tinymacClientState_Registered; /* FIXME: Needed? */
		tinymac_ctx->net_id = rand();
		tinymac_ctx->addr = 0x00;
	} else
#endif
	{
		tinymac_ctx->state = tinymacClientState_Unregistered;
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
		tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
	}

	/* Register PHY receive callback */
	phy_register_recv_cb(tinymac_recv_cb);
	tinymac_ctx->phy_mtu = phy_get_mtu();

	if (!(tinymac_ctx->params.flags & TINYMAC_ATTACH_FLAGS_SLEEPY)) {
		/* Receiver enabled full-time */
		phy_listen();
	}

	return 0;
}

void tinymac_register_recv_cb(tinymac_recv_cb_t cb)
{
	tinymac_ctx->rx_cb = cb;
}

void tinymac_tick_handler(void *arg)
{
#if WITH_TINYMAC_COORDINATOR
	if (tinymac_ctx->params.coordinator) {
		unsigned int n;
		tinymac_node_t *node;
		uint32_t now = tinymac_ctx->tick_count;

		/* This is called once per beacon slot (250 ms) - check if a beacon is due in this
		 * slot and increment the counter */
		if (((++tinymac_ctx->slot) & ((1 << tinymac_ctx->params.beacon_interval) - 1)) == tinymac_ctx->params.beacon_offset) {
			/* Beacon due */
			tinymac_tx_beacon(TRUE);
			TRACE("sync beacon sent\n");
		}

		/* Do per-node ops */
		node = tinymac_ctx->nodes;
		for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
			if (node->state == tinymacNodeState_Registered) {
				/* Check for heartbeat expiry */
				uint32_t heartbeat = 1 << (node->flags & TINYMAC_ATTACH_HEARTBEAT_MASK);
				if ((int32_t)(node->last_heard +
						TINYMAC_SECONDS(heartbeat + TINYMAC_HEARTBEAT_GRACE) - now) <= 0) {
					/* Node has gone away */
					/* NOTE: We could ping if this isn't a sleeping node but there doesn't seem
					 * much point - the node should have called in by now anyway */
					INFO("Node %02X heartbeat has expired\n", node->addr);
					tinymac_deregister_node(node);
				}
			}

			/* Despatch deferred operations */
			tinymac_despatch_timer(&node->ack_timer);
			tinymac_despatch_timer(&node->validity_timer);
		}
	} else
#endif
	{
		/* Unregistered clients may request a beacon */
		if (tinymac_ctx->state == tinymacClientState_Unregistered) {
			tinymac_ctx->state = tinymacClientState_BeaconRequest;
			tinymac_tx_packet(NULL, (uint16_t)tinymacType_BeaconRequest, NULL, 0, 0, NULL);

			/* Set a timer for beacon request timeout */
			tinymac_set_timer(&tinymac_ctx->timer, tinymac_request_timeout, NULL, TINYMAC_SECONDS(TINYMAC_BEACON_REQUEST_TIMEOUT));
		}

		/* Despatch deferred operations */
		tinymac_despatch_timer(&tinymac_ctx->coord.ack_timer);
		tinymac_despatch_timer(&tinymac_ctx->coord.validity_timer);
		tinymac_despatch_timer(&tinymac_ctx->timer);
	}

	tinymac_ctx->tick_count++;
}

int tinymac_send(uint8_t dest, const char *buf, size_t size,
		uint16_t validity, uint8_t flags,
		tinymac_send_cb_t cb)
{
	tinymac_node_t *node;

	node = tinymac_get_node_by_addr(dest);
	if (!node) {
		ERROR("Node %02X not registered\n", dest);
		return -1;
	}

	if (validity == 0) {
		/* Default validity period to the heartbeat interval for the destination */
		validity = 1 << (node->flags & TINYMAC_ATTACH_HEARTBEAT_MASK);
	}

	return tinymac_tx_packet(node, (flags & TINYMAC_FLAGS_ACK_REQUEST) | (uint16_t)tinymacType_Data,
			buf, size, validity, cb);
}

int tinymac_is_registered(void)
{
	return (tinymac_ctx->state == tinymacClientState_Registered) ? 1 : 0;
}

#if WITH_TINYMAC_COORDINATOR
void tinymac_permit_attach(boolean_t permit)
{
	TRACE("permit_attach=%d\n", permit);

	tinymac_ctx->permit_attach = permit;
}

void tinymac_register_reg_cb(tinymac_reg_cb_t cb)
{
	tinymac_ctx->reg_cb = cb;
}

void tinymac_register_dereg_cb(tinymac_reg_cb_t cb)
{
	tinymac_ctx->dereg_cb = cb;
}

void tinymac_dump_nodes(void)
{
	tinymac_node_t *node = tinymac_ctx->nodes;
	unsigned int n;

	printf("Network %02X\n", tinymac_ctx->net_id);
	printf("Permit attach: %s\n", tinymac_ctx->permit_attach ? "Yes" : "No");
	printf("\nKnown nodes:\n\n");

	printf("Addr  UUID              State             RSSI  Last Heard Ago  Heartbeat  Sleepy\n");
	printf("---------------------------------------------------------------------------------\n");
	for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
		if (node->uuid) {
			printf("%02X    %016" PRIX64 "  %16s  %4d  %14u  %9u  %s\n",
					node->addr, node->uuid, tinymac_node_states[node->state],
					node->rssi,
					(tinymac_ctx->tick_count - node->last_heard) * TINYMAC_TICK_MS / 1000,
					(1 << (node->flags & TINYMAC_ATTACH_HEARTBEAT_MASK)),
					(node->flags & TINYMAC_ATTACH_FLAGS_SLEEPY) ? "Yes" : "");
		}
	}

}
#endif

