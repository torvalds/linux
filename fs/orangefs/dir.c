/*
 * Copyright 2017 Omnibond Systems, L.L.C.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"

/*
 * There can be up to 512 directory entries.  Each entry is encoded as
 * follows:
 * 4 bytes: string size (n)
 * n bytes: string
 * 1 byte: trailing zero
 * padding to 8 bytes
 * 16 bytes: khandle
 * padding to 8 bytes
 */
#define MAX_DIRECTORY ((4 + 257 + 3 + 16)*512)

struct orangefs_dir {
	__u64 token;
	void *directory;
	size_t len;
	int error;
};

/*
 * The userspace component sends several directory entries of the
 * following format.  The first four bytes are the string length not
 * including a trailing zero byte.  This is followed by the string and a
 * trailing zero padded to the next four byte boundry.  This is followed
 * by the sixteen byte khandle padded to the next eight byte boundry.
 *
 * The trailer_buf starts with a struct orangefs_readdir_response_s
 * which must be skipped to get to the directory data.
 */

static int orangefs_dir_more(struct orangefs_inode_s *oi,
    struct orangefs_dir *od, struct dentry *dentry)
{
	const size_t offset =
	    sizeof(struct orangefs_readdir_response_s);
	struct orangefs_readdir_response_s *resp;
	struct orangefs_kernel_op_s *op;
	int bufi, r;

	op = op_alloc(ORANGEFS_VFS_OP_READDIR);
	if (!op) {
		od->error = -ENOMEM;
		return -ENOMEM;
	}

	/*
	 * Despite the badly named field, readdir does not use shared
	 * memory.  However, there are a limited number of readdir
	 * slots, which must be allocated here.  This flag simply tells
	 * the op scheduler to return the op here for retry.
	 */
	op->uses_shared_memory = 1;
	op->upcall.req.readdir.refn = oi->refn;
	op->upcall.req.readdir.token = od->token;
	op->upcall.req.readdir.max_dirent_count =
	    ORANGEFS_MAX_DIRENT_COUNT_READDIR;

again:
	bufi = orangefs_readdir_index_get();
	if (bufi < 0) {
		op_release(op);
		od->error = bufi;
		return bufi;
	}

	op->upcall.req.readdir.buf_index = bufi;

	r = service_operation(op, "orangefs_readdir",
	    get_interruptible_flag(dentry->d_inode));

	orangefs_readdir_index_put(bufi);

	if (op_state_purged(op)) {
		if (r == -EAGAIN) {
			vfree(op->downcall.trailer_buf);
			goto again;
		} else if (r == -EIO) {
			vfree(op->downcall.trailer_buf);
			op_release(op);
			od->error = r;
			return r;
		}
	}

	if (r < 0) {
		vfree(op->downcall.trailer_buf);
		op_release(op);
		od->error = r;
		return r;
	} else if (op->downcall.status) {
		vfree(op->downcall.trailer_buf);
		op_release(op);
		od->error = op->downcall.status;
		return op->downcall.status;
	}

	resp = (struct orangefs_readdir_response_s *)
	    op->downcall.trailer_buf;
	od->token = resp->token;

	if (od->len + op->downcall.trailer_size - offset <=
	    MAX_DIRECTORY) {
		memcpy(od->directory + od->len,
		    op->downcall.trailer_buf + offset,
		    op->downcall.trailer_size - offset);
		od->len += op->downcall.trailer_size - offset;
	} else {
		/* This limit was chosen based on protocol limits. */
		gossip_err("orangefs_dir_more: userspace sent too much data\n");
		vfree(op->downcall.trailer_buf);
		op_release(op);
		od->error = -EIO;
		return -EIO;
	}

	vfree(op->downcall.trailer_buf);
	op_release(op);
	return 0;
}

static int orangefs_dir_fill(struct orangefs_inode_s *oi,
    struct orangefs_dir *od, struct dentry *dentry,
    struct dir_context *ctx)
{
	struct orangefs_khandle *khandle;
	__u32 *len, padlen;
	loff_t i;
	char *s;
	i = ctx->pos - 2;
	while (i < od->len) {
		if (od->len < i + sizeof *len)
			goto eio;
		len = od->directory + i;
		/*
		 * len is the size of the string itself.  padlen is the
		 * total size of the encoded string.
		 */
		padlen = (sizeof *len + *len + 1) +
		    (4 - (sizeof *len + *len + 1)%8)%8;
		if (od->len < i + padlen + sizeof *khandle)
			goto eio;
		s = od->directory + i + sizeof *len;
		if (s[*len] != 0)
			goto eio;
		khandle = od->directory + i + padlen;

		if (!dir_emit(ctx, s, *len,
		    orangefs_khandle_to_ino(khandle), DT_UNKNOWN))
			return 0;
		i += padlen + sizeof *khandle;
		i = i + (8 - i%8)%8;
		ctx->pos = i + 2;
	}
	BUG_ON(i > od->len);
	return 0;
eio:
	/*
	 * Here either data from userspace is corrupt or the application
	 * has sought to an invalid location.
	 */
	od->error = -EIO;
	return -EIO;
}

static int orangefs_dir_iterate(struct file *file,
    struct dir_context *ctx)
{
	struct orangefs_inode_s *oi;
	struct orangefs_dir *od;
	struct dentry *dentry;
	int r;

	dentry = file->f_path.dentry;
	oi = ORANGEFS_I(dentry->d_inode);
	od = file->private_data;

	if (od->error)
		return od->error;

	if (ctx->pos == 0) {
		if (!dir_emit_dot(file, ctx))
			return 0;
		ctx->pos++;
	}
	if (ctx->pos == 1) {
		if (!dir_emit_dotdot(file, ctx))
			return 0;
		ctx->pos++;
	}

	r = 0;

	/*
	 * Must read more if the user has sought past what has been read
	 * so far.  Stop a user who has sought past the end.
	 */
	while (od->token != ORANGEFS_READDIR_END && ctx->pos - 2 >
	    od->len) {
		r = orangefs_dir_more(oi, od, dentry);
		if (r)
			return r;
	}
	if (od->token == ORANGEFS_READDIR_END && ctx->pos - 2 >
	    od->len) {
		return -EIO;
	}

	/* Then try to fill if there's any left in the buffer. */
	if (ctx->pos - 2 < od->len) {
		r = orangefs_dir_fill(oi, od, dentry, ctx);
		if (r)
			return r;
	}

	/* Finally get some more and try to fill. */
	if (od->token != ORANGEFS_READDIR_END) {
		r = orangefs_dir_more(oi, od, dentry);
		if (r)
			return r;
		r = orangefs_dir_fill(oi, od, dentry, ctx);
	}

	return r;
}

static int orangefs_dir_open(struct inode *inode, struct file *file)
{
	struct orangefs_dir *od;
	file->private_data = kmalloc(sizeof(struct orangefs_dir),
	    GFP_KERNEL);
	if (!file->private_data)
		return -ENOMEM;
	od = file->private_data;
	od->token = ORANGEFS_READDIR_START;
	/*
	 * XXX: It seems wasteful to allocate such a large buffer for
	 * each request.  Most will be much smaller.
	 */
	od->directory = alloc_pages_exact(MAX_DIRECTORY, GFP_KERNEL);
	if (!od->directory) {
		kfree(file->private_data);
		return -ENOMEM;
	}
	od->len = 0;
	od->error = 0;
	return 0;
}

static int orangefs_dir_release(struct inode *inode, struct file *file)
{
	struct orangefs_dir *od = file->private_data;
	orangefs_flush_inode(inode);
	free_pages_exact(od->directory, MAX_DIRECTORY);
	kfree(od);
	return 0;
}

const struct file_operations orangefs_dir_operations = {
	.llseek = default_llseek,
	.read = generic_read_dir,
	.iterate = orangefs_dir_iterate,
	.open = orangefs_dir_open,
	.release = orangefs_dir_release
};
