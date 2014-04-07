/*
 * mqttsn_client.h
 *
 *  Created on: 16 Mar 2014
 *      Author: mike
 */

#ifndef MQTTSN_CLIENT_H_
#define MQTTSN_CLIENT_H_

#include <stddef.h>
#include <stdint.h>

#include "mqttsn.h"

#if defined(__linux__) || defined(__APPLE__)
#include <time.h>
#define get_seconds()		time(NULL)
#else
typedef uint32_t time_t;
extern uint32_t get_seconds();
#endif

typedef enum {
	mqttsnDisconnected = 0,
	mqttsnConnecting,
	mqttsnRegistering,
	mqttsnConnected,
	mqttsnBusy,
	mqttsnDisconnecting,
} mqttsn_client_state_t;

#define MQTTSN_REG_PUBLISH			(0 << 7)
#define MQTTSN_REG_SUBSCRIBE		(1 << 7)

#define MQTTSN_REG_QOS_MASK			(3 << 0)

typedef struct {
	const char *topic;
	uint8_t flags;
} mqttsn_client_topic_t;

typedef int (*mqttsn_client_send_callback_t)(const char *buf, size_t size);

#define PUBLISH(topic)				{ topic, MQTTSN_REG_PUBLISH}
#define SUBSCRIBE(topic,qos)		{ topic, MQTTSN_REG_SUBSCRIBE | ((qos) & MQTTSN_REG_QOS_MASK) }

typedef struct {
	mqttsn_client_state_t	state;						/*< Current state machine state */
	unsigned int			count;						/*< General purpose counter used in some states */
	char					message[MQTTSN_MAX_PACKET];	/*< Current outgoing message (where we may retry) */
	unsigned int			n_retries;					/*< Number of retries remaining */
	time_t					t_retry;					/*< Timeout for current attempt */
	time_t					next_ping;					/*< Time for next PINGRESP to satisfy keep-alive */

	/* Topic registry */
	const mqttsn_client_topic_t	*topics;				/*< Pointer to application supplied topic dictionary */
	uint16_t				topic_ids[MQTTSN_MAX_CLIENT_TOPICS];
	int						is_registered;				/*< Used for skipping registration if we were asleep */

	/* Config */
	char					client_id[MQTTSN_MAX_CLIENT_ID];	/*< Client ID sent on connect */

	/* Callbacks */
	mqttsn_client_send_callback_t	cb_send;
} mqttsn_client_t;

/*!
 * Initialise the MQTT-SN client
 *
 * \param	ctx			Pointer to driver context
 * \param	client_id	Pointer to string to be used as client ID when connecting
 * \param	topics		Pointer to array of topics for publish/subscribe
 * \param	cb_send		Callback to send an outgoing packet
 * \return				0 on success or -ve error code
 */
int mqttsn_client_init(mqttsn_client_t *ctx, const char *client_id,
		const mqttsn_client_topic_t *topics, mqttsn_client_send_callback_t cb_send);

/*!
 * State machine - must be called at minimum 1 second intervals or
 * when a new packet arrives
 *
 * \param	ctx			Pointer to driver context
 * \param	buf			Pointer to incoming packet buffer (may be NULL if periodic call)
 * \param	size		Size of incoming packet (or 0 for periodic call)
 */
void mqttsn_client_handler(mqttsn_client_t *ctx, const char *buf, size_t size);

/*!
 * Returns current state machine state
 * \return				State
 */
mqttsn_client_state_t mqttsn_get_client_state(mqttsn_client_t *ctx);



void mqttsn_connect(mqttsn_client_t *ctx);
void mqttsn_disconnect(mqttsn_client_t *ctx, uint16_t duration);

/*!
 * \param ctx			Pointer to driver context
 * \param topic_index	Index of topic in registry (as defined on connection)
 * \param qos			QoS level (0 or 1) FIXME: support -1 and 2?
 * \param data			String to publish
 */
void mqttsn_publish(mqttsn_client_t *ctx, unsigned int topic_index, const char *data, int qos);

#endif /* MQTTSN_CLIENT_H_ */
