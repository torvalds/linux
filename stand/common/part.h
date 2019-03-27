/*-
 * Copyright (c) 2012 Andrey V. Elsukov <ae@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _PART_H_
#define	_PART_H_

struct ptable;

enum ptable_type {
	PTABLE_NONE,
	PTABLE_BSD,
	PTABLE_MBR,
	PTABLE_GPT,
	PTABLE_VTOC8,
	PTABLE_ISO9660
};

enum partition_type {
	PART_UNKNOWN,
	PART_EFI,
	PART_FREEBSD,
	PART_FREEBSD_BOOT,
	PART_FREEBSD_NANDFS,
	PART_FREEBSD_UFS,
	PART_FREEBSD_ZFS,
	PART_FREEBSD_SWAP,
	PART_FREEBSD_VINUM,
	PART_LINUX,
	PART_LINUX_SWAP,
	PART_DOS,
	PART_ISO9660
};

struct ptable_entry {
	uint64_t		start;
	uint64_t		end;
	int			index;
	enum partition_type	type;
};

/* The offset and size are in sectors */
typedef int (diskread_t)(void *arg, void *buf, size_t blocks, uint64_t offset);
typedef int (ptable_iterate_t)(void *arg, const char *partname,
    const struct ptable_entry *part);

struct ptable *ptable_open(void *dev, uint64_t sectors, uint16_t sectorsize,
    diskread_t *dread);
void ptable_close(struct ptable *table);
enum ptable_type ptable_gettype(const struct ptable *table);
int ptable_getsize(const struct ptable *table, uint64_t *sizep);

int ptable_getpart(const struct ptable *table, struct ptable_entry *part,
    int index);
int ptable_getbestpart(const struct ptable *table, struct ptable_entry *part);

int ptable_iterate(const struct ptable *table, void *arg,
    ptable_iterate_t *iter);
const char *parttype2str(enum partition_type type);

#endif	/* !_PART_H_ */
