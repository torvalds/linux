#ifndef TARGET_CORE_BASE_H
#define TARGET_CORE_BASE_H

#include <linux/in.h>
#include <linux/configfs.h>
#include <linux/dma-mapping.h>
#include <linux/blkdev.h>
#include <scsi/scsi_cmnd.h>
#include <net/sock.h>
#include <net/tcp.h>
#include "target_core_mib.h"

#define TARGET_CORE_MOD_VERSION		"v4.0.0-rc6"
#define SHUTDOWN_SIGS	(sigmask(SIGKILL)|sigmask(SIGINT)|sigmask(SIGABRT))

/* Used by transport_generic_allocate_iovecs() */
#define TRANSPORT_IOV_DATA_BUFFER		5
/* Maximum Number of LUNs per Target Portal Group */
#define TRANSPORT_MAX_LUNS_PER_TPG		256
/*
 * By default we use 32-byte CDBs in TCM Core and subsystem plugin code.
 *
 * Note that both include/scsi/scsi_cmnd.h:MAX_COMMAND_SIZE and
 * include/linux/blkdev.h:BLOCK_MAX_CDB as of v2.6.36-rc4 still use
 * 16-byte CDBs by default and require an extra allocation for
 * 32-byte CDBs to becasue of legacy issues.
 *
 * Within TCM Core there are no such legacy limitiations, so we go ahead
 * use 32-byte CDBs by default and use include/scsi/scsi.h:scsi_command_size()
 * within all TCM Core and subsystem plugin code.
 */
#define TCM_MAX_COMMAND_SIZE			32
/*
 * From include/scsi/scsi_cmnd.h:SCSI_SENSE_BUFFERSIZE, currently
 * defined 96, but the real limit is 252 (or 260 including the header)
 */
#define TRANSPORT_SENSE_BUFFER			SCSI_SENSE_BUFFERSIZE
/* Used by transport_send_check_condition_and_sense() */
#define SPC_SENSE_KEY_OFFSET			2
#define SPC_ASC_KEY_OFFSET			12
#define SPC_ASCQ_KEY_OFFSET			13
#define TRANSPORT_IQN_LEN			224
/* Used by target_core_store_alua_lu_gp() and target_core_alua_lu_gp_show_attr_members() */
#define LU_GROUP_NAME_BUF			256
/* Used by core_alua_store_tg_pt_gp_info() and target_core_alua_tg_pt_gp_show_attr_members() */
#define TG_PT_GROUP_NAME_BUF			256
/* Used to parse VPD into struct t10_vpd */
#define VPD_TMP_BUF_SIZE			128
/* Used by transport_generic_cmd_sequencer() */
#define READ_BLOCK_LEN          		6
#define READ_CAP_LEN            		8
#define READ_POSITION_LEN       		20
#define INQUIRY_LEN				36
/* Used by transport_get_inquiry_vpd_serial() */
#define INQUIRY_VPD_SERIAL_LEN			254
/* Used by transport_get_inquiry_vpd_device_ident() */
#define INQUIRY_VPD_DEVICE_IDENTIFIER_LEN	254

/* struct se_hba->hba_flags */
enum hba_flags_table {
	HBA_FLAGS_INTERNAL_USE	= 0x01,
	HBA_FLAGS_PSCSI_MODE	= 0x02,
};

/* struct se_lun->lun_status */
enum transport_lun_status_table {
	TRANSPORT_LUN_STATUS_FREE = 0,
	TRANSPORT_LUN_STATUS_ACTIVE = 1,
};

/* struct se_portal_group->se_tpg_type */
enum transport_tpg_type_table {
	TRANSPORT_TPG_TYPE_NORMAL = 0,
	TRANSPORT_TPG_TYPE_DISCOVERY = 1,
};

/* Used for generate timer flags */
enum timer_flags_table {
	TF_RUNNING	= 0x01,
	TF_STOP		= 0x02,
};

/* Special transport agnostic struct se_cmd->t_states */
enum transport_state_table {
	TRANSPORT_NO_STATE	= 0,
	TRANSPORT_NEW_CMD	= 1,
	TRANSPORT_DEFERRED_CMD	= 2,
	TRANSPORT_WRITE_PENDING	= 3,
	TRANSPORT_PROCESS_WRITE	= 4,
	TRANSPORT_PROCESSING	= 5,
	TRANSPORT_COMPLETE_OK	= 6,
	TRANSPORT_COMPLETE_FAILURE = 7,
	TRANSPORT_COMPLETE_TIMEOUT = 8,
	TRANSPORT_PROCESS_TMR	= 9,
	TRANSPORT_TMR_COMPLETE	= 10,
	TRANSPORT_ISTATE_PROCESSING = 11,
	TRANSPORT_ISTATE_PROCESSED = 12,
	TRANSPORT_KILL		= 13,
	TRANSPORT_REMOVE	= 14,
	TRANSPORT_FREE		= 15,
	TRANSPORT_NEW_CMD_MAP	= 16,
};

/* Used for struct se_cmd->se_cmd_flags */
enum se_cmd_flags_table {
	SCF_SUPPORTED_SAM_OPCODE	= 0x00000001,
	SCF_TRANSPORT_TASK_SENSE	= 0x00000002,
	SCF_EMULATED_TASK_SENSE		= 0x00000004,
	SCF_SCSI_DATA_SG_IO_CDB		= 0x00000008,
	SCF_SCSI_CONTROL_SG_IO_CDB	= 0x00000010,
	SCF_SCSI_CONTROL_NONSG_IO_CDB	= 0x00000020,
	SCF_SCSI_NON_DATA_CDB		= 0x00000040,
	SCF_SCSI_CDB_EXCEPTION		= 0x00000080,
	SCF_SCSI_RESERVATION_CONFLICT	= 0x00000100,
	SCF_CMD_PASSTHROUGH_NOALLOC	= 0x00000200,
	SCF_SE_CMD_FAILED		= 0x00000400,
	SCF_SE_LUN_CMD			= 0x00000800,
	SCF_SE_ALLOW_EOO		= 0x00001000,
	SCF_SE_DISABLE_ONLINE_CHECK	= 0x00002000,
	SCF_SENT_CHECK_CONDITION	= 0x00004000,
	SCF_OVERFLOW_BIT		= 0x00008000,
	SCF_UNDERFLOW_BIT		= 0x00010000,
	SCF_SENT_DELAYED_TAS		= 0x00020000,
	SCF_ALUA_NON_OPTIMIZED		= 0x00040000,
	SCF_DELAYED_CMD_FROM_SAM_ATTR	= 0x00080000,
	SCF_PASSTHROUGH_SG_TO_MEM	= 0x00100000,
	SCF_PASSTHROUGH_CONTIG_TO_SG	= 0x00200000,
	SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC = 0x00400000,
	SCF_EMULATE_SYNC_CACHE		= 0x00800000,
	SCF_EMULATE_CDB_ASYNC		= 0x01000000,
	SCF_EMULATE_SYNC_UNMAP		= 0x02000000
};

/* struct se_dev_entry->lun_flags and struct se_lun->lun_access */
enum transport_lunflags_table {
	TRANSPORT_LUNFLAGS_NO_ACCESS		= 0x00,
	TRANSPORT_LUNFLAGS_INITIATOR_ACCESS	= 0x01,
	TRANSPORT_LUNFLAGS_READ_ONLY		= 0x02,
	TRANSPORT_LUNFLAGS_READ_WRITE		= 0x04,
};

/* struct se_device->dev_status */
enum transport_device_status_table {
	TRANSPORT_DEVICE_ACTIVATED		= 0x01,
	TRANSPORT_DEVICE_DEACTIVATED		= 0x02,
	TRANSPORT_DEVICE_QUEUE_FULL		= 0x04,
	TRANSPORT_DEVICE_SHUTDOWN		= 0x08,
	TRANSPORT_DEVICE_OFFLINE_ACTIVATED	= 0x10,
	TRANSPORT_DEVICE_OFFLINE_DEACTIVATED	= 0x20,
};

/*
 * Used by transport_send_check_condition_and_sense() and se_cmd->scsi_sense_reason
 * to signal which ASC/ASCQ sense payload should be built.
 */
enum tcm_sense_reason_table {
	TCM_NON_EXISTENT_LUN			= 0x01,
	TCM_UNSUPPORTED_SCSI_OPCODE		= 0x02,
	TCM_INCORRECT_AMOUNT_OF_DATA		= 0x03,
	TCM_UNEXPECTED_UNSOLICITED_DATA		= 0x04,
	TCM_SERVICE_CRC_ERROR			= 0x05,
	TCM_SNACK_REJECTED			= 0x06,
	TCM_SECTOR_COUNT_TOO_MANY		= 0x07,
	TCM_INVALID_CDB_FIELD			= 0x08,
	TCM_INVALID_PARAMETER_LIST		= 0x09,
	TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE	= 0x0a,
	TCM_UNKNOWN_MODE_PAGE			= 0x0b,
	TCM_WRITE_PROTECTED			= 0x0c,
	TCM_CHECK_CONDITION_ABORT_CMD		= 0x0d,
	TCM_CHECK_CONDITION_UNIT_ATTENTION	= 0x0e,
	TCM_CHECK_CONDITION_NOT_READY		= 0x0f,
};

struct se_obj {
	atomic_t obj_access_count;
} ____cacheline_aligned;

/*
 * Used by TCM Core internally to signal if ALUA emulation is enabled or
 * disabled, or running in with TCM/pSCSI passthrough mode
 */
typedef enum {
	SPC_ALUA_PASSTHROUGH,
	SPC2_ALUA_DISABLED,
	SPC3_ALUA_EMULATED
} t10_alua_index_t;

/*
 * Used by TCM Core internally to signal if SAM Task Attribute emulation
 * is enabled or disabled, or running in with TCM/pSCSI passthrough mode
 */
typedef enum {
	SAM_TASK_ATTR_PASSTHROUGH,
	SAM_TASK_ATTR_UNTAGGED,
	SAM_TASK_ATTR_EMULATED
} t10_task_attr_index_t;

struct se_cmd;

struct t10_alua {
	t10_alua_index_t alua_type;
	/* ALUA Target Port Group ID */
	u16	alua_tg_pt_gps_counter;
	u32	alua_tg_pt_gps_count;
	spinlock_t tg_pt_gps_lock;
	struct se_subsystem_dev *t10_sub_dev;
	/* Used for default ALUA Target Port Group */
	struct t10_alua_tg_pt_gp *default_tg_pt_gp;
	/* Used for default ALUA Target Port Group ConfigFS group */
	struct config_group alua_tg_pt_gps_group;
	int (*alua_state_check)(struct se_cmd *, unsigned char *, u8 *);
	struct list_head tg_pt_gps_list;
} ____cacheline_aligned;

struct t10_alua_lu_gp {
	u16	lu_gp_id;
	int	lu_gp_valid_id;
	u32	lu_gp_members;
	atomic_t lu_gp_shutdown;
	atomic_t lu_gp_ref_cnt;
	spinlock_t lu_gp_lock;
	struct config_group lu_gp_group;
	struct list_head lu_gp_list;
	struct list_head lu_gp_mem_list;
} ____cacheline_aligned;

struct t10_alua_lu_gp_member {
	int lu_gp_assoc:1;
	atomic_t lu_gp_mem_ref_cnt;
	spinlock_t lu_gp_mem_lock;
	struct t10_alua_lu_gp *lu_gp;
	struct se_device *lu_gp_mem_dev;
	struct list_head lu_gp_mem_list;
} ____cacheline_aligned;

struct t10_alua_tg_pt_gp {
	u16	tg_pt_gp_id;
	int	tg_pt_gp_valid_id;
	int	tg_pt_gp_alua_access_status;
	int	tg_pt_gp_alua_access_type;
	int	tg_pt_gp_nonop_delay_msecs;
	int	tg_pt_gp_trans_delay_msecs;
	int	tg_pt_gp_pref;
	int	tg_pt_gp_write_metadata;
	/* Used by struct t10_alua_tg_pt_gp->tg_pt_gp_md_buf_len */
#define ALUA_MD_BUF_LEN				1024
	u32	tg_pt_gp_md_buf_len;
	u32	tg_pt_gp_members;
	atomic_t tg_pt_gp_alua_access_state;
	atomic_t tg_pt_gp_ref_cnt;
	spinlock_t tg_pt_gp_lock;
	struct mutex tg_pt_gp_md_mutex;
	struct se_subsystem_dev *tg_pt_gp_su_dev;
	struct config_group tg_pt_gp_group;
	struct list_head tg_pt_gp_list;
	struct list_head tg_pt_gp_mem_list;
} ____cacheline_aligned;

struct t10_alua_tg_pt_gp_member {
	int tg_pt_gp_assoc:1;
	atomic_t tg_pt_gp_mem_ref_cnt;
	spinlock_t tg_pt_gp_mem_lock;
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	struct se_port *tg_pt;
	struct list_head tg_pt_gp_mem_list;
} ____cacheline_aligned;

struct t10_vpd {
	unsigned char device_identifier[INQUIRY_VPD_DEVICE_IDENTIFIER_LEN];
	int protocol_identifier_set;
	u32 protocol_identifier;
	u32 device_identifier_code_set;
	u32 association;
	u32 device_identifier_type;
	struct list_head vpd_list;
} ____cacheline_aligned;

struct t10_wwn {
	unsigned char vendor[8];
	unsigned char model[16];
	unsigned char revision[4];
	unsigned char unit_serial[INQUIRY_VPD_SERIAL_LEN];
	spinlock_t t10_vpd_lock;
	struct se_subsystem_dev *t10_sub_dev;
	struct config_group t10_wwn_group;
	struct list_head t10_vpd_list;
} ____cacheline_aligned;


/*
 * Used by TCM Core internally to signal if >= SPC-3 peristent reservations
 * emulation is enabled or disabled, or running in with TCM/pSCSI passthrough
 * mode
 */
typedef enum {
	SPC_PASSTHROUGH,
	SPC2_RESERVATIONS,
	SPC3_PERSISTENT_RESERVATIONS
} t10_reservations_index_t;

struct t10_pr_registration {
	/* Used for fabrics that contain WWN+ISID */
#define PR_REG_ISID_LEN				16
	/* PR_REG_ISID_LEN + ',i,0x' */
#define PR_REG_ISID_ID_LEN			(PR_REG_ISID_LEN + 5)
	char pr_reg_isid[PR_REG_ISID_LEN];
	/* Used during APTPL metadata reading */
#define PR_APTPL_MAX_IPORT_LEN			256
	unsigned char pr_iport[PR_APTPL_MAX_IPORT_LEN];
	/* Used during APTPL metadata reading */
#define PR_APTPL_MAX_TPORT_LEN			256
	unsigned char pr_tport[PR_APTPL_MAX_TPORT_LEN];
	/* For writing out live meta data */
	unsigned char *pr_aptpl_buf;
	u16 pr_aptpl_rpti;
	u16 pr_reg_tpgt;
	/* Reservation effects all target ports */
	int pr_reg_all_tg_pt;
	/* Activate Persistence across Target Power Loss */
	int pr_reg_aptpl;
	int pr_res_holder;
	int pr_res_type;
	int pr_res_scope;
	/* Used for fabric initiator WWPNs using a ISID */
	int isid_present_at_reg:1;
	u32 pr_res_mapped_lun;
	u32 pr_aptpl_target_lun;
	u32 pr_res_generation;
	u64 pr_reg_bin_isid;
	u64 pr_res_key;
	atomic_t pr_res_holders;
	struct se_node_acl *pr_reg_nacl;
	struct se_dev_entry *pr_reg_deve;
	struct se_lun *pr_reg_tg_pt_lun;
	struct list_head pr_reg_list;
	struct list_head pr_reg_abort_list;
	struct list_head pr_reg_aptpl_list;
	struct list_head pr_reg_atp_list;
	struct list_head pr_reg_atp_mem_list;
} ____cacheline_aligned;

/*
 * This set of function pointer ops is set based upon SPC3_PERSISTENT_RESERVATIONS,
 * SPC2_RESERVATIONS or SPC_PASSTHROUGH in drivers/target/target_core_pr.c:
 * core_setup_reservations()
 */
struct t10_reservation_ops {
	int (*t10_reservation_check)(struct se_cmd *, u32 *);
	int (*t10_seq_non_holder)(struct se_cmd *, unsigned char *, u32);
	int (*t10_pr_register)(struct se_cmd *);
	int (*t10_pr_clear)(struct se_cmd *);
};

struct t10_reservation_template {
	/* Reservation effects all target ports */
	int pr_all_tg_pt;
	/* Activate Persistence across Target Power Loss enabled
	 * for SCSI device */
	int pr_aptpl_active;
	/* Used by struct t10_reservation_template->pr_aptpl_buf_len */
#define PR_APTPL_BUF_LEN			8192
	u32 pr_aptpl_buf_len;
	u32 pr_generation;
	t10_reservations_index_t res_type;
	spinlock_t registration_lock;
	spinlock_t aptpl_reg_lock;
	/*
	 * This will always be set by one individual I_T Nexus.
	 * However with all_tg_pt=1, other I_T Nexus from the
	 * same initiator can access PR reg/res info on a different
	 * target port.
	 *
	 * There is also the 'All Registrants' case, where there is
	 * a single *pr_res_holder of the reservation, but all
	 * registrations are considered reservation holders.
	 */
	struct se_node_acl *pr_res_holder;
	struct list_head registration_list;
	struct list_head aptpl_reg_list;
	struct t10_reservation_ops pr_ops;
} ____cacheline_aligned;

struct se_queue_req {
	int			state;
	void			*cmd;
	struct list_head	qr_list;
} ____cacheline_aligned;

struct se_queue_obj {
	atomic_t		queue_cnt;
	spinlock_t		cmd_queue_lock;
	struct list_head	qobj_list;
	wait_queue_head_t	thread_wq;
} ____cacheline_aligned;

/*
 * Used one per struct se_cmd to hold all extra struct se_task
 * metadata.  This structure is setup and allocated in
 * drivers/target/target_core_transport.c:__transport_alloc_se_cmd()
 */
struct se_transport_task {
	unsigned char		*t_task_cdb;
	unsigned char		__t_task_cdb[TCM_MAX_COMMAND_SIZE];
	unsigned long long	t_task_lba;
	int			t_tasks_failed;
	int			t_tasks_fua;
	int			t_tasks_bidi:1;
	u32			t_task_cdbs;
	u32			t_tasks_check;
	u32			t_tasks_no;
	u32			t_tasks_sectors;
	u32			t_tasks_se_num;
	u32			t_tasks_se_bidi_num;
	u32			t_tasks_sg_chained_no;
	atomic_t		t_fe_count;
	atomic_t		t_se_count;
	atomic_t		t_task_cdbs_left;
	atomic_t		t_task_cdbs_ex_left;
	atomic_t		t_task_cdbs_timeout_left;
	atomic_t		t_task_cdbs_sent;
	atomic_t		t_transport_aborted;
	atomic_t		t_transport_active;
	atomic_t		t_transport_complete;
	atomic_t		t_transport_queue_active;
	atomic_t		t_transport_sent;
	atomic_t		t_transport_stop;
	atomic_t		t_transport_timeout;
	atomic_t		transport_dev_active;
	atomic_t		transport_lun_active;
	atomic_t		transport_lun_fe_stop;
	atomic_t		transport_lun_stop;
	spinlock_t		t_state_lock;
	struct completion	t_transport_stop_comp;
	struct completion	transport_lun_fe_stop_comp;
	struct completion	transport_lun_stop_comp;
	struct scatterlist	*t_tasks_sg_chained;
	struct scatterlist	t_tasks_sg_bounce;
	void			*t_task_buf;
	/*
	 * Used for pre-registered fabric SGL passthrough WRITE and READ
	 * with the special SCF_PASSTHROUGH_CONTIG_TO_SG case for TCM_Loop
	 * and other HW target mode fabric modules.
	 */
	struct scatterlist	*t_task_pt_sgl;
	struct list_head	*t_mem_list;
	/* Used for BIDI READ */
	struct list_head	*t_mem_bidi_list;
	struct list_head	t_task_list;
} ____cacheline_aligned;

struct se_task {
	unsigned char	task_sense;
	struct scatterlist *task_sg;
	struct scatterlist *task_sg_bidi;
	u8		task_scsi_status;
	u8		task_flags;
	int		task_error_status;
	int		task_state_flags;
	int		task_padded_sg:1;
	unsigned long long	task_lba;
	u32		task_no;
	u32		task_sectors;
	u32		task_size;
	u32		task_sg_num;
	u32		task_sg_offset;
	enum dma_data_direction	task_data_direction;
	struct se_cmd *task_se_cmd;
	struct se_device	*se_dev;
	struct completion	task_stop_comp;
	atomic_t	task_active;
	atomic_t	task_execute_queue;
	atomic_t	task_timeout;
	atomic_t	task_sent;
	atomic_t	task_stop;
	atomic_t	task_state_active;
	struct timer_list	task_timer;
	struct se_device *se_obj_ptr;
	struct list_head t_list;
	struct list_head t_execute_list;
	struct list_head t_state_list;
} ____cacheline_aligned;

#define TASK_CMD(task)	((struct se_cmd *)task->task_se_cmd)
#define TASK_DEV(task)	((struct se_device *)task->se_dev)

struct se_cmd {
	/* SAM response code being sent to initiator */
	u8			scsi_status;
	u8			scsi_asc;
	u8			scsi_ascq;
	u8			scsi_sense_reason;
	u16			scsi_sense_length;
	/* Delay for ALUA Active/NonOptimized state access in milliseconds */
	int			alua_nonop_delay;
	/* See include/linux/dma-mapping.h */
	enum dma_data_direction	data_direction;
	/* For SAM Task Attribute */
	int			sam_task_attr;
	/* Transport protocol dependent state, see transport_state_table */
	enum transport_state_table t_state;
	/* Transport protocol dependent state for out of order CmdSNs */
	int			deferred_t_state;
	/* Transport specific error status */
	int			transport_error_status;
	/* See se_cmd_flags_table */
	u32			se_cmd_flags;
	u32			se_ordered_id;
	/* Total size in bytes associated with command */
	u32			data_length;
	/* SCSI Presented Data Transfer Length */
	u32			cmd_spdtl;
	u32			residual_count;
	u32			orig_fe_lun;
	/* Persistent Reservation key */
	u64			pr_res_key;
	atomic_t                transport_sent;
	/* Used for sense data */
	void			*sense_buffer;
	struct list_head	se_delayed_list;
	struct list_head	se_ordered_list;
	struct list_head	se_lun_list;
	struct se_device      *se_dev;
	struct se_dev_entry   *se_deve;
	struct se_device	*se_obj_ptr;
	struct se_device	*se_orig_obj_ptr;
	struct se_lun		*se_lun;
	/* Only used for internal passthrough and legacy TCM fabric modules */
	struct se_session	*se_sess;
	struct se_tmr_req	*se_tmr_req;
	/* t_task is setup to t_task_backstore in transport_init_se_cmd() */
	struct se_transport_task *t_task;
	struct se_transport_task t_task_backstore;
	struct target_core_fabric_ops *se_tfo;
	int (*transport_emulate_cdb)(struct se_cmd *);
	void (*transport_split_cdb)(unsigned long long, u32 *, unsigned char *);
	void (*transport_wait_for_tasks)(struct se_cmd *, int, int);
	void (*transport_complete_callback)(struct se_cmd *);
} ____cacheline_aligned;

#define T_TASK(cmd)     ((struct se_transport_task *)(cmd->t_task))
#define CMD_TFO(cmd) ((struct target_core_fabric_ops *)cmd->se_tfo)

struct se_tmr_req {
	/* Task Management function to be preformed */
	u8			function;
	/* Task Management response to send */
	u8			response;
	int			call_transport;
	/* Reference to ITT that Task Mgmt should be preformed */
	u32			ref_task_tag;
	/* 64-bit encoded SAM LUN from $FABRIC_MOD TMR header */
	u64			ref_task_lun;
	void 			*fabric_tmr_ptr;
	struct se_cmd		*task_cmd;
	struct se_cmd		*ref_cmd;
	struct se_device	*tmr_dev;
	struct se_lun		*tmr_lun;
	struct list_head	tmr_list;
} ____cacheline_aligned;

struct se_ua {
	u8			ua_asc;
	u8			ua_ascq;
	struct se_node_acl	*ua_nacl;
	struct list_head	ua_dev_list;
	struct list_head	ua_nacl_list;
} ____cacheline_aligned;

struct se_node_acl {
	char			initiatorname[TRANSPORT_IQN_LEN];
	/* Used to signal demo mode created ACL, disabled by default */
	int			dynamic_node_acl:1;
	u32			queue_depth;
	u32			acl_index;
	u64			num_cmds;
	u64			read_bytes;
	u64			write_bytes;
	spinlock_t		stats_lock;
	/* Used for PR SPEC_I_PT=1 and REGISTER_AND_MOVE */
	atomic_t		acl_pr_ref_count;
	/* Used for MIB access */
	atomic_t		mib_ref_count;
	struct se_dev_entry	*device_list;
	struct se_session	*nacl_sess;
	struct se_portal_group *se_tpg;
	spinlock_t		device_list_lock;
	spinlock_t		nacl_sess_lock;
	struct config_group	acl_group;
	struct config_group	acl_attrib_group;
	struct config_group	acl_auth_group;
	struct config_group	acl_param_group;
	struct config_group	*acl_default_groups[4];
	struct list_head	acl_list;
	struct list_head	acl_sess_list;
} ____cacheline_aligned;

struct se_session {
	/* Used for MIB access */
	atomic_t		mib_ref_count;
	u64			sess_bin_isid;
	struct se_node_acl	*se_node_acl;
	struct se_portal_group *se_tpg;
	void			*fabric_sess_ptr;
	struct list_head	sess_list;
	struct list_head	sess_acl_list;
} ____cacheline_aligned;

#define SE_SESS(cmd)		((struct se_session *)(cmd)->se_sess)
#define SE_NODE_ACL(sess)	((struct se_node_acl *)(sess)->se_node_acl)

struct se_device;
struct se_transform_info;
struct scatterlist;

struct se_lun_acl {
	char			initiatorname[TRANSPORT_IQN_LEN];
	u32			mapped_lun;
	struct se_node_acl	*se_lun_nacl;
	struct se_lun		*se_lun;
	struct list_head	lacl_list;
	struct config_group	se_lun_group;
}  ____cacheline_aligned;

struct se_dev_entry {
	int			def_pr_registered:1;
	/* See transport_lunflags_table */
	u32			lun_flags;
	u32			deve_cmds;
	u32			mapped_lun;
	u32			average_bytes;
	u32			last_byte_count;
	u32			total_cmds;
	u32			total_bytes;
	u64			pr_res_key;
	u64			creation_time;
	u32			attach_count;
	u64			read_bytes;
	u64			write_bytes;
	atomic_t		ua_count;
	/* Used for PR SPEC_I_PT=1 and REGISTER_AND_MOVE */
	atomic_t		pr_ref_count;
	struct se_lun_acl	*se_lun_acl;
	spinlock_t		ua_lock;
	struct se_lun		*se_lun;
	struct list_head	alua_port_list;
	struct list_head	ua_list;
}  ____cacheline_aligned;

struct se_dev_limits {
	/* Max supported HW queue depth */
	u32		hw_queue_depth;
	/* Max supported virtual queue depth */
	u32		queue_depth;
	/* From include/linux/blkdev.h for the other HW/SW limits. */
	struct queue_limits limits;
} ____cacheline_aligned;

struct se_dev_attrib {
	int		emulate_dpo;
	int		emulate_fua_write;
	int		emulate_fua_read;
	int		emulate_write_cache;
	int		emulate_ua_intlck_ctrl;
	int		emulate_tas;
	int		emulate_tpu;
	int		emulate_tpws;
	int		emulate_reservations;
	int		emulate_alua;
	int		enforce_pr_isids;
	u32		hw_block_size;
	u32		block_size;
	u32		hw_max_sectors;
	u32		max_sectors;
	u32		optimal_sectors;
	u32		hw_queue_depth;
	u32		queue_depth;
	u32		task_timeout;
	u32		max_unmap_lba_count;
	u32		max_unmap_block_desc_count;
	u32		unmap_granularity;
	u32		unmap_granularity_alignment;
	struct se_subsystem_dev *da_sub_dev;
	struct config_group da_group;
} ____cacheline_aligned;

struct se_subsystem_dev {
/* Used for struct se_subsystem_dev-->se_dev_alias, must be less than PAGE_SIZE */
#define SE_DEV_ALIAS_LEN		512
	unsigned char	se_dev_alias[SE_DEV_ALIAS_LEN];
/* Used for struct se_subsystem_dev->se_dev_udev_path[], must be less than PAGE_SIZE */
#define SE_UDEV_PATH_LEN		512
	unsigned char	se_dev_udev_path[SE_UDEV_PATH_LEN];
	u32		su_dev_flags;
	struct se_hba *se_dev_hba;
	struct se_device *se_dev_ptr;
	struct se_dev_attrib se_dev_attrib;
	/* T10 Asymmetric Logical Unit Assignment for Target Ports */
	struct t10_alua	t10_alua;
	/* T10 Inquiry and VPD WWN Information */
	struct t10_wwn	t10_wwn;
	/* T10 SPC-2 + SPC-3 Reservations */
	struct t10_reservation_template t10_reservation;
	spinlock_t      se_dev_lock;
	void            *se_dev_su_ptr;
	struct list_head g_se_dev_list;
	struct config_group se_dev_group;
	/* For T10 Reservations */
	struct config_group se_dev_pr_group;
} ____cacheline_aligned;

#define T10_ALUA(su_dev)	(&(su_dev)->t10_alua)
#define T10_RES(su_dev)		(&(su_dev)->t10_reservation)
#define T10_PR_OPS(su_dev)	(&(su_dev)->t10_reservation.pr_ops)

struct se_device {
	/* Set to 1 if thread is NOT sleeping on thread_sem */
	u8			thread_active;
	u8			dev_status_timer_flags;
	/* RELATIVE TARGET PORT IDENTIFER Counter */
	u16			dev_rpti_counter;
	/* Used for SAM Task Attribute ordering */
	u32			dev_cur_ordered_id;
	u32			dev_flags;
	u32			dev_port_count;
	/* See transport_device_status_table */
	u32			dev_status;
	u32			dev_tcq_window_closed;
	/* Physical device queue depth */
	u32			queue_depth;
	/* Used for SPC-2 reservations enforce of ISIDs */
	u64			dev_res_bin_isid;
	t10_task_attr_index_t	dev_task_attr_type;
	/* Pointer to transport specific device structure */
	void 			*dev_ptr;
	u32			dev_index;
	u64			creation_time;
	u32			num_resets;
	u64			num_cmds;
	u64			read_bytes;
	u64			write_bytes;
	spinlock_t		stats_lock;
	/* Active commands on this virtual SE device */
	atomic_t		active_cmds;
	atomic_t		simple_cmds;
	atomic_t		depth_left;
	atomic_t		dev_ordered_id;
	atomic_t		dev_tur_active;
	atomic_t		execute_tasks;
	atomic_t		dev_status_thr_count;
	atomic_t		dev_hoq_count;
	atomic_t		dev_ordered_sync;
	struct se_obj		dev_obj;
	struct se_obj		dev_access_obj;
	struct se_obj		dev_export_obj;
	struct se_queue_obj	*dev_queue_obj;
	struct se_queue_obj	*dev_status_queue_obj;
	spinlock_t		delayed_cmd_lock;
	spinlock_t		ordered_cmd_lock;
	spinlock_t		execute_task_lock;
	spinlock_t		state_task_lock;
	spinlock_t		dev_alua_lock;
	spinlock_t		dev_reservation_lock;
	spinlock_t		dev_state_lock;
	spinlock_t		dev_status_lock;
	spinlock_t		dev_status_thr_lock;
	spinlock_t		se_port_lock;
	spinlock_t		se_tmr_lock;
	/* Used for legacy SPC-2 reservationsa */
	struct se_node_acl	*dev_reserved_node_acl;
	/* Used for ALUA Logical Unit Group membership */
	struct t10_alua_lu_gp_member *dev_alua_lu_gp_mem;
	/* Used for SPC-3 Persistent Reservations */
	struct t10_pr_registration *dev_pr_res_holder;
	struct list_head	dev_sep_list;
	struct list_head	dev_tmr_list;
	struct timer_list	dev_status_timer;
	/* Pointer to descriptor for processing thread */
	struct task_struct	*process_thread;
	pid_t			process_thread_pid;
	struct task_struct		*dev_mgmt_thread;
	struct list_head	delayed_cmd_list;
	struct list_head	ordered_cmd_list;
	struct list_head	execute_task_list;
	struct list_head	state_task_list;
	/* Pointer to associated SE HBA */
	struct se_hba		*se_hba;
	struct se_subsystem_dev *se_sub_dev;
	/* Pointer to template of function pointers for transport */
	struct se_subsystem_api *transport;
	/* Linked list for struct se_hba struct se_device list */
	struct list_head	dev_list;
	/* Linked list for struct se_global->g_se_dev_list */
	struct list_head	g_se_dev_list;
}  ____cacheline_aligned;

#define SE_DEV(cmd)		((struct se_device *)(cmd)->se_lun->lun_se_dev)
#define SU_DEV(dev)		((struct se_subsystem_dev *)(dev)->se_sub_dev)
#define DEV_ATTRIB(dev)		(&(dev)->se_sub_dev->se_dev_attrib)
#define DEV_T10_WWN(dev)	(&(dev)->se_sub_dev->t10_wwn)

struct se_hba {
	u16			hba_tpgt;
	u32			hba_id;
	/* See hba_flags_table */
	u32			hba_flags;
	/* Virtual iSCSI devices attached. */
	u32			dev_count;
	u32			hba_index;
	atomic_t		dev_mib_access_count;
	atomic_t		load_balance_queue;
	atomic_t		left_queue_depth;
	/* Maximum queue depth the HBA can handle. */
	atomic_t		max_queue_depth;
	/* Pointer to transport specific host structure. */
	void			*hba_ptr;
	/* Linked list for struct se_device */
	struct list_head	hba_dev_list;
	struct list_head	hba_list;
	spinlock_t		device_lock;
	spinlock_t		hba_queue_lock;
	struct config_group	hba_group;
	struct mutex		hba_access_mutex;
	struct se_subsystem_api *transport;
}  ____cacheline_aligned;

#define SE_HBA(d)		((struct se_hba *)(d)->se_hba)

struct se_lun {
	/* See transport_lun_status_table */
	enum transport_lun_status_table lun_status;
	u32			lun_access;
	u32			lun_flags;
	u32			unpacked_lun;
	atomic_t		lun_acl_count;
	spinlock_t		lun_acl_lock;
	spinlock_t		lun_cmd_lock;
	spinlock_t		lun_sep_lock;
	struct completion	lun_shutdown_comp;
	struct list_head	lun_cmd_list;
	struct list_head	lun_acl_list;
	struct se_device	*lun_se_dev;
	struct config_group	lun_group;
	struct se_port	*lun_sep;
} ____cacheline_aligned;

#define SE_LUN(c)		((struct se_lun *)(c)->se_lun)

struct se_port {
	/* RELATIVE TARGET PORT IDENTIFER */
	u16		sep_rtpi;
	int		sep_tg_pt_secondary_stat;
	int		sep_tg_pt_secondary_write_md;
	u32		sep_index;
	struct scsi_port_stats sep_stats;
	/* Used for ALUA Target Port Groups membership */
	atomic_t	sep_tg_pt_gp_active;
	atomic_t	sep_tg_pt_secondary_offline;
	/* Used for PR ALL_TG_PT=1 */
	atomic_t	sep_tg_pt_ref_cnt;
	spinlock_t	sep_alua_lock;
	struct mutex	sep_tg_pt_md_mutex;
	struct t10_alua_tg_pt_gp_member *sep_alua_tg_pt_gp_mem;
	struct se_lun *sep_lun;
	struct se_portal_group *sep_tpg;
	struct list_head sep_alua_list;
	struct list_head sep_list;
} ____cacheline_aligned;

struct se_tpg_np {
	struct config_group	tpg_np_group;
} ____cacheline_aligned;

struct se_portal_group {
	/* Type of target portal group, see transport_tpg_type_table */
	enum transport_tpg_type_table se_tpg_type;
	/* Number of ACLed Initiator Nodes for this TPG */
	u32			num_node_acls;
	/* Used for PR SPEC_I_PT=1 and REGISTER_AND_MOVE */
	atomic_t		tpg_pr_ref_count;
	/* Spinlock for adding/removing ACLed Nodes */
	spinlock_t		acl_node_lock;
	/* Spinlock for adding/removing sessions */
	spinlock_t		session_lock;
	spinlock_t		tpg_lun_lock;
	/* Pointer to $FABRIC_MOD portal group */
	void			*se_tpg_fabric_ptr;
	struct list_head	se_tpg_list;
	/* linked list for initiator ACL list */
	struct list_head	acl_node_list;
	struct se_lun		*tpg_lun_list;
	struct se_lun		tpg_virt_lun0;
	/* List of TCM sessions assoicated wth this TPG */
	struct list_head	tpg_sess_list;
	/* Pointer to $FABRIC_MOD dependent code */
	struct target_core_fabric_ops *se_tpg_tfo;
	struct se_wwn		*se_tpg_wwn;
	struct config_group	tpg_group;
	struct config_group	*tpg_default_groups[6];
	struct config_group	tpg_lun_group;
	struct config_group	tpg_np_group;
	struct config_group	tpg_acl_group;
	struct config_group	tpg_attrib_group;
	struct config_group	tpg_param_group;
} ____cacheline_aligned;

#define TPG_TFO(se_tpg)	((struct target_core_fabric_ops *)(se_tpg)->se_tpg_tfo)

struct se_wwn {
	struct target_fabric_configfs *wwn_tf;
	struct config_group	wwn_group;
} ____cacheline_aligned;

struct se_global {
	u16			alua_lu_gps_counter;
	int			g_sub_api_initialized;
	u32			in_shutdown;
	u32			alua_lu_gps_count;
	u32			g_hba_id_counter;
	struct config_group	target_core_hbagroup;
	struct config_group	alua_group;
	struct config_group	alua_lu_gps_group;
	struct list_head	g_lu_gps_list;
	struct list_head	g_se_tpg_list;
	struct list_head	g_hba_list;
	struct list_head	g_se_dev_list;
	struct se_hba		*g_lun0_hba;
	struct se_subsystem_dev *g_lun0_su_dev;
	struct se_device	*g_lun0_dev;
	struct t10_alua_lu_gp	*default_lu_gp;
	spinlock_t		g_device_lock;
	spinlock_t		hba_lock;
	spinlock_t		se_tpg_lock;
	spinlock_t		lu_gps_lock;
	spinlock_t		plugin_class_lock;
} ____cacheline_aligned;

#endif /* TARGET_CORE_BASE_H */
