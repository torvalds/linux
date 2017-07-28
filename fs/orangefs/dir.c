/*
 * Copyright 2017 Omnibond Systems, L.L.C.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"

struct orangefs_dir_part {
	struct orangefs_dir_part *next;
	size_t len;
};

struct orangefs_dir {
	__u64 token;
	struct orangefs_dir_part *part;
	loff_t end;
	int error;
};

#define PART_SHIFT (24)
#define PART_SIZE (1<<24)
#define PART_MASK (~(PART_SIZE - 1))

/*
 * There can be up to 512 directory entries.  Each entry is encoded as
 * follows:
 * 4 bytes: string size (n)
 * n bytes: string
 * 1 byte: trailing zero
 * padding to 8 bytes
 * 16 bytes: khandle
 * padding to 8 bytes
 *
 * The trailer_buf starts with a struct orangefs_readdir_response_s
 * which must be skipped to get to the directory data.
 *
 * The data which is received from the userspace daemon is termed a
 * part and is stored in a linked list in case more than one part is
 * needed for a large directory.
 *
 * The position pointer (ctx->pos) encodes the part and offset on which
 * to begin reading at.  Bits above PART_SHIFT encode the part and bits
 * below PART_SHIFT encode the offset.  Parts are stored in a linked
 * list which grows as data is received from the server.  The overhead
 * associated with managing the list is presumed to be small compared to
 * the overhead of communicating with the server.
 *
 * As data is received from the server, it is placed at the end of the
 * part list.  Data is parsed from the current position as it is needed.
 * When data is determined to be corrupt, it is either because the
 * userspace component has sent back corrupt data or because the file
 * pointer has been moved to an invalid location.  Since the two cannot
 * be differentiated, return EIO.
 *
 * Part zero is synthesized to contains `.' and `..'.  Part one is the
 * first part of the part list.
 */

static int do_readdir(struct orangefs_inode_s *oi,
    struct orangefs_dir *od, struct dentry *dentry,
    struct orangefs_kernel_op_s *op)
{
	struct orangefs_readdir_response_s *resp;
	int bufi, r;

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
			od->error = r;
			return r;
		}
	}

	if (r < 0) {
		vfree(op->downcall.trailer_buf);
		od->error = r;
		return r;
	} else if (op->downcall.status) {
		vfree(op->downcall.trailer_buf);
		od->error = op->downcall.status;
		return op->downcall.status;
	}

	/*
	 * The maximum size is size per entry times the 512 entries plus
	 * the header.  This is well under the limit.
	 */
	if (op->downcall.trailer_size > PART_SIZE) {
		vfree(op->downcall.trailer_buf);
		od->error = -EIO;
		return -EIO;
	}

	resp = (struct orangefs_readdir_response_s *)
	    op->downcall.trailer_buf;
	od->token = resp->token;
	return 0;
}

static int parse_readdir(struct orangefs_dir *od,
    struct orangefs_kernel_op_s *op)
{
	struct orangefs_dir_part *part, *new;
	size_t count;

	count = 1;
	part = od->part;
	while (part) {
		count++;
		if (part->next)
			part = part->next;
		else
			break;
	}

	new = (void *)op->downcall.trailer_buf;
	new->next = NULL;
	new->len = op->downcall.trailer_size -
	    sizeof(struct orangefs_readdir_response_s);
	if (!od->part)
		od->part = new;
	else
		part->next = new;
	count++;
	od->end = count << PART_SHIFT;

	return 0;
}

static int orangefs_dir_more(struct orangefs_inode_s *oi,
    struct orangefs_dir *od, struct dentry *dentry)
{
	struct orangefs_kernel_op_s *op;
	int r;

	op = op_alloc(ORANGEFS_VFS_OP_READDIR);
	if (!op) {
		od->error = -ENOMEM;
		return -ENOMEM;
	}
	r = do_readdir(oi, od, dentry, op);
	if (r) {
		od->error = r;
		goto out;
	}
	r = parse_readdir(od, op);
	if (r) {
		od->error = r;
		goto out;
	}

	od->error = 0;
out:
	op_release(op);
	return od->error;
}

static int fill_from_part(struct orangefs_dir_part *part,
    struct dir_context *ctx)
{
	const int offset = sizeof(struct orangefs_readdir_response_s);
	struct orangefs_khandle *khandle;
	__u32 *len, padlen;
	loff_t i;
	char *s;
	i = ctx->pos & ~PART_MASK;

	/* The file offset from userspace is too large. */
	if (i > part->len)
		return 1;

	/*
	 * If the seek pointer is positioned just before an entry it
	 * should find the next entry.
	 */
	if (i % 8)
		i = i + (8 - i%8)%8;

	while (i < part->len) {
		if (part->len < i + sizeof *len)
			break;
		len = (void *)part + offset + i;
		/*
		 * len is the size of the string itself.  padlen is the
		 * total size of the encoded string.
		 */
		padlen = (sizeof *len + *len + 1) +
		    (8 - (sizeof *len + *len + 1)%8)%8;
		if (part->len < i + padlen + sizeof *khandle)
			goto next;
		s = (void *)part + offset + i + sizeof *len;
		if (s[*len] != 0)
			goto next;
		khandle = (void *)part + offset + i + padlen;
		if (!dir_emit(ctx, s, *len,
		    orangefs_khandle_to_ino(khandle),
		    DT_UNKNOWN))
			return 0;
		i += padlen + sizeof *khandle;
		i = i + (8 - i%8)%8;
		BUG_ON(i > part->len);
		ctx->pos = (ctx->pos & PART_MASK) | i;
		continue;
next:
		i += 8;
	}
	return 1;
}

static int orangefs_dir_fill(struct orangefs_inode_s *oi,
    struct orangefs_dir *od, struct dentry *dentry,
    struct dir_context *ctx)
{
	struct orangefs_dir_part *part;
	size_t count;

	count = ((ctx->pos & PART_MASK) >> PART_SHIFT) - 1;

	part = od->part;
	while (part->next && count) {
		count--;
		part = part->next;
	}
	/* This means the userspace file offset is invalid. */
	if (count) {
		od->error = -EIO;
		return -EIO;
	}

	while (part && part->len) {
		int r;
		r = fill_from_part(part, ctx);
		if (r < 0) {
			od->error = r;
			return r;
		} else if (r == 0) {
			/* Userspace buffer is full. */
			break;
		} else {
			/*
			 * The part ran out of data.  Move to the next
			 * part. */
			ctx->pos = (ctx->pos & PART_MASK) +
			    (1 << PART_SHIFT);
			part = part->next;
		}
	}
	return 0;
}

static loff_t orangefs_dir_llseek(struct file *file, loff_t offset,
    int whence)
{
	struct orangefs_dir *od = file->private_data;
	/*
	 * Delete the stored data so userspace sees new directory
	 * entries.
	 */
	if (!whence && offset < od->end) {
		struct orangefs_dir_part *part = od->part;
		while (part) {
			struct orangefs_dir_part *next = part->next;
			vfree(part);
			part = next;
		}
		od->token = ORANGEFS_ITERATE_START;
		od->part = NULL;
		od->end = 1 << PART_SHIFT;
	}
	return default_llseek(file, offset, whence);
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
		ctx->pos = 1 << PART_SHIFT;
	}

	/*
	 * The seek position is in the first synthesized part but is not
	 * valid.
	 */
	if ((ctx->pos & PART_MASK) == 0)
		return -EIO;

	r = 0;

	/*
	 * Must read more if the user has sought past what has been read
	 * so far.  Stop a user who has sought past the end.
	 */
	while (od->token != ORANGEFS_ITERATE_END &&
	    ctx->pos > od->end) {
		r = orangefs_dir_more(oi, od, dentry);
		if (r)
			return r;
	}
	if (od->token == ORANGEFS_ITERATE_END && ctx->pos > od->end)
		return -EIO;

	/* Then try to fill if there's any left in the buffer. */
	if (ctx->pos < od->end) {
		r = orangefs_dir_fill(oi, od, dentry, ctx);
		if (r)
			return r;
	}

	/* Finally get some more and try to fill. */
	if (od->token != ORANGEFS_ITERATE_END) {
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
	od->token = ORANGEFS_ITERATE_START;
	od->part = NULL;
	od->end = 1 << PART_SHIFT;
	od->error = 0;
	return 0;
}

static int orangefs_dir_release(struct inode *inode, struct file *file)
{
	struct orangefs_dir *od = file->private_data;
	struct orangefs_dir_part *part = od->part;
	orangefs_flush_inode(inode);
	while (part) {
		struct orangefs_dir_part *next = part->next;
		vfree(part);
		part = next;
	}
	kfree(od);
	return 0;
}

const struct file_operations orangefs_dir_operations = {
	.llseek = orangefs_dir_llseek,
	.read = generic_read_dir,
	.iterate = orangefs_dir_iterate,
	.open = orangefs_dir_open,
	.release = orangefs_dir_release
};
