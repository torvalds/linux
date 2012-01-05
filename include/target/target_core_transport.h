#ifndef TARGET_CORE_TRANSPORT_H
#define TARGET_CORE_TRANSPORT_H

#define TARGET_CORE_VERSION			TARGET_CORE_MOD_VERSION

/* Attempts before moving from SHORT to LONG */
#define PYX_TRANSPORT_WINDOW_CLOSED_THRESHOLD	3
#define PYX_TRANSPORT_WINDOW_CLOSED_WAIT_SHORT	3  /* In milliseconds */
#define PYX_TRANSPORT_WINDOW_CLOSED_WAIT_LONG	10 /* In milliseconds */

#define PYX_TRANSPORT_STATUS_INTERVAL		5 /* In seconds */

#define TRANSPORT_PLUGIN_PHBA_PDEV		1
#define TRANSPORT_PLUGIN_VHBA_PDEV		2
#define TRANSPORT_PLUGIN_VHBA_VDEV		3

/*
 * struct se_subsystem_dev->su_dev_flags
*/
#define SDF_FIRMWARE_VPD_UNIT_SERIAL		0x00000001
#define SDF_EMULATED_VPD_UNIT_SERIAL		0x00000002
#define SDF_USING_UDEV_PATH			0x00000004
#define SDF_USING_ALIAS				0x00000008

/*
 * struct se_device->dev_flags
 */
#define DF_READ_ONLY				0x00000001
#define DF_SPC2_RESERVATIONS			0x00000002
#define DF_SPC2_RESERVATIONS_WITH_ISID		0x00000004

/* struct se_dev_attrib sanity values */
/* Default max_unmap_lba_count */
#define DA_MAX_UNMAP_LBA_COUNT			0
/* Default max_unmap_block_desc_count */
#define DA_MAX_UNMAP_BLOCK_DESC_COUNT		0
/* Default unmap_granularity */
#define DA_UNMAP_GRANULARITY_DEFAULT		0
/* Default unmap_granularity_alignment */
#define DA_UNMAP_GRANULARITY_ALIGNMENT_DEFAULT	0
/* Emulation for Direct Page Out */
#define DA_EMULATE_DPO				0
/* Emulation for Forced Unit Access WRITEs */
#define DA_EMULATE_FUA_WRITE			1
/* Emulation for Forced Unit Access READs */
#define DA_EMULATE_FUA_READ			0
/* Emulation for WriteCache and SYNCHRONIZE_CACHE */
#define DA_EMULATE_WRITE_CACHE			0
/* Emulation for UNIT ATTENTION Interlock Control */
#define DA_EMULATE_UA_INTLLCK_CTRL		0
/* Emulation for TASK_ABORTED status (TAS) by default */
#define DA_EMULATE_TAS				1
/* Emulation for Thin Provisioning UNMAP using block/blk-lib.c:blkdev_issue_discard() */
#define DA_EMULATE_TPU				0
/*
 * Emulation for Thin Provisioning WRITE_SAME w/ UNMAP=1 bit using
 * block/blk-lib.c:blkdev_issue_discard()
 */
#define DA_EMULATE_TPWS				0
/* No Emulation for PSCSI by default */
#define DA_EMULATE_RESERVATIONS			0
/* No Emulation for PSCSI by default */
#define DA_EMULATE_ALUA				0
/* Enforce SCSI Initiator Port TransportID with 'ISID' for PR */
#define DA_ENFORCE_PR_ISIDS			1
#define DA_STATUS_MAX_SECTORS_MIN		16
#define DA_STATUS_MAX_SECTORS_MAX		8192
/* By default don't report non-rotating (solid state) medium */
#define DA_IS_NONROT				0
/* Queue Algorithm Modifier default for restricted reordering in control mode page */
#define DA_EMULATE_REST_REORD			0

#define SE_MODE_PAGE_BUF			512

#define MOD_MAX_SECTORS(ms, bs)			(ms % (PAGE_SIZE / bs))

struct se_subsystem_api;

extern int init_se_kmem_caches(void);
extern void release_se_kmem_caches(void);
extern u32 scsi_get_new_index(scsi_index_t);
extern void transport_init_queue_obj(struct se_queue_obj *);
extern void transport_subsystem_check_init(void);
extern int transport_subsystem_register(struct se_subsystem_api *);
extern void transport_subsystem_release(struct se_subsystem_api *);
extern void transport_load_plugins(void);
extern struct se_session *transport_init_session(void);
extern void __transport_register_session(struct se_portal_group *,
					struct se_node_acl *,
					struct se_session *, void *);
extern void transport_register_session(struct se_portal_group *,
					struct se_node_acl *,
					struct se_session *, void *);
extern void transport_free_session(struct se_session *);
extern void transport_deregister_session_configfs(struct se_session *);
extern void transport_deregister_session(struct se_session *);
extern void transport_cmd_finish_abort(struct se_cmd *, int);
extern void transport_complete_sync_cache(struct se_cmd *, int);
extern void transport_complete_task(struct se_task *, int);
extern void transport_add_task_to_execute_queue(struct se_task *,
						struct se_task *,
						struct se_device *);
extern void transport_remove_task_from_execute_queue(struct se_task *,
						struct se_device *);
extern void __transport_remove_task_from_execute_queue(struct se_task *,
						struct se_device *);
unsigned char *transport_dump_cmd_direction(struct se_cmd *);
extern void transport_dump_dev_state(struct se_device *, char *, int *);
extern void transport_dump_dev_info(struct se_device *, struct se_lun *,
					unsigned long long, char *, int *);
extern void transport_dump_vpd_proto_id(struct t10_vpd *,
					unsigned char *, int);
extern void transport_set_vpd_proto_id(struct t10_vpd *, unsigned char *);
extern int transport_dump_vpd_assoc(struct t10_vpd *,
					unsigned char *, int);
extern int transport_set_vpd_assoc(struct t10_vpd *, unsigned char *);
extern int transport_dump_vpd_ident_type(struct t10_vpd *,
					unsigned char *, int);
extern int transport_set_vpd_ident_type(struct t10_vpd *, unsigned char *);
extern int transport_dump_vpd_ident(struct t10_vpd *,
					unsigned char *, int);
extern int transport_set_vpd_ident(struct t10_vpd *, unsigned char *);
extern struct se_device *transport_add_device_to_core_hba(struct se_hba *,
					struct se_subsystem_api *,
					struct se_subsystem_dev *, u32,
					void *, struct se_dev_limits *,
					const char *, const char *);
extern void transport_init_se_cmd(struct se_cmd *,
					struct target_core_fabric_ops *,
					struct se_session *, u32, int, int,
					unsigned char *);
void *transport_kmap_first_data_page(struct se_cmd *cmd);
void transport_kunmap_first_data_page(struct se_cmd *cmd);
extern int transport_generic_allocate_tasks(struct se_cmd *, unsigned char *);
extern int transport_handle_cdb_direct(struct se_cmd *);
extern int transport_generic_handle_cdb_map(struct se_cmd *);
extern int transport_generic_handle_data(struct se_cmd *);
extern int transport_generic_handle_tmr(struct se_cmd *);
extern bool target_stop_task(struct se_task *task, unsigned long *flags);
extern int transport_generic_map_mem_to_cmd(struct se_cmd *cmd, struct scatterlist *, u32,
				struct scatterlist *, u32);
extern int transport_clear_lun_from_sessions(struct se_lun *);
extern bool transport_wait_for_tasks(struct se_cmd *);
extern int transport_check_aborted_status(struct se_cmd *, int);
extern int transport_send_check_condition_and_sense(struct se_cmd *, u8, int);
extern void transport_send_task_abort(struct se_cmd *);
extern void transport_release_cmd(struct se_cmd *);
extern void transport_generic_free_cmd(struct se_cmd *, int);
extern void target_get_sess_cmd(struct se_session *, struct se_cmd *);
extern int target_put_sess_cmd(struct se_session *, struct se_cmd *);
extern void target_splice_sess_cmd_list(struct se_session *);
extern void target_wait_for_sess_cmds(struct se_session *, int);
extern void transport_generic_wait_for_cmds(struct se_cmd *, int);
extern void transport_do_task_sg_chain(struct se_cmd *);
extern void transport_generic_process_write(struct se_cmd *);
extern int transport_generic_new_cmd(struct se_cmd *);
extern int transport_generic_do_tmr(struct se_cmd *);
/* From target_core_alua.c */
extern int core_alua_check_nonop_delay(struct se_cmd *);
/* From target_core_cdb.c */
extern int transport_emulate_control_cdb(struct se_task *);
extern void target_get_task_cdb(struct se_task *task, unsigned char *cdb);

/*
 * Each se_transport_task_t can have N number of possible struct se_task's
 * for the storage transport(s) to possibly execute.
 * Used primarily for splitting up CDBs that exceed the physical storage
 * HBA's maximum sector count per task.
 */
struct se_mem {
	struct page	*se_page;
	u32		se_len;
	u32		se_off;
	struct list_head se_list;
} ____cacheline_aligned;

/*
 * 	Each type of disk transport supported MUST have a template defined
 *	within its .h file.
 */
struct se_subsystem_api {
	/*
	 * The Name. :-)
	 */
	char name[16];
	/*
	 * Transport Type.
	 */
	u8 transport_type;

	unsigned int fua_write_emulated : 1;
	unsigned int write_cache_emulated : 1;

	/*
	 * struct module for struct se_hba references
	 */
	struct module *owner;
	/*
	 * Used for global se_subsystem_api list_head
	 */
	struct list_head sub_api_list;
	/*
	 * attach_hba():
	 */
	int (*attach_hba)(struct se_hba *, u32);
	/*
	 * detach_hba():
	 */
	void (*detach_hba)(struct se_hba *);
	/*
	 * pmode_hba(): Used for TCM/pSCSI subsystem plugin HBA ->
	 *		Linux/SCSI struct Scsi_Host passthrough
	*/
	int (*pmode_enable_hba)(struct se_hba *, unsigned long);
	/*
	 * allocate_virtdevice():
	 */
	void *(*allocate_virtdevice)(struct se_hba *, const char *);
	/*
	 * create_virtdevice(): Only for Virtual HBAs
	 */
	struct se_device *(*create_virtdevice)(struct se_hba *,
				struct se_subsystem_dev *, void *);
	/*
	 * free_device():
	 */
	void (*free_device)(void *);

	/*
	 * transport_complete():
	 *
	 * Use transport_generic_complete() for majority of DAS transport
	 * drivers.  Provided out of convenience.
	 */
	int (*transport_complete)(struct se_task *task);
	struct se_task *(*alloc_task)(unsigned char *cdb);
	/*
	 * do_task():
	 */
	int (*do_task)(struct se_task *);
	/*
	 * Used by virtual subsystem plugins IBLOCK and FILEIO to emulate
	 * UNMAP and WRITE_SAME_* w/ UNMAP=1 <-> Linux/Block Discard
	 */
	int (*do_discard)(struct se_device *, sector_t, u32);
	/*
	 * Used  by virtual subsystem plugins IBLOCK and FILEIO to emulate
	 * SYNCHRONIZE_CACHE_* <-> Linux/Block blkdev_issue_flush()
	 */
	void (*do_sync_cache)(struct se_task *);
	/*
	 * free_task():
	 */
	void (*free_task)(struct se_task *);
	/*
	 * check_configfs_dev_params():
	 */
	ssize_t (*check_configfs_dev_params)(struct se_hba *, struct se_subsystem_dev *);
	/*
	 * set_configfs_dev_params():
	 */
	ssize_t (*set_configfs_dev_params)(struct se_hba *, struct se_subsystem_dev *,
						const char *, ssize_t);
	/*
	 * show_configfs_dev_params():
	 */
	ssize_t (*show_configfs_dev_params)(struct se_hba *, struct se_subsystem_dev *,
						char *);
	/*
	 * get_device_rev():
	 */
	u32 (*get_device_rev)(struct se_device *);
	/*
	 * get_device_type():
	 */
	u32 (*get_device_type)(struct se_device *);
	/*
	 * Get the sector_t from a subsystem backstore..
	 */
	sector_t (*get_blocks)(struct se_device *);
	/*
	 * get_sense_buffer():
	 */
	unsigned char *(*get_sense_buffer)(struct se_task *);
} ____cacheline_aligned;

#endif /* TARGET_CORE_TRANSPORT_H */
