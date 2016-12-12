#ifndef TARGET_CORE_FABRIC_H
#define TARGET_CORE_FABRIC_H

struct target_core_fabric_ops {
	struct module *module;
	const char *name;
	size_t node_acl_size;
	/*
	 * Limits number of scatterlist entries per SCF_SCSI_DATA_CDB payload.
	 * Setting this value tells target-core to enforce this limit, and
	 * report as INQUIRY EVPD=b0 MAXIMUM TRANSFER LENGTH.
	 *
	 * target-core will currently reset se_cmd->data_length to this
	 * maximum size, and set UNDERFLOW residual count if length exceeds
	 * this limit.
	 *
	 * XXX: Not all initiator hosts honor this block-limit EVPD
	 * XXX: Currently assumes single PAGE_SIZE per scatterlist entry
	 */
	u32 max_data_sg_nents;
	char *(*get_fabric_name)(void);
	char *(*tpg_get_wwn)(struct se_portal_group *);
	u16 (*tpg_get_tag)(struct se_portal_group *);
	u32 (*tpg_get_default_depth)(struct se_portal_group *);
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
	/*
	 * Optionally used as a configfs tunable to determine when
	 * target-core should signal the PROTECT=1 feature bit for
	 * backends that don't support T10-PI, so that either fabric
	 * HW offload or target-core emulation performs the associated
	 * WRITE_STRIP and READ_INSERT operations.
	 */
	int (*tpg_check_prot_fabric_only)(struct se_portal_group *);
	u32 (*tpg_get_inst_index)(struct se_portal_group *);
	/*
	 * Optional to release struct se_cmd and fabric dependent allocated
	 * I/O descriptor in transport_cmd_check_stop().
	 *
	 * Returning 1 will signal a descriptor has been released.
	 * Returning 0 will signal a descriptor has not been released.
	 */
	int (*check_stop_free)(struct se_cmd *);
	void (*release_cmd)(struct se_cmd *);
	/*
	 * Called with spin_lock_bh(struct se_portal_group->session_lock held.
	 */
	int (*shutdown_session)(struct se_session *);
	void (*close_session)(struct se_session *);
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
	int (*get_cmd_state)(struct se_cmd *);
	int (*queue_data_in)(struct se_cmd *);
	int (*queue_status)(struct se_cmd *);
	void (*queue_tm_rsp)(struct se_cmd *);
	void (*aborted_task)(struct se_cmd *);
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
	int (*fabric_init_nodeacl)(struct se_node_acl *, const char *);
	void (*fabric_cleanup_nodeacl)(struct se_node_acl *);

	struct configfs_attribute **tfc_discovery_attrs;
	struct configfs_attribute **tfc_wwn_attrs;
	struct configfs_attribute **tfc_tpg_base_attrs;
	struct configfs_attribute **tfc_tpg_np_base_attrs;
	struct configfs_attribute **tfc_tpg_attrib_attrs;
	struct configfs_attribute **tfc_tpg_auth_attrs;
	struct configfs_attribute **tfc_tpg_param_attrs;
	struct configfs_attribute **tfc_tpg_nacl_base_attrs;
	struct configfs_attribute **tfc_tpg_nacl_attrib_attrs;
	struct configfs_attribute **tfc_tpg_nacl_auth_attrs;
	struct configfs_attribute **tfc_tpg_nacl_param_attrs;
};

int target_register_template(const struct target_core_fabric_ops *fo);
void target_unregister_template(const struct target_core_fabric_ops *fo);

int target_depend_item(struct config_item *item);
void target_undepend_item(struct config_item *item);

struct se_session *transport_init_session(enum target_prot_op);
int transport_alloc_session_tags(struct se_session *, unsigned int,
		unsigned int);
struct se_session *transport_init_session_tags(unsigned int, unsigned int,
		enum target_prot_op);
void	__transport_register_session(struct se_portal_group *,
		struct se_node_acl *, struct se_session *, void *);
void	transport_register_session(struct se_portal_group *,
		struct se_node_acl *, struct se_session *, void *);
void	target_get_session(struct se_session *);
void	target_put_session(struct se_session *);
ssize_t	target_show_dynamic_sessions(struct se_portal_group *, char *);
void	transport_free_session(struct se_session *);
void	target_put_nacl(struct se_node_acl *);
void	transport_deregister_session_configfs(struct se_session *);
void	transport_deregister_session(struct se_session *);


void	transport_init_se_cmd(struct se_cmd *,
		const struct target_core_fabric_ops *,
		struct se_session *, u32, int, int, unsigned char *);
sense_reason_t transport_lookup_cmd_lun(struct se_cmd *, u64);
sense_reason_t target_setup_cmd_from_cdb(struct se_cmd *, unsigned char *);
int	target_submit_cmd_map_sgls(struct se_cmd *, struct se_session *,
		unsigned char *, unsigned char *, u64, u32, int, int, int,
		struct scatterlist *, u32, struct scatterlist *, u32,
		struct scatterlist *, u32);
int	target_submit_cmd(struct se_cmd *, struct se_session *, unsigned char *,
		unsigned char *, u64, u32, int, int, int);
int	target_submit_tmr(struct se_cmd *se_cmd, struct se_session *se_sess,
		unsigned char *sense, u64 unpacked_lun,
		void *fabric_tmr_ptr, unsigned char tm_type,
		gfp_t, unsigned int, int);
int	transport_handle_cdb_direct(struct se_cmd *);
sense_reason_t	transport_generic_new_cmd(struct se_cmd *);

void	target_execute_cmd(struct se_cmd *cmd);

int	transport_generic_free_cmd(struct se_cmd *, int);

bool	transport_wait_for_tasks(struct se_cmd *);
int	transport_check_aborted_status(struct se_cmd *, int);
int	transport_send_check_condition_and_sense(struct se_cmd *,
		sense_reason_t, int);
int	target_get_sess_cmd(struct se_cmd *, bool);
int	target_put_sess_cmd(struct se_cmd *);
void	target_sess_cmd_list_set_waiting(struct se_session *);
void	target_wait_for_sess_cmds(struct se_session *);

int	core_alua_check_nonop_delay(struct se_cmd *);

int	core_tmr_alloc_req(struct se_cmd *, void *, u8, gfp_t);
void	core_tmr_release_req(struct se_tmr_req *);
int	transport_generic_handle_tmr(struct se_cmd *);
void	transport_generic_request_failure(struct se_cmd *, sense_reason_t);
int	transport_lookup_tmr_lun(struct se_cmd *, u64);
void	core_allocate_nexus_loss_ua(struct se_node_acl *acl);

struct se_node_acl *core_tpg_get_initiator_node_acl(struct se_portal_group *tpg,
		unsigned char *);
struct se_node_acl *core_tpg_check_initiator_node_acl(struct se_portal_group *,
		unsigned char *);
int	core_tpg_set_initiator_node_queue_depth(struct se_portal_group *,
		unsigned char *, u32, int);
int	core_tpg_set_initiator_node_tag(struct se_portal_group *,
		struct se_node_acl *, const char *);
int	core_tpg_register(struct se_wwn *, struct se_portal_group *, int);
int	core_tpg_deregister(struct se_portal_group *);

/*
 * The LIO target core uses DMA_TO_DEVICE to mean that data is going
 * to the target (eg handling a WRITE) and DMA_FROM_DEVICE to mean
 * that data is coming from the target (eg handling a READ).  However,
 * this is just the opposite of what we have to tell the DMA mapping
 * layer -- eg when handling a READ, the HBA will have to DMA the data
 * out of memory so it can send it to the initiator, which means we
 * need to use DMA_TO_DEVICE when we map the data.
 */
static inline enum dma_data_direction
target_reverse_dma_direction(struct se_cmd *se_cmd)
{
	if (se_cmd->se_cmd_flags & SCF_BIDI)
		return DMA_BIDIRECTIONAL;

	switch (se_cmd->data_direction) {
	case DMA_TO_DEVICE:
		return DMA_FROM_DEVICE;
	case DMA_FROM_DEVICE:
		return DMA_TO_DEVICE;
	case DMA_NONE:
	default:
		return DMA_NONE;
	}
}

#endif /* TARGET_CORE_FABRICH */
