/* Defined in target_core_configfs.h */
struct target_fabric_configfs;

struct target_core_fabric_ops {
	struct configfs_subsystem *tf_subsys;
	/*
	 * Optional to signal struct se_task->task_sg[] padding entries
	 * for scatterlist chaining using transport_do_task_sg_link(),
	 * disabled by default
	 */
	bool task_sg_chaining;
	char *(*get_fabric_name)(void);
	u8 (*get_fabric_proto_ident)(struct se_portal_group *);
	char *(*tpg_get_wwn)(struct se_portal_group *);
	u16 (*tpg_get_tag)(struct se_portal_group *);
	u32 (*tpg_get_default_depth)(struct se_portal_group *);
	u32 (*tpg_get_pr_transport_id)(struct se_portal_group *,
				struct se_node_acl *,
				struct t10_pr_registration *, int *,
				unsigned char *);
	u32 (*tpg_get_pr_transport_id_len)(struct se_portal_group *,
				struct se_node_acl *,
				struct t10_pr_registration *, int *);
	char *(*tpg_parse_pr_out_transport_id)(struct se_portal_group *,
				const char *, u32 *, char **);
	int (*tpg_check_demo_mode)(struct se_portal_group *);
	int (*tpg_check_demo_mode_cache)(struct se_portal_group *);
	int (*tpg_check_demo_mode_write_protect)(struct se_portal_group *);
	int (*tpg_check_prod_mode_write_protect)(struct se_portal_group *);
	/*
	 * Optionally used by fabrics to allow demo-mode login, but not
	 * expose any TPG LUNs, and return 'not connected' in standard
	 * inquiry response
	 */
	int (*tpg_check_demo_mode_login_only)(struct se_portal_group *);
	struct se_node_acl *(*tpg_alloc_fabric_acl)(
					struct se_portal_group *);
	void (*tpg_release_fabric_acl)(struct se_portal_group *,
					struct se_node_acl *);
	u32 (*tpg_get_inst_index)(struct se_portal_group *);
	/*
	 * Optional function pointer for TCM to perform command map
	 * from TCM processing thread context, for those struct se_cmd
	 * initially allocated in interrupt context.
	 */
	int (*new_cmd_map)(struct se_cmd *);
	/*
	 * Optional to release struct se_cmd and fabric dependent allocated
	 * I/O descriptor in transport_cmd_check_stop()
	 */
	void (*check_stop_free)(struct se_cmd *);
	void (*release_cmd)(struct se_cmd *);
	/*
	 * Called with spin_lock_bh(struct se_portal_group->session_lock held.
	 */
	int (*shutdown_session)(struct se_session *);
	void (*close_session)(struct se_session *);
	void (*stop_session)(struct se_session *, int, int);
	void (*fall_back_to_erl0)(struct se_session *);
	int (*sess_logged_in)(struct se_session *);
	u32 (*sess_get_index)(struct se_session *);
	/*
	 * Used only for SCSI fabrics that contain multi-value TransportIDs
	 * (like iSCSI).  All other SCSI fabrics should set this to NULL.
	 */
	u32 (*sess_get_initiator_sid)(struct se_session *,
				      unsigned char *, u32);
	int (*write_pending)(struct se_cmd *);
	int (*write_pending_status)(struct se_cmd *);
	void (*set_default_node_attributes)(struct se_node_acl *);
	u32 (*get_task_tag)(struct se_cmd *);
	int (*get_cmd_state)(struct se_cmd *);
	int (*queue_data_in)(struct se_cmd *);
	int (*queue_status)(struct se_cmd *);
	int (*queue_tm_rsp)(struct se_cmd *);
	u16 (*set_fabric_sense_len)(struct se_cmd *, u32);
	u16 (*get_fabric_sense_len)(void);
	int (*is_state_remove)(struct se_cmd *);
	/*
	 * fabric module calls for target_core_fabric_configfs.c
	 */
	struct se_wwn *(*fabric_make_wwn)(struct target_fabric_configfs *,
				struct config_group *, const char *);
	void (*fabric_drop_wwn)(struct se_wwn *);
	struct se_portal_group *(*fabric_make_tpg)(struct se_wwn *,
				struct config_group *, const char *);
	void (*fabric_drop_tpg)(struct se_portal_group *);
	int (*fabric_post_link)(struct se_portal_group *,
				struct se_lun *);
	void (*fabric_pre_unlink)(struct se_portal_group *,
				struct se_lun *);
	struct se_tpg_np *(*fabric_make_np)(struct se_portal_group *,
				struct config_group *, const char *);
	void (*fabric_drop_np)(struct se_tpg_np *);
	struct se_node_acl *(*fabric_make_nodeacl)(struct se_portal_group *,
				struct config_group *, const char *);
	void (*fabric_drop_nodeacl)(struct se_node_acl *);
};
