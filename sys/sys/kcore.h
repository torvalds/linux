/*	$OpenBSD: kcore.h,v 1.2 2023/01/04 10:59:34 jsg Exp $	*/
/*	$NetBSD: kcore.h,v 1.1 1996/03/10 21:56:00 leo Exp $	*/

/*
 * Copyright (c) 1996 Leo Weppelman.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _SYS_KCORE_H_
#define	_SYS_KCORE_H_

/*
 * Definitions for the kernel crash-dump format. The structure of
 * the files and headers is borrowed from the 'regular' core files
 * as described in <sys/core.h>.
 */
#define	KCORE_MAGIC	0x8fca
#define	KCORESEG_MAGIC	0x8fac

/*
 * Description of a memory segment. To make this suitable for sharing
 * between all architectures, u_quad_t seems to be the necessary type...
 */
typedef struct {
	u_quad_t	start;		/* Physical start address	*/
	u_quad_t	size;		/* Size in bytes		*/
} phys_ram_seg_t;

typedef struct kcore_hdr {
	u_int32_t	c_midmag;	/* Magic, id, flags		*/
	u_int16_t	c_hdrsize;	/* Aligned header size		*/
	u_int16_t	c_seghdrsize;	/* Aligned seg-header size	*/
	u_int32_t	c_nseg;		/* Number of segments		*/
} kcore_hdr_t;

typedef struct kcore_seg {
	u_int32_t	c_midmag;	/* Magic, id, flags		*/
	u_int32_t	c_size;		/* Sizeof this segment		*/
} kcore_seg_t;

#endif /* _SYS_KCORE_H_ */
