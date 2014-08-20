/*
 * tinyhan-test.c
 *
 *  Created on: 19 Aug 2014
 *      Author: mike
 */

#include <stdlib.h>
#include <poll.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "tinymac.h"
#include "phy.h"

int main(int argc, char **argv)
{
	boolean_t coord = FALSE;


	srand(time(NULL) + getpid());

	if (argc > 1) {
		coord = TRUE;
	}

	tinymac_init(rand(), coord);
	if (coord) {
		tinymac_permit_attach(TRUE);
	}
	phy_init();

	while (1) {
		struct pollfd pfd;
		int rc;

		/* Wait for activity */
		memset(&pfd, 0, sizeof(pfd));
		pfd.fd = phy_get_fd();
		pfd.events = POLLIN;
		rc = poll(&pfd, 1, 1000);

		tinymac_process();
	}

	return 0;
}
