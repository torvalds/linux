/*-
 * Copyright (c) 2014 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bus.h"

#include "../../sys/dev/proto/proto_dev.h"

struct resource {
	int	rid;
	int	fd;
	long	addr;
	long	size;
	off_t	ofs;
	caddr_t	ptr;
};

static struct resource *ridtbl = NULL;
static int nrids = 0;

static int
rid_alloc(void)
{
	void *newtbl;
	int rid;

	for (rid = 0; rid < nrids; rid++) {
		if (ridtbl[rid].fd == -1)
			break;
	}
	if (rid == nrids) {
		nrids++;
		newtbl = realloc(ridtbl, sizeof(struct resource) * nrids);
		if (newtbl == NULL) {
			nrids--;
			return (-1);
		} else
			ridtbl = newtbl;
	}
	ridtbl[rid].fd = INT_MAX;
	return (rid);
}

static struct resource *
rid_lookup(int rid)
{
	struct resource *r;

	if (rid < 0 || rid >= nrids) {
		errno = EINVAL;
		return (NULL);
	}
	r = ridtbl + rid;
	if (r->fd == -1) {
		errno = ENXIO;
		return (NULL);
	}
	return (r);
}

int
bs_map(const char *dev, const char *res)
{
	char path[PATH_MAX];
	struct proto_ioc_region region;
	struct resource *r;
	int len, rid;

	len = snprintf(path, PATH_MAX, "/dev/proto/%s/%s", dev, res);
	if (len >= PATH_MAX) {
		errno = EINVAL;
		return (-1);
	}
	rid = rid_alloc();
	if (rid == -1)
		return (-1);
	r = rid_lookup(rid);
	if (r == NULL)
		return (-1);
	r->fd = open(path, O_RDWR);
	if (r->fd == -1)
		return (-1);
	r->rid = -1;
	if (ioctl(r->fd, PROTO_IOC_REGION, &region) == -1) {
		close(r->fd);
		r->fd = -1;
		return (-1);
	}
	r->addr = region.address;
	r->size = region.size;
	r->ofs = 0;
	r->ptr = mmap(NULL, r->size, PROT_READ | PROT_WRITE,
	    MAP_NOCORE | MAP_SHARED, r->fd, r->ofs);
	return (rid);
}

int
bs_read(int rid, off_t ofs, void *buf, ssize_t bufsz)
{
	struct resource *r;
	volatile void *ptr;
	off_t o;
	ssize_t s;

	r = rid_lookup(rid);
	if (r == NULL)
		return (0);
	if (ofs < 0 || ofs > r->size - bufsz) {
		errno = ESPIPE;
		return (0);
	}
	ofs += r->ofs;
	if (r->ptr != MAP_FAILED) {
		ptr = r->ptr + ofs;
		switch (bufsz) {
		case 1:
			*((uint8_t *)buf) = *((volatile uint8_t *)ptr);
			break;
		case 2:
			*((uint16_t *)buf) = *((volatile uint16_t *)ptr);
			break;
		case 4:
			*((uint32_t *)buf) = *((volatile uint32_t *)ptr);
			break;
		default:
			errno = EIO;
			return (0);
		}
	} else {
		o = lseek(r->fd, ofs, SEEK_SET);
		if (o != ofs)
			return (0);
		s = read(r->fd, buf, bufsz);
		if (s != bufsz)
			return (0);
	}
	return (1);
}

int
bs_subregion(int rid0, long ofs, long sz)
{
	struct resource *r;
	void *ptr0;
	long addr0, ofs0;
	int fd0, rid;

	r = rid_lookup(rid0);
	if (r == NULL)
		return (-1);
	if (ofs < 0 || sz < 1) {
		errno = EINVAL;
		return (-1);
	}
	if (ofs + sz > r->size) {
		errno = ENOSPC;
		return (-1);
	}
	fd0 = r->fd;
	addr0 = r->addr;
	ofs0 = r->ofs;
	ptr0 = r->ptr;
	rid = rid_alloc();
	if (rid == -1)
		return (-1);
	r = rid_lookup(rid);
	if (r == NULL)
		return (-1);
	r->rid = rid0;
	r->fd = fd0;
	r->addr = addr0 + ofs;
	r->size = sz;
	r->ofs = ofs0 + ofs;
	r->ptr = ptr0;
	return (rid);
}

int
bs_unmap(int rid)
{
	struct resource *r;

	r = rid_lookup(rid);
	if (r == NULL)
		return (0);
	if (r->rid == -1) {
		if (r->ptr != MAP_FAILED)
			munmap(r->ptr, r->size);
		close(r->fd);
	}
	r->fd = -1;
	return (1);
}

int
bs_write(int rid, off_t ofs, void *buf, ssize_t bufsz)
{
	struct resource *r;
	volatile void *ptr;
	off_t o;
	ssize_t s;

	r = rid_lookup(rid);
	if (r == NULL)
		return (0);
	if (ofs < 0 || ofs > r->size - bufsz) {
		errno = ESPIPE;
		return (0);
	}
	ofs += r->ofs;
	if (r->ptr != MAP_FAILED) {
		ptr = r->ptr + ofs;
		switch (bufsz) {
		case 1:
			*((volatile uint8_t *)ptr) = *((uint8_t *)buf);
			break;
		case 2:
			*((volatile uint16_t *)ptr) = *((uint16_t *)buf);
			break;
		case 4:
			*((volatile uint32_t *)ptr) = *((uint32_t *)buf);
			break;
		default:
			errno = EIO;
			return (0);
		}
	} else {
		o = lseek(r->fd, ofs, SEEK_SET);
		if (o != ofs)
			return (0);
		s = write(r->fd, buf, bufsz);
		if (s != bufsz)
			return (0);
	}
	return (1);
}
