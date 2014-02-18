/*
 * mqttsn.c
 *
 *  Created on: 15 Feb 2014
 *      Author: mike
 */

#include <string.h>

#include "common.h"
#include "packet.h"
#include "mqttsn.h"

// FIXME: Not sure what's required here - look at mqtt-sn-tools for comparison
#define SEND_KEEP_ALIVE

#define mqttsn_htons(a)		(((uint16_t)a >> 8) | ((uint16_t)a << 8))
#define mqttsn_ntohs(a)		mqttsn_htons(a)

// for debugging state changes
static const char *states[] = {
	"DISCONNECTED",
	"CONNECTING",
	"REGISTER",
	"REGISTERING",
	"SUBSCRIBE",
	"SUBSCRIBING",
	"CONNECTED",
	"PUBLISHING",
	"DISCONNECTING",
};

static int mqttsn_send(mqttsn_t *ctx, uint8_t msg_type, size_t size, int do_retry)
{
	mqttsn_header_t *hdr = (mqttsn_header_t*)ctx->message;

	hdr->length = size;
	hdr->msg_type = msg_type;
	if (do_retry) {
		ctx->n_retries = MQTTSN_N_RETRY;
		ctx->t_retry = get_seconds() + MQTTSN_T_RETRY;
	}
	ctx->next_ping = get_seconds() + MQTTSN_KEEP_ALIVE;
	return packet_send(ctx->message, size);
}

static inline void mqttsn_abort_send(mqttsn_t *ctx)
{
	ctx->n_retries = 0;
	ctx->t_retry = 0;
}

static void mqttsn_to_state(mqttsn_t *ctx, mqttsn_state_t state)
{
	ctx->state = state;
	mqttsn_abort_send(ctx); // state change always aborts any pending sends
	TRACE("--> %s\n", states[(int)state]);
}

static void mqttsn_connack_handler(mqttsn_t *ctx, const char *buf, size_t size)
{
	mqttsn_connack_t *connack = (mqttsn_connack_t*)buf;

	if (size < sizeof(mqttsn_connack_t)) {
		ERROR("connack: invalid size\n");
		return;
	}

	if (connack->return_code == MQTTSN_RC_ACCEPTED) {
		ctx->count = 0;
		mqttsn_to_state(ctx, mqttsnRegister);
	} else
		mqttsn_to_state(ctx, mqttsnDisconnected);
}

static void mqttsn_register_handler(mqttsn_t *ctx, const char *buf, size_t size)
{

}

static void mqttsn_regack_handler(mqttsn_t *ctx, const char *buf, size_t size)
{
	mqttsn_register_t *reg = (mqttsn_register_t*)ctx->message;
	mqttsn_regack_t *regack = (mqttsn_regack_t*)buf;

	if (size < sizeof(mqttsn_regack_t)) {
		ERROR("regack: invalid size\n");
		return;
	}

	if (ctx->state != mqttsnRegistering) {
		ERROR("regack in invalid state\n");
		return;
	}

	if (regack->msg_id != reg->msg_id) {
		ERROR("regack id mismatch\n");
		return;
	}

	if (regack->return_code == MQTTSN_RC_ACCEPTED) {
		uint16_t topic_id = mqttsn_ntohs(regack->topic_id);
		uint16_t msg_id = mqttsn_ntohs(regack->msg_id);
		/* Update registry - msg_id is already validated against the transmitted value */
		TRACE("registered topic ID 0x%04X for %s (%u)\n", topic_id, reg->topic_name, msg_id);
		ctx->publish_ids[msg_id] = topic_id;
	} else {
		ERROR("registration not accepted: %u\n", regack->return_code);
		// FIXME: retry?
	}

	/* Register next topic */
	mqttsn_to_state(ctx, mqttsnRegister);
}

static void mqttsn_publish_handler(mqttsn_t *ctx, const char *buf, size_t size)
{

}

static void mqttsn_puback_handler(mqttsn_t *ctx, const char *buf, size_t size)
{
	mqttsn_publish_t *publish = (mqttsn_publish_t*)ctx->message;
	mqttsn_puback_t *puback = (mqttsn_puback_t*)buf;

	if (size < sizeof(mqttsn_puback_t)) {
		ERROR("puback: invalid size\n");
		return;
	}

	if (ctx->state != mqttsnPublishing) {
		ERROR("puback in invalid state\n");
		return;
	}

	if (puback->msg_id != publish->msg_id /*|| puback->topic_id != publish->topic_id */) {
		ERROR("puback id mismatch\n");
		return;
	}

	if (puback->return_code != MQTTSN_RC_ACCEPTED) {
		ERROR("publish not accepted: %u\n", puback->return_code);
		// FIXME: If topic not found then we should trigger re-registration
	}

	mqttsn_to_state(ctx, mqttsnConnected);
}

static void mqttsn_suback_handler(mqttsn_t *ctx, const char *buf, size_t size)
{
	mqttsn_subscribe_t *subscribe = (mqttsn_subscribe_t*)ctx->message;
	mqttsn_suback_t *suback = (mqttsn_suback_t*)buf;

	if (size < sizeof(mqttsn_suback_t)) {
		ERROR("suback: invalid size\n");
		return;
	}

	if (ctx->state != mqttsnSubscribing) {
		ERROR("suback in invalid state\n");
		return;
	}

	if (suback->msg_id != subscribe->msg_id) {
		ERROR("suback id mismatch\n");
		return;
	}

	if (suback->return_code == MQTTSN_RC_ACCEPTED) {
		uint16_t topic_id = mqttsn_ntohs(suback->topic_id);
		uint16_t msg_id = mqttsn_ntohs(suback->msg_id);
		/* Update registry - msg_id is already validated against the transmitted value */
		TRACE("subscribed topic ID 0x%04X for %s (%u)\n", topic_id, subscribe->topic.topic_name, msg_id);
		ctx->subscribe_ids[msg_id] = topic_id;
	} else {
		ERROR("subscription not accepted: %u\n", suback->return_code);
		// FIXME: retry?
	}

	/* Register next topic */
	mqttsn_to_state(ctx, mqttsnSubscribe);
}

static void mqttsn_disconnect_handler(mqttsn_t *ctx, const char *buf, size_t size)
{
	mqttsn_to_state(ctx, mqttsnDisconnected);
}

int mqttsn_init(mqttsn_t *ctx, const char *client_id)
{
	packet_init();

	/* Initialise MQTT-SN state */
	memset(ctx, 0, sizeof(mqttsn_t));
	strncpy(ctx->client_id, client_id, sizeof(ctx->client_id));
	return 0;
}

int mqttsn_handler(mqttsn_t *ctx)
{
	static char buf[MQTTSN_MAX_PACKET];
	mqttsn_header_t *hdr;
	int size;

	hdr = (mqttsn_header_t*)ctx->message;
	if (ctx->t_retry && get_seconds() >= ctx->t_retry) {
		/* Re-send pending message */
		if (ctx->n_retries) {
			ctx->n_retries--;
			ctx->t_retry = get_seconds() + MQTTSN_T_RETRY;
			ctx->next_ping = get_seconds() + MQTTSN_KEEP_ALIVE;
			INFO("retrying send (0x%02X), %u remaining\n", hdr->msg_type, ctx->n_retries);
			if (packet_send(ctx->message, hdr->length) < 0) {
				ERROR("send failed\n");
			}
		} else {
			/* Give up */
			ERROR("giving up\n");
			mqttsn_to_state(ctx, mqttsnDisconnected);
		}
	}

#ifdef SEND_KEEP_ALIVE
	if (ctx->state == mqttsnConnected && get_seconds() >= ctx->next_ping) {
		/* Send PINGREQ to keep gateway happy */
		/* FIXME: When waking from sleep this needs to include client_id */
		TRACE("sending PINGREQ\n");
		mqttsn_send(ctx, MQTTSN_PINGREQ, sizeof(mqttsn_pingreq_t), 0);
	}
#endif

	/* Read inbound packet */
	hdr = (mqttsn_header_t*)buf;
	if ((size = packet_recv(buf, sizeof(buf))) > 0) {
		/* Sanity checks */
		if (size < 2) {
			ERROR("packet too short\n");
			return -1;
		}
		if (size < hdr->length) {
			ERROR("packet truncated\n");
			return -1;
		}

		switch (hdr->msg_type) {
	#if 0
		case MQTTSN_ADVERTISE:
			TRACE("ADVERTISE\n");
			break;
		case MQTTSN_GWINFO:
			TRACE("GWINFO\n");
			break;
	#endif
		case MQTTSN_CONNACK:
			TRACE("CONNACK\n");
			mqttsn_connack_handler(ctx, buf, size);
			break;
		case MQTTSN_REGISTER:
			TRACE("REGISTER\n");
			mqttsn_register_handler(ctx, buf, size);
			break;
		case MQTTSN_REGACK:
			TRACE("REGACK\n");
			mqttsn_regack_handler(ctx, buf, size);
			break;
		case MQTTSN_PUBLISH:
			TRACE("PUBLISH\n");
			mqttsn_publish_handler(ctx, buf, size);
			break;
		case MQTTSN_PUBACK:
			TRACE("PUBACK\n");
			mqttsn_puback_handler(ctx, buf, size);
			break;
		case MQTTSN_SUBACK:
			TRACE("SUBACK\n");
			mqttsn_suback_handler(ctx, buf, size);
			break;
		case MQTTSN_UNSUBACK:
			TRACE("UNSUBACK\n");
			break;
		case MQTTSN_PINGREQ:
			TRACE("PINGREQ\n");
			break;
		case MQTTSN_PINGRESP:
			TRACE("PINGRESP\n");
			break;
		case MQTTSN_DISCONNECT:
			TRACE("DISCONNECT\n");
			mqttsn_disconnect_handler(ctx, buf, size);
			break;
		default:
			ERROR("unexpected message type\n");
		}
	}

	/* Handle registrations */
	switch (ctx->state) {
	case mqttsnRegister:
		if (ctx->publish_topics[ctx->count] == NULL) {
			ctx->count = 0;
			mqttsn_to_state(ctx, mqttsnSubscribe);
			break;
		}
		mqttsn_register(ctx, ctx->count, ctx->publish_topics[ctx->count]);
		ctx->count++;
		break;
	case mqttsnSubscribe:
		if (ctx->subscribe_topics[ctx->count] == NULL) {
			ctx->count = 0;
			mqttsn_to_state(ctx, mqttsnConnected);
			break;
		}
		mqttsn_subscribe(ctx, ctx->count, ctx->subscribe_topics[ctx->count]);
		ctx->count++;
		break;
	}

	return 0;
}

mqttsn_state_t mqttsn_get_state(mqttsn_t *ctx)
{
	return ctx->state;
}

void mqttsn_connect(mqttsn_t *ctx, const char **publish_topics, const char **subscribe_topics)
{
	mqttsn_connect_t *connect = (mqttsn_connect_t*)ctx->message;

	if (ctx->state != mqttsnDisconnected) {
		ERROR("Already connected\n");
		return;
	}
	mqttsn_to_state(ctx, mqttsnConnecting);

	/* TODO: Similar for subscriptions.  Should this go here or in init?  Maybe here
	 * but allow NULL in which case CLEAN_SESSION is not used? */
	ctx->publish_topics = publish_topics;
	ctx->subscribe_topics = subscribe_topics;

	connect->flags = MQTTSN_FLAG_CLEAN_SESSION; // FIXME: can we reconnect without this?
	connect->protocol_id = MQTTSN_PROTOCOL_ID;
	connect->duration = mqttsn_htons(MQTTSN_KEEP_ALIVE);
	strcpy(connect->client_id, ctx->client_id); // FIXME: bounds checking
	mqttsn_send(ctx, MQTTSN_CONNECT, sizeof(mqttsn_connect_t) + strlen(ctx->client_id), 1);
}

void mqttsn_disconnect(mqttsn_t *ctx, uint16_t duration)
{
	mqttsn_disconnect_t *disconnect = (mqttsn_disconnect_t*)ctx->message;

	mqttsn_to_state(ctx, mqttsnDisconnecting);

	disconnect->duration = 0;
	mqttsn_send(ctx, MQTTSN_DISCONNECT, sizeof(mqttsn_header_t), 1); // disconnect field is only sent by sleeping clients
}

void mqttsn_register(mqttsn_t *ctx, uint16_t msg_id, const char *topic)
{
	mqttsn_register_t *reg = (mqttsn_register_t*)ctx->message;

	// FIXME: Check state?

	TRACE("REGISTER: %s\n", topic);
	mqttsn_to_state(ctx, mqttsnRegistering);
	reg->topic_id = 0;
	reg->msg_id = mqttsn_htons(msg_id);
	strcpy(reg->topic_name, topic); // FIXME: bounds checking
	mqttsn_send(ctx, MQTTSN_REGISTER, sizeof(mqttsn_register_t) + strlen(topic), 1);
}

void mqttsn_subscribe(mqttsn_t *ctx, uint16_t msg_id, const char *topic)
{
	mqttsn_subscribe_t *subscribe = (mqttsn_subscribe_t*)ctx->message;

	// FIXME: Check state?

	TRACE("SUBSCRIBE: %s\n", topic);
	mqttsn_to_state(ctx, mqttsnSubscribing);
	subscribe->flags = 0; // FIXME: QoS - need to pass this from topic dictionary
	subscribe->msg_id = mqttsn_htons(msg_id);
	strcpy(subscribe->topic.topic_name, topic); // FIXME: bounds checking
	mqttsn_send(ctx, MQTTSN_SUBSCRIBE, sizeof(mqttsn_subscribe_t) + strlen(topic), 1);

	/* Set DUP bit in case we retry */
	subscribe->flags |= MQTTSN_FLAG_DUP;
}

void mqttsn_publish(mqttsn_t *ctx, uint16_t topic_id, const char *data, int qos)
{
	mqttsn_publish_t *publish = (mqttsn_publish_t*)ctx->message;

	if (ctx->state != mqttsnConnected) {
		ERROR("Not connected or busy\n");
		return;
	}

	TRACE("PUBLISH: 0x%04X = %s (qos=%d)\n", topic_id, data, qos);
	if (qos) {
		/* Only if we expect to get a PUBACK back */
		mqttsn_to_state(ctx, mqttsnPublishing);
	}

	/* Send first try (only try for QoS 0) */
	publish->flags = MQTTSN_FLAG_TOPIC_ID_NORM | (qos ? MQTTSN_FLAG_QOS_1 : MQTTSN_FLAG_QOS_0);
	//publish->topic_id = mqttsn_htons(topic_id);
	publish->topic_id = mqttsn_htons(ctx->publish_ids[topic_id]); // FIXME: some way to select between short topics and registry?
	publish->msg_id = 0; // FIXME
	strcpy(publish->data, data); // FIXME: bounds checking
	mqttsn_send(ctx, MQTTSN_PUBLISH, sizeof(mqttsn_publish_t) + strlen(data), qos);

	/* Set DUP bit in case we retry */
	publish->flags |= MQTTSN_FLAG_DUP;
}

