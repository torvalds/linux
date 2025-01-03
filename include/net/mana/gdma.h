/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (c) 2021, Microsoft Corporation. */

#ifndef _GDMA_H
#define _GDMA_H

#include <linux/dma-mapping.h>
#include <linux/netdevice.h>

#include "shm_channel.h"

#define GDMA_STATUS_MORE_ENTRIES	0x00000105

/* Structures labeled with "HW DATA" are exchanged with the hardware. All of
 * them are naturally aligned and hence don't need __packed.
 */

enum gdma_request_type {
	GDMA_VERIFY_VF_DRIVER_VERSION	= 1,
	GDMA_QUERY_MAX_RESOURCES	= 2,
	GDMA_LIST_DEVICES		= 3,
	GDMA_REGISTER_DEVICE		= 4,
	GDMA_DEREGISTER_DEVICE		= 5,
	GDMA_GENERATE_TEST_EQE		= 10,
	GDMA_CREATE_QUEUE		= 12,
	GDMA_DISABLE_QUEUE		= 13,
	GDMA_ALLOCATE_RESOURCE_RANGE	= 22,
	GDMA_DESTROY_RESOURCE_RANGE	= 24,
	GDMA_CREATE_DMA_REGION		= 25,
	GDMA_DMA_REGION_ADD_PAGES	= 26,
	GDMA_DESTROY_DMA_REGION		= 27,
	GDMA_CREATE_PD			= 29,
	GDMA_DESTROY_PD			= 30,
	GDMA_CREATE_MR			= 31,
	GDMA_DESTROY_MR			= 32,
	GDMA_QUERY_HWC_TIMEOUT		= 84, /* 0x54 */
};

#define GDMA_RESOURCE_DOORBELL_PAGE	27

enum gdma_queue_type {
	GDMA_INVALID_QUEUE,
	GDMA_SQ,
	GDMA_RQ,
	GDMA_CQ,
	GDMA_EQ,
};

enum gdma_work_request_flags {
	GDMA_WR_NONE			= 0,
	GDMA_WR_OOB_IN_SGL		= BIT(0),
	GDMA_WR_PAD_BY_SGE0		= BIT(1),
};

enum gdma_eqe_type {
	GDMA_EQE_COMPLETION		= 3,
	GDMA_EQE_TEST_EVENT		= 64,
	GDMA_EQE_HWC_INIT_EQ_ID_DB	= 129,
	GDMA_EQE_HWC_INIT_DATA		= 130,
	GDMA_EQE_HWC_INIT_DONE		= 131,
	GDMA_EQE_HWC_SOC_RECONFIG	= 132,
	GDMA_EQE_HWC_SOC_RECONFIG_DATA	= 133,
	GDMA_EQE_RNIC_QP_FATAL		= 176,
};

enum {
	GDMA_DEVICE_NONE	= 0,
	GDMA_DEVICE_HWC		= 1,
	GDMA_DEVICE_MANA	= 2,
	GDMA_DEVICE_MANA_IB	= 3,
};

struct gdma_resource {
	/* Protect the bitmap */
	spinlock_t lock;

	/* The bitmap size in bits. */
	u32 size;

	/* The bitmap tracks the resources. */
	unsigned long *map;
};

union gdma_doorbell_entry {
	u64	as_uint64;

	struct {
		u64 id		: 24;
		u64 reserved	: 8;
		u64 tail_ptr	: 31;
		u64 arm		: 1;
	} cq;

	struct {
		u64 id		: 24;
		u64 wqe_cnt	: 8;
		u64 tail_ptr	: 32;
	} rq;

	struct {
		u64 id		: 24;
		u64 reserved	: 8;
		u64 tail_ptr	: 32;
	} sq;

	struct {
		u64 id		: 16;
		u64 reserved	: 16;
		u64 tail_ptr	: 31;
		u64 arm		: 1;
	} eq;
}; /* HW DATA */

struct gdma_msg_hdr {
	u32 hdr_type;
	u32 msg_type;
	u16 msg_version;
	u16 hwc_msg_id;
	u32 msg_size;
}; /* HW DATA */

struct gdma_dev_id {
	union {
		struct {
			u16 type;
			u16 instance;
		};

		u32 as_uint32;
	};
}; /* HW DATA */

struct gdma_req_hdr {
	struct gdma_msg_hdr req;
	struct gdma_msg_hdr resp; /* The expected response */
	struct gdma_dev_id dev_id;
	u32 activity_id;
}; /* HW DATA */

struct gdma_resp_hdr {
	struct gdma_msg_hdr response;
	struct gdma_dev_id dev_id;
	u32 activity_id;
	u32 status;
	u32 reserved;
}; /* HW DATA */

struct gdma_general_req {
	struct gdma_req_hdr hdr;
}; /* HW DATA */

#define GDMA_MESSAGE_V1 1
#define GDMA_MESSAGE_V2 2
#define GDMA_MESSAGE_V3 3

struct gdma_general_resp {
	struct gdma_resp_hdr hdr;
}; /* HW DATA */

#define GDMA_STANDARD_HEADER_TYPE 0

static inline void mana_gd_init_req_hdr(struct gdma_req_hdr *hdr, u32 code,
					u32 req_size, u32 resp_size)
{
	hdr->req.hdr_type = GDMA_STANDARD_HEADER_TYPE;
	hdr->req.msg_type = code;
	hdr->req.msg_version = GDMA_MESSAGE_V1;
	hdr->req.msg_size = req_size;

	hdr->resp.hdr_type = GDMA_STANDARD_HEADER_TYPE;
	hdr->resp.msg_type = code;
	hdr->resp.msg_version = GDMA_MESSAGE_V1;
	hdr->resp.msg_size = resp_size;
}

/* The 16-byte struct is part of the GDMA work queue entry (WQE). */
struct gdma_sge {
	u64 address;
	u32 mem_key;
	u32 size;
}; /* HW DATA */

struct gdma_wqe_request {
	struct gdma_sge *sgl;
	u32 num_sge;

	u32 inline_oob_size;
	const void *inline_oob_data;

	u32 flags;
	u32 client_data_unit;
};

enum gdma_page_type {
	GDMA_PAGE_TYPE_4K,
};

#define GDMA_INVALID_DMA_REGION 0

struct gdma_mem_info {
	struct device *dev;

	dma_addr_t dma_handle;
	void *virt_addr;
	u64 length;

	/* Allocated by the PF driver */
	u64 dma_region_handle;
};

#define REGISTER_ATB_MST_MKEY_LOWER_SIZE 8

struct gdma_dev {
	struct gdma_context *gdma_context;

	struct gdma_dev_id dev_id;

	u32 pdid;
	u32 doorbell;
	u32 gpa_mkey;

	/* GDMA driver specific pointer */
	void *driver_data;

	struct auxiliary_device *adev;
};

/* MANA_PAGE_SIZE is the DMA unit */
#define MANA_PAGE_SHIFT 12
#define MANA_PAGE_SIZE BIT(MANA_PAGE_SHIFT)
#define MANA_PAGE_ALIGN(x) ALIGN((x), MANA_PAGE_SIZE)
#define MANA_PAGE_ALIGNED(addr) IS_ALIGNED((unsigned long)(addr), MANA_PAGE_SIZE)
#define MANA_PFN(a) ((a) >> MANA_PAGE_SHIFT)

/* Required by HW */
#define MANA_MIN_QSIZE MANA_PAGE_SIZE

#define GDMA_CQE_SIZE 64
#define GDMA_EQE_SIZE 16
#define GDMA_MAX_SQE_SIZE 512
#define GDMA_MAX_RQE_SIZE 256

#define GDMA_COMP_DATA_SIZE 0x3C

#define GDMA_EVENT_DATA_SIZE 0xC

/* The WQE size must be a multiple of the Basic Unit, which is 32 bytes. */
#define GDMA_WQE_BU_SIZE 32

#define INVALID_PDID		UINT_MAX
#define INVALID_DOORBELL	UINT_MAX
#define INVALID_MEM_KEY		UINT_MAX
#define INVALID_QUEUE_ID	UINT_MAX
#define INVALID_PCI_MSIX_INDEX  UINT_MAX

struct gdma_comp {
	u32 cqe_data[GDMA_COMP_DATA_SIZE / 4];
	u32 wq_num;
	bool is_sq;
};

struct gdma_event {
	u32 details[GDMA_EVENT_DATA_SIZE / 4];
	u8  type;
};

struct gdma_queue;

struct mana_eq {
	struct gdma_queue	*eq;
	struct dentry		*mana_eq_debugfs;
};

typedef void gdma_eq_callback(void *context, struct gdma_queue *q,
			      struct gdma_event *e);

typedef void gdma_cq_callback(void *context, struct gdma_queue *q);

/* The 'head' is the producer index. For SQ/RQ, when the driver posts a WQE
 * (Note: the WQE size must be a multiple of the 32-byte Basic Unit), the
 * driver increases the 'head' in BUs rather than in bytes, and notifies
 * the HW of the updated head. For EQ/CQ, the driver uses the 'head' to track
 * the HW head, and increases the 'head' by 1 for every processed EQE/CQE.
 *
 * The 'tail' is the consumer index for SQ/RQ. After the CQE of the SQ/RQ is
 * processed, the driver increases the 'tail' to indicate that WQEs have
 * been consumed by the HW, so the driver can post new WQEs into the SQ/RQ.
 *
 * The driver doesn't use the 'tail' for EQ/CQ, because the driver ensures
 * that the EQ/CQ is big enough so they can't overflow, and the driver uses
 * the owner bits mechanism to detect if the queue has become empty.
 */
struct gdma_queue {
	struct gdma_dev *gdma_dev;

	enum gdma_queue_type type;
	u32 id;

	struct gdma_mem_info mem_info;

	void *queue_mem_ptr;
	u32 queue_size;

	bool monitor_avl_buf;

	u32 head;
	u32 tail;
	struct list_head entry;

	/* Extra fields specific to EQ/CQ. */
	union {
		struct {
			bool disable_needed;

			gdma_eq_callback *callback;
			void *context;

			unsigned int msix_index;

			u32 log2_throttle_limit;
		} eq;

		struct {
			gdma_cq_callback *callback;
			void *context;

			struct gdma_queue *parent; /* For CQ/EQ relationship */
		} cq;
	};
};

struct gdma_queue_spec {
	enum gdma_queue_type type;
	bool monitor_avl_buf;
	unsigned int queue_size;

	/* Extra fields specific to EQ/CQ. */
	union {
		struct {
			gdma_eq_callback *callback;
			void *context;

			unsigned long log2_throttle_limit;
			unsigned int msix_index;
		} eq;

		struct {
			gdma_cq_callback *callback;
			void *context;

			struct gdma_queue *parent_eq;

		} cq;
	};
};

#define MANA_IRQ_NAME_SZ 32

struct gdma_irq_context {
	void (*handler)(void *arg);
	/* Protect the eq_list */
	spinlock_t lock;
	struct list_head eq_list;
	char name[MANA_IRQ_NAME_SZ];
};

struct gdma_context {
	struct device		*dev;
	struct dentry		*mana_pci_debugfs;

	/* Per-vPort max number of queues */
	unsigned int		max_num_queues;
	unsigned int		max_num_msix;
	unsigned int		num_msix_usable;
	struct gdma_irq_context	*irq_contexts;

	/* L2 MTU */
	u16 adapter_mtu;

	/* This maps a CQ index to the queue structure. */
	unsigned int		max_num_cqs;
	struct gdma_queue	**cq_table;

	/* Protect eq_test_event and test_event_eq_id  */
	struct mutex		eq_test_event_mutex;
	struct completion	eq_test_event;
	u32			test_event_eq_id;

	bool			is_pf;
	phys_addr_t		bar0_pa;
	void __iomem		*bar0_va;
	void __iomem		*shm_base;
	void __iomem		*db_page_base;
	phys_addr_t		phys_db_page_base;
	u32 db_page_size;
	int                     numa_node;

	/* Shared memory chanenl (used to bootstrap HWC) */
	struct shm_channel	shm_channel;

	/* Hardware communication channel (HWC) */
	struct gdma_dev		hwc;

	/* Azure network adapter */
	struct gdma_dev		mana;

	/* Azure RDMA adapter */
	struct gdma_dev		mana_ib;
};

#define MAX_NUM_GDMA_DEVICES	4

static inline bool mana_gd_is_mana(struct gdma_dev *gd)
{
	return gd->dev_id.type == GDMA_DEVICE_MANA;
}

static inline bool mana_gd_is_hwc(struct gdma_dev *gd)
{
	return gd->dev_id.type == GDMA_DEVICE_HWC;
}

u8 *mana_gd_get_wqe_ptr(const struct gdma_queue *wq, u32 wqe_offset);
u32 mana_gd_wq_avail_space(struct gdma_queue *wq);

int mana_gd_test_eq(struct gdma_context *gc, struct gdma_queue *eq);

int mana_gd_create_hwc_queue(struct gdma_dev *gd,
			     const struct gdma_queue_spec *spec,
			     struct gdma_queue **queue_ptr);

int mana_gd_create_mana_eq(struct gdma_dev *gd,
			   const struct gdma_queue_spec *spec,
			   struct gdma_queue **queue_ptr);

int mana_gd_create_mana_wq_cq(struct gdma_dev *gd,
			      const struct gdma_queue_spec *spec,
			      struct gdma_queue **queue_ptr);

void mana_gd_destroy_queue(struct gdma_context *gc, struct gdma_queue *queue);

int mana_gd_poll_cq(struct gdma_queue *cq, struct gdma_comp *comp, int num_cqe);

void mana_gd_ring_cq(struct gdma_queue *cq, u8 arm_bit);

struct gdma_wqe {
	u32 reserved	:24;
	u32 last_vbytes	:8;

	union {
		u32 flags;

		struct {
			u32 num_sge		:8;
			u32 inline_oob_size_div4:3;
			u32 client_oob_in_sgl	:1;
			u32 reserved1		:4;
			u32 client_data_unit	:14;
			u32 reserved2		:2;
		};
	};
}; /* HW DATA */

#define INLINE_OOB_SMALL_SIZE 8
#define INLINE_OOB_LARGE_SIZE 24

#define MAX_TX_WQE_SIZE 512
#define MAX_RX_WQE_SIZE 256

#define MAX_TX_WQE_SGL_ENTRIES	((GDMA_MAX_SQE_SIZE -			   \
			sizeof(struct gdma_sge) - INLINE_OOB_SMALL_SIZE) / \
			sizeof(struct gdma_sge))

#define MAX_RX_WQE_SGL_ENTRIES	((GDMA_MAX_RQE_SIZE -			   \
			sizeof(struct gdma_sge)) / sizeof(struct gdma_sge))

struct gdma_cqe {
	u32 cqe_data[GDMA_COMP_DATA_SIZE / 4];

	union {
		u32 as_uint32;

		struct {
			u32 wq_num	: 24;
			u32 is_sq	: 1;
			u32 reserved	: 4;
			u32 owner_bits	: 3;
		};
	} cqe_info;
}; /* HW DATA */

#define GDMA_CQE_OWNER_BITS 3

#define GDMA_CQE_OWNER_MASK ((1 << GDMA_CQE_OWNER_BITS) - 1)

#define SET_ARM_BIT 1

#define GDMA_EQE_OWNER_BITS 3

union gdma_eqe_info {
	u32 as_uint32;

	struct {
		u32 type	: 8;
		u32 reserved1	: 8;
		u32 client_id	: 2;
		u32 reserved2	: 11;
		u32 owner_bits	: 3;
	};
}; /* HW DATA */

#define GDMA_EQE_OWNER_MASK ((1 << GDMA_EQE_OWNER_BITS) - 1)
#define INITIALIZED_OWNER_BIT(log2_num_entries) (1UL << (log2_num_entries))

struct gdma_eqe {
	u32 details[GDMA_EVENT_DATA_SIZE / 4];
	u32 eqe_info;
}; /* HW DATA */

#define GDMA_REG_DB_PAGE_OFFSET	8
#define GDMA_REG_DB_PAGE_SIZE	0x10
#define GDMA_REG_SHM_OFFSET	0x18

#define GDMA_PF_REG_DB_PAGE_SIZE	0xD0
#define GDMA_PF_REG_DB_PAGE_OFF		0xC8
#define GDMA_PF_REG_SHM_OFF		0x70

#define GDMA_SRIOV_REG_CFG_BASE_OFF	0x108

#define MANA_PF_DEVICE_ID 0x00B9
#define MANA_VF_DEVICE_ID 0x00BA

struct gdma_posted_wqe_info {
	u32 wqe_size_in_bu;
};

/* GDMA_GENERATE_TEST_EQE */
struct gdma_generate_test_event_req {
	struct gdma_req_hdr hdr;
	u32 queue_index;
}; /* HW DATA */

/* GDMA_VERIFY_VF_DRIVER_VERSION */
enum {
	GDMA_PROTOCOL_V1	= 1,
	GDMA_PROTOCOL_FIRST	= GDMA_PROTOCOL_V1,
	GDMA_PROTOCOL_LAST	= GDMA_PROTOCOL_V1,
};

#define GDMA_DRV_CAP_FLAG_1_EQ_SHARING_MULTI_VPORT BIT(0)

/* Advertise to the NIC firmware: the NAPI work_done variable race is fixed,
 * so the driver is able to reliably support features like busy_poll.
 */
#define GDMA_DRV_CAP_FLAG_1_NAPI_WKDONE_FIX BIT(2)
#define GDMA_DRV_CAP_FLAG_1_HWC_TIMEOUT_RECONFIG BIT(3)
#define GDMA_DRV_CAP_FLAG_1_VARIABLE_INDIRECTION_TABLE_SUPPORT BIT(5)

#define GDMA_DRV_CAP_FLAGS1 \
	(GDMA_DRV_CAP_FLAG_1_EQ_SHARING_MULTI_VPORT | \
	 GDMA_DRV_CAP_FLAG_1_NAPI_WKDONE_FIX | \
	 GDMA_DRV_CAP_FLAG_1_HWC_TIMEOUT_RECONFIG | \
	 GDMA_DRV_CAP_FLAG_1_VARIABLE_INDIRECTION_TABLE_SUPPORT)

#define GDMA_DRV_CAP_FLAGS2 0

#define GDMA_DRV_CAP_FLAGS3 0

#define GDMA_DRV_CAP_FLAGS4 0

struct gdma_verify_ver_req {
	struct gdma_req_hdr hdr;

	/* Mandatory fields required for protocol establishment */
	u64 protocol_ver_min;
	u64 protocol_ver_max;

	/* Gdma Driver Capability Flags */
	u64 gd_drv_cap_flags1;
	u64 gd_drv_cap_flags2;
	u64 gd_drv_cap_flags3;
	u64 gd_drv_cap_flags4;

	/* Advisory fields */
	u64 drv_ver;
	u32 os_type; /* Linux = 0x10; Windows = 0x20; Other = 0x30 */
	u32 reserved;
	u32 os_ver_major;
	u32 os_ver_minor;
	u32 os_ver_build;
	u32 os_ver_platform;
	u64 reserved_2;
	u8 os_ver_str1[128];
	u8 os_ver_str2[128];
	u8 os_ver_str3[128];
	u8 os_ver_str4[128];
}; /* HW DATA */

struct gdma_verify_ver_resp {
	struct gdma_resp_hdr hdr;
	u64 gdma_protocol_ver;
	u64 pf_cap_flags1;
	u64 pf_cap_flags2;
	u64 pf_cap_flags3;
	u64 pf_cap_flags4;
}; /* HW DATA */

/* GDMA_QUERY_MAX_RESOURCES */
struct gdma_query_max_resources_resp {
	struct gdma_resp_hdr hdr;
	u32 status;
	u32 max_sq;
	u32 max_rq;
	u32 max_cq;
	u32 max_eq;
	u32 max_db;
	u32 max_mst;
	u32 max_cq_mod_ctx;
	u32 max_mod_cq;
	u32 max_msix;
}; /* HW DATA */

/* GDMA_LIST_DEVICES */
struct gdma_list_devices_resp {
	struct gdma_resp_hdr hdr;
	u32 num_of_devs;
	u32 reserved;
	struct gdma_dev_id devs[64];
}; /* HW DATA */

/* GDMA_REGISTER_DEVICE */
struct gdma_register_device_resp {
	struct gdma_resp_hdr hdr;
	u32 pdid;
	u32 gpa_mkey;
	u32 db_id;
}; /* HW DATA */

struct gdma_allocate_resource_range_req {
	struct gdma_req_hdr hdr;
	u32 resource_type;
	u32 num_resources;
	u32 alignment;
	u32 allocated_resources;
};

struct gdma_allocate_resource_range_resp {
	struct gdma_resp_hdr hdr;
	u32 allocated_resources;
};

struct gdma_destroy_resource_range_req {
	struct gdma_req_hdr hdr;
	u32 resource_type;
	u32 num_resources;
	u32 allocated_resources;
};

/* GDMA_CREATE_QUEUE */
struct gdma_create_queue_req {
	struct gdma_req_hdr hdr;
	u32 type;
	u32 reserved1;
	u32 pdid;
	u32 doolbell_id;
	u64 gdma_region;
	u32 reserved2;
	u32 queue_size;
	u32 log2_throttle_limit;
	u32 eq_pci_msix_index;
	u32 cq_mod_ctx_id;
	u32 cq_parent_eq_id;
	u8  rq_drop_on_overrun;
	u8  rq_err_on_wqe_overflow;
	u8  rq_chain_rec_wqes;
	u8  sq_hw_db;
	u32 reserved3;
}; /* HW DATA */

struct gdma_create_queue_resp {
	struct gdma_resp_hdr hdr;
	u32 queue_index;
}; /* HW DATA */

/* GDMA_DISABLE_QUEUE */
struct gdma_disable_queue_req {
	struct gdma_req_hdr hdr;
	u32 type;
	u32 queue_index;
	u32 alloc_res_id_on_creation;
}; /* HW DATA */

/* GDMA_QUERY_HWC_TIMEOUT */
struct gdma_query_hwc_timeout_req {
	struct gdma_req_hdr hdr;
	u32 timeout_ms;
	u32 reserved;
};

struct gdma_query_hwc_timeout_resp {
	struct gdma_resp_hdr hdr;
	u32 timeout_ms;
	u32 reserved;
};

enum atb_page_size {
	ATB_PAGE_SIZE_4K,
	ATB_PAGE_SIZE_8K,
	ATB_PAGE_SIZE_16K,
	ATB_PAGE_SIZE_32K,
	ATB_PAGE_SIZE_64K,
	ATB_PAGE_SIZE_128K,
	ATB_PAGE_SIZE_256K,
	ATB_PAGE_SIZE_512K,
	ATB_PAGE_SIZE_1M,
	ATB_PAGE_SIZE_2M,
	ATB_PAGE_SIZE_MAX,
};

enum gdma_mr_access_flags {
	GDMA_ACCESS_FLAG_LOCAL_READ = BIT_ULL(0),
	GDMA_ACCESS_FLAG_LOCAL_WRITE = BIT_ULL(1),
	GDMA_ACCESS_FLAG_REMOTE_READ = BIT_ULL(2),
	GDMA_ACCESS_FLAG_REMOTE_WRITE = BIT_ULL(3),
	GDMA_ACCESS_FLAG_REMOTE_ATOMIC = BIT_ULL(4),
};

/* GDMA_CREATE_DMA_REGION */
struct gdma_create_dma_region_req {
	struct gdma_req_hdr hdr;

	/* The total size of the DMA region */
	u64 length;

	/* The offset in the first page */
	u32 offset_in_page;

	/* enum gdma_page_type */
	u32 gdma_page_type;

	/* The total number of pages */
	u32 page_count;

	/* If page_addr_list_len is smaller than page_count,
	 * the remaining page addresses will be added via the
	 * message GDMA_DMA_REGION_ADD_PAGES.
	 */
	u32 page_addr_list_len;
	u64 page_addr_list[];
}; /* HW DATA */

struct gdma_create_dma_region_resp {
	struct gdma_resp_hdr hdr;
	u64 dma_region_handle;
}; /* HW DATA */

/* GDMA_DMA_REGION_ADD_PAGES */
struct gdma_dma_region_add_pages_req {
	struct gdma_req_hdr hdr;

	u64 dma_region_handle;

	u32 page_addr_list_len;
	u32 reserved3;

	u64 page_addr_list[];
}; /* HW DATA */

/* GDMA_DESTROY_DMA_REGION */
struct gdma_destroy_dma_region_req {
	struct gdma_req_hdr hdr;

	u64 dma_region_handle;
}; /* HW DATA */

enum gdma_pd_flags {
	GDMA_PD_FLAG_INVALID = 0,
};

struct gdma_create_pd_req {
	struct gdma_req_hdr hdr;
	enum gdma_pd_flags flags;
	u32 reserved;
};/* HW DATA */

struct gdma_create_pd_resp {
	struct gdma_resp_hdr hdr;
	u64 pd_handle;
	u32 pd_id;
	u32 reserved;
};/* HW DATA */

struct gdma_destroy_pd_req {
	struct gdma_req_hdr hdr;
	u64 pd_handle;
};/* HW DATA */

struct gdma_destory_pd_resp {
	struct gdma_resp_hdr hdr;
};/* HW DATA */

enum gdma_mr_type {
	/* Guest Virtual Address - MRs of this type allow access
	 * to memory mapped by PTEs associated with this MR using a virtual
	 * address that is set up in the MST
	 */
	GDMA_MR_TYPE_GVA = 2,
};

struct gdma_create_mr_params {
	u64 pd_handle;
	enum gdma_mr_type mr_type;
	union {
		struct {
			u64 dma_region_handle;
			u64 virtual_address;
			enum gdma_mr_access_flags access_flags;
		} gva;
	};
};

struct gdma_create_mr_request {
	struct gdma_req_hdr hdr;
	u64 pd_handle;
	enum gdma_mr_type mr_type;
	u32 reserved_1;

	union {
		struct {
			u64 dma_region_handle;
			u64 virtual_address;
			enum gdma_mr_access_flags access_flags;
		} gva;

	};
	u32 reserved_2;
};/* HW DATA */

struct gdma_create_mr_response {
	struct gdma_resp_hdr hdr;
	u64 mr_handle;
	u32 lkey;
	u32 rkey;
};/* HW DATA */

struct gdma_destroy_mr_request {
	struct gdma_req_hdr hdr;
	u64 mr_handle;
};/* HW DATA */

struct gdma_destroy_mr_response {
	struct gdma_resp_hdr hdr;
};/* HW DATA */

int mana_gd_verify_vf_version(struct pci_dev *pdev);

int mana_gd_register_device(struct gdma_dev *gd);
int mana_gd_deregister_device(struct gdma_dev *gd);

int mana_gd_post_work_request(struct gdma_queue *wq,
			      const struct gdma_wqe_request *wqe_req,
			      struct gdma_posted_wqe_info *wqe_info);

int mana_gd_post_and_ring(struct gdma_queue *queue,
			  const struct gdma_wqe_request *wqe,
			  struct gdma_posted_wqe_info *wqe_info);

int mana_gd_alloc_res_map(u32 res_avail, struct gdma_resource *r);
void mana_gd_free_res_map(struct gdma_resource *r);

void mana_gd_wq_ring_doorbell(struct gdma_context *gc,
			      struct gdma_queue *queue);

int mana_gd_alloc_memory(struct gdma_context *gc, unsigned int length,
			 struct gdma_mem_info *gmi);

void mana_gd_free_memory(struct gdma_mem_info *gmi);

int mana_gd_send_request(struct gdma_context *gc, u32 req_len, const void *req,
			 u32 resp_len, void *resp);

int mana_gd_destroy_dma_region(struct gdma_context *gc, u64 dma_region_handle);
void mana_register_debugfs(void);
void mana_unregister_debugfs(void);

#endif /* _GDMA_H */
