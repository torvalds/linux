/*
 * Copyright (c) 2018-2019 Cavium, Inc.
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */


/*
 * File: qlnxr_def.h
 * Author: David C Somayajulu
 */

#ifndef __QLNX_DEF_H_
#define __QLNX_DEF_H_

#include <sys/ktr.h>

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/completion.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/wait.h>
#include <linux/kref.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <sys/vmem.h>

#include <asm/byteorder.h>

#include <netinet/in.h>
#include <net/ipv6.h>
#include <netinet/toecore.h>

#include <rdma/ib_smi.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_verbs.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_sa.h>

#if __FreeBSD_version < 1100000
#undef MODULE_VERSION
#endif

#include "qlnx_os.h"
#include "bcm_osal.h"

#include "reg_addr.h"
#include "ecore_gtt_reg_addr.h"
#include "ecore.h"
#include "ecore_chain.h"
#include "ecore_status.h"
#include "ecore_hw.h"
#include "ecore_rt_defs.h"
#include "ecore_init_ops.h"
#include "ecore_int.h"
#include "ecore_cxt.h"
#include "ecore_spq.h"
#include "ecore_init_fw_funcs.h"
#include "ecore_sp_commands.h"
#include "ecore_dev_api.h"
#include "ecore_l2_api.h"
#ifdef CONFIG_ECORE_SRIOV
#include "ecore_sriov.h"
#include "ecore_vf.h"
#endif
#ifdef CONFIG_ECORE_LL2
#include "ecore_ll2.h"
#endif
#ifdef CONFIG_ECORE_FCOE
#include "ecore_fcoe.h"
#endif
#ifdef CONFIG_ECORE_ISCSI
#include "ecore_iscsi.h"
#endif
#include "ecore_mcp.h"
#include "ecore_hw_defs.h"
#include "mcp_public.h"

#ifdef CONFIG_ECORE_RDMA
#include "ecore_rdma.h"
#include "ecore_rdma_api.h"
#endif

#ifdef CONFIG_ECORE_ROCE
#include "ecore_roce.h"
#endif

#ifdef CONFIG_ECORE_IWARP
#include "ecore_iwarp.h"
#endif

#include "ecore_iro.h"
#include "nvm_cfg.h"

#include "ecore_dbg_fw_funcs.h"
#include "rdma_common.h"

#include "qlnx_ioctl.h"
#include "qlnx_def.h"
#include "qlnx_rdma.h"
#include "qlnxr_verbs.h"
#include "qlnxr_user.h"
#include "qlnx_ver.h"
#include <sys/smp.h>

#define QLNXR_ROCE_INTERFACE_VERSION     1801

#define QLNXR_MODULE_VERSION     "8.18.1.0"
#define QLNXR_NODE_DESC "QLogic 579xx RoCE HCA"

#define OC_SKH_DEVICE_PF 0x720
#define OC_SKH_DEVICE_VF 0x728
#define QLNXR_MAX_AH 512

/* QLNXR Limitations */

/* SQ/RQ Limitations
 * An S/RQ PBL contains a list a pointers to pages. Each page contains S/RQE
 * elements. Several S/RQE elements make an S/RQE, up to a certain maximum that
 * is different between SQ and RQ. The size of the PBL was chosen such as not to
 * limit the MAX_WR supported by ECORE, and rounded up to a power of two.
 */
/* SQ */
#define QLNXR_MAX_SQ_PBL (0x8000) /* 2^15 bytes */
#define QLNXR_MAX_SQ_PBL_ENTRIES (0x10000 / sizeof(void *)) /* number */
#define QLNXR_SQE_ELEMENT_SIZE (sizeof(struct rdma_sq_sge)) /* bytes */
#define QLNXR_MAX_SQE_ELEMENTS_PER_SQE (ROCE_REQ_MAX_SINGLE_SQ_WQE_SIZE / \
                QLNXR_SQE_ELEMENT_SIZE) /* number */
#define QLNXR_MAX_SQE_ELEMENTS_PER_PAGE ((RDMA_RING_PAGE_SIZE) / \
                QLNXR_SQE_ELEMENT_SIZE) /* number */
#define QLNXR_MAX_SQE ((QLNXR_MAX_SQ_PBL_ENTRIES) * (RDMA_RING_PAGE_SIZE) / \
                (QLNXR_SQE_ELEMENT_SIZE) / (QLNXR_MAX_SQE_ELEMENTS_PER_SQE))
/* RQ */
#define QLNXR_MAX_RQ_PBL (0x2000) /* 2^13 bytes */
#define QLNXR_MAX_RQ_PBL_ENTRIES (0x10000 / sizeof(void *)) /* number */
#define QLNXR_RQE_ELEMENT_SIZE (sizeof(struct rdma_rq_sge)) /* bytes */
#define QLNXR_MAX_RQE_ELEMENTS_PER_RQE (RDMA_MAX_SGE_PER_RQ_WQE) /* number */
#define QLNXR_MAX_RQE_ELEMENTS_PER_PAGE ((RDMA_RING_PAGE_SIZE) / \
                QLNXR_RQE_ELEMENT_SIZE) /* number */
#define QLNXR_MAX_RQE ((QLNXR_MAX_RQ_PBL_ENTRIES) * (RDMA_RING_PAGE_SIZE) / \
                (QLNXR_RQE_ELEMENT_SIZE) / (QLNXR_MAX_RQE_ELEMENTS_PER_RQE))

/* CQE Limitation
 * Although FW supports two layer PBL we use single layer since it is more
 * than enough. For that layer we use a maximum size of 512 kB, again, because
 * it reaches the maximum number of page pointers. Notice is the '-1' in the
 * calculation that comes from having a u16 for the number of pages i.e. 0xffff
 * is the maximum number of pages (in single layer).
 */
#define QLNXR_CQE_SIZE   (sizeof(union rdma_cqe))
#define QLNXR_MAX_CQE_PBL_SIZE (512*1024) /* 512kB */
#define QLNXR_MAX_CQE_PBL_ENTRIES (((QLNXR_MAX_CQE_PBL_SIZE) / \
                                  sizeof(u64)) - 1) /* 64k -1 */
#define QLNXR_MAX_CQES ((u32)((QLNXR_MAX_CQE_PBL_ENTRIES) * (ECORE_CHAIN_PAGE_SIZE)\
                             / QLNXR_CQE_SIZE)) /* 8M -4096/32 = 8,388,480 */

/* CNQ size Limitation
 * The maximum CNQ size is not reachable because the FW supports a chain of u16
 * (specifically 64k-1). The FW can buffer CNQ elements avoiding an overflow, on
 * the expense of performance. Hence we set it to an arbitrarily smaller value
 * than the maximum.
 */
#define QLNXR_ROCE_MAX_CNQ_SIZE          (0x4000) /* 2^16 */

#define QLNXR_MAX_PORT                   (1)
#define QLNXR_PORT                       (1)

#define QLNXR_UVERBS(CMD_NAME) (1ull << IB_USER_VERBS_CMD_##CMD_NAME)

#define convert_to_64bit(lo, hi) ((u64)hi << 32 | (u64)lo)

/* The following number is used to determine if a handle recevied from the FW
 * actually point to a CQ/QP.
 */
#define QLNXR_CQ_MAGIC_NUMBER    (0x11223344)
#define QLNXR_QP_MAGIC_NUMBER    (0x77889900)

/* Fast path debug prints */
#define FP_DP_VERBOSE(...)
/* #define FP_DP_VERBOSE(...)   DP_VERBOSE(__VA_ARGS__) */

#define FW_PAGE_SIZE    (RDMA_RING_PAGE_SIZE)

#define QLNXR_MSG_INIT		0x10000,
#define QLNXR_MSG_FAIL		0x10000,
#define QLNXR_MSG_CQ		0x20000,
#define QLNXR_MSG_RQ		0x40000,
#define QLNXR_MSG_SQ		0x80000,
#define QLNXR_MSG_QP		(QLNXR_MSG_SQ | QLNXR_MSG_RQ),
#define QLNXR_MSG_MR		0x100000,
#define QLNXR_MSG_GSI		0x200000,
#define QLNXR_MSG_MISC		0x400000,
#define QLNXR_MSG_SRQ		0x800000,
#define QLNXR_MSG_IWARP		0x1000000,

#define QLNXR_ROCE_PKEY_MAX		1
#define QLNXR_ROCE_PKEY_TABLE_LEN	1
#define QLNXR_ROCE_PKEY_DEFAULT		0xffff

#define QLNXR_MAX_SGID			128 /* TBD - add more source gids... */

#define QLNXR_ENET_STATE_BIT     (0)

#define QLNXR_MAX_MSIX		(16)


struct qlnxr_cnq {
        struct qlnxr_dev	*dev;
        struct ecore_chain	pbl;
        struct ecore_sb_info	*sb;
        char			name[32];
        u64			n_comp;
        __le16			*hw_cons_ptr;
        u8			index;
	int			irq_rid;
	struct resource		*irq;
	void			*irq_handle;
};

struct qlnxr_device_attr {
        /* Vendor specific information */
        u32     vendor_id;
        u32     vendor_part_id;
        u32     hw_ver;
        u64     fw_ver;

        u64     node_guid;      /* node GUID */
        u64     sys_image_guid; /* System image GUID */

        u8      max_cnq;
        u8      max_sge;        /* Maximum # of scatter/gather entries
                                 * per Work Request supported
                                 */
        u16     max_inline;
        u32     max_sqe;        /* Maximum number of send outstanding send work
                                 * requests on any Work Queue supported
                                 */
        u32     max_rqe;        /* Maximum number of receive outstanding receive
                                 * work requests on any Work Queue supported
                                 */
        u8      max_qp_resp_rd_atomic_resc;     /* Maximum number of RDMA Reads
                                                 * & atomic operation that can
                                                 * be outstanding per QP
                                                 */

        u8      max_qp_req_rd_atomic_resc;      /* The maximum depth per QP for
                                                 * initiation of RDMA Read
                                                 * & atomic operations
                                                 */
        u64     max_dev_resp_rd_atomic_resc;
        u32     max_cq;
        u32     max_qp;
        u32     max_mr;         /* Maximum # of MRs supported */
        u64     max_mr_size;    /* Size (in bytes) of largest contiguous memory
                                 * block that can be registered by this device
                                 */
        u32     max_cqe;
        u32     max_mw;         /* Maximum # of memory windows supported */
        u32     max_fmr;
        u32     max_mr_mw_fmr_pbl;
        u64     max_mr_mw_fmr_size;
        u32     max_pd;         /* Maximum # of protection domains supported */
        u32     max_ah;
        u8      max_pkey;
        u32     max_srq;        /* Maximum number of SRQs */
        u32     max_srq_wr;     /* Maximum number of WRs per SRQ */
        u8      max_srq_sge;     /* Maximum number of SGE per WQE */
        u8      max_stats_queues; /* Maximum number of statistics queues */
        u32     dev_caps;

        /* Abilty to support RNR-NAK generation */

#define QLNXR_ROCE_DEV_CAP_RNR_NAK_MASK                           0x1
#define QLNXR_ROCE_DEV_CAP_RNR_NAK_SHIFT                  0
        /* Abilty to support shutdown port */
#define QLNXR_ROCE_DEV_CAP_SHUTDOWN_PORT_MASK                     0x1
#define QLNXR_ROCE_DEV_CAP_SHUTDOWN_PORT_SHIFT                    1
        /* Abilty to support port active event */
#define QLNXR_ROCE_DEV_CAP_PORT_ACTIVE_EVENT_MASK         0x1
#define QLNXR_ROCE_DEV_CAP_PORT_ACTIVE_EVENT_SHIFT                2
        /* Abilty to support port change event */
#define QLNXR_ROCE_DEV_CAP_PORT_CHANGE_EVENT_MASK         0x1
#define QLNXR_ROCE_DEV_CAP_PORT_CHANGE_EVENT_SHIFT                3
        /* Abilty to support system image GUID */
#define QLNXR_ROCE_DEV_CAP_SYS_IMAGE_MASK                 0x1
#define QLNXR_ROCE_DEV_CAP_SYS_IMAGE_SHIFT                        4
        /* Abilty to support bad P_Key counter support */
#define QLNXR_ROCE_DEV_CAP_BAD_PKEY_CNT_MASK                      0x1
#define QLNXR_ROCE_DEV_CAP_BAD_PKEY_CNT_SHIFT                     5
        /* Abilty to support atomic operations */
#define QLNXR_ROCE_DEV_CAP_ATOMIC_OP_MASK                 0x1
#define QLNXR_ROCE_DEV_CAP_ATOMIC_OP_SHIFT                        6
#define QLNXR_ROCE_DEV_CAP_RESIZE_CQ_MASK                 0x1
#define QLNXR_ROCE_DEV_CAP_RESIZE_CQ_SHIFT                        7
        /* Abilty to support modifying the maximum number of
         * outstanding work requests per QP
         */
#define QLNXR_ROCE_DEV_CAP_RESIZE_MAX_WR_MASK                     0x1
#define QLNXR_ROCE_DEV_CAP_RESIZE_MAX_WR_SHIFT                    8

                /* Abilty to support automatic path migration */
#define QLNXR_ROCE_DEV_CAP_AUTO_PATH_MIG_MASK                     0x1
#define QLNXR_ROCE_DEV_CAP_AUTO_PATH_MIG_SHIFT                    9
        /* Abilty to support the base memory management extensions */
#define QLNXR_ROCE_DEV_CAP_BASE_MEMORY_EXT_MASK                   0x1
#define QLNXR_ROCE_DEV_CAP_BASE_MEMORY_EXT_SHIFT          10
#define QLNXR_ROCE_DEV_CAP_BASE_QUEUE_EXT_MASK                    0x1
#define QLNXR_ROCE_DEV_CAP_BASE_QUEUE_EXT_SHIFT                   11
        /* Abilty to support multipile page sizes per memory region */
#define QLNXR_ROCE_DEV_CAP_MULTI_PAGE_PER_MR_EXT_MASK             0x1
#define QLNXR_ROCE_DEV_CAP_MULTI_PAGE_PER_MR_EXT_SHIFT            12
        /* Abilty to support block list physical buffer list */
#define QLNXR_ROCE_DEV_CAP_BLOCK_MODE_MASK                        0x1
#define QLNXR_ROCE_DEV_CAP_BLOCK_MODE_SHIFT                       13
        /* Abilty to support zero based virtual addresses */
#define QLNXR_ROCE_DEV_CAP_ZBVA_MASK                              0x1
#define QLNXR_ROCE_DEV_CAP_ZBVA_SHIFT                             14
        /* Abilty to support local invalidate fencing */
#define QLNXR_ROCE_DEV_CAP_LOCAL_INV_FENCE_MASK                   0x1
#define QLNXR_ROCE_DEV_CAP_LOCAL_INV_FENCE_SHIFT          15
        /* Abilty to support Loopback on QP */
#define QLNXR_ROCE_DEV_CAP_LB_INDICATOR_MASK                      0x1
#define QLNXR_ROCE_DEV_CAP_LB_INDICATOR_SHIFT                     16
        u64                     page_size_caps;
        u8                      dev_ack_delay;
        u32                     reserved_lkey;   /* Value of reserved L_key */
        u32                     bad_pkey_counter;/* Bad P_key counter support
                                                  * indicator
                                                  */
        struct ecore_rdma_events  events;
};

struct qlnxr_dev {
	struct ib_device	ibdev;
	qlnx_host_t		*ha;
	struct ecore_dev	*cdev;

	/* Added to extend Applications Support */
        struct pci_dev          *pdev;
	uint32_t		dp_module;
	uint8_t			dp_level;

	void			*rdma_ctx;

	struct mtx		idr_lock;
	struct idr		qpidr;

	uint32_t		wq_multiplier;
	int			num_cnq;

	struct ecore_sb_info	sb_array[QLNXR_MAX_MSIX];
	struct qlnxr_cnq	cnq_array[QLNXR_MAX_MSIX];

        int			sb_start;

        int			gsi_qp_created;
        struct qlnxr_cq		*gsi_sqcq;
        struct qlnxr_cq		*gsi_rqcq;
        struct qlnxr_qp		*gsi_qp;

        /* TBD: we'll need an array of these probablly per DPI... */
        void __iomem		*db_addr;
        uint64_t		db_phys_addr;
        uint32_t		db_size;
        uint16_t		dpi;

        uint64_t		guid;
        enum ib_atomic_cap	atomic_cap;

        union ib_gid		sgid_tbl[QLNXR_MAX_SGID];
        struct mtx		sgid_lock;
        struct notifier_block	nb_inet;
        struct notifier_block	nb_inet6;

        uint8_t			mr_key;
        struct list_head	entry;

        struct dentry		*dbgfs;

        uint8_t			gsi_ll2_mac_address[ETH_ALEN];
        uint8_t			gsi_ll2_handle;

	unsigned long		enet_state;

	struct workqueue_struct *iwarp_wq;

	volatile uint32_t	pd_count;
	struct                  qlnxr_device_attr attr;
        uint8_t                 user_dpm_enabled;
};

typedef struct qlnxr_dev qlnxr_dev_t;


struct qlnxr_pd {
        struct ib_pd ibpd;
        u32 pd_id;
        struct qlnxr_ucontext *uctx;
};

struct qlnxr_ucontext {
        struct ib_ucontext ibucontext;
        struct qlnxr_dev *dev;
        struct qlnxr_pd *pd;
        u64 dpi_addr;
        u64 dpi_phys_addr;
        u32 dpi_size;
        u16 dpi;

        struct list_head mm_head;
        struct mutex mm_list_lock;
};



struct qlnxr_dev_attr {
        struct ib_device_attr ib_attr;
};

struct qlnxr_dma_mem {
        void *va;
        dma_addr_t pa;
        u32 size;
};

struct qlnxr_pbl {
        struct list_head list_entry;
        void *va;
        dma_addr_t pa;
};

struct qlnxr_queue_info {
        void *va;
        dma_addr_t dma;
        u32 size;
        u16 len;
        u16 entry_size;         /* Size of an element in the queue */
        u16 id;                 /* qid, where to ring the doorbell. */
        u16 head, tail;
        bool created;
};

struct qlnxr_eq {
        struct qlnxr_queue_info q;
        u32 vector;
        int cq_cnt;
        struct qlnxr_dev *dev;
        char irq_name[32];
};

struct qlnxr_mq {
        struct qlnxr_queue_info sq;
        struct qlnxr_queue_info cq;
        bool rearm_cq;
};

struct phy_info {
        u16 auto_speeds_supported;
        u16 fixed_speeds_supported;
        u16 phy_type;
        u16 interface_type;
};

union db_prod64 {
	struct rdma_pwm_val32_data data;
        u64 raw;
};

enum qlnxr_cq_type {
        QLNXR_CQ_TYPE_GSI,
        QLNXR_CQ_TYPE_KERNEL,
        QLNXR_CQ_TYPE_USER
};

struct qlnxr_pbl_info {
        u32 num_pbls;
        u32 num_pbes;
        u32 pbl_size;
        u32 pbe_size;
        bool two_layered;
};

struct qlnxr_userq {
        struct ib_umem *umem;
        struct qlnxr_pbl_info pbl_info;
        struct qlnxr_pbl *pbl_tbl;
        u64 buf_addr;
        size_t buf_len;
};

struct qlnxr_cq {
        struct ib_cq		ibcq; /* must be first */

        enum qlnxr_cq_type	cq_type;
        uint32_t		sig;
        uint16_t		icid;

        /* relevant to cqs created from kernel space only (ULPs) */
        spinlock_t		cq_lock;
        uint8_t			arm_flags;
        struct ecore_chain	pbl;

        void __iomem		*db_addr; /* db address for cons update*/
        union db_prod64		db;

        uint8_t			pbl_toggle;
        union rdma_cqe		*latest_cqe;
        union rdma_cqe		*toggle_cqe;

        /* TODO: remove since it is redundant with 32 bit chains */
        uint32_t		cq_cons;

        /* relevant to cqs created from user space only (applications) */
        struct qlnxr_userq	q;

        /* destroy-IRQ handler race prevention */
        uint8_t			destroyed;
        uint16_t		cnq_notif;
};


struct qlnxr_ah {
        struct ib_ah		ibah;
        struct ib_ah_attr	attr;
};

union db_prod32 {
	struct rdma_pwm_val16_data data;
        u32 raw;
};

struct qlnxr_qp_hwq_info {
        /* WQE Elements*/
        struct ecore_chain      pbl;
        u64                     p_phys_addr_tbl;
        u32                     max_sges;

        /* WQE */
        u16                     prod;     /* WQE prod index for SW ring */
        u16                     cons;     /* WQE cons index for SW ring */
        u16                     wqe_cons;
        u16                     gsi_cons; /* filled in by GSI implementation */
        u16                     max_wr;

        /* DB */
        void __iomem            *db;      /* Doorbell address */
        union db_prod32         db_data;  /* Doorbell data */

        /* Required for iwarp_only */
        void __iomem            *iwarp_db2;      /* Doorbell address */
        union db_prod32         iwarp_db2_data;  /* Doorbell data */
};

#define QLNXR_INC_SW_IDX(p_info, index)                          \
        do {                                                    \
                p_info->index = (p_info->index + 1) &           \
                        ecore_chain_get_capacity(p_info->pbl)     \
        } while (0)

struct qlnxr_srq_hwq_info {
        u32 max_sges;
        u32 max_wr;
        struct ecore_chain pbl;
        u64 p_phys_addr_tbl;
        u32 wqe_prod;     /* WQE prod index in HW ring */
        u32 sge_prod;     /* SGE prod index in HW ring */
        u32 wr_prod_cnt; /* wr producer count */
        u32 wr_cons_cnt; /* wr consumer count */
        u32 num_elems;

        u32 *virt_prod_pair_addr; /* producer pair virtual address */
        dma_addr_t phy_prod_pair_addr; /* producer pair physical address */
};

struct qlnxr_srq {
        struct ib_srq ibsrq;
        struct qlnxr_dev *dev;
        /* relevant to cqs created from user space only (applications) */
        struct qlnxr_userq       usrq;
        struct qlnxr_srq_hwq_info hw_srq;
        struct ib_umem *prod_umem;
        u16 srq_id;
        /* lock to protect srq recv post */
        spinlock_t lock;
};

enum qlnxr_qp_err_bitmap {
        QLNXR_QP_ERR_SQ_FULL     = 1 << 0,
        QLNXR_QP_ERR_RQ_FULL     = 1 << 1,
        QLNXR_QP_ERR_BAD_SR      = 1 << 2,
        QLNXR_QP_ERR_BAD_RR      = 1 << 3,
        QLNXR_QP_ERR_SQ_PBL_FULL = 1 << 4,
        QLNXR_QP_ERR_RQ_PBL_FULL = 1 << 5,
};

struct mr_info {
        struct qlnxr_pbl *pbl_table;
        struct qlnxr_pbl_info pbl_info;
        struct list_head free_pbl_list;
        struct list_head inuse_pbl_list;
        u32 completed;
        u32 completed_handled;
};

#if __FreeBSD_version < 1102000
#define DEFINE_IB_FAST_REG
#else
#define DEFINE_ALLOC_MR
#endif

#ifdef DEFINE_IB_FAST_REG
struct qlnxr_fast_reg_page_list {
        struct ib_fast_reg_page_list ibfrpl;
        struct qlnxr_dev *dev;
        struct mr_info info;
};
#endif
struct qlnxr_qp {
        struct ib_qp ibqp;              /* must be first */
        struct qlnxr_dev *dev;
        struct qlnxr_iw_ep *ep;
        struct qlnxr_qp_hwq_info sq;
        struct qlnxr_qp_hwq_info rq;

        u32 max_inline_data;

#if __FreeBSD_version >= 1100000
        spinlock_t q_lock ____cacheline_aligned;
#else
	spinlock_t q_lock;
#endif

        struct qlnxr_cq *sq_cq;
        struct qlnxr_cq *rq_cq;
        struct qlnxr_srq *srq;
        enum ecore_roce_qp_state state;   /*  QP state */
        u32 id;
        struct qlnxr_pd *pd;
        enum ib_qp_type qp_type;
        struct ecore_rdma_qp *ecore_qp;
        u32 qp_id;
        u16 icid;
        u16 mtu;
        int sgid_idx;
        u32 rq_psn;
        u32 sq_psn;
        u32 qkey;
        u32 dest_qp_num;
        u32 sig;                /* unique siganture to identify valid QP */

        /* relevant to qps created from kernel space only (ULPs) */
        u8 prev_wqe_size;
        u16 wqe_cons;
        u32 err_bitmap;
        bool signaled;
        /* SQ shadow */
        struct {
                u64 wr_id;
                enum ib_wc_opcode opcode;
                u32 bytes_len;
                u8 wqe_size;
                bool  signaled;
                dma_addr_t icrc_mapping;
                u32 *icrc;
#ifdef DEFINE_IB_FAST_REG
                struct qlnxr_fast_reg_page_list *frmr;
#endif
                struct qlnxr_mr *mr;
        } *wqe_wr_id;

        /* RQ shadow */
        struct {
                u64 wr_id;
                struct ib_sge sg_list[RDMA_MAX_SGE_PER_RQ_WQE];
                uint8_t wqe_size;

                /* for GSI only */
                u8 smac[ETH_ALEN];
                u16 vlan_id;
                int rc;
        } *rqe_wr_id;

        /* relevant to qps created from user space only (applications) */
        struct qlnxr_userq usq;
        struct qlnxr_userq urq;
        atomic_t refcnt;
	bool destroyed;
};

enum qlnxr_mr_type {
        QLNXR_MR_USER,
        QLNXR_MR_KERNEL,
        QLNXR_MR_DMA,
        QLNXR_MR_FRMR
};


struct qlnxr_mr {
        struct ib_mr    ibmr;
        struct ib_umem  *umem;

        struct ecore_rdma_register_tid_in_params hw_mr;
        enum qlnxr_mr_type type;

        struct qlnxr_dev *dev;
        struct mr_info info;

        u64 *pages;
        u32 npages;

	u64 *iova_start; /* valid only for kernel_mr */
};


struct qlnxr_mm {
        struct {
                u64 phy_addr;
                unsigned long len;
        } key;
        struct list_head entry;
};

struct qlnxr_iw_listener {
        struct qlnxr_dev *dev;
        struct iw_cm_id *cm_id;
        int backlog;
        void *ecore_handle;
};

struct qlnxr_iw_ep {
        struct qlnxr_dev *dev;
        struct iw_cm_id *cm_id;
        struct qlnxr_qp *qp;
        void *ecore_context;
	u8 during_connect;
};

static inline void
qlnxr_inc_sw_cons(struct qlnxr_qp_hwq_info *info)
{
        info->cons = (info->cons + 1) % info->max_wr;
        info->wqe_cons++;
}

static inline void
qlnxr_inc_sw_prod(struct qlnxr_qp_hwq_info *info)
{
        info->prod = (info->prod + 1) % info->max_wr;
}

static inline struct qlnxr_dev *
get_qlnxr_dev(struct ib_device *ibdev)
{
        return container_of(ibdev, struct qlnxr_dev, ibdev);
}

static inline struct qlnxr_ucontext *
get_qlnxr_ucontext(struct ib_ucontext *ibucontext)
{
        return container_of(ibucontext, struct qlnxr_ucontext, ibucontext);
}

static inline struct qlnxr_pd *
get_qlnxr_pd(struct ib_pd *ibpd)
{
        return container_of(ibpd, struct qlnxr_pd, ibpd);
}

static inline struct qlnxr_cq *
get_qlnxr_cq(struct ib_cq *ibcq)
{
        return container_of(ibcq, struct qlnxr_cq, ibcq);
}

static inline struct qlnxr_qp *
get_qlnxr_qp(struct ib_qp *ibqp)
{
        return container_of(ibqp, struct qlnxr_qp, ibqp);
}

static inline struct qlnxr_mr *
get_qlnxr_mr(struct ib_mr *ibmr)
{
        return container_of(ibmr, struct qlnxr_mr, ibmr);
}

static inline struct qlnxr_ah *
get_qlnxr_ah(struct ib_ah *ibah)
{
        return container_of(ibah, struct qlnxr_ah, ibah);
}

static inline struct qlnxr_srq *
get_qlnxr_srq(struct ib_srq *ibsrq)
{
        return container_of(ibsrq, struct qlnxr_srq, ibsrq);
}

static inline bool qlnxr_qp_has_srq(struct qlnxr_qp *qp)
{
        return !!qp->srq;
}

static inline bool qlnxr_qp_has_sq(struct qlnxr_qp *qp)
{
        if (qp->qp_type == IB_QPT_GSI)
                return 0;

        return 1;
}

static inline bool qlnxr_qp_has_rq(struct qlnxr_qp *qp)
{
        if (qp->qp_type == IB_QPT_GSI || qlnxr_qp_has_srq(qp))
                return 0;

        return 1;
}


#ifdef DEFINE_IB_FAST_REG
static inline struct qlnxr_fast_reg_page_list *get_qlnxr_frmr_list(
        struct ib_fast_reg_page_list *ifrpl)
{
        return container_of(ifrpl, struct qlnxr_fast_reg_page_list, ibfrpl);
}
#endif

#define SET_FIELD2(value, name, flag)                          \
        do {                                                   \
                (value) |= ((flag) << (name ## _SHIFT));       \
        } while (0)

#define QLNXR_RESP_IMM	(RDMA_CQE_RESPONDER_IMM_FLG_MASK << \
                         RDMA_CQE_RESPONDER_IMM_FLG_SHIFT)
#define QLNXR_RESP_RDMA	(RDMA_CQE_RESPONDER_RDMA_FLG_MASK << \
                         RDMA_CQE_RESPONDER_RDMA_FLG_SHIFT)
#define QLNXR_RESP_INV  (RDMA_CQE_RESPONDER_INV_FLG_MASK << \
                         RDMA_CQE_RESPONDER_INV_FLG_SHIFT)

#define QLNXR_RESP_RDMA_IMM (QLNXR_RESP_IMM | QLNXR_RESP_RDMA)

static inline int
qlnxr_get_dmac(struct qlnxr_dev *dev, struct ib_ah_attr *ah_attr, u8 *mac_addr)
{
#ifdef DEFINE_NO_IP_BASED_GIDS
        u8 *guid = &ah_attr->grh.dgid.raw[8]; /* GID's 64 MSBs are the GUID */
#endif
        union ib_gid zero_sgid = { { 0 } };
        struct in6_addr in6;

        if (!memcmp(&ah_attr->grh.dgid, &zero_sgid, sizeof(union ib_gid))) {
                memset(mac_addr, 0x00, ETH_ALEN);
                return -EINVAL;
        }

        memcpy(&in6, ah_attr->grh.dgid.raw, sizeof(in6));

#ifdef DEFINE_NO_IP_BASED_GIDS
        /* get the MAC address from the GUID i.e. EUI-64 to MAC address */
        mac_addr[0] = guid[0] ^ 2; /* toggle the local/universal bit to local */
        mac_addr[1] = guid[1];
        mac_addr[2] = guid[2];
        mac_addr[3] = guid[5];
        mac_addr[4] = guid[6];
        mac_addr[5] = guid[7];
#else
        memcpy(mac_addr, ah_attr->dmac, ETH_ALEN);
#endif
        return 0;
}

extern int qlnx_rdma_ll2_set_mac_filter(void *rdma_ctx, uint8_t *old_mac_address,
                uint8_t *new_mac_address);


#define QLNXR_ROCE_PKEY_MAX 1
#define QLNXR_ROCE_PKEY_TABLE_LEN 1
#define QLNXR_ROCE_PKEY_DEFAULT 0xffff

#if __FreeBSD_version < 1100000
#define DEFINE_IB_AH_ATTR_WITH_DMAC     (0)
#define DEFINE_IB_UMEM_WITH_CHUNK	(1)
#else
#define DEFINE_IB_AH_ATTR_WITH_DMAC     (1)
#endif

#define QLNX_IS_IWARP(rdev)	IS_IWARP(ECORE_LEADING_HWFN(rdev->cdev))
#define QLNX_IS_ROCE(rdev)	IS_ROCE(ECORE_LEADING_HWFN(rdev->cdev))

#define MAX_RXMIT_CONNS		16

#endif /* #ifndef __QLNX_DEF_H_ */
