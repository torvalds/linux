#ifndef _SCSI_SCSI_HOST_H
#define _SCSI_SCSI_HOST_H

#include <linux/device.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <scsi/scsi.h>

struct request_queue;
struct block_device;
struct completion;
struct module;
struct scsi_cmnd;
struct scsi_device;
struct scsi_target;
struct Scsi_Host;
struct scsi_host_cmd_pool;
struct scsi_transport_template;
struct blk_queue_tags;


/*
 * The various choices mean:
 * NONE: Self evident.	Host adapter is not capable of scatter-gather.
 * ALL:	 Means that the host adapter module can do scatter-gather,
 *	 and that there is no limit to the size of the table to which
 *	 we scatter/gather data.  The value we set here is the maximum
 *	 single element sglist.  To use chained sglists, the adapter
 *	 has to set a value beyond ALL (and correctly use the chain
 *	 handling API.
 * Anything else:  Indicates the maximum number of chains that can be
 *	 used in one scatter-gather request.
 */
#define SG_NONE 0
#define SG_ALL	SCSI_MAX_SG_SEGMENTS

#define MODE_UNKNOWN 0x00
#define MODE_INITIATOR 0x01
#define MODE_TARGET 0x02

#define DISABLE_CLUSTERING 0
#define ENABLE_CLUSTERING 1

enum {
	SCSI_QDEPTH_DEFAULT,	/* default requested change, e.g. from sysfs */
	SCSI_QDEPTH_QFULL,	/* scsi-ml requested due to queue full */
	SCSI_QDEPTH_RAMP_UP,	/* scsi-ml requested due to threshold event */
};

struct scsi_host_template {
	struct module *module;
	const char *name;

	/*
	 * Used to initialize old-style drivers.  For new-style drivers
	 * just perform all work in your module initialization function.
	 *
	 * Status:  OBSOLETE
	 */
	int (* detect)(struct scsi_host_template *);

	/*
	 * Used as unload callback for hosts with old-style drivers.
	 *
	 * Status: OBSOLETE
	 */
	int (* release)(struct Scsi_Host *);

	/*
	 * The info function will return whatever useful information the
	 * developer sees fit.  If not provided, then the name field will
	 * be used instead.
	 *
	 * Status: OPTIONAL
	 */
	const char *(* info)(struct Scsi_Host *);

	/*
	 * Ioctl interface
	 *
	 * Status: OPTIONAL
	 */
	int (* ioctl)(struct scsi_device *dev, int cmd, void __user *arg);


#ifdef CONFIG_COMPAT
	/* 
	 * Compat handler. Handle 32bit ABI.
	 * When unknown ioctl is passed return -ENOIOCTLCMD.
	 *
	 * Status: OPTIONAL
	 */
	int (* compat_ioctl)(struct scsi_device *dev, int cmd, void __user *arg);
#endif

	/*
	 * The queuecommand function is used to queue up a scsi
	 * command block to the LLDD.  When the driver finished
	 * processing the command the done callback is invoked.
	 *
	 * If queuecommand returns 0, then the HBA has accepted the
	 * command.  The done() function must be called on the command
	 * when the driver has finished with it. (you may call done on the
	 * command before queuecommand returns, but in this case you
	 * *must* return 0 from queuecommand).
	 *
	 * Queuecommand may also reject the command, in which case it may
	 * not touch the command and must not call done() for it.
	 *
	 * There are two possible rejection returns:
	 *
	 *   SCSI_MLQUEUE_DEVICE_BUSY: Block this device temporarily, but
	 *   allow commands to other devices serviced by this host.
	 *
	 *   SCSI_MLQUEUE_HOST_BUSY: Block all devices served by this
	 *   host temporarily.
	 *
         * For compatibility, any other non-zero return is treated the
         * same as SCSI_MLQUEUE_HOST_BUSY.
	 *
	 * NOTE: "temporarily" means either until the next command for#
	 * this device/host completes, or a period of time determined by
	 * I/O pressure in the system if there are no other outstanding
	 * commands.
	 *
	 * STATUS: REQUIRED
	 */
	int (* queuecommand)(struct Scsi_Host *, struct scsi_cmnd *);

	/*
	 * The transfer functions are used to queue a scsi command to
	 * the LLD. When the driver is finished processing the command
	 * the done callback is invoked.
	 *
	 * This is called to inform the LLD to transfer
	 * scsi_bufflen(cmd) bytes. scsi_sg_count(cmd) speciefies the
	 * number of scatterlist entried in the command and
	 * scsi_sglist(cmd) returns the scatterlist.
	 *
	 * return values: see queuecommand
	 *
	 * If the LLD accepts the cmd, it should set the result to an
	 * appropriate value when completed before calling the done function.
	 *
	 * STATUS: REQUIRED FOR TARGET DRIVERS
	 */
	/* TODO: rename */
	int (* transfer_response)(struct scsi_cmnd *,
				  void (*done)(struct scsi_cmnd *));

	/*
	 * This is an error handling strategy routine.  You don't need to
	 * define one of these if you don't want to - there is a default
	 * routine that is present that should work in most cases.  For those
	 * driver authors that have the inclination and ability to write their
	 * own strategy routine, this is where it is specified.  Note - the
	 * strategy routine is *ALWAYS* run in the context of the kernel eh
	 * thread.  Thus you are guaranteed to *NOT* be in an interrupt
	 * handler when you execute this, and you are also guaranteed to
	 * *NOT* have any other commands being queued while you are in the
	 * strategy routine. When you return from this function, operations
	 * return to normal.
	 *
	 * See scsi_error.c scsi_unjam_host for additional comments about
	 * what this function should and should not be attempting to do.
	 *
	 * Status: REQUIRED	(at least one of them)
	 */
	int (* eh_abort_handler)(struct scsi_cmnd *);
	int (* eh_device_reset_handler)(struct scsi_cmnd *);
	int (* eh_target_reset_handler)(struct scsi_cmnd *);
	int (* eh_bus_reset_handler)(struct scsi_cmnd *);
	int (* eh_host_reset_handler)(struct scsi_cmnd *);

	/*
	 * Before the mid layer attempts to scan for a new device where none
	 * currently exists, it will call this entry in your driver.  Should
	 * your driver need to allocate any structs or perform any other init
	 * items in order to send commands to a currently unused target/lun
	 * combo, then this is where you can perform those allocations.  This
	 * is specifically so that drivers won't have to perform any kind of
	 * "is this a new device" checks in their queuecommand routine,
	 * thereby making the hot path a bit quicker.
	 *
	 * Return values: 0 on success, non-0 on failure
	 *
	 * Deallocation:  If we didn't find any devices at this ID, you will
	 * get an immediate call to slave_destroy().  If we find something
	 * here then you will get a call to slave_configure(), then the
	 * device will be used for however long it is kept around, then when
	 * the device is removed from the system (or * possibly at reboot
	 * time), you will then get a call to slave_destroy().  This is
	 * assuming you implement slave_configure and slave_destroy.
	 * However, if you allocate memory and hang it off the device struct,
	 * then you must implement the slave_destroy() routine at a minimum
	 * in order to avoid leaking memory
	 * each time a device is tore down.
	 *
	 * Status: OPTIONAL
	 */
	int (* slave_alloc)(struct scsi_device *);

	/*
	 * Once the device has responded to an INQUIRY and we know the
	 * device is online, we call into the low level driver with the
	 * struct scsi_device *.  If the low level device driver implements
	 * this function, it *must* perform the task of setting the queue
	 * depth on the device.  All other tasks are optional and depend
	 * on what the driver supports and various implementation details.
	 * 
	 * Things currently recommended to be handled at this time include:
	 *
	 * 1.  Setting the device queue depth.  Proper setting of this is
	 *     described in the comments for scsi_adjust_queue_depth.
	 * 2.  Determining if the device supports the various synchronous
	 *     negotiation protocols.  The device struct will already have
	 *     responded to INQUIRY and the results of the standard items
	 *     will have been shoved into the various device flag bits, eg.
	 *     device->sdtr will be true if the device supports SDTR messages.
	 * 3.  Allocating command structs that the device will need.
	 * 4.  Setting the default timeout on this device (if needed).
	 * 5.  Anything else the low level driver might want to do on a device
	 *     specific setup basis...
	 * 6.  Return 0 on success, non-0 on error.  The device will be marked
	 *     as offline on error so that no access will occur.  If you return
	 *     non-0, your slave_destroy routine will never get called for this
	 *     device, so don't leave any loose memory hanging around, clean
	 *     up after yourself before returning non-0
	 *
	 * Status: OPTIONAL
	 */
	int (* slave_configure)(struct scsi_device *);

	/*
	 * Immediately prior to deallocating the device and after all activity
	 * has ceased the mid layer calls this point so that the low level
	 * driver may completely detach itself from the scsi device and vice
	 * versa.  The low level driver is responsible for freeing any memory
	 * it allocated in the slave_alloc or slave_configure calls. 
	 *
	 * Status: OPTIONAL
	 */
	void (* slave_destroy)(struct scsi_device *);

	/*
	 * Before the mid layer attempts to scan for a new device attached
	 * to a target where no target currently exists, it will call this
	 * entry in your driver.  Should your driver need to allocate any
	 * structs or perform any other init items in order to send commands
	 * to a currently unused target, then this is where you can perform
	 * those allocations.
	 *
	 * Return values: 0 on success, non-0 on failure
	 *
	 * Status: OPTIONAL
	 */
	int (* target_alloc)(struct scsi_target *);

	/*
	 * Immediately prior to deallocating the target structure, and
	 * after all activity to attached scsi devices has ceased, the
	 * midlayer calls this point so that the driver may deallocate
	 * and terminate any references to the target.
	 *
	 * Status: OPTIONAL
	 */
	void (* target_destroy)(struct scsi_target *);

	/*
	 * If a host has the ability to discover targets on its own instead
	 * of scanning the entire bus, it can fill in this function and
	 * call scsi_scan_host().  This function will be called periodically
	 * until it returns 1 with the scsi_host and the elapsed time of
	 * the scan in jiffies.
	 *
	 * Status: OPTIONAL
	 */
	int (* scan_finished)(struct Scsi_Host *, unsigned long);

	/*
	 * If the host wants to be called before the scan starts, but
	 * after the midlayer has set up ready for the scan, it can fill
	 * in this function.
	 *
	 * Status: OPTIONAL
	 */
	void (* scan_start)(struct Scsi_Host *);

	/*
	 * Fill in this function to allow the queue depth of this host
	 * to be changeable (on a per device basis).  Returns either
	 * the current queue depth setting (may be different from what
	 * was passed in) or an error.  An error should only be
	 * returned if the requested depth is legal but the driver was
	 * unable to set it.  If the requested depth is illegal, the
	 * driver should set and return the closest legal queue depth.
	 *
	 * Status: OPTIONAL
	 */
	int (* change_queue_depth)(struct scsi_device *, int, int);

	/*
	 * Fill in this function to allow the changing of tag types
	 * (this also allows the enabling/disabling of tag command
	 * queueing).  An error should only be returned if something
	 * went wrong in the driver while trying to set the tag type.
	 * If the driver doesn't support the requested tag type, then
	 * it should set the closest type it does support without
	 * returning an error.  Returns the actual tag type set.
	 *
	 * Status: OPTIONAL
	 */
	int (* change_queue_type)(struct scsi_device *, int);

	/*
	 * This function determines the BIOS parameters for a given
	 * harddisk.  These tend to be numbers that are made up by
	 * the host adapter.  Parameters:
	 * size, device, list (heads, sectors, cylinders)
	 *
	 * Status: OPTIONAL
	 */
	int (* bios_param)(struct scsi_device *, struct block_device *,
			sector_t, int []);

	/*
	 * This function is called when one or more partitions on the
	 * device reach beyond the end of the device.
	 *
	 * Status: OPTIONAL
	 */
	void (*unlock_native_capacity)(struct scsi_device *);

	/*
	 * Can be used to export driver statistics and other infos to the
	 * world outside the kernel ie. userspace and it also provides an
	 * interface to feed the driver with information.
	 *
	 * Status: OBSOLETE
	 */
	int (*proc_info)(struct Scsi_Host *, char *, char **, off_t, int, int);

	/*
	 * This is an optional routine that allows the transport to become
	 * involved when a scsi io timer fires. The return value tells the
	 * timer routine how to finish the io timeout handling:
	 * EH_HANDLED:		I fixed the error, please complete the command
	 * EH_RESET_TIMER:	I need more time, reset the timer and
	 *			begin counting again
	 * EH_NOT_HANDLED	Begin normal error recovery
	 *
	 * Status: OPTIONAL
	 */
	enum blk_eh_timer_return (*eh_timed_out)(struct scsi_cmnd *);

	/* This is an optional routine that allows transport to initiate
	 * LLD adapter or firmware reset using sysfs attribute.
	 *
	 * Return values: 0 on success, -ve value on failure.
	 *
	 * Status: OPTIONAL
	 */

	int (*host_reset)(struct Scsi_Host *shost, int reset_type);
#define SCSI_ADAPTER_RESET	1
#define SCSI_FIRMWARE_RESET	2


	/*
	 * Name of proc directory
	 */
	const char *proc_name;

	/*
	 * Used to store the procfs directory if a driver implements the
	 * proc_info method.
	 */
	struct proc_dir_entry *proc_dir;

	/*
	 * This determines if we will use a non-interrupt driven
	 * or an interrupt driven scheme.  It is set to the maximum number
	 * of simultaneous commands a given host adapter will accept.
	 */
	int can_queue;

	/*
	 * In many instances, especially where disconnect / reconnect are
	 * supported, our host also has an ID on the SCSI bus.  If this is
	 * the case, then it must be reserved.  Please set this_id to -1 if
	 * your setup is in single initiator mode, and the host lacks an
	 * ID.
	 */
	int this_id;

	/*
	 * This determines the degree to which the host adapter is capable
	 * of scatter-gather.
	 */
	unsigned short sg_tablesize;
	unsigned short sg_prot_tablesize;

	/*
	 * Set this if the host adapter has limitations beside segment count.
	 */
	unsigned short max_sectors;

	/*
	 * DMA scatter gather segment boundary limit. A segment crossing this
	 * boundary will be split in two.
	 */
	unsigned long dma_boundary;

	/*
	 * This specifies "machine infinity" for host templates which don't
	 * limit the transfer size.  Note this limit represents an absolute
	 * maximum, and may be over the transfer limits allowed for
	 * individual devices (e.g. 256 for SCSI-1).
	 */
#define SCSI_DEFAULT_MAX_SECTORS	1024

	/*
	 * True if this host adapter can make good use of linked commands.
	 * This will allow more than one command to be queued to a given
	 * unit on a given host.  Set this to the maximum number of command
	 * blocks to be provided for each device.  Set this to 1 for one
	 * command block per lun, 2 for two, etc.  Do not set this to 0.
	 * You should make sure that the host adapter will do the right thing
	 * before you try setting this above 1.
	 */
	short cmd_per_lun;

	/*
	 * present contains counter indicating how many boards of this
	 * type were found when we did the scan.
	 */
	unsigned char present;

	/*
	 * This specifies the mode that a LLD supports.
	 */
	unsigned supported_mode:2;

	/*
	 * True if this host adapter uses unchecked DMA onto an ISA bus.
	 */
	unsigned unchecked_isa_dma:1;

	/*
	 * True if this host adapter can make good use of clustering.
	 * I originally thought that if the tablesize was large that it
	 * was a waste of CPU cycles to prepare a cluster list, but
	 * it works out that the Buslogic is faster if you use a smaller
	 * number of segments (i.e. use clustering).  I guess it is
	 * inefficient.
	 */
	unsigned use_clustering:1;

	/*
	 * True for emulated SCSI host adapters (e.g. ATAPI).
	 */
	unsigned emulated:1;

	/*
	 * True if the low-level driver performs its own reset-settle delays.
	 */
	unsigned skip_settle_delay:1;

	/*
	 * True if we are using ordered write support.
	 */
	unsigned ordered_tag:1;

	/*
	 * Countdown for host blocking with no commands outstanding.
	 */
	unsigned int max_host_blocked;

	/*
	 * Default value for the blocking.  If the queue is empty,
	 * host_blocked counts down in the request_fn until it restarts
	 * host operations as zero is reached.  
	 *
	 * FIXME: This should probably be a value in the template
	 */
#define SCSI_DEFAULT_HOST_BLOCKED	7

	/*
	 * Pointer to the sysfs class properties for this host, NULL terminated.
	 */
	struct device_attribute **shost_attrs;

	/*
	 * Pointer to the SCSI device properties for this host, NULL terminated.
	 */
	struct device_attribute **sdev_attrs;

	/*
	 * List of hosts per template.
	 *
	 * This is only for use by scsi_module.c for legacy templates.
	 * For these access to it is synchronized implicitly by
	 * module_init/module_exit.
	 */
	struct list_head legacy_hosts;

	/*
	 * Vendor Identifier associated with the host
	 *
	 * Note: When specifying vendor_id, be sure to read the
	 *   Vendor Type and ID formatting requirements specified in
	 *   scsi_netlink.h
	 */
	u64 vendor_id;
};

/*
 * Temporary #define for host lock push down. Can be removed when all
 * drivers have been updated to take advantage of unlocked
 * queuecommand.
 *
 */
#define DEF_SCSI_QCMD(func_name) \
	int func_name(struct Scsi_Host *shost, struct scsi_cmnd *cmd)	\
	{								\
		unsigned long irq_flags;				\
		int rc;							\
		spin_lock_irqsave(shost->host_lock, irq_flags);		\
		scsi_cmd_get_serial(shost, cmd);			\
		rc = func_name##_lck (cmd, cmd->scsi_done);			\
		spin_unlock_irqrestore(shost->host_lock, irq_flags);	\
		return rc;						\
	}


/*
 * shost state: If you alter this, you also need to alter scsi_sysfs.c
 * (for the ascii descriptions) and the state model enforcer:
 * scsi_host_set_state()
 */
enum scsi_host_state {
	SHOST_CREATED = 1,
	SHOST_RUNNING,
	SHOST_CANCEL,
	SHOST_DEL,
	SHOST_RECOVERY,
	SHOST_CANCEL_RECOVERY,
	SHOST_DEL_RECOVERY,
};

struct Scsi_Host {
	/*
	 * __devices is protected by the host_lock, but you should
	 * usually use scsi_device_lookup / shost_for_each_device
	 * to access it and don't care about locking yourself.
	 * In the rare case of beeing in irq context you can use
	 * their __ prefixed variants with the lock held. NEVER
	 * access this list directly from a driver.
	 */
	struct list_head	__devices;
	struct list_head	__targets;
	
	struct scsi_host_cmd_pool *cmd_pool;
	spinlock_t		free_list_lock;
	struct list_head	free_list; /* backup store of cmd structs */
	struct list_head	starved_list;

	spinlock_t		default_lock;
	spinlock_t		*host_lock;

	struct mutex		scan_mutex;/* serialize scanning activity */

	struct list_head	eh_cmd_q;
	struct task_struct    * ehandler;  /* Error recovery thread. */
	struct completion     * eh_action; /* Wait for specific actions on the
					      host. */
	wait_queue_head_t       host_wait;
	struct scsi_host_template *hostt;
	struct scsi_transport_template *transportt;

	/*
	 * Area to keep a shared tag map (if needed, will be
	 * NULL if not).
	 */
	struct blk_queue_tag	*bqt;

	/*
	 * The following two fields are protected with host_lock;
	 * however, eh routines can safely access during eh processing
	 * without acquiring the lock.
	 */
	unsigned int host_busy;		   /* commands actually active on low-level */
	unsigned int host_failed;	   /* commands that failed. */
	unsigned int host_eh_scheduled;    /* EH scheduled without command */
    
	unsigned int host_no;  /* Used for IOCTL_GET_IDLUN, /proc/scsi et al. */
	int resetting; /* if set, it means that last_reset is a valid value */
	unsigned long last_reset;

	/*
	 * These three parameters can be used to allow for wide scsi,
	 * and for host adapters that support multiple busses
	 * The first two should be set to 1 more than the actual max id
	 * or lun (i.e. 8 for normal systems).
	 */
	unsigned int max_id;
	unsigned int max_lun;
	unsigned int max_channel;

	/*
	 * This is a unique identifier that must be assigned so that we
	 * have some way of identifying each detected host adapter properly
	 * and uniquely.  For hosts that do not support more than one card
	 * in the system at one time, this does not need to be set.  It is
	 * initialized to 0 in scsi_register.
	 */
	unsigned int unique_id;

	/*
	 * The maximum length of SCSI commands that this host can accept.
	 * Probably 12 for most host adapters, but could be 16 for others.
	 * or 260 if the driver supports variable length cdbs.
	 * For drivers that don't set this field, a value of 12 is
	 * assumed.
	 */
	unsigned short max_cmd_len;

	int this_id;
	int can_queue;
	short cmd_per_lun;
	short unsigned int sg_tablesize;
	short unsigned int sg_prot_tablesize;
	short unsigned int max_sectors;
	unsigned long dma_boundary;
	/* 
	 * Used to assign serial numbers to the cmds.
	 * Protected by the host lock.
	 */
	unsigned long cmd_serial_number;
	
	unsigned active_mode:2;
	unsigned unchecked_isa_dma:1;
	unsigned use_clustering:1;
	unsigned use_blk_tcq:1;

	/*
	 * Host has requested that no further requests come through for the
	 * time being.
	 */
	unsigned host_self_blocked:1;
    
	/*
	 * Host uses correct SCSI ordering not PC ordering. The bit is
	 * set for the minority of drivers whose authors actually read
	 * the spec ;).
	 */
	unsigned reverse_ordering:1;

	/*
	 * Ordered write support
	 */
	unsigned ordered_tag:1;

	/* Task mgmt function in progress */
	unsigned tmf_in_progress:1;

	/* Asynchronous scan in progress */
	unsigned async_scan:1;

	/* Don't resume host in EH */
	unsigned eh_noresume:1;

	/*
	 * Optional work queue to be utilized by the transport
	 */
	char work_q_name[20];
	struct workqueue_struct *work_q;

	/*
	 * Host has rejected a command because it was busy.
	 */
	unsigned int host_blocked;

	/*
	 * Value host_blocked counts down from
	 */
	unsigned int max_host_blocked;

	/* Protection Information */
	unsigned int prot_capabilities;
	unsigned char prot_guard_type;

	/*
	 * q used for scsi_tgt msgs, async events or any other requests that
	 * need to be processed in userspace
	 */
	struct request_queue *uspace_req_q;

	/* legacy crap */
	unsigned long base;
	unsigned long io_port;
	unsigned char n_io_port;
	unsigned char dma_channel;
	unsigned int  irq;
	

	enum scsi_host_state shost_state;

	/* ldm bits */
	struct device		shost_gendev, shost_dev;

	/*
	 * List of hosts per template.
	 *
	 * This is only for use by scsi_module.c for legacy templates.
	 * For these access to it is synchronized implicitly by
	 * module_init/module_exit.
	 */
	struct list_head sht_legacy_list;

	/*
	 * Points to the transport data (if any) which is allocated
	 * separately
	 */
	void *shost_data;

	/*
	 * Points to the physical bus device we'd use to do DMA
	 * Needed just in case we have virtual hosts.
	 */
	struct device *dma_dev;

	/*
	 * We should ensure that this is aligned, both for better performance
	 * and also because some compilers (m68k) don't automatically force
	 * alignment to a long boundary.
	 */
	unsigned long hostdata[0]  /* Used for storage of host specific stuff */
		__attribute__ ((aligned (sizeof(unsigned long))));
};

#define		class_to_shost(d)	\
	container_of(d, struct Scsi_Host, shost_dev)

#define shost_printk(prefix, shost, fmt, a...)	\
	dev_printk(prefix, &(shost)->shost_gendev, fmt, ##a)

static inline void *shost_priv(struct Scsi_Host *shost)
{
	return (void *)shost->hostdata;
}

int scsi_is_host_device(const struct device *);

static inline struct Scsi_Host *dev_to_shost(struct device *dev)
{
	while (!scsi_is_host_device(dev)) {
		if (!dev->parent)
			return NULL;
		dev = dev->parent;
	}
	return container_of(dev, struct Scsi_Host, shost_gendev);
}

static inline int scsi_host_in_recovery(struct Scsi_Host *shost)
{
	return shost->shost_state == SHOST_RECOVERY ||
		shost->shost_state == SHOST_CANCEL_RECOVERY ||
		shost->shost_state == SHOST_DEL_RECOVERY ||
		shost->tmf_in_progress;
}

extern int scsi_queue_work(struct Scsi_Host *, struct work_struct *);
extern void scsi_flush_work(struct Scsi_Host *);

extern struct Scsi_Host *scsi_host_alloc(struct scsi_host_template *, int);
extern int __must_check scsi_add_host_with_dma(struct Scsi_Host *,
					       struct device *,
					       struct device *);
extern void scsi_scan_host(struct Scsi_Host *);
extern void scsi_rescan_device(struct device *);
extern void scsi_remove_host(struct Scsi_Host *);
extern struct Scsi_Host *scsi_host_get(struct Scsi_Host *);
extern void scsi_host_put(struct Scsi_Host *t);
extern struct Scsi_Host *scsi_host_lookup(unsigned short);
extern const char *scsi_host_state_name(enum scsi_host_state);
extern void scsi_cmd_get_serial(struct Scsi_Host *, struct scsi_cmnd *);

extern u64 scsi_calculate_bounce_limit(struct Scsi_Host *);

static inline int __must_check scsi_add_host(struct Scsi_Host *host,
					     struct device *dev)
{
	return scsi_add_host_with_dma(host, dev, dev);
}

static inline struct device *scsi_get_device(struct Scsi_Host *shost)
{
        return shost->shost_gendev.parent;
}

/**
 * scsi_host_scan_allowed - Is scanning of this host allowed
 * @shost:	Pointer to Scsi_Host.
 **/
static inline int scsi_host_scan_allowed(struct Scsi_Host *shost)
{
	return shost->shost_state == SHOST_RUNNING ||
	       shost->shost_state == SHOST_RECOVERY;
}

extern void scsi_unblock_requests(struct Scsi_Host *);
extern void scsi_block_requests(struct Scsi_Host *);

struct class_container;

extern struct request_queue *__scsi_alloc_queue(struct Scsi_Host *shost,
						void (*) (struct request_queue *));
/*
 * These two functions are used to allocate and free a pseudo device
 * which will connect to the host adapter itself rather than any
 * physical device.  You must deallocate when you are done with the
 * thing.  This physical pseudo-device isn't real and won't be available
 * from any high-level drivers.
 */
extern void scsi_free_host_dev(struct scsi_device *);
extern struct scsi_device *scsi_get_host_dev(struct Scsi_Host *);

/*
 * DIF defines the exchange of protection information between
 * initiator and SBC block device.
 *
 * DIX defines the exchange of protection information between OS and
 * initiator.
 */
enum scsi_host_prot_capabilities {
	SHOST_DIF_TYPE1_PROTECTION = 1 << 0, /* T10 DIF Type 1 */
	SHOST_DIF_TYPE2_PROTECTION = 1 << 1, /* T10 DIF Type 2 */
	SHOST_DIF_TYPE3_PROTECTION = 1 << 2, /* T10 DIF Type 3 */

	SHOST_DIX_TYPE0_PROTECTION = 1 << 3, /* DIX between OS and HBA only */
	SHOST_DIX_TYPE1_PROTECTION = 1 << 4, /* DIX with DIF Type 1 */
	SHOST_DIX_TYPE2_PROTECTION = 1 << 5, /* DIX with DIF Type 2 */
	SHOST_DIX_TYPE3_PROTECTION = 1 << 6, /* DIX with DIF Type 3 */
};

/*
 * SCSI hosts which support the Data Integrity Extensions must
 * indicate their capabilities by setting the prot_capabilities using
 * this call.
 */
static inline void scsi_host_set_prot(struct Scsi_Host *shost, unsigned int mask)
{
	shost->prot_capabilities = mask;
}

static inline unsigned int scsi_host_get_prot(struct Scsi_Host *shost)
{
	return shost->prot_capabilities;
}

static inline int scsi_host_prot_dma(struct Scsi_Host *shost)
{
	return shost->prot_capabilities >= SHOST_DIX_TYPE0_PROTECTION;
}

static inline unsigned int scsi_host_dif_capable(struct Scsi_Host *shost, unsigned int target_type)
{
	static unsigned char cap[] = { 0,
				       SHOST_DIF_TYPE1_PROTECTION,
				       SHOST_DIF_TYPE2_PROTECTION,
				       SHOST_DIF_TYPE3_PROTECTION };

	return shost->prot_capabilities & cap[target_type] ? target_type : 0;
}

static inline unsigned int scsi_host_dix_capable(struct Scsi_Host *shost, unsigned int target_type)
{
#if defined(CONFIG_BLK_DEV_INTEGRITY)
	static unsigned char cap[] = { SHOST_DIX_TYPE0_PROTECTION,
				       SHOST_DIX_TYPE1_PROTECTION,
				       SHOST_DIX_TYPE2_PROTECTION,
				       SHOST_DIX_TYPE3_PROTECTION };

	return shost->prot_capabilities & cap[target_type];
#endif
	return 0;
}

/*
 * All DIX-capable initiators must support the T10-mandated CRC
 * checksum.  Controllers can optionally implement the IP checksum
 * scheme which has much lower impact on system performance.  Note
 * that the main rationale for the checksum is to match integrity
 * metadata with data.  Detecting bit errors are a job for ECC memory
 * and buses.
 */

enum scsi_host_guard_type {
	SHOST_DIX_GUARD_CRC = 1 << 0,
	SHOST_DIX_GUARD_IP  = 1 << 1,
};

static inline void scsi_host_set_guard(struct Scsi_Host *shost, unsigned char type)
{
	shost->prot_guard_type = type;
}

static inline unsigned char scsi_host_get_guard(struct Scsi_Host *shost)
{
	return shost->prot_guard_type;
}

/* legacy interfaces */
extern struct Scsi_Host *scsi_register(struct scsi_host_template *, int);
extern void scsi_unregister(struct Scsi_Host *);
extern int scsi_host_set_state(struct Scsi_Host *, enum scsi_host_state);

#endif /* _SCSI_SCSI_HOST_H */
