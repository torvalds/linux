/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_VDUSE_H_
#define _UAPI_VDUSE_H_

#include <linux/types.h>

#define VDUSE_BASE	0x81

/* The ioctls for control device (/dev/vduse/control) */

#define VDUSE_API_VERSION	0

/*
 * Get the version of VDUSE API that kernel supported (VDUSE_API_VERSION).
 * This is used for future extension.
 */
#define VDUSE_GET_API_VERSION	_IOR(VDUSE_BASE, 0x00, __u64)

/* Set the version of VDUSE API that userspace supported. */
#define VDUSE_SET_API_VERSION	_IOW(VDUSE_BASE, 0x01, __u64)

/**
 * struct vduse_dev_config - basic configuration of a VDUSE device
 * @name: VDUSE device name, needs to be NUL terminated
 * @vendor_id: virtio vendor id
 * @device_id: virtio device id
 * @features: virtio features
 * @vq_num: the number of virtqueues
 * @vq_align: the allocation alignment of virtqueue's metadata
 * @reserved: for future use, needs to be initialized to zero
 * @config_size: the size of the configuration space
 * @config: the buffer of the configuration space
 *
 * Structure used by VDUSE_CREATE_DEV ioctl to create VDUSE device.
 */
struct vduse_dev_config {
#define VDUSE_NAME_MAX	256
	char name[VDUSE_NAME_MAX];
	__u32 vendor_id;
	__u32 device_id;
	__u64 features;
	__u32 vq_num;
	__u32 vq_align;
	__u32 reserved[13];
	__u32 config_size;
	__u8 config[];
};

/* Create a VDUSE device which is represented by a char device (/dev/vduse/$NAME) */
#define VDUSE_CREATE_DEV	_IOW(VDUSE_BASE, 0x02, struct vduse_dev_config)

/*
 * Destroy a VDUSE device. Make sure there are no more references
 * to the char device (/dev/vduse/$NAME).
 */
#define VDUSE_DESTROY_DEV	_IOW(VDUSE_BASE, 0x03, char[VDUSE_NAME_MAX])

/* The ioctls for VDUSE device (/dev/vduse/$NAME) */

/**
 * struct vduse_iotlb_entry - entry of IOTLB to describe one IOVA region [start, last]
 * @offset: the mmap offset on returned file descriptor
 * @start: start of the IOVA region
 * @last: last of the IOVA region
 * @perm: access permission of the IOVA region
 *
 * Structure used by VDUSE_IOTLB_GET_FD ioctl to find an overlapped IOVA region.
 */
struct vduse_iotlb_entry {
	__u64 offset;
	__u64 start;
	__u64 last;
#define VDUSE_ACCESS_RO 0x1
#define VDUSE_ACCESS_WO 0x2
#define VDUSE_ACCESS_RW 0x3
	__u8 perm;
};

/*
 * Find the first IOVA region that overlaps with the range [start, last]
 * and return the corresponding file descriptor. Return -EINVAL means the
 * IOVA region doesn't exist. Caller should set start and last fields.
 */
#define VDUSE_IOTLB_GET_FD	_IOWR(VDUSE_BASE, 0x10, struct vduse_iotlb_entry)

/*
 * Get the negotiated virtio features. It's a subset of the features in
 * struct vduse_dev_config which can be accepted by virtio driver. It's
 * only valid after FEATURES_OK status bit is set.
 */
#define VDUSE_DEV_GET_FEATURES	_IOR(VDUSE_BASE, 0x11, __u64)

/**
 * struct vduse_config_data - data used to update configuration space
 * @offset: the offset from the beginning of configuration space
 * @length: the length to write to configuration space
 * @buffer: the buffer used to write from
 *
 * Structure used by VDUSE_DEV_SET_CONFIG ioctl to update device
 * configuration space.
 */
struct vduse_config_data {
	__u32 offset;
	__u32 length;
	__u8 buffer[];
};

/* Set device configuration space */
#define VDUSE_DEV_SET_CONFIG	_IOW(VDUSE_BASE, 0x12, struct vduse_config_data)

/*
 * Inject a config interrupt. It's usually used to notify virtio driver
 * that device configuration space has changed.
 */
#define VDUSE_DEV_INJECT_CONFIG_IRQ	_IO(VDUSE_BASE, 0x13)

/**
 * struct vduse_vq_config - basic configuration of a virtqueue
 * @index: virtqueue index
 * @max_size: the max size of virtqueue
 * @reserved: for future use, needs to be initialized to zero
 *
 * Structure used by VDUSE_VQ_SETUP ioctl to setup a virtqueue.
 */
struct vduse_vq_config {
	__u32 index;
	__u16 max_size;
	__u16 reserved[13];
};

/*
 * Setup the specified virtqueue. Make sure all virtqueues have been
 * configured before the device is attached to vDPA bus.
 */
#define VDUSE_VQ_SETUP		_IOW(VDUSE_BASE, 0x14, struct vduse_vq_config)

/**
 * struct vduse_vq_state_split - split virtqueue state
 * @avail_index: available index
 */
struct vduse_vq_state_split {
	__u16 avail_index;
};

/**
 * struct vduse_vq_state_packed - packed virtqueue state
 * @last_avail_counter: last driver ring wrap counter observed by device
 * @last_avail_idx: device available index
 * @last_used_counter: device ring wrap counter
 * @last_used_idx: used index
 */
struct vduse_vq_state_packed {
	__u16 last_avail_counter;
	__u16 last_avail_idx;
	__u16 last_used_counter;
	__u16 last_used_idx;
};

/**
 * struct vduse_vq_info - information of a virtqueue
 * @index: virtqueue index
 * @num: the size of virtqueue
 * @desc_addr: address of desc area
 * @driver_addr: address of driver area
 * @device_addr: address of device area
 * @split: split virtqueue state
 * @packed: packed virtqueue state
 * @ready: ready status of virtqueue
 *
 * Structure used by VDUSE_VQ_GET_INFO ioctl to get virtqueue's information.
 */
struct vduse_vq_info {
	__u32 index;
	__u32 num;
	__u64 desc_addr;
	__u64 driver_addr;
	__u64 device_addr;
	union {
		struct vduse_vq_state_split split;
		struct vduse_vq_state_packed packed;
	};
	__u8 ready;
};

/* Get the specified virtqueue's information. Caller should set index field. */
#define VDUSE_VQ_GET_INFO	_IOWR(VDUSE_BASE, 0x15, struct vduse_vq_info)

/**
 * struct vduse_vq_eventfd - eventfd configuration for a virtqueue
 * @index: virtqueue index
 * @fd: eventfd, -1 means de-assigning the eventfd
 *
 * Structure used by VDUSE_VQ_SETUP_KICKFD ioctl to setup kick eventfd.
 */
struct vduse_vq_eventfd {
	__u32 index;
#define VDUSE_EVENTFD_DEASSIGN -1
	int fd;
};

/*
 * Setup kick eventfd for specified virtqueue. The kick eventfd is used
 * by VDUSE kernel module to notify userspace to consume the avail vring.
 */
#define VDUSE_VQ_SETUP_KICKFD	_IOW(VDUSE_BASE, 0x16, struct vduse_vq_eventfd)

/*
 * Inject an interrupt for specific virtqueue. It's used to notify virtio driver
 * to consume the used vring.
 */
#define VDUSE_VQ_INJECT_IRQ	_IOW(VDUSE_BASE, 0x17, __u32)

/* The control messages definition for read(2)/write(2) on /dev/vduse/$NAME */

/**
 * enum vduse_req_type - request type
 * @VDUSE_GET_VQ_STATE: get the state for specified virtqueue from userspace
 * @VDUSE_SET_STATUS: set the device status
 * @VDUSE_UPDATE_IOTLB: Notify userspace to update the memory mapping for
 *                      specified IOVA range via VDUSE_IOTLB_GET_FD ioctl
 */
enum vduse_req_type {
	VDUSE_GET_VQ_STATE,
	VDUSE_SET_STATUS,
	VDUSE_UPDATE_IOTLB,
};

/**
 * struct vduse_vq_state - virtqueue state
 * @index: virtqueue index
 * @split: split virtqueue state
 * @packed: packed virtqueue state
 */
struct vduse_vq_state {
	__u32 index;
	union {
		struct vduse_vq_state_split split;
		struct vduse_vq_state_packed packed;
	};
};

/**
 * struct vduse_dev_status - device status
 * @status: device status
 */
struct vduse_dev_status {
	__u8 status;
};

/**
 * struct vduse_iova_range - IOVA range [start, last]
 * @start: start of the IOVA range
 * @last: last of the IOVA range
 */
struct vduse_iova_range {
	__u64 start;
	__u64 last;
};

/**
 * struct vduse_dev_request - control request
 * @type: request type
 * @request_id: request id
 * @reserved: for future use
 * @vq_state: virtqueue state, only index field is available
 * @s: device status
 * @iova: IOVA range for updating
 * @padding: padding
 *
 * Structure used by read(2) on /dev/vduse/$NAME.
 */
struct vduse_dev_request {
	__u32 type;
	__u32 request_id;
	__u32 reserved[4];
	union {
		struct vduse_vq_state vq_state;
		struct vduse_dev_status s;
		struct vduse_iova_range iova;
		__u32 padding[32];
	};
};

/**
 * struct vduse_dev_response - response to control request
 * @request_id: corresponding request id
 * @result: the result of request
 * @reserved: for future use, needs to be initialized to zero
 * @vq_state: virtqueue state
 * @padding: padding
 *
 * Structure used by write(2) on /dev/vduse/$NAME.
 */
struct vduse_dev_response {
	__u32 request_id;
#define VDUSE_REQ_RESULT_OK	0x00
#define VDUSE_REQ_RESULT_FAILED	0x01
	__u32 result;
	__u32 reserved[4];
	union {
		struct vduse_vq_state vq_state;
		__u32 padding[32];
	};
};

#endif /* _UAPI_VDUSE_H_ */
