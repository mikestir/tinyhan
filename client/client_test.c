/*
 * test.c
 *
 *  Created on: 15 Feb 2014
 *      Author: mike
 */

#include <stdlib.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "tinymac.h"
#include "phy.h"
#include "client/client.h"

#define INTERVAL 2

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

mqttsn_c_t _ctx;
mqttsn_c_t *ctx = &_ctx;

static void break_handler(int signum)
{
	quit = 1;
}

static void rx_handler(uint8_t src, const char *buf, size_t size)
{
	mqttsn_c_handler(ctx, buf, size);
}

static int packet_send(const char *buf, size_t size)
{
	return tinymac_send(0, buf, size);
}

int main(void)
{
	struct sigaction new_sa, old_sa;
	time_t next;
	uint64_t id;
	char idstr[10];

	new_sa.sa_handler = break_handler;
	sigemptyset(&new_sa.sa_mask);
	new_sa.sa_flags = 0;
	sigaction(SIGINT, &new_sa, &old_sa);

	srand(time(NULL) + getpid());
	id = rand();
	snprintf(idstr, sizeof(idstr), "test%04X", (uint16_t)(id & 0xffff));

	phy_init();
	tinymac_init(rand(), FALSE);
	tinymac_register_recv_cb(rx_handler);
	mqttsn_c_init(ctx, idstr, topics, packet_send);
	mqttsn_c_connect(ctx);

	next = time(NULL) + INTERVAL;
	while (!quit) {
		struct pollfd pfd;
		int rc;

		/* Wait for activity */
		memset(&pfd, 0, sizeof(pfd));
		pfd.fd = phy_get_fd();
		pfd.events = POLLIN;
		rc = poll(&pfd, 1, 1000);

		/* Execute non-blocking tasks */
		tinymac_process();

		/* Periodic stuff */
		if (time(NULL) >= next) {
			next = time(NULL) + INTERVAL;
			mqttsn_c_handler(ctx, NULL, 0); /* periodic call */

			switch (mqttsn_c_get_state(ctx)) {
			case mqttsnDisconnected:
					mqttsn_c_connect(ctx);
				break;
			case mqttsnConnected:
					mqttsn_c_publish(ctx, 0, 0, "hello", 5);
					mqttsn_c_publish(ctx, 1, 0, "world", 5);
					mqttsn_c_publish(ctx, 2, 1, "qos", 3);
				break;
			default:
				break;
			}

		}
	}

	mqttsn_c_disconnect(ctx, 0);
	sigaction(SIGINT, &old_sa, NULL);

	return 0;
}
