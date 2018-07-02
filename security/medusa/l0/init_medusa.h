struct inode_list {
	struct inode *inode;
	struct list_head list;
};

struct cred_list {
	struct cred *cred;
	gfp_t gfp;
	struct list_head list;
};

struct kern_ipc_perm_list {
	struct kern_ipc_perm *ipcp;
	int (*medusa_l1_ipc_alloc_security)(struct kern_ipc_perm *);
	struct list_head list;
};

struct msg_msg_list {
	struct msg_msg *msg;
	struct list_head list;
};

extern struct security_hook_list medusa_l0_hooks[];
extern struct cred_list l0_cred_list;
extern struct inode_list l0_inode_list;
extern struct kern_ipc_perm_list l0_kern_ipc_perm_list;
extern struct msg_msg_list l0_msg_msg_list;

