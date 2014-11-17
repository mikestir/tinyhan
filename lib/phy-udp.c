/*
 * Copyright 2013-2014 Mike Stirling
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of the Tiny Home Area Network stack.
 *
 * http://www.tinyhan.co.uk/
 *
 * phy-udp.c
 *
 * Simulation PHY driver which uses UDP multicast for evaluating the
 * MAC protocol on a PC.
 *
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "phy.h"

#define MULTICAST_GROUP		"239.0.0.1"
#define UDP_PORT			10400
#define MAX_PACKET			256

static const uint16_t phy_crc_table[] = {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
        0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
        0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
        0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
        0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
        0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
        0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
        0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
        0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
        0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
        0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
        0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
        0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
        0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
        0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
        0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
        0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
        0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
        0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
        0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
        0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
        0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
        0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
        0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
        0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
        0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
        0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
        0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
        0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
        0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
        0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
        0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
};

static int phy_sock;
static phy_recv_cb_t phy_recv_cb = NULL;
static boolean_t phy_listening = FALSE;

static void update_crc(uint16_t *crc, const uint8_t *buf, size_t size) {
	while (size--) {
		*crc = (*crc << 8) ^ phy_crc_table[(*crc >> 8) ^ *buf++];
	}
}


int phy_init(void)
{
	struct sockaddr_in sa;
	struct ip_mreq group;
	int zero = 0, one = 1;

	phy_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (phy_sock < 0) {
		perror("socket");
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
	sa.sin_port = htons(UDP_PORT);

	/* Allow multiple instances to bind the same port */
	setsockopt(phy_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	if (bind(phy_sock, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
		perror("bind");
		close(phy_sock);
		return -1;
	}

	/* Join multicast group with loopback */
	group.imr_multiaddr.s_addr = inet_addr(MULTICAST_GROUP);
	group.imr_interface.s_addr = INADDR_ANY;
	setsockopt(phy_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group));
	setsockopt(phy_sock, IPPROTO_IP, IP_MULTICAST_LOOP, &one, sizeof(one));

	/* Conceptual listen/standby mode simply throws away packets when we're supposed to
	 * be asleep */
	phy_listening = TRUE;

	return 0;
}

int phy_suspend(void)
{
	return 0;
}

int phy_resume(void)
{
	return 0;
}

int phy_listen(void)
{
	phy_listening = TRUE;
	return 0;
}

int phy_standby(void)
{
	/* FIXME: Add test functionality for sleepy nodes */
	//phy_listening = FALSE;
	return 0;
}

int phy_delayed_standby(uint16_t us)
{
	/* FIXME: Add test functionality for sleepy nodes */
	//phy_listening = FALSE;
	return 0;
}

void phy_register_recv_cb(phy_recv_cb_t cb)
{
	phy_recv_cb = cb;
}

void phy_event_handler(void)
{
	char payload[MAX_PACKET];
	uint16_t ourcrc = 0, *theircrc;
	struct pollfd pfd;
	struct sockaddr_in sa;
	socklen_t addrlen = sizeof(sa);
	int rc;

	/* phy_event_handler is non-blocking, so poll with immediate timeout */
	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = phy_sock;
	pfd.events = POLLIN;
	rc = poll(&pfd, 1, 0);
	if (rc > 0) {
		int size;

		/* Read packet and pass to receive handler */
		memset(&sa, 0, sizeof(sa));
		size = recvfrom(phy_sock, payload, sizeof(payload), 0, (struct sockaddr*)&sa, &addrlen);
		if (size < 0) {
			perror("recvfrom");
			return;
		}

		if (size < 2) {
			/* Must have at least a CRC */
			ERROR("short packet\n");
			return;
		}

		/* Validate CRC */
		update_crc(&ourcrc, payload, size - 2);
		theircrc = (uint16_t*)&payload[size - 2];
		if (ourcrc != *theircrc) {
			ERROR("crc error\n");
			return;
		}

		if (phy_recv_cb && phy_listening) {
			phy_recv_cb(payload, (size_t)size - 2, PHY_RSSI_NONE);
		}
	}
}

int phy_send(phy_buf_t *bufs, unsigned int nbufs, uint8_t flags)
{
	struct sockaddr_in sa;
	uint16_t crc;
	unsigned int size, n;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(MULTICAST_GROUP);
	sa.sin_port = htons(UDP_PORT);

	/* Calculate CRC and total size for all fragments */
	size = 0;
	crc = 0;
	for (n = 0; n < nbufs; n++) {
		size += bufs[n].size;
		update_crc(&crc, bufs[n].buf, bufs[n].size);
	}

	/* Send datagram */
	for (n = 0; n < nbufs; n++) {
		sendto(phy_sock, bufs[n].buf, bufs[n].size, MSG_MORE, (struct sockaddr*)&sa, sizeof(sa));
	}
	sendto(phy_sock, &crc, sizeof(crc), 0, (struct sockaddr*)&sa, sizeof(sa));
	return 0;
}

int phy_set_power(int dbm)
{
	return 0;
}

int phy_set_channel(unsigned int n)
{
	return 0;
}

unsigned int phy_get_mtu(void)
{
	return MAX_PACKET - 2; /* CRC takes up two bytes */
}

int phy_get_fd(void)
{
	return phy_sock;
}
