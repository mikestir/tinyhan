/*
 * main.c
 *
 *  Created on: 23 Aug 2014
 *      Author: mike
 */

#include <stdlib.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "common.h"
#include "tinymac.h"
#include "phy.h"

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
	struct pollfd pfd[MAX_DEVICES + 1];

	/* Trap break */
	new_sa.sa_handler = break_handler;
	sigemptyset(&new_sa.sa_mask);
	new_sa.sa_flags = 0;
	sigaction(SIGINT, &new_sa, &old_sa);

	srand(time(NULL) + getpid());
	phy_init();
	tinymac_init(rand(), TRUE);
	tinymac_register_recv_cb(rx_handler);
	tinymac_permit_attach(TRUE);

	memset(pfd, 0, sizeof(pfd));
	pfd[MAX_DEVICES].fd = phy_get_fd();
	pfd[MAX_DEVICES].events = POLLIN;

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

		pfd[n].fd = socks[n];
		pfd[n].events = POLLIN;
	}

	while (!quit) {
		int rc;

		/* Wait for activity with timeout for periodic actions */
		rc = poll(pfd, ARRAY_SIZE(pfd), 1000);

		/* Check for broker data */
		if (rc) {
			for (n = 0; n < MAX_DEVICES; n++) {
				if (pfd[n].revents == POLLIN) {
					char payload[MAX_PAYLOAD];
					struct sockaddr_in rxsa;
					socklen_t addrlen = sizeof(rxsa);
					int size;

					/* Relay to device */
					memset(&sa, 0, sizeof(sa));
					size = recvfrom(socks[n], payload, sizeof(payload), 0, (struct sockaddr*)&rxsa, &addrlen);
					if (size < 0) {
						perror("recvfrom");
						return 1;
					}
					tinymac_send((uint8_t)n, payload, size);
				}
				pfd[n].revents = 0;
			}
		}

		/* Execute non-blocking tasks */
		tinymac_process();
	}

	for (n = 0; n < MAX_DEVICES; n++) {
		close(socks[n]);
	}
	sigaction(SIGINT, &old_sa, NULL);

	return 0;
}
