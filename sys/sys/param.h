/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *
 *	@(#)param.h	8.3 (Berkeley) 4/4/95
 * $FreeBSD$
 */

#ifndef _SYS_PARAM_H_
#define _SYS_PARAM_H_

#include <sys/_null.h>

#define	BSD	199506		/* System version (year & month). */
#define BSD4_3	1
#define BSD4_4	1

/*
 * __FreeBSD_version numbers are documented in the Porter's Handbook.
 * If you bump the version for any reason, you should update the documentation
 * there.
 * Currently this lives here in the doc/ repository:
 *
 *	head/en_US.ISO8859-1/books/porters-handbook/versions/chapter.xml
 *
 * scheme is:  <major><two digit minor>Rxx
 *		'R' is in the range 0 to 4 if this is a release branch or
 *		X.0-CURRENT before releng/X.0 is created, otherwise 'R' is
 *		in the range 5 to 9.
 */
#undef __FreeBSD_version
#define __FreeBSD_version 1300017	/* Master, propagated to newvers */

/*
 * __FreeBSD_kernel__ indicates that this system uses the kernel of FreeBSD,
 * which by definition is always true on FreeBSD. This macro is also defined
 * on other systems that use the kernel of FreeBSD, such as GNU/kFreeBSD.
 *
 * It is tempting to use this macro in userland code when we want to enable
 * kernel-specific routines, and in fact it's fine to do this in code that
 * is part of FreeBSD itself.  However, be aware that as presence of this
 * macro is still not widespread (e.g. older FreeBSD versions, 3rd party
 * compilers, etc), it is STRONGLY DISCOURAGED to check for this macro in
 * external applications without also checking for __FreeBSD__ as an
 * alternative.
 */
#undef __FreeBSD_kernel__
#define __FreeBSD_kernel__

#if defined(_KERNEL) || defined(IN_RTLD)
#define	P_OSREL_SIGWAIT			700000
#define	P_OSREL_SIGSEGV			700004
#define	P_OSREL_MAP_ANON		800104
#define	P_OSREL_MAP_FSTRICT		1100036
#define	P_OSREL_SHUTDOWN_ENOTCONN	1100077
#define	P_OSREL_MAP_GUARD		1200035
#define	P_OSREL_WRFSBASE		1200041
#define	P_OSREL_CK_CYLGRP		1200046
#define	P_OSREL_VMTOTAL64		1200054
#define	P_OSREL_CK_SUPERBLOCK		1300000
#define	P_OSREL_CK_INODE		1300005

#define	P_OSREL_MAJOR(x)		((x) / 100000)
#endif

#ifndef LOCORE
#include <sys/types.h>
#endif

/*
 * Machine-independent constants (some used in following include files).
 * Redefined constants are from POSIX 1003.1 limits file.
 *
 * MAXCOMLEN should be >= sizeof(ac_comm) (see <acct.h>)
 */
#include <sys/syslimits.h>

#define	MAXCOMLEN	19		/* max command name remembered */
#define	MAXINTERP	PATH_MAX	/* max interpreter file name length */
#define	MAXLOGNAME	33		/* max login name length (incl. NUL) */
#define	MAXUPRC		CHILD_MAX	/* max simultaneous processes */
#define	NCARGS		ARG_MAX		/* max bytes for an exec function */
#define	NGROUPS		(NGROUPS_MAX+1)	/* max number groups */
#define	NOFILE		OPEN_MAX	/* max open files per process */
#define	NOGROUP		65535		/* marker for empty group set member */
#define MAXHOSTNAMELEN	256		/* max hostname size */
#define SPECNAMELEN	255		/* max length of devicename */

/* More types and definitions used throughout the kernel. */
#ifdef _KERNEL
#include <sys/cdefs.h>
#include <sys/errno.h>
#ifndef LOCORE
#include <sys/time.h>
#include <sys/priority.h>
#endif

#ifndef FALSE
#define	FALSE	0
#endif
#ifndef TRUE
#define	TRUE	1
#endif
#endif

#ifndef _KERNEL
/* Signals. */
#include <sys/signal.h>
#endif

/* Machine type dependent parameters. */
#include <machine/param.h>
#ifndef _KERNEL
#include <sys/limits.h>
#endif

#ifndef DEV_BSHIFT
#define	DEV_BSHIFT	9		/* log2(DEV_BSIZE) */
#endif
#define	DEV_BSIZE	(1<<DEV_BSHIFT)

#ifndef BLKDEV_IOSIZE
#define BLKDEV_IOSIZE  PAGE_SIZE	/* default block device I/O size */
#endif
#ifndef DFLTPHYS
#define DFLTPHYS	(64 * 1024)	/* default max raw I/O transfer size */
#endif
#ifndef MAXPHYS
#define MAXPHYS		(128 * 1024)	/* max raw I/O transfer size */
#endif
#ifndef MAXDUMPPGS
#define MAXDUMPPGS	(DFLTPHYS/PAGE_SIZE)
#endif

/*
 * Constants related to network buffer management.
 * MCLBYTES must be no larger than PAGE_SIZE.
 */
#ifndef	MSIZE
#define	MSIZE		256		/* size of an mbuf */
#endif

#ifndef	MCLSHIFT
#define MCLSHIFT	11		/* convert bytes to mbuf clusters */
#endif	/* MCLSHIFT */

#define MCLBYTES	(1 << MCLSHIFT)	/* size of an mbuf cluster */

#if PAGE_SIZE < 2048
#define	MJUMPAGESIZE	MCLBYTES
#elif PAGE_SIZE <= 8192
#define	MJUMPAGESIZE	PAGE_SIZE
#else
#define	MJUMPAGESIZE	(8 * 1024)
#endif

#define	MJUM9BYTES	(9 * 1024)	/* jumbo cluster 9k */
#define	MJUM16BYTES	(16 * 1024)	/* jumbo cluster 16k */

/*
 * Some macros for units conversion
 */

/* clicks to bytes */
#ifndef ctob
#define ctob(x)	((x)<<PAGE_SHIFT)
#endif

/* bytes to clicks */
#ifndef btoc
#define btoc(x)	(((vm_offset_t)(x)+PAGE_MASK)>>PAGE_SHIFT)
#endif

/*
 * btodb() is messy and perhaps slow because `bytes' may be an off_t.  We
 * want to shift an unsigned type to avoid sign extension and we don't
 * want to widen `bytes' unnecessarily.  Assume that the result fits in
 * a daddr_t.
 */
#ifndef btodb
#define btodb(bytes)	 		/* calculates (bytes / DEV_BSIZE) */ \
	(sizeof (bytes) > sizeof(long) \
	 ? (daddr_t)((unsigned long long)(bytes) >> DEV_BSHIFT) \
	 : (daddr_t)((unsigned long)(bytes) >> DEV_BSHIFT))
#endif

#ifndef dbtob
#define dbtob(db)			/* calculates (db * DEV_BSIZE) */ \
	((off_t)(db) << DEV_BSHIFT)
#endif

#define	PRIMASK	0x0ff
#define	PCATCH	0x100		/* OR'd with pri for tsleep to check signals */
#define	PDROP	0x200	/* OR'd with pri to stop re-entry of interlock mutex */

#define	NZERO	0		/* default "nice" */

#define	NBBY	8		/* number of bits in a byte */
#define	NBPW	sizeof(int)	/* number of bytes per word (integer) */

#define	CMASK	022		/* default file mask: S_IWGRP|S_IWOTH */

#define	NODEV	(dev_t)(-1)	/* non-existent device */

/*
 * File system parameters and macros.
 *
 * MAXBSIZE -	Filesystems are made out of blocks of at most MAXBSIZE bytes
 *		per block.  MAXBSIZE may be made larger without effecting
 *		any existing filesystems as long as it does not exceed MAXPHYS,
 *		and may be made smaller at the risk of not being able to use
 *		filesystems which require a block size exceeding MAXBSIZE.
 *
 * MAXBCACHEBUF - Maximum size of a buffer in the buffer cache.  This must
 *		be >= MAXBSIZE and can be set differently for different
 *		architectures by defining it in <machine/param.h>.
 *		Making this larger allows NFS to do larger reads/writes.
 *
 * BKVASIZE -	Nominal buffer space per buffer, in bytes.  BKVASIZE is the
 *		minimum KVM memory reservation the kernel is willing to make.
 *		Filesystems can of course request smaller chunks.  Actual
 *		backing memory uses a chunk size of a page (PAGE_SIZE).
 *		The default value here can be overridden on a per-architecture
 *		basis by defining it in <machine/param.h>.
 *
 *		If you make BKVASIZE too small you risk seriously fragmenting
 *		the buffer KVM map which may slow things down a bit.  If you
 *		make it too big the kernel will not be able to optimally use
 *		the KVM memory reserved for the buffer cache and will wind
 *		up with too-few buffers.
 *
 *		The default is 16384, roughly 2x the block size used by a
 *		normal UFS filesystem.
 */
#define MAXBSIZE	65536	/* must be power of 2 */
#ifndef	MAXBCACHEBUF
#define	MAXBCACHEBUF	MAXBSIZE /* must be a power of 2 >= MAXBSIZE */
#endif
#ifndef	BKVASIZE
#define BKVASIZE	16384	/* must be power of 2 */
#endif
#define BKVAMASK	(BKVASIZE-1)

/*
 * MAXPATHLEN defines the longest permissible path length after expanding
 * symbolic links. It is used to allocate a temporary buffer from the buffer
 * pool in which to do the name expansion, hence should be a power of two,
 * and must be less than or equal to MAXBSIZE.  MAXSYMLINKS defines the
 * maximum number of symbolic links that may be expanded in a path name.
 * It should be set high enough to allow all legitimate uses, but halt
 * infinite loops reasonably quickly.
 */
#define	MAXPATHLEN	PATH_MAX
#define MAXSYMLINKS	32

/* Bit map related macros. */
#define	setbit(a,i)	(((unsigned char *)(a))[(i)/NBBY] |= 1<<((i)%NBBY))
#define	clrbit(a,i)	(((unsigned char *)(a))[(i)/NBBY] &= ~(1<<((i)%NBBY)))
#define	isset(a,i)							\
	(((const unsigned char *)(a))[(i)/NBBY] & (1<<((i)%NBBY)))
#define	isclr(a,i)							\
	((((const unsigned char *)(a))[(i)/NBBY] & (1<<((i)%NBBY))) == 0)

/* Macros for counting and rounding. */
#ifndef howmany
#define	howmany(x, y)	(((x)+((y)-1))/(y))
#endif
#define	nitems(x)	(sizeof((x)) / sizeof((x)[0]))
#define	rounddown(x, y)	(((x)/(y))*(y))
#define	rounddown2(x, y) ((x)&(~((y)-1)))          /* if y is power of two */
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))  /* to any y */
#define	roundup2(x, y)	(((x)+((y)-1))&(~((y)-1))) /* if y is powers of two */
#define powerof2(x)	((((x)-1)&(x))==0)

/* Macros for min/max. */
#define	MIN(a,b) (((a)<(b))?(a):(b))
#define	MAX(a,b) (((a)>(b))?(a):(b))

#ifdef _KERNEL
/*
 * Basic byte order function prototypes for non-inline functions.
 */
#ifndef LOCORE
#ifndef _BYTEORDER_PROTOTYPED
#define	_BYTEORDER_PROTOTYPED
__BEGIN_DECLS
__uint32_t	 htonl(__uint32_t);
__uint16_t	 htons(__uint16_t);
__uint32_t	 ntohl(__uint32_t);
__uint16_t	 ntohs(__uint16_t);
__END_DECLS
#endif
#endif

#ifndef _BYTEORDER_FUNC_DEFINED
#define	_BYTEORDER_FUNC_DEFINED
#define	htonl(x)	__htonl(x)
#define	htons(x)	__htons(x)
#define	ntohl(x)	__ntohl(x)
#define	ntohs(x)	__ntohs(x)
#endif /* !_BYTEORDER_FUNC_DEFINED */
#endif /* _KERNEL */

/*
 * Scale factor for scaled integers used to count %cpu time and load avgs.
 *
 * The number of CPU `tick's that map to a unique `%age' can be expressed
 * by the formula (1 / (2 ^ (FSHIFT - 11))).  The maximum load average that
 * can be calculated (assuming 32 bits) can be closely approximated using
 * the formula (2 ^ (2 * (16 - FSHIFT))) for (FSHIFT < 15).
 *
 * For the scheduler to maintain a 1:1 mapping of CPU `tick' to `%age',
 * FSHIFT must be at least 11; this gives us a maximum load avg of ~1024.
 */
#define	FSHIFT	11		/* bits to right of fixed binary point */
#define FSCALE	(1<<FSHIFT)

#define dbtoc(db)			/* calculates devblks to pages */ \
	((db + (ctodb(1) - 1)) >> (PAGE_SHIFT - DEV_BSHIFT))

#define ctodb(db)			/* calculates pages to devblks */ \
	((db) << (PAGE_SHIFT - DEV_BSHIFT))

/*
 * Old spelling of __containerof().
 */
#define	member2struct(s, m, x)						\
	((struct s *)(void *)((char *)(x) - offsetof(struct s, m)))

/*
 * Access a variable length array that has been declared as a fixed
 * length array.
 */
#define __PAST_END(array, offset) (((__typeof__(*(array)) *)(array))[offset])

#endif	/* _SYS_PARAM_H_ */
