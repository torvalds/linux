#include <linux/bpf.h>
#include <linux/vmalloc.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/idr.h>
#include <linux/namei.h>
#include <linux/user_namespace.h>
#include <linux/security.h>

static bool bpf_ns_capable(struct user_namespace *ns, int cap)
{
	return ns_capable(ns, cap) || (cap != CAP_SYS_ADMIN && ns_capable(ns, CAP_SYS_ADMIN));
}

bool bpf_token_capable(const struct bpf_token *token, int cap)
{
	struct user_namespace *userns;

	/* BPF token allows ns_capable() level of capabilities */
	userns = token ? token->userns : &init_user_ns;
	if (!bpf_ns_capable(userns, cap))
		return false;
	if (token && security_bpf_token_capable(token, cap) < 0)
		return false;
	return true;
}

void bpf_token_inc(struct bpf_token *token)
{
	atomic64_inc(&token->refcnt);
}

static void bpf_token_free(struct bpf_token *token)
{
	security_bpf_token_free(token);
	put_user_ns(token->userns);
	kfree(token);
}

static void bpf_token_put_deferred(struct work_struct *work)
{
	struct bpf_token *token = container_of(work, struct bpf_token, work);

	bpf_token_free(token);
}

void bpf_token_put(struct bpf_token *token)
{
	if (!token)
		return;

	if (!atomic64_dec_and_test(&token->refcnt))
		return;

	INIT_WORK(&token->work, bpf_token_put_deferred);
	schedule_work(&token->work);
}

static int bpf_token_release(struct inode *inode, struct file *filp)
{
	struct bpf_token *token = filp->private_data;

	bpf_token_put(token);
	return 0;
}

static void bpf_token_show_fdinfo(struct seq_file *m, struct file *filp)
{
	struct bpf_token *token = filp->private_data;
	u64 mask;

	BUILD_BUG_ON(__MAX_BPF_CMD >= 64);
	mask = BIT_ULL(__MAX_BPF_CMD) - 1;
	if ((token->allowed_cmds & mask) == mask)
		seq_printf(m, "allowed_cmds:\tany\n");
	else
		seq_printf(m, "allowed_cmds:\t0x%llx\n", token->allowed_cmds);

	BUILD_BUG_ON(__MAX_BPF_MAP_TYPE >= 64);
	mask = BIT_ULL(__MAX_BPF_MAP_TYPE) - 1;
	if ((token->allowed_maps & mask) == mask)
		seq_printf(m, "allowed_maps:\tany\n");
	else
		seq_printf(m, "allowed_maps:\t0x%llx\n", token->allowed_maps);

	BUILD_BUG_ON(__MAX_BPF_PROG_TYPE >= 64);
	mask = BIT_ULL(__MAX_BPF_PROG_TYPE) - 1;
	if ((token->allowed_progs & mask) == mask)
		seq_printf(m, "allowed_progs:\tany\n");
	else
		seq_printf(m, "allowed_progs:\t0x%llx\n", token->allowed_progs);

	BUILD_BUG_ON(__MAX_BPF_ATTACH_TYPE >= 64);
	mask = BIT_ULL(__MAX_BPF_ATTACH_TYPE) - 1;
	if ((token->allowed_attachs & mask) == mask)
		seq_printf(m, "allowed_attachs:\tany\n");
	else
		seq_printf(m, "allowed_attachs:\t0x%llx\n", token->allowed_attachs);
}

#define BPF_TOKEN_INODE_NAME "bpf-token"

static const struct inode_operations bpf_token_iops = { };

static const struct file_operations bpf_token_fops = {
	.release	= bpf_token_release,
	.show_fdinfo	= bpf_token_show_fdinfo,
};

int bpf_token_create(union bpf_attr *attr)
{
	struct bpf_mount_opts *mnt_opts;
	struct bpf_token *token = NULL;
	struct user_namespace *userns;
	struct inode *inode;
	struct file *file;
	struct path path;
	struct fd f;
	umode_t mode;
	int err, fd;

	f = fdget(attr->token_create.bpffs_fd);
	if (!f.file)
		return -EBADF;

	path = f.file->f_path;
	path_get(&path);
	fdput(f);

	if (path.dentry != path.mnt->mnt_sb->s_root) {
		err = -EINVAL;
		goto out_path;
	}
	if (path.mnt->mnt_sb->s_op != &bpf_super_ops) {
		err = -EINVAL;
		goto out_path;
	}
	err = path_permission(&path, MAY_ACCESS);
	if (err)
		goto out_path;

	userns = path.dentry->d_sb->s_user_ns;
	/*
	 * Enforce that creators of BPF tokens are in the same user
	 * namespace as the BPF FS instance. This makes reasoning about
	 * permissions a lot easier and we can always relax this later.
	 */
	if (current_user_ns() != userns) {
		err = -EPERM;
		goto out_path;
	}
	if (!ns_capable(userns, CAP_BPF)) {
		err = -EPERM;
		goto out_path;
	}

	/* Creating BPF token in init_user_ns doesn't make much sense. */
	if (current_user_ns() == &init_user_ns) {
		err = -EOPNOTSUPP;
		goto out_path;
	}

	mnt_opts = path.dentry->d_sb->s_fs_info;
	if (mnt_opts->delegate_cmds == 0 &&
	    mnt_opts->delegate_maps == 0 &&
	    mnt_opts->delegate_progs == 0 &&
	    mnt_opts->delegate_attachs == 0) {
		err = -ENOENT; /* no BPF token delegation is set up */
		goto out_path;
	}

	mode = S_IFREG | ((S_IRUSR | S_IWUSR) & ~current_umask());
	inode = bpf_get_inode(path.mnt->mnt_sb, NULL, mode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_path;
	}

	inode->i_op = &bpf_token_iops;
	inode->i_fop = &bpf_token_fops;
	clear_nlink(inode); /* make sure it is unlinked */

	file = alloc_file_pseudo(inode, path.mnt, BPF_TOKEN_INODE_NAME, O_RDWR, &bpf_token_fops);
	if (IS_ERR(file)) {
		iput(inode);
		err = PTR_ERR(file);
		goto out_path;
	}

	token = kzalloc(sizeof(*token), GFP_USER);
	if (!token) {
		err = -ENOMEM;
		goto out_file;
	}

	atomic64_set(&token->refcnt, 1);

	/* remember bpffs owning userns for future ns_capable() checks */
	token->userns = get_user_ns(userns);

	token->allowed_cmds = mnt_opts->delegate_cmds;
	token->allowed_maps = mnt_opts->delegate_maps;
	token->allowed_progs = mnt_opts->delegate_progs;
	token->allowed_attachs = mnt_opts->delegate_attachs;

	err = security_bpf_token_create(token, attr, &path);
	if (err)
		goto out_token;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		err = fd;
		goto out_token;
	}

	file->private_data = token;
	fd_install(fd, file);

	path_put(&path);
	return fd;

out_token:
	bpf_token_free(token);
out_file:
	fput(file);
out_path:
	path_put(&path);
	return err;
}

struct bpf_token *bpf_token_get_from_fd(u32 ufd)
{
	struct fd f = fdget(ufd);
	struct bpf_token *token;

	if (!f.file)
		return ERR_PTR(-EBADF);
	if (f.file->f_op != &bpf_token_fops) {
		fdput(f);
		return ERR_PTR(-EINVAL);
	}

	token = f.file->private_data;
	bpf_token_inc(token);
	fdput(f);

	return token;
}

bool bpf_token_allow_cmd(const struct bpf_token *token, enum bpf_cmd cmd)
{
	if (!token)
		return false;
	if (!(token->allowed_cmds & BIT_ULL(cmd)))
		return false;
	return security_bpf_token_cmd(token, cmd) == 0;
}

bool bpf_token_allow_map_type(const struct bpf_token *token, enum bpf_map_type type)
{
	if (!token || type >= __MAX_BPF_MAP_TYPE)
		return false;

	return token->allowed_maps & BIT_ULL(type);
}

bool bpf_token_allow_prog_type(const struct bpf_token *token,
			       enum bpf_prog_type prog_type,
			       enum bpf_attach_type attach_type)
{
	if (!token || prog_type >= __MAX_BPF_PROG_TYPE || attach_type >= __MAX_BPF_ATTACH_TYPE)
		return false;

	return (token->allowed_progs & BIT_ULL(prog_type)) &&
	       (token->allowed_attachs & BIT_ULL(attach_type));
}
