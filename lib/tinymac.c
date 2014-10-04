/*
 * mac-tinyhan.c
 *
 *  Created on: 17 Aug 2014
 *      Author: mike
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "common.h"
#include "phy.h"
#include "timer.h"
#include "tinymac.h"

typedef enum {
	tinymacClientState_Unregistered = 0,
	tinymacClientState_BeaconRequest,
	tinymacClientState_Registering,
	tinymacClientState_Registered,
} tinymac_client_state_t;

typedef enum {
	tinymacNodeState_Unregistered = 0,
	tinymacNodeState_Registered,
	tinymacNodeState_SendPending,
	tinymacNodeState_WaitAck,
} tinymac_node_state_t;

static const char *tinymac_node_states[] = {
		"Unregistered",
		"Registered",
		"SendPending",
		"WaitAck",
};

/*! Maximum number of nodes that this node can be aware of (this is the maximum size of the
 * network if this node is a coordinator) */
#define TINYMAC_MAX_NODES				32
/*! Maximum payload length (limited further by the PHY MTU) */
#define TINYMAC_MAX_PAYLOAD				128
/*! Maximum number of retries when transmitting a packet with ack request set */
#define TINYMAC_MAX_RETRIES				3
/*! Time to wait for an acknowledgement response (ms) */
#define TINYMAC_ACK_TIMEOUT				250
/*! Time to wait for a sleeping node to call in for a pending packet before giving up (s)
 * FIXME: This should really be releated to the beacon interval in some way */
#define TINYMAC_SEND_TIMEOUT			5
/*! Time for an unregistered node to wait between beacon request transmissions (seconds) */
#define TINYMAC_BEACON_REQUEST_TIMEOUT	10
/*! Time to wait for a registration request to be answered (ms) */
#define TINYMAC_REGISTRATION_TIMEOUT	1000
/*! Time after which we ping a node which hasn't been heard (s) */
#define TINYMAC_PING_DELAY				10

typedef struct {
	tinymac_node_state_t	state;
	uint8_t					addr;			/*< Assigned short address */
	uint64_t				uuid;			/*< Unit identifier */
	timer_handle_t			timer;			/*< Timer for ack timeout, etc. */
	uint32_t				last_heard;		/*< Last heard time (ticks) */
	uint16_t				flags;			/*< Node flags (from registration) */
	uint8_t					retries;		/*< Number of tx tries remaining */

	tinymac_header_t		pending_header;	/*< Header for pending packet */
	size_t					pending_size;	/*< Size of pending outbound payload */
	char					pending[TINYMAC_MAX_PAYLOAD];	/*< Pending outbound packet payload */
} tinymac_node_t;

typedef struct {
	/**********/
	/* Common */
	/**********/

	tinymac_params_t		params;			/*< MAC configuration */
	unsigned int			phy_mtu;		/*< MTU from PHY driver */
	tinymac_recv_cb_t		rx_cb;			/*< MAC data receive callback */

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
	timer_handle_t			timer;			/*< Timer for registration/beacon requests */

#if WITH_TINYMAC_COORDINATOR
	/***************/
	/* Coordinator */
	/***************/

	uint8_t					bseq;			/*< Current outbound beacon serial number (coordinator) */
	tinymac_node_t			nodes[TINYMAC_MAX_NODES];	/*< Coordinator: List of known nodes */
	boolean_t				permit_attach;	/*< Whether or not we are accepting registration requests */
#endif
} tinymac_t;

static tinymac_t tinymac_ctx_;
static tinymac_t *tinymac_ctx = &tinymac_ctx_;

#if WITH_TINYMAC_COORDINATOR
static void tinymac_dump_nodes(void)
{
	tinymac_node_t *node = tinymac_ctx->nodes;
	unsigned int n;

	printf("Network %02X\n", tinymac_ctx->net_id);
	printf("Permit attach: %s\n", tinymac_ctx->permit_attach ? "Yes" : "No");
	printf("\nKnown nodes:\n\n");

	printf("******************************************************************\n");
	printf("| Addr | UUID             | Flags | State           | Last Heard |\n");
	printf("******************************************************************\n");
	for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
		if (node->uuid) {
			printf("|  %02X  | %016" PRIX64 " | %02X    | %15s | %10u |\n",
				node->addr, node->uuid, node->flags, tinymac_node_states[node->state], node->last_heard);
		}
	}
	printf("******************************************************************\n\n");

}

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

static void tinymac_node_lost(tinymac_node_t *node)
{
	ERROR("Node %02X has gone away\n", node->addr);

	node->state = tinymacNodeState_Unregistered;
	if (node == &tinymac_ctx->coord) {
		/* The node was our coordinator, so we are now unregistered */
		tinymac_ctx->state = tinymacClientState_Unregistered;
		tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
	}
}

static void tinymac_timeout_timer(void *arg)
{
	if (tinymac_ctx->state == tinymacClientState_BeaconRequest || tinymac_ctx->state == tinymacClientState_Registering) {
		/* Coordinator has gone away */
		TRACE("Beacon request/registration timeout\n");
		tinymac_node_lost(&tinymac_ctx->coord);
	} else {
		TRACE("Timeout callback skipped\n");
	}
}

static void tinymac_send_timeout(void *arg)
{
	tinymac_node_t *node = (tinymac_node_t*)arg;

	/* If the destination node doesn't call in for the pending packet then we
	 * assume it has gone away (since this pending packet could have been a ping)
	 * The send timeout must be sufficiently long that it includes several beacon
	 * intervals to allow for the possibility of a client missing the beacon */
	ERROR("Timeout for pending send to node %02X - node has gone away\n", node->addr);
	node->state = tinymacNodeState_Unregistered;
}

/* FIXME: Handle retries to sleeping nodes (see flow chart) */
static void tinymac_ack_timeout(void *arg)
{
	tinymac_node_t *node = (tinymac_node_t*)arg;

	INFO("Ack timeout for node %02X\n", node->addr);
	if (node->retries--) {
		phy_buf_t bufs[] = {
				{ (char*)&node->pending_header, sizeof(tinymac_header_t) },
				{ (char*)node->pending, node->pending_size },
		};

		/* Re-send and schedule another timer */
		TRACE("OUT (retry): %04X %02X %02X %02X %02X (%zu)\n",
				node->pending_header.flags,
				node->pending_header.net_id,
				node->pending_header.dest_addr,
				node->pending_header.src_addr,
				node->pending_header.seq,
				node->pending_size);
		node->timer = timer_request_callback(tinymac_ack_timeout, node, TIMER_MILLIS(TINYMAC_ACK_TIMEOUT), TIMER_ONE_SHOT);
		phy_send(bufs, ARRAY_SIZE(bufs), 0);
	} else {
		/* Give up */
		tinymac_node_lost(node);
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

		/* If destination node is sleepy then defer the transmission (packet contents are
		 * copied) */
		if (dest->flags & TINYMAC_ATTACH_FLAGS_SLEEPY) {
			/* Destination is a sleepy node */
			TRACE("Pending transmission for node %02X\n", dest->addr);
			memcpy(dest->pending, buf, size);
			memcpy(&dest->pending_header, &hdr, sizeof(hdr));
			dest->pending_size = size;
			dest->state = tinymacNodeState_SendPending;

			/* Start a timer for send timeout - the recipient must call in and ask for
			 * pending packets before this expires */
			dest->timer = timer_request_callback(tinymac_send_timeout, dest, TIMER_SECONDS(TINYMAC_SEND_TIMEOUT), TIMER_ONE_SHOT);
			return 0;
		}
	}

	if (dest && (flags_type & TINYMAC_FLAGS_ACK_REQUEST)) {
		/* Unicast packet and an ack was requested - copy the packet in case we need
		 * to retransmit */
		TRACE("Waiting for ack from node %02X\n", dest->addr);
		memcpy(dest->pending, buf, size);
		memcpy(&dest->pending_header, &hdr, sizeof(hdr));
		dest->pending_size = size;
		dest->state = tinymacNodeState_WaitAck;

		/* Start a timer for ack receipt and reset retry counter */
		dest->retries = TINYMAC_MAX_RETRIES;
		dest->timer = timer_request_callback(tinymac_ack_timeout, dest, TIMER_MILLIS(TINYMAC_ACK_TIMEOUT), TIMER_ONE_SHOT);
	}

	/* Send now */
	TRACE("OUT: %04X %02X %02X %02X %02X (%zu)\n", hdr.flags, hdr.net_id, hdr.dest_addr, hdr.src_addr, hdr.seq, size);
	return phy_send(bufs, ARRAY_SIZE(bufs), 0);
}

static int tinymac_tx_ack(tinymac_node_t *node, uint8_t seq)
{
	tinymac_header_t hdr;
	phy_buf_t bufs[] = {
			{ (char*)&hdr, sizeof(hdr) },
			{ NULL, 0 },
	};

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
	/* return */ phy_send(bufs, 1, 0);

	/* Send pending packet if any */
	if (node->state == tinymacNodeState_SendPending) {
		timer_cancel_callback(node->timer);
		node->state = tinymacNodeState_Registered;

		memcpy(&hdr, &node->pending_header, sizeof(hdr));
		bufs[1].buf = node->pending;
		bufs[1].size = node->pending_size;
		if (hdr.flags & TINYMAC_FLAGS_ACK_REQUEST) {
			/* Start a timer and prepare for a retransmission if we don't get an ACK */
			TRACE("Waiting for ack from node %02X\n", node->addr);
			node->retries = TINYMAC_MAX_RETRIES;
			node->state = tinymacNodeState_WaitAck;
			node->timer = timer_request_callback(tinymac_ack_timeout, node, TIMER_MILLIS(TINYMAC_ACK_TIMEOUT), TIMER_ONE_SHOT);
		}
		TRACE("PENDING OUT: %04X %02X %02X %02X %02X (%zu)\n", hdr.flags, hdr.net_id, hdr.dest_addr, hdr.src_addr, hdr.seq, node->pending_size);
		return phy_send(bufs, ARRAY_SIZE(bufs), 0);
	}
	return 0;
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

		/* Build list of nodes with data pending */
		node = tinymac_ctx->nodes;
		for (n = 0; n < TINYMAC_MAX_NODES; n++, node++) {
			if (node->state == tinymacNodeState_SendPending) {
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
	return phy_send(bufs, ARRAY_SIZE(bufs), periodic ? PHY_FLAG_IMMEDIATE : 0);
}
#endif

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
		timer_cancel_callback(tinymac_ctx->timer);
	}

	if (beacon->flags & TINYMAC_BEACON_FLAGS_SYNC) {
		/* FIXME: Sync clock */
	}

	switch (tinymac_ctx->state) {
	case tinymacClientState_Unregistered:
	case tinymacClientState_BeaconRequest:
		if (beacon->flags & TINYMAC_BEACON_FLAGS_PERMIT_ATTACH) {
			tinymac_registration_request_t attach;

			/* Temporarily bind with this network and send an attachment request */
			tinymac_ctx->net_id = hdr->net_id;
			tinymac_ctx->state = tinymacClientState_Registering;

			attach.uuid = tinymac_ctx->params.uuid;
			attach.flags = tinymac_ctx->params.flags;

			/* "register" this node as our coordinator */
			tinymac_ctx->coord.state = tinymacNodeState_Registered;
			tinymac_ctx->coord.addr = hdr->src_addr;
			tinymac_ctx->coord.uuid = beacon->uuid;
			tinymac_ctx->coord.flags = 0;
			tinymac_ctx->coord.last_heard = timer_get_tick_count();

			tinymac_tx_packet(&tinymac_ctx->coord, (uint16_t)tinymacType_RegistrationRequest,
					(const char*)&attach, sizeof(attach));

			/* Start callback timer */
			tinymac_ctx->timer = timer_request_callback(tinymac_timeout_timer, NULL, TIMER_MILLIS(TINYMAC_REGISTRATION_TIMEOUT), TIMER_ONE_SHOT);
		}
		break;
	case tinymacClientState_Registered: {
		unsigned int n;

		/* Check if we are in the address list */
		for (n = 0; n < size - sizeof(tinymac_header_t) - sizeof(tinymac_beacon_t); n++) {
			if (beacon->address_list[n] == tinymac_ctx->addr) {
				INFO("Pinging coordinator for pending data\n");
				tinymac_tx_packet(&tinymac_ctx->coord, TINYMAC_FLAGS_ACK_REQUEST | (uint16_t)tinymacType_Ping, NULL, 0);
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

	/* Check and ignore if UUID doesn't match our own */
	if (addr->uuid != tinymac_ctx->params.uuid) {
		return;
	}

	/* Cancel registration timer */
	if (tinymac_ctx->state == tinymacClientState_Registering) {
		TRACE("Canceling registration timer\n");
		timer_cancel_callback(tinymac_ctx->timer);
	}

	if (addr->status != tinymacRegistrationStatus_Success) {
		/* Coordinator rejected */
		ERROR("registration error %d\n", addr->status);
		tinymac_ctx->state = tinymacClientState_Unregistered;
		return;
	}

	/* Update registration */
	if (addr->addr == TINYMAC_ADDR_UNASSIGNED) {
		/* Detachment */
		tinymac_ctx->state = tinymacClientState_Unregistered;
		tinymac_ctx->addr = TINYMAC_ADDR_UNASSIGNED;
		tinymac_ctx->net_id = TINYMAC_NETWORK_ANY;
	} else if (tinymac_ctx->state == tinymacClientState_Registering) {
		/* Attachment - only if we are expecting it */
		tinymac_ctx->state = tinymacClientState_Registered;
		tinymac_ctx->addr = addr->addr;
		tinymac_ctx->net_id = hdr->net_id;
	}

	INFO("New address %02X %02X\n", hdr->net_id, addr->addr);
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
		ERROR("Bad deregistration request from %016" PRIX64 "\n", detach->uuid);
		return;
	}

	/* Free the slot */
	INFO("De-registered node %02X for %016" PRIX64 " reason %u\n", hdr->src_addr, node->uuid, detach->reason);
	node->state = tinymacNodeState_Unregistered;

	tinymac_dump_nodes();

	/* Send response */
	resp.uuid = detach->uuid;
	resp.addr = TINYMAC_ADDR_UNASSIGNED;
	resp.status = tinymacRegistrationStatus_Success;
	tinymac_tx_packet(node, (uint16_t)tinymacType_RegistrationResponse,
			(const char*)&resp, sizeof(resp));
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

	if (tinymac_ctx->net_id != TINYMAC_NETWORK_ANY &&
			hdr->net_id != tinymac_ctx->net_id && hdr->net_id != TINYMAC_NETWORK_ANY) {
		TRACE("Not my network\n");
		return;
	}

	if (hdr->dest_addr != tinymac_ctx->addr && hdr->dest_addr != TINYMAC_ADDR_BROADCAST) {
		TRACE("Not my addr\n");
		return;
	}

	/* For unicast packets the source node must be known.  For broadcast packets it may be
	 * (in which case the last_heard timestamp will be updated), but it is not mandatory */
	if (hdr->net_id != TINYMAC_NETWORK_ANY && hdr->src_addr != TINYMAC_ADDR_UNASSIGNED) {
		/* See if we know the source node, update timestamp if so */
		node = tinymac_get_node_by_addr(hdr->src_addr);
		if (node) {
			INFO("Updated last_heard for node %02X\n", hdr->src_addr);
			node->last_heard = timer_get_tick_count();
		}

		/* Handle special operations (ack/data pending) for unicast packets */
		if (node && hdr->dest_addr != TINYMAC_ADDR_BROADCAST) {
			if (hdr->flags & TINYMAC_FLAGS_ACK_REQUEST) {
				tinymac_tx_ack(node, hdr->seq);
			}

			/* FIXME: Check data pending and leave rx on for a while
			 * (not applicable for coordinator which is always-on rx anyway) */
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
				/* Ack ok, cancel timer */
				TRACE("Valid ack received from %02X for %02X\n", hdr->src_addr, hdr->seq);
				node->state = tinymacNodeState_Registered;
				timer_cancel_callback(node->timer);
			} else {
				ERROR("Bad ack received from %02X\n", hdr->src_addr);
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
	unsigned int n;

	memcpy(&tinymac_ctx->params, params, sizeof(tinymac_params_t));

#if WITH_TINYMAC_COORDINATOR
	/* Clear registrations and assign addresses */
	for (n = 0; n < TINYMAC_MAX_NODES; n++) {
		tinymac_ctx->nodes[n].state = tinymacNodeState_Unregistered;
		tinymac_ctx->nodes[n].addr = n + 1;
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

#if WITH_TINYMAC_COORDINATOR
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
			if (node->state == tinymacNodeState_Registered &&
					(int32_t)((node->last_heard + TIMER_SECONDS(TINYMAC_PING_DELAY)) - now) <= 0) {
				/* Send a ping */
				INFO("Pinging node %02X\n", node->addr);
				tinymac_tx_packet(node, TINYMAC_FLAGS_ACK_REQUEST | (uint16_t)tinymacType_Ping, NULL, 0);
			}
		}
	} else
#endif
	{
		/* Unregistered clients may request a beacon */
		if (tinymac_ctx->state == tinymacClientState_Unregistered) {
			tinymac_ctx->state = tinymacClientState_BeaconRequest;
			tinymac_tx_packet(NULL, (uint16_t)tinymacType_BeaconRequest, NULL, 0);

			/* Set a timer for beacon request timeout */
			tinymac_ctx->timer = timer_request_callback(tinymac_timeout_timer, NULL, TIMER_SECONDS(TINYMAC_BEACON_REQUEST_TIMEOUT), TIMER_ONE_SHOT);
		}

		/* Search for lost coordinator */
		if (tinymac_ctx->coord.state == tinymacNodeState_Registered &&
				(int32_t)((tinymac_ctx->coord.last_heard + TIMER_SECONDS(TINYMAC_PING_DELAY)) - now) <= 0) {
			/* Send a ping */
			INFO("Pinging coordinator\n");
			tinymac_tx_packet(&tinymac_ctx->coord, TINYMAC_FLAGS_ACK_REQUEST | (uint16_t)tinymacType_Ping, NULL, 0);
		}
	}

}

#if WITH_TINYMAC_COORDINATOR
void tinymac_permit_attach(boolean_t permit)
{
	TRACE("permit_attach=%d\n", permit);

	tinymac_ctx->permit_attach = permit;
}
#endif

int tinymac_send(uint8_t dest, const char *buf, size_t size)
{
	tinymac_node_t *node;

	node = tinymac_get_node_by_addr(dest);
	if (!node) {
		ERROR("Node %02X not registered\n", dest);
		return -1;
	}

	return tinymac_tx_packet(node, (uint16_t)tinymacType_Data, buf, size);
}
