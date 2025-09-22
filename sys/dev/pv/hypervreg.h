/*-
 * Copyright (c) 2009-2012,2016 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#ifndef _DEV_PV_HYPERVREG_H_
#define _DEV_PV_HYPERVREG_H_

struct hv_guid {
	 unsigned char			data[16];
} __packed;

#define VMBUS_CONNID_MESSAGE		1
#define VMBUS_CONNID_EVENT		2
#define VMBUS_SINT_MESSAGE		2

#define VMBUS_GPADL_START		0xffff /* 0x10000 effectively */

/*
 * $FreeBSD: head/sys/dev/hyperv/vmbus/hyperv_reg.h 303283 2016-07-25 03:12:40Z sephe $
 */

/*
 * Hyper-V Synthetic MSRs
 */

#define MSR_HV_GUEST_OS_ID		0x40000000
#define MSR_HV_GUESTID_BUILD_MASK	0xffffULL
#define MSR_HV_GUESTID_VERSION_MASK	0x0000ffffffff0000ULL
#define MSR_HV_GUESTID_VERSION_SHIFT	16
#define MSR_HV_GUESTID_OSID_MASK	0x00ff000000000000ULL
#define MSR_HV_GUESTID_OSID_SHIFT	48
#define MSR_HV_GUESTID_OSTYPE_MASK	0x7f00000000000000ULL
#define MSR_HV_GUESTID_OSTYPE_SHIFT	56
#define MSR_HV_GUESTID_OPENSRC		0x8000000000000000ULL
#define MSR_HV_GUESTID_OSID_OPENBSD	0x0001000000000000ULL
#define MSR_HV_GUESTID_OSTYPE_LINUX	\
	((0x01ULL << MSR_HV_GUESTID_OSTYPE_SHIFT) | MSR_HV_GUESTID_OPENSRC)
#define MSR_HV_GUESTID_OSTYPE_FREEBSD	\
	((0x02ULL << MSR_HV_GUESTID_OSTYPE_SHIFT) | MSR_HV_GUESTID_OPENSRC)
#define MSR_HV_GUESTID_OSTYPE_OPENBSD	\
	((0x02ULL << MSR_HV_GUESTID_OSTYPE_SHIFT) | MSR_HV_GUESTID_OPENSRC | \
	 MSR_HV_GUESTID_OSID_OPENBSD)

#define MSR_HV_HYPERCALL		0x40000001
#define MSR_HV_HYPERCALL_ENABLE		0x0001ULL
#define MSR_HV_HYPERCALL_RSVD_MASK	0x0ffeULL
#define MSR_HV_HYPERCALL_PGSHIFT	12

#define MSR_HV_VP_INDEX			0x40000002

#define MSR_HV_TIME_REF_COUNT		0x40000020

#define MSR_HV_SCONTROL			0x40000080
#define MSR_HV_SCTRL_ENABLE		0x0001ULL
#define MSR_HV_SCTRL_RSVD_MASK		0xfffffffffffffffeULL

#define MSR_HV_SIEFP			0x40000082
#define MSR_HV_SIEFP_ENABLE		0x0001ULL
#define MSR_HV_SIEFP_RSVD_MASK		0x0ffeULL
#define MSR_HV_SIEFP_PGSHIFT		12

#define MSR_HV_SIMP			0x40000083
#define MSR_HV_SIMP_ENABLE		0x0001ULL
#define MSR_HV_SIMP_RSVD_MASK		0x0ffeULL
#define MSR_HV_SIMP_PGSHIFT		12

#define MSR_HV_EOM			0x40000084

#define MSR_HV_SINT0			0x40000090
#define MSR_HV_SINT_VECTOR_MASK		0x00ffULL
#define MSR_HV_SINT_RSVD1_MASK		0xff00ULL
#define MSR_HV_SINT_MASKED		0x00010000ULL
#define MSR_HV_SINT_AUTOEOI		0x00020000ULL
#define MSR_HV_SINT_RSVD2_MASK		0xfffffffffffc0000ULL
#define MSR_HV_SINT_RSVD_MASK		(MSR_HV_SINT_RSVD1_MASK |	\
					 MSR_HV_SINT_RSVD2_MASK)

#define MSR_HV_STIMER0_CONFIG		0x400000b0
#define MSR_HV_STIMER_CFG_ENABLE	0x0001ULL
#define MSR_HV_STIMER_CFG_PERIODIC	0x0002ULL
#define MSR_HV_STIMER_CFG_LAZY		0x0004ULL
#define MSR_HV_STIMER_CFG_AUTOEN	0x0008ULL
#define MSR_HV_STIMER_CFG_SINT_MASK	0x000f0000ULL
#define MSR_HV_STIMER_CFG_SINT_SHIFT	16

#define MSR_HV_STIMER0_COUNT		0x400000b1

/*
 * CPUID leaves
 */

#define CPUID_LEAF_HV_MAXLEAF		0x40000000

#define CPUID_LEAF_HV_INTERFACE		0x40000001
#define CPUID_HV_IFACE_HYPERV		0x31237648	/* HV#1 */

#define CPUID_LEAF_HV_IDENTITY		0x40000002

#define CPUID_LEAF_HV_FEATURES		0x40000003
/* EAX: features */
#define CPUID_HV_MSR_TIME_REFCNT	0x0002	/* MSR_HV_TIME_REF_COUNT */
#define CPUID_HV_MSR_SYNIC		0x0004	/* MSRs for SynIC */
#define CPUID_HV_MSR_SYNTIMER		0x0008	/* MSRs for SynTimer */
#define CPUID_HV_MSR_APIC		0x0010	/* MSR_HV_{EOI,ICR,TPR} */
#define CPUID_HV_MSR_HYPERCALL		0x0020	/* MSR_HV_GUEST_OS_ID
						 * MSR_HV_HYPERCALL */
#define CPUID_HV_MSR_VP_INDEX		0x0040	/* MSR_HV_VP_INDEX */
#define CPUID_HV_MSR_GUEST_IDLE		0x0400	/* MSR_HV_GUEST_IDLE */
/* ECX: power management features */
#define CPUPM_HV_CSTATE_MASK		0x000f	/* deepest C-state */
#define CPUPM_HV_C3_HPET		0x0010	/* C3 requires HPET */
#define CPUPM_HV_CSTATE(f)		((f) & CPUPM_HV_CSTATE_MASK)
/* EDX: features3 */
#define CPUID3_HV_MWAIT			0x0001	/* MWAIT */
#define CPUID3_HV_XMM_HYPERCALL		0x0010	/* Hypercall input through
						 * XMM regs */
#define CPUID3_HV_GUEST_IDLE		0x0020	/* guest idle */
#define CPUID3_HV_NUMA			0x0080	/* NUMA distance query */
#define CPUID3_HV_TIME_FREQ		0x0100	/* timer frequency query
						 * (TSC, LAPIC) */
#define CPUID3_HV_MSR_CRASH		0x0400	/* MSRs for guest crash */

#define CPUID_LEAF_HV_RECOMMENDS	0x40000004
#define CPUID_LEAF_HV_LIMITS		0x40000005
#define CPUID_LEAF_HV_HWFEATURES	0x40000006

/*
 * Hyper-V Monitor Notification Facility
 */
struct hv_mon_param {
	uint32_t	mp_connid;
	uint16_t	mp_evtflag_ofs;
	uint16_t	mp_rsvd;
} __packed;

/*
 * Hyper-V message types
 */
#define VMBUS_MSGTYPE_NONE		0
#define VMBUS_MSGTYPE_CHANNEL		1
#define VMBUS_MSGTYPE_TIMER_EXPIRED	0x80000010

/*
 * Hypercall status codes
 */
#define HYPERCALL_STATUS_SUCCESS	0x0000

/*
 * Hypercall input values
 */
#define HYPERCALL_POST_MESSAGE		0x005c
#define HYPERCALL_SIGNAL_EVENT		0x005d

/*
 * Hypercall input parameters
 */
#define HYPERCALL_PARAM_ALIGN		8
#if 0
/*
 * XXX
 * <<Hypervisor Top Level Functional Specification 4.0b>> requires
 * input parameters size to be multiple of 8, however, many post
 * message input parameters do _not_ meet this requirement.
 */
#define HYPERCALL_PARAM_SIZE_ALIGN	8
#endif

/*
 * HYPERCALL_POST_MESSAGE
 */
#define HYPERCALL_POSTMSGIN_DSIZE_MAX	240
#define HYPERCALL_POSTMSGIN_SIZE	256

struct hypercall_postmsg_in {
	uint32_t	hc_connid;
	uint32_t	hc_rsvd;
	uint32_t	hc_msgtype;	/* VMBUS_MSGTYPE_ */
	uint32_t	hc_dsize;
	uint8_t		hc_data[HYPERCALL_POSTMSGIN_DSIZE_MAX];
} __packed;

/*
 * $FreeBSD: head/sys/dev/hyperv/include/vmbus.h 306389 2016-09-28 04:25:25Z sephe $
 */

/*
 * VMBUS version is 32 bit, upper 16 bit for major_number and lower
 * 16 bit for minor_number.
 *
 * 0.13  --  Windows Server 2008
 * 1.1   --  Windows 7
 * 2.4   --  Windows 8
 * 3.0   --  Windows 8.1
 * 4.0   --  Windows 10
 */
#define VMBUS_VERSION_WS2008		((0 << 16) | (13))
#define VMBUS_VERSION_WIN7		((1 << 16) | (1))
#define VMBUS_VERSION_WIN8		((2 << 16) | (4))
#define VMBUS_VERSION_WIN8_1		((3 << 16) | (0))
#define VMBUS_VERSION_WIN10		((4 << 16) | (0))

#define VMBUS_VERSION_MAJOR(ver)	(((uint32_t)(ver)) >> 16)
#define VMBUS_VERSION_MINOR(ver)	(((uint32_t)(ver)) & 0xffff)

/*
 * GPA stuffs.
 */
struct vmbus_gpa_range {
	uint32_t	gpa_len;
	uint32_t	gpa_ofs;
	uint64_t	gpa_page[0];
} __packed;

/* This is actually vmbus_gpa_range.gpa_page[1] */
struct vmbus_gpa {
	uint32_t	gpa_len;
	uint32_t	gpa_ofs;
	uint64_t	gpa_page;
} __packed;

#define VMBUS_CHANPKT_SIZE_SHIFT	3

#define VMBUS_CHANPKT_GETLEN(pktlen)	\
	(((int)(pktlen)) << VMBUS_CHANPKT_SIZE_SHIFT)

struct vmbus_chanpkt_hdr {
	uint16_t	cph_type;	/* VMBUS_CHANPKT_TYPE_ */
	uint16_t	cph_hlen;	/* header len, in 8 bytes */
	uint16_t	cph_tlen;	/* total len, in 8 bytes */
	uint16_t	cph_flags;	/* VMBUS_CHANPKT_FLAG_ */
	uint64_t	cph_tid;
} __packed;

#define VMBUS_CHANPKT_TYPE_INBAND	0x0006
#define VMBUS_CHANPKT_TYPE_RXBUF	0x0007
#define VMBUS_CHANPKT_TYPE_GPA		0x0009
#define VMBUS_CHANPKT_TYPE_COMP		0x000b

#define VMBUS_CHANPKT_FLAG_RC		0x0001	/* report completion */

#define VMBUS_CHANPKT_CONST_DATA(pkt)			\
	((const void *)((const uint8_t *)(pkt) +	\
	    VMBUS_CHANPKT_GETLEN((pkt)->cph_hlen)))

/*
 * $FreeBSD: head/sys/dev/hyperv/vmbus/vmbus_reg.h 305405 2016-09-05 03:21:31Z sephe $
 */

/*
 * Hyper-V SynIC message format.
 */

#define VMBUS_MSG_DSIZE_MAX		240
#define VMBUS_MSG_SIZE			256

struct vmbus_message {
	uint32_t	msg_type;	/* VMBUS_MSGTYPE_ */
	uint8_t		msg_dsize;	/* data size */
	uint8_t		msg_flags;	/* VMBUS_MSGFLAG_ */
	uint16_t	msg_rsvd;
	uint64_t	msg_id;
	uint8_t		msg_data[VMBUS_MSG_DSIZE_MAX];
} __packed;

#define VMBUS_MSGFLAG_PENDING		0x01

/*
 * Hyper-V SynIC event flags
 */

#define VMBUS_EVTFLAGS_SIZE	256
#define VMBUS_EVTFLAGS_MAX	((VMBUS_EVTFLAGS_SIZE / LONG_BIT) * 8)
#define VMBUS_EVTFLAG_LEN	LONG_BIT
#define VMBUS_EVTFLAG_MASK	(LONG_BIT - 1)

struct vmbus_evtflags {
	ulong		evt_flags[VMBUS_EVTFLAGS_MAX];
} __packed;

/*
 * Hyper-V Monitor Notification Facility
 */

struct vmbus_mon_trig {
	uint32_t	mt_pending;
	uint32_t	mt_armed;
} __packed;

#define VMBUS_MONTRIGS_MAX	4
#define VMBUS_MONTRIG_LEN	32

struct vmbus_mnf {
	uint32_t	mnf_state;
	uint32_t	mnf_rsvd1;

	struct vmbus_mon_trig
			mnf_trigs[VMBUS_MONTRIGS_MAX];
	uint8_t		mnf_rsvd2[536];

	uint16_t	mnf_lat[VMBUS_MONTRIGS_MAX][VMBUS_MONTRIG_LEN];
	uint8_t		mnf_rsvd3[256];

	struct hv_mon_param
			mnf_param[VMBUS_MONTRIGS_MAX][VMBUS_MONTRIG_LEN];
	uint8_t		mnf_rsvd4[1984];
} __packed;

/*
 * Buffer ring
 */
struct vmbus_bufring {
	/*
	 * If br_windex == br_rindex, this bufring is empty; this
	 * means we can _not_ write data to the bufring, if the
	 * write is going to make br_windex same as br_rindex.
	 */
	volatile uint32_t	br_windex;
	volatile uint32_t	br_rindex;

	/*
	 * Interrupt mask {0,1}
	 *
	 * For TX bufring, host set this to 1, when it is processing
	 * the TX bufring, so that we can safely skip the TX event
	 * notification to host.
	 *
	 * For RX bufring, once this is set to 1 by us, host will not
	 * further dispatch interrupts to us, even if there are data
	 * pending on the RX bufring.  This effectively disables the
	 * interrupt of the channel to which this RX bufring is attached.
	 */
	volatile uint32_t	br_imask;

	uint8_t			br_rsvd[4084];
	uint8_t			br_data[0];
} __packed;

/*
 * Channel
 */

#define VMBUS_CHAN_MAX_COMPAT	256
#define VMBUS_CHAN_MAX		(VMBUS_EVTFLAG_LEN * VMBUS_EVTFLAGS_MAX)

/*
 * Channel packets
 */

#define VMBUS_CHANPKT_SIZE_ALIGN	(1 << VMBUS_CHANPKT_SIZE_SHIFT)

#define VMBUS_CHANPKT_SETLEN(pktlen, len)		\
do {							\
	(pktlen) = (len) >> VMBUS_CHANPKT_SIZE_SHIFT;	\
} while (0)

struct vmbus_chanpkt {
	struct vmbus_chanpkt_hdr cp_hdr;
} __packed;

struct vmbus_chanpkt_sglist {
	struct vmbus_chanpkt_hdr cp_hdr;
	uint32_t	cp_rsvd;
	uint32_t	cp_gpa_cnt;
	struct vmbus_gpa cp_gpa[0];
} __packed;

struct vmbus_chanpkt_prplist {
	struct vmbus_chanpkt_hdr cp_hdr;
	uint32_t	cp_rsvd;
	uint32_t	cp_range_cnt;
	struct vmbus_gpa_range cp_range[0];
} __packed;

/*
 * Channel messages
 * - Embedded in vmbus_message.msg_data, e.g. response and notification.
 * - Embedded in hypercall_postmsg_in.hc_data, e.g. request.
 */

#define VMBUS_CHANMSG_CHOFFER			1	/* NOTE */
#define VMBUS_CHANMSG_CHRESCIND			2	/* NOTE */
#define VMBUS_CHANMSG_CHREQUEST			3	/* REQ */
#define VMBUS_CHANMSG_CHOFFER_DONE		4	/* NOTE */
#define VMBUS_CHANMSG_CHOPEN			5	/* REQ */
#define VMBUS_CHANMSG_CHOPEN_RESP		6	/* RESP */
#define VMBUS_CHANMSG_CHCLOSE			7	/* REQ */
#define VMBUS_CHANMSG_GPADL_CONN		8	/* REQ */
#define VMBUS_CHANMSG_GPADL_SUBCONN		9	/* REQ */
#define VMBUS_CHANMSG_GPADL_CONNRESP		10	/* RESP */
#define VMBUS_CHANMSG_GPADL_DISCONN		11	/* REQ */
#define VMBUS_CHANMSG_GPADL_DISCONNRESP		12	/* RESP */
#define VMBUS_CHANMSG_CHFREE			13	/* REQ */
#define VMBUS_CHANMSG_CONNECT			14	/* REQ */
#define VMBUS_CHANMSG_CONNECT_RESP		15	/* RESP */
#define VMBUS_CHANMSG_DISCONNECT		16	/* REQ */
#define VMBUS_CHANMSG_COUNT			17
#define VMBUS_CHANMSG_MAX			22

struct vmbus_chanmsg_hdr {
	uint32_t	chm_type;	/* VMBUS_CHANMSG_* */
	uint32_t	chm_rsvd;
} __packed;

/* VMBUS_CHANMSG_CONNECT */
struct vmbus_chanmsg_connect {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint32_t	chm_ver;
	uint32_t	chm_rsvd;
	uint64_t	chm_evtflags;
	uint64_t	chm_mnf1;
	uint64_t	chm_mnf2;
} __packed;

/* VMBUS_CHANMSG_CONNECT_RESP */
struct vmbus_chanmsg_connect_resp {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint8_t		chm_done;
} __packed;

/* VMBUS_CHANMSG_CHREQUEST */
struct vmbus_chanmsg_chrequest {
	struct vmbus_chanmsg_hdr chm_hdr;
} __packed;

/* VMBUS_CHANMSG_DISCONNECT */
struct vmbus_chanmsg_disconnect {
	struct vmbus_chanmsg_hdr chm_hdr;
} __packed;

/* VMBUS_CHANMSG_CHOPEN */
struct vmbus_chanmsg_chopen {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint32_t	chm_chanid;
	uint32_t	chm_openid;
	uint32_t	chm_gpadl;
	uint32_t	chm_vcpuid;
	uint32_t	chm_txbr_pgcnt;
#define VMBUS_CHANMSG_CHOPEN_UDATA_SIZE	120
	uint8_t		chm_udata[VMBUS_CHANMSG_CHOPEN_UDATA_SIZE];
} __packed;

/* VMBUS_CHANMSG_CHOPEN_RESP */
struct vmbus_chanmsg_chopen_resp {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint32_t	chm_chanid;
	uint32_t	chm_openid;
	uint32_t	chm_status;
} __packed;

/* VMBUS_CHANMSG_GPADL_CONN */
struct vmbus_chanmsg_gpadl_conn {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint32_t	chm_chanid;
	uint32_t	chm_gpadl;
	uint16_t	chm_range_len;
	uint16_t	chm_range_cnt;
	struct vmbus_gpa_range chm_range;
} __packed;

#define VMBUS_CHANMSG_GPADL_CONN_PGMAX		26

/* VMBUS_CHANMSG_GPADL_SUBCONN */
struct vmbus_chanmsg_gpadl_subconn {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint32_t	chm_msgno;
	uint32_t	chm_gpadl;
	uint64_t	chm_gpa_page[0];
} __packed;

#define VMBUS_CHANMSG_GPADL_SUBCONN_PGMAX	28

/* VMBUS_CHANMSG_GPADL_CONNRESP */
struct vmbus_chanmsg_gpadl_connresp {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint32_t	chm_chanid;
	uint32_t	chm_gpadl;
	uint32_t	chm_status;
} __packed;

/* VMBUS_CHANMSG_CHCLOSE */
struct vmbus_chanmsg_chclose {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint32_t	chm_chanid;
} __packed;

/* VMBUS_CHANMSG_GPADL_DISCONN */
struct vmbus_chanmsg_gpadl_disconn {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint32_t	chm_chanid;
	uint32_t	chm_gpadl;
} __packed;

/* VMBUS_CHANMSG_CHFREE */
struct vmbus_chanmsg_chfree {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint32_t	chm_chanid;
} __packed;

/* VMBUS_CHANMSG_CHRESCIND */
struct vmbus_chanmsg_chrescind {
	struct vmbus_chanmsg_hdr chm_hdr;
	uint32_t	chm_chanid;
} __packed;

/* VMBUS_CHANMSG_CHOFFER */
struct vmbus_chanmsg_choffer {
	struct vmbus_chanmsg_hdr chm_hdr;
	struct hv_guid	chm_chtype;
	struct hv_guid	chm_chinst;
	uint64_t	chm_chlat;	/* unit: 100ns */
	uint32_t	chm_chrev;
	uint32_t	chm_svrctx_sz;
	uint16_t	chm_chflags;
	uint16_t	chm_mmio_sz;	/* unit: MB */
	uint8_t		chm_udata[120];
	uint16_t	chm_subidx;
	uint16_t	chm_rsvd;
	uint32_t	chm_chanid;
	uint8_t		chm_montrig;
	uint8_t		chm_flags1;	/* VMBUS_CHOFFER_FLAG1_ */
	uint16_t	chm_flags2;
	uint32_t	chm_connid;
} __packed;

#define VMBUS_CHOFFER_FLAG1_HASMNF	0x01

#endif	/* _DEV_PV_HYPERVREG_H_ */
