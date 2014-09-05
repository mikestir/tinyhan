/*
 * mac.h
 *
 *  Created on: 12 Aug 2014
 *      Author: mike
 */

#ifndef MAC_H_
#define MAC_H_

#include <stdint.h>
#include "common.h"

#define TINYMAC_ADDR_BROADCAST				0xFF
#define TINYMAC_ADDR_UNASSIGNED				0xFF
#define TINYMAC_NETWORK_ANY					0xFF

#define TINYMAC_RETRY_INTERVAL				5

/*! Time (in seconds) after which we try to contact a node that we haven't heard from
 * in a while */
#define TINYMAC_LAST_HEARD_TIMEOUT			5

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
	tinymacType_Reserved1,
	tinymacType_BeaconRequest,
	tinymacType_Ack,
	tinymacType_RegistrationRequest,
	tinymacType_DeregistrationRequest,
	tinymacType_RegistrationResponse,
	tinymacType_Ping,
	tinymacType_Reserved8,
	tinymacType_Reserved9,
	tinymacType_Reserved10,
	tinymacType_Reserved11,
	tinymacType_Reserved12,
	tinymacType_Reserved13,
	tinymacType_Reserved14,
	tinymacType_Reserved15,
	tinymacType_Reserved16,
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
	tinymacType_Data,
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

#define TINYMAC_ATTACH_FLAGS_SLEEPY				(1 << 0)

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
} tinymac_registration_status_t;

typedef struct {
	uint64_t		uuid;
	boolean_t		coordinator;
	uint8_t			flags;					/*< Node flags, e.g. SLEEPY */
	uint8_t			beacon_interval;		/*< 250 ms * 2^n, or TINYMAC_BEACON_INTERVAL_NO_BEACON */
	uint8_t			beacon_offset;
} tinymac_params_t;

typedef void (*tinymac_recv_cb_t)(uint8_t src, const char *payload, size_t size);
typedef void (*tinymac_reg_cb_t)(uint64_t uuid, uint8_t addr);

int tinymac_init(const tinymac_params_t *params);
void tinymac_register_recv_cb(tinymac_recv_cb_t cb);

/*!
 * Called at least when new received data is available (i.e. PHY function
 * has something to do).  Call once per interrupt in main loop when no OS
 * is in use.
 */
void tinymac_recv_handler(void);

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
 * Send a data packet
 *
 * \param dest		Destination short address
 * \param buf		Pointer to payload data
 * \param size		Size of payload data
 * \return			Sequence number or -ve error code
 */
int tinymac_send(uint8_t dest, const char *buf, size_t size);

/********************/
/* Coordinator only */
/********************/

void tinymac_permit_attach(boolean_t permit);

void tinymac_register_reg_cb(tinymac_reg_cb_t cb);
void tinymac_register_dereg_cb(tinymac_reg_cb_t cb);

#endif /* MAC_H_ */