/*!
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
 * tinymac.h
 *
 * TinyHAN MAC layer
 *
 */

#ifndef MAC_H_
#define MAC_H_

#include <stdint.h>
#include "common.h"

#define TINYMAC_MAX_UUID_STRING				16

#define TINYMAC_ADDR_BROADCAST				0xFF
#define TINYMAC_ADDR_UNASSIGNED				0xFF
#define TINYMAC_NETWORK_ANY					0xFF

/*! Tick interval in ms */
#define TINYMAC_TICK_MS						250

typedef struct {
	uint16_t		flags;
	uint8_t			net_id;
	uint8_t			dest_addr;
	uint8_t			src_addr;
	uint8_t			seq;
	char			payload[0];
} PACKED tinymac_header_t;

#define TINYMAC_FLAGS_VERSION				(0 << 13)
#define TINYMAC_FLAGS_VERSION_SHIFT			13
#define TINYMAC_FLAGS_VERSION_MASK			(7 << 13)

#define TINYMAC_FLAGS_DATA_PENDING			(1 << 7)
#define TINYMAC_FLAGS_ACK_REQUEST			(1 << 6)

#define TINYMAC_FLAGS_TYPE_SHIFT			0
#define TINYMAC_FLAGS_TYPE_MASK				(31 << 0)

typedef enum {
	tinymacType_Beacon = 0,
	tinymacType_BeaconRequest,
	tinymacType_Poll,
	tinymacType_Ack,
	tinymacType_RegistrationRequest,
	tinymacType_DeregistrationRequest,
	tinymacType_RegistrationResponse,
	tinymacType_Reserved7,
	tinymacType_Reserved8,
	tinymacType_Reserved9,
	tinymacType_Reserved10,
	tinymacType_Reserved11,
	tinymacType_Reserved12,
	tinymacType_Reserved13,
	tinymacType_Reserved14,
	tinymacType_Reserved15,
	tinymacType_Data,
	tinymacType_Reserved17,
	tinymacType_Reserved18,
	tinymacType_Reserved19,
	tinymacType_Reserved20,
	tinymacType_Reserved21,
	tinymacType_Reserved22,
	tinymacType_Reserved23,
	tinymacType_Reserved24,
	tinymacType_Reserved25,
	tinymacType_Reserved26,
	tinymacType_Reserved27,
	tinymacType_Reserved28,
	tinymacType_Reserved29,
	tinymacType_Reserved30,
	tinymacType_Reserved31,
} tinymac_packet_type_t;

typedef struct {
	uint64_t		uuid;
	uint16_t		timestamp;
	uint8_t			flags;
	uint8_t			beacon_interval;
	uint8_t			address_list[0];
} PACKED tinymac_beacon_t;

#define TINYMAC_BEACON_FLAGS_FSECONDS_SHIFT		6
#define TINYMAC_BEACON_FLAGS_FSECONDS_MASK		(3 << 6)

#define TINYMAC_BEACON_FLAGS_PERMIT_ATTACH		(1 << 1)
#define TINYMAC_BEACON_FLAGS_SYNC				(1 << 0)

#define TINYMAC_BEACON_INTERVAL_OFFSET_SHIFT	4
#define TINYMAC_BEACON_INTERVAL_OFFSET_MASK		(15 << 4)
#define TINYMAC_BEACON_INTERVAL_INTERVAL_SHIFT	0
#define TINYMAC_BEACON_INTERVAL_INTERVAL_MASK	(15 << 0)

#define TINYMAC_BEACON_INTERVAL_NO_BEACON		0x0f

typedef struct {
	uint64_t		uuid;
	uint16_t		flags;
} PACKED tinymac_registration_request_t;

#define TINYMAC_ATTACH_FLAGS_SLEEPY				(1 << 4)
#define TINYMAC_ATTACH_HEARTBEAT_SHIFT			0
#define TINYMAC_ATTACH_HEARTBEAT_MASK			(15 << 0)

typedef struct {
	uint64_t		uuid;
	uint8_t			reason;
} PACKED tinymac_deregistration_request_t;

typedef enum {
	tinymacDeregReason_User = 0,
	tinymacDeregReason_PowerDown,
} tinymac_dereg_reason_t;

typedef struct {
	uint64_t		uuid;
	uint8_t			addr;
	uint8_t			status;
} PACKED tinymac_registration_response_t;

typedef enum {
	tinymacRegistrationStatus_Success = 0,
	tinymacRegistrationStatus_AccessDenied,
	tinymacRegistrationStatus_NetworkFull,
	tinymacRegistrationStatus_Shutdown,
	tinymacRegistrationStatus_Admin,
	tinymacRegistrationStatus_AddressInvalid,
} tinymac_registration_status_t;

/*******************************/
/* End of protocol definitions */
/*******************************/

/*! Maximum number of nodes that this node can be aware of (this is the maximum size of the
 * network if this node is a coordinator) */
#define TINYMAC_MAX_NODES				32
/*! Maximum payload length (limited further by the PHY MTU) */
#define TINYMAC_MAX_PAYLOAD				128
/*! Maximum number of retries when transmitting a packet with ack request set */
#define TINYMAC_MAX_RETRIES				3
/*! Time to wait for an acknowledgement response (ms) */
#define TINYMAC_ACK_TIMEOUT				250
/*! Time for an unregistered node to wait between beacon request transmissions (seconds) */
#define TINYMAC_BEACON_REQUEST_TIMEOUT	10
/*! Time to wait for a registration request to be answered (ms) */
#define TINYMAC_REGISTRATION_TIMEOUT	1000
/*! Coordinator grace period to allow after heartbeat expiry before assuming a client has gone (seconds) */
#define TINYMAC_HEARTBEAT_GRACE			2
/*! Time a sleeping node should listen after transmitting or receiving a packet with
 * data pending bit set (microseconds) */
#define TINYMAC_LISTEN_PERIOD_US		10000

#define TINYMAC_MILLIS(ms)				((ms) / TINYMAC_TICK_MS)
#define TINYMAC_SECONDS(s)				((s) * 1000 / TINYMAC_TICK_MS)

typedef void (*tinymac_timer_cb_t)(void *arg);
typedef void (*tinymac_send_cb_t)(int result);

typedef enum {
	tinymacNodeState_Unregistered = 0,
	tinymacNodeState_Registered,
	tinymacNodeState_SendPending,
	tinymacNodeState_WaitAck,
} tinymac_node_state_t;

typedef struct {
	tinymac_timer_cb_t		callback;
	void					*arg;
	uint32_t				expiry;
} tinymac_timer_t;

typedef struct {
	uint64_t				uuid;			/*< Unit identifier */
	uint32_t				last_heard;		/*< Last heard time (ticks) */
	uint16_t				flags;			/*< Node flags (from registration) */
	uint8_t					addr;			/*< Assigned short address */
	int8_t					rssi;			/*< Last signal strength if known (dBm), or 0 */
	tinymac_node_state_t	state;			/*< Current node state */

	/* Private elements follow - don't look! */

	tinymac_header_t		pending_header;	/*< Header for pending packet */
	size_t					pending_size;	/*< Size of pending outbound payload */
	char					pending[TINYMAC_MAX_PAYLOAD];	/*< Pending outbound packet payload */
	tinymac_send_cb_t		send_cb;		/*< Callback invoked when pending packet sent/expires */

	tinymac_timer_t			ack_timer;		/*< Timer for ack timeout */
	tinymac_timer_t			validity_timer;	/*< Validity timeout for deferred sends */
	uint8_t					retries;		/*< Number of tx tries remaining */
} tinymac_node_t;

typedef struct {
	uint64_t		uuid;
	boolean_t		coordinator;
	uint16_t		flags;					/*< Node flags, e.g. SLEEPY */
	uint8_t			beacon_interval;		/*< 250 ms * 2^n, or TINYMAC_BEACON_INTERVAL_NO_BEACON */
	uint8_t			beacon_offset;
} tinymac_params_t;

typedef void (*tinymac_recv_cb_t)(const tinymac_node_t *node, const char *payload, size_t size);
typedef void (*tinymac_reg_cb_t)(const tinymac_node_t *node);

int tinymac_init(const tinymac_params_t *params);

/*!
 * Register callback to be invoked when a received packet is to be passed
 * to the upper layer
 * \param cb		Pointer to callback to be invoked
 */
void tinymac_register_recv_cb(tinymac_recv_cb_t cb);

/*!
 * Called at 250 ms intervals for generation of beacon frames and
 * execution of periodic state changes.  This may be called directly from
 * interrupt context if no OS is in use.
 *
 * In a node implementation the timer used to generate these calls must be
 * synced with incoming beacons such that the call occurs just prior to
 * the expected beacon transmission.
 */
void tinymac_tick_handler(void *arg);

/*!
 * Send a data packet.
 * NOTE: If used under an OS this function must be called from the same thread that
 * calls the tick handler and the PHY receive handler.
 *
 * \param dest		Destination short address
 * \param buf		Pointer to payload data (will be copied if necessary)
 * \param size		Size of payload data
 * \param validity	Validity period (in seconds) for packets sent to a sleeping node
 * \param flags		Flags to set (TINYMAC_FLAGS_ACK_REQUEST only)
 * \param cb		Callback invoked on successful delivery or expiry of validity period
 * \return			Sequence number or -ve error code
 */
int tinymac_send(uint8_t dest, const char *buf, size_t size, uint16_t validity, uint8_t flags, tinymac_send_cb_t cb);

/*!
 * Check if we are connected to a coordinator
 *
 * \return			0 (false) if not connected, 1 (true) if we are
 */
int tinymac_is_registered(void);

/********************/
/* Coordinator only */
/********************/

void tinymac_permit_attach(boolean_t permit);

/*!
 * Register callback to be invoked when a node registers
 * \param cb		Pointer to callback to be invoked
 */
void tinymac_register_reg_cb(tinymac_reg_cb_t cb);

/*!
 * Register callback to be invoked when a node goes away
 * \param cb		Pointer to callback to be invoked
 */
void tinymac_register_dereg_cb(tinymac_reg_cb_t cb);

void tinymac_dump_nodes(void);


#endif /* MAC_H_ */
