/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _UAPI_LINUX_MEM_BUF_H
#define _UAPI_LINUX_MEM_BUF_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define MEM_BUF_IOC_MAGIC 'M'

/**
 * enum mem_buf_mem_type: Types of memory that can be allocated from and to
 * @MEM_BUF_ION_MEM_TYPE: The memory for the source or destination is ION memory
 */
enum mem_buf_mem_type {
	MEM_BUF_ION_MEM_TYPE,
	MEM_BUF_MAX_MEM_TYPE,
};
#define MEM_BUF_DMAHEAP_MEM_TYPE (MEM_BUF_ION_MEM_TYPE + 1)

/* The mem-buf values that represent VMIDs for an ACL. */
#define MEM_BUF_VMID_PRIMARY_VM 0
#define	MEM_BUF_VMID_TRUSTED_VM 1

#define MEM_BUF_PERM_FLAG_READ (1U << 0)
#define MEM_BUF_PERM_FLAG_WRITE (1U << 1)
#define MEM_BUF_PERM_FLAG_EXEC (1U << 2)
#define MEM_BUF_PERM_VALID_FLAGS\
	(MEM_BUF_PERM_FLAG_READ | MEM_BUF_PERM_FLAG_WRITE |\
	 MEM_BUF_PERM_FLAG_EXEC)

#define MEM_BUF_MAX_NR_ACL_ENTS 16

/**
 * struct acl_entry: Represents the access control permissions for a VMID.
 * @vmid: The mem-buf VMID specifier associated with the VMID that will access
 * the memory.
 * @perms: The access permissions for the VMID in @vmid. This flag is
 * interpreted as a bitmap, and thus, should be a combination of one or more
 * of the MEM_BUF_PERM_FLAG_* flags.
 */
struct acl_entry {
	__u32 vmid;
	__u32 perms;
};

/**
 * struct mem_buf_ion_data: Data that is unique to memory that is of type
 * MEM_BUF_ION_MEM_TYPE.
 * @heap_id: The heap ID of where memory should be allocated from or added to.
 */
struct mem_buf_ion_data {
	__u32 heap_id;
};

#define MEM_BUF_MAX_DMAHEAP_NAME_LEN 128
/**
 * struct mem_buf_dmaheap_data: Data that is unique to memory that is of type
 * MEM_BUF_DMAHEAP_MEM_TYPE.
 * @heap_name: array of characters containing the heap name.
 */
struct mem_buf_dmaheap_data {
	__u64 heap_name;
};

/**
 * struct mem_buf_alloc_ioctl_arg: A request to allocate memory from another
 * VM to other VMs.
 * @size: The size of the allocation.
 * @acl_list: An array of structures, where each structure specifies a VMID
 * and the access permissions that the VMID will have to the memory to be
 * allocated.
 * @nr_acl_entries: The number of ACL entries in @acl_list.
 * @src_mem_type: The type of memory that the source VM should allocate from.
 * This should be one of the mem_buf_mem_type enum values.
 * @src_data: A pointer to data that the source VM should interpret when
 * performing the allocation.
 * @dst_mem_type: The type of memory that the destination VM should treat the
 * incoming allocation from the source VM as. This should be one of the
 * mem_buf_mem_type enum values.
 * @mem_buf_fd: A file descriptor representing the memory that was allocated
 * from the source VM and added to the current VM. Calling close() on this file
 * descriptor will deallocate the memory from the current VM, and return it
 * to the source VM.
 * * @dst_data: A pointer to data that the destination VM should interpret when
 * adding the memory to the current VM.
 *
 * All reserved fields must be zeroed out by the caller prior to invoking the
 * allocation IOCTL command with this argument.
 */
struct mem_buf_alloc_ioctl_arg {
	__u64 size;
	__u64 acl_list;
	__u32 nr_acl_entries;
	__u32 src_mem_type;
	__u64 src_data;
	__u32 dst_mem_type;
	__u32 mem_buf_fd;
	__u64 dst_data;
	__u64 reserved0;
	__u64 reserved1;
	__u64 reserved2;
};

#define MEM_BUF_IOC_ALLOC		_IOWR(MEM_BUF_IOC_MAGIC, 0,\
					      struct mem_buf_alloc_ioctl_arg)

/**
 * struct mem_buf_lend_ioctl_arg: A request to lend memory from the local VM
 * VM to one or more remote VMs.
 * @dma_buf_fd: The fd of the dma-buf that will be exported to another VM.
 * @nr_acl_entries: The number of ACL entries in @acl_list.
 * @acl_list: An array of structures, where each structure specifies a VMID
 * and the access permissions that the VMID will have to the memory to be
 * exported. Must not include the local VMID.
 * @memparcel_hdl: The handle associated with the memparcel that was created by
 * granting access to the dma-buf for the VMIDs specified in @acl_list.
 *
 * All reserved fields must be zeroed out by the caller prior to invoking the
 * import IOCTL command with this argument.
 */
struct mem_buf_lend_ioctl_arg {
	__u32 dma_buf_fd;
	__u32 nr_acl_entries;
	__u64 acl_list;
	__u64 memparcel_hdl;
	__u64 reserved0;
	__u64 reserved1;
	__u64 reserved2;
};

#define MEM_BUF_IOC_LEND		_IOWR(MEM_BUF_IOC_MAGIC, 3,\
					      struct mem_buf_lend_ioctl_arg)

#define MEM_BUF_VALID_FD_FLAGS (O_CLOEXEC | O_ACCMODE)
/**
 * struct mem_buf_retrieve_ioctl_arg: A request to retrieve memory from another
 * VM as a dma-buf
 * @sender_vm_fd: An open file descriptor identifing the VM who sent the handle.
 * @nr_acl_entries: The number of ACL entries in @acl_list.
 * @acl_list: An array of structures, where each structure specifies a VMID
 * and the access permissions that the VMID should have for the memparcel.
 * @memparcel_hdl: The handle that corresponds to the memparcel we are
 * importing.
 * @dma_buf_import_fd: A dma-buf file descriptor that the client can use to
 * access the buffer. This fd must be closed to release the memory.
 * @fd_flags:		file descriptor flags used when allocating
 *
 * All reserved fields must be zeroed out by the caller prior to invoking the
 * import IOCTL command with this argument.
 */
struct mem_buf_retrieve_ioctl_arg {
	__u32 sender_vm_fd;
	__u32 nr_acl_entries;
	__u64 acl_list;
	__u64 memparcel_hdl;
	__u32 dma_buf_import_fd;
	__u32 fd_flags;
	__u64 reserved0;
	__u64 reserved1;
	__u64 reserved2;
};

#define MEM_BUF_IOC_RETRIEVE		_IOWR(MEM_BUF_IOC_MAGIC, 4,\
					      struct mem_buf_retrieve_ioctl_arg)

/**
 * struct mem_buf_reclaim_ioctl_arg: A request to reclaim memory from another
 * VM. The other VM must have relinquished access, and the current VM must be
 * the original owner of the memory. The dma-buf file will not be closed by
 * this operation.
 * @memparcel_hdl: The handle that corresponds to the memparcel we are
 * reclaiming.
 * @dma_buf_fd: A dma-buf file descriptor that the client can use to
 * access the buffer.
 *
 * All reserved fields must be zeroed out by the caller prior to invoking the
 * import IOCTL command with this argument.
 */
struct mem_buf_reclaim_ioctl_arg {
	__u64 memparcel_hdl;
	__u32 dma_buf_fd;
	__u32 reserved0;
	__u64 reserved1;
	__u64 reserved2;
};

#define MEM_BUF_IOC_RECLAIM		_IOWR(MEM_BUF_IOC_MAGIC, 3,\
					      struct mem_buf_reclaim_ioctl_arg)

/**
 * struct mem_buf_share_ioctl_arg: An request to share memory between the
 * local VM and one or more remote VMs.
 * @dma_buf_fd: The fd of the dma-buf that will be exported to another VM.
 * @nr_acl_entries: The number of ACL entries in @acl_list.
 * @acl_list: An array of structures, where each structure specifies a VMID
 * and the access permissions that the VMID will have to the memory to be
 * exported. Must include the local VMID.
 * @memparcel_hdl: The handle associated with the memparcel that was created by
 * granting access to the dma-buf for the VMIDs specified in @acl_list.
 *
 * All reserved fields must be zeroed out by the caller prior to invoking the
 * import IOCTL command with this argument.
 */
struct mem_buf_share_ioctl_arg {
	__u32 dma_buf_fd;
	__u32 nr_acl_entries;
	__u64 acl_list;
	__u64 memparcel_hdl;
	__u64 reserved0;
	__u64 reserved1;
	__u64 reserved2;
};

#define MEM_BUF_IOC_SHARE		_IOWR(MEM_BUF_IOC_MAGIC, 6,\
					      struct mem_buf_share_ioctl_arg)

/**
 * struct mem_buf_exclusive_owner_ioctl_arg: A request to see if a DMA-BUF
 * is owned by and belongs exclusively to this VM.
 * @dma_buf_fd: The fd of the dma-buf the user wants to obtain information on
 * @is_exclusive_owner:
 */
struct mem_buf_exclusive_owner_ioctl_arg {
	__u32 dma_buf_fd;
	__u32 is_exclusive_owner;
};

#define MEM_BUF_IOC_EXCLUSIVE_OWNER	_IOWR(MEM_BUF_IOC_MAGIC, 2,\
					      struct mem_buf_exclusive_owner_ioctl_arg)

#endif /* _UAPI_LINUX_MEM_BUF_H */
