/*-
 * Copyright (c) 2005-2006,2016 John H. Baldwin <jhb@FreeBSD.org>
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

#include <sys/ioccom.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysdecode.h>

static void
usage(char **av)
{
	fprintf(stderr, "%s: <ioctl> [ ... ]\n", av[0]);
	exit(1);
}

int
main(int ac, char **av)
{
	unsigned long cmd;
	const char *name;
	char *cp;
	int group, i;

	if (ac < 2)
		usage(av);
	printf("  command :  dir  group num  len name\n");
	for (i = 1; i < ac; i++) {
		errno = 0;
		cmd = strtoul(av[i], &cp, 0);
		if (*cp != '\0' || errno != 0) {
			fprintf(stderr, "Invalid integer: %s\n", av[i]);
			usage(av);
		}
		printf("0x%08lx: ", cmd);
		switch (cmd & IOC_DIRMASK) {
		case IOC_VOID:
			printf("VOID ");
			break;
		case IOC_OUT:
			printf("OUT  ");
			break;
		case IOC_IN:
			printf("IN   ");
			break;
		case IOC_INOUT:
			printf("INOUT");
			break;
		default:
			printf("%01lx ???", (cmd & IOC_DIRMASK) >> 29);
			break;
		}
		printf(" ");
		group = IOCGROUP(cmd);
		if (isprint(group))
			printf(" '%c' ", group);
		else
			printf(" 0x%02x", group);
		printf(" %3lu %4lu", cmd & 0xff, IOCPARM_LEN(cmd));
		name = sysdecode_ioctlname(cmd);
		if (name != NULL)
			printf(" %s", name);
		printf("\n");
	}
	return (0);
}
