/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_LINUX_GUNYAH
#define _UAPI_LINUX_GUNYAH

/*
 * Userspace interface for /dev/gunyah - gunyah based virtual machine
 *
 * Note: this interface is considered experimental and may change without
 *       notice.
 */

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/virtio_types.h>

#define GH_IOCTL_TYPE			0xB2

/*
 * fw_name is used to find the secure VM image by name to be loaded.
 */
#define GH_VM_FW_NAME_MAX		16

/** @struct gh_fw_name
 * A structure to be passed to GH_VM_SET_FM_NAME ioctl
 * @name - name of the secure VM image
 */
struct gh_fw_name {
	char name[GH_VM_FW_NAME_MAX];
};

#define VBE_ASSIGN_IOEVENTFD	1
#define VBE_DEASSIGN_IOEVENTFD	2

#define VBE_ASSIGN_IRQFD	1
#define VBE_DEASSIGN_IRQFD	2

#define EVENT_NEW_BUFFER	1
#define EVENT_RESET_RQST	2
#define EVENT_INTERRUPT_ACK	4
#define EVENT_DRIVER_OK		8
#define EVENT_DRIVER_FAILED	0x10
#define EVENT_MODULE_EXIT	0x20
#define EVENT_VM_EXIT		0x40
#define EVENT_APP_EXIT		0x100

/*
 * gh_vm_exit_reasons specifies the various reasons why
 * the secondary VM ended its execution. VCPU_RUN returns these values
 * to userspace.
 */
#define GH_VM_EXIT_REASON_UNKNOWN		0
#define GH_VM_EXIT_REASON_SHUTDOWN		1
#define GH_VM_EXIT_REASON_RESTART		2
#define GH_VM_EXIT_REASON_PANIC			3
#define GH_VM_EXIT_REASON_NSWD			4
#define GH_VM_EXIT_REASON_HYP_ERROR		5
#define GH_VM_EXIT_REASON_ASYNC_EXT_ABORT	6
#define GH_VM_EXIT_REASON_FORCE_STOPPED		7
#define GH_VM_EXIT_REASONS_MAX			8

/*
 * ioctls for /dev/gunyah fds:
 */
/**
 * GH_CREATE_VM - Driver creates a VM sepecific structure. An anon file is
 *		  also created per VM. This would be the first IOCTL made
 *		  on /dev/gunyah node to obtain a per VM fd for futher
 *		  VM specific operations like VCPU creation, memory etc.
 *
 * Return: an fd for the per VM file created, -errno on failure
 */
#define GH_CREATE_VM			_IO(GH_IOCTL_TYPE, 0x01)

/*
 * ioctls for VM fd.
 */
/**
 * GH_CREATE_VCPU - Driver creates a VCPU sepecific structure. It takes
 *		    vcpu id as the input. This also creates an anon file
 *		    per vcpu which is used for further vcpu specific
 *		    operations.
 *
 * Return: an fd for the per VCPU file created, -errno on failure
 */
#define GH_CREATE_VCPU			_IO(GH_IOCTL_TYPE, 0x40)
/*
 * ioctls for VM properties
 */
/**
 * GH_VM_SET_FW_NAME - Userspace will specify the name of the firmware
 *		       image that needs to be loaded into VM's memory
 *		       after authentication. The loaded VM memory details
 *		       are forwarded to Gunyah Hypervisor underneath.
 *
 * Input: gh_fw_name structure with Secure VM name as name attribute of
 *        the struct.
 * Return: 0 if success, -errno on failure
 */
#define GH_VM_SET_FW_NAME		_IOW(GH_IOCTL_TYPE, 0x41, struct gh_fw_name)
/**
 * GH_VM_GET_FW_NAME - Userspace can use this IOCTL to query the name of
 *		       the secure VM image that was loaded.
 *
 * Input: gh_fw_name structure to be filled with Secure VM name as the
 *	  name attribute of the struct.
 * Return: 0 if success and firmware name in struct fw_name that
 *         represents the firmware image name currently associated with
 *         the VM if a call to GH_VM_SET_FW_NAME ioctl previously was
 *         successful, -errno on failure
 */
#define GH_VM_GET_FW_NAME		_IOR(GH_IOCTL_TYPE, 0x42, struct gh_fw_name)
/**
 * GH_VM_GET_VCPU_COUNT - Userspace can use this IOCTL to query the number
 *			  of vcpus that are supported for the VM. Userspace
 *			  can further use this count to create VCPUs.
 *
 * Return: nr_vcpus for proxy scheduled VMs, 1 for hypervisor scheduled VMs,
 *         -errno on failure
 */
#define GH_VM_GET_VCPU_COUNT		_IO(GH_IOCTL_TYPE, 0x43)
/*
 *  IOCTLs supported by virtio backend driver
 */
/**
 * GH_GET_SHARED_MEMORY_SIZE - Userspace can use this IOCTL to query the virtio
 *                             shared memory size of the VM. Userpsace can use
 *                             it for mmap.
 *
 * Input: 64 bit unsigned integer variable to be filled with shared memory size.
 *
 * Return: 0 if success with shared memory size as u64 in the third argument,
 *         -errno on failure
 */
#define GH_GET_SHARED_MEMORY_SIZE	_IOR(GH_IOCTL_TYPE, 0x61, __u64)
/**
 * GH_IOEVENTFD - Eventfd created in userspace is passed to kernel using this
 *                ioctl. Userspace is signalled by virtio backend driver through
 *                this fd when data is available in the ring.
 *
 * Input: virtio_eventfd structure with required attributes.
 *
 * Return: 0 if success, -errno on failure
 */
#define GH_IOEVENTFD		_IOW(GH_IOCTL_TYPE, 0x62, \
						struct virtio_eventfd)
/**
 * GH_IRQFD - Eventfd created in userspace is passed to kernel using this ioctl.
 *            Virtio backned driver is signalled by userspace using this fd when
 *            the ring is serviced.
 *
 * Input: virtio_irqfd structure with required attributes.
 *
 * Return: 0 if success, -errno on failure
 */
#define GH_IRQFD			_IOW(GH_IOCTL_TYPE, 0x63, \
						struct virtio_irqfd)
/**
 * GH_WAIT_FOR_EVENT - Userspace waits for events from the virtio backend driver
 *                     for indefinite time. For example when hypervisor detects
 *                     a DRIVER_OK event, it is passed to userspace using this
 *                     ioctl.
 *
 * Input: virtio_event structure with required attributes.
 *
 * Return: 0 if success, with the event data in struct virtio_event
 *         -errno on failure
 */
#define GH_WAIT_FOR_EVENT        _IOWR(GH_IOCTL_TYPE, 0x64, \
						struct virtio_event)

/**
 * GH_SET_DEVICE_FEATURES - This ioctl writes virtio device features supported
 *                          by the userspace to a page that is shared with
 *                          guest VM.
 *
 * Input: virtio_dev_features structure with required attributes.
 *
 * Return: 0 if success, -errno on failure
 */
#define GH_SET_DEVICE_FEATURES        _IOW(GH_IOCTL_TYPE, 0x65, \
						struct virtio_dev_features)
/**
 * GH_SET_QUEUE_NUM_MAX - This ioctl writes max virtio queue size supported by
 *                        the userspace to a page that is shared with guest VM.
 *
 * Input: virtio_queue_max structure with required attributes.
 *
 * Return: 0 if success, -errno on failure
 */
#define GH_SET_QUEUE_NUM_MAX        _IOW(GH_IOCTL_TYPE, 0x66, \
						struct virtio_queue_max)
/**
 * GH_SET_DEVICE_CONFIG_DATA - This ioctl writes device configuration data
 *                             to a page that is shared with guest VM.
 *
 * Input: virtio_config_data structure with required attributes.
 *
 * Return: 0 if success, -errno on failure
 */
#define GH_SET_DEVICE_CONFIG_DATA        _IOW(GH_IOCTL_TYPE, 0x67, \
						struct virtio_config_data)
/**
 * GH_GET_DRIVER_CONFIG_DATA - This ioctl reads the driver supported virtio
 *                             device configuration data from a page that is
 *                             shared with guest VM.
 *
 * Input: virtio_config_data structure with required attributes.
 *
 * Return: 0 if success with driver config data in struct virtio_config_data,
 *         -errno on failure
 */
#define GH_GET_DRIVER_CONFIG_DATA        _IOWR(GH_IOCTL_TYPE, 0x68, \
						struct virtio_config_data)
/**
 * GH_GET_QUEUE_INFO - This ioctl reads the driver supported virtqueue info from
 *                     a page that is shared with guest VM.
 *
 * Input: virtio_queue_info structure with required attributes.
 *
 * Return: 0 if success with virtqueue info in struct virtio_queue_info,
 *         -errno on failure
 */
#define GH_GET_QUEUE_INFO        _IOWR(GH_IOCTL_TYPE, 0x69, \
						struct virtio_queue_info)
/**
 * GH_GET_DRIVER_FEATURES - This ioctl reads the driver supported features from
 *                          a page that is shared with guest VM.
 *
 * Input: virtio_driver_features structure with required attributes.
 *
 * Return: 0 if success with driver features in struct virtio_driver_features,
 *         -errno on failure
 */
#define GH_GET_DRIVER_FEATURES        _IOWR(GH_IOCTL_TYPE, 0x6a, \
						struct virtio_driver_features)
/**
 * GH_ACK_DRIVER_OK - This ioctl acknowledges the DRIVER_OK event from virtio
 *                    backend driver.
 *
 * Input: 32 bit unsigned integer virtio device label.
 *
 * Return: 0 if success, -errno on failure
 */
#define GH_ACK_DRIVER_OK        _IOWR(GH_IOCTL_TYPE, 0x6b, __u32)
/**
 * GH_ACK_RESET - This ioctl acknowledges the RESET event from virtio
 *                backend driver.
 *
 * Input: virtio_ack_reset structure with required attributes.
 *
 * Return: 0 if success, -errno on failure
 */
#define GH_ACK_RESET		_IOW(GH_IOCTL_TYPE, 0x6d, struct virtio_ack_reset)

/*
 * ioctls for vcpu fd.
 */
/**
 * GH_VCPU_RUN - This command is used to run the vcpus created. VCPU_RUN
 *		 is called on vcpu fd created previously. VCPUs are
 *		 started individually if proxy scheduling is chosen as the
 *		 scheduling policy and vcpus are started simultaneously
 *		 in case of VMs whose scheduling is controlled by the
 *		 hypervisor. In the latter case, VCPU_RUN is blocked
 *		 until the VM terminates.
 *
 * Return: Reason for vm termination, -errno on failure
 */
#define GH_VCPU_RUN			_IO(GH_IOCTL_TYPE, 0x80)

struct virtio_ack_reset {
	__u32 label;
	__u32 reserved;
};

struct virtio_driver_features {
	__u32 label;
	__u32 reserved;
	__u32 features_sel;
	__u32 features;
};

struct virtio_queue_info {
	__u32 label;
	__u32 queue_sel;
	__u32 queue_num;
	__u32 queue_ready;
	__u64 queue_desc;
	__u64 queue_driver;
	__u64 queue_device;
};

struct virtio_config_data {
	__u32 label;
	__u32 config_size;
	__u64 config_data;
};

struct virtio_dev_features {
	__u32 label;
	__u32 reserved;
	__u32 features_sel;
	__u32 features;
};

struct virtio_queue_max {
	__u32 label;
	__u32 reserved;
	__u32 queue_sel;
	__u32 queue_num_max;
};

struct virtio_event {
	__u32 label;
	__u32 event;
	__u32 event_data;
	__u32 reserved;
};

struct virtio_eventfd {
	__u32 label;
	__u32 flags;
	__u32 queue_num;
	__s32 fd;
};

struct virtio_irqfd {
	__u32 label;
	__u32 flags;
	__s32 fd;
	__u32 reserved;
};

#endif /* _UAPI_LINUX_GUNYAH */
