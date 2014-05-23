/*
 * mqttsn_client.h
 *
 *  Created on: 16 Mar 2014
 *      Author: mike
 */

#ifndef CLIENT_H_
#define CLIENT_H_

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
} mqttsn_c_state_t;

typedef enum {
	mqttsnOK = 0,
	mqttsnError,
} mqttsn_c_result_t;

#define MQTTSN_REG_PUBLISH			(0 << 7)
#define MQTTSN_REG_SUBSCRIBE		(1 << 7)

#define MQTTSN_REG_QOS_MASK			(3 << 0)

typedef struct {
	const char *topic;
	uint8_t flags;
} mqttsn_c_topic_t;

#define PUBLISH(topic)				{ topic, MQTTSN_REG_PUBLISH}
#define SUBSCRIBE(topic,qos)		{ topic, MQTTSN_REG_SUBSCRIBE | ((qos) & MQTTSN_REG_QOS_MASK) }

struct mqttsn_c;

/*!
 * Called to send an outbound packet to the network - must be implemented
 *
 * \param buf			Pointer to data buffer
 * \param size			Size of message in bytes
 * \return				Number of bytes actually sent, or -ve error code
 */
typedef int (*mqttsn_c_send_callback_t)(const char *buf, size_t size);

/*!
 * Called when an inbound publish is received from the gateway
 *
 * \param ctx			Pointer to driver context
 * \param topic_index	Index of subscription in table (provided during init)
 * \param data			Payload data
 * \param size			Length of payload
 */
typedef void (*mqttsn_c_publish_callback_t)(struct mqttsn_c *ctx, unsigned int topic_index, const char *data, size_t size);

/*!
 * Called when an outbound QoS >= 1 publish has completed (successfully or not)
 *
 * \param ctx			Pointer to driver context
 * \param msg_id		Message serial number as returned by call to mqttsn_c_publish
 * \param result		Result code
 */
typedef void (*mqttsn_c_puback_callback_t)(struct mqttsn_c *ctx, uint16_t msg_id, mqttsn_c_result_t result);

typedef struct mqttsn_c {
	mqttsn_c_state_t	state;						/*< Current state machine state */
	unsigned int			count;						/*< General purpose counter used in some states */
	char					message[MQTTSN_MAX_PACKET];	/*< Current outgoing message (where we may retry) */
	unsigned int			n_retries;					/*< Number of retries remaining */
	time_t					t_retry;					/*< Timeout for current attempt */
	time_t					next_ping;					/*< Time for next PINGRESP to satisfy keep-alive */
	uint16_t				next_id;					/*< Message ID for next message */

	/* Topic registry */
	const mqttsn_c_topic_t	*topics;				/*< Pointer to application supplied topic dictionary */
	uint16_t				topic_ids[MQTTSN_MAX_CLIENT_TOPICS];
	int						is_registered;				/*< Used for skipping registration if we were asleep */

	/* Config */
	char					client_id[MQTTSN_MAX_CLIENT_ID];	/*< Client ID sent on connect */

	/* Callbacks */
	mqttsn_c_send_callback_t		cb_send;
	mqttsn_c_publish_callback_t	cb_publish;
	mqttsn_c_puback_callback_t		cb_puback;
} mqttsn_c_t;

/*!
 * Initialise the MQTT-SN client
 *
 * \param	ctx			Pointer to driver context
 * \param	client_id	Pointer to string to be used as client ID when connecting
 * \param	topics		Pointer to array of topics for publish/subscribe
 * \param	cb_send		Callback to send an outgoing packet
 * \return				0 on success or -ve error code
 */
int mqttsn_c_init(mqttsn_c_t *ctx, const char *client_id,
		const mqttsn_c_topic_t *topics, mqttsn_c_send_callback_t cb_send);

void mqttsn_c_set_publish_callback(mqttsn_c_t *ctx, mqttsn_c_publish_callback_t cb);
void mqttsn_c_set_puback_callback(mqttsn_c_t *ctx, mqttsn_c_puback_callback_t cb);

/*!
 * State machine - must be called at minimum 1 second intervals or
 * when a new packet arrives
 *
 * \param	ctx			Pointer to driver context
 * \param	buf			Pointer to incoming packet buffer (may be NULL if periodic call)
 * \param	size		Size of incoming packet (or 0 for periodic call)
 */
void mqttsn_c_handler(mqttsn_c_t *ctx, const char *buf, size_t size);

/*!
 * Returns current state machine state
 * \return				State
 */
mqttsn_c_state_t mqttsn_c_get_state(mqttsn_c_t *ctx);

void mqttsn_c_connect(mqttsn_c_t *ctx);
void mqttsn_c_disconnect(mqttsn_c_t *ctx, uint16_t duration);

/*!
 * \param ctx			Pointer to driver context
 * \param topic_index	Index of topic in registration table (supplied on init)
 * \param qos			QoS level (0 or 1) FIXME: support -1 and 2?
 * \param data			Content to publish
 * \param size			Size of content
 * \return				Assigned message ID (serial number)
 */
uint16_t mqttsn_c_publish(mqttsn_c_t *ctx, unsigned int topic_index, int qos, const char *data, size_t size);

#endif /* CLIENT_H_ */
