/*	$OpenBSD: usbdevs.c,v 1.36 2022/12/28 21:30:19 jmc Exp $	*/
/*	$NetBSD: usbdevs.c,v 1.19 2002/02/21 00:34:31 christos Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (augustss@netbsd.org).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <dev/usb/usb.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <vis.h>
#include <string.h>
#include <unistd.h>

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#define MINIMUM(a, b) (((a) < (b)) ? (a) : (b))

#define USBDEV "/dev/usb"

int verbose = 0;
char done[USB_MAX_DEVICES];

void usage(void);
void dump_device(int, uint8_t);
void dump_controller(char *, int, uint8_t);
int main(int, char **);

extern char *__progname;

void
usage(void)
{
	fprintf(stderr, "usage: %s [-v] [-a addr] [-d usbdev]\n", __progname);
	exit(1);
}

void
dump_device(int fd, uint8_t addr)
{
	struct usb_device_info di;
	int i;
	char vv[sizeof(di.udi_vendor)*4], vp[sizeof(di.udi_product)*4];
	char vr[sizeof(di.udi_release)*4], vs[sizeof(di.udi_serial)*4];

	di.udi_addr = addr;
	if (ioctl(fd, USB_DEVICEINFO, &di) == -1) {
		if (errno != ENXIO)
			warn("addr %u", addr);
		return;
	}

	done[addr] = 1;

	strvis(vv, di.udi_vendor, VIS_CSTYLE);
	strvis(vp, di.udi_product, VIS_CSTYLE);
	printf("addr %02u: %04x:%04x %s, %s", addr,
	    di.udi_vendorNo, di.udi_productNo,
	    vv, vp);

	if (verbose) {
		printf("\n\t ");
		switch (di.udi_speed) {
		case USB_SPEED_LOW:
			printf("low speed");
			break;
		case USB_SPEED_FULL:
			printf("full speed");
			break;
		case USB_SPEED_HIGH:
			printf("high speed");
			break;
		case USB_SPEED_SUPER:
			printf("super speed");
			break;
		default:
			break;
		}

		if (di.udi_power)
			printf(", power %d mA", di.udi_power);
		else
			printf(", self powered");

		if (di.udi_config)
			printf(", config %d", di.udi_config);
		else
			printf(", unconfigured");

		strvis(vr, di.udi_release, VIS_CSTYLE);
		printf(", rev %s", vr);

		if (di.udi_serial[0] != '\0') {
			strvis(vs, di.udi_serial, VIS_CSTYLE);
			printf(", iSerial %s", vs);
		}
	}
	printf("\n");

	if (verbose)
		for (i = 0; i < USB_MAX_DEVNAMES; i++)
			if (di.udi_devnames[i][0] != '\0')
				printf("\t driver: %s\n", di.udi_devnames[i]);

	if (verbose > 1) {
		int port, nports;

		nports = MINIMUM(di.udi_nports, nitems(di.udi_ports));
		for (port = 0; port < nports; port++) {
			uint16_t status, change;

			status = di.udi_ports[port] & 0xffff;
			change = di.udi_ports[port] >> 16;

			printf("\t port %02u: %04x.%04x", port+1, change,
			    status);

			if (status & UPS_CURRENT_CONNECT_STATUS)
				printf(" connect");

			if (status & UPS_PORT_ENABLED)
				printf(" enabled");

			if (status & UPS_SUSPEND)
				printf(" suspend");

			if (status & UPS_OVERCURRENT_INDICATOR)
				printf(" overcurrent");

			if (di.udi_speed < USB_SPEED_SUPER) {
				if (status & UPS_PORT_L1)
					printf(" l1");

				if (status & UPS_PORT_POWER)
					printf(" power");
			} else {
				if (status & UPS_PORT_POWER_SS)
					printf(" power");

				switch (UPS_PORT_LS_GET(status)) {
				case UPS_PORT_LS_U0:
					printf(" U0");
					break;
				case UPS_PORT_LS_U1:
					printf(" U1");
					break;
				case UPS_PORT_LS_U2:
					printf(" U2");
					break;
				case UPS_PORT_LS_U3:
					printf(" U3");
					break;
				case UPS_PORT_LS_SS_DISABLED:
					printf(" SS.disabled");
					break;
				case UPS_PORT_LS_RX_DETECT:
					printf(" Rx.detect");
					break;
				case UPS_PORT_LS_SS_INACTIVE:
					printf(" ss.inactive");
					break;
				case UPS_PORT_LS_POLLING:
					printf(" polling");
					break;
				case UPS_PORT_LS_RECOVERY:
					printf(" recovery");
					break;
				case UPS_PORT_LS_HOT_RESET:
					printf(" hot.reset");
					break;
				case UPS_PORT_LS_COMP_MOD:
					printf(" comp.mod");
					break;
				case UPS_PORT_LS_LOOPBACK:
					printf(" loopback");
					break;
				}
			}

			printf("\n");
		}
	}
}

void
dump_controller(char *name, int fd, uint8_t addr)
{
	memset(done, 0, sizeof(done));

	if (addr) {
		dump_device(fd, addr);
		return;
	}

	printf("Controller %s:\n", name);
	for (addr = 1; addr < USB_MAX_DEVICES; addr++)
		if (!done[addr])
			dump_device(fd, addr);
}

int
main(int argc, char **argv)
{
	int ch, fd;
	char *controller = NULL;
	uint8_t addr = 0;
	const char *errstr;

	while ((ch = getopt(argc, argv, "a:d:v")) != -1) {
		switch (ch) {
		case 'a':
			addr = strtonum(optarg, 1, USB_MAX_DEVICES-1, &errstr);
			if (errstr)
				errx(1, "addr %s", errstr);
			break;
		case 'd':
			controller = optarg;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (unveil("/dev", "r") == -1)
		err(1, "unveil /dev");
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	if (controller == NULL) {
		int i;
		int ncont = 0;

		for (i = 0; i < 10; i++) {
			char path[PATH_MAX];

			snprintf(path, sizeof(path), "%s%d", USBDEV, i);
			if ((fd = open(path, O_RDONLY)) < 0) {
				if (errno != ENOENT && errno != ENXIO)
					warn("%s", path);
				continue;
			}

			dump_controller(path, fd, addr);
			close(fd);
			ncont++;
		}
		if (verbose && ncont == 0)
			printf("%s: no USB controllers found\n",
			    __progname);
	} else {
		if ((fd = open(controller, O_RDONLY)) < 0)
			err(1, "%s", controller);

		dump_controller(controller, fd, addr);
		close(fd);
	}

	return 0;
}
