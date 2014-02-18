/*
 * packet_udp.c
 *
 *  Created on: 15 Feb 2014
 *      Author: mike
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <poll.h>

#include "common.h"
#include "packet.h"

#define HOST		"localhost"
#define PORT		"1883"

/* Socket is global - this is supposed to emulate an embedded device
 * communicating over a simple connectionless radio (of which there is
 * only one) */
static int sockfd;

int packet_init(void)
{
	struct addrinfo hints;
	struct addrinfo *addr, *thisaddr;

	/* Resolve host address */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(HOST, PORT, &hints, &addr) != 0) {
		ERROR("getaddrinfo\n");
		return -1;
	}

	/* Try all returned addresses until we get a connection */
	for (thisaddr = addr; thisaddr; thisaddr = thisaddr->ai_next) {
		sockfd = socket(thisaddr->ai_family, thisaddr->ai_socktype, thisaddr->ai_protocol);
		if (sockfd < 0)
			continue;
		if (connect(sockfd, thisaddr->ai_addr, thisaddr->ai_addrlen) == 0)
			break;
		close(sockfd);
	}
	freeaddrinfo(addr);

	if (thisaddr == NULL) {
		ERROR("broker connection failed\n");
		return -1;
	}

	return 0;
}

int packet_poll(unsigned int timeout)
{
	struct pollfd fd;

	memset(&fd, 0, sizeof(fd));
	fd.fd = sockfd;
	fd.events = POLLIN;
	return poll(&fd, 1, timeout);
}

int packet_send(const char *buf, size_t size)
{
	TRACE("send\n");
	DUMP(buf, size);
	return send(sockfd, buf, size, 0);
}

int packet_recv(char *buf, size_t size)
{
	int rc;
	rc = recv(sockfd, buf, size, MSG_DONTWAIT);
	if (rc > 0) {
		TRACE("recv\n");
		DUMP(buf, rc);
	}
	return rc;
}

