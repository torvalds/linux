/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * VFIO API definition
 *
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _UAPIVFIO_H
#define _UAPIVFIO_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define VFIO_API_VERSION	0


/* Kernel & User level defines for VFIO IOCTLs. */

/* Extensions */

#define VFIO_TYPE1_IOMMU		1
#define VFIO_SPAPR_TCE_IOMMU		2
#define VFIO_TYPE1v2_IOMMU		3
/*
 * IOMMU enforces DMA cache coherence (ex. PCIe NoSnoop stripping).  This
 * capability is subject to change as groups are added or removed.
 */
#define VFIO_DMA_CC_IOMMU		4

/* Check if EEH is supported */
#define VFIO_EEH			5

/* Two-stage IOMMU */
#define VFIO_TYPE1_NESTING_IOMMU	6	/* Implies v2 */

#define VFIO_SPAPR_TCE_v2_IOMMU		7

/*
 * The No-IOMMU IOMMU offers no translation or isolation for devices and
 * supports no ioctls outside of VFIO_CHECK_EXTENSION.  Use of VFIO's No-IOMMU
 * code will taint the host kernel and should be used with extreme caution.
 */
#define VFIO_NOIOMMU_IOMMU		8

/* Supports VFIO_DMA_UNMAP_FLAG_ALL */
#define VFIO_UNMAP_ALL			9

/*
 * Supports the vaddr flag for DMA map and unmap.  Not supported for mediated
 * devices, so this capability is subject to change as groups are added or
 * removed.
 */
#define VFIO_UPDATE_VADDR		10

/*
 * The IOCTL interface is designed for extensibility by embedding the
 * structure length (argsz) and flags into structures passed between
 * kernel and userspace.  We therefore use the _IO() macro for these
 * defines to avoid implicitly embedding a size into the ioctl request.
 * As structure fields are added, argsz will increase to match and flag
 * bits will be defined to indicate additional fields with valid data.
 * It's *always* the caller's responsibility to indicate the size of
 * the structure passed by setting argsz appropriately.
 */

#define VFIO_TYPE	(';')
#define VFIO_BASE	100

/*
 * For extension of INFO ioctls, VFIO makes use of a capability chain
 * designed after PCI/e capabilities.  A flag bit indicates whether
 * this capability chain is supported and a field defined in the fixed
 * structure defines the offset of the first capability in the chain.
 * This field is only valid when the corresponding bit in the flags
 * bitmap is set.  This offset field is relative to the start of the
 * INFO buffer, as is the next field within each capability header.
 * The id within the header is a shared address space per INFO ioctl,
 * while the version field is specific to the capability id.  The
 * contents following the header are specific to the capability id.
 */
struct vfio_info_cap_header {
	__u16	id;		/* Identifies capability */
	__u16	version;	/* Version specific to the capability ID */
	__u32	next;		/* Offset of next capability */
};

/*
 * Callers of INFO ioctls passing insufficiently sized buffers will see
 * the capability chain flag bit set, a zero value for the first capability
 * offset (if available within the provided argsz), and argsz will be
 * updated to report the necessary buffer size.  For compatibility, the
 * INFO ioctl will not report error in this case, but the capability chain
 * will not be available.
 */

/* -------- IOCTLs for VFIO file descriptor (/dev/vfio/vfio) -------- */

/**
 * VFIO_GET_API_VERSION - _IO(VFIO_TYPE, VFIO_BASE + 0)
 *
 * Report the version of the VFIO API.  This allows us to bump the entire
 * API version should we later need to add or change features in incompatible
 * ways.
 * Return: VFIO_API_VERSION
 * Availability: Always
 */
#define VFIO_GET_API_VERSION		_IO(VFIO_TYPE, VFIO_BASE + 0)

/**
 * VFIO_CHECK_EXTENSION - _IOW(VFIO_TYPE, VFIO_BASE + 1, __u32)
 *
 * Check whether an extension is supported.
 * Return: 0 if not supported, 1 (or some other positive integer) if supported.
 * Availability: Always
 */
#define VFIO_CHECK_EXTENSION		_IO(VFIO_TYPE, VFIO_BASE + 1)

/**
 * VFIO_SET_IOMMU - _IOW(VFIO_TYPE, VFIO_BASE + 2, __s32)
 *
 * Set the iommu to the given type.  The type must be supported by an
 * iommu driver as verified by calling CHECK_EXTENSION using the same
 * type.  A group must be set to this file descriptor before this
 * ioctl is available.  The IOMMU interfaces enabled by this call are
 * specific to the value set.
 * Return: 0 on success, -errno on failure
 * Availability: When VFIO group attached
 */
#define VFIO_SET_IOMMU			_IO(VFIO_TYPE, VFIO_BASE + 2)

/* -------- IOCTLs for GROUP file descriptors (/dev/vfio/$GROUP) -------- */

/**
 * VFIO_GROUP_GET_STATUS - _IOR(VFIO_TYPE, VFIO_BASE + 3,
 *						struct vfio_group_status)
 *
 * Retrieve information about the group.  Fills in provided
 * struct vfio_group_info.  Caller sets argsz.
 * Return: 0 on succes, -errno on failure.
 * Availability: Always
 */
struct vfio_group_status {
	__u32	argsz;
	__u32	flags;
#define VFIO_GROUP_FLAGS_VIABLE		(1 << 0)
#define VFIO_GROUP_FLAGS_CONTAINER_SET	(1 << 1)
};
#define VFIO_GROUP_GET_STATUS		_IO(VFIO_TYPE, VFIO_BASE + 3)

/**
 * VFIO_GROUP_SET_CONTAINER - _IOW(VFIO_TYPE, VFIO_BASE + 4, __s32)
 *
 * Set the container for the VFIO group to the open VFIO file
 * descriptor provided.  Groups may only belong to a single
 * container.  Containers may, at their discretion, support multiple
 * groups.  Only when a container is set are all of the interfaces
 * of the VFIO file descriptor and the VFIO group file descriptor
 * available to the user.
 * Return: 0 on success, -errno on failure.
 * Availability: Always
 */
#define VFIO_GROUP_SET_CONTAINER	_IO(VFIO_TYPE, VFIO_BASE + 4)

/**
 * VFIO_GROUP_UNSET_CONTAINER - _IO(VFIO_TYPE, VFIO_BASE + 5)
 *
 * Remove the group from the attached container.  This is the
 * opposite of the SET_CONTAINER call and returns the group to
 * an initial state.  All device file descriptors must be released
 * prior to calling this interface.  When removing the last group
 * from a container, the IOMMU will be disabled and all state lost,
 * effectively also returning the VFIO file descriptor to an initial
 * state.
 * Return: 0 on success, -errno on failure.
 * Availability: When attached to container
 */
#define VFIO_GROUP_UNSET_CONTAINER	_IO(VFIO_TYPE, VFIO_BASE + 5)

/**
 * VFIO_GROUP_GET_DEVICE_FD - _IOW(VFIO_TYPE, VFIO_BASE + 6, char)
 *
 * Return a new file descriptor for the device object described by
 * the provided string.  The string should match a device listed in
 * the devices subdirectory of the IOMMU group sysfs entry.  The
 * group containing the device must already be added to this context.
 * Return: new file descriptor on success, -errno on failure.
 * Availability: When attached to container
 */
#define VFIO_GROUP_GET_DEVICE_FD	_IO(VFIO_TYPE, VFIO_BASE + 6)

/* --------------- IOCTLs for DEVICE file descriptors --------------- */

/**
 * VFIO_DEVICE_GET_INFO - _IOR(VFIO_TYPE, VFIO_BASE + 7,
 *						struct vfio_device_info)
 *
 * Retrieve information about the device.  Fills in provided
 * struct vfio_device_info.  Caller sets argsz.
 * Return: 0 on success, -errno on failure.
 */
struct vfio_device_info {
	__u32	argsz;
	__u32	flags;
#define VFIO_DEVICE_FLAGS_RESET	(1 << 0)	/* Device supports reset */
#define VFIO_DEVICE_FLAGS_PCI	(1 << 1)	/* vfio-pci device */
#define VFIO_DEVICE_FLAGS_PLATFORM (1 << 2)	/* vfio-platform device */
#define VFIO_DEVICE_FLAGS_AMBA  (1 << 3)	/* vfio-amba device */
#define VFIO_DEVICE_FLAGS_CCW	(1 << 4)	/* vfio-ccw device */
#define VFIO_DEVICE_FLAGS_AP	(1 << 5)	/* vfio-ap device */
#define VFIO_DEVICE_FLAGS_FSL_MC (1 << 6)	/* vfio-fsl-mc device */
#define VFIO_DEVICE_FLAGS_CAPS	(1 << 7)	/* Info supports caps */
#define VFIO_DEVICE_FLAGS_CDX	(1 << 8)	/* vfio-cdx device */
	__u32	num_regions;	/* Max region index + 1 */
	__u32	num_irqs;	/* Max IRQ index + 1 */
	__u32   cap_offset;	/* Offset within info struct of first cap */
};
#define VFIO_DEVICE_GET_INFO		_IO(VFIO_TYPE, VFIO_BASE + 7)

/*
 * Vendor driver using Mediated device framework should provide device_api
 * attribute in supported type attribute groups. Device API string should be one
 * of the following corresponding to device flags in vfio_device_info structure.
 */

#define VFIO_DEVICE_API_PCI_STRING		"vfio-pci"
#define VFIO_DEVICE_API_PLATFORM_STRING		"vfio-platform"
#define VFIO_DEVICE_API_AMBA_STRING		"vfio-amba"
#define VFIO_DEVICE_API_CCW_STRING		"vfio-ccw"
#define VFIO_DEVICE_API_AP_STRING		"vfio-ap"

/*
 * The following capabilities are unique to s390 zPCI devices.  Their contents
 * are further-defined in vfio_zdev.h
 */
#define VFIO_DEVICE_INFO_CAP_ZPCI_BASE		1
#define VFIO_DEVICE_INFO_CAP_ZPCI_GROUP		2
#define VFIO_DEVICE_INFO_CAP_ZPCI_UTIL		3
#define VFIO_DEVICE_INFO_CAP_ZPCI_PFIP		4

/*
 * The following VFIO_DEVICE_INFO capability reports support for PCIe AtomicOp
 * completion to the root bus with supported widths provided via flags.
 */
#define VFIO_DEVICE_INFO_CAP_PCI_ATOMIC_COMP	5
struct vfio_device_info_cap_pci_atomic_comp {
	struct vfio_info_cap_header header;
	__u32 flags;
#define VFIO_PCI_ATOMIC_COMP32	(1 << 0)
#define VFIO_PCI_ATOMIC_COMP64	(1 << 1)
#define VFIO_PCI_ATOMIC_COMP128	(1 << 2)
	__u32 reserved;
};

/**
 * VFIO_DEVICE_GET_REGION_INFO - _IOWR(VFIO_TYPE, VFIO_BASE + 8,
 *				       struct vfio_region_info)
 *
 * Retrieve information about a device region.  Caller provides
 * struct vfio_region_info with index value set.  Caller sets argsz.
 * Implementation of region mapping is bus driver specific.  This is
 * intended to describe MMIO, I/O port, as well as bus specific
 * regions (ex. PCI config space).  Zero sized regions may be used
 * to describe unimplemented regions (ex. unimplemented PCI BARs).
 * Return: 0 on success, -errno on failure.
 */
struct vfio_region_info {
	__u32	argsz;
	__u32	flags;
#define VFIO_REGION_INFO_FLAG_READ	(1 << 0) /* Region supports read */
#define VFIO_REGION_INFO_FLAG_WRITE	(1 << 1) /* Region supports write */
#define VFIO_REGION_INFO_FLAG_MMAP	(1 << 2) /* Region supports mmap */
#define VFIO_REGION_INFO_FLAG_CAPS	(1 << 3) /* Info supports caps */
	__u32	index;		/* Region index */
	__u32	cap_offset;	/* Offset within info struct of first cap */
	__u64	size;		/* Region size (bytes) */
	__u64	offset;		/* Region offset from start of device fd */
};
#define VFIO_DEVICE_GET_REGION_INFO	_IO(VFIO_TYPE, VFIO_BASE + 8)

/*
 * The sparse mmap capability allows finer granularity of specifying areas
 * within a region with mmap support.  When specified, the user should only
 * mmap the offset ranges specified by the areas array.  mmaps outside of the
 * areas specified may fail (such as the range covering a PCI MSI-X table) or
 * may result in improper device behavior.
 *
 * The structures below define version 1 of this capability.
 */
#define VFIO_REGION_INFO_CAP_SPARSE_MMAP	1

struct vfio_region_sparse_mmap_area {
	__u64	offset;	/* Offset of mmap'able area within region */
	__u64	size;	/* Size of mmap'able area */
};

struct vfio_region_info_cap_sparse_mmap {
	struct vfio_info_cap_header header;
	__u32	nr_areas;
	__u32	reserved;
	struct vfio_region_sparse_mmap_area areas[];
};

/*
 * The device specific type capability allows regions unique to a specific
 * device or class of devices to be exposed.  This helps solve the problem for
 * vfio bus drivers of defining which region indexes correspond to which region
 * on the device, without needing to resort to static indexes, as done by
 * vfio-pci.  For instance, if we were to go back in time, we might remove
 * VFIO_PCI_VGA_REGION_INDEX and let vfio-pci simply define that all indexes
 * greater than or equal to VFIO_PCI_NUM_REGIONS are device specific and we'd
 * make a "VGA" device specific type to describe the VGA access space.  This
 * means that non-VGA devices wouldn't need to waste this index, and thus the
 * address space associated with it due to implementation of device file
 * descriptor offsets in vfio-pci.
 *
 * The current implementation is now part of the user ABI, so we can't use this
 * for VGA, but there are other upcoming use cases, such as opregions for Intel
 * IGD devices and framebuffers for vGPU devices.  We missed VGA, but we'll
 * use this for future additions.
 *
 * The structure below defines version 1 of this capability.
 */
#define VFIO_REGION_INFO_CAP_TYPE	2

struct vfio_region_info_cap_type {
	struct vfio_info_cap_header header;
	__u32 type;	/* global per bus driver */
	__u32 subtype;	/* type specific */
};

/*
 * List of region types, global per bus driver.
 * If you introduce a new type, please add it here.
 */

/* PCI region type containing a PCI vendor part */
#define VFIO_REGION_TYPE_PCI_VENDOR_TYPE	(1 << 31)
#define VFIO_REGION_TYPE_PCI_VENDOR_MASK	(0xffff)
#define VFIO_REGION_TYPE_GFX                    (1)
#define VFIO_REGION_TYPE_CCW			(2)
#define VFIO_REGION_TYPE_MIGRATION_DEPRECATED   (3)

/* sub-types for VFIO_REGION_TYPE_PCI_* */

/* 8086 vendor PCI sub-types */
#define VFIO_REGION_SUBTYPE_INTEL_IGD_OPREGION	(1)
#define VFIO_REGION_SUBTYPE_INTEL_IGD_HOST_CFG	(2)
#define VFIO_REGION_SUBTYPE_INTEL_IGD_LPC_CFG	(3)

/* 10de vendor PCI sub-types */
/*
 * NVIDIA GPU NVlink2 RAM is coherent RAM mapped onto the host address space.
 *
 * Deprecated, region no longer provided
 */
#define VFIO_REGION_SUBTYPE_NVIDIA_NVLINK2_RAM	(1)

/* 1014 vendor PCI sub-types */
/*
 * IBM NPU NVlink2 ATSD (Address Translation Shootdown) register of NPU
 * to do TLB invalidation on a GPU.
 *
 * Deprecated, region no longer provided
 */
#define VFIO_REGION_SUBTYPE_IBM_NVLINK2_ATSD	(1)

/* sub-types for VFIO_REGION_TYPE_GFX */
#define VFIO_REGION_SUBTYPE_GFX_EDID            (1)

/**
 * struct vfio_region_gfx_edid - EDID region layout.
 *
 * Set display link state and EDID blob.
 *
 * The EDID blob has monitor information such as brand, name, serial
 * number, physical size, supported video modes and more.
 *
 * This special region allows userspace (typically qemu) set a virtual
 * EDID for the virtual monitor, which allows a flexible display
 * configuration.
 *
 * For the edid blob spec look here:
 *    https://en.wikipedia.org/wiki/Extended_Display_Identification_Data
 *
 * On linux systems you can find the EDID blob in sysfs:
 *    /sys/class/drm/${card}/${connector}/edid
 *
 * You can use the edid-decode ulility (comes with xorg-x11-utils) to
 * decode the EDID blob.
 *
 * @edid_offset: location of the edid blob, relative to the
 *               start of the region (readonly).
 * @edid_max_size: max size of the edid blob (readonly).
 * @edid_size: actual edid size (read/write).
 * @link_state: display link state (read/write).
 * VFIO_DEVICE_GFX_LINK_STATE_UP: Monitor is turned on.
 * VFIO_DEVICE_GFX_LINK_STATE_DOWN: Monitor is turned off.
 * @max_xres: max display width (0 == no limitation, readonly).
 * @max_yres: max display height (0 == no limitation, readonly).
 *
 * EDID update protocol:
 *   (1) set link-state to down.
 *   (2) update edid blob and size.
 *   (3) set link-state to up.
 */
struct vfio_region_gfx_edid {
	__u32 edid_offset;
	__u32 edid_max_size;
	__u32 edid_size;
	__u32 max_xres;
	__u32 max_yres;
	__u32 link_state;
#define VFIO_DEVICE_GFX_LINK_STATE_UP    1
#define VFIO_DEVICE_GFX_LINK_STATE_DOWN  2
};

/* sub-types for VFIO_REGION_TYPE_CCW */
#define VFIO_REGION_SUBTYPE_CCW_ASYNC_CMD	(1)
#define VFIO_REGION_SUBTYPE_CCW_SCHIB		(2)
#define VFIO_REGION_SUBTYPE_CCW_CRW		(3)

/* sub-types for VFIO_REGION_TYPE_MIGRATION */
#define VFIO_REGION_SUBTYPE_MIGRATION_DEPRECATED (1)

struct vfio_device_migration_info {
	__u32 device_state;         /* VFIO device state */
#define VFIO_DEVICE_STATE_V1_STOP      (0)
#define VFIO_DEVICE_STATE_V1_RUNNING   (1 << 0)
#define VFIO_DEVICE_STATE_V1_SAVING    (1 << 1)
#define VFIO_DEVICE_STATE_V1_RESUMING  (1 << 2)
#define VFIO_DEVICE_STATE_MASK      (VFIO_DEVICE_STATE_V1_RUNNING | \
				     VFIO_DEVICE_STATE_V1_SAVING |  \
				     VFIO_DEVICE_STATE_V1_RESUMING)

#define VFIO_DEVICE_STATE_VALID(state) \
	(state & VFIO_DEVICE_STATE_V1_RESUMING ? \
	(state & VFIO_DEVICE_STATE_MASK) == VFIO_DEVICE_STATE_V1_RESUMING : 1)

#define VFIO_DEVICE_STATE_IS_ERROR(state) \
	((state & VFIO_DEVICE_STATE_MASK) == (VFIO_DEVICE_STATE_V1_SAVING | \
					      VFIO_DEVICE_STATE_V1_RESUMING))

#define VFIO_DEVICE_STATE_SET_ERROR(state) \
	((state & ~VFIO_DEVICE_STATE_MASK) | VFIO_DEVICE_STATE_V1_SAVING | \
					     VFIO_DEVICE_STATE_V1_RESUMING)

	__u32 reserved;
	__u64 pending_bytes;
	__u64 data_offset;
	__u64 data_size;
};

/*
 * The MSIX mappable capability informs that MSIX data of a BAR can be mmapped
 * which allows direct access to non-MSIX registers which happened to be within
 * the same system page.
 *
 * Even though the userspace gets direct access to the MSIX data, the existing
 * VFIO_DEVICE_SET_IRQS interface must still be used for MSIX configuration.
 */
#define VFIO_REGION_INFO_CAP_MSIX_MAPPABLE	3

/*
 * Capability with compressed real address (aka SSA - small system address)
 * where GPU RAM is mapped on a system bus. Used by a GPU for DMA routing
 * and by the userspace to associate a NVLink bridge with a GPU.
 *
 * Deprecated, capability no longer provided
 */
#define VFIO_REGION_INFO_CAP_NVLINK2_SSATGT	4

struct vfio_region_info_cap_nvlink2_ssatgt {
	struct vfio_info_cap_header header;
	__u64 tgt;
};

/*
 * Capability with an NVLink link speed. The value is read by
 * the NVlink2 bridge driver from the bridge's "ibm,nvlink-speed"
 * property in the device tree. The value is fixed in the hardware
 * and failing to provide the correct value results in the link
 * not working with no indication from the driver why.
 *
 * Deprecated, capability no longer provided
 */
#define VFIO_REGION_INFO_CAP_NVLINK2_LNKSPD	5

struct vfio_region_info_cap_nvlink2_lnkspd {
	struct vfio_info_cap_header header;
	__u32 link_speed;
	__u32 __pad;
};

/**
 * VFIO_DEVICE_GET_IRQ_INFO - _IOWR(VFIO_TYPE, VFIO_BASE + 9,
 *				    struct vfio_irq_info)
 *
 * Retrieve information about a device IRQ.  Caller provides
 * struct vfio_irq_info with index value set.  Caller sets argsz.
 * Implementation of IRQ mapping is bus driver specific.  Indexes
 * using multiple IRQs are primarily intended to support MSI-like
 * interrupt blocks.  Zero count irq blocks may be used to describe
 * unimplemented interrupt types.
 *
 * The EVENTFD flag indicates the interrupt index supports eventfd based
 * signaling.
 *
 * The MASKABLE flags indicates the index supports MASK and UNMASK
 * actions described below.
 *
 * AUTOMASKED indicates that after signaling, the interrupt line is
 * automatically masked by VFIO and the user needs to unmask the line
 * to receive new interrupts.  This is primarily intended to distinguish
 * level triggered interrupts.
 *
 * The NORESIZE flag indicates that the interrupt lines within the index
 * are setup as a set and new subindexes cannot be enabled without first
 * disabling the entire index.  This is used for interrupts like PCI MSI
 * and MSI-X where the driver may only use a subset of the available
 * indexes, but VFIO needs to enable a specific number of vectors
 * upfront.  In the case of MSI-X, where the user can enable MSI-X and
 * then add and unmask vectors, it's up to userspace to make the decision
 * whether to allocate the maximum supported number of vectors or tear
 * down setup and incrementally increase the vectors as each is enabled.
 * Absence of the NORESIZE flag indicates that vectors can be enabled
 * and disabled dynamically without impacting other vectors within the
 * index.
 */
struct vfio_irq_info {
	__u32	argsz;
	__u32	flags;
#define VFIO_IRQ_INFO_EVENTFD		(1 << 0)
#define VFIO_IRQ_INFO_MASKABLE		(1 << 1)
#define VFIO_IRQ_INFO_AUTOMASKED	(1 << 2)
#define VFIO_IRQ_INFO_NORESIZE		(1 << 3)
	__u32	index;		/* IRQ index */
	__u32	count;		/* Number of IRQs within this index */
};
#define VFIO_DEVICE_GET_IRQ_INFO	_IO(VFIO_TYPE, VFIO_BASE + 9)

/**
 * VFIO_DEVICE_SET_IRQS - _IOW(VFIO_TYPE, VFIO_BASE + 10, struct vfio_irq_set)
 *
 * Set signaling, masking, and unmasking of interrupts.  Caller provides
 * struct vfio_irq_set with all fields set.  'start' and 'count' indicate
 * the range of subindexes being specified.
 *
 * The DATA flags specify the type of data provided.  If DATA_NONE, the
 * operation performs the specified action immediately on the specified
 * interrupt(s).  For example, to unmask AUTOMASKED interrupt [0,0]:
 * flags = (DATA_NONE|ACTION_UNMASK), index = 0, start = 0, count = 1.
 *
 * DATA_BOOL allows sparse support for the same on arrays of interrupts.
 * For example, to mask interrupts [0,1] and [0,3] (but not [0,2]):
 * flags = (DATA_BOOL|ACTION_MASK), index = 0, start = 1, count = 3,
 * data = {1,0,1}
 *
 * DATA_EVENTFD binds the specified ACTION to the provided __s32 eventfd.
 * A value of -1 can be used to either de-assign interrupts if already
 * assigned or skip un-assigned interrupts.  For example, to set an eventfd
 * to be trigger for interrupts [0,0] and [0,2]:
 * flags = (DATA_EVENTFD|ACTION_TRIGGER), index = 0, start = 0, count = 3,
 * data = {fd1, -1, fd2}
 * If index [0,1] is previously set, two count = 1 ioctls calls would be
 * required to set [0,0] and [0,2] without changing [0,1].
 *
 * Once a signaling mechanism is set, DATA_BOOL or DATA_NONE can be used
 * with ACTION_TRIGGER to perform kernel level interrupt loopback testing
 * from userspace (ie. simulate hardware triggering).
 *
 * Setting of an event triggering mechanism to userspace for ACTION_TRIGGER
 * enables the interrupt index for the device.  Individual subindex interrupts
 * can be disabled using the -1 value for DATA_EVENTFD or the index can be
 * disabled as a whole with: flags = (DATA_NONE|ACTION_TRIGGER), count = 0.
 *
 * Note that ACTION_[UN]MASK specify user->kernel signaling (irqfds) while
 * ACTION_TRIGGER specifies kernel->user signaling.
 */
struct vfio_irq_set {
	__u32	argsz;
	__u32	flags;
#define VFIO_IRQ_SET_DATA_NONE		(1 << 0) /* Data not present */
#define VFIO_IRQ_SET_DATA_BOOL		(1 << 1) /* Data is bool (u8) */
#define VFIO_IRQ_SET_DATA_EVENTFD	(1 << 2) /* Data is eventfd (s32) */
#define VFIO_IRQ_SET_ACTION_MASK	(1 << 3) /* Mask interrupt */
#define VFIO_IRQ_SET_ACTION_UNMASK	(1 << 4) /* Unmask interrupt */
#define VFIO_IRQ_SET_ACTION_TRIGGER	(1 << 5) /* Trigger interrupt */
	__u32	index;
	__u32	start;
	__u32	count;
	__u8	data[];
};
#define VFIO_DEVICE_SET_IRQS		_IO(VFIO_TYPE, VFIO_BASE + 10)

#define VFIO_IRQ_SET_DATA_TYPE_MASK	(VFIO_IRQ_SET_DATA_NONE | \
					 VFIO_IRQ_SET_DATA_BOOL | \
					 VFIO_IRQ_SET_DATA_EVENTFD)
#define VFIO_IRQ_SET_ACTION_TYPE_MASK	(VFIO_IRQ_SET_ACTION_MASK | \
					 VFIO_IRQ_SET_ACTION_UNMASK | \
					 VFIO_IRQ_SET_ACTION_TRIGGER)
/**
 * VFIO_DEVICE_RESET - _IO(VFIO_TYPE, VFIO_BASE + 11)
 *
 * Reset a device.
 */
#define VFIO_DEVICE_RESET		_IO(VFIO_TYPE, VFIO_BASE + 11)

/*
 * The VFIO-PCI bus driver makes use of the following fixed region and
 * IRQ index mapping.  Unimplemented regions return a size of zero.
 * Unimplemented IRQ types return a count of zero.
 */

enum {
	VFIO_PCI_BAR0_REGION_INDEX,
	VFIO_PCI_BAR1_REGION_INDEX,
	VFIO_PCI_BAR2_REGION_INDEX,
	VFIO_PCI_BAR3_REGION_INDEX,
	VFIO_PCI_BAR4_REGION_INDEX,
	VFIO_PCI_BAR5_REGION_INDEX,
	VFIO_PCI_ROM_REGION_INDEX,
	VFIO_PCI_CONFIG_REGION_INDEX,
	/*
	 * Expose VGA regions defined for PCI base class 03, subclass 00.
	 * This includes I/O port ranges 0x3b0 to 0x3bb and 0x3c0 to 0x3df
	 * as well as the MMIO range 0xa0000 to 0xbffff.  Each implemented
	 * range is found at it's identity mapped offset from the region
	 * offset, for example 0x3b0 is region_info.offset + 0x3b0.  Areas
	 * between described ranges are unimplemented.
	 */
	VFIO_PCI_VGA_REGION_INDEX,
	VFIO_PCI_NUM_REGIONS = 9 /* Fixed user ABI, region indexes >=9 use */
				 /* device specific cap to define content. */
};

enum {
	VFIO_PCI_INTX_IRQ_INDEX,
	VFIO_PCI_MSI_IRQ_INDEX,
	VFIO_PCI_MSIX_IRQ_INDEX,
	VFIO_PCI_ERR_IRQ_INDEX,
	VFIO_PCI_REQ_IRQ_INDEX,
	VFIO_PCI_NUM_IRQS
};

/*
 * The vfio-ccw bus driver makes use of the following fixed region and
 * IRQ index mapping. Unimplemented regions return a size of zero.
 * Unimplemented IRQ types return a count of zero.
 */

enum {
	VFIO_CCW_CONFIG_REGION_INDEX,
	VFIO_CCW_NUM_REGIONS
};

enum {
	VFIO_CCW_IO_IRQ_INDEX,
	VFIO_CCW_CRW_IRQ_INDEX,
	VFIO_CCW_REQ_IRQ_INDEX,
	VFIO_CCW_NUM_IRQS
};

/*
 * The vfio-ap bus driver makes use of the following IRQ index mapping.
 * Unimplemented IRQ types return a count of zero.
 */
enum {
	VFIO_AP_REQ_IRQ_INDEX,
	VFIO_AP_NUM_IRQS
};

/**
 * VFIO_DEVICE_GET_PCI_HOT_RESET_INFO - _IOWR(VFIO_TYPE, VFIO_BASE + 12,
 *					      struct vfio_pci_hot_reset_info)
 *
 * Return: 0 on success, -errno on failure:
 *	-enospc = insufficient buffer, -enodev = unsupported for device.
 */
struct vfio_pci_dependent_device {
	__u32	group_id;
	__u16	segment;
	__u8	bus;
	__u8	devfn; /* Use PCI_SLOT/PCI_FUNC */
};

struct vfio_pci_hot_reset_info {
	__u32	argsz;
	__u32	flags;
	__u32	count;
	struct vfio_pci_dependent_device	devices[];
};

#define VFIO_DEVICE_GET_PCI_HOT_RESET_INFO	_IO(VFIO_TYPE, VFIO_BASE + 12)

/**
 * VFIO_DEVICE_PCI_HOT_RESET - _IOW(VFIO_TYPE, VFIO_BASE + 13,
 *				    struct vfio_pci_hot_reset)
 *
 * Return: 0 on success, -errno on failure.
 */
struct vfio_pci_hot_reset {
	__u32	argsz;
	__u32	flags;
	__u32	count;
	__s32	group_fds[];
};

#define VFIO_DEVICE_PCI_HOT_RESET	_IO(VFIO_TYPE, VFIO_BASE + 13)

/**
 * VFIO_DEVICE_QUERY_GFX_PLANE - _IOW(VFIO_TYPE, VFIO_BASE + 14,
 *                                    struct vfio_device_query_gfx_plane)
 *
 * Set the drm_plane_type and flags, then retrieve the gfx plane info.
 *
 * flags supported:
 * - VFIO_GFX_PLANE_TYPE_PROBE and VFIO_GFX_PLANE_TYPE_DMABUF are set
 *   to ask if the mdev supports dma-buf. 0 on support, -EINVAL on no
 *   support for dma-buf.
 * - VFIO_GFX_PLANE_TYPE_PROBE and VFIO_GFX_PLANE_TYPE_REGION are set
 *   to ask if the mdev supports region. 0 on support, -EINVAL on no
 *   support for region.
 * - VFIO_GFX_PLANE_TYPE_DMABUF or VFIO_GFX_PLANE_TYPE_REGION is set
 *   with each call to query the plane info.
 * - Others are invalid and return -EINVAL.
 *
 * Note:
 * 1. Plane could be disabled by guest. In that case, success will be
 *    returned with zero-initialized drm_format, size, width and height
 *    fields.
 * 2. x_hot/y_hot is set to 0xFFFFFFFF if no hotspot information available
 *
 * Return: 0 on success, -errno on other failure.
 */
struct vfio_device_gfx_plane_info {
	__u32 argsz;
	__u32 flags;
#define VFIO_GFX_PLANE_TYPE_PROBE (1 << 0)
#define VFIO_GFX_PLANE_TYPE_DMABUF (1 << 1)
#define VFIO_GFX_PLANE_TYPE_REGION (1 << 2)
	/* in */
	__u32 drm_plane_type;	/* type of plane: DRM_PLANE_TYPE_* */
	/* out */
	__u32 drm_format;	/* drm format of plane */
	__u64 drm_format_mod;   /* tiled mode */
	__u32 width;	/* width of plane */
	__u32 height;	/* height of plane */
	__u32 stride;	/* stride of plane */
	__u32 size;	/* size of plane in bytes, align on page*/
	__u32 x_pos;	/* horizontal position of cursor plane */
	__u32 y_pos;	/* vertical position of cursor plane*/
	__u32 x_hot;    /* horizontal position of cursor hotspot */
	__u32 y_hot;    /* vertical position of cursor hotspot */
	union {
		__u32 region_index;	/* region index */
		__u32 dmabuf_id;	/* dma-buf id */
	};
};

#define VFIO_DEVICE_QUERY_GFX_PLANE _IO(VFIO_TYPE, VFIO_BASE + 14)

/**
 * VFIO_DEVICE_GET_GFX_DMABUF - _IOW(VFIO_TYPE, VFIO_BASE + 15, __u32)
 *
 * Return a new dma-buf file descriptor for an exposed guest framebuffer
 * described by the provided dmabuf_id. The dmabuf_id is returned from VFIO_
 * DEVICE_QUERY_GFX_PLANE as a token of the exposed guest framebuffer.
 */

#define VFIO_DEVICE_GET_GFX_DMABUF _IO(VFIO_TYPE, VFIO_BASE + 15)

/**
 * VFIO_DEVICE_IOEVENTFD - _IOW(VFIO_TYPE, VFIO_BASE + 16,
 *                              struct vfio_device_ioeventfd)
 *
 * Perform a write to the device at the specified device fd offset, with
 * the specified data and width when the provided eventfd is triggered.
 * vfio bus drivers may not support this for all regions, for all widths,
 * or at all.  vfio-pci currently only enables support for BAR regions,
 * excluding the MSI-X vector table.
 *
 * Return: 0 on success, -errno on failure.
 */
struct vfio_device_ioeventfd {
	__u32	argsz;
	__u32	flags;
#define VFIO_DEVICE_IOEVENTFD_8		(1 << 0) /* 1-byte write */
#define VFIO_DEVICE_IOEVENTFD_16	(1 << 1) /* 2-byte write */
#define VFIO_DEVICE_IOEVENTFD_32	(1 << 2) /* 4-byte write */
#define VFIO_DEVICE_IOEVENTFD_64	(1 << 3) /* 8-byte write */
#define VFIO_DEVICE_IOEVENTFD_SIZE_MASK	(0xf)
	__u64	offset;			/* device fd offset of write */
	__u64	data;			/* data to be written */
	__s32	fd;			/* -1 for de-assignment */
};

#define VFIO_DEVICE_IOEVENTFD		_IO(VFIO_TYPE, VFIO_BASE + 16)

/**
 * VFIO_DEVICE_FEATURE - _IOWR(VFIO_TYPE, VFIO_BASE + 17,
 *			       struct vfio_device_feature)
 *
 * Get, set, or probe feature data of the device.  The feature is selected
 * using the FEATURE_MASK portion of the flags field.  Support for a feature
 * can be probed by setting both the FEATURE_MASK and PROBE bits.  A probe
 * may optionally include the GET and/or SET bits to determine read vs write
 * access of the feature respectively.  Probing a feature will return success
 * if the feature is supported and all of the optionally indicated GET/SET
 * methods are supported.  The format of the data portion of the structure is
 * specific to the given feature.  The data portion is not required for
 * probing.  GET and SET are mutually exclusive, except for use with PROBE.
 *
 * Return 0 on success, -errno on failure.
 */
struct vfio_device_feature {
	__u32	argsz;
	__u32	flags;
#define VFIO_DEVICE_FEATURE_MASK	(0xffff) /* 16-bit feature index */
#define VFIO_DEVICE_FEATURE_GET		(1 << 16) /* Get feature into data[] */
#define VFIO_DEVICE_FEATURE_SET		(1 << 17) /* Set feature from data[] */
#define VFIO_DEVICE_FEATURE_PROBE	(1 << 18) /* Probe feature support */
	__u8	data[];
};

#define VFIO_DEVICE_FEATURE		_IO(VFIO_TYPE, VFIO_BASE + 17)

/*
 * Provide support for setting a PCI VF Token, which is used as a shared
 * secret between PF and VF drivers.  This feature may only be set on a
 * PCI SR-IOV PF when SR-IOV is enabled on the PF and there are no existing
 * open VFs.  Data provided when setting this feature is a 16-byte array
 * (__u8 b[16]), representing a UUID.
 */
#define VFIO_DEVICE_FEATURE_PCI_VF_TOKEN	(0)

/*
 * Indicates the device can support the migration API through
 * VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE. If this GET succeeds, the RUNNING and
 * ERROR states are always supported. Support for additional states is
 * indicated via the flags field; at least VFIO_MIGRATION_STOP_COPY must be
 * set.
 *
 * VFIO_MIGRATION_STOP_COPY means that STOP, STOP_COPY and
 * RESUMING are supported.
 *
 * VFIO_MIGRATION_STOP_COPY | VFIO_MIGRATION_P2P means that RUNNING_P2P
 * is supported in addition to the STOP_COPY states.
 *
 * VFIO_MIGRATION_STOP_COPY | VFIO_MIGRATION_PRE_COPY means that
 * PRE_COPY is supported in addition to the STOP_COPY states.
 *
 * VFIO_MIGRATION_STOP_COPY | VFIO_MIGRATION_P2P | VFIO_MIGRATION_PRE_COPY
 * means that RUNNING_P2P, PRE_COPY and PRE_COPY_P2P are supported
 * in addition to the STOP_COPY states.
 *
 * Other combinations of flags have behavior to be defined in the future.
 */
struct vfio_device_feature_migration {
	__aligned_u64 flags;
#define VFIO_MIGRATION_STOP_COPY	(1 << 0)
#define VFIO_MIGRATION_P2P		(1 << 1)
#define VFIO_MIGRATION_PRE_COPY		(1 << 2)
};
#define VFIO_DEVICE_FEATURE_MIGRATION 1

/*
 * Upon VFIO_DEVICE_FEATURE_SET, execute a migration state change on the VFIO
 * device. The new state is supplied in device_state, see enum
 * vfio_device_mig_state for details
 *
 * The kernel migration driver must fully transition the device to the new state
 * value before the operation returns to the user.
 *
 * The kernel migration driver must not generate asynchronous device state
 * transitions outside of manipulation by the user or the VFIO_DEVICE_RESET
 * ioctl as described above.
 *
 * If this function fails then current device_state may be the original
 * operating state or some other state along the combination transition path.
 * The user can then decide if it should execute a VFIO_DEVICE_RESET, attempt
 * to return to the original state, or attempt to return to some other state
 * such as RUNNING or STOP.
 *
 * If the new_state starts a new data transfer session then the FD associated
 * with that session is returned in data_fd. The user is responsible to close
 * this FD when it is finished. The user must consider the migration data stream
 * carried over the FD to be opaque and must preserve the byte order of the
 * stream. The user is not required to preserve buffer segmentation when writing
 * the data stream during the RESUMING operation.
 *
 * Upon VFIO_DEVICE_FEATURE_GET, get the current migration state of the VFIO
 * device, data_fd will be -1.
 */
struct vfio_device_feature_mig_state {
	__u32 device_state; /* From enum vfio_device_mig_state */
	__s32 data_fd;
};
#define VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE 2

/*
 * The device migration Finite State Machine is described by the enum
 * vfio_device_mig_state. Some of the FSM arcs will create a migration data
 * transfer session by returning a FD, in this case the migration data will
 * flow over the FD using read() and write() as discussed below.
 *
 * There are 5 states to support VFIO_MIGRATION_STOP_COPY:
 *  RUNNING - The device is running normally
 *  STOP - The device does not change the internal or external state
 *  STOP_COPY - The device internal state can be read out
 *  RESUMING - The device is stopped and is loading a new internal state
 *  ERROR - The device has failed and must be reset
 *
 * And optional states to support VFIO_MIGRATION_P2P:
 *  RUNNING_P2P - RUNNING, except the device cannot do peer to peer DMA
 * And VFIO_MIGRATION_PRE_COPY:
 *  PRE_COPY - The device is running normally but tracking internal state
 *             changes
 * And VFIO_MIGRATION_P2P | VFIO_MIGRATION_PRE_COPY:
 *  PRE_COPY_P2P - PRE_COPY, except the device cannot do peer to peer DMA
 *
 * The FSM takes actions on the arcs between FSM states. The driver implements
 * the following behavior for the FSM arcs:
 *
 * RUNNING_P2P -> STOP
 * STOP_COPY -> STOP
 *   While in STOP the device must stop the operation of the device. The device
 *   must not generate interrupts, DMA, or any other change to external state.
 *   It must not change its internal state. When stopped the device and kernel
 *   migration driver must accept and respond to interaction to support external
 *   subsystems in the STOP state, for example PCI MSI-X and PCI config space.
 *   Failure by the user to restrict device access while in STOP must not result
 *   in error conditions outside the user context (ex. host system faults).
 *
 *   The STOP_COPY arc will terminate a data transfer session.
 *
 * RESUMING -> STOP
 *   Leaving RESUMING terminates a data transfer session and indicates the
 *   device should complete processing of the data delivered by write(). The
 *   kernel migration driver should complete the incorporation of data written
 *   to the data transfer FD into the device internal state and perform
 *   final validity and consistency checking of the new device state. If the
 *   user provided data is found to be incomplete, inconsistent, or otherwise
 *   invalid, the migration driver must fail the SET_STATE ioctl and
 *   optionally go to the ERROR state as described below.
 *
 *   While in STOP the device has the same behavior as other STOP states
 *   described above.
 *
 *   To abort a RESUMING session the device must be reset.
 *
 * PRE_COPY -> RUNNING
 * RUNNING_P2P -> RUNNING
 *   While in RUNNING the device is fully operational, the device may generate
 *   interrupts, DMA, respond to MMIO, all vfio device regions are functional,
 *   and the device may advance its internal state.
 *
 *   The PRE_COPY arc will terminate a data transfer session.
 *
 * PRE_COPY_P2P -> RUNNING_P2P
 * RUNNING -> RUNNING_P2P
 * STOP -> RUNNING_P2P
 *   While in RUNNING_P2P the device is partially running in the P2P quiescent
 *   state defined below.
 *
 *   The PRE_COPY_P2P arc will terminate a data transfer session.
 *
 * RUNNING -> PRE_COPY
 * RUNNING_P2P -> PRE_COPY_P2P
 * STOP -> STOP_COPY
 *   PRE_COPY, PRE_COPY_P2P and STOP_COPY form the "saving group" of states
 *   which share a data transfer session. Moving between these states alters
 *   what is streamed in session, but does not terminate or otherwise affect
 *   the associated fd.
 *
 *   These arcs begin the process of saving the device state and will return a
 *   new data_fd. The migration driver may perform actions such as enabling
 *   dirty logging of device state when entering PRE_COPY or PER_COPY_P2P.
 *
 *   Each arc does not change the device operation, the device remains
 *   RUNNING, P2P quiesced or in STOP. The STOP_COPY state is described below
 *   in PRE_COPY_P2P -> STOP_COPY.
 *
 * PRE_COPY -> PRE_COPY_P2P
 *   Entering PRE_COPY_P2P continues all the behaviors of PRE_COPY above.
 *   However, while in the PRE_COPY_P2P state, the device is partially running
 *   in the P2P quiescent state defined below, like RUNNING_P2P.
 *
 * PRE_COPY_P2P -> PRE_COPY
 *   This arc allows returning the device to a full RUNNING behavior while
 *   continuing all the behaviors of PRE_COPY.
 *
 * PRE_COPY_P2P -> STOP_COPY
 *   While in the STOP_COPY state the device has the same behavior as STOP
 *   with the addition that the data transfers session continues to stream the
 *   migration state. End of stream on the FD indicates the entire device
 *   state has been transferred.
 *
 *   The user should take steps to restrict access to vfio device regions while
 *   the device is in STOP_COPY or risk corruption of the device migration data
 *   stream.
 *
 * STOP -> RESUMING
 *   Entering the RESUMING state starts a process of restoring the device state
 *   and will return a new data_fd. The data stream fed into the data_fd should
 *   be taken from the data transfer output of a single FD during saving from
 *   a compatible device. The migration driver may alter/reset the internal
 *   device state for this arc if required to prepare the device to receive the
 *   migration data.
 *
 * STOP_COPY -> PRE_COPY
 * STOP_COPY -> PRE_COPY_P2P
 *   These arcs are not permitted and return error if requested. Future
 *   revisions of this API may define behaviors for these arcs, in this case
 *   support will be discoverable by a new flag in
 *   VFIO_DEVICE_FEATURE_MIGRATION.
 *
 * any -> ERROR
 *   ERROR cannot be specified as a device state, however any transition request
 *   can be failed with an errno return and may then move the device_state into
 *   ERROR. In this case the device was unable to execute the requested arc and
 *   was also unable to restore the device to any valid device_state.
 *   To recover from ERROR VFIO_DEVICE_RESET must be used to return the
 *   device_state back to RUNNING.
 *
 * The optional peer to peer (P2P) quiescent state is intended to be a quiescent
 * state for the device for the purposes of managing multiple devices within a
 * user context where peer-to-peer DMA between devices may be active. The
 * RUNNING_P2P and PRE_COPY_P2P states must prevent the device from initiating
 * any new P2P DMA transactions. If the device can identify P2P transactions
 * then it can stop only P2P DMA, otherwise it must stop all DMA. The migration
 * driver must complete any such outstanding operations prior to completing the
 * FSM arc into a P2P state. For the purpose of specification the states
 * behave as though the device was fully running if not supported. Like while in
 * STOP or STOP_COPY the user must not touch the device, otherwise the state
 * can be exited.
 *
 * The remaining possible transitions are interpreted as combinations of the
 * above FSM arcs. As there are multiple paths through the FSM arcs the path
 * should be selected based on the following rules:
 *   - Select the shortest path.
 *   - The path cannot have saving group states as interior arcs, only
 *     starting/end states.
 * Refer to vfio_mig_get_next_state() for the result of the algorithm.
 *
 * The automatic transit through the FSM arcs that make up the combination
 * transition is invisible to the user. When working with combination arcs the
 * user may see any step along the path in the device_state if SET_STATE
 * fails. When handling these types of errors users should anticipate future
 * revisions of this protocol using new states and those states becoming
 * visible in this case.
 *
 * The optional states cannot be used with SET_STATE if the device does not
 * support them. The user can discover if these states are supported by using
 * VFIO_DEVICE_FEATURE_MIGRATION. By using combination transitions the user can
 * avoid knowing about these optional states if the kernel driver supports them.
 *
 * Arcs touching PRE_COPY and PRE_COPY_P2P are removed if support for PRE_COPY
 * is not present.
 */
enum vfio_device_mig_state {
	VFIO_DEVICE_STATE_ERROR = 0,
	VFIO_DEVICE_STATE_STOP = 1,
	VFIO_DEVICE_STATE_RUNNING = 2,
	VFIO_DEVICE_STATE_STOP_COPY = 3,
	VFIO_DEVICE_STATE_RESUMING = 4,
	VFIO_DEVICE_STATE_RUNNING_P2P = 5,
	VFIO_DEVICE_STATE_PRE_COPY = 6,
	VFIO_DEVICE_STATE_PRE_COPY_P2P = 7,
};

/**
 * VFIO_MIG_GET_PRECOPY_INFO - _IO(VFIO_TYPE, VFIO_BASE + 21)
 *
 * This ioctl is used on the migration data FD in the precopy phase of the
 * migration data transfer. It returns an estimate of the current data sizes
 * remaining to be transferred. It allows the user to judge when it is
 * appropriate to leave PRE_COPY for STOP_COPY.
 *
 * This ioctl is valid only in PRE_COPY states and kernel driver should
 * return -EINVAL from any other migration state.
 *
 * The vfio_precopy_info data structure returned by this ioctl provides
 * estimates of data available from the device during the PRE_COPY states.
 * This estimate is split into two categories, initial_bytes and
 * dirty_bytes.
 *
 * The initial_bytes field indicates the amount of initial precopy
 * data available from the device. This field should have a non-zero initial
 * value and decrease as migration data is read from the device.
 * It is recommended to leave PRE_COPY for STOP_COPY only after this field
 * reaches zero. Leaving PRE_COPY earlier might make things slower.
 *
 * The dirty_bytes field tracks device state changes relative to data
 * previously retrieved.  This field starts at zero and may increase as
 * the internal device state is modified or decrease as that modified
 * state is read from the device.
 *
 * Userspace may use the combination of these fields to estimate the
 * potential data size available during the PRE_COPY phases, as well as
 * trends relative to the rate the device is dirtying its internal
 * state, but these fields are not required to have any bearing relative
 * to the data size available during the STOP_COPY phase.
 *
 * Drivers have a lot of flexibility in when and what they transfer during the
 * PRE_COPY phase, and how they report this from VFIO_MIG_GET_PRECOPY_INFO.
 *
 * During pre-copy the migration data FD has a temporary "end of stream" that is
 * reached when both initial_bytes and dirty_byte are zero. For instance, this
 * may indicate that the device is idle and not currently dirtying any internal
 * state. When read() is done on this temporary end of stream the kernel driver
 * should return ENOMSG from read(). Userspace can wait for more data (which may
 * never come) by using poll.
 *
 * Once in STOP_COPY the migration data FD has a permanent end of stream
 * signaled in the usual way by read() always returning 0 and poll always
 * returning readable. ENOMSG may not be returned in STOP_COPY.
 * Support for this ioctl is mandatory if a driver claims to support
 * VFIO_MIGRATION_PRE_COPY.
 *
 * Return: 0 on success, -1 and errno set on failure.
 */
struct vfio_precopy_info {
	__u32 argsz;
	__u32 flags;
	__aligned_u64 initial_bytes;
	__aligned_u64 dirty_bytes;
};

#define VFIO_MIG_GET_PRECOPY_INFO _IO(VFIO_TYPE, VFIO_BASE + 21)

/*
 * Upon VFIO_DEVICE_FEATURE_SET, allow the device to be moved into a low power
 * state with the platform-based power management.  Device use of lower power
 * states depends on factors managed by the runtime power management core,
 * including system level support and coordinating support among dependent
 * devices.  Enabling device low power entry does not guarantee lower power
 * usage by the device, nor is a mechanism provided through this feature to
 * know the current power state of the device.  If any device access happens
 * (either from the host or through the vfio uAPI) when the device is in the
 * low power state, then the host will move the device out of the low power
 * state as necessary prior to the access.  Once the access is completed, the
 * device may re-enter the low power state.  For single shot low power support
 * with wake-up notification, see
 * VFIO_DEVICE_FEATURE_LOW_POWER_ENTRY_WITH_WAKEUP below.  Access to mmap'd
 * device regions is disabled on LOW_POWER_ENTRY and may only be resumed after
 * calling LOW_POWER_EXIT.
 */
#define VFIO_DEVICE_FEATURE_LOW_POWER_ENTRY 3

/*
 * This device feature has the same behavior as
 * VFIO_DEVICE_FEATURE_LOW_POWER_ENTRY with the exception that the user
 * provides an eventfd for wake-up notification.  When the device moves out of
 * the low power state for the wake-up, the host will not allow the device to
 * re-enter a low power state without a subsequent user call to one of the low
 * power entry device feature IOCTLs.  Access to mmap'd device regions is
 * disabled on LOW_POWER_ENTRY_WITH_WAKEUP and may only be resumed after the
 * low power exit.  The low power exit can happen either through LOW_POWER_EXIT
 * or through any other access (where the wake-up notification has been
 * generated).  The access to mmap'd device regions will not trigger low power
 * exit.
 *
 * The notification through the provided eventfd will be generated only when
 * the device has entered and is resumed from a low power state after
 * calling this device feature IOCTL.  A device that has not entered low power
 * state, as managed through the runtime power management core, will not
 * generate a notification through the provided eventfd on access.  Calling the
 * LOW_POWER_EXIT feature is optional in the case where notification has been
 * signaled on the provided eventfd that a resume from low power has occurred.
 */
struct vfio_device_low_power_entry_with_wakeup {
	__s32 wakeup_eventfd;
	__u32 reserved;
};

#define VFIO_DEVICE_FEATURE_LOW_POWER_ENTRY_WITH_WAKEUP 4

/*
 * Upon VFIO_DEVICE_FEATURE_SET, disallow use of device low power states as
 * previously enabled via VFIO_DEVICE_FEATURE_LOW_POWER_ENTRY or
 * VFIO_DEVICE_FEATURE_LOW_POWER_ENTRY_WITH_WAKEUP device features.
 * This device feature IOCTL may itself generate a wakeup eventfd notification
 * in the latter case if the device had previously entered a low power state.
 */
#define VFIO_DEVICE_FEATURE_LOW_POWER_EXIT 5

/*
 * Upon VFIO_DEVICE_FEATURE_SET start/stop device DMA logging.
 * VFIO_DEVICE_FEATURE_PROBE can be used to detect if the device supports
 * DMA logging.
 *
 * DMA logging allows a device to internally record what DMAs the device is
 * initiating and report them back to userspace. It is part of the VFIO
 * migration infrastructure that allows implementing dirty page tracking
 * during the pre copy phase of live migration. Only DMA WRITEs are logged,
 * and this API is not connected to VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE.
 *
 * When DMA logging is started a range of IOVAs to monitor is provided and the
 * device can optimize its logging to cover only the IOVA range given. Each
 * DMA that the device initiates inside the range will be logged by the device
 * for later retrieval.
 *
 * page_size is an input that hints what tracking granularity the device
 * should try to achieve. If the device cannot do the hinted page size then
 * it's the driver choice which page size to pick based on its support.
 * On output the device will return the page size it selected.
 *
 * ranges is a pointer to an array of
 * struct vfio_device_feature_dma_logging_range.
 *
 * The core kernel code guarantees to support by minimum num_ranges that fit
 * into a single kernel page. User space can try higher values but should give
 * up if the above can't be achieved as of some driver limitations.
 *
 * A single call to start device DMA logging can be issued and a matching stop
 * should follow at the end. Another start is not allowed in the meantime.
 */
struct vfio_device_feature_dma_logging_control {
	__aligned_u64 page_size;
	__u32 num_ranges;
	__u32 __reserved;
	__aligned_u64 ranges;
};

struct vfio_device_feature_dma_logging_range {
	__aligned_u64 iova;
	__aligned_u64 length;
};

#define VFIO_DEVICE_FEATURE_DMA_LOGGING_START 6

/*
 * Upon VFIO_DEVICE_FEATURE_SET stop device DMA logging that was started
 * by VFIO_DEVICE_FEATURE_DMA_LOGGING_START
 */
#define VFIO_DEVICE_FEATURE_DMA_LOGGING_STOP 7

/*
 * Upon VFIO_DEVICE_FEATURE_GET read back and clear the device DMA log
 *
 * Query the device's DMA log for written pages within the given IOVA range.
 * During querying the log is cleared for the IOVA range.
 *
 * bitmap is a pointer to an array of u64s that will hold the output bitmap
 * with 1 bit reporting a page_size unit of IOVA. The mapping of IOVA to bits
 * is given by:
 *  bitmap[(addr - iova)/page_size] & (1ULL << (addr % 64))
 *
 * The input page_size can be any power of two value and does not have to
 * match the value given to VFIO_DEVICE_FEATURE_DMA_LOGGING_START. The driver
 * will format its internal logging to match the reporting page size, possibly
 * by replicating bits if the internal page size is lower than requested.
 *
 * The LOGGING_REPORT will only set bits in the bitmap and never clear or
 * perform any initialization of the user provided bitmap.
 *
 * If any error is returned userspace should assume that the dirty log is
 * corrupted. Error recovery is to consider all memory dirty and try to
 * restart the dirty tracking, or to abort/restart the whole migration.
 *
 * If DMA logging is not enabled, an error will be returned.
 *
 */
struct vfio_device_feature_dma_logging_report {
	__aligned_u64 iova;
	__aligned_u64 length;
	__aligned_u64 page_size;
	__aligned_u64 bitmap;
};

#define VFIO_DEVICE_FEATURE_DMA_LOGGING_REPORT 8

/*
 * Upon VFIO_DEVICE_FEATURE_GET read back the estimated data length that will
 * be required to complete stop copy.
 *
 * Note: Can be called on each device state.
 */

struct vfio_device_feature_mig_data_size {
	__aligned_u64 stop_copy_length;
};

#define VFIO_DEVICE_FEATURE_MIG_DATA_SIZE 9

/* -------- API for Type1 VFIO IOMMU -------- */

/**
 * VFIO_IOMMU_GET_INFO - _IOR(VFIO_TYPE, VFIO_BASE + 12, struct vfio_iommu_info)
 *
 * Retrieve information about the IOMMU object. Fills in provided
 * struct vfio_iommu_info. Caller sets argsz.
 *
 * XXX Should we do these by CHECK_EXTENSION too?
 */
struct vfio_iommu_type1_info {
	__u32	argsz;
	__u32	flags;
#define VFIO_IOMMU_INFO_PGSIZES (1 << 0)	/* supported page sizes info */
#define VFIO_IOMMU_INFO_CAPS	(1 << 1)	/* Info supports caps */
	__u64	iova_pgsizes;	/* Bitmap of supported page sizes */
	__u32   cap_offset;	/* Offset within info struct of first cap */
};

/*
 * The IOVA capability allows to report the valid IOVA range(s)
 * excluding any non-relaxable reserved regions exposed by
 * devices attached to the container. Any DMA map attempt
 * outside the valid iova range will return error.
 *
 * The structures below define version 1 of this capability.
 */
#define VFIO_IOMMU_TYPE1_INFO_CAP_IOVA_RANGE  1

struct vfio_iova_range {
	__u64	start;
	__u64	end;
};

struct vfio_iommu_type1_info_cap_iova_range {
	struct	vfio_info_cap_header header;
	__u32	nr_iovas;
	__u32	reserved;
	struct	vfio_iova_range iova_ranges[];
};

/*
 * The migration capability allows to report supported features for migration.
 *
 * The structures below define version 1 of this capability.
 *
 * The existence of this capability indicates that IOMMU kernel driver supports
 * dirty page logging.
 *
 * pgsize_bitmap: Kernel driver returns bitmap of supported page sizes for dirty
 * page logging.
 * max_dirty_bitmap_size: Kernel driver returns maximum supported dirty bitmap
 * size in bytes that can be used by user applications when getting the dirty
 * bitmap.
 */
#define VFIO_IOMMU_TYPE1_INFO_CAP_MIGRATION  2

struct vfio_iommu_type1_info_cap_migration {
	struct	vfio_info_cap_header header;
	__u32	flags;
	__u64	pgsize_bitmap;
	__u64	max_dirty_bitmap_size;		/* in bytes */
};

/*
 * The DMA available capability allows to report the current number of
 * simultaneously outstanding DMA mappings that are allowed.
 *
 * The structure below defines version 1 of this capability.
 *
 * avail: specifies the current number of outstanding DMA mappings allowed.
 */
#define VFIO_IOMMU_TYPE1_INFO_DMA_AVAIL 3

struct vfio_iommu_type1_info_dma_avail {
	struct	vfio_info_cap_header header;
	__u32	avail;
};

#define VFIO_IOMMU_GET_INFO _IO(VFIO_TYPE, VFIO_BASE + 12)

/**
 * VFIO_IOMMU_MAP_DMA - _IOW(VFIO_TYPE, VFIO_BASE + 13, struct vfio_dma_map)
 *
 * Map process virtual addresses to IO virtual addresses using the
 * provided struct vfio_dma_map. Caller sets argsz. READ &/ WRITE required.
 *
 * If flags & VFIO_DMA_MAP_FLAG_VADDR, update the base vaddr for iova. The vaddr
 * must have previously been invalidated with VFIO_DMA_UNMAP_FLAG_VADDR.  To
 * maintain memory consistency within the user application, the updated vaddr
 * must address the same memory object as originally mapped.  Failure to do so
 * will result in user memory corruption and/or device misbehavior.  iova and
 * size must match those in the original MAP_DMA call.  Protection is not
 * changed, and the READ & WRITE flags must be 0.
 */
struct vfio_iommu_type1_dma_map {
	__u32	argsz;
	__u32	flags;
#define VFIO_DMA_MAP_FLAG_READ (1 << 0)		/* readable from device */
#define VFIO_DMA_MAP_FLAG_WRITE (1 << 1)	/* writable from device */
#define VFIO_DMA_MAP_FLAG_VADDR (1 << 2)
	__u64	vaddr;				/* Process virtual address */
	__u64	iova;				/* IO virtual address */
	__u64	size;				/* Size of mapping (bytes) */
};

#define VFIO_IOMMU_MAP_DMA _IO(VFIO_TYPE, VFIO_BASE + 13)

struct vfio_bitmap {
	__u64        pgsize;	/* page size for bitmap in bytes */
	__u64        size;	/* in bytes */
	__u64 __user *data;	/* one bit per page */
};

/**
 * VFIO_IOMMU_UNMAP_DMA - _IOWR(VFIO_TYPE, VFIO_BASE + 14,
 *							struct vfio_dma_unmap)
 *
 * Unmap IO virtual addresses using the provided struct vfio_dma_unmap.
 * Caller sets argsz.  The actual unmapped size is returned in the size
 * field.  No guarantee is made to the user that arbitrary unmaps of iova
 * or size different from those used in the original mapping call will
 * succeed.
 *
 * VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP should be set to get the dirty bitmap
 * before unmapping IO virtual addresses. When this flag is set, the user must
 * provide a struct vfio_bitmap in data[]. User must provide zero-allocated
 * memory via vfio_bitmap.data and its size in the vfio_bitmap.size field.
 * A bit in the bitmap represents one page, of user provided page size in
 * vfio_bitmap.pgsize field, consecutively starting from iova offset. Bit set
 * indicates that the page at that offset from iova is dirty. A Bitmap of the
 * pages in the range of unmapped size is returned in the user-provided
 * vfio_bitmap.data.
 *
 * If flags & VFIO_DMA_UNMAP_FLAG_ALL, unmap all addresses.  iova and size
 * must be 0.  This cannot be combined with the get-dirty-bitmap flag.
 *
 * If flags & VFIO_DMA_UNMAP_FLAG_VADDR, do not unmap, but invalidate host
 * virtual addresses in the iova range.  DMA to already-mapped pages continues.
 * Groups may not be added to the container while any addresses are invalid.
 * This cannot be combined with the get-dirty-bitmap flag.
 */
struct vfio_iommu_type1_dma_unmap {
	__u32	argsz;
	__u32	flags;
#define VFIO_DMA_UNMAP_FLAG_GET_DIRTY_BITMAP (1 << 0)
#define VFIO_DMA_UNMAP_FLAG_ALL		     (1 << 1)
#define VFIO_DMA_UNMAP_FLAG_VADDR	     (1 << 2)
	__u64	iova;				/* IO virtual address */
	__u64	size;				/* Size of mapping (bytes) */
	__u8    data[];
};

#define VFIO_IOMMU_UNMAP_DMA _IO(VFIO_TYPE, VFIO_BASE + 14)

/*
 * IOCTLs to enable/disable IOMMU container usage.
 * No parameters are supported.
 */
#define VFIO_IOMMU_ENABLE	_IO(VFIO_TYPE, VFIO_BASE + 15)
#define VFIO_IOMMU_DISABLE	_IO(VFIO_TYPE, VFIO_BASE + 16)

/**
 * VFIO_IOMMU_DIRTY_PAGES - _IOWR(VFIO_TYPE, VFIO_BASE + 17,
 *                                     struct vfio_iommu_type1_dirty_bitmap)
 * IOCTL is used for dirty pages logging.
 * Caller should set flag depending on which operation to perform, details as
 * below:
 *
 * Calling the IOCTL with VFIO_IOMMU_DIRTY_PAGES_FLAG_START flag set, instructs
 * the IOMMU driver to log pages that are dirtied or potentially dirtied by
 * the device; designed to be used when a migration is in progress. Dirty pages
 * are logged until logging is disabled by user application by calling the IOCTL
 * with VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP flag.
 *
 * Calling the IOCTL with VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP flag set, instructs
 * the IOMMU driver to stop logging dirtied pages.
 *
 * Calling the IOCTL with VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP flag set
 * returns the dirty pages bitmap for IOMMU container for a given IOVA range.
 * The user must specify the IOVA range and the pgsize through the structure
 * vfio_iommu_type1_dirty_bitmap_get in the data[] portion. This interface
 * supports getting a bitmap of the smallest supported pgsize only and can be
 * modified in future to get a bitmap of any specified supported pgsize. The
 * user must provide a zeroed memory area for the bitmap memory and specify its
 * size in bitmap.size. One bit is used to represent one page consecutively
 * starting from iova offset. The user should provide page size in bitmap.pgsize
 * field. A bit set in the bitmap indicates that the page at that offset from
 * iova is dirty. The caller must set argsz to a value including the size of
 * structure vfio_iommu_type1_dirty_bitmap_get, but excluding the size of the
 * actual bitmap. If dirty pages logging is not enabled, an error will be
 * returned.
 *
 * Only one of the flags _START, _STOP and _GET may be specified at a time.
 *
 */
struct vfio_iommu_type1_dirty_bitmap {
	__u32        argsz;
	__u32        flags;
#define VFIO_IOMMU_DIRTY_PAGES_FLAG_START	(1 << 0)
#define VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP	(1 << 1)
#define VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP	(1 << 2)
	__u8         data[];
};

struct vfio_iommu_type1_dirty_bitmap_get {
	__u64              iova;	/* IO virtual address */
	__u64              size;	/* Size of iova range */
	struct vfio_bitmap bitmap;
};

#define VFIO_IOMMU_DIRTY_PAGES             _IO(VFIO_TYPE, VFIO_BASE + 17)

/* -------- Additional API for SPAPR TCE (Server POWERPC) IOMMU -------- */

/*
 * The SPAPR TCE DDW info struct provides the information about
 * the details of Dynamic DMA window capability.
 *
 * @pgsizes contains a page size bitmask, 4K/64K/16M are supported.
 * @max_dynamic_windows_supported tells the maximum number of windows
 * which the platform can create.
 * @levels tells the maximum number of levels in multi-level IOMMU tables;
 * this allows splitting a table into smaller chunks which reduces
 * the amount of physically contiguous memory required for the table.
 */
struct vfio_iommu_spapr_tce_ddw_info {
	__u64 pgsizes;			/* Bitmap of supported page sizes */
	__u32 max_dynamic_windows_supported;
	__u32 levels;
};

/*
 * The SPAPR TCE info struct provides the information about the PCI bus
 * address ranges available for DMA, these values are programmed into
 * the hardware so the guest has to know that information.
 *
 * The DMA 32 bit window start is an absolute PCI bus address.
 * The IOVA address passed via map/unmap ioctls are absolute PCI bus
 * addresses too so the window works as a filter rather than an offset
 * for IOVA addresses.
 *
 * Flags supported:
 * - VFIO_IOMMU_SPAPR_INFO_DDW: informs the userspace that dynamic DMA windows
 *   (DDW) support is present. @ddw is only supported when DDW is present.
 */
struct vfio_iommu_spapr_tce_info {
	__u32 argsz;
	__u32 flags;
#define VFIO_IOMMU_SPAPR_INFO_DDW	(1 << 0)	/* DDW supported */
	__u32 dma32_window_start;	/* 32 bit window start (bytes) */
	__u32 dma32_window_size;	/* 32 bit window size (bytes) */
	struct vfio_iommu_spapr_tce_ddw_info ddw;
};

#define VFIO_IOMMU_SPAPR_TCE_GET_INFO	_IO(VFIO_TYPE, VFIO_BASE + 12)

/*
 * EEH PE operation struct provides ways to:
 * - enable/disable EEH functionality;
 * - unfreeze IO/DMA for frozen PE;
 * - read PE state;
 * - reset PE;
 * - configure PE;
 * - inject EEH error.
 */
struct vfio_eeh_pe_err {
	__u32 type;
	__u32 func;
	__u64 addr;
	__u64 mask;
};

struct vfio_eeh_pe_op {
	__u32 argsz;
	__u32 flags;
	__u32 op;
	union {
		struct vfio_eeh_pe_err err;
	};
};

#define VFIO_EEH_PE_DISABLE		0	/* Disable EEH functionality */
#define VFIO_EEH_PE_ENABLE		1	/* Enable EEH functionality  */
#define VFIO_EEH_PE_UNFREEZE_IO		2	/* Enable IO for frozen PE   */
#define VFIO_EEH_PE_UNFREEZE_DMA	3	/* Enable DMA for frozen PE  */
#define VFIO_EEH_PE_GET_STATE		4	/* PE state retrieval        */
#define  VFIO_EEH_PE_STATE_NORMAL	0	/* PE in functional state    */
#define  VFIO_EEH_PE_STATE_RESET	1	/* PE reset in progress      */
#define  VFIO_EEH_PE_STATE_STOPPED	2	/* Stopped DMA and IO        */
#define  VFIO_EEH_PE_STATE_STOPPED_DMA	4	/* Stopped DMA only          */
#define  VFIO_EEH_PE_STATE_UNAVAIL	5	/* State unavailable         */
#define VFIO_EEH_PE_RESET_DEACTIVATE	5	/* Deassert PE reset         */
#define VFIO_EEH_PE_RESET_HOT		6	/* Assert hot reset          */
#define VFIO_EEH_PE_RESET_FUNDAMENTAL	7	/* Assert fundamental reset  */
#define VFIO_EEH_PE_CONFIGURE		8	/* PE configuration          */
#define VFIO_EEH_PE_INJECT_ERR		9	/* Inject EEH error          */

#define VFIO_EEH_PE_OP			_IO(VFIO_TYPE, VFIO_BASE + 21)

/**
 * VFIO_IOMMU_SPAPR_REGISTER_MEMORY - _IOW(VFIO_TYPE, VFIO_BASE + 17, struct vfio_iommu_spapr_register_memory)
 *
 * Registers user space memory where DMA is allowed. It pins
 * user pages and does the locked memory accounting so
 * subsequent VFIO_IOMMU_MAP_DMA/VFIO_IOMMU_UNMAP_DMA calls
 * get faster.
 */
struct vfio_iommu_spapr_register_memory {
	__u32	argsz;
	__u32	flags;
	__u64	vaddr;				/* Process virtual address */
	__u64	size;				/* Size of mapping (bytes) */
};
#define VFIO_IOMMU_SPAPR_REGISTER_MEMORY	_IO(VFIO_TYPE, VFIO_BASE + 17)

/**
 * VFIO_IOMMU_SPAPR_UNREGISTER_MEMORY - _IOW(VFIO_TYPE, VFIO_BASE + 18, struct vfio_iommu_spapr_register_memory)
 *
 * Unregisters user space memory registered with
 * VFIO_IOMMU_SPAPR_REGISTER_MEMORY.
 * Uses vfio_iommu_spapr_register_memory for parameters.
 */
#define VFIO_IOMMU_SPAPR_UNREGISTER_MEMORY	_IO(VFIO_TYPE, VFIO_BASE + 18)

/**
 * VFIO_IOMMU_SPAPR_TCE_CREATE - _IOWR(VFIO_TYPE, VFIO_BASE + 19, struct vfio_iommu_spapr_tce_create)
 *
 * Creates an additional TCE table and programs it (sets a new DMA window)
 * to every IOMMU group in the container. It receives page shift, window
 * size and number of levels in the TCE table being created.
 *
 * It allocates and returns an offset on a PCI bus of the new DMA window.
 */
struct vfio_iommu_spapr_tce_create {
	__u32 argsz;
	__u32 flags;
	/* in */
	__u32 page_shift;
	__u32 __resv1;
	__u64 window_size;
	__u32 levels;
	__u32 __resv2;
	/* out */
	__u64 start_addr;
};
#define VFIO_IOMMU_SPAPR_TCE_CREATE	_IO(VFIO_TYPE, VFIO_BASE + 19)

/**
 * VFIO_IOMMU_SPAPR_TCE_REMOVE - _IOW(VFIO_TYPE, VFIO_BASE + 20, struct vfio_iommu_spapr_tce_remove)
 *
 * Unprograms a TCE table from all groups in the container and destroys it.
 * It receives a PCI bus offset as a window id.
 */
struct vfio_iommu_spapr_tce_remove {
	__u32 argsz;
	__u32 flags;
	/* in */
	__u64 start_addr;
};
#define VFIO_IOMMU_SPAPR_TCE_REMOVE	_IO(VFIO_TYPE, VFIO_BASE + 20)

/* ***************************************************************** */

#endif /* _UAPIVFIO_H */
