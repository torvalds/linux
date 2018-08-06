struct inode_list {
	struct inode *inode;
	struct list_head list;
};

struct task_list {
	struct task_struct *task;
	unsigned long clone_flags;
	struct list_head list;
};

struct kern_ipc_perm_list {
	struct kern_ipc_perm *ipcp;
	int (*medusa_l1_ipc_alloc_security)(struct kern_ipc_perm *);
	struct list_head list;
};

extern struct security_hook_list medusa_l0_hooks[];
extern struct task_list l0_task_list;
extern struct inode_list l0_inode_list;
extern struct kern_ipc_perm_list l0_kern_ipc_perm_list;

