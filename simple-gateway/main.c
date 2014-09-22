/*
 * main.c
 *
 *  Created on: 23 Aug 2014
 *      Author: mike
 */

#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>

#include "common.h"
#include "tinymac.h"
#include "phy.h"
#include "timer.h"

#define MAX_EVENTS			16
#define MAX_DEVICES 		256
#define MAX_PAYLOAD			1024
#define BROKER_ADDR			"127.0.0.1"
#define BROKER_PORT			1883
#define DEVICE_PORT_BASE	11000

static volatile boolean_t quit = FALSE;
static int socks[MAX_DEVICES];

static void break_handler(int signum)
{
	quit = TRUE;
}

static void rx_handler(uint8_t src, const char *buf, size_t size)
{
	struct sockaddr_in sa;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(BROKER_ADDR);
	sa.sin_port = htons(BROKER_PORT);

	/* UDP send to broker */
	sendto(socks[src], buf, size, 0, (struct sockaddr*)&sa, sizeof(sa));
}

int main(void)
{
	struct sigaction new_sa, old_sa;
	unsigned int n;
	struct sockaddr_in sa;
	struct epoll_event ev, events[MAX_EVENTS];
	int epoll_fd, timer_fd;
	struct itimerspec its;
	tinymac_params_t params = {
			.coordinator = TRUE,
			.beacon_interval = 3,
			.beacon_offset = 0,
	};

	/* Trap break */
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
	tinymac_permit_attach(TRUE);

	/* Set up epoll */
	epoll_fd = epoll_create(ARRAY_SIZE(events));
	if (epoll_fd < 0) {
		perror("epoll_create");
		return 1;
	}

	/* watch tinymac PHY fd */
	ev.events = EPOLLIN;
	ev.data.u32 = MAX_DEVICES + 0;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, phy_get_fd(), &ev) < 0) {
		perror("epoll_ctl");
		return 1;
	}

	/* Bind a UDP socket for each device we will be gating, for
	 * replies from the broker */
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	for (n = 0; n < MAX_DEVICES; n++) {
		sa.sin_port = htons(DEVICE_PORT_BASE + n);

		socks[n] = socket(AF_INET, SOCK_DGRAM, 0);
		if (socks[n] < 0) {
			perror("socket");
			return 1;
		}

		if (bind(socks[n], (struct sockaddr*)&sa, sizeof(sa)) < 0) {
			perror("bind");
			return 1;
		}

		/* Watch for events */
		ev.events = EPOLLIN;
		ev.data.u32 = n;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socks[n], &ev) < 0) {
			perror("epoll_ctl");
			return 1;
		}
	}

	/* Create a timer fd for despatching timer callbacks */
	timer_fd = timerfd_create(CLOCK_REALTIME, 0);
	if (timer_fd < 0) {
		perror("timerfd_create");
		return 1;
	}
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 10000000ul; /* 10 ms timer tick */
	its.it_value = its.it_interval;
	if (timerfd_settime(timer_fd, 0, &its, NULL) < 0) {
		perror("timerfd_settime");
		return 1;
	}
	ev.events = EPOLLIN;
	ev.data.u32 = MAX_DEVICES + 1;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev) < 0) {
		perror("epoll_ctl");
		return 1;
	}

	timer_request_callback(tinymac_tick_handler, NULL, TIMER_MILLIS(250), 0);

	while (!quit) {
		int n, nfds;

		/* Wait for event */
		nfds = epoll_wait(epoll_fd, events, ARRAY_SIZE(events), -1);
		if (nfds < 0) {
			perror("epoll_wait");
			return 1;
		}

		for (n = 0; n < nfds; n++) {
			if (events[n].data.u32 < MAX_DEVICES) {
				/* Incoming data from broker */
				char payload[MAX_PAYLOAD];
				struct sockaddr_in rxsa;
				socklen_t addrlen = sizeof(rxsa);
				int size;

				/* Relay to device */
				memset(&sa, 0, sizeof(sa));
				size = recvfrom(socks[events[n].data.u32], payload, sizeof(payload), 0, (struct sockaddr*)&rxsa, &addrlen);
				if (size < 0) {
					perror("recvfrom");
					return 1;
				}
				tinymac_send((uint8_t)events[n].data.u32, payload, size);
			} else if (events[n].data.u32 == MAX_DEVICES + 0) {
				/* tinymac */
				tinymac_recv_handler();
			} else if (events[n].data.u32 == MAX_DEVICES + 1) {
				/* tick */
				char dummy[8];
				read(timer_fd, dummy, sizeof(dummy));

				timer_despatch_callbacks();
			}
		}
	}

	/* Close sockets */
	for (n = 0; n < MAX_DEVICES; n++) {
		close(socks[n]);
	}
	/* Terminate ticker */
	close(timer_fd);

	sigaction(SIGINT, &old_sa, NULL);

	return 0;
}
