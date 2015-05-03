#define TARGET_CORE_CONFIGFS_VERSION TARGET_CORE_MOD_VERSION

#define TARGET_CORE_CONFIG_ROOT	"/sys/kernel/config"

#define TARGET_CORE_NAME_MAX_LEN	64
#define TARGET_FABRIC_NAME_SIZE		32

struct target_fabric_configfs {
	atomic_t		tf_access_cnt;
	struct list_head	tf_list;
	struct config_group	tf_group;
	struct config_group	tf_disc_group;
	struct config_group	*tf_default_groups[2];
	const struct target_core_fabric_ops *tf_ops;

	struct config_item_type tf_discovery_cit;
	struct config_item_type	tf_wwn_cit;
	struct config_item_type tf_wwn_fabric_stats_cit;
	struct config_item_type tf_tpg_cit;
	struct config_item_type tf_tpg_base_cit;
	struct config_item_type tf_tpg_lun_cit;
	struct config_item_type tf_tpg_port_cit;
	struct config_item_type tf_tpg_port_stat_cit;
	struct config_item_type tf_tpg_np_cit;
	struct config_item_type tf_tpg_np_base_cit;
	struct config_item_type tf_tpg_attrib_cit;
	struct config_item_type tf_tpg_auth_cit;
	struct config_item_type tf_tpg_param_cit;
	struct config_item_type tf_tpg_nacl_cit;
	struct config_item_type tf_tpg_nacl_base_cit;
	struct config_item_type tf_tpg_nacl_attrib_cit;
	struct config_item_type tf_tpg_nacl_auth_cit;
	struct config_item_type tf_tpg_nacl_param_cit;
	struct config_item_type tf_tpg_nacl_stat_cit;
	struct config_item_type tf_tpg_mappedlun_cit;
	struct config_item_type tf_tpg_mappedlun_stat_cit;
};
