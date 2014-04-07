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
#include <arpa/inet.h>

#undef DEBUG
#include "common.h"
#include "packet.h"

/* Socket is global - this is supposed to emulate an embedded device
 * communicating over a simple connectionless radio (of which there is
 * only one) */
static int sockfd;

int packet_init(const char *host, const char *port)
{
	struct addrinfo hints;
	struct addrinfo *addr, *thisaddr;

	/* Resolve host address */
	memset(&hints, 0, sizeof(hints));
	//hints.ai_family = AF_UNSPEC;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (host == NULL) {
		hints.ai_flags = AI_PASSIVE; /* To use INADDR_ANY */
	}
	if (getaddrinfo(host, port, &hints, &addr) != 0) {
		ERROR("getaddrinfo\n");
		return -1;
	}

	/* Try all returned addresses until we get a connection */
	for (thisaddr = addr; thisaddr; thisaddr = thisaddr->ai_next) {
		sockfd = socket(thisaddr->ai_family, thisaddr->ai_socktype, thisaddr->ai_protocol);
		if (sockfd < 0)
			continue;
		if (host) {
			/* Connect to remote host */
			INFO("Connecting to remote host: %s:%s\n", host, port);
			if (connect(sockfd, thisaddr->ai_addr, thisaddr->ai_addrlen) == 0) {
				break;
			}
		} else {
			/* Bind for server operation */
			INFO("Binding server to port: %s\n", port);
			if (bind(sockfd, thisaddr->ai_addr, thisaddr->ai_addrlen) == 0) {
				break;
			}
		}
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

/* Server extensions */

int packet_sendto(uint64_t tag, const char *buf, size_t size)
{
	struct sockaddr_in addr;
	char ipstr[INET6_ADDRSTRLEN];

	/* Decode tag to address/port */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = (uint16_t)(tag & 0xffff);
	addr.sin_addr.s_addr = (uint32_t)(tag >> 16);

	TRACE("sendto %s\n", inet_ntop(addr.sin_family, &addr.sin_addr, ipstr, sizeof(ipstr)),
			ntohs(addr.sin_port));
	DUMP(buf, size);
	return sendto(sockfd, buf, size, 0, &addr, sizeof(addr));
}

int packet_recvfrom(uint64_t *tag, char *buf, size_t maxsize)
{
	struct sockaddr_storage addr;
	unsigned int addrlen;
	int rc;
	char ipstr[INET6_ADDRSTRLEN];

	addrlen = sizeof(addr);
	rc = recvfrom(sockfd, buf, maxsize, MSG_DONTWAIT, (struct sockaddr*)&addr, &addrlen);
	if (rc > 0) {
		TRACE("recvfrom %s:%u\n", inet_ntop(addr.ss_family, (addr.ss_family == AF_INET) ?
				&((struct sockaddr_in*)&addr)->sin_addr :
				&((struct sockaddr_in6*)&addr)->sin6_addr, ipstr, sizeof(ipstr)),
				ntohs(((struct sockaddr_in*)&addr)->sin_port));
		DUMP(buf, rc);

		/*
		 * Generate tag to identify the client at the transport layer - in this
		 * case we use the IP address and port number
		 */
		if (addr.ss_family == AF_INET) {
			*tag = ((uint64_t)((struct sockaddr_in*)&addr)->sin_addr.s_addr << 16) | (((struct sockaddr_in*)&addr)->sin_port);
			TRACE("tag = %llX\n", *tag);
		} else {
			*tag = 0; // FIXME:
		}

	}
	return rc;
}


