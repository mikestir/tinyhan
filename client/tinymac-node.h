/*
 * mac.h
 *
 *  Created on: 12 Aug 2014
 *      Author: mike
 */

#ifndef TINYMAC_NODE_H_
#define TINYMAC_NODE_H_

#include <stdint.h>

#include "common.h"
#include "tinymac.h"

typedef void (*tinymac_recv_cb_t)(uint8_t src, const char *payload, size_t size);

int tinymac_init(uint64_t uuid);
void tinymac_process(void);

void tinymac_register_recv_cb(tinymac_recv_cb_t cb);

/*!
 * Send a data packet
 *
 * \param dest		Destination short address
 * \param buf		Pointer to payload data
 * \param size		Size of payload data
 * \return			Sequence number or -ve error code
 */
int tinymac_send(uint8_t dest, const char *buf, size_t size);

#endif /* MAC_H_ */
