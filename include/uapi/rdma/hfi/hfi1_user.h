/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * This file contains defines, structures, etc. that are used
 * to communicate between kernel and user code.
 */

#ifndef _LINUX__HFI1_USER_H
#define _LINUX__HFI1_USER_H

#include <linux/types.h>

/*
 * This version number is given to the driver by the user code during
 * initialization in the spu_userversion field of hfi1_user_info, so
 * the driver can check for compatibility with user code.
 *
 * The major version changes when data structures change in an incompatible
 * way. The driver must be the same for initialization to succeed.
 */
#define HFI1_USER_SWMAJOR 6

/*
 * Minor version differences are always compatible
 * a within a major version, however if user software is larger
 * than driver software, some new features and/or structure fields
 * may not be implemented; the user code must deal with this if it
 * cares, or it must abort after initialization reports the difference.
 */
#define HFI1_USER_SWMINOR 3

/*
 * We will encode the major/minor inside a single 32bit version number.
 */
#define HFI1_SWMAJOR_SHIFT 16

/*
 * Set of HW and driver capability/feature bits.
 * These bit values are used to configure enabled/disabled HW and
 * driver features. The same set of bits are communicated to user
 * space.
 */
#define HFI1_CAP_DMA_RTAIL        (1UL <<  0) /* Use DMA'ed RTail value */
#define HFI1_CAP_SDMA             (1UL <<  1) /* Enable SDMA support */
#define HFI1_CAP_SDMA_AHG         (1UL <<  2) /* Enable SDMA AHG support */
#define HFI1_CAP_EXTENDED_PSN     (1UL <<  3) /* Enable Extended PSN support */
#define HFI1_CAP_HDRSUPP          (1UL <<  4) /* Enable Header Suppression */
/* 1UL << 5 unused */
#define HFI1_CAP_USE_SDMA_HEAD    (1UL <<  6) /* DMA Hdr Q tail vs. use CSR */
#define HFI1_CAP_MULTI_PKT_EGR    (1UL <<  7) /* Enable multi-packet Egr buffs*/
#define HFI1_CAP_NODROP_RHQ_FULL  (1UL <<  8) /* Don't drop on Hdr Q full */
#define HFI1_CAP_NODROP_EGR_FULL  (1UL <<  9) /* Don't drop on EGR buffs full */
#define HFI1_CAP_TID_UNMAP        (1UL << 10) /* Disable Expected TID caching */
#define HFI1_CAP_PRINT_UNIMPL     (1UL << 11) /* Show for unimplemented feats */
#define HFI1_CAP_ALLOW_PERM_JKEY  (1UL << 12) /* Allow use of permissive JKEY */
#define HFI1_CAP_NO_INTEGRITY     (1UL << 13) /* Enable ctxt integrity checks */
#define HFI1_CAP_PKEY_CHECK       (1UL << 14) /* Enable ctxt PKey checking */
#define HFI1_CAP_STATIC_RATE_CTRL (1UL << 15) /* Allow PBC.StaticRateControl */
/* 1UL << 16 unused */
#define HFI1_CAP_SDMA_HEAD_CHECK  (1UL << 17) /* SDMA head checking */
#define HFI1_CAP_EARLY_CREDIT_RETURN (1UL << 18) /* early credit return */

#define HFI1_RCVHDR_ENTSIZE_2    (1UL << 0)
#define HFI1_RCVHDR_ENTSIZE_16   (1UL << 1)
#define HFI1_RCVDHR_ENTSIZE_32   (1UL << 2)

/* User commands. */
#define HFI1_CMD_ASSIGN_CTXT     1	/* allocate HFI and context */
#define HFI1_CMD_CTXT_INFO       2	/* find out what resources we got */
#define HFI1_CMD_USER_INFO       3	/* set up userspace */
#define HFI1_CMD_TID_UPDATE      4	/* update expected TID entries */
#define HFI1_CMD_TID_FREE        5	/* free expected TID entries */
#define HFI1_CMD_CREDIT_UPD      6	/* force an update of PIO credit */

#define HFI1_CMD_RECV_CTRL       8	/* control receipt of packets */
#define HFI1_CMD_POLL_TYPE       9	/* set the kind of polling we want */
#define HFI1_CMD_ACK_EVENT       10	/* ack & clear user status bits */
#define HFI1_CMD_SET_PKEY        11     /* set context's pkey */
#define HFI1_CMD_CTXT_RESET      12     /* reset context's HW send context */
#define HFI1_CMD_TID_INVAL_READ  13     /* read TID cache invalidations */
#define HFI1_CMD_GET_VERS	 14	/* get the version of the user cdev */

/*
 * User IOCTLs can not go above 128 if they do then see common.h and change the
 * base for the snoop ioctl
 */
#define IB_IOCTL_MAGIC 0x1b /* See Documentation/ioctl/ioctl-number.txt */

/*
 * Make the ioctls occupy the last 0xf0-0xff portion of the IB range
 */
#define __NUM(cmd) (HFI1_CMD_##cmd + 0xe0)

struct hfi1_cmd;
#define HFI1_IOCTL_ASSIGN_CTXT \
	_IOWR(IB_IOCTL_MAGIC, __NUM(ASSIGN_CTXT), struct hfi1_user_info)
#define HFI1_IOCTL_CTXT_INFO \
	_IOW(IB_IOCTL_MAGIC, __NUM(CTXT_INFO), struct hfi1_ctxt_info)
#define HFI1_IOCTL_USER_INFO \
	_IOW(IB_IOCTL_MAGIC, __NUM(USER_INFO), struct hfi1_base_info)
#define HFI1_IOCTL_TID_UPDATE \
	_IOWR(IB_IOCTL_MAGIC, __NUM(TID_UPDATE), struct hfi1_tid_info)
#define HFI1_IOCTL_TID_FREE \
	_IOWR(IB_IOCTL_MAGIC, __NUM(TID_FREE), struct hfi1_tid_info)
#define HFI1_IOCTL_CREDIT_UPD \
	_IO(IB_IOCTL_MAGIC, __NUM(CREDIT_UPD))
#define HFI1_IOCTL_RECV_CTRL \
	_IOW(IB_IOCTL_MAGIC, __NUM(RECV_CTRL), int)
#define HFI1_IOCTL_POLL_TYPE \
	_IOW(IB_IOCTL_MAGIC, __NUM(POLL_TYPE), int)
#define HFI1_IOCTL_ACK_EVENT \
	_IOW(IB_IOCTL_MAGIC, __NUM(ACK_EVENT), unsigned long)
#define HFI1_IOCTL_SET_PKEY \
	_IOW(IB_IOCTL_MAGIC, __NUM(SET_PKEY), __u16)
#define HFI1_IOCTL_CTXT_RESET \
	_IO(IB_IOCTL_MAGIC, __NUM(CTXT_RESET))
#define HFI1_IOCTL_TID_INVAL_READ \
	_IOWR(IB_IOCTL_MAGIC, __NUM(TID_INVAL_READ), struct hfi1_tid_info)
#define HFI1_IOCTL_GET_VERS \
	_IOR(IB_IOCTL_MAGIC, __NUM(GET_VERS), int)

#define _HFI1_EVENT_FROZEN_BIT         0
#define _HFI1_EVENT_LINKDOWN_BIT       1
#define _HFI1_EVENT_LID_CHANGE_BIT     2
#define _HFI1_EVENT_LMC_CHANGE_BIT     3
#define _HFI1_EVENT_SL2VL_CHANGE_BIT   4
#define _HFI1_EVENT_TID_MMU_NOTIFY_BIT 5
#define _HFI1_MAX_EVENT_BIT _HFI1_EVENT_TID_MMU_NOTIFY_BIT

#define HFI1_EVENT_FROZEN            (1UL << _HFI1_EVENT_FROZEN_BIT)
#define HFI1_EVENT_LINKDOWN          (1UL << _HFI1_EVENT_LINKDOWN_BIT)
#define HFI1_EVENT_LID_CHANGE        (1UL << _HFI1_EVENT_LID_CHANGE_BIT)
#define HFI1_EVENT_LMC_CHANGE        (1UL << _HFI1_EVENT_LMC_CHANGE_BIT)
#define HFI1_EVENT_SL2VL_CHANGE      (1UL << _HFI1_EVENT_SL2VL_CHANGE_BIT)
#define HFI1_EVENT_TID_MMU_NOTIFY    (1UL << _HFI1_EVENT_TID_MMU_NOTIFY_BIT)

/*
 * These are the status bits readable (in ASCII form, 64bit value)
 * from the "status" sysfs file.  For binary compatibility, values
 * must remain as is; removed states can be reused for different
 * purposes.
 */
#define HFI1_STATUS_INITTED       0x1    /* basic initialization done */
/* Chip has been found and initialized */
#define HFI1_STATUS_CHIP_PRESENT 0x20
/* IB link is at ACTIVE, usable for data traffic */
#define HFI1_STATUS_IB_READY     0x40
/* link is configured, LID, MTU, etc. have been set */
#define HFI1_STATUS_IB_CONF      0x80
/* A Fatal hardware error has occurred. */
#define HFI1_STATUS_HWERROR     0x200

/*
 * Number of supported shared contexts.
 * This is the maximum number of software contexts that can share
 * a hardware send/receive context.
 */
#define HFI1_MAX_SHARED_CTXTS 8

/*
 * Poll types
 */
#define HFI1_POLL_TYPE_ANYRCV     0x0
#define HFI1_POLL_TYPE_URGENT     0x1

/*
 * This structure is passed to the driver to tell it where
 * user code buffers are, sizes, etc.   The offsets and sizes of the
 * fields must remain unchanged, for binary compatibility.  It can
 * be extended, if userversion is changed so user code can tell, if needed
 */
struct hfi1_user_info {
	/*
	 * version of user software, to detect compatibility issues.
	 * Should be set to HFI1_USER_SWVERSION.
	 */
	__u32 userversion;
	__u32 pad;
	/*
	 * If two or more processes wish to share a context, each process
	 * must set the subcontext_cnt and subcontext_id to the same
	 * values.  The only restriction on the subcontext_id is that
	 * it be unique for a given node.
	 */
	__u16 subctxt_cnt;
	__u16 subctxt_id;
	/* 128bit UUID passed in by PSM. */
	__u8 uuid[16];
};

struct hfi1_ctxt_info {
	__u64 runtime_flags;    /* chip/drv runtime flags (HFI1_CAP_*) */
	__u32 rcvegr_size;      /* size of each eager buffer */
	__u16 num_active;       /* number of active units */
	__u16 unit;             /* unit (chip) assigned to caller */
	__u16 ctxt;             /* ctxt on unit assigned to caller */
	__u16 subctxt;          /* subctxt on unit assigned to caller */
	__u16 rcvtids;          /* number of Rcv TIDs for this context */
	__u16 credits;          /* number of PIO credits for this context */
	__u16 numa_node;        /* NUMA node of the assigned device */
	__u16 rec_cpu;          /* cpu # for affinity (0xffff if none) */
	__u16 send_ctxt;        /* send context in use by this user context */
	__u16 egrtids;          /* number of RcvArray entries for Eager Rcvs */
	__u16 rcvhdrq_cnt;      /* number of RcvHdrQ entries */
	__u16 rcvhdrq_entsize;  /* size (in bytes) for each RcvHdrQ entry */
	__u16 sdma_ring_size;   /* number of entries in SDMA request ring */
};

struct hfi1_tid_info {
	/* virtual address of first page in transfer */
	__u64 vaddr;
	/* pointer to tid array. this array is big enough */
	__u64 tidlist;
	/* number of tids programmed by this request */
	__u32 tidcnt;
	/* length of transfer buffer programmed by this request */
	__u32 length;
};

enum hfi1_sdma_comp_state {
	FREE = 0,
	QUEUED,
	COMPLETE,
	ERROR
};

/*
 * SDMA completion ring entry
 */
struct hfi1_sdma_comp_entry {
	__u32 status;
	__u32 errcode;
};

/*
 * Device status and notifications from driver to user-space.
 */
struct hfi1_status {
	__u64 dev;      /* device/hw status bits */
	__u64 port;     /* port state and status bits */
	char freezemsg[0];
};

/*
 * This structure is returned by the driver immediately after
 * open to get implementation-specific info, and info specific to this
 * instance.
 *
 * This struct must have explicit pad fields where type sizes
 * may result in different alignments between 32 and 64 bit
 * programs, since the 64 bit * bit kernel requires the user code
 * to have matching offsets
 */
struct hfi1_base_info {
	/* version of hardware, for feature checking. */
	__u32 hw_version;
	/* version of software, for feature checking. */
	__u32 sw_version;
	/* Job key */
	__u16 jkey;
	__u16 padding1;
	/*
	 * The special QP (queue pair) value that identifies PSM
	 * protocol packet from standard IB packets.
	 */
	__u32 bthqp;
	/* PIO credit return address, */
	__u64 sc_credits_addr;
	/*
	 * Base address of write-only pio buffers for this process.
	 * Each buffer has sendpio_credits*64 bytes.
	 */
	__u64 pio_bufbase_sop;
	/*
	 * Base address of write-only pio buffers for this process.
	 * Each buffer has sendpio_credits*64 bytes.
	 */
	__u64 pio_bufbase;
	/* address where receive buffer queue is mapped into */
	__u64 rcvhdr_bufbase;
	/* base address of Eager receive buffers. */
	__u64 rcvegr_bufbase;
	/* base address of SDMA completion ring */
	__u64 sdma_comp_bufbase;
	/*
	 * User register base for init code, not to be used directly by
	 * protocol or applications.  Always maps real chip register space.
	 * the register addresses are:
	 * ur_rcvhdrhead, ur_rcvhdrtail, ur_rcvegrhead, ur_rcvegrtail,
	 * ur_rcvtidflow
	 */
	__u64 user_regbase;
	/* notification events */
	__u64 events_bufbase;
	/* status page */
	__u64 status_bufbase;
	/* rcvhdrtail update */
	__u64 rcvhdrtail_base;
	/*
	 * shared memory pages for subctxts if ctxt is shared; these cover
	 * all the processes in the group sharing a single context.
	 * all have enough space for the num_subcontexts value on this job.
	 */
	__u64 subctxt_uregbase;
	__u64 subctxt_rcvegrbuf;
	__u64 subctxt_rcvhdrbuf;
};

enum sdma_req_opcode {
	EXPECTED = 0,
	EAGER
};

#define HFI1_SDMA_REQ_VERSION_MASK 0xF
#define HFI1_SDMA_REQ_VERSION_SHIFT 0x0
#define HFI1_SDMA_REQ_OPCODE_MASK 0xF
#define HFI1_SDMA_REQ_OPCODE_SHIFT 0x4
#define HFI1_SDMA_REQ_IOVCNT_MASK 0xFF
#define HFI1_SDMA_REQ_IOVCNT_SHIFT 0x8

struct sdma_req_info {
	/*
	 * bits 0-3 - version (currently unused)
	 * bits 4-7 - opcode (enum sdma_req_opcode)
	 * bits 8-15 - io vector count
	 */
	__u16 ctrl;
	/*
	 * Number of fragments contained in this request.
	 * User-space has already computed how many
	 * fragment-sized packet the user buffer will be
	 * split into.
	 */
	__u16 npkts;
	/*
	 * Size of each fragment the user buffer will be
	 * split into.
	 */
	__u16 fragsize;
	/*
	 * Index of the slot in the SDMA completion ring
	 * this request should be using. User-space is
	 * in charge of managing its own ring.
	 */
	__u16 comp_idx;
} __packed;

/*
 * SW KDETH header.
 * swdata is SW defined portion.
 */
struct hfi1_kdeth_header {
	__le32 ver_tid_offset;
	__le16 jkey;
	__le16 hcrc;
	__le32 swdata[7];
} __packed;

/*
 * Structure describing the headers that User space uses. The
 * structure above is a subset of this one.
 */
struct hfi1_pkt_header {
	__le16 pbc[4];
	__be16 lrh[4];
	__be32 bth[3];
	struct hfi1_kdeth_header kdeth;
} __packed;


/*
 * The list of usermode accessible registers.
 */
enum hfi1_ureg {
	/* (RO)  DMA RcvHdr to be used next. */
	ur_rcvhdrtail = 0,
	/* (RW)  RcvHdr entry to be processed next by host. */
	ur_rcvhdrhead = 1,
	/* (RO)  Index of next Eager index to use. */
	ur_rcvegrindextail = 2,
	/* (RW)  Eager TID to be processed next */
	ur_rcvegrindexhead = 3,
	/* (RO)  Receive Eager Offset Tail */
	ur_rcvegroffsettail = 4,
	/* For internal use only; max register number. */
	ur_maxreg,
	/* (RW)  Receive TID flow table */
	ur_rcvtidflowtable = 256
};

#endif /* _LINIUX__HFI1_USER_H */
