/*	$OpenBSD: disk.h,v 1.7 2020/12/09 18:10:18 krw Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _DISK_H
#define _DISK_H

#include <sys/queue.h>

struct efi_diskinfo;
typedef struct efi_diskinfo *efi_diskinfo_t;

/* All the info on a disk we've found */
struct diskinfo {
	efi_diskinfo_t efi_info;
	bios_diskinfo_t bios_info;
	struct disklabel disklabel;
	struct sr_boot_volume *sr_vol;

	int (*diskio)(int, struct diskinfo *, u_int, int, void *);
	int (*strategy)(void *, int, daddr_t, size_t, void *, size_t *);

	dev_t bsddev, bootdev;

	TAILQ_ENTRY(diskinfo) list;
};
TAILQ_HEAD(disklist_lh, diskinfo);

/* Disk info for boot device. */
extern struct diskinfo *bootdev_dip;

/* List of probed disks. */
extern struct disklist_lh disklist;

void dump_diskinfo(void);

void check_hibernate(struct diskinfo *);

#endif /* _DISK_H */
