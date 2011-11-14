#ifndef TARGET_CORE_TPG_H
#define TARGET_CORE_TPG_H

extern struct se_node_acl *core_tpg_check_initiator_node_acl(
						struct se_portal_group *,
						unsigned char *);
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

#endif /* TARGET_CORE_TPG_H */
