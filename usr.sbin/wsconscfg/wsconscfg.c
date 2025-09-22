/* $OpenBSD: wsconscfg.c,v 1.19 2024/11/06 17:14:03 miod Exp $ */
/* $NetBSD: wsconscfg.c,v 1.4 1999/07/29 18:24:10 augustss Exp $ */

/*
 * Copyright (c) 1999
 *	Matthias Drochner.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

#include <dev/wscons/wsconsio.h>

#define DEFDEV "/dev/ttyCcfg"

static void usage(void);
int main(int, char**);

static void
usage(void)
{
	extern char *__progname;

	(void)fprintf(stderr,
	    "usage: %s [-dFgkm] [-e emul] [-f ctldev] [-t type] index\n",
	    __progname);
	exit(1);
}


int
main(int argc, char *argv[])
{
	char *wsdev;
	int c, delete, get, kbd, idx, wsfd, res, mux;
	struct wsdisplay_addscreendata asd;
	struct wsdisplay_delscreendata dsd;
	struct wsmux_device wmd;

	wsdev = DEFDEV;
	delete = 0;
	get = 0;
	kbd = 0;
	mux = 0;
	asd.screentype[0] = 0;
	asd.emul[0] = 0;
	dsd.flags = 0;

	while ((c = getopt(argc, argv, "f:dgkmt:e:F")) != -1) {
		switch (c) {
		case 'f':
			wsdev = optarg;
			break;
		case 'g':
			get = 1;
			break;
		case 'd':
			delete = 1;
			break;
		case 'k':
			kbd = 1;
			break;
		case 'm':
			mux = 1;
			kbd = 1;
			break;
		case 't':
			strlcpy(asd.screentype, optarg, WSSCREEN_NAME_SIZE);
			break;
		case 'e':
			strlcpy(asd.emul, optarg, WSEMUL_NAME_SIZE);
			break;
		case 'F':
			dsd.flags |= WSDISPLAY_DELSCR_FORCE;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if ((kbd && get) ||
	    ((kbd || get) ? (argc > 1) : (argc != 1)))
		usage();

	idx = -1;
	if (argc > 0 && sscanf(argv[0], "%d", &idx) != 1)
		errx(1, "invalid index");

	wsfd = open(wsdev, get ? O_RDONLY : O_RDWR);
	if (wsfd < 0)
		err(2, "%s", wsdev);

	if (kbd) {
		if (mux)
			wmd.type = WSMUX_MUX;
		else
			wmd.type = WSMUX_KBD;
		wmd.idx = idx;
		if (delete) {
			res = ioctl(wsfd, WSMUXIO_REMOVE_DEVICE, &wmd);
			if (res < 0)
				err(3, "WSMUXIO_REMOVE_DEVICE");
		} else {
			res = ioctl(wsfd, WSMUXIO_ADD_DEVICE, &wmd);
			if (res < 0)
				err(3, "WSMUXIO_ADD_DEVICE");
		}
	} else if (delete) {
		dsd.idx = idx;
		res = ioctl(wsfd, WSDISPLAYIO_DELSCREEN, &dsd);
		if (res < 0)
			err(3, "WSDISPLAYIO_DELSCREEN");
	} else if (get) {
		asd.idx = idx;
		res = ioctl(wsfd, WSDISPLAYIO_GETSCREEN, &asd);
		if (res < 0)
			err(3, "WSDISPLAYIO_GETSCREEN");
		else
			printf("%d\n", asd.idx);
	} else {
		asd.idx = idx;
		res = ioctl(wsfd, WSDISPLAYIO_ADDSCREEN, &asd);
		if (res < 0) {
			if (errno == EBUSY)
				errx(3, "screen %d is already configured", idx);
			else
				err(3, "WSDISPLAYIO_ADDSCREEN");
		}
	}

	return (0);
}
