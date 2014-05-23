/*
 * test.c
 *
 *  Created on: 15 Feb 2014
 *      Author: mike
 */

#include <signal.h>

#include "common.h"
#include "packet.h"
#include "client/client.h"

#define INTERVAL 60

static volatile int quit = 0;

static const mqttsn_c_topic_t topics[] = {
#if 1
	PUBLISH("zone/1/0/temp"),
	PUBLISH("zone/1/0/status"),
	PUBLISH("zone/1/0/important"),
#endif
	SUBSCRIBE("zone/1/target", 0),
	SUBSCRIBE("zone/1/message", 0),
	{NULL}
};

static void break_handler(int signum)
{
	quit = 1;
}

#define HOST		"localhost"
#define PORT		1883

int main(void)
{
	struct sigaction new_sa, old_sa;
	time_t next = time(NULL) + INTERVAL;
	mqttsn_c_t _ctx;
	mqttsn_c_t *ctx = &_ctx;

	new_sa.sa_handler = break_handler;
	sigemptyset(&new_sa.sa_mask);
	new_sa.sa_flags = 0;
	sigaction(SIGINT, &new_sa, &old_sa);

	packet_init(HOST, PORT);
	mqttsn_c_init(ctx, "test1", topics, packet_send);
	mqttsn_c_connect(ctx);
	while (!quit) {
		char buf[MQTTSN_MAX_PACKET];
		int size;

		/* Poll for incoming packets. Call handler on packet arrival
		 * or every second */
		packet_poll(1000);
		size = packet_recv(buf, sizeof(buf));
		mqttsn_c_handler(ctx, (size < 0) ? NULL : buf, size);

		switch (mqttsn_c_get_state(ctx)) {
		case mqttsnDisconnected:
			// try to (re-)connect
			if (time(NULL) >= next) {
				next = time(NULL) + INTERVAL;
				mqttsn_c_connect(ctx);
			}
			break;
		case mqttsnConnected:
			// do publish stuff
			if (time(NULL) >= next) {
				next = time(NULL) + INTERVAL;
//				mqttsn_c_disconnect(ctx, 0);
#if 1
				mqttsn_c_publish(ctx, 0, 0, "hello", 5);
				mqttsn_c_publish(ctx, 1, 0, "world", 5);
				mqttsn_c_publish(ctx, 2, 1, "qos", 3);
#endif
			}
			break;
		default:
			break;
		}
	}

	mqttsn_c_disconnect(ctx, 0);
	sigaction(SIGINT, &old_sa, NULL);

	return 0;
}
