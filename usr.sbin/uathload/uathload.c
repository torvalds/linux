/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

/*
 * Atheros AR5523 USB Station Firmware downloader.
 *
 *    uathload -d ugen-device [firmware-file]
 *
 * Intended to be called from devd on device discovery.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/endian.h>
#include <sys/mman.h>

#include <sys/ioctl.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>

#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* all fields are big endian */
struct uath_fwmsg {
	uint32_t	flags;
#define UATH_WRITE_BLOCK	(1 << 4)

	uint32_t	len;
#define UATH_MAX_FWBLOCK_SIZE	2048

	uint32_t	total;
	uint32_t	remain;
	uint32_t	rxtotal;
	uint32_t	pad[123];
} __packed;

#define UATH_DATA_TIMEOUT	10000
#define UATH_CMD_TIMEOUT	1000

#define	VERBOSE(_fmt, ...) do {			\
	if (verbose) {				\
		printf(_fmt, __VA_ARGS__);	\
		fflush(stdout);			\
	}					\
} while (0)

extern	uint8_t _binary_ar5523_bin_start;
extern	uint8_t _binary_ar5523_bin_end;

static int
getdevname(const char *udevname, char *msgdev, char *datadev)
{
	char *bn, *bnbuf, *dn, *dnbuf;

	dnbuf = strdup(udevname);
	if (dnbuf == NULL)
		return (-1);
	dn = dirname(dnbuf);
	bnbuf = strdup(udevname);
	if (bnbuf == NULL) {
		free(dnbuf);
		return (-1);
	}
	bn = basename(bnbuf);
	if (strncmp(bn, "ugen", 4) != 0) {
		free(dnbuf);
		free(bnbuf);
		return (-1);
	}
	bn += 4;

	/* NB: pipes are hardcoded */
	snprintf(msgdev, 256, "%s/usb/%s.1", dn, bn);
	snprintf(datadev, 256, "%s/usb/%s.2", dn, bn);
	free(dnbuf);
	free(bnbuf);
	return (0);
}

static void
usage(void)
{
	errx(-1, "usage: uathload [-v] -d devname [firmware]");
}

int
main(int argc, char *argv[])
{
	const char *fwname, *udevname;
	char msgdev[256], datadev[256];
	struct uath_fwmsg txmsg, rxmsg;
	char *txdata;
	struct stat sb;
	int msg, data, fw, timeout, b, c;
	int bufsize = 512, verbose = 0;
	ssize_t len;

	udevname = NULL;
	while ((c = getopt(argc, argv, "d:v")) != -1) {
		switch (c) {
		case 'd':
			udevname = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			/*NOTREACHED*/
		}
	}
	argc -= optind;
	argv += optind;

	if (udevname == NULL)
		errx(-1, "No device name; use -d to specify the ugen device");
	if (argc > 1)
		usage();

	if (argc == 1)
		fwname = argv[0];
	else
		fwname = _PATH_FIRMWARE "/ar5523.bin";
	fw = open(fwname, O_RDONLY, 0);
	if (fw < 0)
		err(-1, "open(%s)", fwname);
	if (fstat(fw, &sb) < 0)
		err(-1, "fstat(%s)", fwname);
	txdata = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fw, 0);
	if (txdata == MAP_FAILED)
		err(-1, "mmap(%s)", fwname);
	len = sb.st_size;
	/* XXX verify device is an AR5005 part */
	if (getdevname(udevname, msgdev, datadev))
		err(-1, "getdevname error");

	msg = open(msgdev, O_RDWR, 0);
	if (msg < 0)
		err(-1, "open(%s)", msgdev);
	timeout = UATH_DATA_TIMEOUT;
	if (ioctl(msg, USB_SET_RX_TIMEOUT, &timeout) < 0)
		err(-1, "%s: USB_SET_RX_TIMEOUT(%u)", msgdev, UATH_DATA_TIMEOUT);
	if (ioctl(msg, USB_SET_RX_BUFFER_SIZE, &bufsize) < 0)
		err(-1, "%s: USB_SET_RX_BUFFER_SIZE(%u)", msgdev, bufsize);

	data = open(datadev, O_WRONLY, 0);
	if (data < 0)
		err(-1, "open(%s)", datadev);
	timeout = UATH_DATA_TIMEOUT;
	if (ioctl(data, USB_SET_TX_TIMEOUT, &timeout) < 0)
		err(-1, "%s: USB_SET_TX_TIMEOUT(%u)", datadev,
		    UATH_DATA_TIMEOUT);

	VERBOSE("Load firmware %s to %s\n", fwname, udevname);

	bzero(&txmsg, sizeof (struct uath_fwmsg));
	txmsg.flags = htobe32(UATH_WRITE_BLOCK);
	txmsg.total = htobe32(len);

	b = 0;
	while (len > 0) {
		int mlen;

		mlen = len;
		if (mlen > UATH_MAX_FWBLOCK_SIZE)
			mlen = UATH_MAX_FWBLOCK_SIZE;
		txmsg.remain = htobe32(len - mlen);
		txmsg.len = htobe32(mlen);

		/* send firmware block meta-data */
		VERBOSE("send block %2u: %zd bytes remaining", b, len - mlen);
		if (write(msg, &txmsg, sizeof(txmsg)) != sizeof(txmsg)) {
			VERBOSE("%s", "\n");
			err(-1, "error sending msg (%s)", msgdev);
			break;
		}

		/* send firmware block data */
		VERBOSE("%s", "\n             : data...");
		if (write(data, txdata, mlen) != mlen) {
			VERBOSE("%s", "\n");
			err(-1, "error sending data (%s)", datadev);
			break;
		}

		/* wait for ack from firmware */
		VERBOSE("%s", "\n             : wait for ack...");
		bzero(&rxmsg, sizeof(rxmsg));
		if (read(msg, &rxmsg, sizeof(rxmsg)) != sizeof(rxmsg)) {
			VERBOSE("%s", "\n");
			err(-1, "error reading msg (%s)", msgdev);
			break;
		}

		VERBOSE("flags=0x%x total=%d\n",
		    be32toh(rxmsg.flags), be32toh(rxmsg.rxtotal));
		len -= mlen;
		txdata += mlen;
		b++;
	}
	sleep(1);
	close(fw);
	close(msg);
	close(data);
	return 0;
}
