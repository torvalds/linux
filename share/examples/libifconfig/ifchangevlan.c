/*
 * Copyright (c) 2017, Marie Helene Kvello-Aune
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * thislist of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <err.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libifconfig.h>

#include <net/if_vlan_var.h>

int
main(int argc, char *argv[])
{
	char *ifname, *parentif;
	unsigned short vlantag;
	const char *errstr;
	ifconfig_handle_t *lifh;

	if (argc != 4) {
		errx(EINVAL, "Invalid number of arguments."
		    " Should provide exactly three arguments: "
		    "INTERFACE, PARENT_INTERFACE and VLAN_TAG.");
	}

	/* We have a static number of arguments. Therefore we can do it simple. */
	ifname = strdup(argv[1]);
	parentif = strdup(argv[2]);
	vlantag = strtonum(argv[3], 0, USHRT_MAX, &errstr);

	if (errstr != NULL) {
		errx(1, "VLAN_TAG must be between 0 and %i.\n", USHRT_MAX);
	}

	printf("Interface: %s\nNew VLAN tag: %i\n", ifname, vlantag);

	lifh = ifconfig_open();
	if (lifh == NULL) {
		errx(ENOMEM, "Failed to open libifconfig handle.");
		return (-1);
	}

	if (ifconfig_set_vlantag(lifh, ifname, parentif, vlantag) == 0) {
		printf("Successfully changed vlan tag.\n");
		ifconfig_close(lifh);
		lifh = NULL;
		free(ifname);
		free(parentif);
		return (0);
	}

	switch (ifconfig_err_errtype(lifh)) {
	case SOCKET:
		warnx("couldn't create socket. This shouldn't happen.\n");
		break;
	case IOCTL:
		if (ifconfig_err_ioctlreq(lifh) == SIOCGETVLAN) {
			warnx("Target interface isn't a VLAN interface.\n");
		}
		if (ifconfig_err_ioctlreq(lifh) == SIOCSETVLAN) {
			warnx(
				"Couldn't change VLAN properties of interface.\n");
		}
		break;
	default:
		warnx(
			"This is a thorough example accommodating for temporary"
			" 'not implemented yet' errors. That's likely what happened"
			" now. If not, your guess is as good as mine. ;)"
			" Error code: %d\n", ifconfig_err_errno(
				lifh));
		break;
	}

	ifconfig_close(lifh);
	lifh = NULL;
	free(ifname);
	free(parentif);
	return (-1);
}
