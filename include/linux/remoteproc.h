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
#include <linux/mutex.h>
#include <linux/virtio.h>
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/idr.h>
#include <linux/of.h>
#include <linux/rsc_table.h>

struct rproc;

/**
 * struct rproc_mem_entry - memory entry descriptor
 * @va:	virtual address
 * @is_iomem: io memory
 * @dma: dma address
 * @len: length, in bytes
 * @da: device address
 * @release: release associated memory
 * @priv: associated data
 * @name: associated memory region name (optional)
 * @node: list node
 * @rsc_offset: offset in resource table
 * @flags: iommu protection flags
 * @of_resm_idx: reserved memory phandle index
 * @alloc: specific memory allocator function
 */
struct rproc_mem_entry {
	void *va;
	bool is_iomem;
	dma_addr_t dma;
	size_t len;
	u32 da;
	void *priv;
	char name[32];
	struct list_head node;
	u32 rsc_offset;
	u32 flags;
	u32 of_resm_idx;
	int (*alloc)(struct rproc *rproc, struct rproc_mem_entry *mem);
	int (*release)(struct rproc *rproc, struct rproc_mem_entry *mem);
};

struct firmware;

/**
 * enum rsc_handling_status - return status of rproc_ops handle_rsc hook
 * @RSC_HANDLED:	resource was handled
 * @RSC_IGNORED:	resource was ignored
 */
enum rsc_handling_status {
	RSC_HANDLED	= 0,
	RSC_IGNORED	= 1,
};

/**
 * struct rproc_ops - platform-specific device handlers
 * @prepare:	prepare device for code loading
 * @unprepare:	unprepare device after stop
 * @start:	power on the device and boot it
 * @stop:	power off the device
 * @attach:	attach to a device that his already powered up
 * @detach:	detach from a device, leaving it powered up
 * @kick:	kick a virtqueue (virtqueue id given as a parameter)
 * @da_to_va:	optional platform hook to perform address translations
 * @parse_fw:	parse firmware to extract information (e.g. resource table)
 * @handle_rsc:	optional platform hook to handle vendor resources. Should return
 *		RSC_HANDLED if resource was handled, RSC_IGNORED if not handled
 *		and a negative value on error
 * @find_loaded_rsc_table: find the loaded resource table from firmware image
 * @get_loaded_rsc_table: get resource table installed in memory
 *			  by external entity
 * @load:		load firmware to memory, where the remote processor
 *			expects to find it
 * @sanity_check:	sanity check the fw image
 * @get_boot_addr:	get boot address to entry point specified in firmware
 * @panic:	optional callback to react to system panic, core will delay
 *		panic at least the returned number of milliseconds
 * @coredump:	  collect firmware dump after the subsystem is shutdown
 */
struct rproc_ops {
	int (*prepare)(struct rproc *rproc);
	int (*unprepare)(struct rproc *rproc);
	int (*start)(struct rproc *rproc);
	int (*stop)(struct rproc *rproc);
	int (*attach)(struct rproc *rproc);
	int (*detach)(struct rproc *rproc);
	void (*kick)(struct rproc *rproc, int vqid);
	void * (*da_to_va)(struct rproc *rproc, u64 da, size_t len, bool *is_iomem);
	int (*parse_fw)(struct rproc *rproc, const struct firmware *fw);
	int (*handle_rsc)(struct rproc *rproc, u32 rsc_type, void *rsc,
			  int offset, int avail);
	struct resource_table *(*find_loaded_rsc_table)(
				struct rproc *rproc, const struct firmware *fw);
	struct resource_table *(*get_loaded_rsc_table)(
				struct rproc *rproc, size_t *size);
	int (*load)(struct rproc *rproc, const struct firmware *fw);
	int (*sanity_check)(struct rproc *rproc, const struct firmware *fw);
	u64 (*get_boot_addr)(struct rproc *rproc, const struct firmware *fw);
	unsigned long (*panic)(struct rproc *rproc);
	void (*coredump)(struct rproc *rproc);
};

/**
 * enum rproc_state - remote processor states
 * @RPROC_OFFLINE:	device is powered off
 * @RPROC_SUSPENDED:	device is suspended; needs to be woken up to receive
 *			a message.
 * @RPROC_RUNNING:	device is up and running
 * @RPROC_CRASHED:	device has crashed; need to start recovery
 * @RPROC_DELETED:	device is deleted
 * @RPROC_ATTACHED:	device has been booted by another entity and the core
 *			has attached to it
 * @RPROC_DETACHED:	device has been booted by another entity and waiting
 *			for the core to attach to it
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
	RPROC_DELETED	= 4,
	RPROC_ATTACHED	= 5,
	RPROC_DETACHED	= 6,
	RPROC_LAST	= 7,
};

/**
 * enum rproc_crash_type - remote processor crash types
 * @RPROC_MMUFAULT:	iommu fault
 * @RPROC_WATCHDOG:	watchdog bite
 * @RPROC_FATAL_ERROR:	fatal error
 *
 * Each element of the enum is used as an array index. So that, the value of
 * the elements should be always something sane.
 *
 * Feel free to add more types when needed.
 */
enum rproc_crash_type {
	RPROC_MMUFAULT,
	RPROC_WATCHDOG,
	RPROC_FATAL_ERROR,
};

/**
 * enum rproc_dump_mechanism - Coredump options for core
 * @RPROC_COREDUMP_DISABLED:	Don't perform any dump
 * @RPROC_COREDUMP_ENABLED:	Copy dump to separate buffer and carry on with
 *				recovery
 * @RPROC_COREDUMP_INLINE:	Read segments directly from device memory. Stall
 *				recovery until all segments are read
 */
enum rproc_dump_mechanism {
	RPROC_COREDUMP_DISABLED,
	RPROC_COREDUMP_ENABLED,
	RPROC_COREDUMP_INLINE,
};

/**
 * struct rproc_dump_segment - segment info from ELF header
 * @node:	list node related to the rproc segment list
 * @da:		device address of the segment
 * @size:	size of the segment
 * @priv:	private data associated with the dump_segment
 * @dump:	custom dump function to fill device memory segment associated
 *		with coredump
 * @offset:	offset of the segment
 */
struct rproc_dump_segment {
	struct list_head node;

	dma_addr_t da;
	size_t size;

	void *priv;
	void (*dump)(struct rproc *rproc, struct rproc_dump_segment *segment,
		     void *dest, size_t offset, size_t size);
	loff_t offset;
};

/**
 * enum rproc_features - features supported
 *
 * @RPROC_FEAT_ATTACH_ON_RECOVERY: The remote processor does not need help
 *				   from Linux to recover, such as firmware
 *				   loading. Linux just needs to attach after
 *				   recovery.
 */

enum rproc_features {
	RPROC_FEAT_ATTACH_ON_RECOVERY,
	RPROC_MAX_FEATURES,
};

/**
 * struct rproc - represents a physical remote processor device
 * @node: list node of this rproc object
 * @domain: iommu domain
 * @name: human readable name of the rproc
 * @firmware: name of firmware file to be loaded
 * @priv: private data which belongs to the platform-specific rproc module
 * @ops: platform-specific start/stop rproc handlers
 * @dev: virtual device for refcounting and common remoteproc behavior
 * @power: refcount of users who need this rproc powered up
 * @state: state of the device
 * @dump_conf: Currently selected coredump configuration
 * @lock: lock which protects concurrent manipulations of the rproc
 * @dbg_dir: debugfs directory of this rproc device
 * @traces: list of trace buffers
 * @num_traces: number of trace buffers
 * @carveouts: list of physically contiguous memory allocations
 * @mappings: list of iommu mappings we initiated, needed on shutdown
 * @bootaddr: address of first instruction to boot rproc with (optional)
 * @rvdevs: list of remote virtio devices
 * @subdevs: list of subdevices, to following the running state
 * @notifyids: idr for dynamically assigning rproc-wide unique notify ids
 * @index: index of this rproc device
 * @crash_handler: workqueue for handling a crash
 * @crash_cnt: crash counter
 * @recovery_disabled: flag that state if recovery was disabled
 * @max_notifyid: largest allocated notify id.
 * @table_ptr: pointer to the resource table in effect
 * @clean_table: copy of the resource table without modifications.  Used
 *		 when a remote processor is attached or detached from the core
 * @cached_table: copy of the resource table
 * @table_sz: size of @cached_table
 * @has_iommu: flag to indicate if remote processor is behind an MMU
 * @auto_boot: flag to indicate if remote processor should be auto-started
 * @sysfs_read_only: flag to make remoteproc sysfs files read only
 * @dump_segments: list of segments in the firmware
 * @nb_vdev: number of vdev currently handled by rproc
 * @elf_class: firmware ELF class
 * @elf_machine: firmware ELF machine
 * @cdev: character device of the rproc
 * @cdev_put_on_release: flag to indicate if remoteproc should be shutdown on @char_dev release
 * @features: indicate remoteproc features
 */
struct rproc {
	struct list_head node;
	struct iommu_domain *domain;
	const char *name;
	const char *firmware;
	void *priv;
	struct rproc_ops *ops;
	struct device dev;
	atomic_t power;
	unsigned int state;
	enum rproc_dump_mechanism dump_conf;
	struct mutex lock;
	struct dentry *dbg_dir;
	struct list_head traces;
	int num_traces;
	struct list_head carveouts;
	struct list_head mappings;
	u64 bootaddr;
	struct list_head rvdevs;
	struct list_head subdevs;
	struct idr notifyids;
	int index;
	struct work_struct crash_handler;
	unsigned int crash_cnt;
	bool recovery_disabled;
	int max_notifyid;
	struct resource_table *table_ptr;
	struct resource_table *clean_table;
	struct resource_table *cached_table;
	size_t table_sz;
	bool has_iommu;
	bool auto_boot;
	bool sysfs_read_only;
	struct list_head dump_segments;
	int nb_vdev;
	u8 elf_class;
	u16 elf_machine;
	struct cdev cdev;
	bool cdev_put_on_release;
	DECLARE_BITMAP(features, RPROC_MAX_FEATURES);
};

/**
 * struct rproc_subdev - subdevice tied to a remoteproc
 * @node: list node related to the rproc subdevs list
 * @prepare: prepare function, called before the rproc is started
 * @start: start function, called after the rproc has been started
 * @stop: stop function, called before the rproc is stopped; the @crashed
 *	    parameter indicates if this originates from a recovery
 * @unprepare: unprepare function, called after the rproc has been stopped
 */
struct rproc_subdev {
	struct list_head node;

	int (*prepare)(struct rproc_subdev *subdev);
	int (*start)(struct rproc_subdev *subdev);
	void (*stop)(struct rproc_subdev *subdev, bool crashed);
	void (*unprepare)(struct rproc_subdev *subdev);
};

/* we currently support only two vrings per rvdev */

#define RVDEV_NUM_VRINGS 2

/**
 * struct rproc_vring - remoteproc vring state
 * @va:	virtual address
 * @num: vring size
 * @da: device address
 * @align: vring alignment
 * @notifyid: rproc-specific unique vring index
 * @rvdev: remote vdev
 * @vq: the virtqueue of this vring
 */
struct rproc_vring {
	void *va;
	int num;
	u32 da;
	u32 align;
	int notifyid;
	struct rproc_vdev *rvdev;
	struct virtqueue *vq;
};

/**
 * struct rproc_vdev - remoteproc state for a supported virtio device
 * @subdev: handle for registering the vdev as a rproc subdevice
 * @pdev: remoteproc virtio platform device
 * @id: virtio device id (as in virtio_ids.h)
 * @node: list node
 * @rproc: the rproc handle
 * @vring: the vrings for this vdev
 * @rsc_offset: offset of the vdev's resource entry
 * @index: vdev position versus other vdev declared in resource table
 */
struct rproc_vdev {

	struct rproc_subdev subdev;
	struct platform_device *pdev;

	unsigned int id;
	struct list_head node;
	struct rproc *rproc;
	struct rproc_vring vring[RVDEV_NUM_VRINGS];
	u32 rsc_offset;
	u32 index;
};

struct rproc *rproc_get_by_phandle(phandle phandle);
struct rproc *rproc_get_by_child(struct device *dev);

struct rproc *rproc_alloc(struct device *dev, const char *name,
			  const struct rproc_ops *ops,
			  const char *firmware, int len);
void rproc_put(struct rproc *rproc);
int rproc_add(struct rproc *rproc);
int rproc_del(struct rproc *rproc);
void rproc_free(struct rproc *rproc);
void rproc_resource_cleanup(struct rproc *rproc);

struct rproc *devm_rproc_alloc(struct device *dev, const char *name,
			       const struct rproc_ops *ops,
			       const char *firmware, int len);
int devm_rproc_add(struct device *dev, struct rproc *rproc);

void rproc_add_carveout(struct rproc *rproc, struct rproc_mem_entry *mem);

struct rproc_mem_entry *
rproc_mem_entry_init(struct device *dev,
		     void *va, dma_addr_t dma, size_t len, u32 da,
		     int (*alloc)(struct rproc *, struct rproc_mem_entry *),
		     int (*release)(struct rproc *, struct rproc_mem_entry *),
		     const char *name, ...);

struct rproc_mem_entry *
rproc_of_resm_mem_entry_init(struct device *dev, u32 of_resm_idx, size_t len,
			     u32 da, const char *name, ...);

int rproc_boot(struct rproc *rproc);
int rproc_shutdown(struct rproc *rproc);
int rproc_detach(struct rproc *rproc);
int rproc_set_firmware(struct rproc *rproc, const char *fw_name);
void rproc_report_crash(struct rproc *rproc, enum rproc_crash_type type);
void *rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem);

/* from remoteproc_coredump.c */
void rproc_coredump_cleanup(struct rproc *rproc);
void rproc_coredump(struct rproc *rproc);
void rproc_coredump_using_sections(struct rproc *rproc);
int rproc_coredump_add_segment(struct rproc *rproc, dma_addr_t da, size_t size);
int rproc_coredump_add_custom_segment(struct rproc *rproc,
				      dma_addr_t da, size_t size,
				      void (*dumpfn)(struct rproc *rproc,
						     struct rproc_dump_segment *segment,
						     void *dest, size_t offset,
						     size_t size),
				      void *priv);
int rproc_coredump_set_elf_info(struct rproc *rproc, u8 class, u16 machine);

void rproc_add_subdev(struct rproc *rproc, struct rproc_subdev *subdev);

void rproc_remove_subdev(struct rproc *rproc, struct rproc_subdev *subdev);

#endif /* REMOTEPROC_H */
