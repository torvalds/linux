/*	$OpenBSD: ophandlers.c,v 1.18 2023/03/08 04:43:13 guenther Exp $	*/
/*	$NetBSD: ophandlers.c,v 1.2 1996/02/28 01:13:30 thorpej Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vis.h>

#include <machine/openpromio.h>

#include "defs.h"

extern	char *path_openprom;
extern	int eval;
extern	int verbose;

static	char err_str[BUFSIZE];

static	void op_notsupp(struct extabent *, struct opiocdesc *, char *);
static	void op_print(char *);

/*
 * There are several known fields that I either don't know how to
 * deal with or require special treatment.
 */
static	struct extabent opextab[] = {
	{ "security-password",		op_notsupp },
	{ "security-mode",		op_notsupp },
	{ "oem-logo",			op_notsupp },
	{ NULL,				op_notsupp },
};

#define BARF(str1, str2) {						\
	snprintf(err_str, sizeof err_str, "%s: %s", (str1), (str2));	\
	++eval;								\
	return (err_str);						\
};

char *
op_handler(char *keyword, char *arg)
{
	struct opiocdesc opio;
	struct extabent *ex;
	char opio_buf[BUFSIZE];
	int fd, optnode;

	if ((fd = open(path_openprom, arg ? O_RDWR : O_RDONLY)) == -1)
		BARF(path_openprom, strerror(errno));

	/* Check to see if it's a special-case keyword. */
	for (ex = opextab; ex->ex_keyword != NULL; ++ex)
		if (strcmp(ex->ex_keyword, keyword) == 0)
			break;

	if (ioctl(fd, OPIOCGETOPTNODE, (char *)&optnode) == -1)
		BARF("OPIOCGETOPTNODE", strerror(errno));

	bzero(&opio_buf[0], sizeof(opio_buf));
	bzero(&opio, sizeof(opio));
	opio.op_nodeid = optnode;
	opio.op_name = keyword;
	opio.op_namelen = strlen(opio.op_name);

	if (arg) {
		if (verbose) {
			printf("old: ");

			opio.op_buf = &opio_buf[0];
			opio.op_buflen = sizeof(opio_buf);
			if (ioctl(fd, OPIOCGET, (char *)&opio) == -1)
				BARF("OPIOCGET", strerror(errno));

			if (opio.op_buflen <= 0) {
				printf("nothing available for %s\n", keyword);
				goto out;
			}

			if (ex->ex_keyword != NULL)
				(*ex->ex_handler)(ex, &opio, NULL);
			else
				op_print(opio.op_buf);
		}
 out:
		if (ex->ex_keyword != NULL)
			(*ex->ex_handler)(ex, &opio, arg);
		else {
			opio.op_buf = arg;
			opio.op_buflen = strlen(arg);
		}

		if (ioctl(fd, OPIOCSET, (char *)&opio) == -1)
			BARF("invalid keyword", keyword);

		if (verbose) {
			printf("new: ");
			if (ex->ex_keyword != NULL)
				(*ex->ex_handler)(ex, &opio, NULL);
			else
				op_print(opio.op_buf);
		}
	} else {
		opio.op_buf = &opio_buf[0];
		opio.op_buflen = sizeof(opio_buf);
		if (ioctl(fd, OPIOCGET, (char *)&opio) == -1)
			BARF("OPIOCGET", strerror(errno));

		if (opio.op_buflen <= 0) {
			snprintf(err_str, sizeof err_str,
			    "nothing available for %s",
			    keyword);
			return (err_str);
		}

		if (ex->ex_keyword != NULL)
			(*ex->ex_handler)(ex, &opio, NULL);
		else {
			printf("%s=", keyword);
			op_print(opio.op_buf);
		}
	}

	(void)close(fd);
	return (NULL);
}

static void
op_notsupp(struct extabent *exent, struct opiocdesc *opiop, char *arg)
{

	warnx("property `%s' not yet supported", exent->ex_keyword);
}

/*
 * XXX: This code is quite ugly.  You have been warned.
 * (Really!  This is the only way I could get it to work!)
 */
void
op_dump(void)
{
	struct opiocdesc opio1, opio2;
	struct extabent *ex;
	char buf1[BUFSIZE], buf2[BUFSIZE], buf3[BUFSIZE], buf4[BUFSIZE];
	int fd, optnode;

	if ((fd = open(path_openprom, O_RDONLY)) == -1)
		err(1, "open: %s", path_openprom);

	if (ioctl(fd, OPIOCGETOPTNODE, (char *)&optnode) == -1)
		err(1, "OPIOCGETOPTNODE");

	bzero(&opio1, sizeof(opio1));

	/* This will grab the first property name from OPIOCNEXTPROP. */
	bzero(buf1, sizeof(buf1));
	bzero(buf2, sizeof(buf2));

	opio1.op_nodeid = opio2.op_nodeid = optnode;

	opio1.op_name = buf1;
	opio1.op_buf = buf2;

	opio2.op_name = buf3;
	opio2.op_buf = buf4;

	/*
	 * For reference: opio1 is for obtaining the name.  Pass the
	 * name of the last property read in op_name, and the next one
	 * will be returned in op_buf.  To get the first name, pass
	 * an empty string.  There are no more properties when an
	 * empty string is returned.
	 *
	 * opio2 is for obtaining the value associated with that name.
	 * For some crazy reason, it seems as if we need to do all
	 * of that gratuitous zapping and copying.  *sigh*
	 */
	for (;;) {
		opio1.op_namelen = strlen(opio1.op_name);
		opio1.op_buflen = sizeof(buf2);

		if (ioctl(fd, OPIOCNEXTPROP, (char *)&opio1) == -1)
			err(1, "ioctl: OPIOCNEXTPROP");

		/*
		 * The name of the property we wish to get the
		 * value for has been stored in the value field
		 * of opio1.  If the length of the name is 0, there
		 * are no more properties left.
		 */
		strlcpy(opio2.op_name, opio1.op_buf, sizeof(buf3));
		opio2.op_namelen = strlen(opio2.op_name);

		if (opio2.op_namelen == 0) {
			(void)close(fd);
			return;
		}

		bzero(opio2.op_buf, sizeof(buf4));
		opio2.op_buflen = sizeof(buf4);

		if (ioctl(fd, OPIOCGET, (char *)&opio2) == -1)
			err(1, "ioctl: OPIOCGET");

		for (ex = opextab; ex->ex_keyword != NULL; ++ex)
			if (strcmp(ex->ex_keyword, opio2.op_name) == 0)
				break;

		if (ex->ex_keyword != NULL)
			(*ex->ex_handler)(ex, &opio2, NULL);
		else {
			printf("%s=", opio2.op_name);
			op_print(opio2.op_buf);
		}

		/*
		 * Place the name of the last read value back into
		 * opio1 so that we may obtain the next name.
		 */
		bzero(opio1.op_name, sizeof(buf1));
		bzero(opio1.op_buf, sizeof(buf2));
		strlcpy(opio1.op_name, opio2.op_name, sizeof(buf1));
	}
	/* NOTREACHED */
}

static void
op_print(char *op_buf)
{
	char *vistr;
	size_t size;

	size = 1 + 4 * strlen(op_buf);
	vistr = malloc(size);
	if (vistr == NULL)
		printf("(out of memory)\n");
	else {
		strnvis(vistr, op_buf, size, VIS_NL | VIS_TAB | VIS_OCTAL);
		printf("%s\n", vistr);
		free(vistr);
	}
}
