/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 ARM Ltd.
 */

#ifndef _LINUX_ARM_FFA_H
#define _LINUX_ARM_FFA_H

#include <linux/device.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/uuid.h>

#define FFA_SMC(calling_convention, func_num)				\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, (calling_convention),	\
			   ARM_SMCCC_OWNER_STANDARD, (func_num))

#define FFA_SMC_32(func_num)	FFA_SMC(ARM_SMCCC_SMC_32, (func_num))
#define FFA_SMC_64(func_num)	FFA_SMC(ARM_SMCCC_SMC_64, (func_num))

#define FFA_ERROR			FFA_SMC_32(0x60)
#define FFA_SUCCESS			FFA_SMC_32(0x61)
#define FFA_INTERRUPT			FFA_SMC_32(0x62)
#define FFA_VERSION			FFA_SMC_32(0x63)
#define FFA_FEATURES			FFA_SMC_32(0x64)
#define FFA_RX_RELEASE			FFA_SMC_32(0x65)
#define FFA_RXTX_MAP			FFA_SMC_32(0x66)
#define FFA_FN64_RXTX_MAP		FFA_SMC_64(0x66)
#define FFA_RXTX_UNMAP			FFA_SMC_32(0x67)
#define FFA_PARTITION_INFO_GET		FFA_SMC_32(0x68)
#define FFA_ID_GET			FFA_SMC_32(0x69)
#define FFA_MSG_POLL			FFA_SMC_32(0x6A)
#define FFA_MSG_WAIT			FFA_SMC_32(0x6B)
#define FFA_YIELD			FFA_SMC_32(0x6C)
#define FFA_RUN				FFA_SMC_32(0x6D)
#define FFA_MSG_SEND			FFA_SMC_32(0x6E)
#define FFA_MSG_SEND_DIRECT_REQ		FFA_SMC_32(0x6F)
#define FFA_FN64_MSG_SEND_DIRECT_REQ	FFA_SMC_64(0x6F)
#define FFA_MSG_SEND_DIRECT_RESP	FFA_SMC_32(0x70)
#define FFA_FN64_MSG_SEND_DIRECT_RESP	FFA_SMC_64(0x70)
#define FFA_MEM_DONATE			FFA_SMC_32(0x71)
#define FFA_FN64_MEM_DONATE		FFA_SMC_64(0x71)
#define FFA_MEM_LEND			FFA_SMC_32(0x72)
#define FFA_FN64_MEM_LEND		FFA_SMC_64(0x72)
#define FFA_MEM_SHARE			FFA_SMC_32(0x73)
#define FFA_FN64_MEM_SHARE		FFA_SMC_64(0x73)
#define FFA_MEM_RETRIEVE_REQ		FFA_SMC_32(0x74)
#define FFA_FN64_MEM_RETRIEVE_REQ	FFA_SMC_64(0x74)
#define FFA_MEM_RETRIEVE_RESP		FFA_SMC_32(0x75)
#define FFA_MEM_RELINQUISH		FFA_SMC_32(0x76)
#define FFA_MEM_RECLAIM			FFA_SMC_32(0x77)
#define FFA_MEM_OP_PAUSE		FFA_SMC_32(0x78)
#define FFA_MEM_OP_RESUME		FFA_SMC_32(0x79)
#define FFA_MEM_FRAG_RX			FFA_SMC_32(0x7A)
#define FFA_MEM_FRAG_TX			FFA_SMC_32(0x7B)
#define FFA_NORMAL_WORLD_RESUME		FFA_SMC_32(0x7C)

/*
 * For some calls it is necessary to use SMC64 to pass or return 64-bit values.
 * For such calls FFA_FN_NATIVE(name) will choose the appropriate
 * (native-width) function ID.
 */
#ifdef CONFIG_64BIT
#define FFA_FN_NATIVE(name)	FFA_FN64_##name
#else
#define FFA_FN_NATIVE(name)	FFA_##name
#endif

/* FFA error codes. */
#define FFA_RET_SUCCESS            (0)
#define FFA_RET_NOT_SUPPORTED      (-1)
#define FFA_RET_INVALID_PARAMETERS (-2)
#define FFA_RET_NO_MEMORY          (-3)
#define FFA_RET_BUSY               (-4)
#define FFA_RET_INTERRUPTED        (-5)
#define FFA_RET_DENIED             (-6)
#define FFA_RET_RETRY              (-7)
#define FFA_RET_ABORTED            (-8)

/* FFA version encoding */
#define FFA_MAJOR_VERSION_MASK	GENMASK(30, 16)
#define FFA_MINOR_VERSION_MASK	GENMASK(15, 0)
#define FFA_MAJOR_VERSION(x)	((u16)(FIELD_GET(FFA_MAJOR_VERSION_MASK, (x))))
#define FFA_MINOR_VERSION(x)	((u16)(FIELD_GET(FFA_MINOR_VERSION_MASK, (x))))
#define FFA_PACK_VERSION_INFO(major, minor)			\
	(FIELD_PREP(FFA_MAJOR_VERSION_MASK, (major)) |		\
	 FIELD_PREP(FFA_MINOR_VERSION_MASK, (minor)))
#define FFA_VERSION_1_0		FFA_PACK_VERSION_INFO(1, 0)

/**
 * FF-A specification mentions explicitly about '4K pages'. This should
 * not be confused with the kernel PAGE_SIZE, which is the translation
 * granule kernel is configured and may be one among 4K, 16K and 64K.
 */
#define FFA_PAGE_SIZE		SZ_4K

/*
 * Minimum buffer size/alignment encodings returned by an FFA_FEATURES
 * query for FFA_RXTX_MAP.
 */
#define FFA_FEAT_RXTX_MIN_SZ_4K		0
#define FFA_FEAT_RXTX_MIN_SZ_64K	1
#define FFA_FEAT_RXTX_MIN_SZ_16K	2

/* FFA Bus/Device/Driver related */
struct ffa_device {
	u32 id;
	int vm_id;
	bool mode_32bit;
	uuid_t uuid;
	struct device dev;
	const struct ffa_ops *ops;
};

#define to_ffa_dev(d) container_of(d, struct ffa_device, dev)

struct ffa_device_id {
	uuid_t uuid;
};

struct ffa_driver {
	const char *name;
	int (*probe)(struct ffa_device *sdev);
	void (*remove)(struct ffa_device *sdev);
	const struct ffa_device_id *id_table;

	struct device_driver driver;
};

#define to_ffa_driver(d) container_of(d, struct ffa_driver, driver)

static inline void ffa_dev_set_drvdata(struct ffa_device *fdev, void *data)
{
	dev_set_drvdata(&fdev->dev, data);
}

static inline void *ffa_dev_get_drvdata(struct ffa_device *fdev)
{
	return dev_get_drvdata(&fdev->dev);
}

#if IS_REACHABLE(CONFIG_ARM_FFA_TRANSPORT)
struct ffa_device *ffa_device_register(const uuid_t *uuid, int vm_id,
				       const struct ffa_ops *ops);
void ffa_device_unregister(struct ffa_device *ffa_dev);
int ffa_driver_register(struct ffa_driver *driver, struct module *owner,
			const char *mod_name);
void ffa_driver_unregister(struct ffa_driver *driver);
bool ffa_device_is_valid(struct ffa_device *ffa_dev);

#else
static inline
struct ffa_device *ffa_device_register(const uuid_t *uuid, int vm_id,
				       const struct ffa_ops *ops)
{
	return NULL;
}

static inline void ffa_device_unregister(struct ffa_device *dev) {}

static inline int
ffa_driver_register(struct ffa_driver *driver, struct module *owner,
		    const char *mod_name)
{
	return -EINVAL;
}

static inline void ffa_driver_unregister(struct ffa_driver *driver) {}

static inline
bool ffa_device_is_valid(struct ffa_device *ffa_dev) { return false; }

#endif /* CONFIG_ARM_FFA_TRANSPORT */

#define ffa_register(driver) \
	ffa_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
#define ffa_unregister(driver) \
	ffa_driver_unregister(driver)

/**
 * module_ffa_driver() - Helper macro for registering a psa_ffa driver
 * @__ffa_driver: ffa_driver structure
 *
 * Helper macro for psa_ffa drivers to set up proper module init / exit
 * functions.  Replaces module_init() and module_exit() and keeps people from
 * printing pointless things to the kernel log when their driver is loaded.
 */
#define module_ffa_driver(__ffa_driver)	\
	module_driver(__ffa_driver, ffa_register, ffa_unregister)

/* FFA transport related */
struct ffa_partition_info {
	u16 id;
	u16 exec_ctxt;
/* partition supports receipt of direct requests */
#define FFA_PARTITION_DIRECT_RECV	BIT(0)
/* partition can send direct requests. */
#define FFA_PARTITION_DIRECT_SEND	BIT(1)
/* partition can send and receive indirect messages. */
#define FFA_PARTITION_INDIRECT_MSG	BIT(2)
/* partition runs in the AArch64 execution state. */
#define FFA_PARTITION_AARCH64_EXEC	BIT(8)
	u32 properties;
	u32 uuid[4];
};

/* For use with FFA_MSG_SEND_DIRECT_{REQ,RESP} which pass data via registers */
struct ffa_send_direct_data {
	unsigned long data0; /* w3/x3 */
	unsigned long data1; /* w4/x4 */
	unsigned long data2; /* w5/x5 */
	unsigned long data3; /* w6/x6 */
	unsigned long data4; /* w7/x7 */
};

struct ffa_mem_region_addr_range {
	/* The base IPA of the constituent memory region, aligned to 4 kiB */
	u64 address;
	/* The number of 4 kiB pages in the constituent memory region. */
	u32 pg_cnt;
	u32 reserved;
};

struct ffa_composite_mem_region {
	/*
	 * The total number of 4 kiB pages included in this memory region. This
	 * must be equal to the sum of page counts specified in each
	 * `struct ffa_mem_region_addr_range`.
	 */
	u32 total_pg_cnt;
	/* The number of constituents included in this memory region range */
	u32 addr_range_cnt;
	u64 reserved;
	/** An array of `addr_range_cnt` memory region constituents. */
	struct ffa_mem_region_addr_range constituents[];
};

struct ffa_mem_region_attributes {
	/* The ID of the VM to which the memory is being given or shared. */
	u16 receiver;
	/*
	 * The permissions with which the memory region should be mapped in the
	 * receiver's page table.
	 */
#define FFA_MEM_EXEC		BIT(3)
#define FFA_MEM_NO_EXEC		BIT(2)
#define FFA_MEM_RW		BIT(1)
#define FFA_MEM_RO		BIT(0)
	u8 attrs;
	/*
	 * Flags used during FFA_MEM_RETRIEVE_REQ and FFA_MEM_RETRIEVE_RESP
	 * for memory regions with multiple borrowers.
	 */
#define FFA_MEM_RETRIEVE_SELF_BORROWER	BIT(0)
	u8 flag;
	/*
	 * Offset in bytes from the start of the outer `ffa_memory_region` to
	 * an `struct ffa_mem_region_addr_range`.
	 */
	u32 composite_off;
	u64 reserved;
};

struct ffa_mem_region {
	/* The ID of the VM/owner which originally sent the memory region */
	u16 sender_id;
#define FFA_MEM_NORMAL		BIT(5)
#define FFA_MEM_DEVICE		BIT(4)

#define FFA_MEM_WRITE_BACK	(3 << 2)
#define FFA_MEM_NON_CACHEABLE	(1 << 2)

#define FFA_DEV_nGnRnE		(0 << 2)
#define FFA_DEV_nGnRE		(1 << 2)
#define FFA_DEV_nGRE		(2 << 2)
#define FFA_DEV_GRE		(3 << 2)

#define FFA_MEM_NON_SHAREABLE	(0)
#define FFA_MEM_OUTER_SHAREABLE	(2)
#define FFA_MEM_INNER_SHAREABLE	(3)
	u8 attributes;
	u8 reserved_0;
/*
 * Clear memory region contents after unmapping it from the sender and
 * before mapping it for any receiver.
 */
#define FFA_MEM_CLEAR			BIT(0)
/*
 * Whether the hypervisor may time slice the memory sharing or retrieval
 * operation.
 */
#define FFA_TIME_SLICE_ENABLE		BIT(1)

#define FFA_MEM_RETRIEVE_TYPE_IN_RESP	(0 << 3)
#define FFA_MEM_RETRIEVE_TYPE_SHARE	(1 << 3)
#define FFA_MEM_RETRIEVE_TYPE_LEND	(2 << 3)
#define FFA_MEM_RETRIEVE_TYPE_DONATE	(3 << 3)

#define FFA_MEM_RETRIEVE_ADDR_ALIGN_HINT	BIT(9)
#define FFA_MEM_RETRIEVE_ADDR_ALIGN(x)		((x) << 5)
	/* Flags to control behaviour of the transaction. */
	u32 flags;
#define HANDLE_LOW_MASK		GENMASK_ULL(31, 0)
#define HANDLE_HIGH_MASK	GENMASK_ULL(63, 32)
#define HANDLE_LOW(x)		((u32)(FIELD_GET(HANDLE_LOW_MASK, (x))))
#define	HANDLE_HIGH(x)		((u32)(FIELD_GET(HANDLE_HIGH_MASK, (x))))

#define PACK_HANDLE(l, h)		\
	(FIELD_PREP(HANDLE_LOW_MASK, (l)) | FIELD_PREP(HANDLE_HIGH_MASK, (h)))
	/*
	 * A globally-unique ID assigned by the hypervisor for a region
	 * of memory being sent between VMs.
	 */
	u64 handle;
	/*
	 * An implementation defined value associated with the receiver and the
	 * memory region.
	 */
	u64 tag;
	u32 reserved_1;
	/*
	 * The number of `ffa_mem_region_attributes` entries included in this
	 * transaction.
	 */
	u32 ep_count;
	/*
	 * An array of endpoint memory access descriptors.
	 * Each one specifies a memory region offset, an endpoint and the
	 * attributes with which this memory region should be mapped in that
	 * endpoint's page table.
	 */
	struct ffa_mem_region_attributes ep_mem_access[];
};

#define	COMPOSITE_OFFSET(x)	\
	(offsetof(struct ffa_mem_region, ep_mem_access[x]))
#define CONSTITUENTS_OFFSET(x)	\
	(offsetof(struct ffa_composite_mem_region, constituents[x]))
#define COMPOSITE_CONSTITUENTS_OFFSET(x, y)	\
	(COMPOSITE_OFFSET(x) + CONSTITUENTS_OFFSET(y))

struct ffa_mem_ops_args {
	bool use_txbuf;
	u32 nattrs;
	u32 flags;
	u64 tag;
	u64 g_handle;
	struct scatterlist *sg;
	struct ffa_mem_region_attributes *attrs;
};

struct ffa_info_ops {
	u32 (*api_version_get)(void);
	int (*partition_info_get)(const char *uuid_str,
				  struct ffa_partition_info *buffer);
};

struct ffa_msg_ops {
	void (*mode_32bit_set)(struct ffa_device *dev);
	int (*sync_send_receive)(struct ffa_device *dev,
				 struct ffa_send_direct_data *data);
};

struct ffa_mem_ops {
	int (*memory_reclaim)(u64 g_handle, u32 flags);
	int (*memory_share)(struct ffa_mem_ops_args *args);
	int (*memory_lend)(struct ffa_mem_ops_args *args);
};

struct ffa_ops {
	const struct ffa_info_ops *info_ops;
	const struct ffa_msg_ops *msg_ops;
	const struct ffa_mem_ops *mem_ops;
};

#endif /* _LINUX_ARM_FFA_H */
