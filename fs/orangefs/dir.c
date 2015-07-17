/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

struct readdir_handle_s {
	int buffer_index;
	struct pvfs2_readdir_response_s readdir_response;
	void *dents_buf;
};

/*
 * decode routine needed by kmod to make sense of the shared page for readdirs.
 */
static long decode_dirents(char *ptr, struct pvfs2_readdir_response_s *readdir)
{
	int i;
	struct pvfs2_readdir_response_s *rd =
		(struct pvfs2_readdir_response_s *) ptr;
	char *buf = ptr;
	char **pptr = &buf;

	readdir->token = rd->token;
	readdir->pvfs_dirent_outcount = rd->pvfs_dirent_outcount;
	readdir->dirent_array = kmalloc(readdir->pvfs_dirent_outcount *
					sizeof(*readdir->dirent_array),
					GFP_KERNEL);
	if (readdir->dirent_array == NULL)
		return -ENOMEM;
	*pptr += offsetof(struct pvfs2_readdir_response_s, dirent_array);
	for (i = 0; i < readdir->pvfs_dirent_outcount; i++) {
		dec_string(pptr, &readdir->dirent_array[i].d_name,
			   &readdir->dirent_array[i].d_length);
		readdir->dirent_array[i].khandle =
			*(struct pvfs2_khandle *) *pptr;
		*pptr += 16;
	}
	return (unsigned long)*pptr - (unsigned long)ptr;
}

static long readdir_handle_ctor(struct readdir_handle_s *rhandle, void *buf,
				int buffer_index)
{
	long ret;

	if (buf == NULL) {
		gossip_err
		    ("Invalid NULL buffer specified in readdir_handle_ctor\n");
		return -ENOMEM;
	}
	if (buffer_index < 0) {
		gossip_err
		    ("Invalid buffer index specified in readdir_handle_ctor\n");
		return -EINVAL;
	}
	rhandle->buffer_index = buffer_index;
	rhandle->dents_buf = buf;
	ret = decode_dirents(buf, &rhandle->readdir_response);
	if (ret < 0) {
		gossip_err("Could not decode readdir from buffer %ld\n", ret);
		rhandle->buffer_index = -1;
		gossip_debug(GOSSIP_DIR_DEBUG, "vfree %p\n", buf);
		vfree(buf);
		rhandle->dents_buf = NULL;
	}
	return ret;
}

static void readdir_handle_dtor(struct pvfs2_bufmap *bufmap,
		struct readdir_handle_s *rhandle)
{
	if (rhandle == NULL)
		return;

	/* kfree(NULL) is safe */
	kfree(rhandle->readdir_response.dirent_array);
	rhandle->readdir_response.dirent_array = NULL;

	if (rhandle->buffer_index >= 0) {
		readdir_index_put(bufmap, rhandle->buffer_index);
		rhandle->buffer_index = -1;
	}
	if (rhandle->dents_buf) {
		gossip_debug(GOSSIP_DIR_DEBUG, "vfree %p\n",
			     rhandle->dents_buf);
		vfree(rhandle->dents_buf);
		rhandle->dents_buf = NULL;
	}
}

/*
 * Read directory entries from an instance of an open directory.
 *
 * \note This routine was converted for the readdir to iterate change
 *       in "struct file_operations". "converted" mostly amounts to
 *       changing occurrences of "readdir" and "filldir" in the
 *       comments to "iterate" and "dir_emit". Also filldir calls
 *       were changed to dir_emit calls.
 *
 * \param dir_emit callback function called for each entry read.
 *
 * \retval <0 on error
 * \retval 0  when directory has been completely traversed
 * \retval >0 if we don't call dir_emit for all entries
 *
 * \note If the dir_emit call-back returns non-zero, then iterate should
 *       assume that it has had enough, and should return as well.
 */
static int pvfs2_readdir(struct file *file, struct dir_context *ctx)
{
	struct pvfs2_bufmap *bufmap = NULL;
	int ret = 0;
	int buffer_index;
	__u64 *ptoken = file->private_data;
	__u64 pos = 0;
	ino_t ino = 0;
	struct dentry *dentry = file->f_path.dentry;
	struct pvfs2_kernel_op_s *new_op = NULL;
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(dentry->d_inode);
	int buffer_full = 0;
	struct readdir_handle_s rhandle;
	int i = 0;
	int len = 0;
	ino_t current_ino = 0;
	char *current_entry = NULL;
	long bytes_decoded;

	gossip_ldebug(GOSSIP_DIR_DEBUG,
		      "%s: ctx->pos:%lld, token = %llu\n",
		      __func__,
		      lld(ctx->pos),
		      llu(*ptoken));

	pos = (__u64) ctx->pos;

	/* are we done? */
	if (pos == PVFS_READDIR_END) {
		gossip_debug(GOSSIP_DIR_DEBUG,
			     "Skipping to termination path\n");
		return 0;
	}

	gossip_debug(GOSSIP_DIR_DEBUG,
		     "pvfs2_readdir called on %s (pos=%llu)\n",
		     dentry->d_name.name, llu(pos));

	rhandle.buffer_index = -1;
	rhandle.dents_buf = NULL;
	memset(&rhandle.readdir_response, 0, sizeof(rhandle.readdir_response));

	new_op = op_alloc(PVFS2_VFS_OP_READDIR);
	if (!new_op)
		return -ENOMEM;

	new_op->uses_shared_memory = 1;
	new_op->upcall.req.readdir.refn = pvfs2_inode->refn;
	new_op->upcall.req.readdir.max_dirent_count = MAX_DIRENT_COUNT_READDIR;

	gossip_debug(GOSSIP_DIR_DEBUG,
		     "%s: upcall.req.readdir.refn.khandle: %pU\n",
		     __func__,
		     &new_op->upcall.req.readdir.refn.khandle);

	/*
	 * NOTE: the position we send to the readdir upcall is out of
	 * sync with ctx->pos since:
	 * 1. pvfs2 doesn't include the "." and ".." entries that are
	 *    added below.
	 * 2. the introduction of distributed directory logic makes token no
	 *    longer be related to f_pos and pos. Instead an independent
	 *    variable is used inside the function and stored in the
	 *    private_data of the file structure.
	 */
	new_op->upcall.req.readdir.token = *ptoken;

get_new_buffer_index:
	ret = readdir_index_get(&bufmap, &buffer_index);
	if (ret < 0) {
		gossip_lerr("pvfs2_readdir: readdir_index_get() failure (%d)\n",
			    ret);
		goto out_free_op;
	}
	new_op->upcall.req.readdir.buf_index = buffer_index;

	ret = service_operation(new_op,
				"pvfs2_readdir",
				get_interruptible_flag(dentry->d_inode));

	gossip_debug(GOSSIP_DIR_DEBUG,
		     "Readdir downcall status is %d.  ret:%d\n",
		     new_op->downcall.status,
		     ret);

	if (ret == -EAGAIN && op_state_purged(new_op)) {
		/*
		 * readdir shared memory aread has been wiped due to
		 * pvfs2-client-core restarting, so we must get a new
		 * index into the shared memory.
		 */
		gossip_debug(GOSSIP_DIR_DEBUG,
			"%s: Getting new buffer_index for retry of readdir..\n",
			 __func__);
		readdir_index_put(bufmap, buffer_index);
		goto get_new_buffer_index;
	}

	if (ret == -EIO && op_state_purged(new_op)) {
		gossip_err("%s: Client is down. Aborting readdir call.\n",
			__func__);
		readdir_index_put(bufmap, buffer_index);
		goto out_free_op;
	}

	if (ret < 0 || new_op->downcall.status != 0) {
		gossip_debug(GOSSIP_DIR_DEBUG,
			     "Readdir request failed.  Status:%d\n",
			     new_op->downcall.status);
		readdir_index_put(bufmap, buffer_index);
		if (ret >= 0)
			ret = new_op->downcall.status;
		goto out_free_op;
	}

	bytes_decoded =
		readdir_handle_ctor(&rhandle,
				    new_op->downcall.trailer_buf,
				    buffer_index);
	if (bytes_decoded < 0) {
		gossip_err("pvfs2_readdir: Could not decode trailer buffer into a readdir response %d\n",
			ret);
		ret = bytes_decoded;
		readdir_index_put(bufmap, buffer_index);
		goto out_free_op;
	}

	if (bytes_decoded != new_op->downcall.trailer_size) {
		gossip_err("pvfs2_readdir: # bytes decoded (%ld) != trailer size (%ld)\n",
			bytes_decoded,
			(long)new_op->downcall.trailer_size);
		ret = -EINVAL;
		goto out_destroy_handle;
	}

	if (pos == 0) {
		ino = get_ino_from_khandle(dentry->d_inode);
		gossip_debug(GOSSIP_DIR_DEBUG,
			     "%s: calling dir_emit of \".\" with pos = %llu\n",
			     __func__,
			     llu(pos));
		ret = dir_emit(ctx, ".", 1, ino, DT_DIR);
		if (ret < 0)
			goto out_destroy_handle;
		ctx->pos++;
		gossip_ldebug(GOSSIP_DIR_DEBUG,
			      "%s: ctx->pos:%lld\n",
			      __func__,
			      lld(ctx->pos));
		pos++;
	}

	if (pos == 1) {
		ino = get_parent_ino_from_dentry(dentry);
		gossip_debug(GOSSIP_DIR_DEBUG,
			     "%s: calling dir_emit of \"..\" with pos = %llu\n",
			     __func__,
			     llu(pos));
		ret = dir_emit(ctx, "..", 2, ino, DT_DIR);
		if (ret < 0)
			goto out_destroy_handle;
		ctx->pos++;
		gossip_ldebug(GOSSIP_DIR_DEBUG,
			      "%s: ctx->pos:%lld\n",
			      __func__,
			      lld(ctx->pos));
		pos++;
	}

	for (i = 0; i < rhandle.readdir_response.pvfs_dirent_outcount; i++) {
		len = rhandle.readdir_response.dirent_array[i].d_length;
		current_entry = rhandle.readdir_response.dirent_array[i].d_name;
		current_ino = pvfs2_khandle_to_ino(
			&(rhandle.readdir_response.dirent_array[i].khandle));

		gossip_debug(GOSSIP_DIR_DEBUG,
			     "calling dir_emit for %s with len %d, pos %ld\n",
			     current_entry,
			     len,
			     (unsigned long)pos);
		ret =
		    dir_emit(ctx, current_entry, len, current_ino, DT_UNKNOWN);
		if (ret < 0) {
			gossip_debug(GOSSIP_DIR_DEBUG,
				     "dir_emit() failed. ret:%d\n",
				     ret);
			if (i < 2) {
				gossip_err("dir_emit failed on one of the first two true PVFS directory entries.\n");
				gossip_err("Duplicate entries may appear.\n");
			}
			buffer_full = 1;
			break;
		}
		ctx->pos++;
		gossip_ldebug(GOSSIP_DIR_DEBUG,
			      "%s: ctx->pos:%lld\n",
			      __func__,
			      lld(ctx->pos));

		pos++;
	}

	/* this means that all of the dir_emit calls succeeded */
	if (i == rhandle.readdir_response.pvfs_dirent_outcount) {
		/* update token */
		*ptoken = rhandle.readdir_response.token;
	} else {
		/* this means a dir_emit call failed */
		if (rhandle.readdir_response.token == PVFS_READDIR_END) {
			/*
			 * If PVFS hit end of directory, then there
			 * is no way to do math on the token that it
			 * returned. Instead we go by ctx->pos but
			 * back up to account for the artificial .
			 * and .. entries.
			 */
			ctx->pos -= 3;
		} else {
			/*
			 * this means a dir_emit call failed. !!! need to set
			 * back to previous ctx->pos, no middle value allowed
			 */
			pos -= (i - 1);
			ctx->pos -= (i - 1);
		}
		gossip_debug(GOSSIP_DIR_DEBUG,
			"at least one dir_emit call failed. Setting ctx->pos to: %lld\n",
			lld(ctx->pos));
	}

	/*
	 * Did we hit the end of the directory?
	 */
	if (rhandle.readdir_response.token == PVFS_READDIR_END &&
	    !buffer_full) {
		gossip_debug(GOSSIP_DIR_DEBUG, "End of dir detected; setting ctx->pos to PVFS_READDIR_END.\n");
		ctx->pos = PVFS_READDIR_END;
	}

	gossip_debug(GOSSIP_DIR_DEBUG,
		     "pos = %llu, token = %llu"
		     ", ctx->pos should have been %lld\n",
		     llu(pos),
		     llu(*ptoken),
		     lld(ctx->pos));

out_destroy_handle:
	readdir_handle_dtor(bufmap, &rhandle);
out_free_op:
	op_release(new_op);
	gossip_debug(GOSSIP_DIR_DEBUG, "pvfs2_readdir returning %d\n", ret);
	return ret;
}

static int pvfs2_dir_open(struct inode *inode, struct file *file)
{
	__u64 *ptoken;

	file->private_data = kmalloc(sizeof(__u64), GFP_KERNEL);
	if (!file->private_data)
		return -ENOMEM;

	ptoken = file->private_data;
	*ptoken = PVFS_READDIR_START;
	return 0;
}

static int pvfs2_dir_release(struct inode *inode, struct file *file)
{
	pvfs2_flush_inode(inode);
	kfree(file->private_data);
	return 0;
}

/** PVFS2 implementation of VFS directory operations */
const struct file_operations pvfs2_dir_operations = {
	.read = generic_read_dir,
	.iterate = pvfs2_readdir,
	.open = pvfs2_dir_open,
	.release = pvfs2_dir_release,
};
