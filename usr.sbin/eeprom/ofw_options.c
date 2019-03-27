/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marius Strobl
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Handlers for Open Firmware /options node.
 */

#include <sys/types.h>

#include <dev/ofw/openfirm.h>

#include <err.h>
#include <fcntl.h>
#include <readpassphrase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "ofw_options.h"
#include "ofw_util.h"

#define	OFWO_LOGO	512
#define	OFWO_MAXPROP	31
#define	OFWO_MAXPWD	8

struct ofwo_extabent {
	const char	*ex_prop;
	int		(*ex_handler)(const struct ofwo_extabent *, int,
			    const void *, int, const char *);
};

static int	ofwo_oemlogo(const struct ofwo_extabent *, int, const void *,
		    int, const char *);
static int	ofwo_secmode(const struct ofwo_extabent *, int, const void *,
		    int, const char *);
static int	ofwo_secpwd(const struct ofwo_extabent *, int, const void *,
		    int, const char *);

static const struct ofwo_extabent ofwo_extab[] = {
	{ "oem-logo",			ofwo_oemlogo },
	{ "security-mode",		ofwo_secmode },
	{ "security-password",		ofwo_secpwd },
	{ NULL,				NULL }
};

static int	ofwo_setpass(int);
static int	ofwo_setstr(int, const void *, int, const char *, const char *);

static __inline void
ofwo_printprop(const char *prop, const char* buf, int buflen)
{

	printf("%s: %.*s\n", prop, buflen, buf);
}

static int
ofwo_oemlogo(const struct ofwo_extabent *exent, int fd, const void *buf,
    int buflen, const char *val)
{
	int lfd;
	char logo[OFWO_LOGO + 1];

	if (val) {
		if (val[0] == '\0')
			ofw_setprop(fd, ofw_optnode(fd), exent->ex_prop, "", 1);
		else {
			if ((lfd = open(val, O_RDONLY)) == -1) {
				warn("could not open '%s'", val);
				return (EX_USAGE);
			}
			if (read(lfd, logo, OFWO_LOGO) != OFWO_LOGO ||
			    lseek(lfd, 0, SEEK_END) != OFWO_LOGO) {
				close(lfd);
				warnx("logo '%s' has wrong size.", val);
				return (EX_USAGE);
			}
			close(lfd);
			logo[OFWO_LOGO] = '\0';
			if (ofw_setprop(fd, ofw_optnode(fd), exent->ex_prop,
			    logo, OFWO_LOGO + 1) != OFWO_LOGO)
				errx(EX_IOERR, "writing logo failed.");
		}
	} else
		if (buflen != 0)
			printf("%s: <logo data>\n", exent->ex_prop);
		else
			ofwo_printprop(exent->ex_prop, (const char *)buf,
			    buflen);
	return (EX_OK);
}

static int
ofwo_secmode(const struct ofwo_extabent *exent, int fd, const void *buf,
    int buflen, const char *val)
{
	int res;

	if (val) {
		if (strcmp(val, "full") == 0 || strcmp(val, "command") == 0) {
			if ((res = ofwo_setpass(fd)) != EX_OK)
				return (res);
			if ((res = ofwo_setstr(fd, buf, buflen, exent->ex_prop,
			    val)) != EX_OK)
				ofw_setprop(fd, ofw_optnode(fd),
				    "security-password", "", 1);
			return (res);
		}
		if (strcmp(val, "none") == 0) {
			ofw_setprop(fd, ofw_optnode(fd), "security-password",
			    "", 1);
			return (ofwo_setstr(fd, buf, buflen, exent->ex_prop,
			    val));
		}
		return (EX_DATAERR);
	} else
		ofwo_printprop(exent->ex_prop, (const char *)buf, buflen);
	return (EX_OK);
}

static int
ofwo_secpwd(const struct ofwo_extabent *exent, int fd, const void *buf,
    int buflen, const char *val)
{
	void *pbuf;
	int len, pblen, rv;

	pblen = 0;
	rv = EX_OK;
	pbuf = NULL;
	if (val) {
		len = ofw_getprop_alloc(fd, ofw_optnode(fd), "security-mode",
		    &pbuf, &pblen, 1);
		if (len <= 0 || strncmp("none", (char *)pbuf, len) == 0) {
			rv = EX_CONFIG;
			warnx("no security mode set.");
		} else if (strncmp("command", (char *)pbuf, len) == 0 ||
		    strncmp("full", (char *)pbuf, len) == 0) {
			rv = ofwo_setpass(fd);
		} else {
			rv = EX_CONFIG;
			warnx("invalid security mode.");
		}
	} else
		ofwo_printprop(exent->ex_prop, (const char *)buf, buflen);
	if (pbuf != NULL)
		free(pbuf);
	return (rv);
}

static int
ofwo_setpass(int fd)
{
	char pwd1[OFWO_MAXPWD + 1], pwd2[OFWO_MAXPWD + 1];

	if (readpassphrase("New password:", pwd1, sizeof(pwd1),
	    RPP_ECHO_OFF | RPP_REQUIRE_TTY) == NULL ||
	    readpassphrase("Retype new password:", pwd2, sizeof(pwd2),
	    RPP_ECHO_OFF | RPP_REQUIRE_TTY) == NULL)
		errx(EX_USAGE, "failed to get password.");
	if (strlen(pwd1) == 0) {
		printf("Password unchanged.\n");
		return (EX_OK);
	}
	if (strcmp(pwd1, pwd2) != 0) {
		printf("Mismatch - password unchanged.\n");
		return (EX_USAGE);
	}
	ofw_setprop(fd, ofw_optnode(fd), "security-password", pwd1,
	    strlen(pwd1) + 1);
	return (EX_OK);
}

static int
ofwo_setstr(int fd, const void *buf, int buflen, const char *prop,
    const char *val)
{
	void *pbuf;
	int len, pblen, rv;
	phandle_t optnode;
	char *oval;

	pblen = 0;
	rv = EX_OK;
	pbuf = NULL;
	optnode = ofw_optnode(fd);
	ofw_setprop(fd, optnode, prop, val, strlen(val) + 1);
	len = ofw_getprop_alloc(fd, optnode, prop, &pbuf, &pblen, 1);
	if (len < 0 || strncmp(val, (char *)pbuf, len) != 0) {
		/*
		 * The value is too long for this property and the OFW has
		 * truncated it to fit or the value is illegal and a legal
		 * one has been written instead (e.g. attempted to write
		 * "foobar" to a "true"/"false"-property) - try to recover
		 * the old value.
		 */
		rv = EX_DATAERR;
		if ((oval = malloc(buflen + 1)) == NULL)
			err(EX_OSERR, "malloc() failed.");
		strncpy(oval, buf, buflen);
		oval[buflen] = '\0';
		len = ofw_setprop(fd, optnode, prop, oval, buflen + 1);
		if (len != buflen)
			errx(EX_IOERR, "recovery of old value failed.");
		free(oval);
		goto out;
	}
	printf("%s: %.*s%s->%s%.*s\n", prop, buflen, (const char *)buf,
	    buflen > 0 ? " " : "", len > 0 ? " " : "", len, (char *)pbuf);
out:
	if (pbuf != NULL)
		free(pbuf);
	return (rv);
}

void
ofwo_dump(void)
{
	void *pbuf;
	int fd, len, nlen, pblen;
	phandle_t optnode;
	char prop[OFWO_MAXPROP + 1];
	const struct ofwo_extabent *ex;

	pblen = 0;
	pbuf = NULL;
	fd = ofw_open(O_RDONLY);
	optnode = ofw_optnode(fd);
	for (nlen = ofw_firstprop(fd, optnode, prop, sizeof(prop)); nlen != 0;
	    nlen = ofw_nextprop(fd, optnode, prop, prop, sizeof(prop))) {
		len = ofw_getprop_alloc(fd, optnode, prop, &pbuf, &pblen, 1);
		if (len < 0)
			continue;
		if (strcmp(prop, "name") == 0)
			continue;
		for (ex = ofwo_extab; ex->ex_prop != NULL; ++ex)
			if (strcmp(ex->ex_prop, prop) == 0)
				break;
		if (ex->ex_prop != NULL)
			(*ex->ex_handler)(ex, fd, pbuf, len, NULL);
		else
			ofwo_printprop(prop, (char *)pbuf, len);
	}
	if (pbuf != NULL)
		free(pbuf);
	ofw_close(fd);
}

int
ofwo_action(const char *prop, const char *val)
{
	void *pbuf;
	int fd, len, pblen, rv;
	const struct ofwo_extabent *ex;

	pblen = 0;
	rv = EX_OK;
	pbuf = NULL;
	if (strcmp(prop, "name") == 0)
		return (EX_UNAVAILABLE);
	if (val)
		fd = ofw_open(O_RDWR);
	else
		fd = ofw_open(O_RDONLY);
	len = ofw_getprop_alloc(fd, ofw_optnode(fd), prop, &pbuf, &pblen, 1);
	if (len < 0) {
		rv = EX_UNAVAILABLE;
		goto out;
	}
	for (ex = ofwo_extab; ex->ex_prop != NULL; ++ex)
		if (strcmp(ex->ex_prop, prop) == 0)
			break;
	if (ex->ex_prop != NULL)
		rv = (*ex->ex_handler)(ex, fd, pbuf, len, val);
	else if (val)
		rv = ofwo_setstr(fd, pbuf, len, prop, val);
	else
		ofwo_printprop(prop, (char *)pbuf, len);
out:
	if (pbuf != NULL)
		free(pbuf);
	ofw_close(fd);
	return (rv);
}
