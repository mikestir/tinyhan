/*
 * tinyhan-sniffer.c
 *
 *  Created on: 19 Aug 2014
 *      Author: mike
 */

#include <stdlib.h>
#include <poll.h>
#include <sys/time.h>
#include <string.h>

#include "common.h"
#include "tinymac.h"
#include "phy.h"
#include "ansi.h"

static double timestamp(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec + (double)tv.tv_usec * 1.0e-6;
}

#define log(f, hdr, ...)		printf("%0.2f: %04X N: %02X D:%02X S:%02X [%03u] - " f ATTR_RESET "\n", timestamp(), hdr->flags, hdr->net_id, hdr->dest_addr, hdr->src_addr, hdr->seq, ##__VA_ARGS__)

void rx_func(const char *buf, size_t size)
{
	const tinymac_header_t *hdr = (const tinymac_header_t*)buf;

	if (size < sizeof(tinymac_header_t)) {
		printf("%0.2f: " BG_RED FG_WHITE "SHORT" ATTR_RESET "\n", timestamp());
		return;
	}

	switch (hdr->flags & TINYMAC_FLAGS_TYPE_MASK) {
	case tinymacType_Beacon: {
		const tinymac_beacon_t *beacon = (const tinymac_beacon_t*)hdr->payload;
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_beacon_t)) {
			log(BG_RED FG_WHITE "SHORT (Beacon)", hdr);
			return;
		}
		if (beacon->flags & TINYMAC_BEACON_FLAGS_PERMIT_ATTACH) {
			log(FG_GREEN "%s Beacon from %02X %016llX (Attach permitted)", hdr,
					(beacon->flags & TINYMAC_BEACON_FLAGS_SYNC) ? "Sync" : "Advertisement",
					hdr->net_id, beacon->uuid);
		} else {
			log(FG_YELLOW "%s Beacon from %02X %016llX", hdr,
					(beacon->flags & TINYMAC_BEACON_FLAGS_SYNC) ? "Sync" : "Advertisement",
					hdr->net_id, beacon->uuid);
		}
	} break;
	case tinymacType_BeaconRequest: {
		log("Beacon request", hdr);
	} break;
	case tinymacType_Ack: {
		log("Acknowledgement for [%03u]", hdr, hdr->seq);
	} break;
	case tinymacType_RegistrationRequest: {
		const tinymac_registration_request_t *attach = (const tinymac_registration_request_t*)hdr->payload;
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_registration_request_t)) {
			log(BG_RED FG_WHITE "SHORT (registration request)", hdr);
			return;
		}
		log("Registration request from %016llX", hdr, attach->uuid);
	} break;
	case tinymacType_DeregistrationRequest: {
		const tinymac_deregistration_request_t *detach = (const tinymac_deregistration_request_t*)hdr->payload;
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_deregistration_request_t)) {
			log(BG_RED FG_WHITE "SHORT (detach request)", hdr);
			return;
		}
		log("Detachment request from %016llX (reason=%u)", hdr, detach->uuid, detach->reason);
	} break;
	case tinymacType_RegistrationResponse: {
		const tinymac_registration_response_t *addr = (const tinymac_registration_response_t*)hdr->payload;
		if (size < sizeof(tinymac_header_t) + sizeof(tinymac_registration_response_t)) {
			log(BG_RED FG_WHITE "SHORT (address update)", hdr);
			return;
		}
		log(BG_GREEN FG_RED "Address assignment %02X %02X to device %016llX", hdr, hdr->net_id, addr->addr, addr->uuid);
	} break;
	default:
		log("", hdr);
	}
}

int main(int argc, char **argv)
{
	phy_init();
	phy_register_recv_cb(rx_func);

	while (1) {
		struct pollfd pfd;
		int rc;

		/* Wait for activity */
		memset(&pfd, 0, sizeof(pfd));
		pfd.fd = phy_get_fd();
		pfd.events = POLLIN;
		rc = poll(&pfd, 1, 1000);

		phy_process();
	}

	return 0;
}
