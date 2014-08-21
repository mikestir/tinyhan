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

/*!
 * Define to disable coordinator functionality (reduced code size), otherwise
 * this is configurable at run time.
 */
#define TINYMAC_COORDINATOR_SUPPORT			1

#define TINYMAC_ADDR_BROADCAST				0xFF
#define TINYMAC_ADDR_UNASSIGNED				0xFF
#define TINYMAC_NETWORK_ANY					0xFF

#define TINYMAC_RETRY_INTERVAL				5

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
	uint32_t		timestamp;
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


typedef enum {
	/* Client states */
	tinymacState_Unregistered = 0,
	tinymacState_BeaconRequest,
	tinymacState_Registering,
	tinymacState_Registered,

	/* Send states */
	tinymacState_WaitAck,
	tinymacState_Ack,
	tinymacState_Nak,
} tinymac_state_t;

typedef void (*tinymac_recv_cb_t)(uint8_t src, const char *payload, size_t size);

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
#endif

	unsigned int		phy_mtu;		/*< MTU from PHY driver */
	tinymac_recv_cb_t	rx_cb;			/*< MAC data receive callback */
} tinymac_t;

int tinymac_init(uint64_t uuid, boolean_t coord);

void tinymac_process(void);

void tinymac_register_recv_cb(tinymac_recv_cb_t cb);
void tinymac_permit_attach(boolean_t permit);

int tinymac_send(uint8_t dest, const char *buf, size_t size);

#endif /* MAC_H_ */
