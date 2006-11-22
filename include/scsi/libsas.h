/*
 * SAS host prototypes and structures header file
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#ifndef _LIBSAS_H_
#define _LIBSAS_H_


#include <linux/timer.h>
#include <linux/pci.h>
#include <scsi/sas.h>
#include <linux/list.h>
#include <asm/semaphore.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport_sas.h>

struct block_device;

enum sas_class {
	SAS,
	EXPANDER
};

enum sas_phy_role {
	PHY_ROLE_NONE = 0,
	PHY_ROLE_TARGET = 0x40,
	PHY_ROLE_INITIATOR = 0x80,
};

enum sas_phy_type {
        PHY_TYPE_PHYSICAL,
        PHY_TYPE_VIRTUAL
};

/* The events are mnemonically described in sas_dump.c
 * so when updating/adding events here, please also
 * update the other file too.
 */
enum ha_event {
	HAE_RESET             = 0U,
	HA_NUM_EVENTS         = 1,
};

enum port_event {
	PORTE_BYTES_DMAED     = 0U,
	PORTE_BROADCAST_RCVD  = 1,
	PORTE_LINK_RESET_ERR  = 2,
	PORTE_TIMER_EVENT     = 3,
	PORTE_HARD_RESET      = 4,
	PORT_NUM_EVENTS       = 5,
};

enum phy_event {
	PHYE_LOSS_OF_SIGNAL   = 0U,
	PHYE_OOB_DONE         = 1,
	PHYE_OOB_ERROR        = 2,
	PHYE_SPINUP_HOLD      = 3, /* hot plug SATA, no COMWAKE sent */
	PHY_NUM_EVENTS        = 4,
};

enum discover_event {
	DISCE_DISCOVER_DOMAIN   = 0U,
	DISCE_REVALIDATE_DOMAIN = 1,
	DISCE_PORT_GONE         = 2,
	DISC_NUM_EVENTS 	= 3,
};

/* ---------- Expander Devices ---------- */

#define ETASK 0xFA

#define to_dom_device(_obj) container_of(_obj, struct domain_device, dev_obj)
#define to_dev_attr(_attr)  container_of(_attr, struct domain_dev_attribute,\
                                         attr)

enum routing_attribute {
	DIRECT_ROUTING,
	SUBTRACTIVE_ROUTING,
	TABLE_ROUTING,
};

enum ex_phy_state {
	PHY_EMPTY,
	PHY_VACANT,
	PHY_NOT_PRESENT,
	PHY_DEVICE_DISCOVERED
};

struct ex_phy {
	int    phy_id;

	enum ex_phy_state phy_state;

	enum sas_dev_type attached_dev_type;
	enum sas_linkrate linkrate;

	u8   attached_sata_host:1;
	u8   attached_sata_dev:1;
	u8   attached_sata_ps:1;

	enum sas_proto attached_tproto;
	enum sas_proto attached_iproto;

	u8   attached_sas_addr[SAS_ADDR_SIZE];
	u8   attached_phy_id;

	u8   phy_change_count;
	enum routing_attribute routing_attr;
	u8   virtual:1;

	int  last_da_index;

	struct sas_phy *phy;
	struct sas_port *port;
};

struct expander_device {
	struct list_head children;

	u16    ex_change_count;
	u16    max_route_indexes;
	u8     num_phys;
	u8     configuring:1;
	u8     conf_route_table:1;
	u8     enclosure_logical_id[8];

	struct ex_phy *ex_phy;
	struct sas_port *parent_port;
};

/* ---------- SATA device ---------- */
enum ata_command_set {
        ATA_COMMAND_SET   = 0,
        ATAPI_COMMAND_SET = 1,
};

struct sata_device {
        enum   ata_command_set command_set;
        struct smp_resp        rps_resp; /* report_phy_sata_resp */
        __le16 *identify_device;
        __le16 *identify_packet_device;

        u8     port_no;        /* port number, if this is a PM (Port) */
        struct list_head children; /* PM Ports if this is a PM */
};

/* ---------- Domain device ---------- */
struct domain_device {
        enum sas_dev_type dev_type;

        enum sas_linkrate linkrate;
        enum sas_linkrate min_linkrate;
        enum sas_linkrate max_linkrate;

        int  pathways;

        struct domain_device *parent;
        struct list_head siblings; /* devices on the same level */
        struct asd_sas_port *port;        /* shortcut to root of the tree */

        struct list_head dev_list_node;

        enum sas_proto    iproto;
        enum sas_proto    tproto;

        struct sas_rphy *rphy;

        u8  sas_addr[SAS_ADDR_SIZE];
        u8  hashed_sas_addr[HASHED_SAS_ADDR_SIZE];

        u8  frame_rcvd[32];

        union {
                struct expander_device ex_dev;
                struct sata_device     sata_dev; /* STP & directly attached */
        };

        void *lldd_dev;
};

struct sas_discovery_event {
	struct work_struct work;
	struct asd_sas_port *port;
};

struct sas_discovery {
	spinlock_t disc_event_lock;
	struct sas_discovery_event disc_work[DISC_NUM_EVENTS];
	unsigned long    pending;
	u8     fanout_sas_addr[8];
	u8     eeds_a[8];
	u8     eeds_b[8];
	int    max_level;
};


/* The port struct is Class:RW, driver:RO */
struct asd_sas_port {
/* private: */
	struct completion port_gone_completion;

	struct sas_discovery disc;
	struct domain_device *port_dev;
	spinlock_t dev_list_lock;
	struct list_head dev_list;
	enum   sas_linkrate linkrate;

	struct sas_phy *phy;
	struct work_struct work;

/* public: */
	int id;

	enum sas_class   class;
	u8               sas_addr[SAS_ADDR_SIZE];
	u8               attached_sas_addr[SAS_ADDR_SIZE];
	enum sas_proto   iproto;
	enum sas_proto   tproto;

	enum sas_oob_mode oob_mode;

	spinlock_t       phy_list_lock;
	struct list_head phy_list;
	int              num_phys;
	u32              phy_mask;

	struct sas_ha_struct *ha;

	struct sas_port	*port;

	void *lldd_port;	  /* not touched by the sas class code */
};

struct asd_sas_event {
	struct work_struct work;
	struct asd_sas_phy *phy;
};

/* The phy pretty much is controlled by the LLDD.
 * The class only reads those fields.
 */
struct asd_sas_phy {
/* private: */
	/* protected by ha->event_lock */
	struct asd_sas_event   port_events[PORT_NUM_EVENTS];
	struct asd_sas_event   phy_events[PHY_NUM_EVENTS];

	unsigned long port_events_pending;
	unsigned long phy_events_pending;

	int error;

	struct sas_phy *phy;

/* public: */
	/* The following are class:RO, driver:R/W */
	int            enabled;	  /* must be set */

	int            id;	  /* must be set */
	enum sas_class class;
	enum sas_proto iproto;
	enum sas_proto tproto;

	enum sas_phy_type  type;
	enum sas_phy_role  role;
	enum sas_oob_mode  oob_mode;
	enum sas_linkrate linkrate;

	u8   *sas_addr;		  /* must be set */
	u8   attached_sas_addr[SAS_ADDR_SIZE]; /* class:RO, driver: R/W */

	spinlock_t     frame_rcvd_lock;
	u8             *frame_rcvd; /* must be set */
	int            frame_rcvd_size;

	spinlock_t     sas_prim_lock;
	u32            sas_prim;

	struct list_head port_phy_el; /* driver:RO */
	struct asd_sas_port      *port; /* Class:RW, driver: RO */

	struct sas_ha_struct *ha; /* may be set; the class sets it anyway */

	void *lldd_phy;		  /* not touched by the sas_class_code */
};

struct scsi_core {
	struct Scsi_Host *shost;

	spinlock_t        task_queue_lock;
	struct list_head  task_queue;
	int               task_queue_size;

	struct semaphore  queue_thread_sema;
	int               queue_thread_kill;
};

struct sas_ha_event {
	struct work_struct work;
	struct sas_ha_struct *ha;
};

struct sas_ha_struct {
/* private: */
	spinlock_t       event_lock;
	struct sas_ha_event ha_events[HA_NUM_EVENTS];
	unsigned long	 pending;

	struct scsi_core core;

/* public: */
	char *sas_ha_name;
	struct pci_dev *pcidev;	  /* should be set */
	struct module *lldd_module; /* should be set */

	u8 *sas_addr;		  /* must be set */
	u8 hashed_sas_addr[HASHED_SAS_ADDR_SIZE];

	spinlock_t      phy_port_lock;
	struct asd_sas_phy  **sas_phy; /* array of valid pointers, must be set */
	struct asd_sas_port **sas_port; /* array of valid pointers, must be set */
	int             num_phys; /* must be set, gt 0, static */

	/* The class calls this to send a task for execution. */
	int lldd_max_execute_num;
	int lldd_queue_size;

	/* LLDD calls these to notify the class of an event. */
	void (*notify_ha_event)(struct sas_ha_struct *, enum ha_event);
	void (*notify_port_event)(struct asd_sas_phy *, enum port_event);
	void (*notify_phy_event)(struct asd_sas_phy *, enum phy_event);

	void *lldd_ha;		  /* not touched by sas class code */
};

#define SHOST_TO_SAS_HA(_shost) (*(struct sas_ha_struct **)(_shost)->hostdata)

static inline struct domain_device *
starget_to_domain_dev(struct scsi_target *starget) {
	return starget->hostdata;
}

static inline struct domain_device *
sdev_to_domain_dev(struct scsi_device *sdev) {
	return starget_to_domain_dev(sdev->sdev_target);
}

static inline struct domain_device *
cmd_to_domain_dev(struct scsi_cmnd *cmd)
{
	return sdev_to_domain_dev(cmd->device);
}

void sas_hash_addr(u8 *hashed, const u8 *sas_addr);

/* Before calling a notify event, LLDD should use this function
 * when the link is severed (possibly from its tasklet).
 * The idea is that the Class only reads those, while the LLDD,
 * can R/W these (thus avoiding a race).
 */
static inline void sas_phy_disconnected(struct asd_sas_phy *phy)
{
	phy->oob_mode = OOB_NOT_CONNECTED;
	phy->linkrate = SAS_LINK_RATE_UNKNOWN;
}

/* ---------- Tasks ---------- */
/*
      service_response |  SAS_TASK_COMPLETE  |  SAS_TASK_UNDELIVERED |
  exec_status          |                     |                       |
  ---------------------+---------------------+-----------------------+
       SAM_...         |         X           |                       |
       DEV_NO_RESPONSE |         X           |           X           |
       INTERRUPTED     |         X           |                       |
       QUEUE_FULL      |                     |           X           |
       DEVICE_UNKNOWN  |                     |           X           |
       SG_ERR          |                     |           X           |
  ---------------------+---------------------+-----------------------+
 */

enum service_response {
	SAS_TASK_COMPLETE,
	SAS_TASK_UNDELIVERED = -1,
};

enum exec_status {
	SAM_GOOD         = 0,
	SAM_CHECK_COND   = 2,
	SAM_COND_MET     = 4,
	SAM_BUSY         = 8,
	SAM_INTERMEDIATE = 0x10,
	SAM_IM_COND_MET  = 0x12,
	SAM_RESV_CONFLICT= 0x14,
	SAM_TASK_SET_FULL= 0x28,
	SAM_ACA_ACTIVE   = 0x30,
	SAM_TASK_ABORTED = 0x40,

	SAS_DEV_NO_RESPONSE = 0x80,
	SAS_DATA_UNDERRUN,
	SAS_DATA_OVERRUN,
	SAS_INTERRUPTED,
	SAS_QUEUE_FULL,
	SAS_DEVICE_UNKNOWN,
	SAS_SG_ERR,
	SAS_OPEN_REJECT,
	SAS_OPEN_TO,
	SAS_PROTO_RESPONSE,
	SAS_PHY_DOWN,
	SAS_NAK_R_ERR,
	SAS_PENDING,
	SAS_ABORTED_TASK,
};

/* When a task finishes with a response, the LLDD examines the
 * response:
 * 	- For an ATA task task_status_struct::stat is set to
 * SAS_PROTO_RESPONSE, and the task_status_struct::buf is set to the
 * contents of struct ata_task_resp.
 * 	- For SSP tasks, if no data is present or status/TMF response
 * is valid, task_status_struct::stat is set.  If data is present
 * (SENSE data), the LLDD copies up to SAS_STATUS_BUF_SIZE, sets
 * task_status_struct::buf_valid_size, and task_status_struct::stat is
 * set to SAM_CHECK_COND.
 *
 * "buf" has format SCSI Sense for SSP task, or struct ata_task_resp
 * for ATA task.
 *
 * "frame_len" is the total frame length, which could be more or less
 * than actually copied.
 *
 * Tasks ending with response, always set the residual field.
 */
struct ata_task_resp {
	u16  frame_len;
	u8   ending_fis[24];	  /* dev to host or data-in */
	u32  sstatus;
	u32  serror;
	u32  scontrol;
	u32  sactive;
};

#define SAS_STATUS_BUF_SIZE 96

struct task_status_struct {
	enum service_response resp;
	enum exec_status      stat;
	int  buf_valid_size;

	u8   buf[SAS_STATUS_BUF_SIZE];

	u32  residual;
	enum sas_open_rej_reason open_rej_reason;
};

/* ATA and ATAPI task queuable to a SAS LLDD.
 */
struct sas_ata_task {
	struct host_to_dev_fis fis;
	u8     atapi_packet[16];  /* 0 if not ATAPI task */

	u8     retry_count;	  /* hardware retry, should be > 0 */

	u8     dma_xfer:1;	  /* PIO:0 or DMA:1 */
	u8     use_ncq:1;
	u8     set_affil_pol:1;
	u8     stp_affil_pol:1;

	u8     device_control_reg_update:1;
};

struct sas_smp_task {
	struct scatterlist smp_req;
	struct scatterlist smp_resp;
};

enum task_attribute {
	TASK_ATTR_SIMPLE = 0,
	TASK_ATTR_HOQ    = 1,
	TASK_ATTR_ORDERED= 2,
	TASK_ATTR_ACA    = 4,
};

struct sas_ssp_task {
	u8     retry_count;	  /* hardware retry, should be > 0 */

	u8     LUN[8];
	u8     enable_first_burst:1;
	enum   task_attribute task_attr;
	u8     task_prio;
	u8     cdb[16];
};

struct sas_task {
	struct domain_device *dev;
	struct list_head      list;

	spinlock_t   task_state_lock;
	unsigned     task_state_flags;

	enum   sas_proto      task_proto;

	/* Used by the discovery code. */
	struct timer_list     timer;
	struct completion     completion;

	union {
		struct sas_ata_task ata_task;
		struct sas_smp_task smp_task;
		struct sas_ssp_task ssp_task;
	};

	struct scatterlist *scatter;
	int    num_scatter;
	u32    total_xfer_len;
	u8     data_dir:2;	  /* Use PCI_DMA_... */

	struct task_status_struct task_status;
	void   (*task_done)(struct sas_task *);

	void   *lldd_task;	  /* for use by LLDDs */
	void   *uldd_task;
};



#define SAS_TASK_STATE_PENDING  1
#define SAS_TASK_STATE_DONE     2
#define SAS_TASK_STATE_ABORTED  4

static inline struct sas_task *sas_alloc_task(gfp_t flags)
{
	extern kmem_cache_t *sas_task_cache;
	struct sas_task *task = kmem_cache_alloc(sas_task_cache, flags);

	if (task) {
		memset(task, 0, sizeof(*task));
		INIT_LIST_HEAD(&task->list);
		spin_lock_init(&task->task_state_lock);
		task->task_state_flags = SAS_TASK_STATE_PENDING;
		init_timer(&task->timer);
		init_completion(&task->completion);
	}

	return task;
}

static inline void sas_free_task(struct sas_task *task)
{
	if (task) {
		extern kmem_cache_t *sas_task_cache;
		BUG_ON(!list_empty(&task->list));
		kmem_cache_free(sas_task_cache, task);
	}
}

struct sas_domain_function_template {
	/* The class calls these to notify the LLDD of an event. */
	void (*lldd_port_formed)(struct asd_sas_phy *);
	void (*lldd_port_deformed)(struct asd_sas_phy *);

	/* The class calls these when a device is found or gone. */
	int  (*lldd_dev_found)(struct domain_device *);
	void (*lldd_dev_gone)(struct domain_device *);

	int (*lldd_execute_task)(struct sas_task *, int num,
				 gfp_t gfp_flags);

	/* Task Management Functions. Must be called from process context. */
	int (*lldd_abort_task)(struct sas_task *);
	int (*lldd_abort_task_set)(struct domain_device *, u8 *lun);
	int (*lldd_clear_aca)(struct domain_device *, u8 *lun);
	int (*lldd_clear_task_set)(struct domain_device *, u8 *lun);
	int (*lldd_I_T_nexus_reset)(struct domain_device *);
	int (*lldd_lu_reset)(struct domain_device *, u8 *lun);
	int (*lldd_query_task)(struct sas_task *);

	/* Port and Adapter management */
	int (*lldd_clear_nexus_port)(struct asd_sas_port *);
	int (*lldd_clear_nexus_ha)(struct sas_ha_struct *);

	/* Phy management */
	int (*lldd_control_phy)(struct asd_sas_phy *, enum phy_func, void *);
};

extern int sas_register_ha(struct sas_ha_struct *);
extern int sas_unregister_ha(struct sas_ha_struct *);

extern int sas_queuecommand(struct scsi_cmnd *,
		     void (*scsi_done)(struct scsi_cmnd *));
extern int sas_target_alloc(struct scsi_target *);
extern int sas_slave_alloc(struct scsi_device *);
extern int sas_slave_configure(struct scsi_device *);
extern void sas_slave_destroy(struct scsi_device *);
extern int sas_change_queue_depth(struct scsi_device *, int new_depth);
extern int sas_change_queue_type(struct scsi_device *, int qt);
extern int sas_bios_param(struct scsi_device *,
			  struct block_device *,
			  sector_t capacity, int *hsc);
extern struct scsi_transport_template *
sas_domain_attach_transport(struct sas_domain_function_template *);
extern void sas_domain_release_transport(struct scsi_transport_template *);

int  sas_discover_root_expander(struct domain_device *);

void sas_init_ex_attr(void);

int  sas_ex_revalidate_domain(struct domain_device *);

void sas_unregister_domain_devices(struct asd_sas_port *port);
void sas_init_disc(struct sas_discovery *disc, struct asd_sas_port *);
int  sas_discover_event(struct asd_sas_port *, enum discover_event ev);

int  sas_discover_sata(struct domain_device *);
int  sas_discover_end_dev(struct domain_device *);

void sas_unregister_dev(struct domain_device *);

void sas_init_dev(struct domain_device *);

#endif /* _SASLIB_H_ */
