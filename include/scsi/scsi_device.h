#ifndef _SCSI_SCSI_DEVICE_H
#define _SCSI_SCSI_DEVICE_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/blkdev.h>
#include <scsi/scsi.h>
#include <linux/atomic.h>

struct device;
struct request_queue;
struct scsi_cmnd;
struct scsi_lun;
struct scsi_sense_hdr;

struct scsi_mode_data {
	__u32	length;
	__u16	block_descriptor_length;
	__u8	medium_type;
	__u8	device_specific;
	__u8	header_length;
	__u8	longlba:1;
};

/*
 * sdev state: If you alter this, you also need to alter scsi_sysfs.c
 * (for the ascii descriptions) and the state model enforcer:
 * scsi_lib:scsi_device_set_state().
 */
enum scsi_device_state {
	SDEV_CREATED = 1,	/* device created but not added to sysfs
				 * Only internal commands allowed (for inq) */
	SDEV_RUNNING,		/* device properly configured
				 * All commands allowed */
	SDEV_CANCEL,		/* beginning to delete device
				 * Only error handler commands allowed */
	SDEV_DEL,		/* device deleted 
				 * no commands allowed */
	SDEV_QUIESCE,		/* Device quiescent.  No block commands
				 * will be accepted, only specials (which
				 * originate in the mid-layer) */
	SDEV_OFFLINE,		/* Device offlined (by error handling or
				 * user request */
	SDEV_TRANSPORT_OFFLINE,	/* Offlined by transport class error handler */
	SDEV_BLOCK,		/* Device blocked by scsi lld.  No
				 * scsi commands from user or midlayer
				 * should be issued to the scsi
				 * lld. */
	SDEV_CREATED_BLOCK,	/* same as above but for created devices */
};

enum scsi_device_event {
	SDEV_EVT_MEDIA_CHANGE	= 1,	/* media has changed */

	SDEV_EVT_LAST		= SDEV_EVT_MEDIA_CHANGE,
	SDEV_EVT_MAXBITS	= SDEV_EVT_LAST + 1
};

struct scsi_event {
	enum scsi_device_event	evt_type;
	struct list_head	node;

	/* put union of data structures, for non-simple event types,
	 * here
	 */
};

struct scsi_device {
	struct Scsi_Host *host;
	struct request_queue *request_queue;

	/* the next two are protected by the host->host_lock */
	struct list_head    siblings;   /* list of all devices on this host */
	struct list_head    same_target_siblings; /* just the devices sharing same target id */

	/* this is now protected by the request_queue->queue_lock */
	unsigned int device_busy;	/* commands actually active on
					 * low-level. protected by queue_lock. */
	spinlock_t list_lock;
	struct list_head cmd_list;	/* queue of in use SCSI Command structures */
	struct list_head starved_entry;
	struct scsi_cmnd *current_cmnd;	/* currently active command */
	unsigned short queue_depth;	/* How deep of a queue we want */
	unsigned short max_queue_depth;	/* max queue depth */
	unsigned short last_queue_full_depth; /* These two are used by */
	unsigned short last_queue_full_count; /* scsi_track_queue_full() */
	unsigned long last_queue_full_time;	/* last queue full time */
	unsigned long queue_ramp_up_period;	/* ramp up period in jiffies */
#define SCSI_DEFAULT_RAMP_UP_PERIOD	(120 * HZ)

	unsigned long last_queue_ramp_up;	/* last queue ramp up time */

	unsigned int id, lun, channel;

	unsigned int manufacturer;	/* Manufacturer of device, for using 
					 * vendor-specific cmd's */
	unsigned sector_size;	/* size in bytes */

	void *hostdata;		/* available to low-level driver */
	char type;
	char scsi_level;
	char inq_periph_qual;	/* PQ from INQUIRY data */	
	unsigned char inquiry_len;	/* valid bytes in 'inquiry' */
	unsigned char * inquiry;	/* INQUIRY response data */
	const char * vendor;		/* [back_compat] point into 'inquiry' ... */
	const char * model;		/* ... after scan; point to static string */
	const char * rev;		/* ... "nullnullnullnull" before scan */
	unsigned char current_tag;	/* current tag */
	struct scsi_target      *sdev_target;   /* used only for single_lun */

	unsigned int	sdev_bflags; /* black/white flags as also found in
				 * scsi_devinfo.[hc]. For now used only to
				 * pass settings from slave_alloc to scsi
				 * core. */
	unsigned writeable:1;
	unsigned removable:1;
	unsigned changed:1;	/* Data invalid due to media change */
	unsigned busy:1;	/* Used to prevent races */
	unsigned lockable:1;	/* Able to prevent media removal */
	unsigned locked:1;      /* Media removal disabled */
	unsigned borken:1;	/* Tell the Seagate driver to be 
				 * painfully slow on this device */
	unsigned disconnect:1;	/* can disconnect */
	unsigned soft_reset:1;	/* Uses soft reset option */
	unsigned sdtr:1;	/* Device supports SDTR messages */
	unsigned wdtr:1;	/* Device supports WDTR messages */
	unsigned ppr:1;		/* Device supports PPR messages */
	unsigned tagged_supported:1;	/* Supports SCSI-II tagged queuing */
	unsigned simple_tags:1;	/* simple queue tag messages are enabled */
	unsigned ordered_tags:1;/* ordered queue tag messages are enabled */
	unsigned was_reset:1;	/* There was a bus reset on the bus for 
				 * this device */
	unsigned expecting_cc_ua:1; /* Expecting a CHECK_CONDITION/UNIT_ATTN
				     * because we did a bus reset. */
	unsigned use_10_for_rw:1; /* first try 10-byte read / write */
	unsigned use_10_for_ms:1; /* first try 10-byte mode sense/select */
	unsigned skip_ms_page_8:1;	/* do not use MODE SENSE page 0x08 */
	unsigned skip_ms_page_3f:1;	/* do not use MODE SENSE page 0x3f */
	unsigned skip_vpd_pages:1;	/* do not read VPD pages */
	unsigned use_192_bytes_for_3f:1; /* ask for 192 bytes from page 0x3f */
	unsigned no_start_on_add:1;	/* do not issue start on add */
	unsigned allow_restart:1; /* issue START_UNIT in error handler */
	unsigned manage_start_stop:1;	/* Let HLD (sd) manage start/stop */
	unsigned start_stop_pwr_cond:1;	/* Set power cond. in START_STOP_UNIT */
	unsigned no_uld_attach:1; /* disable connecting to upper level drivers */
	unsigned select_no_atn:1;
	unsigned fix_capacity:1;	/* READ_CAPACITY is too high by 1 */
	unsigned guess_capacity:1;	/* READ_CAPACITY might be too high by 1 */
	unsigned retry_hwerror:1;	/* Retry HARDWARE_ERROR */
	unsigned last_sector_bug:1;	/* do not use multisector accesses on
					   SD_LAST_BUGGY_SECTORS */
	unsigned no_read_disc_info:1;	/* Avoid READ_DISC_INFO cmds */
	unsigned no_read_capacity_16:1; /* Avoid READ_CAPACITY_16 cmds */
	unsigned try_rc_10_first:1;	/* Try READ_CAPACACITY_10 first */
	unsigned is_visible:1;	/* is the device visible in sysfs */
	unsigned can_power_off:1; /* Device supports runtime power off */
	unsigned wce_default_on:1;	/* Cache is ON by default */

	DECLARE_BITMAP(supported_events, SDEV_EVT_MAXBITS); /* supported events */
	struct list_head event_list;	/* asserted events */
	struct work_struct event_work;

	unsigned int device_blocked;	/* Device returned QUEUE_FULL. */

	unsigned int max_device_blocked; /* what device_blocked counts down from  */
#define SCSI_DEFAULT_DEVICE_BLOCKED	3

	atomic_t iorequest_cnt;
	atomic_t iodone_cnt;
	atomic_t ioerr_cnt;

	struct device		sdev_gendev,
				sdev_dev;

	struct execute_work	ew; /* used to get process context on put */
	struct work_struct	requeue_work;

	struct scsi_dh_data	*scsi_dh_data;
	enum scsi_device_state sdev_state;
	unsigned long		sdev_data[0];
} __attribute__((aligned(sizeof(unsigned long))));

struct scsi_dh_devlist {
	char *vendor;
	char *model;
};

typedef void (*activate_complete)(void *, int);
struct scsi_device_handler {
	/* Used by the infrastructure */
	struct list_head list; /* list of scsi_device_handlers */

	/* Filled by the hardware handler */
	struct module *module;
	const char *name;
	const struct scsi_dh_devlist *devlist;
	int (*check_sense)(struct scsi_device *, struct scsi_sense_hdr *);
	int (*attach)(struct scsi_device *);
	void (*detach)(struct scsi_device *);
	int (*activate)(struct scsi_device *, activate_complete, void *);
	int (*prep_fn)(struct scsi_device *, struct request *);
	int (*set_params)(struct scsi_device *, const char *);
	bool (*match)(struct scsi_device *);
};

struct scsi_dh_data {
	struct scsi_device_handler *scsi_dh;
	struct scsi_device *sdev;
	struct kref kref;
	char buf[0];
};

#define	to_scsi_device(d)	\
	container_of(d, struct scsi_device, sdev_gendev)
#define	class_to_sdev(d)	\
	container_of(d, struct scsi_device, sdev_dev)
#define transport_class_to_sdev(class_dev) \
	to_scsi_device(class_dev->parent)

#define sdev_printk(prefix, sdev, fmt, a...)	\
	dev_printk(prefix, &(sdev)->sdev_gendev, fmt, ##a)

#define scmd_printk(prefix, scmd, fmt, a...)				\
        (scmd)->request->rq_disk ?					\
	sdev_printk(prefix, (scmd)->device, "[%s] " fmt,		\
		    (scmd)->request->rq_disk->disk_name, ##a) :		\
	sdev_printk(prefix, (scmd)->device, fmt, ##a)

enum scsi_target_state {
	STARGET_CREATED = 1,
	STARGET_RUNNING,
	STARGET_DEL,
};

/*
 * scsi_target: representation of a scsi target, for now, this is only
 * used for single_lun devices. If no one has active IO to the target,
 * starget_sdev_user is NULL, else it points to the active sdev.
 */
struct scsi_target {
	struct scsi_device	*starget_sdev_user;
	struct list_head	siblings;
	struct list_head	devices;
	struct device		dev;
	unsigned int		reap_ref; /* protected by the host lock */
	unsigned int		channel;
	unsigned int		id; /* target id ... replace
				     * scsi_device.id eventually */
	unsigned int		create:1; /* signal that it needs to be added */
	unsigned int		single_lun:1;	/* Indicates we should only
						 * allow I/O to one of the luns
						 * for the device at a time. */
	unsigned int		pdt_1f_for_no_lun:1;	/* PDT = 0x1f
						 * means no lun present. */
	unsigned int		no_report_luns:1;	/* Don't use
						 * REPORT LUNS for scanning. */
	/* commands actually active on LLD. protected by host lock. */
	unsigned int		target_busy;
	/*
	 * LLDs should set this in the slave_alloc host template callout.
	 * If set to zero then there is not limit.
	 */
	unsigned int		can_queue;
	unsigned int		target_blocked;
	unsigned int		max_target_blocked;
#define SCSI_DEFAULT_TARGET_BLOCKED	3

	char			scsi_level;
	struct execute_work	ew;
	enum scsi_target_state	state;
	void 			*hostdata; /* available to low-level driver */
	unsigned long		starget_data[0]; /* for the transport */
	/* starget_data must be the last element!!!! */
} __attribute__((aligned(sizeof(unsigned long))));

#define to_scsi_target(d)	container_of(d, struct scsi_target, dev)
static inline struct scsi_target *scsi_target(struct scsi_device *sdev)
{
	return to_scsi_target(sdev->sdev_gendev.parent);
}
#define transport_class_to_starget(class_dev) \
	to_scsi_target(class_dev->parent)

#define starget_printk(prefix, starget, fmt, a...)	\
	dev_printk(prefix, &(starget)->dev, fmt, ##a)

extern struct scsi_device *__scsi_add_device(struct Scsi_Host *,
		uint, uint, uint, void *hostdata);
extern int scsi_add_device(struct Scsi_Host *host, uint channel,
			   uint target, uint lun);
extern int scsi_register_device_handler(struct scsi_device_handler *scsi_dh);
extern void scsi_remove_device(struct scsi_device *);
extern int scsi_unregister_device_handler(struct scsi_device_handler *scsi_dh);

extern int scsi_device_get(struct scsi_device *);
extern void scsi_device_put(struct scsi_device *);
extern struct scsi_device *scsi_device_lookup(struct Scsi_Host *,
					      uint, uint, uint);
extern struct scsi_device *__scsi_device_lookup(struct Scsi_Host *,
						uint, uint, uint);
extern struct scsi_device *scsi_device_lookup_by_target(struct scsi_target *,
							uint);
extern struct scsi_device *__scsi_device_lookup_by_target(struct scsi_target *,
							  uint);
extern void starget_for_each_device(struct scsi_target *, void *,
		     void (*fn)(struct scsi_device *, void *));
extern void __starget_for_each_device(struct scsi_target *, void *,
				      void (*fn)(struct scsi_device *,
						 void *));

/* only exposed to implement shost_for_each_device */
extern struct scsi_device *__scsi_iterate_devices(struct Scsi_Host *,
						  struct scsi_device *);

/**
 * shost_for_each_device - iterate over all devices of a host
 * @sdev: the &struct scsi_device to use as a cursor
 * @shost: the &struct scsi_host to iterate over
 *
 * Iterator that returns each device attached to @shost.  This loop
 * takes a reference on each device and releases it at the end.  If
 * you break out of the loop, you must call scsi_device_put(sdev).
 */
#define shost_for_each_device(sdev, shost) \
	for ((sdev) = __scsi_iterate_devices((shost), NULL); \
	     (sdev); \
	     (sdev) = __scsi_iterate_devices((shost), (sdev)))

/**
 * __shost_for_each_device - iterate over all devices of a host (UNLOCKED)
 * @sdev: the &struct scsi_device to use as a cursor
 * @shost: the &struct scsi_host to iterate over
 *
 * Iterator that returns each device attached to @shost.  It does _not_
 * take a reference on the scsi_device, so the whole loop must be
 * protected by shost->host_lock.
 *
 * Note: The only reason to use this is because you need to access the
 * device list in interrupt context.  Otherwise you really want to use
 * shost_for_each_device instead.
 */
#define __shost_for_each_device(sdev, shost) \
	list_for_each_entry((sdev), &((shost)->__devices), siblings)

extern void scsi_adjust_queue_depth(struct scsi_device *, int, int);
extern int scsi_track_queue_full(struct scsi_device *, int);

extern int scsi_set_medium_removal(struct scsi_device *, char);

extern int scsi_mode_sense(struct scsi_device *sdev, int dbd, int modepage,
			   unsigned char *buffer, int len, int timeout,
			   int retries, struct scsi_mode_data *data,
			   struct scsi_sense_hdr *);
extern int scsi_mode_select(struct scsi_device *sdev, int pf, int sp,
			    int modepage, unsigned char *buffer, int len,
			    int timeout, int retries,
			    struct scsi_mode_data *data,
			    struct scsi_sense_hdr *);
extern int scsi_test_unit_ready(struct scsi_device *sdev, int timeout,
				int retries, struct scsi_sense_hdr *sshdr);
extern int scsi_get_vpd_page(struct scsi_device *, u8 page, unsigned char *buf,
			     int buf_len);
extern int scsi_device_set_state(struct scsi_device *sdev,
				 enum scsi_device_state state);
extern struct scsi_event *sdev_evt_alloc(enum scsi_device_event evt_type,
					  gfp_t gfpflags);
extern void sdev_evt_send(struct scsi_device *sdev, struct scsi_event *evt);
extern void sdev_evt_send_simple(struct scsi_device *sdev,
			  enum scsi_device_event evt_type, gfp_t gfpflags);
extern int scsi_device_quiesce(struct scsi_device *sdev);
extern void scsi_device_resume(struct scsi_device *sdev);
extern void scsi_target_quiesce(struct scsi_target *);
extern void scsi_target_resume(struct scsi_target *);
extern void scsi_scan_target(struct device *parent, unsigned int channel,
			     unsigned int id, unsigned int lun, int rescan);
extern void scsi_target_reap(struct scsi_target *);
extern void scsi_target_block(struct device *);
extern void scsi_target_unblock(struct device *, enum scsi_device_state);
extern void scsi_remove_target(struct device *);
extern void int_to_scsilun(unsigned int, struct scsi_lun *);
extern int scsilun_to_int(struct scsi_lun *);
extern const char *scsi_device_state_name(enum scsi_device_state);
extern int scsi_is_sdev_device(const struct device *);
extern int scsi_is_target_device(const struct device *);
extern int scsi_execute(struct scsi_device *sdev, const unsigned char *cmd,
			int data_direction, void *buffer, unsigned bufflen,
			unsigned char *sense, int timeout, int retries,
			int flag, int *resid);
extern int scsi_execute_req(struct scsi_device *sdev, const unsigned char *cmd,
			    int data_direction, void *buffer, unsigned bufflen,
			    struct scsi_sense_hdr *, int timeout, int retries,
			    int *resid);

#ifdef CONFIG_PM_RUNTIME
extern int scsi_autopm_get_device(struct scsi_device *);
extern void scsi_autopm_put_device(struct scsi_device *);
#else
static inline int scsi_autopm_get_device(struct scsi_device *d) { return 0; }
static inline void scsi_autopm_put_device(struct scsi_device *d) {}
#endif /* CONFIG_PM_RUNTIME */

static inline int __must_check scsi_device_reprobe(struct scsi_device *sdev)
{
	return device_reprobe(&sdev->sdev_gendev);
}

static inline unsigned int sdev_channel(struct scsi_device *sdev)
{
	return sdev->channel;
}

static inline unsigned int sdev_id(struct scsi_device *sdev)
{
	return sdev->id;
}

#define scmd_id(scmd) sdev_id((scmd)->device)
#define scmd_channel(scmd) sdev_channel((scmd)->device)

/*
 * checks for positions of the SCSI state machine
 */
static inline int scsi_device_online(struct scsi_device *sdev)
{
	return (sdev->sdev_state != SDEV_OFFLINE &&
		sdev->sdev_state != SDEV_TRANSPORT_OFFLINE &&
		sdev->sdev_state != SDEV_DEL);
}
static inline int scsi_device_blocked(struct scsi_device *sdev)
{
	return sdev->sdev_state == SDEV_BLOCK ||
		sdev->sdev_state == SDEV_CREATED_BLOCK;
}
static inline int scsi_device_created(struct scsi_device *sdev)
{
	return sdev->sdev_state == SDEV_CREATED ||
		sdev->sdev_state == SDEV_CREATED_BLOCK;
}

/* accessor functions for the SCSI parameters */
static inline int scsi_device_sync(struct scsi_device *sdev)
{
	return sdev->sdtr;
}
static inline int scsi_device_wide(struct scsi_device *sdev)
{
	return sdev->wdtr;
}
static inline int scsi_device_dt(struct scsi_device *sdev)
{
	return sdev->ppr;
}
static inline int scsi_device_dt_only(struct scsi_device *sdev)
{
	if (sdev->inquiry_len < 57)
		return 0;
	return (sdev->inquiry[56] & 0x0c) == 0x04;
}
static inline int scsi_device_ius(struct scsi_device *sdev)
{
	if (sdev->inquiry_len < 57)
		return 0;
	return sdev->inquiry[56] & 0x01;
}
static inline int scsi_device_qas(struct scsi_device *sdev)
{
	if (sdev->inquiry_len < 57)
		return 0;
	return sdev->inquiry[56] & 0x02;
}
static inline int scsi_device_enclosure(struct scsi_device *sdev)
{
	return sdev->inquiry ? (sdev->inquiry[6] & (1<<6)) : 1;
}

static inline int scsi_device_protection(struct scsi_device *sdev)
{
	return sdev->scsi_level > SCSI_2 && sdev->inquiry[5] & (1<<0);
}

static inline int scsi_device_tpgs(struct scsi_device *sdev)
{
	return sdev->inquiry ? (sdev->inquiry[5] >> 4) & 0x3 : 0;
}

#define MODULE_ALIAS_SCSI_DEVICE(type) \
	MODULE_ALIAS("scsi:t-" __stringify(type) "*")
#define SCSI_DEVICE_MODALIAS_FMT "scsi:t-0x%02x"

#endif /* _SCSI_SCSI_DEVICE_H */
