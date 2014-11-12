/*
 * test.c
 *
 *  Created on: 15 Feb 2014
 *      Author: mike
 */

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>

#include "common.h"
#include "tinymac.h"
#include "phy.h"
#include "mqttsn-client.h"

#define MAX_EVENTS		4

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
	return tinymac_send(0, buf, size, 0, NULL);
}

int main(void)
{
	struct sigaction new_sa, old_sa;
	char idstr[10];
	struct epoll_event ev, events[MAX_EVENTS];
	int epoll_fd, timer_fd;
	struct itimerspec its;
	tinymac_params_t params = {
			.flags = /* TINYMAC_ATTACH_FLAGS_SLEEPY | */ 5, /* specify hearbeat interval */
	};

	new_sa.sa_handler = break_handler;
	sigemptyset(&new_sa.sa_mask);
	new_sa.sa_flags = 0;
	sigaction(SIGINT, &new_sa, &old_sa);

	/* Initialise comms */
	srand(time(NULL) + getpid());
	phy_init();
	params.uuid = rand();
	tinymac_init(&params);
	tinymac_register_recv_cb(rx_handler);
	snprintf(idstr, sizeof(idstr), "test%04X", (uint16_t)(params.uuid & 0xffff));
	mqttsn_c_init(ctx, idstr, topics, packet_send);
	mqttsn_c_connect(ctx);

	/* Set up epoll */
	epoll_fd = epoll_create(ARRAY_SIZE(events));
	if (epoll_fd < 0) {
		perror("epoll_create");
		return 1;
	}

	/* watch tinymac PHY fd */
	ev.events = EPOLLIN;
	ev.data.u32 = 0;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, phy_get_fd(), &ev) < 0) {
		perror("epoll_ctl");
		return 1;
	}

	/* Create a timer fd for periodic handler */
	timer_fd = timerfd_create(CLOCK_REALTIME, 0);
	if (timer_fd < 0) {
		perror("timerfd_create");
		return 1;
	}
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 1000000ull * TINYMAC_TICK_MS;
	its.it_value = its.it_interval;
	if (timerfd_settime(timer_fd, 0, &its, NULL) < 0) {
		perror("timerfd_settime");
		return 1;
	}
	ev.events = EPOLLIN;
	ev.data.u32 = 1;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev) < 0) {
		perror("epoll_ctl");
		return 1;
	}

	while (!quit) {
		int n, nfds;

		/* Wait for event */
		nfds = epoll_wait(epoll_fd, events, ARRAY_SIZE(events), -1);
		if (nfds < 0) {
			perror("epoll_wait");
			return 1;
		}

		for (n = 0; n < nfds; n++) {
			if (events[n].data.u32 == 0) {
				/* PHY event */
				phy_event_handler();
			} else if (events[n].data.u32 == 1) {
				/* tick */
				char dummy[8];
				read(timer_fd, dummy, sizeof(dummy));

				tinymac_tick_handler(NULL);

				mqttsn_c_handler(ctx, NULL, 0); /* periodic call */

#if 0
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
#endif
			}
		}
	}

	mqttsn_c_disconnect(ctx, 0);
	sigaction(SIGINT, &old_sa, NULL);

	return 0;
}
