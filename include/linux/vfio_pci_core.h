/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * Derived from original vfio:
 * Copyright 2010 Cisco Systems, Inc.  All rights reserved.
 * Author: Tom Lyon, pugs@cisco.com
 */

#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/vfio.h>
#include <linux/irqbypass.h>
#include <linux/rcupdate.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <linux/notifier.h>

#ifndef VFIO_PCI_CORE_H
#define VFIO_PCI_CORE_H

#define VFIO_PCI_OFFSET_SHIFT   40
#define VFIO_PCI_OFFSET_TO_INDEX(off)	(off >> VFIO_PCI_OFFSET_SHIFT)
#define VFIO_PCI_INDEX_TO_OFFSET(index)	((u64)(index) << VFIO_PCI_OFFSET_SHIFT)
#define VFIO_PCI_OFFSET_MASK	(((u64)(1) << VFIO_PCI_OFFSET_SHIFT) - 1)

struct vfio_pci_core_device;
struct vfio_pci_region;
struct p2pdma_provider;
struct dma_buf_phys_vec;
struct dma_buf_attachment;

struct vfio_pci_eventfd {
	struct eventfd_ctx	*ctx;
	struct rcu_head		rcu;
};

struct vfio_pci_regops {
	ssize_t (*rw)(struct vfio_pci_core_device *vdev, char __user *buf,
		      size_t count, loff_t *ppos, bool iswrite);
	void	(*release)(struct vfio_pci_core_device *vdev,
			   struct vfio_pci_region *region);
	int	(*mmap)(struct vfio_pci_core_device *vdev,
			struct vfio_pci_region *region,
			struct vm_area_struct *vma);
	int	(*add_capability)(struct vfio_pci_core_device *vdev,
				  struct vfio_pci_region *region,
				  struct vfio_info_cap *caps);
};

struct vfio_pci_region {
	u32				type;
	u32				subtype;
	const struct vfio_pci_regops	*ops;
	void				*data;
	size_t				size;
	u32				flags;
};

struct vfio_pci_device_ops {
	int (*get_dmabuf_phys)(struct vfio_pci_core_device *vdev,
			       struct p2pdma_provider **provider,
			       unsigned int region_index,
			       struct dma_buf_phys_vec *phys_vec,
			       struct vfio_region_dma_range *dma_ranges,
			       size_t nr_ranges);
};

#if IS_ENABLED(CONFIG_VFIO_PCI_DMABUF)
int vfio_pci_core_fill_phys_vec(struct dma_buf_phys_vec *phys_vec,
				struct vfio_region_dma_range *dma_ranges,
				size_t nr_ranges, phys_addr_t start,
				phys_addr_t len);
int vfio_pci_core_get_dmabuf_phys(struct vfio_pci_core_device *vdev,
				  struct p2pdma_provider **provider,
				  unsigned int region_index,
				  struct dma_buf_phys_vec *phys_vec,
				  struct vfio_region_dma_range *dma_ranges,
				  size_t nr_ranges);
#else
static inline int
vfio_pci_core_fill_phys_vec(struct dma_buf_phys_vec *phys_vec,
			    struct vfio_region_dma_range *dma_ranges,
			    size_t nr_ranges, phys_addr_t start,
			    phys_addr_t len)
{
	return -EINVAL;
}
static inline int vfio_pci_core_get_dmabuf_phys(
	struct vfio_pci_core_device *vdev, struct p2pdma_provider **provider,
	unsigned int region_index, struct dma_buf_phys_vec *phys_vec,
	struct vfio_region_dma_range *dma_ranges, size_t nr_ranges)
{
	return -EOPNOTSUPP;
}
#endif

struct vfio_pci_core_device {
	struct vfio_device	vdev;
	struct pci_dev		*pdev;
	const struct vfio_pci_device_ops *pci_ops;
	void __iomem		*barmap[PCI_STD_NUM_BARS];
	bool			bar_mmap_supported[PCI_STD_NUM_BARS];
	u8			*pci_config_map;
	u8			*vconfig;
	struct perm_bits	*msi_perm;
	spinlock_t		irqlock;
	struct mutex		igate;
	struct xarray		ctx;
	int			irq_type;
	int			num_regions;
	struct vfio_pci_region	*region;
	u8			msi_qmax;
	u8			msix_bar;
	u16			msix_size;
	u32			msix_offset;
	u32			rbar[7];
	bool			has_dyn_msix:1;
	bool			pci_2_3:1;
	bool			virq_disabled:1;
	bool			reset_works:1;
	bool			extended_caps:1;
	bool			bardirty:1;
	bool			has_vga:1;
	bool			needs_reset:1;
	bool			nointx:1;
	bool			needs_pm_restore:1;
	bool			pm_intx_masked:1;
	bool			pm_runtime_engaged:1;
	struct pci_saved_state	*pci_saved_state;
	struct pci_saved_state	*pm_save;
	int			ioeventfds_nr;
	struct vfio_pci_eventfd __rcu *err_trigger;
	struct vfio_pci_eventfd __rcu *req_trigger;
	struct eventfd_ctx	*pm_wake_eventfd_ctx;
	struct list_head	dummy_resources_list;
	struct mutex		ioeventfds_lock;
	struct list_head	ioeventfds_list;
	struct vfio_pci_vf_token	*vf_token;
	struct list_head		sriov_pfs_item;
	struct vfio_pci_core_device	*sriov_pf_core_dev;
	struct notifier_block	nb;
	struct rw_semaphore	memory_lock;
	struct list_head	dmabufs;
};

enum vfio_pci_io_width {
	VFIO_PCI_IO_WIDTH_1 = 1,
	VFIO_PCI_IO_WIDTH_2 = 2,
	VFIO_PCI_IO_WIDTH_4 = 4,
	VFIO_PCI_IO_WIDTH_8 = 8,
};

/* Will be exported for vfio pci drivers usage */
int vfio_pci_core_register_dev_region(struct vfio_pci_core_device *vdev,
				      unsigned int type, unsigned int subtype,
				      const struct vfio_pci_regops *ops,
				      size_t size, u32 flags, void *data);
void vfio_pci_core_set_params(bool nointxmask, bool is_disable_vga,
			      bool is_disable_idle_d3);
void vfio_pci_core_close_device(struct vfio_device *core_vdev);
int vfio_pci_core_init_dev(struct vfio_device *core_vdev);
void vfio_pci_core_release_dev(struct vfio_device *core_vdev);
int vfio_pci_core_register_device(struct vfio_pci_core_device *vdev);
void vfio_pci_core_unregister_device(struct vfio_pci_core_device *vdev);
extern const struct pci_error_handlers vfio_pci_core_err_handlers;
int vfio_pci_core_sriov_configure(struct vfio_pci_core_device *vdev,
				  int nr_virtfn);
long vfio_pci_core_ioctl(struct vfio_device *core_vdev, unsigned int cmd,
		unsigned long arg);
int vfio_pci_core_ioctl_feature(struct vfio_device *device, u32 flags,
				void __user *arg, size_t argsz);
int vfio_pci_ioctl_get_region_info(struct vfio_device *core_vdev,
				   struct vfio_region_info *info,
				   struct vfio_info_cap *caps);
ssize_t vfio_pci_core_read(struct vfio_device *core_vdev, char __user *buf,
		size_t count, loff_t *ppos);
ssize_t vfio_pci_core_write(struct vfio_device *core_vdev, const char __user *buf,
		size_t count, loff_t *ppos);
vm_fault_t vfio_pci_vmf_insert_pfn(struct vfio_pci_core_device *vdev,
				   struct vm_fault *vmf, unsigned long pfn,
				   unsigned int order);
int vfio_pci_core_mmap(struct vfio_device *core_vdev, struct vm_area_struct *vma);
void vfio_pci_core_request(struct vfio_device *core_vdev, unsigned int count);
int vfio_pci_core_match(struct vfio_device *core_vdev, char *buf);
int vfio_pci_core_match_token_uuid(struct vfio_device *core_vdev,
				   const uuid_t *uuid);
int vfio_pci_core_enable(struct vfio_pci_core_device *vdev);
void vfio_pci_core_disable(struct vfio_pci_core_device *vdev);
void vfio_pci_core_finish_enable(struct vfio_pci_core_device *vdev);
int vfio_pci_core_setup_barmap(struct vfio_pci_core_device *vdev, int bar);
pci_ers_result_t vfio_pci_core_aer_err_detected(struct pci_dev *pdev,
						pci_channel_state_t state);
ssize_t vfio_pci_core_do_io_rw(struct vfio_pci_core_device *vdev, bool test_mem,
			       void __iomem *io, char __user *buf,
			       loff_t off, size_t count, size_t x_start,
			       size_t x_end, bool iswrite,
			       enum vfio_pci_io_width max_width);
bool __vfio_pci_memory_enabled(struct vfio_pci_core_device *vdev);
bool vfio_pci_core_range_intersect_range(loff_t buf_start, size_t buf_cnt,
					 loff_t reg_start, size_t reg_cnt,
					 loff_t *buf_offset,
					 size_t *intersect_count,
					 size_t *register_offset);
#define VFIO_IOWRITE_DECLARATION(size) \
int vfio_pci_core_iowrite##size(struct vfio_pci_core_device *vdev,	\
			bool test_mem, u##size val, void __iomem *io);

VFIO_IOWRITE_DECLARATION(8)
VFIO_IOWRITE_DECLARATION(16)
VFIO_IOWRITE_DECLARATION(32)
#ifdef iowrite64
VFIO_IOWRITE_DECLARATION(64)
#endif

#define VFIO_IOREAD_DECLARATION(size) \
int vfio_pci_core_ioread##size(struct vfio_pci_core_device *vdev,	\
			bool test_mem, u##size *val, void __iomem *io);

VFIO_IOREAD_DECLARATION(8)
VFIO_IOREAD_DECLARATION(16)
VFIO_IOREAD_DECLARATION(32)
#ifdef ioread64
VFIO_IOREAD_DECLARATION(64)
#endif

static inline bool is_aligned_for_order(struct vm_area_struct *vma,
					unsigned long addr,
					unsigned long pfn,
					unsigned int order)
{
	return !(order && (addr < vma->vm_start ||
			   addr + (PAGE_SIZE << order) > vma->vm_end ||
			   !IS_ALIGNED(pfn, 1 << order)));
}

int vfio_pci_dma_buf_iommufd_map(struct dma_buf_attachment *attachment,
				 struct dma_buf_phys_vec *phys);

#endif /* VFIO_PCI_CORE_H */
