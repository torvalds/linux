/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_VHOST_H
#define _LINUX_VHOST_H
/* Userspace interface for in-kernel virtio accelerators. */

/* vhost is used to reduce the number of system calls involved in virtio.
 *
 * Existing virtio net code is used in the guest without modification.
 *
 * This header includes interface used by userspace hypervisor for
 * device configuration.
 */

#include <linux/vhost_types.h>
#include <linux/types.h>
#include <linux/ioctl.h>

#define VHOST_FILE_UNBIND -1

/* ioctls */

#define VHOST_VIRTIO 0xAF

/* Features bitmask for forward compatibility.  Transport bits are used for
 * vhost specific features. */
#define VHOST_GET_FEATURES	_IOR(VHOST_VIRTIO, 0x00, __u64)
#define VHOST_SET_FEATURES	_IOW(VHOST_VIRTIO, 0x00, __u64)

/* Set current process as the (exclusive) owner of this file descriptor.  This
 * must be called before any other vhost command.  Further calls to
 * VHOST_OWNER_SET fail until VHOST_OWNER_RESET is called. */
#define VHOST_SET_OWNER _IO(VHOST_VIRTIO, 0x01)
/* Give up ownership, and reset the device to default values.
 * Allows subsequent call to VHOST_OWNER_SET to succeed. */
#define VHOST_RESET_OWNER _IO(VHOST_VIRTIO, 0x02)

/* Set up/modify memory layout */
#define VHOST_SET_MEM_TABLE	_IOW(VHOST_VIRTIO, 0x03, struct vhost_memory)

/* Write logging setup. */
/* Memory writes can optionally be logged by setting bit at an offset
 * (calculated from the physical address) from specified log base.
 * The bit is set using an atomic 32 bit operation. */
/* Set base address for logging. */
#define VHOST_SET_LOG_BASE _IOW(VHOST_VIRTIO, 0x04, __u64)
/* Specify an eventfd file descriptor to signal on log write. */
#define VHOST_SET_LOG_FD _IOW(VHOST_VIRTIO, 0x07, int)
/* By default, a device gets one vhost_worker that its virtqueues share. This
 * command allows the owner of the device to create an additional vhost_worker
 * for the device. It can later be bound to 1 or more of its virtqueues using
 * the VHOST_ATTACH_VRING_WORKER command.
 *
 * This must be called after VHOST_SET_OWNER and the caller must be the owner
 * of the device. The new thread will inherit caller's cgroups and namespaces,
 * and will share the caller's memory space. The new thread will also be
 * counted against the caller's RLIMIT_NPROC value.
 *
 * The worker's ID used in other commands will be returned in
 * vhost_worker_state.
 */
#define VHOST_NEW_WORKER _IOR(VHOST_VIRTIO, 0x8, struct vhost_worker_state)
/* Free a worker created with VHOST_NEW_WORKER if it's not attached to any
 * virtqueue. If userspace is not able to call this for workers its created,
 * the kernel will free all the device's workers when the device is closed.
 */
#define VHOST_FREE_WORKER _IOW(VHOST_VIRTIO, 0x9, struct vhost_worker_state)

/* Ring setup. */
/* Set number of descriptors in ring. This parameter can not
 * be modified while ring is running (bound to a device). */
#define VHOST_SET_VRING_NUM _IOW(VHOST_VIRTIO, 0x10, struct vhost_vring_state)
/* Set addresses for the ring. */
#define VHOST_SET_VRING_ADDR _IOW(VHOST_VIRTIO, 0x11, struct vhost_vring_addr)
/* Base value where queue looks for available descriptors */
#define VHOST_SET_VRING_BASE _IOW(VHOST_VIRTIO, 0x12, struct vhost_vring_state)
/* Get accessor: reads index, writes value in num */
#define VHOST_GET_VRING_BASE _IOWR(VHOST_VIRTIO, 0x12, struct vhost_vring_state)

/* Set the vring byte order in num. Valid values are VHOST_VRING_LITTLE_ENDIAN
 * or VHOST_VRING_BIG_ENDIAN (other values return -EINVAL).
 * The byte order cannot be changed while the device is active: trying to do so
 * returns -EBUSY.
 * This is a legacy only API that is simply ignored when VIRTIO_F_VERSION_1 is
 * set.
 * Not all kernel configurations support this ioctl, but all configurations that
 * support SET also support GET.
 */
#define VHOST_VRING_LITTLE_ENDIAN 0
#define VHOST_VRING_BIG_ENDIAN 1
#define VHOST_SET_VRING_ENDIAN _IOW(VHOST_VIRTIO, 0x13, struct vhost_vring_state)
#define VHOST_GET_VRING_ENDIAN _IOW(VHOST_VIRTIO, 0x14, struct vhost_vring_state)
/* Attach a vhost_worker created with VHOST_NEW_WORKER to one of the device's
 * virtqueues.
 *
 * This will replace the virtqueue's existing worker. If the replaced worker
 * is no longer attached to any virtqueues, it can be freed with
 * VHOST_FREE_WORKER.
 */
#define VHOST_ATTACH_VRING_WORKER _IOW(VHOST_VIRTIO, 0x15,		\
				       struct vhost_vring_worker)
/* Return the vring worker's ID */
#define VHOST_GET_VRING_WORKER _IOWR(VHOST_VIRTIO, 0x16,		\
				     struct vhost_vring_worker)

/* The following ioctls use eventfd file descriptors to signal and poll
 * for events. */

/* Set eventfd to poll for added buffers */
#define VHOST_SET_VRING_KICK _IOW(VHOST_VIRTIO, 0x20, struct vhost_vring_file)
/* Set eventfd to signal when buffers have beed used */
#define VHOST_SET_VRING_CALL _IOW(VHOST_VIRTIO, 0x21, struct vhost_vring_file)
/* Set eventfd to signal an error */
#define VHOST_SET_VRING_ERR _IOW(VHOST_VIRTIO, 0x22, struct vhost_vring_file)
/* Set busy loop timeout (in us) */
#define VHOST_SET_VRING_BUSYLOOP_TIMEOUT _IOW(VHOST_VIRTIO, 0x23,	\
					 struct vhost_vring_state)
/* Get busy loop timeout (in us) */
#define VHOST_GET_VRING_BUSYLOOP_TIMEOUT _IOW(VHOST_VIRTIO, 0x24,	\
					 struct vhost_vring_state)

/* Set or get vhost backend capability */

#define VHOST_SET_BACKEND_FEATURES _IOW(VHOST_VIRTIO, 0x25, __u64)
#define VHOST_GET_BACKEND_FEATURES _IOR(VHOST_VIRTIO, 0x26, __u64)

/* VHOST_NET specific defines */

/* Attach virtio net ring to a raw socket, or tap device.
 * The socket must be already bound to an ethernet device, this device will be
 * used for transmit.  Pass fd -1 to unbind from the socket and the transmit
 * device.  This can be used to stop the ring (e.g. for migration). */
#define VHOST_NET_SET_BACKEND _IOW(VHOST_VIRTIO, 0x30, struct vhost_vring_file)

/* VHOST_SCSI specific defines */

#define VHOST_SCSI_SET_ENDPOINT _IOW(VHOST_VIRTIO, 0x40, struct vhost_scsi_target)
#define VHOST_SCSI_CLEAR_ENDPOINT _IOW(VHOST_VIRTIO, 0x41, struct vhost_scsi_target)
/* Changing this breaks userspace. */
#define VHOST_SCSI_GET_ABI_VERSION _IOW(VHOST_VIRTIO, 0x42, int)
/* Set and get the events missed flag */
#define VHOST_SCSI_SET_EVENTS_MISSED _IOW(VHOST_VIRTIO, 0x43, __u32)
#define VHOST_SCSI_GET_EVENTS_MISSED _IOW(VHOST_VIRTIO, 0x44, __u32)

/* VHOST_VSOCK specific defines */

#define VHOST_VSOCK_SET_GUEST_CID	_IOW(VHOST_VIRTIO, 0x60, __u64)
#define VHOST_VSOCK_SET_RUNNING		_IOW(VHOST_VIRTIO, 0x61, int)

/* VHOST_VDPA specific defines */

/* Get the device id. The device ids follow the same definition of
 * the device id defined in virtio-spec.
 */
#define VHOST_VDPA_GET_DEVICE_ID	_IOR(VHOST_VIRTIO, 0x70, __u32)
/* Get and set the status. The status bits follow the same definition
 * of the device status defined in virtio-spec.
 */
#define VHOST_VDPA_GET_STATUS		_IOR(VHOST_VIRTIO, 0x71, __u8)
#define VHOST_VDPA_SET_STATUS		_IOW(VHOST_VIRTIO, 0x72, __u8)
/* Get and set the device config. The device config follows the same
 * definition of the device config defined in virtio-spec.
 */
#define VHOST_VDPA_GET_CONFIG		_IOR(VHOST_VIRTIO, 0x73, \
					     struct vhost_vdpa_config)
#define VHOST_VDPA_SET_CONFIG		_IOW(VHOST_VIRTIO, 0x74, \
					     struct vhost_vdpa_config)
/* Enable/disable the ring. */
#define VHOST_VDPA_SET_VRING_ENABLE	_IOW(VHOST_VIRTIO, 0x75, \
					     struct vhost_vring_state)
/* Get the max ring size. */
#define VHOST_VDPA_GET_VRING_NUM	_IOR(VHOST_VIRTIO, 0x76, __u16)

/* Set event fd for config interrupt*/
#define VHOST_VDPA_SET_CONFIG_CALL	_IOW(VHOST_VIRTIO, 0x77, int)

/* Get the valid iova range */
#define VHOST_VDPA_GET_IOVA_RANGE	_IOR(VHOST_VIRTIO, 0x78, \
					     struct vhost_vdpa_iova_range)
/* Get the config size */
#define VHOST_VDPA_GET_CONFIG_SIZE	_IOR(VHOST_VIRTIO, 0x79, __u32)

/* Get the count of all virtqueues */
#define VHOST_VDPA_GET_VQS_COUNT	_IOR(VHOST_VIRTIO, 0x80, __u32)

/* Get the number of virtqueue groups. */
#define VHOST_VDPA_GET_GROUP_NUM	_IOR(VHOST_VIRTIO, 0x81, __u32)

/* Get the number of address spaces. */
#define VHOST_VDPA_GET_AS_NUM		_IOR(VHOST_VIRTIO, 0x7A, unsigned int)

/* Get the group for a virtqueue: read index, write group in num,
 * The virtqueue index is stored in the index field of
 * vhost_vring_state. The group for this specific virtqueue is
 * returned via num field of vhost_vring_state.
 */
#define VHOST_VDPA_GET_VRING_GROUP	_IOWR(VHOST_VIRTIO, 0x7B,	\
					      struct vhost_vring_state)
/* Set the ASID for a virtqueue group. The group index is stored in
 * the index field of vhost_vring_state, the ASID associated with this
 * group is stored at num field of vhost_vring_state.
 */
#define VHOST_VDPA_SET_GROUP_ASID	_IOW(VHOST_VIRTIO, 0x7C, \
					     struct vhost_vring_state)

/* Suspend a device so it does not process virtqueue requests anymore
 *
 * After the return of ioctl the device must preserve all the necessary state
 * (the virtqueue vring base plus the possible device specific states) that is
 * required for restoring in the future. The device must not change its
 * configuration after that point.
 */
#define VHOST_VDPA_SUSPEND		_IO(VHOST_VIRTIO, 0x7D)

/* Resume a device so it can resume processing virtqueue requests
 *
 * After the return of this ioctl the device will have restored all the
 * necessary states and it is fully operational to continue processing the
 * virtqueue descriptors.
 */
#define VHOST_VDPA_RESUME		_IO(VHOST_VIRTIO, 0x7E)

/* Get the group for the descriptor table including driver & device areas
 * of a virtqueue: read index, write group in num.
 * The virtqueue index is stored in the index field of vhost_vring_state.
 * The group ID of the descriptor table for this specific virtqueue
 * is returned via num field of vhost_vring_state.
 */
#define VHOST_VDPA_GET_VRING_DESC_GROUP	_IOWR(VHOST_VIRTIO, 0x7F,	\
					      struct vhost_vring_state)

/* Get the queue size of a specific virtqueue.
 * userspace set the vring index in vhost_vring_state.index
 * kernel set the queue size in vhost_vring_state.num
 */
#define VHOST_VDPA_GET_VRING_SIZE	_IOWR(VHOST_VIRTIO, 0x80,	\
					      struct vhost_vring_state)
#endif
