/*
 * test.c
 *
 *  Created on: 15 Feb 2014
 *      Author: mike
 */

#include <signal.h>

#include "common.h"
#include "mqttsn.h"

#define INTERVAL 5

static volatile int quit = 0;

static const char* publish_topics[] = {
		"zone/1/0/temp",
		"zone/1/0/status",
		"zone/1/0/important",
		NULL,
};

static const char* subscribe_topics[] = {
		"zone/1/target",
		NULL,
};

static void break_handler(int signum)
{
	quit = 1;
}

int main(void)
{
	struct sigaction new_sa, old_sa;
	time_t next = 0;
	mqttsn_t _ctx;
	mqttsn_t *ctx = &_ctx;

	new_sa.sa_handler = break_handler;
	sigemptyset(&new_sa.sa_mask);
	new_sa.sa_flags = 0;
	sigaction(SIGINT, &new_sa, &old_sa);

	mqttsn_init(ctx, "test1");
	mqttsn_connect(ctx, publish_topics, subscribe_topics);
	while (!quit) {
		packet_poll(1000);
		mqttsn_handler(ctx);
		switch (mqttsn_get_state(ctx)) {
		case mqttsnDisconnected:
			// try to (re-)connect
			mqttsn_connect(ctx, publish_topics, subscribe_topics);
			break;
		case mqttsnConnected:
			// do publish stuff
			if (time(NULL) >= next) {
				next = time(NULL) + INTERVAL;
				mqttsn_publish(ctx, 0, "hello", 0);
				mqttsn_publish(ctx, 1, "world", 0);
				mqttsn_publish(ctx, 2, "qos", 1);
			}
			break;
		case mqttsnPublishing:
			break;
		}
	}

	mqttsn_disconnect(ctx, 0);
	sigaction(SIGINT, &old_sa, NULL);

	return 0;
}
