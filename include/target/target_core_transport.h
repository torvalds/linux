#ifndef TARGET_CORE_TRANSPORT_H
#define TARGET_CORE_TRANSPORT_H

#define TARGET_CORE_VERSION			TARGET_CORE_MOD_VERSION

/* Attempts before moving from SHORT to LONG */
#define PYX_TRANSPORT_WINDOW_CLOSED_THRESHOLD	3
#define PYX_TRANSPORT_WINDOW_CLOSED_WAIT_SHORT	3  /* In milliseconds */
#define PYX_TRANSPORT_WINDOW_CLOSED_WAIT_LONG	10 /* In milliseconds */

#define PYX_TRANSPORT_STATUS_INTERVAL		5 /* In seconds */

#define PYX_TRANSPORT_SENT_TO_TRANSPORT		0
#define PYX_TRANSPORT_WRITE_PENDING		1

#define PYX_TRANSPORT_UNKNOWN_SAM_OPCODE	-1
#define PYX_TRANSPORT_HBA_QUEUE_FULL		-2
#define PYX_TRANSPORT_REQ_TOO_MANY_SECTORS	-3
#define PYX_TRANSPORT_OUT_OF_MEMORY_RESOURCES	-4
#define PYX_TRANSPORT_INVALID_CDB_FIELD		-5
#define PYX_TRANSPORT_INVALID_PARAMETER_LIST	-6
#define PYX_TRANSPORT_LU_COMM_FAILURE		-7
#define PYX_TRANSPORT_UNKNOWN_MODE_PAGE		-8
#define PYX_TRANSPORT_WRITE_PROTECTED		-9
#define PYX_TRANSPORT_TASK_TIMEOUT		-10
#define PYX_TRANSPORT_RESERVATION_CONFLICT	-11
#define PYX_TRANSPORT_ILLEGAL_REQUEST		-12
#define PYX_TRANSPORT_USE_SENSE_REASON		-13

#ifndef SAM_STAT_RESERVATION_CONFLICT
#define SAM_STAT_RESERVATION_CONFLICT		0x18
#endif

#define TRANSPORT_PLUGIN_FREE			0
#define TRANSPORT_PLUGIN_REGISTERED		1

#define TRANSPORT_PLUGIN_PHBA_PDEV		1
#define TRANSPORT_PLUGIN_VHBA_PDEV		2
#define TRANSPORT_PLUGIN_VHBA_VDEV		3

/* For SE OBJ Plugins, in seconds */
#define TRANSPORT_TIMEOUT_TUR			10
#define TRANSPORT_TIMEOUT_TYPE_DISK		60
#define TRANSPORT_TIMEOUT_TYPE_ROM		120
#define TRANSPORT_TIMEOUT_TYPE_TAPE		600
#define TRANSPORT_TIMEOUT_TYPE_OTHER		300

/* For se_task->task_state_flags */
#define TSF_EXCEPTION_CLEARED			0x01

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
/* 10 Minutes */
#define DA_TASK_TIMEOUT_MAX			600
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

#define SE_MODE_PAGE_BUF			512

#define MOD_MAX_SECTORS(ms, bs)			(ms % (PAGE_SIZE / bs))

struct se_mem;
struct se_subsystem_api;

extern int init_se_global(void);
extern void release_se_global(void);
extern void init_scsi_index_table(void);
extern u32 scsi_get_new_index(scsi_index_t);
extern void transport_init_queue_obj(struct se_queue_obj *);
extern int transport_subsystem_check_init(void);
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
extern void transport_cmd_finish_abort_tmr(struct se_cmd *);
extern void transport_complete_sync_cache(struct se_cmd *, int);
extern void transport_complete_task(struct se_task *, int);
extern void transport_add_task_to_execute_queue(struct se_task *,
						struct se_task *,
						struct se_device *);
extern void transport_remove_task_from_execute_queue(struct se_task *,
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
extern void transport_device_setup_cmd(struct se_cmd *);
extern void transport_init_se_cmd(struct se_cmd *,
					struct target_core_fabric_ops *,
					struct se_session *, u32, int, int,
					unsigned char *);
extern void transport_free_se_cmd(struct se_cmd *);
extern int transport_generic_allocate_tasks(struct se_cmd *, unsigned char *);
extern int transport_generic_handle_cdb(struct se_cmd *);
extern int transport_generic_handle_cdb_map(struct se_cmd *);
extern int transport_generic_handle_data(struct se_cmd *);
extern void transport_new_cmd_failure(struct se_cmd *);
extern int transport_generic_handle_tmr(struct se_cmd *);
extern void __transport_stop_task_timer(struct se_task *, unsigned long *);
extern unsigned char transport_asciihex_to_binaryhex(unsigned char val[2]);
extern int transport_generic_map_mem_to_cmd(struct se_cmd *cmd, struct scatterlist *, u32,
				struct scatterlist *, u32);
extern int transport_clear_lun_from_sessions(struct se_lun *);
extern int transport_check_aborted_status(struct se_cmd *, int);
extern int transport_send_check_condition_and_sense(struct se_cmd *, u8, int);
extern void transport_send_task_abort(struct se_cmd *);
extern void transport_release_cmd_to_pool(struct se_cmd *);
extern void transport_generic_free_cmd(struct se_cmd *, int, int, int);
extern void transport_generic_wait_for_cmds(struct se_cmd *, int);
extern u32 transport_calc_sg_num(struct se_task *, struct se_mem *, u32);
extern int transport_map_mem_to_sg(struct se_task *, struct list_head *,
					void *, struct se_mem *,
					struct se_mem **, u32 *, u32 *);
extern void transport_do_task_sg_chain(struct se_cmd *);
extern void transport_generic_process_write(struct se_cmd *);
extern int transport_generic_do_tmr(struct se_cmd *);
/* From target_core_alua.c */
extern int core_alua_check_nonop_delay(struct se_cmd *);

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
	/*
	 * struct module for struct se_hba references
	 */
	struct module *owner;
	/*
	 * Used for global se_subsystem_api list_head
	 */
	struct list_head sub_api_list;
	/*
	 * For SCF_SCSI_NON_DATA_CDB
	 */
	int (*cdb_none)(struct se_task *);
	/*
	 * For SCF_SCSI_CONTROL_NONSG_IO_CDB
	 */
	int (*map_task_non_SG)(struct se_task *);
	/*
	 * For SCF_SCSI_DATA_SG_IO_CDB and SCF_SCSI_CONTROL_SG_IO_CDB
	 */
	int (*map_task_SG)(struct se_task *);
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
	 * dpo_emulated():
	 */
	int (*dpo_emulated)(struct se_device *);
	/*
	 * fua_write_emulated():
	 */
	int (*fua_write_emulated)(struct se_device *);
	/*
	 * fua_read_emulated():
	 */
	int (*fua_read_emulated)(struct se_device *);
	/*
	 * write_cache_emulated():
	 */
	int (*write_cache_emulated)(struct se_device *);
	/*
	 * transport_complete():
	 *
	 * Use transport_generic_complete() for majority of DAS transport
	 * drivers.  Provided out of convenience.
	 */
	int (*transport_complete)(struct se_task *task);
	struct se_task *(*alloc_task)(struct se_cmd *);
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
	 * get_cdb():
	 */
	unsigned char *(*get_cdb)(struct se_task *);
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
	 * do_se_mem_map():
	 */
	int (*do_se_mem_map)(struct se_task *, struct list_head *, void *,
				struct se_mem *, struct se_mem **, u32 *, u32 *);
	/*
	 * get_sense_buffer():
	 */
	unsigned char *(*get_sense_buffer)(struct se_task *);
} ____cacheline_aligned;

#define TRANSPORT(dev)		((dev)->transport)
#define HBA_TRANSPORT(hba)	((hba)->transport)

extern struct se_global *se_global;

#endif /* TARGET_CORE_TRANSPORT_H */
