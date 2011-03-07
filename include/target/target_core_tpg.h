#ifndef TARGET_CORE_TPG_H
#define TARGET_CORE_TPG_H

extern struct se_node_acl *__core_tpg_get_initiator_node_acl(struct se_portal_group *tpg,
						const char *);
extern struct se_node_acl *core_tpg_get_initiator_node_acl(struct se_portal_group *tpg,
						unsigned char *);
extern void core_tpg_add_node_to_devs(struct se_node_acl *,
						struct se_portal_group *);
extern struct se_node_acl *core_tpg_check_initiator_node_acl(
						struct se_portal_group *,
						unsigned char *);
extern void core_tpg_wait_for_nacl_pr_ref(struct se_node_acl *);
extern void core_tpg_wait_for_mib_ref(struct se_node_acl *);
extern void core_tpg_clear_object_luns(struct se_portal_group *);
extern struct se_node_acl *core_tpg_add_initiator_node_acl(
					struct se_portal_group *,
					struct se_node_acl *,
					const char *, u32);
extern int core_tpg_del_initiator_node_acl(struct se_portal_group *,
						struct se_node_acl *, int);
extern int core_tpg_set_initiator_node_queue_depth(struct se_portal_group *,
						unsigned char *, u32, int);
extern int core_tpg_register(struct target_core_fabric_ops *,
					struct se_wwn *,
					struct se_portal_group *, void *,
					int);
extern int core_tpg_deregister(struct se_portal_group *);
extern struct se_lun *core_tpg_pre_addlun(struct se_portal_group *, u32);
extern int core_tpg_post_addlun(struct se_portal_group *, struct se_lun *, u32,
				void *);
extern struct se_lun *core_tpg_pre_dellun(struct se_portal_group *, u32, int *);
extern int core_tpg_post_dellun(struct se_portal_group *, struct se_lun *);

#endif /* TARGET_CORE_TPG_H */
