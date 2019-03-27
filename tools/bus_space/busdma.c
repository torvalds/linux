/*-
 * Copyright (c) 2015 Marcel Moolenaar
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
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "busdma.h"

#include "../../sys/dev/proto/proto_dev.h"

struct obj {
	int	oid;
	u_int	type;
#define	OBJ_TYPE_NONE	0
#define	OBJ_TYPE_TAG	1
#define	OBJ_TYPE_MD	2
#define	OBJ_TYPE_SEG	3
	u_int	refcnt;
	int	fd;
	struct obj *parent;
	u_long	key;
	union {
		struct {
			unsigned long	align;
			unsigned long	bndry;
			unsigned long	maxaddr;
			unsigned long	maxsz;
			unsigned long	maxsegsz;
			unsigned long	nsegs;
			unsigned long	datarate;
		} tag;
		struct {
			struct obj	*seg[3];
			int		nsegs[3];
#define	BUSDMA_MD_BUS	0
#define	BUSDMA_MD_PHYS	1
#define	BUSDMA_MD_VIRT	2
		} md;
		struct {
			struct obj	*next;
			unsigned long	address;
			unsigned long	size;
		} seg;
	} u;
};

static struct obj **oidtbl = NULL;
static int noids = 0;

static struct obj *
obj_alloc(u_int type)
{
	struct obj **newtbl, *obj;
	int oid;

	obj = calloc(1, sizeof(struct obj));
	obj->type = type;

	for (oid = 0; oid < noids; oid++) {
		if (oidtbl[oid] == 0)
			break;
	}
	if (oid == noids) {
		newtbl = realloc(oidtbl, sizeof(struct obj *) * (noids + 1));
		if (newtbl == NULL) {
			free(obj);
			return (NULL);
		}
		oidtbl = newtbl;
		noids++;
	}
	oidtbl[oid] = obj;
	obj->oid = oid;
	return (obj);
}

static int
obj_free(struct obj *obj)
{

	oidtbl[obj->oid] = NULL;
	free(obj);
	return (0);
}

static struct obj *
obj_lookup(int oid, u_int type)
{
	struct obj *obj;

	if (oid < 0 || oid >= noids) {
		errno = EINVAL;
		return (NULL);
	}
	obj = oidtbl[oid];
	if (obj->refcnt == 0) {
		errno = ENXIO;
		return (NULL);
	}
	if (type != OBJ_TYPE_NONE && obj->type != type) {
		errno = ENODEV;
		return (NULL);
	}
	return (obj);
}

static struct obj *
bd_tag_new(struct obj *ptag, int fd, u_long align, u_long bndry,
    u_long maxaddr, u_long maxsz, u_int nsegs, u_long maxsegsz,
    u_int datarate, u_int flags)
{
	struct proto_ioc_busdma ioc;
	struct obj *tag;

	tag = obj_alloc(OBJ_TYPE_TAG);
	if (tag == NULL)
		return (NULL);

	memset(&ioc, 0, sizeof(ioc));
	ioc.request = (ptag != NULL) ? PROTO_IOC_BUSDMA_TAG_DERIVE :
	    PROTO_IOC_BUSDMA_TAG_CREATE;
	ioc.key = (ptag != NULL) ? ptag->key : 0;
	ioc.u.tag.align = align;
	ioc.u.tag.bndry = bndry;
	ioc.u.tag.maxaddr = maxaddr;
	ioc.u.tag.maxsz = maxsz;
	ioc.u.tag.nsegs = nsegs;
	ioc.u.tag.maxsegsz = maxsegsz;
	ioc.u.tag.datarate = datarate;
	ioc.u.tag.flags = flags;
	if (ioctl(fd, PROTO_IOC_BUSDMA, &ioc) == -1) {
		obj_free(tag);
		return (NULL);
	}
	tag->refcnt = 1;
	tag->fd = fd;
	tag->parent = ptag;
	tag->key = ioc.result;
	tag->u.tag.align = ioc.u.tag.align;
	tag->u.tag.bndry = ioc.u.tag.bndry;
	tag->u.tag.maxaddr = ioc.u.tag.maxaddr;
	tag->u.tag.maxsz = ioc.u.tag.maxsz;
	tag->u.tag.maxsegsz = ioc.u.tag.maxsegsz;
	tag->u.tag.nsegs = ioc.u.tag.nsegs;
	tag->u.tag.datarate = ioc.u.tag.datarate;
	return (tag);
}

int
bd_tag_create(const char *dev, u_long align, u_long bndry, u_long maxaddr,
    u_long maxsz, u_int nsegs, u_long maxsegsz, u_int datarate, u_int flags)
{
	char path[PATH_MAX];
	struct obj *tag;
	int fd, len;

	len = snprintf(path, PATH_MAX, "/dev/proto/%s/busdma", dev);
	if (len >= PATH_MAX) {
		errno = EINVAL;
		return (-1);
	}
	fd = open(path, O_RDWR);
	if (fd == -1)
		return (-1);

	tag = bd_tag_new(NULL, fd, align, bndry, maxaddr, maxsz, nsegs,
	    maxsegsz, datarate, flags);
	if (tag == NULL) {
		close(fd);
		return (-1);
	}
	return (tag->oid);
}

int
bd_tag_derive(int ptid, u_long align, u_long bndry, u_long maxaddr,
    u_long maxsz, u_int nsegs, u_long maxsegsz, u_int datarate, u_int flags)
{
	struct obj *ptag, *tag;

	ptag = obj_lookup(ptid, OBJ_TYPE_TAG);
	if (ptag == NULL)
		return (-1);

	tag = bd_tag_new(ptag, ptag->fd, align, bndry, maxaddr, maxsz, nsegs,
	    maxsegsz, datarate, flags);
	if (tag == NULL)
		return (-1);
	ptag->refcnt++;
	return (tag->oid);
}

int
bd_tag_destroy(int tid)
{
	struct proto_ioc_busdma ioc;
	struct obj *ptag, *tag;

	tag = obj_lookup(tid, OBJ_TYPE_TAG);
	if (tag == NULL)
		return (errno);
	if (tag->refcnt > 1)
		return (EBUSY);

	memset(&ioc, 0, sizeof(ioc));
	ioc.request = PROTO_IOC_BUSDMA_TAG_DESTROY;
	ioc.key = tag->key;
	if (ioctl(tag->fd, PROTO_IOC_BUSDMA, &ioc) == -1)
		return (errno);

	if (tag->parent != NULL)
		tag->parent->refcnt--;
	else
		close(tag->fd);
	obj_free(tag);
	return (0);
}

static int
bd_md_add_seg(struct obj *md, int type, u_long addr, u_long size)
{
	struct obj *seg;

	seg = obj_alloc(OBJ_TYPE_SEG);
	if (seg == NULL)
		return (errno);
	seg->refcnt = 1;
	seg->parent = md;
	seg->u.seg.address = addr;
	seg->u.seg.size = size;

	md->u.md.seg[type] = seg;
	md->u.md.nsegs[type] = 1;
	return (0);
}

static int
bd_md_del_segs(struct obj *md, int type, int unmap)
{
	struct obj *seg, *seg0;

	for (seg = md->u.md.seg[type]; seg != NULL; seg = seg0) {
		if (unmap)
			munmap((void *)seg->u.seg.address, seg->u.seg.size);
		seg0 = seg->u.seg.next;
		obj_free(seg);
	}
	return (0);
}

int
bd_md_create(int tid, u_int flags)
{
	struct proto_ioc_busdma ioc;
	struct obj *md, *tag;

	tag = obj_lookup(tid, OBJ_TYPE_TAG);
	if (tag == NULL)
		return (-1);

	md = obj_alloc(OBJ_TYPE_MD);
	if (md == NULL)
		return (-1);

	memset(&ioc, 0, sizeof(ioc));
	ioc.request = PROTO_IOC_BUSDMA_MD_CREATE;
	ioc.u.md.tag = tag->key;
	ioc.u.md.flags = flags;
	if (ioctl(tag->fd, PROTO_IOC_BUSDMA, &ioc) == -1) {
		obj_free(md);
		return (-1);
	}

	md->refcnt = 1;
	md->fd = tag->fd;
	md->parent = tag;
	tag->refcnt++;
	md->key = ioc.result;
	return (md->oid);
}

int
bd_md_destroy(int mdid)
{
	struct proto_ioc_busdma ioc;
	struct obj *md;

	md = obj_lookup(mdid, OBJ_TYPE_MD);
	if (md == NULL)
		return (errno);

	memset(&ioc, 0, sizeof(ioc));
	ioc.request = PROTO_IOC_BUSDMA_MD_DESTROY;
	ioc.key = md->key;
	if (ioctl(md->fd, PROTO_IOC_BUSDMA, &ioc) == -1)
		return (errno);

	md->parent->refcnt--;
	obj_free(md);
	return (0);
}

int
bd_md_load(int mdid, void *buf, u_long len, u_int flags)
{
	struct proto_ioc_busdma ioc;
	struct obj *md;
	int error;

	md = obj_lookup(mdid, OBJ_TYPE_MD);
	if (md == NULL)
		return (errno);

	memset(&ioc, 0, sizeof(ioc));
	ioc.request = PROTO_IOC_BUSDMA_MD_LOAD;
	ioc.key = md->key;
	ioc.u.md.flags = flags;
	ioc.u.md.virt_addr = (uintptr_t)buf;
	ioc.u.md.virt_size = len;
	if (ioctl(md->fd, PROTO_IOC_BUSDMA, &ioc) == -1)
		return (errno);

	error = bd_md_add_seg(md, BUSDMA_MD_VIRT, ioc.u.md.virt_addr, len);
	error = bd_md_add_seg(md, BUSDMA_MD_PHYS, ioc.u.md.phys_addr, len);
	error = bd_md_add_seg(md, BUSDMA_MD_BUS, ioc.u.md.bus_addr, len);
	return (error);
}

int
bd_md_unload(int mdid)
{
	struct proto_ioc_busdma ioc;
	struct obj *md;
	int error;

	md = obj_lookup(mdid, OBJ_TYPE_MD);
	if (md == NULL)
		return (errno);

	memset(&ioc, 0, sizeof(ioc));
	ioc.request = PROTO_IOC_BUSDMA_MD_UNLOAD;
	ioc.key = md->key;
	if (ioctl(md->fd, PROTO_IOC_BUSDMA, &ioc) == -1)
		return (errno);

	bd_md_del_segs(md, BUSDMA_MD_VIRT, 0);
	bd_md_del_segs(md, BUSDMA_MD_PHYS, 0);
	bd_md_del_segs(md, BUSDMA_MD_BUS, 0);
	return (0);
}

int
bd_mem_alloc(int tid, u_int flags)
{
	struct proto_ioc_busdma ioc;
	struct obj *md, *tag;
	uintptr_t addr;
	int error;

	tag = obj_lookup(tid, OBJ_TYPE_TAG);
	if (tag == NULL)
		return (-1);

	md = obj_alloc(OBJ_TYPE_MD);
	if (md == NULL)
		return (-1);

	memset(&ioc, 0, sizeof(ioc));
	ioc.request = PROTO_IOC_BUSDMA_MEM_ALLOC;
	ioc.u.md.tag = tag->key;
	ioc.u.md.flags = flags;
	if (ioctl(tag->fd, PROTO_IOC_BUSDMA, &ioc) == -1) {
		obj_free(md);
		return (-1);
	}

	md->refcnt = 1;
	md->fd = tag->fd;
	md->parent = tag;
	tag->refcnt++;
	md->key = ioc.result;

	/* XXX we need to support multiple segments */
	assert(ioc.u.md.phys_nsegs == 1);
	assert(ioc.u.md.bus_nsegs == 1);
	error = bd_md_add_seg(md, BUSDMA_MD_PHYS, ioc.u.md.phys_addr,
	    tag->u.tag.maxsz);
	error = bd_md_add_seg(md, BUSDMA_MD_BUS, ioc.u.md.bus_addr,
	    tag->u.tag.maxsz);

	addr = (uintptr_t)mmap(NULL, tag->u.tag.maxsz, PROT_READ | PROT_WRITE,
	    MAP_NOCORE | MAP_SHARED, md->fd, ioc.u.md.phys_addr);
	if (addr == (uintptr_t)MAP_FAILED)
		goto fail;
	error = bd_md_add_seg(md, BUSDMA_MD_VIRT, addr, tag->u.tag.maxsz);

	return (md->oid);

 fail:
	memset(&ioc, 0, sizeof(ioc));
	ioc.request = PROTO_IOC_BUSDMA_MEM_FREE;
	ioc.key = md->key;
	ioctl(md->fd, PROTO_IOC_BUSDMA, &ioc);
	md->parent->refcnt--;
	obj_free(md);
	return (-1);
}

int
bd_mem_free(int mdid)
{
	struct proto_ioc_busdma ioc;
	struct obj *md;

	md = obj_lookup(mdid, OBJ_TYPE_MD);
	if (md == NULL)
		return (errno);

	memset(&ioc, 0, sizeof(ioc));
	ioc.request = PROTO_IOC_BUSDMA_MEM_FREE;
	ioc.key = md->key;
	if (ioctl(md->fd, PROTO_IOC_BUSDMA, &ioc) == -1)
		return (errno);

	bd_md_del_segs(md, BUSDMA_MD_VIRT, 1);
	bd_md_del_segs(md, BUSDMA_MD_PHYS, 0);
	bd_md_del_segs(md, BUSDMA_MD_BUS, 0);
	md->parent->refcnt--;
	obj_free(md);
	return (0);
}

int
bd_md_first_seg(int mdid, int space)
{
	struct obj *md, *seg;

	md = obj_lookup(mdid, OBJ_TYPE_MD);
	if (md == NULL)
		return (-1);

	if (space != BUSDMA_MD_BUS && space != BUSDMA_MD_PHYS &&
	    space != BUSDMA_MD_VIRT) {
		errno = EINVAL;
		return (-1);
	}
	seg = md->u.md.seg[space];
	if (seg == NULL) {
		errno = ENXIO;
		return (-1);
	}
	return (seg->oid);
}

int
bd_md_next_seg(int mdid, int sid)
{
	struct obj *seg;

	seg = obj_lookup(sid, OBJ_TYPE_SEG);
	if (seg == NULL)
		return (-1);

	seg = seg->u.seg.next;
	if (seg == NULL) {
		errno = ENXIO;
		return (-1);
	}
	return (seg->oid);
}

int
bd_seg_get_addr(int sid, u_long *addr_p)
{
	struct obj *seg;

	if (addr_p == NULL)
		return (EINVAL);

	seg = obj_lookup(sid, OBJ_TYPE_SEG);
	if (seg == NULL)
		return (errno);

	*addr_p = seg->u.seg.address;
	return (0);
}

int
bd_seg_get_size(int sid, u_long *size_p)
{
	struct obj *seg;

	if (size_p == NULL)
		return (EINVAL);

	seg = obj_lookup(sid, OBJ_TYPE_SEG);
	if (seg == NULL)
		return (errno);

	*size_p = seg->u.seg.size;
	return (0);
}

int
bd_sync(int mdid, u_int op, u_long ofs, u_long len)
{
	struct proto_ioc_busdma ioc;
	struct obj *md;

	md = obj_lookup(mdid, OBJ_TYPE_MD);
	if (md == NULL)
		return (errno);

	memset(&ioc, 0, sizeof(ioc));
	ioc.request = PROTO_IOC_BUSDMA_SYNC;
	ioc.key = md->key;
	ioc.u.sync.op = op;
	ioc.u.sync.base = ofs;
	ioc.u.sync.size = len;
	if (ioctl(md->fd, PROTO_IOC_BUSDMA, &ioc) == -1)
		return (errno);

	return (0);
}
