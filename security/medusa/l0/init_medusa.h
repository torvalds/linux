struct inode_list{
    struct inode *inode;
    struct list_head list; 
};

struct cred_list{
    struct cred *task;
    struct list_head list; 
};

extern struct security_hook_list medusa_tmp_hooks[];
extern struct cred_list tmp_cred_list;
extern struct inode_list tmp_inode_list;

