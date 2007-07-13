#ifndef _LINUX_SUNRPC_RPC_PIPE_FS_H
#define _LINUX_SUNRPC_RPC_PIPE_FS_H

#ifdef __KERNEL__

struct rpc_pipe_msg {
	struct list_head list;
	void *data;
	size_t len;
	size_t copied;
	int errno;
};

struct rpc_pipe_ops {
	ssize_t (*upcall)(struct file *, struct rpc_pipe_msg *, char __user *, size_t);
	ssize_t (*downcall)(struct file *, const char __user *, size_t);
	void (*release_pipe)(struct inode *);
	void (*destroy_msg)(struct rpc_pipe_msg *);
};

struct rpc_inode {
	struct inode vfs_inode;
	void *private;
	struct list_head pipe;
	struct list_head in_upcall;
	struct list_head in_downcall;
	int pipelen;
	int nreaders;
	int nwriters;
	int nkern_readwriters;
	wait_queue_head_t waitq;
#define RPC_PIPE_WAIT_FOR_OPEN	1
	int flags;
	struct rpc_pipe_ops *ops;
	struct delayed_work queue_timeout;
};

static inline struct rpc_inode *
RPC_I(struct inode *inode)
{
	return container_of(inode, struct rpc_inode, vfs_inode);
}

extern int rpc_queue_upcall(struct inode *, struct rpc_pipe_msg *);

extern struct dentry *rpc_mkdir(char *, struct rpc_clnt *);
extern int rpc_rmdir(struct dentry *);
extern struct dentry *rpc_mkpipe(struct dentry *, const char *, void *, struct rpc_pipe_ops *, int flags);
extern int rpc_unlink(struct dentry *);
extern struct vfsmount *rpc_get_mount(void);
extern void rpc_put_mount(void);
extern int register_rpc_pipefs(void);
extern void unregister_rpc_pipefs(void);

#endif
#endif
