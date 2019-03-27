/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
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
 *      @(#)bpf.h	8.1 (Berkeley) 6/10/93
 *	@(#)bpf.h	1.34 (LBL)     6/16/96
 *
 * $FreeBSD$
 */

#ifndef _NET_BPF_H_
#define _NET_BPF_H_

/* BSD style release date */
#define	BPF_RELEASE 199606

typedef	int32_t	  bpf_int32;
typedef	u_int32_t bpf_u_int32;
typedef	int64_t	  bpf_int64;
typedef	u_int64_t bpf_u_int64;

/*
 * Alignment macros.  BPF_WORDALIGN rounds up to the next
 * even multiple of BPF_ALIGNMENT.
 */
#define BPF_ALIGNMENT sizeof(long)
#define BPF_WORDALIGN(x) (((x)+(BPF_ALIGNMENT-1))&~(BPF_ALIGNMENT-1))

#define BPF_MAXINSNS 512
#define BPF_MAXBUFSIZE 0x80000
#define BPF_MINBUFSIZE 32

/*
 *  Structure for BIOCSETF.
 */
struct bpf_program {
	u_int bf_len;
	struct bpf_insn *bf_insns;
};

/*
 * Struct returned by BIOCGSTATS.
 */
struct bpf_stat {
	u_int bs_recv;		/* number of packets received */
	u_int bs_drop;		/* number of packets dropped */
};

/*
 * Struct return by BIOCVERSION.  This represents the version number of
 * the filter language described by the instruction encodings below.
 * bpf understands a program iff kernel_major == filter_major &&
 * kernel_minor >= filter_minor, that is, if the value returned by the
 * running kernel has the same major number and a minor number equal
 * equal to or less than the filter being downloaded.  Otherwise, the
 * results are undefined, meaning an error may be returned or packets
 * may be accepted haphazardly.
 * It has nothing to do with the source code version.
 */
struct bpf_version {
	u_short bv_major;
	u_short bv_minor;
};
/* Current version number of filter architecture. */
#define BPF_MAJOR_VERSION 1
#define BPF_MINOR_VERSION 1

/*
 * Historically, BPF has supported a single buffering model, first using mbuf
 * clusters in kernel, and later using malloc(9) buffers in kernel.  We now
 * support multiple buffering modes, which may be queried and set using
 * BIOCGETBUFMODE and BIOCSETBUFMODE.  So as to avoid handling the complexity
 * of changing modes while sniffing packets, the mode becomes fixed once an
 * interface has been attached to the BPF descriptor.
 */
#define	BPF_BUFMODE_BUFFER	1	/* Kernel buffers with read(). */
#define	BPF_BUFMODE_ZBUF	2	/* Zero-copy buffers. */

/*-
 * Struct used by BIOCSETZBUF, BIOCROTZBUF: describes up to two zero-copy
 * buffer as used by BPF.
 */
struct bpf_zbuf {
	void	*bz_bufa;	/* Location of 'a' zero-copy buffer. */
	void	*bz_bufb;	/* Location of 'b' zero-copy buffer. */
	size_t	 bz_buflen;	/* Size of zero-copy buffers. */
};

#define	BIOCGBLEN	_IOR('B', 102, u_int)
#define	BIOCSBLEN	_IOWR('B', 102, u_int)
#define	BIOCSETF	_IOW('B', 103, struct bpf_program)
#define	BIOCFLUSH	_IO('B', 104)
#define	BIOCPROMISC	_IO('B', 105)
#define	BIOCGDLT	_IOR('B', 106, u_int)
#define	BIOCGETIF	_IOR('B', 107, struct ifreq)
#define	BIOCSETIF	_IOW('B', 108, struct ifreq)
#define	BIOCSRTIMEOUT	_IOW('B', 109, struct timeval)
#define	BIOCGRTIMEOUT	_IOR('B', 110, struct timeval)
#define	BIOCGSTATS	_IOR('B', 111, struct bpf_stat)
#define	BIOCIMMEDIATE	_IOW('B', 112, u_int)
#define	BIOCVERSION	_IOR('B', 113, struct bpf_version)
#define	BIOCGRSIG	_IOR('B', 114, u_int)
#define	BIOCSRSIG	_IOW('B', 115, u_int)
#define	BIOCGHDRCMPLT	_IOR('B', 116, u_int)
#define	BIOCSHDRCMPLT	_IOW('B', 117, u_int)
#define	BIOCGDIRECTION	_IOR('B', 118, u_int)
#define	BIOCSDIRECTION	_IOW('B', 119, u_int)
#define	BIOCSDLT	_IOW('B', 120, u_int)
#define	BIOCGDLTLIST	_IOWR('B', 121, struct bpf_dltlist)
#define	BIOCLOCK	_IO('B', 122)
#define	BIOCSETWF	_IOW('B', 123, struct bpf_program)
#define	BIOCFEEDBACK	_IOW('B', 124, u_int)
#define	BIOCGETBUFMODE	_IOR('B', 125, u_int)
#define	BIOCSETBUFMODE	_IOW('B', 126, u_int)
#define	BIOCGETZMAX	_IOR('B', 127, size_t)
#define	BIOCROTZBUF	_IOR('B', 128, struct bpf_zbuf)
#define	BIOCSETZBUF	_IOW('B', 129, struct bpf_zbuf)
#define	BIOCSETFNR	_IOW('B', 130, struct bpf_program)
#define	BIOCGTSTAMP	_IOR('B', 131, u_int)
#define	BIOCSTSTAMP	_IOW('B', 132, u_int)

/* Obsolete */
#define	BIOCGSEESENT	BIOCGDIRECTION
#define	BIOCSSEESENT	BIOCSDIRECTION

/* Packet directions */
enum bpf_direction {
	BPF_D_IN,	/* See incoming packets */
	BPF_D_INOUT,	/* See incoming and outgoing packets */
	BPF_D_OUT	/* See outgoing packets */
};

/* Time stamping functions */
#define	BPF_T_MICROTIME		0x0000
#define	BPF_T_NANOTIME		0x0001
#define	BPF_T_BINTIME		0x0002
#define	BPF_T_NONE		0x0003
#define	BPF_T_FORMAT_MASK	0x0003
#define	BPF_T_NORMAL		0x0000
#define	BPF_T_FAST		0x0100
#define	BPF_T_MONOTONIC		0x0200
#define	BPF_T_MONOTONIC_FAST	(BPF_T_FAST | BPF_T_MONOTONIC)
#define	BPF_T_FLAG_MASK		0x0300
#define	BPF_T_FORMAT(t)		((t) & BPF_T_FORMAT_MASK)
#define	BPF_T_FLAG(t)		((t) & BPF_T_FLAG_MASK)
#define	BPF_T_VALID(t)						\
    ((t) == BPF_T_NONE || (BPF_T_FORMAT(t) != BPF_T_NONE &&	\
    ((t) & ~(BPF_T_FORMAT_MASK | BPF_T_FLAG_MASK)) == 0))

#define	BPF_T_MICROTIME_FAST		(BPF_T_MICROTIME | BPF_T_FAST)
#define	BPF_T_NANOTIME_FAST		(BPF_T_NANOTIME | BPF_T_FAST)
#define	BPF_T_BINTIME_FAST		(BPF_T_BINTIME | BPF_T_FAST)
#define	BPF_T_MICROTIME_MONOTONIC	(BPF_T_MICROTIME | BPF_T_MONOTONIC)
#define	BPF_T_NANOTIME_MONOTONIC	(BPF_T_NANOTIME | BPF_T_MONOTONIC)
#define	BPF_T_BINTIME_MONOTONIC		(BPF_T_BINTIME | BPF_T_MONOTONIC)
#define	BPF_T_MICROTIME_MONOTONIC_FAST	(BPF_T_MICROTIME | BPF_T_MONOTONIC_FAST)
#define	BPF_T_NANOTIME_MONOTONIC_FAST	(BPF_T_NANOTIME | BPF_T_MONOTONIC_FAST)
#define	BPF_T_BINTIME_MONOTONIC_FAST	(BPF_T_BINTIME | BPF_T_MONOTONIC_FAST)

/*
 * Structure prepended to each packet.
 */
struct bpf_ts {
	bpf_int64	bt_sec;		/* seconds */
	bpf_u_int64	bt_frac;	/* fraction */
};
struct bpf_xhdr {
	struct bpf_ts	bh_tstamp;	/* time stamp */
	bpf_u_int32	bh_caplen;	/* length of captured portion */
	bpf_u_int32	bh_datalen;	/* original length of packet */
	u_short		bh_hdrlen;	/* length of bpf header (this struct
					   plus alignment padding) */
};
/* Obsolete */
struct bpf_hdr {
	struct timeval	bh_tstamp;	/* time stamp */
	bpf_u_int32	bh_caplen;	/* length of captured portion */
	bpf_u_int32	bh_datalen;	/* original length of packet */
	u_short		bh_hdrlen;	/* length of bpf header (this struct
					   plus alignment padding) */
};
#ifdef _KERNEL
#define	MTAG_BPF		0x627066
#define	MTAG_BPF_TIMESTAMP	0
#endif

/*
 * When using zero-copy BPF buffers, a shared memory header is present
 * allowing the kernel BPF implementation and user process to synchronize
 * without using system calls.  This structure defines that header.  When
 * accessing these fields, appropriate atomic operation and memory barriers
 * are required in order not to see stale or out-of-order data; see bpf(4)
 * for reference code to access these fields from userspace.
 *
 * The layout of this structure is critical, and must not be changed; if must
 * fit in a single page on all architectures.
 */
struct bpf_zbuf_header {
	volatile u_int	bzh_kernel_gen;	/* Kernel generation number. */
	volatile u_int	bzh_kernel_len;	/* Length of data in the buffer. */
	volatile u_int	bzh_user_gen;	/* User generation number. */
	u_int _bzh_pad[5];
};

/* Pull in data-link level type codes. */
#include <net/dlt.h>

/*
 * The instruction encodings.
 *
 * Please inform tcpdump-workers@lists.tcpdump.org if you use any
 * of the reserved values, so that we can note that they're used
 * (and perhaps implement it in the reference BPF implementation
 * and encourage its implementation elsewhere).
 */

/*
 * The upper 8 bits of the opcode aren't used. BSD/OS used 0x8000.
 */

/* instruction classes */
#define BPF_CLASS(code) ((code) & 0x07)
#define		BPF_LD		0x00
#define		BPF_LDX		0x01
#define		BPF_ST		0x02
#define		BPF_STX		0x03
#define		BPF_ALU		0x04
#define		BPF_JMP		0x05
#define		BPF_RET		0x06
#define		BPF_MISC	0x07

/* ld/ldx fields */
#define BPF_SIZE(code)	((code) & 0x18)
#define		BPF_W		0x00
#define		BPF_H		0x08
#define		BPF_B		0x10
/*				0x18	reserved; used by BSD/OS */
#define BPF_MODE(code)	((code) & 0xe0)
#define		BPF_IMM 	0x00
#define		BPF_ABS		0x20
#define		BPF_IND		0x40
#define		BPF_MEM		0x60
#define		BPF_LEN		0x80
#define		BPF_MSH		0xa0
/*				0xc0	reserved; used by BSD/OS */
/*				0xe0	reserved; used by BSD/OS */

/* alu/jmp fields */
#define BPF_OP(code)	((code) & 0xf0)
#define		BPF_ADD		0x00
#define		BPF_SUB		0x10
#define		BPF_MUL		0x20
#define		BPF_DIV		0x30
#define		BPF_OR		0x40
#define		BPF_AND		0x50
#define		BPF_LSH		0x60
#define		BPF_RSH		0x70
#define		BPF_NEG		0x80
#define		BPF_MOD		0x90
#define		BPF_XOR		0xa0
/*				0xb0	reserved */
/*				0xc0	reserved */
/*				0xd0	reserved */
/*				0xe0	reserved */
/*				0xf0	reserved */

#define		BPF_JA		0x00
#define		BPF_JEQ		0x10
#define		BPF_JGT		0x20
#define		BPF_JGE		0x30
#define		BPF_JSET	0x40
/*				0x50	reserved; used on BSD/OS */
/*				0x60	reserved */
/*				0x70	reserved */
/*				0x80	reserved */
/*				0x90	reserved */
/*				0xa0	reserved */
/*				0xb0	reserved */
/*				0xc0	reserved */
/*				0xd0	reserved */
/*				0xe0	reserved */
/*				0xf0	reserved */
#define BPF_SRC(code)	((code) & 0x08)
#define		BPF_K		0x00
#define		BPF_X		0x08

/* ret - BPF_K and BPF_X also apply */
#define BPF_RVAL(code)	((code) & 0x18)
#define		BPF_A		0x10
/*				0x18	reserved */

/* misc */
#define BPF_MISCOP(code) ((code) & 0xf8)
#define		BPF_TAX		0x00
/*				0x08	reserved */
/*				0x10	reserved */
/*				0x18	reserved */
/* #define	BPF_COP		0x20	NetBSD "coprocessor" extensions */
/*				0x28	reserved */
/*				0x30	reserved */
/*				0x38	reserved */
/* #define	BPF_COPX	0x40	NetBSD "coprocessor" extensions */
/*					also used on BSD/OS */
/*				0x48	reserved */
/*				0x50	reserved */
/*				0x58	reserved */
/*				0x60	reserved */
/*				0x68	reserved */
/*				0x70	reserved */
/*				0x78	reserved */
#define		BPF_TXA		0x80
/*				0x88	reserved */
/*				0x90	reserved */
/*				0x98	reserved */
/*				0xa0	reserved */
/*				0xa8	reserved */
/*				0xb0	reserved */
/*				0xb8	reserved */
/*				0xc0	reserved; used on BSD/OS */
/*				0xc8	reserved */
/*				0xd0	reserved */
/*				0xd8	reserved */
/*				0xe0	reserved */
/*				0xe8	reserved */
/*				0xf0	reserved */
/*				0xf8	reserved */

/*
 * The instruction data structure.
 */
struct bpf_insn {
	u_short		code;
	u_char		jt;
	u_char		jf;
	bpf_u_int32	k;
};

/*
 * Macros for insn array initializers.
 */
#define BPF_STMT(code, k) { (u_short)(code), 0, 0, k }
#define BPF_JUMP(code, k, jt, jf) { (u_short)(code), jt, jf, k }

/*
 * Structure to retrieve available DLTs for the interface.
 */
struct bpf_dltlist {
	u_int	bfl_len;	/* number of bfd_list array */
	u_int	*bfl_list;	/* array of DLTs */
};

#ifdef _KERNEL
#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_BPF);
#endif
#ifdef SYSCTL_DECL
SYSCTL_DECL(_net_bpf);
#endif

/*
 * Rotate the packet buffers in descriptor d.  Move the store buffer into the
 * hold slot, and the free buffer into the store slot.  Zero the length of the
 * new store buffer.  Descriptor lock should be held.  One must be careful to
 * not rotate the buffers twice, i.e. if fbuf != NULL.
 */
#define	ROTATE_BUFFERS(d)	do {					\
	(d)->bd_hbuf = (d)->bd_sbuf;					\
	(d)->bd_hlen = (d)->bd_slen;					\
	(d)->bd_sbuf = (d)->bd_fbuf;					\
	(d)->bd_slen = 0;						\
	(d)->bd_fbuf = NULL;						\
	bpf_bufheld(d);							\
} while (0)

/*
 * Descriptor associated with each attached hardware interface.
 * Part of this structure is exposed to external callers to speed up
 * bpf_peers_present() calls.
 */
struct bpf_if;

struct bpf_if_ext {
	LIST_ENTRY(bpf_if)	bif_next;	/* list of all interfaces */
	LIST_HEAD(, bpf_d)	bif_dlist;	/* descriptor list */
};

void	 bpf_bufheld(struct bpf_d *d);
int	 bpf_validate(const struct bpf_insn *, int);
void	 bpf_tap(struct bpf_if *, u_char *, u_int);
void	 bpf_mtap(struct bpf_if *, struct mbuf *);
void	 bpf_mtap2(struct bpf_if *, void *, u_int, struct mbuf *);
void	 bpfattach(struct ifnet *, u_int, u_int);
void	 bpfattach2(struct ifnet *, u_int, u_int, struct bpf_if **);
void	 bpfdetach(struct ifnet *);
#ifdef VIMAGE
int	 bpf_get_bp_params(struct bpf_if *, u_int *, u_int *);
#endif

void	 bpfilterattach(int);
u_int	 bpf_filter(const struct bpf_insn *, u_char *, u_int, u_int);

static __inline int
bpf_peers_present(struct bpf_if *bpf)
{
	struct bpf_if_ext *ext;

	ext = (struct bpf_if_ext *)bpf;
	if (!LIST_EMPTY(&ext->bif_dlist))
		return (1);
	return (0);
}

#define	BPF_TAP(_ifp,_pkt,_pktlen) do {				\
	if (bpf_peers_present((_ifp)->if_bpf))			\
		bpf_tap((_ifp)->if_bpf, (_pkt), (_pktlen));	\
} while (0)
#define	BPF_MTAP(_ifp,_m) do {					\
	if (bpf_peers_present((_ifp)->if_bpf)) {		\
		M_ASSERTVALID(_m);				\
		bpf_mtap((_ifp)->if_bpf, (_m));			\
	}							\
} while (0)
#define	BPF_MTAP2(_ifp,_data,_dlen,_m) do {			\
	if (bpf_peers_present((_ifp)->if_bpf)) {		\
		M_ASSERTVALID(_m);				\
		bpf_mtap2((_ifp)->if_bpf,(_data),(_dlen),(_m));	\
	}							\
} while (0)
#endif

/*
 * Number of scratch memory words (for BPF_LD|BPF_MEM and BPF_ST).
 */
#define BPF_MEMWORDS 16

#ifdef _SYS_EVENTHANDLER_H_
/* BPF attach/detach events */
struct ifnet;
typedef void (*bpf_track_fn)(void *, struct ifnet *, int /* dlt */,
    int /* 1 =>'s attach */);
EVENTHANDLER_DECLARE(bpf_track, bpf_track_fn);
#endif /* _SYS_EVENTHANDLER_H_ */

#endif /* _NET_BPF_H_ */
