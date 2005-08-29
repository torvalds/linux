/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/reiserfs_fs.h>
#include <linux/stat.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <asm/uaccess.h>

extern struct reiserfs_key MIN_KEY;

static int reiserfs_readdir(struct file *, void *, filldir_t);
static int reiserfs_dir_fsync(struct file *filp, struct dentry *dentry,
			      int datasync);

struct file_operations reiserfs_dir_operations = {
	.read = generic_read_dir,
	.readdir = reiserfs_readdir,
	.fsync = reiserfs_dir_fsync,
	.ioctl = reiserfs_ioctl,
};

static int reiserfs_dir_fsync(struct file *filp, struct dentry *dentry,
			      int datasync)
{
	struct inode *inode = dentry->d_inode;
	int err;
	reiserfs_write_lock(inode->i_sb);
	err = reiserfs_commit_for_inode(inode);
	reiserfs_write_unlock(inode->i_sb);
	if (err < 0)
		return err;
	return 0;
}

#define store_ih(where,what) copy_item_head (where, what)

//
static int reiserfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct cpu_key pos_key;	/* key of current position in the directory (key of directory entry) */
	INITIALIZE_PATH(path_to_entry);
	struct buffer_head *bh;
	int item_num, entry_num;
	const struct reiserfs_key *rkey;
	struct item_head *ih, tmp_ih;
	int search_res;
	char *local_buf;
	loff_t next_pos;
	char small_buf[32];	/* avoid kmalloc if we can */
	struct reiserfs_dir_entry de;
	int ret = 0;

	reiserfs_write_lock(inode->i_sb);

	reiserfs_check_lock_depth(inode->i_sb, "readdir");

	/* form key for search the next directory entry using f_pos field of
	   file structure */
	make_cpu_key(&pos_key, inode,
		     (filp->f_pos) ? (filp->f_pos) : DOT_OFFSET, TYPE_DIRENTRY,
		     3);
	next_pos = cpu_key_k_offset(&pos_key);

	/*  reiserfs_warning (inode->i_sb, "reiserfs_readdir 1: f_pos = %Ld", filp->f_pos); */

	path_to_entry.reada = PATH_READA;
	while (1) {
	      research:
		/* search the directory item, containing entry with specified key */
		search_res =
		    search_by_entry_key(inode->i_sb, &pos_key, &path_to_entry,
					&de);
		if (search_res == IO_ERROR) {
			// FIXME: we could just skip part of directory which could
			// not be read
			ret = -EIO;
			goto out;
		}
		entry_num = de.de_entry_num;
		bh = de.de_bh;
		item_num = de.de_item_num;
		ih = de.de_ih;
		store_ih(&tmp_ih, ih);

		/* we must have found item, that is item of this directory, */
		RFALSE(COMP_SHORT_KEYS(&(ih->ih_key), &pos_key),
		       "vs-9000: found item %h does not match to dir we readdir %K",
		       ih, &pos_key);
		RFALSE(item_num > B_NR_ITEMS(bh) - 1,
		       "vs-9005 item_num == %d, item amount == %d",
		       item_num, B_NR_ITEMS(bh));

		/* and entry must be not more than number of entries in the item */
		RFALSE(I_ENTRY_COUNT(ih) < entry_num,
		       "vs-9010: entry number is too big %d (%d)",
		       entry_num, I_ENTRY_COUNT(ih));

		if (search_res == POSITION_FOUND
		    || entry_num < I_ENTRY_COUNT(ih)) {
			/* go through all entries in the directory item beginning from the entry, that has been found */
			struct reiserfs_de_head *deh =
			    B_I_DEH(bh, ih) + entry_num;

			for (; entry_num < I_ENTRY_COUNT(ih);
			     entry_num++, deh++) {
				int d_reclen;
				char *d_name;
				off_t d_off;
				ino_t d_ino;

				if (!de_visible(deh))
					/* it is hidden entry */
					continue;
				d_reclen = entry_length(bh, ih, entry_num);
				d_name = B_I_DEH_ENTRY_FILE_NAME(bh, ih, deh);
				if (!d_name[d_reclen - 1])
					d_reclen = strlen(d_name);

				if (d_reclen >
				    REISERFS_MAX_NAME(inode->i_sb->
						      s_blocksize)) {
					/* too big to send back to VFS */
					continue;
				}

				/* Ignore the .reiserfs_priv entry */
				if (reiserfs_xattrs(inode->i_sb) &&
				    !old_format_only(inode->i_sb) &&
				    filp->f_dentry == inode->i_sb->s_root &&
				    REISERFS_SB(inode->i_sb)->priv_root &&
				    REISERFS_SB(inode->i_sb)->priv_root->d_inode
				    && deh_objectid(deh) ==
				    le32_to_cpu(INODE_PKEY
						(REISERFS_SB(inode->i_sb)->
						 priv_root->d_inode)->
						k_objectid)) {
					continue;
				}

				d_off = deh_offset(deh);
				filp->f_pos = d_off;
				d_ino = deh_objectid(deh);
				if (d_reclen <= 32) {
					local_buf = small_buf;
				} else {
					local_buf =
					    reiserfs_kmalloc(d_reclen, GFP_NOFS,
							     inode->i_sb);
					if (!local_buf) {
						pathrelse(&path_to_entry);
						ret = -ENOMEM;
						goto out;
					}
					if (item_moved(&tmp_ih, &path_to_entry)) {
						reiserfs_kfree(local_buf,
							       d_reclen,
							       inode->i_sb);
						goto research;
					}
				}
				// Note, that we copy name to user space via temporary
				// buffer (local_buf) because filldir will block if
				// user space buffer is swapped out. At that time
				// entry can move to somewhere else
				memcpy(local_buf, d_name, d_reclen);
				if (filldir
				    (dirent, local_buf, d_reclen, d_off, d_ino,
				     DT_UNKNOWN) < 0) {
					if (local_buf != small_buf) {
						reiserfs_kfree(local_buf,
							       d_reclen,
							       inode->i_sb);
					}
					goto end;
				}
				if (local_buf != small_buf) {
					reiserfs_kfree(local_buf, d_reclen,
						       inode->i_sb);
				}
				// next entry should be looked for with such offset
				next_pos = deh_offset(deh) + 1;

				if (item_moved(&tmp_ih, &path_to_entry)) {
					goto research;
				}
			}	/* for */
		}

		if (item_num != B_NR_ITEMS(bh) - 1)
			// end of directory has been reached
			goto end;

		/* item we went through is last item of node. Using right
		   delimiting key check is it directory end */
		rkey = get_rkey(&path_to_entry, inode->i_sb);
		if (!comp_le_keys(rkey, &MIN_KEY)) {
			/* set pos_key to key, that is the smallest and greater
			   that key of the last entry in the item */
			set_cpu_key_k_offset(&pos_key, next_pos);
			continue;
		}

		if (COMP_SHORT_KEYS(rkey, &pos_key)) {
			// end of directory has been reached
			goto end;
		}

		/* directory continues in the right neighboring block */
		set_cpu_key_k_offset(&pos_key,
				     le_key_k_offset(KEY_FORMAT_3_5, rkey));

	}			/* while */

      end:
	filp->f_pos = next_pos;
	pathrelse(&path_to_entry);
	reiserfs_check_path(&path_to_entry);
      out:
	reiserfs_write_unlock(inode->i_sb);
	return ret;
}

/* compose directory item containing "." and ".." entries (entries are
   not aligned to 4 byte boundary) */
/* the last four params are LE */
void make_empty_dir_item_v1(char *body, __le32 dirid, __le32 objid,
			    __le32 par_dirid, __le32 par_objid)
{
	struct reiserfs_de_head *deh;

	memset(body, 0, EMPTY_DIR_SIZE_V1);
	deh = (struct reiserfs_de_head *)body;

	/* direntry header of "." */
	put_deh_offset(&(deh[0]), DOT_OFFSET);
	/* these two are from make_le_item_head, and are are LE */
	deh[0].deh_dir_id = dirid;
	deh[0].deh_objectid = objid;
	deh[0].deh_state = 0;	/* Endian safe if 0 */
	put_deh_location(&(deh[0]), EMPTY_DIR_SIZE_V1 - strlen("."));
	mark_de_visible(&(deh[0]));

	/* direntry header of ".." */
	put_deh_offset(&(deh[1]), DOT_DOT_OFFSET);
	/* key of ".." for the root directory */
	/* these two are from the inode, and are are LE */
	deh[1].deh_dir_id = par_dirid;
	deh[1].deh_objectid = par_objid;
	deh[1].deh_state = 0;	/* Endian safe if 0 */
	put_deh_location(&(deh[1]), deh_location(&(deh[0])) - strlen(".."));
	mark_de_visible(&(deh[1]));

	/* copy ".." and "." */
	memcpy(body + deh_location(&(deh[0])), ".", 1);
	memcpy(body + deh_location(&(deh[1])), "..", 2);
}

/* compose directory item containing "." and ".." entries */
void make_empty_dir_item(char *body, __le32 dirid, __le32 objid,
			 __le32 par_dirid, __le32 par_objid)
{
	struct reiserfs_de_head *deh;

	memset(body, 0, EMPTY_DIR_SIZE);
	deh = (struct reiserfs_de_head *)body;

	/* direntry header of "." */
	put_deh_offset(&(deh[0]), DOT_OFFSET);
	/* these two are from make_le_item_head, and are are LE */
	deh[0].deh_dir_id = dirid;
	deh[0].deh_objectid = objid;
	deh[0].deh_state = 0;	/* Endian safe if 0 */
	put_deh_location(&(deh[0]), EMPTY_DIR_SIZE - ROUND_UP(strlen(".")));
	mark_de_visible(&(deh[0]));

	/* direntry header of ".." */
	put_deh_offset(&(deh[1]), DOT_DOT_OFFSET);
	/* key of ".." for the root directory */
	/* these two are from the inode, and are are LE */
	deh[1].deh_dir_id = par_dirid;
	deh[1].deh_objectid = par_objid;
	deh[1].deh_state = 0;	/* Endian safe if 0 */
	put_deh_location(&(deh[1]),
			 deh_location(&(deh[0])) - ROUND_UP(strlen("..")));
	mark_de_visible(&(deh[1]));

	/* copy ".." and "." */
	memcpy(body + deh_location(&(deh[0])), ".", 1);
	memcpy(body + deh_location(&(deh[1])), "..", 2);
}
