/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jonathan Lemon <jlemon@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/ttycom.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVDIR	"/dev/"

static void __dead2
usage(void)
{

	(void)fprintf(stderr, "%s\n%s\n%s\n%s\n",
	    "usage: conscontrol [list]",
	    "       conscontrol mute on | off",
	    "       conscontrol add | delete console",
	    "       conscontrol set | unset console");
	exit(1);
}

static void
consstatus(void)
{
	int mute;
	size_t len;
	char *buf, *p, *avail;

	len = sizeof(mute);
	if (sysctlbyname("kern.consmute", &mute, &len, NULL, 0) == -1)
		err(1, "kern.consmute retrieval failed");
	if (sysctlbyname("kern.console", NULL, &len, NULL, 0) == -1)
		err(1, "kern.console estimate failed");
	if ((buf = malloc(len)) == NULL)
		errx(1, "kern.console malloc failed");
	if (sysctlbyname("kern.console", buf, &len, NULL, 0) == -1)
		err(1, "kern.console retrieval failed");
	if ((avail = strchr(buf, '/')) == NULL)
		errx(1, "kern.console format not understood");
	p = avail;
	*avail++ = '\0';
	if (p != buf)
		*--p = '\0';			/* remove trailing ',' */
	p = avail + strlen(avail);
	if (p != avail)
		*--p = '\0';			/* remove trailing ',' */
	printf("Configured: %s\n", buf);
	printf(" Available: %s\n", avail);
	printf("    Muting: %s\n", mute ? "on" : "off");
	free(buf);
}

static void
consmute(const char *onoff)
{
	int mute;
	size_t len;

	if (strcmp(onoff, "on") == 0)
		mute = 1;
	else if (strcmp(onoff, "off") == 0)
		mute = 0;
	else
		usage();
	len = sizeof(mute);
	if (sysctlbyname("kern.consmute", NULL, NULL, &mute, len) == -1)
		err(1, "could not change console muting");
}

/*
 * The name we supply to the sysctls should be an entry in /dev.  If
 * the user has specified the full pathname in /dev, DTRT.  If he
 * specifies a name in some other directory, it's an error.
 */

static char*
stripdev(char *devnam)
{
	if (memcmp (devnam, DEVDIR, strlen(DEVDIR)) == 0)
		return (&devnam[strlen(DEVDIR)]);	    /* remove /dev */
	else if (strchr (devnam, '/')) {
		fprintf(stderr, "Not a device in /dev: %s\n", devnam);
		return (NULL);				    /* end of string */
	} else
		return (devnam);			    /* passed */
}

static void
consadd(char *devnam)
{
	size_t len;

	devnam = stripdev(devnam);
	if (devnam == NULL)
		return;
	len = strlen(devnam);
	if (sysctlbyname("kern.console", NULL, NULL, devnam, len) == -1)
		err(1, "could not add %s as a console", devnam);
}

static void
consdel(char *devnam)
{
	char *buf;
	size_t len;

	devnam = stripdev(devnam);
	if (devnam == NULL)
		return;
	len = strlen(devnam) + sizeof("-");
	if ((buf = malloc(len)) == NULL)
		errx(1, "malloc failed");
	buf[0] = '-';
	strcpy(buf + 1, devnam);
	if (sysctlbyname("kern.console", NULL, NULL, buf, len) == -1)
		err(1, "could not remove %s as a console", devnam);
	free(buf);
}

static void
consset(char *devnam, int flag)
{
	int ttyfd;

	ttyfd = open(devnam, O_RDONLY);
	if (ttyfd == -1)
		err(1, "opening %s", devnam);
	if (ioctl(ttyfd, TIOCCONS, &flag) == -1)
		err(1, "could not %s %s as virtual console",
		    flag ? "set" : "unset", devnam);
	close(ttyfd);
}

int
main(int argc, char **argv)
{

	if (getopt(argc, argv, "") != -1)
		usage();
	argc -= optind;
	argv += optind;

	if (argc > 0 && strcmp(argv[0], "list") != 0) {
		if (argc != 2)
			usage();
		else if (strcmp(argv[0], "mute") == 0)
			consmute(argv[1]);
		else if (strcmp(argv[0], "add") == 0)
			consadd(argv[1]);
		else if (strcmp(argv[0], "delete") == 0)
			consdel(argv[1]);
		else if (strcmp(argv[0], "set") == 0)
			consset(argv[1], 1);
		else if (strcmp(argv[0], "unset") == 0)
			consset(argv[1], 0);
		else
			usage();
	}
	consstatus();
	exit(0);
}
