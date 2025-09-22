/*	$OpenBSD: swap.h,v 1.7 2013/09/30 12:02:30 millert Exp $	*/
/*	$NetBSD: swap.h,v 1.2 1998/09/13 14:46:24 christos Exp $	*/

/*
 * Copyright (c) 1995, 1996, 1998 Matthew R. Green, Tobias Weingartner
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
 * 3. The name of the author may not be used to endorse or promote products
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

#ifndef _SYS_SWAP_H_
#define _SYS_SWAP_H_

#include <sys/syslimits.h>

/* These structures are used to return swap information for userland */
struct swapent {
	dev_t	se_dev;			/* device id */
	int	se_flags;		/* flags */
	int	se_nblks;		/* total blocks */
	int	se_inuse;		/* blocks in use */
	int	se_priority;		/* priority of this device */
	char	se_path[PATH_MAX];	/* path name */
};

#ifdef _KERNEL
#define	NETDEV		(dev_t)(-2)	/* network device (for nfs swap) */
#endif /* _KERNEL */

#define SWAP_ON		1		/* begin swapping on device */
#define SWAP_OFF	2		/* (stop swapping on device) */
#define SWAP_NSWAP	3		/* how many swap devices ? */
#define SWAP_STATS	4		/* get device info */
#define SWAP_CTL	5		/* change priority on device */
#define SWAP_DUMPDEV 	7		/* use this device as dump device */

#define SWF_INUSE	0x00000001	/* in use: we have swapped here */
#define SWF_ENABLE	0x00000002	/* enabled: we can swap here */
#define SWF_BUSY	0x00000004	/* busy: I/O happening here */
#define SWF_FAKE	0x00000008	/* fake: still being built */

#endif /* _SYS_SWAP_H_ */
