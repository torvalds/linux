/*	$OpenBSD: bfd.h,v 1.14 2025/07/24 00:49:22 jsg Exp $	*/

/*
 * Copyright (c) 2016 Peter Hessler <phessler@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Support for Bi-directional Forwarding Detection (RFC 5880 / 5881)
 */

#ifndef _NET_BFD_H_
#define _NET_BFD_H_

/* Public Interface */

#define BFD_MODE_ASYNC			1
#define BFD_MODE_DEMAND			2

/* Diagnostic Code (RFC 5880 Page 8) */
#define BFD_DIAG_NONE			0
#define BFD_DIAG_EXPIRED		1
#define BFD_DIAG_ECHO_FAILED		2
#define BFD_DIAG_NEIGHBOR_SIGDOWN	3
#define BFD_DIAG_FIB_RESET		4
#define BFD_DIAG_PATH_DOWN		5
#define BFD_DIAG_CONCAT_PATH_DOWN	6
#define BFD_DIAG_ADMIN_DOWN		7
#define BFD_DIAG_CONCAT_REVERSE_DOWN	8

/* State (RFC 5880 Page 8) */
#define BFD_STATE_ADMINDOWN		0
#define BFD_STATE_DOWN			1
#define BFD_STATE_INIT			2
#define BFD_STATE_UP			3

/* Flags (RFC 5880 Page 8) */
#define BFD_FLAG_P			0x20
#define BFD_FLAG_F			0x10
#define BFD_FLAG_C			0x08
#define BFD_FLAG_A			0x04
#define BFD_FLAG_D			0x02
#define BFD_FLAG_M			0x01

struct sockaddr_bfd {
	uint8_t		bs_len;		/* total length */
	uint8_t		bs_family;	/* address family */
	/* above matches sockaddr_storage */

	/* Sorted for bit boundaries */
	uint16_t	bs_mode;
	uint32_t	bs_localdiscr;

	int64_t		bs_uptime;

	int64_t		bs_lastuptime;

	uint32_t	bs_mintx;
	uint32_t	bs_minrx;

	uint32_t	bs_minecho;
	uint32_t	bs_localdiag;

	uint32_t	bs_remotediscr;
	uint32_t	bs_remotediag;

	uint16_t	bs_multiplier;
	uint16_t	bs_pad0;
	unsigned int	bs_state;
	unsigned int	bs_remotestate;
	unsigned int	bs_laststate;
	unsigned int	bs_error;

	/* add padding to reach a power of two */
	uint64_t	bs_pad1;
};

struct bfd_msghdr {
	uint16_t	bm_msglen;
	uint8_t		bm_version;
	uint8_t		bm_type;
	uint16_t	bm_hdrlen;
	uint16_t	bm_index;

	uint16_t	bm_tableid;
	uint8_t		bm_priority;
	uint8_t		bm_mpls;
	int		bm_addrs;
	int		bm_flags;
	/* above matches rt_msghdr */
	uint16_t	bm_pad0;	/* for 4 byte boundary */

	struct sockaddr_bfd	bm_sa;	/* bfd msg for userland */
};

#ifdef _KERNEL
/* state machine from RFC 5880 6.8.1*/
struct bfd_neighbor {
	uint32_t	bn_lstate;		/* SessionState */
	uint32_t	bn_rstate;		/* RemoteSessionState */
	uint32_t	bn_ldiscr;		/* LocalDiscr */
	uint32_t	bn_rdiscr;		/* RemoteDiscr */
	uint32_t	bn_ldiag;		/* LocalDiag */
	uint32_t	bn_rdiag;		/* RemoteDiag */
	uint32_t	bn_mintx;		/* DesiredMinTxInterval */
	uint32_t	bn_req_minrx;		/* RequiredMinRxInterval */
	uint32_t	bn_rminrx;		/* RemoteMinRxInterval */
	uint32_t	bn_demand;		/* DemandMode */
	uint32_t	bn_rdemand;		/* RemoteDemandMode */
	uint32_t	bn_authtype;		/* AuthType */
	uint32_t	bn_rauthseq;		/* RcvAuthSeq */
	uint32_t	bn_lauthseq;		/* XmitAuthSeq */
	uint32_t	bn_authseqknown;	/* AuthSeqKnown */
	uint16_t	bn_mult;		/* DetectMult */
};

struct bfd_config {
	TAILQ_ENTRY(bfd_config)	 bc_entry;
	struct socket		*bc_so;
	struct socket		*bc_upcallso;
	struct socket		*bc_soecho;
	struct socket		*bc_sosend;
	struct rtentry		*bc_rt;
	struct bfd_neighbor	*bc_neighbor;
	struct timeval		*bc_time;
	struct task		 bc_bfd_task;
	struct task		 bc_bfd_send_task;
	struct task		 bc_upcall_task;
	struct task		 bc_clear_task;
	struct timeout		 bc_timo_rx;
	struct timeout		 bc_timo_tx;
	time_t			 bc_lastuptime;
	unsigned int		 bc_laststate;
	unsigned int		 bc_state;
	unsigned int		 bc_poll;
	unsigned int		 bc_error;
	uint32_t		 bc_minrx;
	uint32_t		 bc_mintx;
	uint32_t		 bc_minecho;
	uint16_t		 bc_multiplier;
	uint16_t		 bc_mode;
};

struct sockaddr *bfd2sa(const struct rtentry *, struct sockaddr_bfd *);

int		 bfdset(struct rtentry *);
void		 bfdclear(struct rtentry *);
void		 bfdinit(void);

#endif /* _KERNEL */

#endif /* _NET_BFD_H_ */
