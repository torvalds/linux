#ifndef TARGET_CORE_DEVICE_H
#define TARGET_CORE_DEVICE_H

extern int transport_get_lun_for_cmd(struct se_cmd *, unsigned char *, u32);
extern int transport_get_lun_for_tmr(struct se_cmd *, u32);
extern struct se_dev_entry *core_get_se_deve_from_rtpi(
					struct se_node_acl *, u16);
extern int core_free_device_list_for_node(struct se_node_acl *,
					struct se_portal_group *);
extern void core_dec_lacl_count(struct se_node_acl *, struct se_cmd *);
extern void core_update_device_list_access(u32, u32, struct se_node_acl *);
extern int core_update_device_list_for_node(struct se_lun *, struct se_lun_acl *, u32,
					u32, struct se_node_acl *,
					struct se_portal_group *, int);
extern void core_clear_lun_from_tpg(struct se_lun *, struct se_portal_group *);
extern int core_dev_export(struct se_device *, struct se_portal_group *,
					struct se_lun *);
extern void core_dev_unexport(struct se_device *, struct se_portal_group *,
					struct se_lun *);
extern int transport_core_report_lun_response(struct se_cmd *);
extern void se_release_device_for_hba(struct se_device *);
extern void se_release_vpd_for_dev(struct se_device *);
extern void se_clear_dev_ports(struct se_device *);
extern int se_free_virtual_device(struct se_device *, struct se_hba *);
extern int se_dev_check_online(struct se_device *);
extern int se_dev_check_shutdown(struct se_device *);
extern void se_dev_set_default_attribs(struct se_device *, struct se_dev_limits *);
extern int se_dev_set_task_timeout(struct se_device *, u32);
extern int se_dev_set_max_unmap_lba_count(struct se_device *, u32);
extern int se_dev_set_max_unmap_block_desc_count(struct se_device *, u32);
extern int se_dev_set_unmap_granularity(struct se_device *, u32);
extern int se_dev_set_unmap_granularity_alignment(struct se_device *, u32);
extern int se_dev_set_emulate_dpo(struct se_device *, int);
extern int se_dev_set_emulate_fua_write(struct se_device *, int);
extern int se_dev_set_emulate_fua_read(struct se_device *, int);
extern int se_dev_set_emulate_write_cache(struct se_device *, int);
extern int se_dev_set_emulate_ua_intlck_ctrl(struct se_device *, int);
extern int se_dev_set_emulate_tas(struct se_device *, int);
extern int se_dev_set_emulate_tpu(struct se_device *, int);
extern int se_dev_set_emulate_tpws(struct se_device *, int);
extern int se_dev_set_enforce_pr_isids(struct se_device *, int);
extern int se_dev_set_queue_depth(struct se_device *, u32);
extern int se_dev_set_max_sectors(struct se_device *, u32);
extern int se_dev_set_optimal_sectors(struct se_device *, u32);
extern int se_dev_set_block_size(struct se_device *, u32);
extern struct se_lun *core_dev_add_lun(struct se_portal_group *, struct se_hba *,
					struct se_device *, u32);
extern int core_dev_del_lun(struct se_portal_group *, u32);
extern struct se_lun *core_get_lun_from_tpg(struct se_portal_group *, u32);
extern struct se_lun_acl *core_dev_init_initiator_node_lun_acl(struct se_portal_group *,
							u32, char *, int *);
extern int core_dev_add_initiator_node_lun_acl(struct se_portal_group *,
						struct se_lun_acl *, u32, u32);
extern int core_dev_del_initiator_node_lun_acl(struct se_portal_group *,
						struct se_lun *, struct se_lun_acl *);
extern void core_dev_free_initiator_node_lun_acl(struct se_portal_group *,
						struct se_lun_acl *lacl);
extern int core_dev_setup_virtual_lun0(void);
extern void core_dev_release_virtual_lun0(void);

#endif /* TARGET_CORE_DEVICE_H */
