/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"

/*
 * decode routine used by kmod to deal with the blob sent from
 * userspace for readdirs. The blob contains zero or more of these
 * sub-blobs:
 *   __u32 - represents length of the character string that follows.
 *   string - between 1 and ORANGEFS_NAME_MAX bytes long.
 *   padding - (if needed) to cause the __u32 plus the string to be
 *             eight byte aligned.
 *   khandle - sizeof(khandle) bytes.
 */
static long decode_dirents(char *ptr, size_t size,
                           struct orangefs_readdir_response_s *readdir)
{
	int i;
	struct orangefs_readdir_response_s *rd =
		(struct orangefs_readdir_response_s *) ptr;
	char *buf = ptr;
	int khandle_size = sizeof(struct orangefs_khandle);
	size_t offset = offsetof(struct orangefs_readdir_response_s,
				dirent_array);
	/* 8 reflects eight byte alignment */
	int smallest_blob = khandle_size + 8;
	__u32 len;
	int aligned_len;
	int sizeof_u32 = sizeof(__u32);
	long ret;

	gossip_debug(GOSSIP_DIR_DEBUG, "%s: size:%zu:\n", __func__, size);

	/* size is = offset on empty dirs, > offset on non-empty dirs... */
	if (size < offset) {
		gossip_err("%s: size:%zu: offset:%zu:\n",
			   __func__,
			   size,
			   offset);
		ret = -EINVAL;
		goto out;
	}

        if ((size == offset) && (readdir->orangefs_dirent_outcount != 0)) {
		gossip_err("%s: size:%zu: dirent_outcount:%d:\n",
			   __func__,
			   size,
			   readdir->orangefs_dirent_outcount);
		ret = -EINVAL;
		goto out;
	}

	readdir->token = rd->token;
	readdir->orangefs_dirent_outcount = rd->orangefs_dirent_outcount;
	readdir->dirent_array = kcalloc(readdir->orangefs_dirent_outcount,
					sizeof(*readdir->dirent_array),
					GFP_KERNEL);
	if (readdir->dirent_array == NULL) {
		gossip_err("%s: kcalloc failed.\n", __func__);
		ret = -ENOMEM;
		goto out;
	}

	buf += offset;
	size -= offset;

	for (i = 0; i < readdir->orangefs_dirent_outcount; i++) {
		if (size < smallest_blob) {
			gossip_err("%s: size:%zu: smallest_blob:%d:\n",
				   __func__,
				   size,
				   smallest_blob);
			ret = -EINVAL;
			goto free;
		}

		len = *(__u32 *)buf;
		if ((len < 1) || (len > ORANGEFS_NAME_MAX)) {
			gossip_err("%s: len:%d:\n", __func__, len);
			ret = -EINVAL;
			goto free;
		}

		gossip_debug(GOSSIP_DIR_DEBUG,
			     "%s: size:%zu: len:%d:\n",
			     __func__,
			     size,
			     len);

		readdir->dirent_array[i].d_name = buf + sizeof_u32;
		readdir->dirent_array[i].d_length = len;

		/*
		 * Calculate "aligned" length of this string and its
		 * associated __u32 descriptor.
		 */
		aligned_len = ((sizeof_u32 + len + 1) + 7) & ~7;
		gossip_debug(GOSSIP_DIR_DEBUG,
			     "%s: aligned_len:%d:\n",
			     __func__,
			     aligned_len);

		/*
		 * The end of the blob should coincide with the end
		 * of the last sub-blob.
		 */
		if (size < aligned_len + khandle_size) {
			gossip_err("%s: ran off the end of the blob.\n",
				   __func__);
			ret = -EINVAL;
			goto free;
		}
		size -= aligned_len + khandle_size;

		buf += aligned_len;

		readdir->dirent_array[i].khandle =
			*(struct orangefs_khandle *) buf;
		buf += khandle_size;
	}
	ret = buf - ptr;
	gossip_debug(GOSSIP_DIR_DEBUG, "%s: returning:%ld:\n", __func__, ret);
	goto out;

free:
	kfree(readdir->dirent_array);
	readdir->dirent_array = NULL;

out:
	return ret;
}

/*
 * Read directory entries from an instance of an open directory.
 */
static int orangefs_readdir(struct file *file, struct dir_context *ctx)
{
	int ret = 0;
	int buffer_index;
	/*
	 * ptoken supports Orangefs' distributed directory logic, added
	 * in 2.9.2.
	 */
	__u64 *ptoken = file->private_data;
	__u64 pos = 0;
	ino_t ino = 0;
	struct dentry *dentry = file->f_path.dentry;
	struct orangefs_kernel_op_s *new_op = NULL;
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(dentry->d_inode);
	int buffer_full = 0;
	struct orangefs_readdir_response_s readdir_response;
	void *dents_buf;
	int i = 0;
	int len = 0;
	ino_t current_ino = 0;
	char *current_entry = NULL;
	long bytes_decoded;

	gossip_debug(GOSSIP_DIR_DEBUG,
		     "%s: ctx->pos:%lld, ptoken = %llu\n",
		     __func__,
		     lld(ctx->pos),
		     llu(*ptoken));

	pos = (__u64) ctx->pos;

	/* are we done? */
	if (pos == ORANGEFS_READDIR_END) {
		gossip_debug(GOSSIP_DIR_DEBUG,
			     "Skipping to termination path\n");
		return 0;
	}

	gossip_debug(GOSSIP_DIR_DEBUG,
		     "orangefs_readdir called on %s (pos=%llu)\n",
		     dentry->d_name.name, llu(pos));

	memset(&readdir_response, 0, sizeof(readdir_response));

	new_op = op_alloc(ORANGEFS_VFS_OP_READDIR);
	if (!new_op)
		return -ENOMEM;

	/*
	 * Only the indices are shared. No memory is actually shared, but the
	 * mechanism is used.
	 */
	new_op->uses_shared_memory = 1;
	new_op->upcall.req.readdir.refn = orangefs_inode->refn;
	new_op->upcall.req.readdir.max_dirent_count =
	    ORANGEFS_MAX_DIRENT_COUNT_READDIR;

	gossip_debug(GOSSIP_DIR_DEBUG,
		     "%s: upcall.req.readdir.refn.khandle: %pU\n",
		     __func__,
		     &new_op->upcall.req.readdir.refn.khandle);

	new_op->upcall.req.readdir.token = *ptoken;

get_new_buffer_index:
	buffer_index = orangefs_readdir_index_get();
	if (buffer_index < 0) {
		ret = buffer_index;
		gossip_lerr("orangefs_readdir: orangefs_readdir_index_get() failure (%d)\n",
			    ret);
		goto out_free_op;
	}
	new_op->upcall.req.readdir.buf_index = buffer_index;

	ret = service_operation(new_op,
				"orangefs_readdir",
				get_interruptible_flag(dentry->d_inode));

	gossip_debug(GOSSIP_DIR_DEBUG,
		     "Readdir downcall status is %d.  ret:%d\n",
		     new_op->downcall.status,
		     ret);

	orangefs_readdir_index_put(buffer_index);

	if (ret == -EAGAIN && op_state_purged(new_op)) {
		/* Client-core indices are invalid after it restarted. */
		gossip_debug(GOSSIP_DIR_DEBUG,
			"%s: Getting new buffer_index for retry of readdir..\n",
			 __func__);
		goto get_new_buffer_index;
	}

	if (ret == -EIO && op_state_purged(new_op)) {
		gossip_err("%s: Client is down. Aborting readdir call.\n",
			__func__);
		goto out_free_op;
	}

	if (ret < 0 || new_op->downcall.status != 0) {
		gossip_debug(GOSSIP_DIR_DEBUG,
			     "Readdir request failed.  Status:%d\n",
			     new_op->downcall.status);
		if (ret >= 0)
			ret = new_op->downcall.status;
		goto out_free_op;
	}

	dents_buf = new_op->downcall.trailer_buf;
	if (dents_buf == NULL) {
		gossip_err("Invalid NULL buffer in readdir response\n");
		ret = -ENOMEM;
		goto out_free_op;
	}

	bytes_decoded = decode_dirents(dents_buf, new_op->downcall.trailer_size,
					&readdir_response);
	if (bytes_decoded < 0) {
		ret = bytes_decoded;
		gossip_err("Could not decode readdir from buffer %d\n", ret);
		goto out_vfree;
	}

	if (bytes_decoded != new_op->downcall.trailer_size) {
		gossip_err("orangefs_readdir: # bytes decoded (%ld) "
			   "!= trailer size (%ld)\n",
			   bytes_decoded,
			   (long)new_op->downcall.trailer_size);
		ret = -EINVAL;
		goto out_destroy_handle;
	}

	/*
	 *  orangefs doesn't actually store dot and dot-dot, but
	 *  we need to have them represented.
	 */
	if (pos == 0) {
		ino = get_ino_from_khandle(dentry->d_inode);
		gossip_debug(GOSSIP_DIR_DEBUG,
			     "%s: calling dir_emit of \".\" with pos = %llu\n",
			     __func__,
			     llu(pos));
		ret = dir_emit(ctx, ".", 1, ino, DT_DIR);
		pos += 1;
	}

	if (pos == 1) {
		ino = get_parent_ino_from_dentry(dentry);
		gossip_debug(GOSSIP_DIR_DEBUG,
			     "%s: calling dir_emit of \"..\" with pos = %llu\n",
			     __func__,
			     llu(pos));
		ret = dir_emit(ctx, "..", 2, ino, DT_DIR);
		pos += 1;
	}

	/*
	 * we stored ORANGEFS_ITERATE_NEXT in ctx->pos last time around
	 * to prevent "finding" dot and dot-dot on any iteration
	 * other than the first.
	 */
	if (ctx->pos == ORANGEFS_ITERATE_NEXT)
		ctx->pos = 0;

	gossip_debug(GOSSIP_DIR_DEBUG,
		     "%s: dirent_outcount:%d:\n",
		     __func__,
		     readdir_response.orangefs_dirent_outcount);
	for (i = ctx->pos;
	     i < readdir_response.orangefs_dirent_outcount;
	     i++) {
		len = readdir_response.dirent_array[i].d_length;
		current_entry = readdir_response.dirent_array[i].d_name;
		current_ino = orangefs_khandle_to_ino(
			&readdir_response.dirent_array[i].khandle);

		gossip_debug(GOSSIP_DIR_DEBUG,
			     "calling dir_emit for %s with len %d"
			     ", ctx->pos %ld\n",
			     current_entry,
			     len,
			     (unsigned long)ctx->pos);
		/*
		 * type is unknown. We don't return object type
		 * in the dirent_array. This leaves getdents
		 * clueless about type.
		 */
		ret =
		    dir_emit(ctx, current_entry, len, current_ino, DT_UNKNOWN);
		if (!ret)
			break;
		ctx->pos++;
		gossip_debug(GOSSIP_DIR_DEBUG,
			      "%s: ctx->pos:%lld\n",
			      __func__,
			      lld(ctx->pos));

	}

	/*
	 * we ran all the way through the last batch, set up for
	 * getting another batch...
	 */
	if (ret) {
		*ptoken = readdir_response.token;
		ctx->pos = ORANGEFS_ITERATE_NEXT;
	}

	/*
	 * Did we hit the end of the directory?
	 */
	if (readdir_response.token == ORANGEFS_READDIR_END &&
	    !buffer_full) {
		gossip_debug(GOSSIP_DIR_DEBUG,
		"End of dir detected; setting ctx->pos to ORANGEFS_READDIR_END.\n");
		ctx->pos = ORANGEFS_READDIR_END;
	}

out_destroy_handle:
	/* kfree(NULL) is safe */
	kfree(readdir_response.dirent_array);
out_vfree:
	gossip_debug(GOSSIP_DIR_DEBUG, "vfree %p\n", dents_buf);
	vfree(dents_buf);
out_free_op:
	op_release(new_op);
	gossip_debug(GOSSIP_DIR_DEBUG, "orangefs_readdir returning %d\n", ret);
	return ret;
}

static int orangefs_dir_open(struct inode *inode, struct file *file)
{
	__u64 *ptoken;

	file->private_data = kmalloc(sizeof(__u64), GFP_KERNEL);
	if (!file->private_data)
		return -ENOMEM;

	ptoken = file->private_data;
	*ptoken = ORANGEFS_READDIR_START;
	return 0;
}

static int orangefs_dir_release(struct inode *inode, struct file *file)
{
	orangefs_flush_inode(inode);
	kfree(file->private_data);
	return 0;
}

/** ORANGEFS implementation of VFS directory operations */
const struct file_operations orangefs_dir_operations = {
	.read = generic_read_dir,
	.iterate = orangefs_readdir,
	.open = orangefs_dir_open,
	.release = orangefs_dir_release,
};
