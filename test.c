/*
 * test.c
 *
 *  Created on: 15 Feb 2014
 *      Author: mike
 */

#include <signal.h>

#include "common.h"
#include "packet.h"
#include "mqttsn.h"

#define INTERVAL 5

static volatile int quit = 0;

static const mqttsn_topic_t topics[] = {
#if 1
	PUBLISH("zone/1/0/temp"),
	PUBLISH("zone/1/0/status"),
	PUBLISH("zone/1/0/important"),
#endif
	SUBSCRIBE("zone/1/target", 0),
	SUBSCRIBE("zone/1/message", 0),
	NULL
};

static void break_handler(int signum)
{
	quit = 1;
}

int main(void)
{
	struct sigaction new_sa, old_sa;
	time_t next = time(NULL) + INTERVAL;
	mqttsn_t _ctx;
	mqttsn_t *ctx = &_ctx;

	new_sa.sa_handler = break_handler;
	sigemptyset(&new_sa.sa_mask);
	new_sa.sa_flags = 0;
	sigaction(SIGINT, &new_sa, &old_sa);

	mqttsn_init(ctx, "test1", topics);
	mqttsn_connect(ctx);
	while (!quit) {
		packet_poll(1000);
		mqttsn_handler(ctx);
		switch (mqttsn_get_state(ctx)) {
		case mqttsnDisconnected:
			// try to (re-)connect
			if (time(NULL) >= next) {
				next = time(NULL) + INTERVAL;
				mqttsn_connect(ctx);
			}
			break;
		case mqttsnConnected:
			// do publish stuff
			if (time(NULL) >= next) {
				next = time(NULL) + INTERVAL;
				mqttsn_disconnect(ctx, 0);
#if 0
				mqttsn_publish(ctx, 0, "hello", 0);
				mqttsn_publish(ctx, 1, "world", 0);
				mqttsn_publish(ctx, 2, "qos", 1);
#endif
			}
			break;
		}
	}

	mqttsn_disconnect(ctx, 0);
	sigaction(SIGINT, &old_sa, NULL);

	return 0;
}
