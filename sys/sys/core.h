/*	$OpenBSD: core.h,v 1.9 2024/01/17 22:22:25 kurt Exp $	*/
/*	$NetBSD: core.h,v 1.4 1994/10/29 08:20:14 cgd Exp $	*/

/*
 * Copyright (c) 1994 Paul Kranenburg
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Paul Kranenburg.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#define COREMAGIC	0507
#define CORESEGMAGIC	0510

/*
 * The core structure's c_midmag field (like exec's a_midmag) is a
 * network-byteorder encoding of this int
 *	FFFFFFmmmmmmmmmmMMMMMMMMMMMMMMMM
 * Where `F' is 6 bits of flag (currently unused),
 *       `m' is 10 bits of machine-id, and
 *       `M' is 16 bits worth of magic number, ie. COREMAGIC.
 * The macros below will set/get the needed fields.
 */
#define	CORE_GETMAGIC(c)  (  ntohl(((c).c_midmag))        & 0xffff )
#define	CORE_GETMID(c)    ( (ntohl(((c).c_midmag)) >> 16) & 0x03ff )
#define	CORE_GETFLAG(c)   ( (ntohl(((c).c_midmag)) >> 26) & 0x03f  )
#define	CORE_SETMAGIC(c,mag,mid,flag) ( (c).c_midmag = htonl ( \
			( ((flag) & 0x3f)   << 26) | \
			( ((mid)  & 0x03ff) << 16) | \
			( ((mag)  & 0xffff)      ) ) )

/* Flag definitions */
#define CORE_CPU	1
#define CORE_DATA	2
#define CORE_STACK	4

#ifndef _KERNEL
/*
 * XXX OBSOLETE, NO LONGER USED
 * XXX This header file exists to support binutils' netbsd-core format
 * XXX which is still needed for the a.out-m88k-openbsd use in luna88k
 * XXX boot block creation.
 *
 * A core file consists of a header followed by a number of segments.
 * Each segment is preceded by a `coreseg' structure giving the
 * segment's type, the virtual address where the bits resided in
 * process address space and the size of the segment.
 *
 * The core header specifies the lengths of the core header itself and
 * each of the following core segment headers to allow for any machine
 * dependent alignment requirements.
 */

struct core {
	u_int32_t c_midmag;		/* magic, id, flags */
	u_int16_t c_hdrsize;		/* Size of this header (machdep algn) */
	u_int16_t c_seghdrsize;		/* Size of a segment header */
	u_int32_t c_nseg;		/* # of core segments */
	char	c_name[_MAXCOMLEN];	/* Copy of p->p_comm, incl NUL */
	u_int32_t c_signo;		/* Killing signal */
	u_long	c_ucode;		/* Hmm ? */
	u_long	c_cpusize;		/* Size of machine dependent segment */
	u_long	c_tsize;		/* Size of traditional text segment */
	u_long	c_dsize;		/* Size of traditional data segment */
	u_long	c_ssize;		/* Size of traditional stack segment */
};

struct coreseg {
	u_int32_t c_midmag;		/* magic, id, flags */
	u_long	c_addr;			/* Virtual address of segment */
	u_long	c_size;			/* Size of this segment */
};

#else
int	coredump_write(void *, enum uio_seg, const void *, size_t, int);
void	coredump_unmap(void *, vaddr_t, vaddr_t);
#endif
