/*
 * mqttsn.h
 *
 *  Created on: 15 Feb 2014
 *      Author: mike
 */

#ifndef MQTTSN_H_
#define MQTTSN_H_

#include <stddef.h>
#include <stdint.h>

/* MsgType */

#define MQTTSN_ADVERTISE			0x00
#define MQTTSN_SEARCHGW				0x01
#define MQTTSN_GWINFO				0x02
#define MQTTSN_CONNECT				0x04
#define MQTTSN_CONNACK				0x05
#define MQTTSN_WILLTOPICREQ			0x06
#define MQTTSN_WILLTOPIC			0x07
#define MQTTSN_WILLMSGREQ			0x08
#define MQTTSN_WILLMSG				0x09
#define MQTTSN_REGISTER				0x0A
#define MQTTSN_REGACK				0x0B
#define MQTTSN_PUBLISH				0x0C
#define MQTTSN_PUBACK				0x0D
#define MQTTSN_PUBCOMP				0x0E
#define MQTTSN_PUBREC				0x0F
#define MQTTSN_PUBREL				0x10
#define MQTTSN_SUBSCRIBE			0x12
#define MQTTSN_SUBACK				0x13
#define MQTTSN_UNSUBSCRIBE			0x14
#define MQTTSN_UNSUBACK				0x15
#define MQTTSN_PINGREQ				0x16
#define MQTTSN_PINGRESP				0x17
#define MQTTSN_DISCONNECT			0x18
#define MQTTSN_WILLTOPICUPD			0x1A
#define MQTTSN_WILLTOPICRESP		0x1B
#define MQTTSN_WILLMSGUPD			0x1C
#define MQTTSN_WILLMSGRESPO			0x1D

/* Flags */

#define MQTTSN_FLAG_DUP				(1 << 7)
#define MQTTSN_FLAG_RETAIN			(1 << 4)
#define MQTTSN_FLAG_WILL			(1 << 3)
#define MQTTSN_FLAG_CLEAN_SESSION	(1 << 2)

#define MQTTSN_FLAG_QOS_M1			(3 << 5)
#define MQTTSN_FLAG_QOS_0			(0 << 5)
#define MQTTSN_FLAG_QOS_1			(1 << 5)
#define MQTTSN_FLAG_QOS_2			(2 << 5)
#define MQTTSN_FLAG_QOS_MASK		(3 << 5)

#define MQTTSN_FLAG_TOPIC_ID_NORM	(0 << 0)
#define MQTTSN_FLAG_TOPIC_ID_PRE	(1 << 0)
#define MQTTSN_FLAG_TOPIC_ID_SHORT	(2 << 0)
#define MQTTSN_FLAG_TOPIC_ID_MASK	(3 << 0)

/* Return code */

#define MQTTSN_RC_ACCEPTED			0x00
#define MQTTSN_RC_CONGESTION		0x01
#define MQTTSN_RC_INVALID_TOPIC		0x02
#define MQTTSN_RC_NOT_SUPPORTED		0x03

#define MQTTSN_PROTOCOL_ID			0x01

typedef struct {
	uint8_t		length;
	uint8_t		msg_type;
} __attribute__((packed)) mqttsn_header_t;

typedef struct {
	mqttsn_header_t	header;
	uint8_t		gw_id;
	uint16_t	duration;
} __attribute__((packed)) mqttsn_advertise_t;

typedef struct {
	mqttsn_header_t	header;
	uint8_t		radius;
} __attribute__((packed)) mqttsn_searchgw_t;

typedef struct {
	mqttsn_header_t	header;
	uint8_t		gw_id;
	uint8_t		gw_add[0]; /* optional */
} __attribute__((packed)) mqttsn_gwinfo_t;

typedef struct {
	mqttsn_header_t	header;
	uint8_t		flags;
	uint8_t		protocol_id;
	uint16_t	duration;
	char		client_id[0]; /* variable length */
} __attribute__((packed)) mqttsn_connect_t;

typedef struct {
	mqttsn_header_t	header;
	uint8_t		return_code;
} __attribute__((packed)) mqttsn_connack_t;

/* WILLTOPICREQ, WILLTOPIC, WILLMSGREQ, WILLMSG not implemented */

typedef struct {
	mqttsn_header_t	header;
	uint16_t	topic_id;
	uint16_t	msg_id;
	char		topic_name[0]; /* variable length */
} __attribute__((packed)) mqttsn_register_t;

typedef struct {
	mqttsn_header_t	header;
	uint16_t	topic_id;
	uint16_t	msg_id;
	uint8_t		return_code;
} __attribute__((packed)) mqttsn_regack_t;

typedef struct {
	mqttsn_header_t	header;
	uint8_t		flags;
	uint16_t	topic_id;
	uint16_t	msg_id;
	char		data[0]; /* variable length */
} __attribute__((packed)) mqttsn_publish_t;

typedef struct {
	mqttsn_header_t	header;
	uint16_t	topic_id;
	uint16_t	msg_id;
	uint8_t		return_code;
} __attribute__((packed)) mqttsn_puback_t;

/* PUBREC, PUBREL, PUBCOMP (QoS level 2) not implemented */

typedef struct {
	mqttsn_header_t	header;
	uint8_t		flags;
	uint16_t	msg_id;
	union {
		uint16_t	topic_id;
		char		topic_name[0]; /* variable length */
	} topic;
} __attribute__((packed)) mqttsn_subscribe_t;

typedef struct {
	mqttsn_header_t	header;
	uint8_t		flags;
	uint16_t	topic_id;
	uint16_t	msg_id;
	uint8_t		return_code;
} __attribute__((packed)) mqttsn_suback_t;

typedef mqttsn_subscribe_t mqttsn_unsubscribe_t; /* same */

typedef struct {
	mqttsn_header_t	header;
	uint16_t	msg_id;
} __attribute__((packed)) mqttsn_unsuback_t;

typedef struct {
	mqttsn_header_t header;
	char		client_id[0]; /* variable length */
} __attribute__((packed)) mqttsn_pingreq_t;

typedef struct {
	mqttsn_header_t	header;
} __attribute__((packed)) mqttsn_pingresp_t;

typedef struct {
	mqttsn_header_t	header;
	uint16_t	duration;
} __attribute__((packed)) mqttsn_disconnect_t;

/* WILLTOPICUPD, WILLMSGUPD, WILLTOPICRESP, WILLMSGRESP not implemented */


#endif /* MQTTSN_H_ */
