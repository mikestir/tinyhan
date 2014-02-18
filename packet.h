/*
 * packet.h
 *
 *  Created on: 15 Feb 2014
 *      Author: mike
 */

#ifndef PACKET_H_
#define PACKET_H_

#include <stddef.h>

int packet_init(void);

/*!
 * Poll for incoming data with timeout
 *
 * \param timeout		Timeout /ms
 * \return				0 on timeout, >0 on read available, <0 on error
 */
int packet_poll(unsigned int timeout);

int packet_send(const char *buf, size_t size);

/*!
 * Packet receive should return an entire packet or nothing
 * (non-blocking)
 *
 * \param buf			Pointer to buffer to be filled in
 * \param maxsize		Size of buffer
 * \return				Number of bytes returned, -1 on error or no packet available
 */
int packet_recv(char *buf, size_t maxsize);

#endif /* PACKET_H_ */
