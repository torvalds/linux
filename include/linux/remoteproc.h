/*
 * Remote Processor Framework
 *
 * Copyright(c) 2011 Texas Instruments, Inc.
 * Copyright(c) 2011 Google, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name Texas Instruments nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef REMOTEPROC_H
#define REMOTEPROC_H

#include <linux/types.h>
#include <linux/kref.h>
#include <linux/klist.h>
#include <linux/mutex.h>
#include <linux/virtio.h>
#include <linux/completion.h>

/*
 * The alignment between the consumer and producer parts of the vring.
 * Note: this is part of the "wire" protocol. If you change this, you need
 * to update your peers too.
 */
#define AMP_VRING_ALIGN	(4096)

/**
 * struct fw_resource - describes an entry from the resource section
 * @type: resource type
 * @id: index number of the resource
 * @da: device address of the resource
 * @pa: physical address of the resource
 * @len: size, in bytes, of the resource
 * @flags: properties of the resource, e.g. iommu protection required
 * @reserved: must be 0 atm
 * @name: name of resource
 *
 * The remote processor firmware should contain a "resource table":
 * array of 'struct fw_resource' entries.
 *
 * Some resources entries are mere announcements, where the host is informed
 * of specific remoteproc configuration. Other entries require the host to
 * do something (e.g. reserve a requested resource) and possibly also reply
 * by overwriting a member inside 'struct fw_resource' with info about the
 * allocated resource.
 *
 * Different resource entries use different members of this struct,
 * with different meanings. This is pretty limiting and error-prone,
 * so the plan is to move to variable-length TLV-based resource entries,
 * where each resource type will have its own structure.
 */
struct fw_resource {
	u32 type;
	u32 id;
	u64 da;
	u64 pa;
	u32 len;
	u32 flags;
	u8 reserved[16];
	u8 name[48];
} __packed;

/**
 * enum fw_resource_type - types of resource entries
 *
 * @RSC_CARVEOUT:   request for allocation of a physically contiguous
 *		    memory region.
 * @RSC_DEVMEM:     request to iommu_map a memory-based peripheral.
 * @RSC_TRACE:	    announces the availability of a trace buffer into which
 *		    the remote processor will be writing logs. In this case,
 *		    'da' indicates the device address where logs are written to,
 *		    and 'len' is the size of the trace buffer.
 * @RSC_VRING:	    request for allocation of a virtio vring (address should
 *		    be indicated in 'da', and 'len' should contain the number
 *		    of buffers supported by the vring).
 * @RSC_VIRTIO_DEV: this entry declares about support for a virtio device,
 *		    and serves as the virtio header. 'da' holds the
 *		    the virtio device features, 'pa' holds the virtio guest
 *		    features, 'len' holds the virtio status, and 'flags' holds
 *		    the virtio id (currently only VIRTIO_ID_RPMSG is supported).
 * @RSC_LAST:       just keep this one at the end
 *
 * Most of the resource entries share the basic idea of address/length
 * negotiation with the host: the firmware usually asks (on behalf of the
 * remote processor that will soon be booted with it) for memory
 * of size 'len' bytes, and the host needs to allocate it and provide
 * the device/physical address (when relevant) in 'da'/'pa' respectively.
 *
 * If the firmware is compiled with hard coded device addresses, and
 * can't handle dynamically allocated 'da' values, then the 'da' field
 * will contain the expected device addresses (today we actually only support
 * this scheme, as there aren't yet any use cases for dynamically allocated
 * device addresses).
 *
 * Please note that these values are used as indices to the rproc_handle_rsc
 * lookup table, so please keep them sane. Moreover, @RSC_LAST is used to
 * check the validity of an index before the lookup table is accessed, so
 * please update it as needed.
 */
enum fw_resource_type {
	RSC_CARVEOUT	= 0,
	RSC_DEVMEM	= 1,
	RSC_TRACE	= 2,
	RSC_VRING	= 3,
	RSC_VIRTIO_DEV	= 4,
	RSC_LAST	= 5,
};

/**
 * struct rproc_mem_entry - memory entry descriptor
 * @va:	virtual address
 * @dma: dma address
 * @len: length, in bytes
 * @da: device address
 * @priv: associated data
 * @node: list node
 */
struct rproc_mem_entry {
	void *va;
	dma_addr_t dma;
	int len;
	u64 da;
	void *priv;
	struct list_head node;
};

struct rproc;

/**
 * struct rproc_ops - platform-specific device handlers
 * @start:	power on the device and boot it
 * @stop:	power off the device
 * @kick:	kick a virtqueue (virtqueue id given as a parameter)
 */
struct rproc_ops {
	int (*start)(struct rproc *rproc);
	int (*stop)(struct rproc *rproc);
	void (*kick)(struct rproc *rproc, int vqid);
};

/**
 * enum rproc_state - remote processor states
 * @RPROC_OFFLINE:	device is powered off
 * @RPROC_SUSPENDED:	device is suspended; needs to be woken up to receive
 *			a message.
 * @RPROC_RUNNING:	device is up and running
 * @RPROC_CRASHED:	device has crashed; need to start recovery
 * @RPROC_LAST:		just keep this one at the end
 *
 * Please note that the values of these states are used as indices
 * to rproc_state_string, a state-to-name lookup table,
 * so please keep the two synchronized. @RPROC_LAST is used to check
 * the validity of an index before the lookup table is accessed, so
 * please update it as needed too.
 */
enum rproc_state {
	RPROC_OFFLINE	= 0,
	RPROC_SUSPENDED	= 1,
	RPROC_RUNNING	= 2,
	RPROC_CRASHED	= 3,
	RPROC_LAST	= 4,
};

/**
 * struct rproc - represents a physical remote processor device
 * @node: klist node of this rproc object
 * @domain: iommu domain
 * @name: human readable name of the rproc
 * @firmware: name of firmware file to be loaded
 * @priv: private data which belongs to the platform-specific rproc module
 * @ops: platform-specific start/stop rproc handlers
 * @dev: underlying device
 * @refcount: refcount of users that have a valid pointer to this rproc
 * @power: refcount of users who need this rproc powered up
 * @state: state of the device
 * @lock: lock which protects concurrent manipulations of the rproc
 * @dbg_dir: debugfs directory of this rproc device
 * @traces: list of trace buffers
 * @num_traces: number of trace buffers
 * @carveouts: list of physically contiguous memory allocations
 * @mappings: list of iommu mappings we initiated, needed on shutdown
 * @firmware_loading_complete: marks e/o asynchronous firmware loading
 * @bootaddr: address of first instruction to boot rproc with (optional)
 * @rvdev: virtio device (we only support a single rpmsg virtio device for now)
 */
struct rproc {
	struct klist_node node;
	struct iommu_domain *domain;
	const char *name;
	const char *firmware;
	void *priv;
	const struct rproc_ops *ops;
	struct device *dev;
	struct kref refcount;
	atomic_t power;
	unsigned int state;
	struct mutex lock;
	struct dentry *dbg_dir;
	struct list_head traces;
	int num_traces;
	struct list_head carveouts;
	struct list_head mappings;
	struct completion firmware_loading_complete;
	u64 bootaddr;
	struct rproc_vdev *rvdev;
};

/**
 * struct rproc_vdev - remoteproc state for a supported virtio device
 * @rproc: the rproc handle
 * @vdev: the virio device
 * @vq: the virtqueues for this vdev
 * @vring: the vrings for this vdev
 * @dfeatures: virtio device features
 * @gfeatures: virtio guest features
 */
struct rproc_vdev {
	struct rproc *rproc;
	struct virtio_device vdev;
	struct virtqueue *vq[2];
	struct rproc_mem_entry vring[2];
	unsigned long dfeatures;
	unsigned long gfeatures;
};

struct rproc *rproc_get_by_name(const char *name);
void rproc_put(struct rproc *rproc);

struct rproc *rproc_alloc(struct device *dev, const char *name,
				const struct rproc_ops *ops,
				const char *firmware, int len);
void rproc_free(struct rproc *rproc);
int rproc_register(struct rproc *rproc);
int rproc_unregister(struct rproc *rproc);

int rproc_boot(struct rproc *rproc);
void rproc_shutdown(struct rproc *rproc);

static inline struct rproc *vdev_to_rproc(struct virtio_device *vdev)
{
	struct rproc_vdev *rvdev = container_of(vdev, struct rproc_vdev, vdev);

	return rvdev->rproc;
}

#endif /* REMOTEPROC_H */
