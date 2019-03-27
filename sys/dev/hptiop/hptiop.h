/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * HighPoint RR3xxx/4xxx RAID Driver for FreeBSD
 * Copyright (C) 2007-2012 HighPoint Technologies, Inc. All Rights Reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _HPTIOP_H
#define _HPTIOP_H

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define DBG 0

#ifdef DBG
int hpt_iop_dbg_level = 0;
#define KdPrint(x)  do { if (hpt_iop_dbg_level) printf x; } while (0)
#define HPT_ASSERT(x) assert(x)
#else
#define KdPrint(x)
#define HPT_ASSERT(x)
#endif

#define HPT_SRB_MAX_REQ_SIZE                600
#define HPT_SRB_MAX_QUEUE_SIZE              0x100

/* beyond 64G mem */
#define HPT_SRB_FLAG_HIGH_MEM_ACESS         0x1
#define HPT_SRB_MAX_SIZE  ((sizeof(struct hpt_iop_srb) + 0x1f) & ~0x1f)
#ifndef offsetof
#define offsetof(TYPE, MEM) ((size_t)&((TYPE*)0)->MEM)
#endif

#ifndef MIN
#define MIN(a, b)		((a) < (b) ? (a) : (b))
#endif

#define HPT_IOCTL_MAGIC   0xA1B2C3D4
#define HPT_IOCTL_MAGIC32   0x1A2B3C4D

struct hpt_iopmu_itl {
	u_int32_t resrved0[4];
	u_int32_t inbound_msgaddr0;
	u_int32_t inbound_msgaddr1;
	u_int32_t outbound_msgaddr0;
	u_int32_t outbound_msgaddr1;
	u_int32_t inbound_doorbell;
	u_int32_t inbound_intstatus;
	u_int32_t inbound_intmask;
	u_int32_t outbound_doorbell;
	u_int32_t outbound_intstatus;
	u_int32_t outbound_intmask;
	u_int32_t reserved1[2];
	u_int32_t inbound_queue;
	u_int32_t outbound_queue;
};

#define IOPMU_QUEUE_EMPTY            0xffffffff
#define IOPMU_QUEUE_MASK_HOST_BITS   0xf0000000
#define IOPMU_QUEUE_ADDR_HOST_BIT    0x80000000
#define IOPMU_QUEUE_REQUEST_SIZE_BIT    0x40000000
#define IOPMU_QUEUE_REQUEST_RESULT_BIT   0x40000000
#define IOPMU_MAX_MEM_SUPPORT_MASK_64G 0xfffffff000000000ull
#define IOPMU_MAX_MEM_SUPPORT_MASK_32G 0xfffffff800000000ull

#define IOPMU_OUTBOUND_INT_MSG0      1
#define IOPMU_OUTBOUND_INT_MSG1      2
#define IOPMU_OUTBOUND_INT_DOORBELL  4
#define IOPMU_OUTBOUND_INT_POSTQUEUE 8
#define IOPMU_OUTBOUND_INT_PCI       0x10

#define IOPMU_INBOUND_INT_MSG0       1
#define IOPMU_INBOUND_INT_MSG1       2
#define IOPMU_INBOUND_INT_DOORBELL   4
#define IOPMU_INBOUND_INT_ERROR      8
#define IOPMU_INBOUND_INT_POSTQUEUE  0x10

#define MVIOP_QUEUE_LEN  512
struct hpt_iopmu_mv {
	u_int32_t inbound_head;
	u_int32_t inbound_tail;
	u_int32_t outbound_head;
	u_int32_t outbound_tail;
	u_int32_t inbound_msg;
	u_int32_t outbound_msg;
	u_int32_t reserve[10];
	u_int64_t inbound_q[MVIOP_QUEUE_LEN];
	u_int64_t outbound_q[MVIOP_QUEUE_LEN];
};

struct hpt_iopmv_regs {
	u_int32_t reserved[0x20400 / 4];
	u_int32_t inbound_doorbell;
	u_int32_t inbound_intmask;
	u_int32_t outbound_doorbell;
	u_int32_t outbound_intmask;
};

#define CL_POINTER_TOGGLE        0x00004000
#define CPU_TO_F0_DRBL_MSG_A_BIT 0x02000000

#pragma pack(1)
struct hpt_iopmu_mvfrey {
	u_int32_t reserved[0x4000 / 4];

	/* hpt_frey_com_reg */
	u_int32_t inbound_base; /* 0x4000 : 0 */
	u_int32_t inbound_base_high; /* 4 */
	u_int32_t reserved2[(0x18 - 8)/ 4];
	u_int32_t inbound_write_ptr; /* 0x18 */
	u_int32_t inbound_read_ptr; /* 0x1c */
	u_int32_t reserved3[(0x2c - 0x20) / 4];
	u_int32_t inbound_conf_ctl; /* 0x2c */
	u_int32_t reserved4[(0x50 - 0x30) / 4];
	u_int32_t outbound_base; /* 0x50 */
	u_int32_t outbound_base_high; /* 0x54 */
	u_int32_t outbound_shadow_base; /* 0x58 */
	u_int32_t outbound_shadow_base_high; /* 0x5c */
	u_int32_t reserved5[(0x68 - 0x60) / 4];
	u_int32_t outbound_write; /* 0x68 */
	u_int32_t reserved6[(0x70 - 0x6c) / 4];
	u_int32_t outbound_read; /* 0x70 */
	u_int32_t reserved7[(0x88 - 0x74) / 4];
	u_int32_t isr_cause; /* 0x88 */
	u_int32_t isr_enable; /* 0x8c */

	u_int32_t reserved8[(0x10200 - 0x4090) / 4];

	/* hpt_frey_intr_ctl intr_ctl */
	u_int32_t main_int_cuase; /* 0x10200: 0 */
	u_int32_t main_irq_enable; /* 4 */
	u_int32_t main_fiq_enable; /* 8 */
	u_int32_t pcie_f0_int_enable; /* 0xc */
	u_int32_t pcie_f1_int_enable; /* 0x10 */
	u_int32_t pcie_f2_int_enable; /* 0x14 */
	u_int32_t pcie_f3_int_enable; /* 0x18 */

	u_int32_t reserved9[(0x10400 - 0x1021c) / 4];

	/* hpt_frey_msg_drbl */
	u_int32_t f0_to_cpu_msg_a; /* 0x10400: 0 */
	u_int32_t reserved10[(0x20 - 4) / 4];
	u_int32_t cpu_to_f0_msg_a; /* 0x20 */
	u_int32_t reserved11[(0x80 - 0x24) / 4];
	u_int32_t f0_doorbell; /* 0x80 */
	u_int32_t f0_doorbell_enable; /* 0x84 */
};

struct mvfrey_inlist_entry {
	u_int64_t addr;
	u_int32_t intrfc_len;
	u_int32_t reserved;
};

struct mvfrey_outlist_entry {
	u_int32_t val;
};

#pragma pack()

#define MVIOP_IOCTLCFG_SIZE	0x800
#define MVIOP_MU_QUEUE_ADDR_HOST_MASK   (~(0x1full))
#define MVIOP_MU_QUEUE_ADDR_HOST_BIT    4

#define MVIOP_MU_QUEUE_ADDR_IOP_HIGH32  0xffffffff
#define MVIOP_MU_QUEUE_REQUEST_RESULT_BIT   1
#define MVIOP_MU_QUEUE_REQUEST_RETURN_CONTEXT 2

#define MVIOP_MU_INBOUND_INT_MSG        1
#define MVIOP_MU_INBOUND_INT_POSTQUEUE  2
#define MVIOP_MU_OUTBOUND_INT_MSG       1
#define MVIOP_MU_OUTBOUND_INT_POSTQUEUE 2

#define MVIOP_CMD_TYPE_GET_CONFIG (1 << 5)
#define MVIOP_CMD_TYPE_SET_CONFIG (1 << 6)
#define MVIOP_CMD_TYPE_SCSI (1 << 7)
#define MVIOP_CMD_TYPE_IOCTL (1 << 8)
#define MVIOP_CMD_TYPE_BLOCK (1 << 9)

#define MVIOP_REQUEST_NUMBER_START_BIT 16

#define MVFREYIOPMU_QUEUE_REQUEST_RESULT_BIT   0x40000000

enum hpt_iopmu_message {
	/* host-to-iop messages */
	IOPMU_INBOUND_MSG0_NOP = 0,
	IOPMU_INBOUND_MSG0_RESET,
	IOPMU_INBOUND_MSG0_FLUSH,
	IOPMU_INBOUND_MSG0_SHUTDOWN,
	IOPMU_INBOUND_MSG0_STOP_BACKGROUND_TASK,
	IOPMU_INBOUND_MSG0_START_BACKGROUND_TASK,
	IOPMU_INBOUND_MSG0_RESET_COMM,
	IOPMU_INBOUND_MSG0_MAX = 0xff,
	/* iop-to-host messages */
	IOPMU_OUTBOUND_MSG0_REGISTER_DEVICE_0 = 0x100,
	IOPMU_OUTBOUND_MSG0_REGISTER_DEVICE_MAX = 0x1ff,
	IOPMU_OUTBOUND_MSG0_UNREGISTER_DEVICE_0 = 0x200,
	IOPMU_OUTBOUND_MSG0_UNREGISTER_DEVICE_MAX = 0x2ff,
	IOPMU_OUTBOUND_MSG0_REVALIDATE_DEVICE_0 = 0x300,
	IOPMU_OUTBOUND_MSG0_REVALIDATE_DEVICE_MAX = 0x3ff,
};

#define IOP_REQUEST_FLAG_SYNC_REQUEST 1
#define IOP_REQUEST_FLAG_BIST_REQUEST 2
#define IOP_REQUEST_FLAG_REMAPPED     4
#define IOP_REQUEST_FLAG_OUTPUT_CONTEXT 8

#define IOP_REQUEST_FLAG_ADDR_BITS 0x40 /* flags[31:16] is phy_addr[47:32] */

enum hpt_iop_request_type {
	IOP_REQUEST_TYPE_GET_CONFIG = 0,
	IOP_REQUEST_TYPE_SET_CONFIG,
	IOP_REQUEST_TYPE_BLOCK_COMMAND,
	IOP_REQUEST_TYPE_SCSI_COMMAND,
	IOP_REQUEST_TYPE_IOCTL_COMMAND,
	IOP_REQUEST_TYPE_MAX
};

enum hpt_iop_result_type {
	IOP_RESULT_PENDING = 0,
	IOP_RESULT_SUCCESS,
	IOP_RESULT_FAIL,
	IOP_RESULT_BUSY,
	IOP_RESULT_RESET,
	IOP_RESULT_INVALID_REQUEST,
	IOP_RESULT_BAD_TARGET,
	IOP_RESULT_CHECK_CONDITION,
};

#pragma pack(1)
struct hpt_iop_request_header {
	u_int32_t size;
	u_int32_t type;
	u_int32_t flags;
	u_int32_t result;
	u_int64_t context; /* host context */
};

struct hpt_iop_request_get_config {
	struct hpt_iop_request_header header;
	u_int32_t interface_version;
	u_int32_t firmware_version;
	u_int32_t max_requests;
	u_int32_t request_size;
	u_int32_t max_sg_count;
	u_int32_t data_transfer_length;
	u_int32_t alignment_mask;
	u_int32_t max_devices;
	u_int32_t sdram_size;
};

struct hpt_iop_request_set_config {
	struct hpt_iop_request_header header;
	u_int32_t iop_id;
	u_int16_t vbus_id;
	u_int16_t max_host_request_size;
	u_int32_t reserve[6];
};

struct hpt_iopsg {
	u_int32_t size;
	u_int32_t eot; /* non-zero: end of table */
	u_int64_t pci_address;
};

#define IOP_BLOCK_COMMAND_READ     1
#define IOP_BLOCK_COMMAND_WRITE    2
#define IOP_BLOCK_COMMAND_VERIFY   3
#define IOP_BLOCK_COMMAND_FLUSH    4
#define IOP_BLOCK_COMMAND_SHUTDOWN 5
struct hpt_iop_request_block_command {
	struct hpt_iop_request_header header;
	u_int8_t     channel;
	u_int8_t     target;
	u_int8_t     lun;
	u_int8_t     pad1;
	u_int16_t    command; /* IOP_BLOCK_COMMAND_{READ,WRITE} */
	u_int16_t    sectors;
	u_int64_t    lba;
	struct hpt_iopsg sg_list[1];
};

struct hpt_iop_request_scsi_command {
	struct hpt_iop_request_header header;
	u_int8_t     channel;
	u_int8_t     target;
	u_int8_t     lun;
	u_int8_t     pad1;
	u_int8_t     cdb[16];
	u_int32_t    dataxfer_length;
	struct hpt_iopsg sg_list[1];
};

struct hpt_iop_request_ioctl_command {
	struct hpt_iop_request_header header;
	u_int32_t    ioctl_code;
	u_int32_t    inbuf_size;
	u_int32_t    outbuf_size;
	u_int32_t    bytes_returned;
	u_int8_t     buf[1];
	/* out data should be put at buf[(inbuf_size+3)&~3] */
};

struct hpt_iop_ioctl_param {
	u_int32_t        Magic;                 /* used to check if it's a valid ioctl packet */
	u_int32_t        dwIoControlCode;       /* operation control code */
	unsigned long    lpInBuffer;            /* input data buffer */
	u_int32_t        nInBufferSize;         /* size of input data buffer */
	unsigned long    lpOutBuffer;           /* output data buffer */
	u_int32_t        nOutBufferSize;        /* size of output data buffer */
	unsigned long    lpBytesReturned;       /* count of HPT_U8s returned */
} __packed;

#define HPT_IOCTL_FLAG_OPEN 1
#define HPT_CTL_CODE_BSD_TO_IOP(x) ((x)-0xff00)

typedef struct cdev * ioctl_dev_t;

typedef struct thread * ioctl_thread_t;

struct hpt_iop_hba {
	struct hptiop_adapter_ops *ops;
	union {
		struct {
			struct hpt_iopmu_itl *mu;
		} itl;
		struct {
			struct hpt_iopmv_regs *regs;
			struct hpt_iopmu_mv *mu;
		} mv;
		struct {
			struct hpt_iop_request_get_config *config;
			struct hpt_iopmu_mvfrey *mu;

			int internal_mem_size;
			int list_count;
			struct mvfrey_inlist_entry *inlist;
			u_int64_t inlist_phy;
			u_int32_t inlist_wptr;
			struct mvfrey_outlist_entry *outlist;
			u_int64_t outlist_phy;
			u_int32_t *outlist_cptr; /* copy pointer shadow */
			u_int64_t outlist_cptr_phy;
			u_int32_t outlist_rptr;
		} mvfrey;
	} u;
	
	struct hpt_iop_hba    *next;
	
	u_int32_t             firmware_version;
	u_int32_t             interface_version;
	u_int32_t             max_devices;
	u_int32_t             max_requests;
	u_int32_t             max_request_size;
	u_int32_t             max_sg_count;

	u_int32_t             msg_done;

	device_t              pcidev;
	u_int32_t             pciunit;
	ioctl_dev_t           ioctl_dev;

	bus_dma_tag_t         parent_dmat;
	bus_dma_tag_t         io_dmat;
	bus_dma_tag_t         srb_dmat;
	bus_dma_tag_t	      ctlcfg_dmat;
	
	bus_dmamap_t          srb_dmamap;
	bus_dmamap_t          ctlcfg_dmamap;

	struct resource       *bar0_res;
	bus_space_tag_t       bar0t;
	bus_space_handle_t    bar0h;
	int                   bar0_rid;

	struct resource       *bar2_res;
	bus_space_tag_t	      bar2t;
	bus_space_handle_t    bar2h;
	int                   bar2_rid;
	
	/* to release */
	u_int8_t              *uncached_ptr;
	void		      *ctlcfg_ptr;
	/* for scsi request block */
	struct hpt_iop_srb    *srb_list;
	/* for interrupt */
	struct resource       *irq_res;
	void                  *irq_handle;

	/* for ioctl and set/get config */
	struct resource	      *ctlcfg_res;
	void		      *ctlcfg_handle;
	u_int64_t             ctlcfgcmd_phy;
	u_int32_t             config_done; /* can be negative value */
	u_int32_t             initialized:1;

	/* other resources */
	struct cam_sim        *sim;
	struct cam_path       *path;
	void                  *req;
	struct mtx            lock;
#define HPT_IOCTL_FLAG_OPEN     1
	u_int32_t             flag;
	struct hpt_iop_srb* srb[HPT_SRB_MAX_QUEUE_SIZE];
};
#pragma pack()

enum hptiop_family {
	INTEL_BASED_IOP = 0,
	MV_BASED_IOP,
	MVFREY_BASED_IOP,
	UNKNOWN_BASED_IOP = 0xf
};

struct hptiop_adapter_ops {
	enum hptiop_family family;
	int  (*iop_wait_ready)(struct hpt_iop_hba *hba, u_int32_t millisec);
	int  (*internal_memalloc)(struct hpt_iop_hba *hba);
	int  (*internal_memfree)(struct hpt_iop_hba *hba);
	int  (*alloc_pci_res)(struct hpt_iop_hba *hba);
	void (*release_pci_res)(struct hpt_iop_hba *hba);
	void (*enable_intr)(struct hpt_iop_hba *hba);
	void (*disable_intr)(struct hpt_iop_hba *hba);
	int  (*get_config)(struct hpt_iop_hba *hba,
				struct hpt_iop_request_get_config *config);
	int  (*set_config)(struct hpt_iop_hba *hba,
				struct hpt_iop_request_set_config *config);
	int  (*iop_intr)(struct hpt_iop_hba *hba);
	void (*post_msg)(struct hpt_iop_hba *hba, u_int32_t msg);
	void (*post_req)(struct hpt_iop_hba *hba, struct hpt_iop_srb *srb, bus_dma_segment_t *segs, int nsegs);
	int (*do_ioctl)(struct hpt_iop_hba *hba, struct hpt_iop_ioctl_param * pParams);
	int (*reset_comm)(struct hpt_iop_hba *hba);
};

struct hpt_iop_srb {
	u_int8_t             req[HPT_SRB_MAX_REQ_SIZE];
	struct hpt_iop_hba   *hba;
	union ccb            *ccb;
	struct hpt_iop_srb   *next;
	bus_dmamap_t         dma_map;
	u_int64_t            phy_addr;
	u_int32_t            srb_flag;
	int                  index;
	struct callout	     timeout;
};

#define hptiop_lock_adapter(hba)   mtx_lock(&(hba)->lock)
#define hptiop_unlock_adapter(hba) mtx_unlock(&(hba)->lock)

#define HPT_OSM_TIMEOUT (20*hz)  /* timeout value for OS commands */

#define HPT_DO_IOCONTROL    _IOW('H', 0, struct hpt_iop_ioctl_param)
#define HPT_SCAN_BUS        _IO('H', 1)

static  __inline int hptiop_sleep(struct hpt_iop_hba *hba, void *ident,
				int priority, const char *wmesg, int timo)
{

	int retval;

	retval = msleep(ident, &hba->lock, priority, wmesg, timo);

	return retval;

}


#define HPT_DEV_MAJOR   200

#endif

