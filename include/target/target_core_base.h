#ifndef TARGET_CORE_BASE_H
#define TARGET_CORE_BASE_H

#include <linux/in.h>
#include <linux/configfs.h>
#include <linux/dma-mapping.h>
#include <linux/blkdev.h>
#include <linux/percpu_ida.h>
#include <linux/t10-pi.h>
#include <net/sock.h>
#include <net/tcp.h>

#define TARGET_CORE_VERSION		"v5.0"

/*
 * Maximum size of a CDB that can be stored in se_cmd without allocating
 * memory dynamically for the CDB.
 */
#define TCM_MAX_COMMAND_SIZE			32
/*
 * From include/scsi/scsi_cmnd.h:SCSI_SENSE_BUFFERSIZE, currently
 * defined 96, but the real limit is 252 (or 260 including the header)
 */
#define TRANSPORT_SENSE_BUFFER			96
/* Used by transport_send_check_condition_and_sense() */
#define SPC_SENSE_KEY_OFFSET			2
#define SPC_ADD_SENSE_LEN_OFFSET		7
#define SPC_DESC_TYPE_OFFSET			8
#define SPC_ADDITIONAL_DESC_LEN_OFFSET		9
#define SPC_VALIDITY_OFFSET			10
#define SPC_ASC_KEY_OFFSET			12
#define SPC_ASCQ_KEY_OFFSET			13
#define TRANSPORT_IQN_LEN			224
/* Used by target_core_store_alua_lu_gp() and target_core_alua_lu_gp_show_attr_members() */
#define LU_GROUP_NAME_BUF			256
/* Used by core_alua_store_tg_pt_gp_info() and target_core_alua_tg_pt_gp_show_attr_members() */
#define TG_PT_GROUP_NAME_BUF			256
/* Used to parse VPD into struct t10_vpd */
#define VPD_TMP_BUF_SIZE			254
/* Used by transport_generic_cmd_sequencer() */
#define READ_BLOCK_LEN          		6
#define READ_CAP_LEN            		8
#define READ_POSITION_LEN       		20
#define INQUIRY_LEN				36
/* Used by transport_get_inquiry_vpd_serial() */
#define INQUIRY_VPD_SERIAL_LEN			254
/* Used by transport_get_inquiry_vpd_device_ident() */
#define INQUIRY_VPD_DEVICE_IDENTIFIER_LEN	254

/* Attempts before moving from SHORT to LONG */
#define PYX_TRANSPORT_WINDOW_CLOSED_THRESHOLD	3
#define PYX_TRANSPORT_WINDOW_CLOSED_WAIT_SHORT	3  /* In milliseconds */
#define PYX_TRANSPORT_WINDOW_CLOSED_WAIT_LONG	10 /* In milliseconds */

#define PYX_TRANSPORT_STATUS_INTERVAL		5 /* In seconds */

/* struct se_dev_attrib sanity values */
/* Default max_unmap_lba_count */
#define DA_MAX_UNMAP_LBA_COUNT			0
/* Default max_unmap_block_desc_count */
#define DA_MAX_UNMAP_BLOCK_DESC_COUNT		0
/* Default unmap_granularity */
#define DA_UNMAP_GRANULARITY_DEFAULT		0
/* Default unmap_granularity_alignment */
#define DA_UNMAP_GRANULARITY_ALIGNMENT_DEFAULT	0
/* Default max_write_same_len, disabled by default */
#define DA_MAX_WRITE_SAME_LEN			0
/* Use a model alias based on the configfs backend device name */
#define DA_EMULATE_MODEL_ALIAS			0
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
/* Emulation for CompareAndWrite (AtomicTestandSet) by default */
#define DA_EMULATE_CAW				1
/* Emulation for 3rd Party Copy (ExtendedCopy) by default */
#define DA_EMULATE_3PC				1
/* No Emulation for PSCSI by default */
#define DA_EMULATE_ALUA				0
/* Enforce SCSI Initiator Port TransportID with 'ISID' for PR */
#define DA_ENFORCE_PR_ISIDS			1
/* Force SPC-3 PR Activate Persistence across Target Power Loss */
#define DA_FORCE_PR_APTPL			0
#define DA_STATUS_MAX_SECTORS_MIN		16
#define DA_STATUS_MAX_SECTORS_MAX		8192
/* By default don't report non-rotating (solid state) medium */
#define DA_IS_NONROT				0
/* Queue Algorithm Modifier default for restricted reordering in control mode page */
#define DA_EMULATE_REST_REORD			0

#define SE_INQUIRY_BUF				1024
#define SE_MODE_PAGE_BUF			512
#define SE_SENSE_BUF				96

/* struct se_hba->hba_flags */
enum hba_flags_table {
	HBA_FLAGS_INTERNAL_USE	= 0x01,
	HBA_FLAGS_PSCSI_MODE	= 0x02,
};

/* Special transport agnostic struct se_cmd->t_states */
enum transport_state_table {
	TRANSPORT_NO_STATE	= 0,
	TRANSPORT_NEW_CMD	= 1,
	TRANSPORT_WRITE_PENDING	= 3,
	TRANSPORT_PROCESSING	= 5,
	TRANSPORT_COMPLETE	= 6,
	TRANSPORT_ISTATE_PROCESSING = 11,
	TRANSPORT_COMPLETE_QF_WP = 18,
	TRANSPORT_COMPLETE_QF_OK = 19,
};

/* Used for struct se_cmd->se_cmd_flags */
enum se_cmd_flags_table {
	SCF_SUPPORTED_SAM_OPCODE	= 0x00000001,
	SCF_TRANSPORT_TASK_SENSE	= 0x00000002,
	SCF_EMULATED_TASK_SENSE		= 0x00000004,
	SCF_SCSI_DATA_CDB		= 0x00000008,
	SCF_SCSI_TMR_CDB		= 0x00000010,
	SCF_FUA				= 0x00000080,
	SCF_SE_LUN_CMD			= 0x00000100,
	SCF_BIDI			= 0x00000400,
	SCF_SENT_CHECK_CONDITION	= 0x00000800,
	SCF_OVERFLOW_BIT		= 0x00001000,
	SCF_UNDERFLOW_BIT		= 0x00002000,
	SCF_SEND_DELAYED_TAS		= 0x00004000,
	SCF_ALUA_NON_OPTIMIZED		= 0x00008000,
	SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC = 0x00020000,
	SCF_COMPARE_AND_WRITE		= 0x00080000,
	SCF_COMPARE_AND_WRITE_POST	= 0x00100000,
	SCF_PASSTHROUGH_PROT_SG_TO_MEM_NOALLOC = 0x00200000,
};

/* struct se_dev_entry->lun_flags and struct se_lun->lun_access */
enum transport_lunflags_table {
	TRANSPORT_LUNFLAGS_READ_ONLY		= 0x01,
	TRANSPORT_LUNFLAGS_READ_WRITE		= 0x02,
};

/*
 * Used by transport_send_check_condition_and_sense()
 * to signal which ASC/ASCQ sense payload should be built.
 */
typedef unsigned __bitwise__ sense_reason_t;

enum tcm_sense_reason_table {
#define R(x)	(__force sense_reason_t )(x)
	TCM_NO_SENSE				= R(0x00),
	TCM_NON_EXISTENT_LUN			= R(0x01),
	TCM_UNSUPPORTED_SCSI_OPCODE		= R(0x02),
	TCM_INCORRECT_AMOUNT_OF_DATA		= R(0x03),
	TCM_UNEXPECTED_UNSOLICITED_DATA		= R(0x04),
	TCM_SERVICE_CRC_ERROR			= R(0x05),
	TCM_SNACK_REJECTED			= R(0x06),
	TCM_SECTOR_COUNT_TOO_MANY		= R(0x07),
	TCM_INVALID_CDB_FIELD			= R(0x08),
	TCM_INVALID_PARAMETER_LIST		= R(0x09),
	TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE	= R(0x0a),
	TCM_UNKNOWN_MODE_PAGE			= R(0x0b),
	TCM_WRITE_PROTECTED			= R(0x0c),
	TCM_CHECK_CONDITION_ABORT_CMD		= R(0x0d),
	TCM_CHECK_CONDITION_UNIT_ATTENTION	= R(0x0e),
	TCM_CHECK_CONDITION_NOT_READY		= R(0x0f),
	TCM_RESERVATION_CONFLICT		= R(0x10),
	TCM_ADDRESS_OUT_OF_RANGE		= R(0x11),
	TCM_OUT_OF_RESOURCES			= R(0x12),
	TCM_PARAMETER_LIST_LENGTH_ERROR		= R(0x13),
	TCM_MISCOMPARE_VERIFY			= R(0x14),
	TCM_LOGICAL_BLOCK_GUARD_CHECK_FAILED	= R(0x15),
	TCM_LOGICAL_BLOCK_APP_TAG_CHECK_FAILED	= R(0x16),
	TCM_LOGICAL_BLOCK_REF_TAG_CHECK_FAILED	= R(0x17),
#undef R
};

enum target_sc_flags_table {
	TARGET_SCF_BIDI_OP		= 0x01,
	TARGET_SCF_ACK_KREF		= 0x02,
	TARGET_SCF_UNKNOWN_SIZE		= 0x04,
};

/* fabric independent task management function values */
enum tcm_tmreq_table {
	TMR_ABORT_TASK		= 1,
	TMR_ABORT_TASK_SET	= 2,
	TMR_CLEAR_ACA		= 3,
	TMR_CLEAR_TASK_SET	= 4,
	TMR_LUN_RESET		= 5,
	TMR_TARGET_WARM_RESET	= 6,
	TMR_TARGET_COLD_RESET	= 7,
};

/* fabric independent task management response values */
enum tcm_tmrsp_table {
	TMR_FUNCTION_FAILED		= 0,
	TMR_FUNCTION_COMPLETE		= 1,
	TMR_TASK_DOES_NOT_EXIST		= 2,
	TMR_LUN_DOES_NOT_EXIST		= 3,
	TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED	= 4,
	TMR_FUNCTION_REJECTED		= 5,
};

/*
 * Used for target SCSI statistics
 */
typedef enum {
	SCSI_INST_INDEX,
	SCSI_DEVICE_INDEX,
	SCSI_AUTH_INTR_INDEX,
	SCSI_INDEX_TYPE_MAX
} scsi_index_t;

struct se_cmd;

struct t10_alua_lba_map_member {
	struct list_head lba_map_mem_list;
	int lba_map_mem_alua_state;
	int lba_map_mem_alua_pg_id;
};

struct t10_alua_lba_map {
	u64 lba_map_first_lba;
	u64 lba_map_last_lba;
	struct list_head lba_map_list;
	struct list_head lba_map_mem_list;
};

struct t10_alua {
	/* ALUA Target Port Group ID */
	u16	alua_tg_pt_gps_counter;
	u32	alua_tg_pt_gps_count;
	/* Referrals support */
	spinlock_t lba_map_lock;
	u32     lba_map_segment_size;
	u32     lba_map_segment_multiplier;
	struct list_head lba_map_list;
	spinlock_t tg_pt_gps_lock;
	struct se_device *t10_dev;
	/* Used for default ALUA Target Port Group */
	struct t10_alua_tg_pt_gp *default_tg_pt_gp;
	/* Used for default ALUA Target Port Group ConfigFS group */
	struct config_group alua_tg_pt_gps_group;
	struct list_head tg_pt_gps_list;
};

struct t10_alua_lu_gp {
	u16	lu_gp_id;
	int	lu_gp_valid_id;
	u32	lu_gp_members;
	atomic_t lu_gp_ref_cnt;
	spinlock_t lu_gp_lock;
	struct config_group lu_gp_group;
	struct list_head lu_gp_node;
	struct list_head lu_gp_mem_list;
};

struct t10_alua_lu_gp_member {
	bool lu_gp_assoc;
	atomic_t lu_gp_mem_ref_cnt;
	spinlock_t lu_gp_mem_lock;
	struct t10_alua_lu_gp *lu_gp;
	struct se_device *lu_gp_mem_dev;
	struct list_head lu_gp_mem_list;
};

struct t10_alua_tg_pt_gp {
	u16	tg_pt_gp_id;
	int	tg_pt_gp_valid_id;
	int	tg_pt_gp_alua_supported_states;
	int	tg_pt_gp_alua_pending_state;
	int	tg_pt_gp_alua_previous_state;
	int	tg_pt_gp_alua_access_status;
	int	tg_pt_gp_alua_access_type;
	int	tg_pt_gp_nonop_delay_msecs;
	int	tg_pt_gp_trans_delay_msecs;
	int	tg_pt_gp_implicit_trans_secs;
	int	tg_pt_gp_pref;
	int	tg_pt_gp_write_metadata;
	u32	tg_pt_gp_members;
	atomic_t tg_pt_gp_alua_access_state;
	atomic_t tg_pt_gp_ref_cnt;
	spinlock_t tg_pt_gp_lock;
	struct mutex tg_pt_gp_md_mutex;
	struct se_device *tg_pt_gp_dev;
	struct config_group tg_pt_gp_group;
	struct list_head tg_pt_gp_list;
	struct list_head tg_pt_gp_lun_list;
	struct se_lun *tg_pt_gp_alua_lun;
	struct se_node_acl *tg_pt_gp_alua_nacl;
	struct delayed_work tg_pt_gp_transition_work;
	struct completion *tg_pt_gp_transition_complete;
};

struct t10_vpd {
	unsigned char device_identifier[INQUIRY_VPD_DEVICE_IDENTIFIER_LEN];
	int protocol_identifier_set;
	u32 protocol_identifier;
	u32 device_identifier_code_set;
	u32 association;
	u32 device_identifier_type;
	struct list_head vpd_list;
};

struct t10_wwn {
	char vendor[8];
	char model[16];
	char revision[4];
	char unit_serial[INQUIRY_VPD_SERIAL_LEN];
	spinlock_t t10_vpd_lock;
	struct se_device *t10_dev;
	struct config_group t10_wwn_group;
	struct list_head t10_vpd_list;
};

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
	bool isid_present_at_reg;
	u64 pr_res_mapped_lun;
	u64 pr_aptpl_target_lun;
	u16 tg_pt_sep_rtpi;
	u32 pr_res_generation;
	u64 pr_reg_bin_isid;
	u64 pr_res_key;
	atomic_t pr_res_holders;
	struct se_node_acl *pr_reg_nacl;
	/* Used by ALL_TG_PT=1 registration with deve->pr_ref taken */
	struct se_dev_entry *pr_reg_deve;
	struct list_head pr_reg_list;
	struct list_head pr_reg_abort_list;
	struct list_head pr_reg_aptpl_list;
	struct list_head pr_reg_atp_list;
	struct list_head pr_reg_atp_mem_list;
};

struct t10_reservation {
	/* Reservation effects all target ports */
	int pr_all_tg_pt;
	/* Activate Persistence across Target Power Loss enabled
	 * for SCSI device */
	int pr_aptpl_active;
#define PR_APTPL_BUF_LEN			262144
	u32 pr_generation;
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
};

struct se_tmr_req {
	/* Task Management function to be performed */
	u8			function;
	/* Task Management response to send */
	u8			response;
	int			call_transport;
	/* Reference to ITT that Task Mgmt should be performed */
	u64			ref_task_tag;
	void 			*fabric_tmr_ptr;
	struct se_cmd		*task_cmd;
	struct se_device	*tmr_dev;
	struct se_lun		*tmr_lun;
	struct list_head	tmr_list;
};

enum target_prot_op {
	TARGET_PROT_NORMAL	= 0,
	TARGET_PROT_DIN_INSERT	= (1 << 0),
	TARGET_PROT_DOUT_INSERT	= (1 << 1),
	TARGET_PROT_DIN_STRIP	= (1 << 2),
	TARGET_PROT_DOUT_STRIP	= (1 << 3),
	TARGET_PROT_DIN_PASS	= (1 << 4),
	TARGET_PROT_DOUT_PASS	= (1 << 5),
};

#define TARGET_PROT_ALL	TARGET_PROT_DIN_INSERT | TARGET_PROT_DOUT_INSERT | \
			TARGET_PROT_DIN_STRIP | TARGET_PROT_DOUT_STRIP | \
			TARGET_PROT_DIN_PASS | TARGET_PROT_DOUT_PASS

enum target_prot_type {
	TARGET_DIF_TYPE0_PROT,
	TARGET_DIF_TYPE1_PROT,
	TARGET_DIF_TYPE2_PROT,
	TARGET_DIF_TYPE3_PROT,
};

enum target_core_dif_check {
	TARGET_DIF_CHECK_GUARD  = 0x1 << 0,
	TARGET_DIF_CHECK_APPTAG = 0x1 << 1,
	TARGET_DIF_CHECK_REFTAG = 0x1 << 2,
};

/* for sam_task_attr */
#define TCM_SIMPLE_TAG	0x20
#define TCM_HEAD_TAG	0x21
#define TCM_ORDERED_TAG	0x22
#define TCM_ACA_TAG	0x24

struct se_cmd {
	/* SAM response code being sent to initiator */
	u8			scsi_status;
	u8			scsi_asc;
	u8			scsi_ascq;
	u16			scsi_sense_length;
	unsigned		cmd_wait_set:1;
	unsigned		unknown_data_length:1;
	bool			state_active:1;
	u64			tag; /* SAM command identifier aka task tag */
	/* Delay for ALUA Active/NonOptimized state access in milliseconds */
	int			alua_nonop_delay;
	/* See include/linux/dma-mapping.h */
	enum dma_data_direction	data_direction;
	/* For SAM Task Attribute */
	int			sam_task_attr;
	/* Used for se_sess->sess_tag_pool */
	unsigned int		map_tag;
	/* Transport protocol dependent state, see transport_state_table */
	enum transport_state_table t_state;
	/* See se_cmd_flags_table */
	u32			se_cmd_flags;
	/* Total size in bytes associated with command */
	u32			data_length;
	u32			residual_count;
	u64			orig_fe_lun;
	/* Persistent Reservation key */
	u64			pr_res_key;
	/* Used for sense data */
	void			*sense_buffer;
	struct list_head	se_delayed_node;
	struct list_head	se_qf_node;
	struct se_device      *se_dev;
	struct se_lun		*se_lun;
	/* Only used for internal passthrough and legacy TCM fabric modules */
	struct se_session	*se_sess;
	struct se_tmr_req	*se_tmr_req;
	struct list_head	se_cmd_list;
	struct completion	cmd_wait_comp;
	const struct target_core_fabric_ops *se_tfo;
	sense_reason_t		(*execute_cmd)(struct se_cmd *);
	sense_reason_t (*transport_complete_callback)(struct se_cmd *, bool);
	void			*protocol_data;

	unsigned char		*t_task_cdb;
	unsigned char		__t_task_cdb[TCM_MAX_COMMAND_SIZE];
	unsigned long long	t_task_lba;
	unsigned int		t_task_nolb;
	unsigned int		transport_state;
#define CMD_T_ABORTED		(1 << 0)
#define CMD_T_ACTIVE		(1 << 1)
#define CMD_T_COMPLETE		(1 << 2)
#define CMD_T_SENT		(1 << 4)
#define CMD_T_STOP		(1 << 5)
#define CMD_T_DEV_ACTIVE	(1 << 7)
#define CMD_T_REQUEST_STOP	(1 << 8)
#define CMD_T_BUSY		(1 << 9)
	spinlock_t		t_state_lock;
	struct kref		cmd_kref;
	struct completion	t_transport_stop_comp;

	struct work_struct	work;

	struct scatterlist	*t_data_sg;
	struct scatterlist	*t_data_sg_orig;
	unsigned int		t_data_nents;
	unsigned int		t_data_nents_orig;
	void			*t_data_vmap;
	struct scatterlist	*t_bidi_data_sg;
	unsigned int		t_bidi_data_nents;

	/* Used for lun->lun_ref counting */
	int			lun_ref_active;

	struct list_head	state_list;

	/* old task stop completion, consider merging with some of the above */
	struct completion	task_stop_comp;

	/* backend private data */
	void			*priv;

	/* DIF related members */
	enum target_prot_op	prot_op;
	enum target_prot_type	prot_type;
	u8			prot_checks;
	bool			prot_pto;
	u32			prot_length;
	u32			reftag_seed;
	struct scatterlist	*t_prot_sg;
	unsigned int		t_prot_nents;
	sense_reason_t		pi_err;
	sector_t		bad_sector;
};

struct se_ua {
	u8			ua_asc;
	u8			ua_ascq;
	struct list_head	ua_nacl_list;
};

struct se_node_acl {
	char			initiatorname[TRANSPORT_IQN_LEN];
	/* Used to signal demo mode created ACL, disabled by default */
	bool			dynamic_node_acl;
	bool			acl_stop:1;
	u32			queue_depth;
	u32			acl_index;
	enum target_prot_type	saved_prot_type;
#define MAX_ACL_TAG_SIZE 64
	char			acl_tag[MAX_ACL_TAG_SIZE];
	/* Used for PR SPEC_I_PT=1 and REGISTER_AND_MOVE */
	atomic_t		acl_pr_ref_count;
	struct hlist_head	lun_entry_hlist;
	struct se_session	*nacl_sess;
	struct se_portal_group *se_tpg;
	struct mutex		lun_entry_mutex;
	spinlock_t		nacl_sess_lock;
	struct config_group	acl_group;
	struct config_group	acl_attrib_group;
	struct config_group	acl_auth_group;
	struct config_group	acl_param_group;
	struct config_group	acl_fabric_stat_group;
	struct config_group	*acl_default_groups[5];
	struct list_head	acl_list;
	struct list_head	acl_sess_list;
	struct completion	acl_free_comp;
	struct kref		acl_kref;
};

struct se_session {
	unsigned		sess_tearing_down:1;
	u64			sess_bin_isid;
	enum target_prot_op	sup_prot_ops;
	enum target_prot_type	sess_prot_type;
	struct se_node_acl	*se_node_acl;
	struct se_portal_group *se_tpg;
	void			*fabric_sess_ptr;
	struct list_head	sess_list;
	struct list_head	sess_acl_list;
	struct list_head	sess_cmd_list;
	struct list_head	sess_wait_list;
	spinlock_t		sess_cmd_lock;
	struct kref		sess_kref;
	void			*sess_cmd_map;
	struct percpu_ida	sess_tag_pool;
};

struct se_device;
struct se_transform_info;
struct scatterlist;

struct se_ml_stat_grps {
	struct config_group	stat_group;
	struct config_group	scsi_auth_intr_group;
	struct config_group	scsi_att_intr_port_group;
};

struct se_lun_acl {
	u64			mapped_lun;
	struct se_node_acl	*se_lun_nacl;
	struct se_lun		*se_lun;
	struct config_group	se_lun_group;
	struct se_ml_stat_grps	ml_stat_grps;
};

struct se_dev_entry {
	/* See transport_lunflags_table */
	u64			mapped_lun;
	u64			pr_res_key;
	u64			creation_time;
	u32			lun_flags;
	u32			attach_count;
	atomic_long_t		total_cmds;
	atomic_long_t		read_bytes;
	atomic_long_t		write_bytes;
	atomic_t		ua_count;
	/* Used for PR SPEC_I_PT=1 and REGISTER_AND_MOVE */
	struct kref		pr_kref;
	struct completion	pr_comp;
	struct se_lun_acl __rcu	*se_lun_acl;
	spinlock_t		ua_lock;
	struct se_lun __rcu	*se_lun;
#define DEF_PR_REG_ACTIVE		1
	unsigned long		deve_flags;
	struct list_head	alua_port_list;
	struct list_head	lun_link;
	struct list_head	ua_list;
	struct hlist_node	link;
	struct rcu_head		rcu_head;
};

struct se_dev_attrib {
	int		emulate_model_alias;
	int		emulate_dpo;
	int		emulate_fua_write;
	int		emulate_fua_read;
	int		emulate_write_cache;
	int		emulate_ua_intlck_ctrl;
	int		emulate_tas;
	int		emulate_tpu;
	int		emulate_tpws;
	int		emulate_caw;
	int		emulate_3pc;
	int		pi_prot_format;
	enum target_prot_type pi_prot_type;
	enum target_prot_type hw_pi_prot_type;
	int		enforce_pr_isids;
	int		force_pr_aptpl;
	int		is_nonrot;
	int		emulate_rest_reord;
	u32		hw_block_size;
	u32		block_size;
	u32		hw_max_sectors;
	u32		optimal_sectors;
	u32		hw_queue_depth;
	u32		queue_depth;
	u32		max_unmap_lba_count;
	u32		max_unmap_block_desc_count;
	u32		unmap_granularity;
	u32		unmap_granularity_alignment;
	u32		max_write_same_len;
	u32		max_bytes_per_io;
	struct se_device *da_dev;
	struct config_group da_group;
};

struct se_port_stat_grps {
	struct config_group stat_group;
	struct config_group scsi_port_group;
	struct config_group scsi_tgt_port_group;
	struct config_group scsi_transport_group;
};

struct scsi_port_stats {
	atomic_long_t	cmd_pdus;
	atomic_long_t	tx_data_octets;
	atomic_long_t	rx_data_octets;
};

struct se_lun {
	u64			unpacked_lun;
#define SE_LUN_LINK_MAGIC			0xffff7771
	u32			lun_link_magic;
	u32			lun_access;
	u32			lun_index;

	/* RELATIVE TARGET PORT IDENTIFER */
	u16			lun_rtpi;
	atomic_t		lun_acl_count;
	struct se_device __rcu	*lun_se_dev;

	struct list_head	lun_deve_list;
	spinlock_t		lun_deve_lock;

	/* ALUA state */
	int			lun_tg_pt_secondary_stat;
	int			lun_tg_pt_secondary_write_md;
	atomic_t		lun_tg_pt_secondary_offline;
	struct mutex		lun_tg_pt_md_mutex;

	/* ALUA target port group linkage */
	struct list_head	lun_tg_pt_gp_link;
	struct t10_alua_tg_pt_gp *lun_tg_pt_gp;
	spinlock_t		lun_tg_pt_gp_lock;

	struct se_portal_group	*lun_tpg;
	struct scsi_port_stats	lun_stats;
	struct config_group	lun_group;
	struct se_port_stat_grps port_stat_grps;
	struct completion	lun_ref_comp;
	struct percpu_ref	lun_ref;
	struct list_head	lun_dev_link;
	struct hlist_node	link;
	struct rcu_head		rcu_head;
};

struct se_dev_stat_grps {
	struct config_group stat_group;
	struct config_group scsi_dev_group;
	struct config_group scsi_tgt_dev_group;
	struct config_group scsi_lu_group;
};

struct se_device {
#define SE_DEV_LINK_MAGIC			0xfeeddeef
	u32			dev_link_magic;
	/* RELATIVE TARGET PORT IDENTIFER Counter */
	u16			dev_rpti_counter;
	/* Used for SAM Task Attribute ordering */
	u32			dev_cur_ordered_id;
	u32			dev_flags;
#define DF_CONFIGURED				0x00000001
#define DF_FIRMWARE_VPD_UNIT_SERIAL		0x00000002
#define DF_EMULATED_VPD_UNIT_SERIAL		0x00000004
#define DF_USING_UDEV_PATH			0x00000008
#define DF_USING_ALIAS				0x00000010
#define DF_READ_ONLY				0x00000020
	/* Physical device queue depth */
	u32			queue_depth;
	/* Used for SPC-2 reservations enforce of ISIDs */
	u64			dev_res_bin_isid;
	/* Pointer to transport specific device structure */
	u32			dev_index;
	u64			creation_time;
	atomic_long_t		num_resets;
	atomic_long_t		num_cmds;
	atomic_long_t		read_bytes;
	atomic_long_t		write_bytes;
	/* Active commands on this virtual SE device */
	atomic_t		simple_cmds;
	atomic_t		dev_ordered_sync;
	atomic_t		dev_qf_count;
	u32			export_count;
	spinlock_t		delayed_cmd_lock;
	spinlock_t		execute_task_lock;
	spinlock_t		dev_reservation_lock;
	unsigned int		dev_reservation_flags;
#define DRF_SPC2_RESERVATIONS			0x00000001
#define DRF_SPC2_RESERVATIONS_WITH_ISID		0x00000002
	spinlock_t		se_port_lock;
	spinlock_t		se_tmr_lock;
	spinlock_t		qf_cmd_lock;
	struct semaphore	caw_sem;
	/* Used for legacy SPC-2 reservationsa */
	struct se_node_acl	*dev_reserved_node_acl;
	/* Used for ALUA Logical Unit Group membership */
	struct t10_alua_lu_gp_member *dev_alua_lu_gp_mem;
	/* Used for SPC-3 Persistent Reservations */
	struct t10_pr_registration *dev_pr_res_holder;
	struct list_head	dev_sep_list;
	struct list_head	dev_tmr_list;
	struct workqueue_struct *tmr_wq;
	struct work_struct	qf_work_queue;
	struct list_head	delayed_cmd_list;
	struct list_head	state_list;
	struct list_head	qf_cmd_list;
	struct list_head	g_dev_node;
	/* Pointer to associated SE HBA */
	struct se_hba		*se_hba;
	/* T10 Inquiry and VPD WWN Information */
	struct t10_wwn		t10_wwn;
	/* T10 Asymmetric Logical Unit Assignment for Target Ports */
	struct t10_alua		t10_alua;
	/* T10 SPC-2 + SPC-3 Reservations */
	struct t10_reservation	t10_pr;
	struct se_dev_attrib	dev_attrib;
	struct config_group	dev_group;
	struct config_group	dev_pr_group;
	struct se_dev_stat_grps dev_stat_grps;
#define SE_DEV_ALIAS_LEN 512		/* must be less than PAGE_SIZE */
	unsigned char		dev_alias[SE_DEV_ALIAS_LEN];
#define SE_UDEV_PATH_LEN 512		/* must be less than PAGE_SIZE */
	unsigned char		udev_path[SE_UDEV_PATH_LEN];
	/* Pointer to template of function pointers for transport */
	const struct target_backend_ops *transport;
	/* Linked list for struct se_hba struct se_device list */
	struct list_head	dev_list;
	struct se_lun		xcopy_lun;
	/* Protection Information */
	int			prot_length;
	/* For se_lun->lun_se_dev RCU read-side critical access */
	u32			hba_index;
	struct rcu_head		rcu_head;
};

struct se_hba {
	u16			hba_tpgt;
	u32			hba_id;
	/* See hba_flags_table */
	u32			hba_flags;
	/* Virtual iSCSI devices attached. */
	u32			dev_count;
	u32			hba_index;
	/* Pointer to transport specific host structure. */
	void			*hba_ptr;
	struct list_head	hba_node;
	spinlock_t		device_lock;
	struct config_group	hba_group;
	struct mutex		hba_access_mutex;
	struct target_backend	*backend;
};

struct se_tpg_np {
	struct se_portal_group *tpg_np_parent;
	struct config_group	tpg_np_group;
};

struct se_portal_group {
	/*
	 * PROTOCOL IDENTIFIER value per SPC4, 7.5.1.
	 *
	 * Negative values can be used by fabric drivers for internal use TPGs.
	 */
	int			proto_id;
	/* Number of ACLed Initiator Nodes for this TPG */
	u32			num_node_acls;
	/* Used for PR SPEC_I_PT=1 and REGISTER_AND_MOVE */
	atomic_t		tpg_pr_ref_count;
	/* Spinlock for adding/removing ACLed Nodes */
	struct mutex		acl_node_mutex;
	/* Spinlock for adding/removing sessions */
	spinlock_t		session_lock;
	struct mutex		tpg_lun_mutex;
	struct list_head	se_tpg_node;
	/* linked list for initiator ACL list */
	struct list_head	acl_node_list;
	struct hlist_head	tpg_lun_hlist;
	struct se_lun		*tpg_virt_lun0;
	/* List of TCM sessions associated wth this TPG */
	struct list_head	tpg_sess_list;
	/* Pointer to $FABRIC_MOD dependent code */
	const struct target_core_fabric_ops *se_tpg_tfo;
	struct se_wwn		*se_tpg_wwn;
	struct config_group	tpg_group;
	struct config_group	*tpg_default_groups[7];
	struct config_group	tpg_lun_group;
	struct config_group	tpg_np_group;
	struct config_group	tpg_acl_group;
	struct config_group	tpg_attrib_group;
	struct config_group	tpg_auth_group;
	struct config_group	tpg_param_group;
};

struct se_wwn {
	struct target_fabric_configfs *wwn_tf;
	struct config_group	wwn_group;
	struct config_group	*wwn_default_groups[2];
	struct config_group	fabric_stat_group;
};

static inline void atomic_inc_mb(atomic_t *v)
{
	smp_mb__before_atomic();
	atomic_inc(v);
	smp_mb__after_atomic();
}

static inline void atomic_dec_mb(atomic_t *v)
{
	smp_mb__before_atomic();
	atomic_dec(v);
	smp_mb__after_atomic();
}

#endif /* TARGET_CORE_BASE_H */
