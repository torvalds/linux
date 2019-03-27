/*-
 * Copyright (c) 2018, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <paths.h>
#include <err.h>
#include <syslog.h>
#include <libsecureboot.h>
#include <libveriexec.h>

#include "veriexec.h"

int dev_fd = -1;
int ForceFlags = 0;
int Verbose = 0;
int VeriexecVersion = 0;

const char *Cdir = NULL;

static int
veriexec_load(const char *manifest)
{
	unsigned char *content;
	int rc;

	content = verify_signed(manifest, VEF_VERBOSE);
	if (!content)
		errx(EX_USAGE, "cannot verify %s", manifest);
	if (manifest_open(manifest, content)) {
		rc = yyparse();
	} else {
		err(EX_NOINPUT, "cannot load %s", manifest);
	}
	free(content);
	return (rc);
}

int
main(int argc, char *argv[])
{
	unsigned long ctl;
	int c;
	int x;

	dev_fd = open(_PATH_DEV_VERIEXEC, O_WRONLY, 0);

	while ((c = getopt(argc, argv, "C:i:x:vz:")) != -1) {
		switch (c) {
		case 'C':
			Cdir = optarg;
			break;
		case 'i':
			if (dev_fd < 0) {
				err(EX_UNAVAILABLE, "cannot open veriexec");
			}
			if (ioctl(dev_fd, VERIEXEC_GETSTATE, &x)) {
				err(EX_UNAVAILABLE,
				    "Cannot get veriexec state");
			}
			switch (optarg[0]) {
			case 'a':	/* active */
				ctl = VERIEXEC_STATE_ACTIVE;
				break;
			case 'e':	/* enforce */
				ctl = VERIEXEC_STATE_ENFORCE;
				break;
			case 'l':	/* loaded/locked */
				ctl = (strncmp(optarg, "lock", 4)) ?
				    VERIEXEC_STATE_LOCKED :
				    VERIEXEC_STATE_LOADED;
				break;
			default:
				errx(EX_USAGE, "unknown state %s", optarg);
				break;
			}
			exit((x & ctl) == 0);
			break;
		case 'v':
			Verbose++;
			break;
		case 'x':
			/*
			 * -x says all other args are paths to check.
			 */
			for (x = 0; optind < argc; optind++) {
				if (veriexec_check_path(argv[optind])) {
					warn("%s", argv[optind]);
					x = 2;
				}
			}
			exit(x);
			break;
		case 'z':
			switch (optarg[0]) {
			case 'a':	/* active */
				ctl = VERIEXEC_ACTIVE;
				break;
			case 'd':	/* debug* */
				ctl = (strstr(optarg, "off")) ?
				    VERIEXEC_DEBUG_OFF : VERIEXEC_DEBUG_ON;
				if (optind < argc && ctl == VERIEXEC_DEBUG_ON) {
					x = atoi(argv[optind]);
					if (x == 0)
						ctl = VERIEXEC_DEBUG_OFF;
				}
				break;
			case 'e':	/* enforce */
				ctl = VERIEXEC_ENFORCE;
				break;
			case 'g':
				ctl = VERIEXEC_GETSTATE; /* get state */
				break;
			case 'l':	/* lock */
				ctl = VERIEXEC_LOCK;
				break;
			default:
				errx(EX_USAGE, "unknown command %s", optarg);
				break;
			}
			if (dev_fd < 0) {
				err(EX_UNAVAILABLE, "cannot open veriexec");
			}
			if (ioctl(dev_fd, ctl, &x)) {
				err(EX_UNAVAILABLE, "cannot %s veriexec", optarg);
			}
			if (ctl == VERIEXEC_DEBUG_ON ||
			    ctl == VERIEXEC_DEBUG_OFF) {
				printf("debug is: %d\n", x);
			} else if (ctl == VERIEXEC_GETSTATE) {
				printf("%#o\n", x);
			}
			exit(EX_OK);
			break;
		}
	}
	openlog(getprogname(), LOG_PID, LOG_AUTH);
	if (ve_trust_init() < 1)
		errx(EX_OSFILE, "cannot initialize trust store");
#ifdef VERIEXEC_GETVERSION
	if (ioctl(dev_fd, VERIEXEC_GETVERSION, &VeriexecVersion)) {
		VeriexecVersion = 0;	/* unknown */
	}
#endif

	for (; optind < argc; optind++) {
		if (veriexec_load(argv[optind])) {
			err(EX_DATAERR, "cannot load %s", argv[optind]);
		}
	}
	exit(EX_OK);
}
