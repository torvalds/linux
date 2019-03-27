/*-
 * Copyright (c) 2002 by Thomas Moestl <tmm@FreeBSD.org>.
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

#include <sys/types.h>
#include <sys/ioctl.h>

#include <dev/ofw/openfirmio.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "pathnames.h"
#include "ofw_util.h"

#define	OFW_IOCTL(fd, cmd, val)	do {					\
	if (ioctl(fd, cmd, val) == -1)					\
		err(EX_IOERR, "ioctl(..., " #cmd ", ...) failed");	\
} while (0)

int
ofw_open(int mode)
{
	int fd;

	if ((fd = open(PATH_DEV_OPENFIRM, mode)) == -1)
		err(EX_UNAVAILABLE, "could not open " PATH_DEV_OPENFIRM);
	return (fd);
}

void
ofw_close(int fd)
{

	close(fd);
}

phandle_t
ofw_root(int fd)
{

	return (ofw_peer(fd, 0));
}

phandle_t
ofw_optnode(int fd)
{
	phandle_t rv;

	OFW_IOCTL(fd, OFIOCGETOPTNODE, &rv);
	return (rv);
}

phandle_t
ofw_peer(int fd, phandle_t node)
{
	phandle_t rv;

	rv = node;
	OFW_IOCTL(fd, OFIOCGETNEXT, &rv);
	return (rv);
}

phandle_t
ofw_child(int fd, phandle_t node)
{
	phandle_t rv;

	rv = node;
	OFW_IOCTL(fd, OFIOCGETCHILD, &rv);
	return (rv);
}

phandle_t
ofw_finddevice(int fd, const char *name)
{
	struct ofiocdesc d;

	d.of_nodeid = 0;
	d.of_namelen = strlen(name);
	d.of_name = name;
	d.of_buflen = 0;
	d.of_buf = NULL;
	if (ioctl(fd, OFIOCFINDDEVICE, &d) == -1) {
		if (errno == ENOENT)
			err(EX_UNAVAILABLE, "Node '%s' not found", name);
		else
			err(EX_IOERR,
			    "ioctl(..., OFIOCFINDDEVICE, ...) failed");
	}
	return (d.of_nodeid);
}

int
ofw_firstprop(int fd, phandle_t node, char *buf, int buflen)
{

	return (ofw_nextprop(fd, node, NULL, buf, buflen));
}

int
ofw_nextprop(int fd, phandle_t node, const char *prev, char *buf, int buflen)
{
	struct ofiocdesc d;

	d.of_nodeid = node;
	d.of_namelen = prev != NULL ? strlen(prev) : 0;
	d.of_name = prev;
	d.of_buflen = buflen;
	d.of_buf = buf;
	if (ioctl(fd, OFIOCNEXTPROP, &d) == -1) {
		if (errno == ENOENT)
			return (0);
		else
			err(EX_IOERR, "ioctl(..., OFIOCNEXTPROP, ...) failed");
	}
	return (d.of_buflen);
}

static void *
ofw_malloc(int size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(EX_OSERR, "malloc() failed");
	return (p);
}

int
ofw_getprop(int fd, phandle_t node, const char *name, void *buf, int buflen)
{
	struct ofiocdesc d;

	d.of_nodeid = node;
	d.of_namelen = strlen(name);
	d.of_name = name;
	d.of_buflen = buflen;
	d.of_buf = buf;
	OFW_IOCTL(fd, OFIOCGET, &d);
	return (d.of_buflen);
}

int
ofw_setprop(int fd, phandle_t node, const char *name, const void *buf,
    int buflen)
{
	struct ofiocdesc d;

	d.of_nodeid = node;
	d.of_namelen = strlen(name);
	d.of_name = name;
	d.of_buflen = buflen;
	d.of_buf = ofw_malloc(buflen);
	memcpy(d.of_buf, buf, buflen);
	OFW_IOCTL(fd, OFIOCSET, &d);
	free(d.of_buf);
	return (d.of_buflen);
}

int
ofw_getproplen(int fd, phandle_t node, const char *name)
{
	struct ofiocdesc d;

	d.of_nodeid = node;
	d.of_namelen = strlen(name);
	d.of_name = name;
	OFW_IOCTL(fd, OFIOCGETPROPLEN, &d);
	return (d.of_buflen);
}

int
ofw_getprop_alloc(int fd, phandle_t node, const char *name, void **buf,
    int *buflen, int reserve)
{
	struct ofiocdesc d;
	int len, rv;

	do {
		len = ofw_getproplen(fd, node, name);
		if (len < 0)
			return (len);
		if (*buflen < len + reserve) {
			if (*buf != NULL)
				free(*buf);
			*buflen = len + reserve + OFIOCMAXVALUE;
			*buf = ofw_malloc(*buflen);
		}
		d.of_nodeid = node;
		d.of_namelen = strlen(name);
		d.of_name = name;
		d.of_buflen = *buflen - reserve;
		d.of_buf = *buf;
		rv = ioctl(fd, OFIOCGET, &d);
	} while (rv == -1 && errno == ENOMEM);
	if (rv == -1)
		err(EX_IOERR, "ioctl(..., OFIOCGET, ...) failed");
	return (d.of_buflen);
}
