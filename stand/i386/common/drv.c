/*-
 * Copyright (c) 1998 Robert Nordier
 * Copyright (c) 2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <btxv86.h>

#include "stand.h"
#include "rbx.h"
#include "drv.h"
#include "edd.h"

static struct edd_params params;

uint64_t
drvsize(struct dsk *dskp)
{

	params.len = sizeof(struct edd_params);
	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	v86.eax = 0x4800;
	v86.edx = dskp->drive;
	v86.ds = VTOPSEG(&params);
	v86.esi = VTOPOFF(&params);
	v86int();
	if (V86_CY(v86.efl)) {
		printf("error %u\n", v86.eax >> 8 & 0xff);
		return (0);
	}
	return (params.sectors);
}

static struct edd_packet packet;

int
drvread(struct dsk *dskp, void *buf, daddr_t lba, unsigned nblk)
{
	static unsigned c = 0x2d5c7c2f;

	if (!OPT_CHECK(RBX_QUIET))
		printf("%c\b", c = c << 8 | c >> 24);
	packet.len = sizeof(struct edd_packet);
	packet.count = nblk;
	packet.off = VTOPOFF(buf);
	packet.seg = VTOPSEG(buf);
	packet.lba = lba;
	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	v86.eax = 0x4200;
	v86.edx = dskp->drive;
	v86.ds = VTOPSEG(&packet);
	v86.esi = VTOPOFF(&packet);
	v86int();
	if (V86_CY(v86.efl)) {
		printf("%s: error %u lba %llu\n",
		    BOOTPROG, v86.eax >> 8 & 0xff, lba);
		return (-1);
	}
	return (0);
}

#if defined(GPT) || defined(ZFS)
int
drvwrite(struct dsk *dskp, void *buf, daddr_t lba, unsigned nblk)
{

	packet.len = sizeof(struct edd_packet);
	packet.count = nblk;
	packet.off = VTOPOFF(buf);
	packet.seg = VTOPSEG(buf);
	packet.lba = lba;
	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	v86.eax = 0x4300;
	v86.edx = dskp->drive;
	v86.ds = VTOPSEG(&packet);
	v86.esi = VTOPOFF(&packet);
	v86int();
	if (V86_CY(v86.efl)) {
		printf("error %u lba %llu\n", v86.eax >> 8 & 0xff, lba);
		return (-1);
	}
	return (0);
}
#endif	/* GPT || ZFS */
