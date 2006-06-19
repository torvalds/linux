/*
 * SPU core / file system interface and HW structures
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _SPU_H
#define _SPU_H
#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/workqueue.h>
#include <linux/sysdev.h>

#define LS_SIZE (256 * 1024)
#define LS_ADDR_MASK (LS_SIZE - 1)

#define MFC_PUT_CMD             0x20
#define MFC_PUTS_CMD            0x28
#define MFC_PUTR_CMD            0x30
#define MFC_PUTF_CMD            0x22
#define MFC_PUTB_CMD            0x21
#define MFC_PUTFS_CMD           0x2A
#define MFC_PUTBS_CMD           0x29
#define MFC_PUTRF_CMD           0x32
#define MFC_PUTRB_CMD           0x31
#define MFC_PUTL_CMD            0x24
#define MFC_PUTRL_CMD           0x34
#define MFC_PUTLF_CMD           0x26
#define MFC_PUTLB_CMD           0x25
#define MFC_PUTRLF_CMD          0x36
#define MFC_PUTRLB_CMD          0x35

#define MFC_GET_CMD             0x40
#define MFC_GETS_CMD            0x48
#define MFC_GETF_CMD            0x42
#define MFC_GETB_CMD            0x41
#define MFC_GETFS_CMD           0x4A
#define MFC_GETBS_CMD           0x49
#define MFC_GETL_CMD            0x44
#define MFC_GETLF_CMD           0x46
#define MFC_GETLB_CMD           0x45

#define MFC_SDCRT_CMD           0x80
#define MFC_SDCRTST_CMD         0x81
#define MFC_SDCRZ_CMD           0x89
#define MFC_SDCRS_CMD           0x8D
#define MFC_SDCRF_CMD           0x8F

#define MFC_GETLLAR_CMD         0xD0
#define MFC_PUTLLC_CMD          0xB4
#define MFC_PUTLLUC_CMD         0xB0
#define MFC_PUTQLLUC_CMD        0xB8
#define MFC_SNDSIG_CMD          0xA0
#define MFC_SNDSIGB_CMD         0xA1
#define MFC_SNDSIGF_CMD         0xA2
#define MFC_BARRIER_CMD         0xC0
#define MFC_EIEIO_CMD           0xC8
#define MFC_SYNC_CMD            0xCC

#define MFC_MIN_DMA_SIZE_SHIFT  4       /* 16 bytes */
#define MFC_MAX_DMA_SIZE_SHIFT  14      /* 16384 bytes */
#define MFC_MIN_DMA_SIZE        (1 << MFC_MIN_DMA_SIZE_SHIFT)
#define MFC_MAX_DMA_SIZE        (1 << MFC_MAX_DMA_SIZE_SHIFT)
#define MFC_MIN_DMA_SIZE_MASK   (MFC_MIN_DMA_SIZE - 1)
#define MFC_MAX_DMA_SIZE_MASK   (MFC_MAX_DMA_SIZE - 1)
#define MFC_MIN_DMA_LIST_SIZE   0x0008  /*   8 bytes */
#define MFC_MAX_DMA_LIST_SIZE   0x4000  /* 16K bytes */

#define MFC_TAGID_TO_TAGMASK(tag_id)  (1 << (tag_id & 0x1F))

/* Events for Channels 0-2 */
#define MFC_DMA_TAG_STATUS_UPDATE_EVENT     0x00000001
#define MFC_DMA_TAG_CMD_STALL_NOTIFY_EVENT  0x00000002
#define MFC_DMA_QUEUE_AVAILABLE_EVENT       0x00000008
#define MFC_SPU_MAILBOX_WRITTEN_EVENT       0x00000010
#define MFC_DECREMENTER_EVENT               0x00000020
#define MFC_PU_INT_MAILBOX_AVAILABLE_EVENT  0x00000040
#define MFC_PU_MAILBOX_AVAILABLE_EVENT      0x00000080
#define MFC_SIGNAL_2_EVENT                  0x00000100
#define MFC_SIGNAL_1_EVENT                  0x00000200
#define MFC_LLR_LOST_EVENT                  0x00000400
#define MFC_PRIV_ATTN_EVENT                 0x00000800
#define MFC_MULTI_SRC_EVENT                 0x00001000

/* Flags indicating progress during context switch. */
#define SPU_CONTEXT_SWITCH_PENDING	0UL
#define SPU_CONTEXT_SWITCH_ACTIVE	1UL

struct spu_context;
struct spu_runqueue;

struct spu {
	char *name;
	unsigned long local_store_phys;
	u8 *local_store;
	unsigned long problem_phys;
	struct spu_problem __iomem *problem;
	struct spu_priv1 __iomem *priv1;
	struct spu_priv2 __iomem *priv2;
	struct list_head list;
	struct list_head sched_list;
	int number;
	int nid;
	u32 isrc;
	u32 node;
	u64 flags;
	u64 dar;
	u64 dsisr;
	size_t ls_size;
	unsigned int slb_replace;
	struct mm_struct *mm;
	struct spu_context *ctx;
	struct spu_runqueue *rq;
	unsigned long long timestamp;
	pid_t pid;
	int prio;
	int class_0_pending;
	spinlock_t register_lock;

	u32 stop_code;
	void (* wbox_callback)(struct spu *spu);
	void (* ibox_callback)(struct spu *spu);
	void (* stop_callback)(struct spu *spu);
	void (* mfc_callback)(struct spu *spu);

	char irq_c0[8];
	char irq_c1[8];
	char irq_c2[8];

	struct sys_device sysdev;
};

struct spu *spu_alloc(void);
void spu_free(struct spu *spu);
int spu_irq_class_0_bottom(struct spu *spu);
int spu_irq_class_1_bottom(struct spu *spu);
void spu_irq_setaffinity(struct spu *spu, int cpu);

/* system callbacks from the SPU */
struct spu_syscall_block {
	u64 nr_ret;
	u64 parm[6];
};
extern long spu_sys_callback(struct spu_syscall_block *s);

/* syscalls implemented in spufs */
extern struct spufs_calls {
	asmlinkage long (*create_thread)(const char __user *name,
					unsigned int flags, mode_t mode);
	asmlinkage long (*spu_run)(struct file *filp, __u32 __user *unpc,
						__u32 __user *ustatus);
	struct module *owner;
} spufs_calls;

#ifdef CONFIG_SPU_FS_MODULE
int register_spu_syscalls(struct spufs_calls *calls);
void unregister_spu_syscalls(struct spufs_calls *calls);
#else
static inline int register_spu_syscalls(struct spufs_calls *calls)
{
	return 0;
}
static inline void unregister_spu_syscalls(struct spufs_calls *calls)
{
}
#endif /* MODULE */


/*
 * This defines the Local Store, Problem Area and Privlege Area of an SPU.
 */

union mfc_tag_size_class_cmd {
	struct {
		u16 mfc_size;
		u16 mfc_tag;
		u8  pad;
		u8  mfc_rclassid;
		u16 mfc_cmd;
	} u;
	struct {
		u32 mfc_size_tag32;
		u32 mfc_class_cmd32;
	} by32;
	u64 all64;
};

struct mfc_cq_sr {
	u64 mfc_cq_data0_RW;
	u64 mfc_cq_data1_RW;
	u64 mfc_cq_data2_RW;
	u64 mfc_cq_data3_RW;
};

struct spu_problem {
#define MS_SYNC_PENDING         1L
	u64 spc_mssync_RW;					/* 0x0000 */
	u8  pad_0x0008_0x3000[0x3000 - 0x0008];

	/* DMA Area */
	u8  pad_0x3000_0x3004[0x4];				/* 0x3000 */
	u32 mfc_lsa_W;						/* 0x3004 */
	u64 mfc_ea_W;						/* 0x3008 */
	union mfc_tag_size_class_cmd mfc_union_W;			/* 0x3010 */
	u8  pad_0x3018_0x3104[0xec];				/* 0x3018 */
	u32 dma_qstatus_R;					/* 0x3104 */
	u8  pad_0x3108_0x3204[0xfc];				/* 0x3108 */
	u32 dma_querytype_RW;					/* 0x3204 */
	u8  pad_0x3208_0x321c[0x14];				/* 0x3208 */
	u32 dma_querymask_RW;					/* 0x321c */
	u8  pad_0x3220_0x322c[0xc];				/* 0x3220 */
	u32 dma_tagstatus_R;					/* 0x322c */
#define DMA_TAGSTATUS_INTR_ANY	1u
#define DMA_TAGSTATUS_INTR_ALL	2u
	u8  pad_0x3230_0x4000[0x4000 - 0x3230]; 		/* 0x3230 */

	/* SPU Control Area */
	u8  pad_0x4000_0x4004[0x4];				/* 0x4000 */
	u32 pu_mb_R;						/* 0x4004 */
	u8  pad_0x4008_0x400c[0x4];				/* 0x4008 */
	u32 spu_mb_W;						/* 0x400c */
	u8  pad_0x4010_0x4014[0x4];				/* 0x4010 */
	u32 mb_stat_R;						/* 0x4014 */
	u8  pad_0x4018_0x401c[0x4];				/* 0x4018 */
	u32 spu_runcntl_RW;					/* 0x401c */
#define SPU_RUNCNTL_STOP	0L
#define SPU_RUNCNTL_RUNNABLE	1L
	u8  pad_0x4020_0x4024[0x4];				/* 0x4020 */
	u32 spu_status_R;					/* 0x4024 */
#define SPU_STOP_STATUS_SHIFT           16
#define SPU_STATUS_STOPPED		0x0
#define SPU_STATUS_RUNNING		0x1
#define SPU_STATUS_STOPPED_BY_STOP	0x2
#define SPU_STATUS_STOPPED_BY_HALT	0x4
#define SPU_STATUS_WAITING_FOR_CHANNEL	0x8
#define SPU_STATUS_SINGLE_STEP		0x10
#define SPU_STATUS_INVALID_INSTR        0x20
#define SPU_STATUS_INVALID_CH           0x40
#define SPU_STATUS_ISOLATED_STATE       0x80
#define SPU_STATUS_ISOLATED_LOAD_STAUTUS 0x200
#define SPU_STATUS_ISOLATED_EXIT_STAUTUS 0x400
	u8  pad_0x4028_0x402c[0x4];				/* 0x4028 */
	u32 spu_spe_R;						/* 0x402c */
	u8  pad_0x4030_0x4034[0x4];				/* 0x4030 */
	u32 spu_npc_RW;						/* 0x4034 */
	u8  pad_0x4038_0x14000[0x14000 - 0x4038];		/* 0x4038 */

	/* Signal Notification Area */
	u8  pad_0x14000_0x1400c[0xc];				/* 0x14000 */
	u32 signal_notify1;					/* 0x1400c */
	u8  pad_0x14010_0x1c00c[0x7ffc];			/* 0x14010 */
	u32 signal_notify2;					/* 0x1c00c */
} __attribute__ ((aligned(0x20000)));

/* SPU Privilege 2 State Area */
struct spu_priv2 {
	/* MFC Registers */
	u8  pad_0x0000_0x1100[0x1100 - 0x0000]; 		/* 0x0000 */

	/* SLB Management Registers */
	u8  pad_0x1100_0x1108[0x8];				/* 0x1100 */
	u64 slb_index_W;					/* 0x1108 */
#define SLB_INDEX_MASK				0x7L
	u64 slb_esid_RW;					/* 0x1110 */
	u64 slb_vsid_RW;					/* 0x1118 */
#define SLB_VSID_SUPERVISOR_STATE	(0x1ull << 11)
#define SLB_VSID_SUPERVISOR_STATE_MASK	(0x1ull << 11)
#define SLB_VSID_PROBLEM_STATE		(0x1ull << 10)
#define SLB_VSID_PROBLEM_STATE_MASK	(0x1ull << 10)
#define SLB_VSID_EXECUTE_SEGMENT	(0x1ull << 9)
#define SLB_VSID_NO_EXECUTE_SEGMENT	(0x1ull << 9)
#define SLB_VSID_EXECUTE_SEGMENT_MASK	(0x1ull << 9)
#define SLB_VSID_4K_PAGE		(0x0 << 8)
#define SLB_VSID_LARGE_PAGE		(0x1ull << 8)
#define SLB_VSID_PAGE_SIZE_MASK		(0x1ull << 8)
#define SLB_VSID_CLASS_MASK		(0x1ull << 7)
#define SLB_VSID_VIRTUAL_PAGE_SIZE_MASK	(0x1ull << 6)
	u64 slb_invalidate_entry_W;				/* 0x1120 */
	u64 slb_invalidate_all_W;				/* 0x1128 */
	u8  pad_0x1130_0x2000[0x2000 - 0x1130]; 		/* 0x1130 */

	/* Context Save / Restore Area */
	struct mfc_cq_sr spuq[16];				/* 0x2000 */
	struct mfc_cq_sr puq[8];				/* 0x2200 */
	u8  pad_0x2300_0x3000[0x3000 - 0x2300]; 		/* 0x2300 */

	/* MFC Control */
	u64 mfc_control_RW;					/* 0x3000 */
#define MFC_CNTL_RESUME_DMA_QUEUE		(0ull << 0)
#define MFC_CNTL_SUSPEND_DMA_QUEUE		(1ull << 0)
#define MFC_CNTL_SUSPEND_DMA_QUEUE_MASK		(1ull << 0)
#define MFC_CNTL_NORMAL_DMA_QUEUE_OPERATION	(0ull << 8)
#define MFC_CNTL_SUSPEND_IN_PROGRESS		(1ull << 8)
#define MFC_CNTL_SUSPEND_COMPLETE		(3ull << 8)
#define MFC_CNTL_SUSPEND_DMA_STATUS_MASK	(3ull << 8)
#define MFC_CNTL_DMA_QUEUES_EMPTY		(1ull << 14)
#define MFC_CNTL_DMA_QUEUES_EMPTY_MASK		(1ull << 14)
#define MFC_CNTL_PURGE_DMA_REQUEST		(1ull << 15)
#define MFC_CNTL_PURGE_DMA_IN_PROGRESS		(1ull << 24)
#define MFC_CNTL_PURGE_DMA_COMPLETE		(3ull << 24)
#define MFC_CNTL_PURGE_DMA_STATUS_MASK		(3ull << 24)
#define MFC_CNTL_RESTART_DMA_COMMAND		(1ull << 32)
#define MFC_CNTL_DMA_COMMAND_REISSUE_PENDING	(1ull << 32)
#define MFC_CNTL_DMA_COMMAND_REISSUE_STATUS_MASK (1ull << 32)
#define MFC_CNTL_MFC_PRIVILEGE_STATE		(2ull << 33)
#define MFC_CNTL_MFC_PROBLEM_STATE		(3ull << 33)
#define MFC_CNTL_MFC_KEY_PROTECTION_STATE_MASK	(3ull << 33)
#define MFC_CNTL_DECREMENTER_HALTED		(1ull << 35)
#define MFC_CNTL_DECREMENTER_RUNNING		(1ull << 40)
#define MFC_CNTL_DECREMENTER_STATUS_MASK	(1ull << 40)
	u8  pad_0x3008_0x4000[0x4000 - 0x3008]; 		/* 0x3008 */

	/* Interrupt Mailbox */
	u64 puint_mb_R;						/* 0x4000 */
	u8  pad_0x4008_0x4040[0x4040 - 0x4008]; 		/* 0x4008 */

	/* SPU Control */
	u64 spu_privcntl_RW;					/* 0x4040 */
#define SPU_PRIVCNTL_MODE_NORMAL		(0x0ull << 0)
#define SPU_PRIVCNTL_MODE_SINGLE_STEP		(0x1ull << 0)
#define SPU_PRIVCNTL_MODE_MASK			(0x1ull << 0)
#define SPU_PRIVCNTL_NO_ATTENTION_EVENT		(0x0ull << 1)
#define SPU_PRIVCNTL_ATTENTION_EVENT		(0x1ull << 1)
#define SPU_PRIVCNTL_ATTENTION_EVENT_MASK	(0x1ull << 1)
#define SPU_PRIVCNT_LOAD_REQUEST_NORMAL		(0x0ull << 2)
#define SPU_PRIVCNT_LOAD_REQUEST_ENABLE_MASK	(0x1ull << 2)
	u8  pad_0x4048_0x4058[0x10];				/* 0x4048 */
	u64 spu_lslr_RW;					/* 0x4058 */
	u64 spu_chnlcntptr_RW;					/* 0x4060 */
	u64 spu_chnlcnt_RW;					/* 0x4068 */
	u64 spu_chnldata_RW;					/* 0x4070 */
	u64 spu_cfg_RW;						/* 0x4078 */
	u8  pad_0x4080_0x5000[0x5000 - 0x4080]; 		/* 0x4080 */

	/* PV2_ImplRegs: Implementation-specific privileged-state 2 regs */
	u64 spu_pm_trace_tag_status_RW;				/* 0x5000 */
	u64 spu_tag_status_query_RW;				/* 0x5008 */
#define TAG_STATUS_QUERY_CONDITION_BITS (0x3ull << 32)
#define TAG_STATUS_QUERY_MASK_BITS (0xffffffffull)
	u64 spu_cmd_buf1_RW;					/* 0x5010 */
#define SPU_COMMAND_BUFFER_1_LSA_BITS (0x7ffffull << 32)
#define SPU_COMMAND_BUFFER_1_EAH_BITS (0xffffffffull)
	u64 spu_cmd_buf2_RW;					/* 0x5018 */
#define SPU_COMMAND_BUFFER_2_EAL_BITS ((0xffffffffull) << 32)
#define SPU_COMMAND_BUFFER_2_TS_BITS (0xffffull << 16)
#define SPU_COMMAND_BUFFER_2_TAG_BITS (0x3full)
	u64 spu_atomic_status_RW;				/* 0x5020 */
} __attribute__ ((aligned(0x20000)));

/* SPU Privilege 1 State Area */
struct spu_priv1 {
	/* Control and Configuration Area */
	u64 mfc_sr1_RW;						/* 0x000 */
#define MFC_STATE1_LOCAL_STORAGE_DECODE_MASK	0x01ull
#define MFC_STATE1_BUS_TLBIE_MASK		0x02ull
#define MFC_STATE1_REAL_MODE_OFFSET_ENABLE_MASK	0x04ull
#define MFC_STATE1_PROBLEM_STATE_MASK		0x08ull
#define MFC_STATE1_RELOCATE_MASK		0x10ull
#define MFC_STATE1_MASTER_RUN_CONTROL_MASK	0x20ull
	u64 mfc_lpid_RW;					/* 0x008 */
	u64 spu_idr_RW;						/* 0x010 */
	u64 mfc_vr_RO;						/* 0x018 */
#define MFC_VERSION_BITS		(0xffff << 16)
#define MFC_REVISION_BITS		(0xffff)
#define MFC_GET_VERSION_BITS(vr)	(((vr) & MFC_VERSION_BITS) >> 16)
#define MFC_GET_REVISION_BITS(vr)	((vr) & MFC_REVISION_BITS)
	u64 spu_vr_RO;						/* 0x020 */
#define SPU_VERSION_BITS		(0xffff << 16)
#define SPU_REVISION_BITS		(0xffff)
#define SPU_GET_VERSION_BITS(vr)	(vr & SPU_VERSION_BITS) >> 16
#define SPU_GET_REVISION_BITS(vr)	(vr & SPU_REVISION_BITS)
	u8  pad_0x28_0x100[0x100 - 0x28];			/* 0x28 */

	/* Interrupt Area */
	u64 int_mask_RW[3];					/* 0x100 */
#define CLASS0_ENABLE_DMA_ALIGNMENT_INTR		0x1L
#define CLASS0_ENABLE_INVALID_DMA_COMMAND_INTR		0x2L
#define CLASS0_ENABLE_SPU_ERROR_INTR			0x4L
#define CLASS0_ENABLE_MFC_FIR_INTR			0x8L
#define CLASS1_ENABLE_SEGMENT_FAULT_INTR		0x1L
#define CLASS1_ENABLE_STORAGE_FAULT_INTR		0x2L
#define CLASS1_ENABLE_LS_COMPARE_SUSPEND_ON_GET_INTR	0x4L
#define CLASS1_ENABLE_LS_COMPARE_SUSPEND_ON_PUT_INTR	0x8L
#define CLASS2_ENABLE_MAILBOX_INTR			0x1L
#define CLASS2_ENABLE_SPU_STOP_INTR			0x2L
#define CLASS2_ENABLE_SPU_HALT_INTR			0x4L
#define CLASS2_ENABLE_SPU_DMA_TAG_GROUP_COMPLETE_INTR	0x8L
	u8  pad_0x118_0x140[0x28];				/* 0x118 */
	u64 int_stat_RW[3];					/* 0x140 */
	u8  pad_0x158_0x180[0x28];				/* 0x158 */
	u64 int_route_RW;					/* 0x180 */

	/* Interrupt Routing */
	u8  pad_0x188_0x200[0x200 - 0x188];			/* 0x188 */

	/* Atomic Unit Control Area */
	u64 mfc_atomic_flush_RW;				/* 0x200 */
#define mfc_atomic_flush_enable			0x1L
	u8  pad_0x208_0x280[0x78];				/* 0x208 */
	u64 resource_allocation_groupID_RW;			/* 0x280 */
	u64 resource_allocation_enable_RW; 			/* 0x288 */
	u8  pad_0x290_0x3c8[0x3c8 - 0x290];			/* 0x290 */

	/* SPU_Cache_ImplRegs: Implementation-dependent cache registers */

	u64 smf_sbi_signal_sel;					/* 0x3c8 */
#define smf_sbi_mask_lsb	56
#define smf_sbi_shift		(63 - smf_sbi_mask_lsb)
#define smf_sbi_mask		(0x301LL << smf_sbi_shift)
#define smf_sbi_bus0_bits	(0x001LL << smf_sbi_shift)
#define smf_sbi_bus2_bits	(0x100LL << smf_sbi_shift)
#define smf_sbi2_bus0_bits	(0x201LL << smf_sbi_shift)
#define smf_sbi2_bus2_bits	(0x300LL << smf_sbi_shift)
	u64 smf_ato_signal_sel;					/* 0x3d0 */
#define smf_ato_mask_lsb	35
#define smf_ato_shift		(63 - smf_ato_mask_lsb)
#define smf_ato_mask		(0x3LL << smf_ato_shift)
#define smf_ato_bus0_bits	(0x2LL << smf_ato_shift)
#define smf_ato_bus2_bits	(0x1LL << smf_ato_shift)
	u8  pad_0x3d8_0x400[0x400 - 0x3d8];			/* 0x3d8 */

	/* TLB Management Registers */
	u64 mfc_sdr_RW;						/* 0x400 */
	u8  pad_0x408_0x500[0xf8];				/* 0x408 */
	u64 tlb_index_hint_RO;					/* 0x500 */
	u64 tlb_index_W;					/* 0x508 */
	u64 tlb_vpn_RW;						/* 0x510 */
	u64 tlb_rpn_RW;						/* 0x518 */
	u8  pad_0x520_0x540[0x20];				/* 0x520 */
	u64 tlb_invalidate_entry_W;				/* 0x540 */
	u64 tlb_invalidate_all_W;				/* 0x548 */
	u8  pad_0x550_0x580[0x580 - 0x550];			/* 0x550 */

	/* SPU_MMU_ImplRegs: Implementation-dependent MMU registers */
	u64 smm_hid;						/* 0x580 */
#define PAGE_SIZE_MASK		0xf000000000000000ull
#define PAGE_SIZE_16MB_64KB	0x2000000000000000ull
	u8  pad_0x588_0x600[0x600 - 0x588];			/* 0x588 */

	/* MFC Status/Control Area */
	u64 mfc_accr_RW;					/* 0x600 */
#define MFC_ACCR_EA_ACCESS_GET		(1 << 0)
#define MFC_ACCR_EA_ACCESS_PUT		(1 << 1)
#define MFC_ACCR_LS_ACCESS_GET		(1 << 3)
#define MFC_ACCR_LS_ACCESS_PUT		(1 << 4)
	u8  pad_0x608_0x610[0x8];				/* 0x608 */
	u64 mfc_dsisr_RW;					/* 0x610 */
#define MFC_DSISR_PTE_NOT_FOUND		(1 << 30)
#define MFC_DSISR_ACCESS_DENIED		(1 << 27)
#define MFC_DSISR_ATOMIC		(1 << 26)
#define MFC_DSISR_ACCESS_PUT		(1 << 25)
#define MFC_DSISR_ADDR_MATCH		(1 << 22)
#define MFC_DSISR_LS			(1 << 17)
#define MFC_DSISR_L			(1 << 16)
#define MFC_DSISR_ADDRESS_OVERFLOW	(1 << 0)
	u8  pad_0x618_0x620[0x8];				/* 0x618 */
	u64 mfc_dar_RW;						/* 0x620 */
	u8  pad_0x628_0x700[0x700 - 0x628];			/* 0x628 */

	/* Replacement Management Table (RMT) Area */
	u64 rmt_index_RW;					/* 0x700 */
	u8  pad_0x708_0x710[0x8];				/* 0x708 */
	u64 rmt_data1_RW;					/* 0x710 */
	u8  pad_0x718_0x800[0x800 - 0x718];			/* 0x718 */

	/* Control/Configuration Registers */
	u64 mfc_dsir_R;						/* 0x800 */
#define MFC_DSIR_Q			(1 << 31)
#define MFC_DSIR_SPU_QUEUE		MFC_DSIR_Q
	u64 mfc_lsacr_RW;					/* 0x808 */
#define MFC_LSACR_COMPARE_MASK		((~0ull) << 32)
#define MFC_LSACR_COMPARE_ADDR		((~0ull) >> 32)
	u64 mfc_lscrr_R;					/* 0x810 */
#define MFC_LSCRR_Q			(1 << 31)
#define MFC_LSCRR_SPU_QUEUE		MFC_LSCRR_Q
#define MFC_LSCRR_QI_SHIFT		32
#define MFC_LSCRR_QI_MASK		((~0ull) << MFC_LSCRR_QI_SHIFT)
	u8  pad_0x818_0x820[0x8];				/* 0x818 */
	u64 mfc_tclass_id_RW;					/* 0x820 */
#define MFC_TCLASS_ID_ENABLE		(1L << 0L)
#define MFC_TCLASS_SLOT2_ENABLE		(1L << 5L)
#define MFC_TCLASS_SLOT1_ENABLE		(1L << 6L)
#define MFC_TCLASS_SLOT0_ENABLE		(1L << 7L)
#define MFC_TCLASS_QUOTA_2_SHIFT	8L
#define MFC_TCLASS_QUOTA_1_SHIFT	16L
#define MFC_TCLASS_QUOTA_0_SHIFT	24L
#define MFC_TCLASS_QUOTA_2_MASK		(0x1FL << MFC_TCLASS_QUOTA_2_SHIFT)
#define MFC_TCLASS_QUOTA_1_MASK		(0x1FL << MFC_TCLASS_QUOTA_1_SHIFT)
#define MFC_TCLASS_QUOTA_0_MASK		(0x1FL << MFC_TCLASS_QUOTA_0_SHIFT)
	u8  pad_0x828_0x900[0x900 - 0x828];			/* 0x828 */

	/* Real Mode Support Registers */
	u64 mfc_rm_boundary;					/* 0x900 */
	u8  pad_0x908_0x938[0x30];				/* 0x908 */
	u64 smf_dma_signal_sel;					/* 0x938 */
#define mfc_dma1_mask_lsb	41
#define mfc_dma1_shift		(63 - mfc_dma1_mask_lsb)
#define mfc_dma1_mask		(0x3LL << mfc_dma1_shift)
#define mfc_dma1_bits		(0x1LL << mfc_dma1_shift)
#define mfc_dma2_mask_lsb	43
#define mfc_dma2_shift		(63 - mfc_dma2_mask_lsb)
#define mfc_dma2_mask		(0x3LL << mfc_dma2_shift)
#define mfc_dma2_bits		(0x1LL << mfc_dma2_shift)
	u8  pad_0x940_0xa38[0xf8];				/* 0x940 */
	u64 smm_signal_sel;					/* 0xa38 */
#define smm_sig_mask_lsb	12
#define smm_sig_shift		(63 - smm_sig_mask_lsb)
#define smm_sig_mask		(0x3LL << smm_sig_shift)
#define smm_sig_bus0_bits	(0x2LL << smm_sig_shift)
#define smm_sig_bus2_bits	(0x1LL << smm_sig_shift)
	u8  pad_0xa40_0xc00[0xc00 - 0xa40];			/* 0xa40 */

	/* DMA Command Error Area */
	u64 mfc_cer_R;						/* 0xc00 */
#define MFC_CER_Q		(1 << 31)
#define MFC_CER_SPU_QUEUE	MFC_CER_Q
	u8  pad_0xc08_0x1000[0x1000 - 0xc08];			/* 0xc08 */

	/* PV1_ImplRegs: Implementation-dependent privileged-state 1 regs */
	/* DMA Command Error Area */
	u64 spu_ecc_cntl_RW;					/* 0x1000 */
#define SPU_ECC_CNTL_E			(1ull << 0ull)
#define SPU_ECC_CNTL_ENABLE		SPU_ECC_CNTL_E
#define SPU_ECC_CNTL_DISABLE		(~SPU_ECC_CNTL_E & 1L)
#define SPU_ECC_CNTL_S			(1ull << 1ull)
#define SPU_ECC_STOP_AFTER_ERROR	SPU_ECC_CNTL_S
#define SPU_ECC_CONTINUE_AFTER_ERROR	(~SPU_ECC_CNTL_S & 2L)
#define SPU_ECC_CNTL_B			(1ull << 2ull)
#define SPU_ECC_BACKGROUND_ENABLE	SPU_ECC_CNTL_B
#define SPU_ECC_BACKGROUND_DISABLE	(~SPU_ECC_CNTL_B & 4L)
#define SPU_ECC_CNTL_I_SHIFT		3ull
#define SPU_ECC_CNTL_I_MASK		(3ull << SPU_ECC_CNTL_I_SHIFT)
#define SPU_ECC_WRITE_ALWAYS		(~SPU_ECC_CNTL_I & 12L)
#define SPU_ECC_WRITE_CORRECTABLE	(1ull << SPU_ECC_CNTL_I_SHIFT)
#define SPU_ECC_WRITE_UNCORRECTABLE	(3ull << SPU_ECC_CNTL_I_SHIFT)
#define SPU_ECC_CNTL_D			(1ull << 5ull)
#define SPU_ECC_DETECTION_ENABLE	SPU_ECC_CNTL_D
#define SPU_ECC_DETECTION_DISABLE	(~SPU_ECC_CNTL_D & 32L)
	u64 spu_ecc_stat_RW;					/* 0x1008 */
#define SPU_ECC_CORRECTED_ERROR		(1ull << 0ul)
#define SPU_ECC_UNCORRECTED_ERROR	(1ull << 1ul)
#define SPU_ECC_SCRUB_COMPLETE		(1ull << 2ul)
#define SPU_ECC_SCRUB_IN_PROGRESS	(1ull << 3ul)
#define SPU_ECC_INSTRUCTION_ERROR	(1ull << 4ul)
#define SPU_ECC_DATA_ERROR		(1ull << 5ul)
#define SPU_ECC_DMA_ERROR		(1ull << 6ul)
#define SPU_ECC_STATUS_CNT_MASK		(256ull << 8)
	u64 spu_ecc_addr_RW;					/* 0x1010 */
	u64 spu_err_mask_RW;					/* 0x1018 */
#define SPU_ERR_ILLEGAL_INSTR		(1ull << 0ul)
#define SPU_ERR_ILLEGAL_CHANNEL		(1ull << 1ul)
	u8  pad_0x1020_0x1028[0x1028 - 0x1020];			/* 0x1020 */

	/* SPU Debug-Trace Bus (DTB) Selection Registers */
	u64 spu_trig0_sel;					/* 0x1028 */
	u64 spu_trig1_sel;					/* 0x1030 */
	u64 spu_trig2_sel;					/* 0x1038 */
	u64 spu_trig3_sel;					/* 0x1040 */
	u64 spu_trace_sel;					/* 0x1048 */
#define spu_trace_sel_mask		0x1f1fLL
#define spu_trace_sel_bus0_bits		0x1000LL
#define spu_trace_sel_bus2_bits		0x0010LL
	u64 spu_event0_sel;					/* 0x1050 */
	u64 spu_event1_sel;					/* 0x1058 */
	u64 spu_event2_sel;					/* 0x1060 */
	u64 spu_event3_sel;					/* 0x1068 */
	u64 spu_trace_cntl;					/* 0x1070 */
} __attribute__ ((aligned(0x2000)));

#endif /* __KERNEL__ */
#endif
