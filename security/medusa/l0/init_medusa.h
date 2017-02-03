struct inode_list{
    struct inode *inode;
    struct list_head list; 
};

struct cred_list{
    struct cred *cred;
    gfp_t gfp;
    struct list_head list; 
};

extern struct security_hook_list medusa_l0_hooks[];
extern struct cred_list l0_cred_list;
extern struct inode_list l0_inode_list;

