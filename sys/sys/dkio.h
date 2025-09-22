/*	$OpenBSD: dkio.h,v 1.13 2025/09/17 10:24:25 deraadt Exp $	*/
/*	$NetBSD: dkio.h,v 1.1 1996/01/30 18:21:48 thorpej Exp $	*/

/*
 * Copyright (c) 1987, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_DKIO_H_
#define _SYS_DKIO_H_

#include <sys/ioccom.h>

/*
 * Disk-specific ioctls.
 */
		/* get and set disklabel; DIOCGPART used internally */
#define DIOCGDINFO	_IOR('d', 101, struct disklabel)/* get */
#define DIOCSDINFO	_IOW('d', 102, struct disklabel)/* set */
#define DIOCWDINFO	_IOW('d', 103, struct disklabel)/* set, update disk */
#define DIOCGPART	_IOW('d', 104, struct partinfo)	/* get partition */

#define DIOCEJECT	_IO('d', 112)		/* eject removable disk */
#define DIOCLOCK	_IOW('d', 113, int)	/* lock/unlock pack */

#define DIOCGPDINFO	_IOR('d', 114, struct disklabel)/* get physical */
#define DIOCRLDINFO	_IO('d', 115)			/* reload disklabel */

#if MAXPARTITIONS != 16
/* XXX temporary to support the transition to more partitions */
#define O_sizeof_disklabel (offsetof(struct disklabel, d_partitions[16]))
#define O_DIOCGDINFO _IOC(IOC_OUT, 'd', 101, O_sizeof_disklabel)
#endif

struct dk_inquiry {
	char		vendor[64];
	char		product[128];
	char		revision[64];
	char		serial[64];
};

#define DIOCINQ		_IOR('d', 116, struct dk_inquiry)

struct dk_cache {
	unsigned int	wrcache;
	unsigned int	rdcache;
};

#define DIOCGCACHE	_IOR('d', 117, struct dk_cache)	/* get cache enabled */
#define DIOCSCACHE	_IOW('d', 118, struct dk_cache)	/* set cache enabled */

struct dk_diskmap {
	char		*device;
	int		fd;
	int		flags;
};

#define	DIOCMAP		_IOWR('d', 119, struct dk_diskmap)

#define	DIOCCACHESYNC	_IOW('d', 120, int)	/* sync cache (force?) */

#endif /* _SYS_DKIO_H_ */
