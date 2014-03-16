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

#define MQTTSN_MAX_PACKET			64
#define MQTTSN_MAX_CLIENT_ID		8
#define MQTTSN_MAX_TOPICS			16

#define MQTTSN_N_RETRY				3
#define MQTTSN_T_RETRY				5	/* seconds */
#define MQTTSN_KEEP_ALIVE			10	/* seconds */

typedef enum {
	mqttsnDisconnected = 0,
	mqttsnConnecting,
	mqttsnRegistering,
	mqttsnConnected,
	mqttsnBusy,
	mqttsnDisconnecting,
} mqttsn_state_t;

#define MQTTSN_REG_PUBLISH		(0 << 7)
#define MQTTSN_REG_SUBSCRIBE	(1 << 7)

#define MQTTSN_REG_QOS_MASK		(3 << 0)

typedef struct {
	const char *topic;
	uint8_t flags;
} mqttsn_topic_t;

#define PUBLISH(topic)				{ topic, MQTTSN_REG_PUBLISH}
#define SUBSCRIBE(topic,qos)		{ topic, MQTTSN_REG_SUBSCRIBE | ((qos) & MQTTSN_REG_QOS_MASK) }

typedef struct {
	mqttsn_state_t	state;								/*< Current state machine state */
	unsigned int	count;								/*< General purpose counter used in some states */
	char			message[MQTTSN_MAX_PACKET];			/*< Current outgoing message (where we may retry) */
	unsigned int	n_retries;							/*< Number of retries remaining */
	time_t			t_retry;							/*< Timeout for current attempt */
	time_t			next_ping;							/*< Time for next PINGRESP to satisfy keep-alive */

	/* Topic registry */
	const mqttsn_topic_t	*topics;					/*< Pointer to application supplied topic dictionary */
	uint16_t		topic_ids[MQTTSN_MAX_TOPICS];
	int				is_registered;						/*< Used for skipping registration if we were asleep */

	/* Config */
	char			client_id[MQTTSN_MAX_CLIENT_ID];	/*< Client ID sent on connect */
} mqttsn_t;

int mqttsn_init(mqttsn_t *ctx, const char *client_id, const mqttsn_topic_t *topics);
int mqttsn_handler(mqttsn_t *ctx);
mqttsn_state_t mqttsn_get_state(mqttsn_t *ctx);

void mqttsn_connect(mqttsn_t *ctx);
void mqttsn_disconnect(mqttsn_t *ctx, uint16_t duration);

/*!
 * \param ctx			Pointer to driver context
 * \param topic_index	Index of topic in registry (as defined on connection)
 * \param qos			QoS level (0 or 1) FIXME: support -1 and 2?
 * \param data			String to publish
 */
void mqttsn_publish(mqttsn_t *ctx, unsigned int topic_index, const char *data, int qos);

#endif /* MQTTSN_CLIENT_H_ */
