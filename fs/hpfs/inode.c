/*
 *  linux/fs/hpfs/inode.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  inode VFS functions
 */

#include <linux/slab.h>
#include <linux/user_namespace.h>
#include "hpfs_fn.h"

void hpfs_init_inode(struct inode *i)
{
	struct super_block *sb = i->i_sb;
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);

	i->i_uid = hpfs_sb(sb)->sb_uid;
	i->i_gid = hpfs_sb(sb)->sb_gid;
	i->i_mode = hpfs_sb(sb)->sb_mode;
	i->i_size = -1;
	i->i_blocks = -1;
	
	hpfs_inode->i_dno = 0;
	hpfs_inode->i_n_secs = 0;
	hpfs_inode->i_file_sec = 0;
	hpfs_inode->i_disk_sec = 0;
	hpfs_inode->i_dpos = 0;
	hpfs_inode->i_dsubdno = 0;
	hpfs_inode->i_ea_mode = 0;
	hpfs_inode->i_ea_uid = 0;
	hpfs_inode->i_ea_gid = 0;
	hpfs_inode->i_ea_size = 0;

	hpfs_inode->i_rddir_off = NULL;
	hpfs_inode->i_dirty = 0;

	i->i_ctime.tv_sec = i->i_ctime.tv_nsec = 0;
	i->i_mtime.tv_sec = i->i_mtime.tv_nsec = 0;
	i->i_atime.tv_sec = i->i_atime.tv_nsec = 0;
}

void hpfs_read_inode(struct inode *i)
{
	struct buffer_head *bh;
	struct fnode *fnode;
	struct super_block *sb = i->i_sb;
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
	void *ea;
	int ea_size;

	if (!(fnode = hpfs_map_fnode(sb, i->i_ino, &bh))) {
		/*i->i_mode |= S_IFREG;
		i->i_mode &= ~0111;
		i->i_op = &hpfs_file_iops;
		i->i_fop = &hpfs_file_ops;
		clear_nlink(i);*/
		make_bad_inode(i);
		return;
	}
	if (hpfs_sb(i->i_sb)->sb_eas) {
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "UID", &ea_size))) {
			if (ea_size == 2) {
				i_uid_write(i, le16_to_cpu(*(__le16*)ea));
				hpfs_inode->i_ea_uid = 1;
			}
			kfree(ea);
		}
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "GID", &ea_size))) {
			if (ea_size == 2) {
				i_gid_write(i, le16_to_cpu(*(__le16*)ea));
				hpfs_inode->i_ea_gid = 1;
			}
			kfree(ea);
		}
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "SYMLINK", &ea_size))) {
			kfree(ea);
			i->i_mode = S_IFLNK | 0777;
			i->i_op = &page_symlink_inode_operations;
			i->i_data.a_ops = &hpfs_symlink_aops;
			set_nlink(i, 1);
			i->i_size = ea_size;
			i->i_blocks = 1;
			brelse(bh);
			return;
		}
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "MODE", &ea_size))) {
			int rdev = 0;
			umode_t mode = hpfs_sb(sb)->sb_mode;
			if (ea_size == 2) {
				mode = le16_to_cpu(*(__le16*)ea);
				hpfs_inode->i_ea_mode = 1;
			}
			kfree(ea);
			i->i_mode = mode;
			if (S_ISBLK(mode) || S_ISCHR(mode)) {
				if ((ea = hpfs_get_ea(i->i_sb, fnode, "DEV", &ea_size))) {
					if (ea_size == 4)
						rdev = le32_to_cpu(*(__le32*)ea);
					kfree(ea);
				}
			}
			if (S_ISBLK(mode) || S_ISCHR(mode) || S_ISFIFO(mode) || S_ISSOCK(mode)) {
				brelse(bh);
				set_nlink(i, 1);
				i->i_size = 0;
				i->i_blocks = 1;
				init_special_inode(i, mode,
					new_decode_dev(rdev));
				return;
			}
		}
	}
	if (fnode_is_dir(fnode)) {
		int n_dnodes, n_subdirs;
		i->i_mode |= S_IFDIR;
		i->i_op = &hpfs_dir_iops;
		i->i_fop = &hpfs_dir_ops;
		hpfs_inode->i_parent_dir = le32_to_cpu(fnode->up);
		hpfs_inode->i_dno = le32_to_cpu(fnode->u.external[0].disk_secno);
		if (hpfs_sb(sb)->sb_chk >= 2) {
			struct buffer_head *bh0;
			if (hpfs_map_fnode(sb, hpfs_inode->i_parent_dir, &bh0)) brelse(bh0);
		}
		n_dnodes = 0; n_subdirs = 0;
		hpfs_count_dnodes(i->i_sb, hpfs_inode->i_dno, &n_dnodes, &n_subdirs, NULL);
		i->i_blocks = 4 * n_dnodes;
		i->i_size = 2048 * n_dnodes;
		set_nlink(i, 2 + n_subdirs);
	} else {
		i->i_mode |= S_IFREG;
		if (!hpfs_inode->i_ea_mode) i->i_mode &= ~0111;
		i->i_op = &hpfs_file_iops;
		i->i_fop = &hpfs_file_ops;
		set_nlink(i, 1);
		i->i_size = le32_to_cpu(fnode->file_size);
		i->i_blocks = ((i->i_size + 511) >> 9) + 1;
		i->i_data.a_ops = &hpfs_aops;
		hpfs_i(i)->mmu_private = i->i_size;
	}
	brelse(bh);
}

static void hpfs_write_inode_ea(struct inode *i, struct fnode *fnode)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
	/*if (le32_to_cpu(fnode->acl_size_l) || le16_to_cpu(fnode->acl_size_s)) {
		   Some unknown structures like ACL may be in fnode,
		   we'd better not overwrite them
		hpfs_error(i->i_sb, "fnode %08x has some unknown HPFS386 stuctures", i->i_ino);
	} else*/ if (hpfs_sb(i->i_sb)->sb_eas >= 2) {
		__le32 ea;
		if (!uid_eq(i->i_uid, hpfs_sb(i->i_sb)->sb_uid) || hpfs_inode->i_ea_uid) {
			ea = cpu_to_le32(i_uid_read(i));
			hpfs_set_ea(i, fnode, "UID", (char*)&ea, 2);
			hpfs_inode->i_ea_uid = 1;
		}
		if (!gid_eq(i->i_gid, hpfs_sb(i->i_sb)->sb_gid) || hpfs_inode->i_ea_gid) {
			ea = cpu_to_le32(i_gid_read(i));
			hpfs_set_ea(i, fnode, "GID", (char *)&ea, 2);
			hpfs_inode->i_ea_gid = 1;
		}
		if (!S_ISLNK(i->i_mode))
			if ((i->i_mode != ((hpfs_sb(i->i_sb)->sb_mode & ~(S_ISDIR(i->i_mode) ? 0 : 0111))
			  | (S_ISDIR(i->i_mode) ? S_IFDIR : S_IFREG))
			  && i->i_mode != ((hpfs_sb(i->i_sb)->sb_mode & ~(S_ISDIR(i->i_mode) ? 0222 : 0333))
			  | (S_ISDIR(i->i_mode) ? S_IFDIR : S_IFREG))) || hpfs_inode->i_ea_mode) {
				ea = cpu_to_le32(i->i_mode);
				/* sick, but legal */
				hpfs_set_ea(i, fnode, "MODE", (char *)&ea, 2);
				hpfs_inode->i_ea_mode = 1;
			}
		if (S_ISBLK(i->i_mode) || S_ISCHR(i->i_mode)) {
			ea = cpu_to_le32(new_encode_dev(i->i_rdev));
			hpfs_set_ea(i, fnode, "DEV", (char *)&ea, 4);
		}
	}
}

void hpfs_write_inode(struct inode *i)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
	struct inode *parent;
	if (i->i_ino == hpfs_sb(i->i_sb)->sb_root) return;
	if (hpfs_inode->i_rddir_off && !atomic_read(&i->i_count)) {
		if (*hpfs_inode->i_rddir_off) printk("HPFS: write_inode: some position still there\n");
		kfree(hpfs_inode->i_rddir_off);
		hpfs_inode->i_rddir_off = NULL;
	}
	if (!i->i_nlink) {
		return;
	}
	parent = iget_locked(i->i_sb, hpfs_inode->i_parent_dir);
	if (parent) {
		hpfs_inode->i_dirty = 0;
		if (parent->i_state & I_NEW) {
			hpfs_init_inode(parent);
			hpfs_read_inode(parent);
			unlock_new_inode(parent);
		}
		hpfs_write_inode_nolock(i);
		iput(parent);
	}
}

void hpfs_write_inode_nolock(struct inode *i)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
	struct buffer_head *bh;
	struct fnode *fnode;
	struct quad_buffer_head qbh;
	struct hpfs_dirent *de;
	if (i->i_ino == hpfs_sb(i->i_sb)->sb_root) return;
	if (!(fnode = hpfs_map_fnode(i->i_sb, i->i_ino, &bh))) return;
	if (i->i_ino != hpfs_sb(i->i_sb)->sb_root && i->i_nlink) {
		if (!(de = map_fnode_dirent(i->i_sb, i->i_ino, fnode, &qbh))) {
			brelse(bh);
			return;
		}
	} else de = NULL;
	if (S_ISREG(i->i_mode)) {
		fnode->file_size = cpu_to_le32(i->i_size);
		if (de) de->file_size = cpu_to_le32(i->i_size);
	} else if (S_ISDIR(i->i_mode)) {
		fnode->file_size = cpu_to_le32(0);
		if (de) de->file_size = cpu_to_le32(0);
	}
	hpfs_write_inode_ea(i, fnode);
	if (de) {
		de->write_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_mtime.tv_sec));
		de->read_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_atime.tv_sec));
		de->creation_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_ctime.tv_sec));
		de->read_only = !(i->i_mode & 0222);
		de->ea_size = cpu_to_le32(hpfs_inode->i_ea_size);
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
	}
	if (S_ISDIR(i->i_mode)) {
		if ((de = map_dirent(i, hpfs_inode->i_dno, "\001\001", 2, NULL, &qbh))) {
			de->write_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_mtime.tv_sec));
			de->read_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_atime.tv_sec));
			de->creation_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_ctime.tv_sec));
			de->read_only = !(i->i_mode & 0222);
			de->ea_size = cpu_to_le32(/*hpfs_inode->i_ea_size*/0);
			de->file_size = cpu_to_le32(0);
			hpfs_mark_4buffers_dirty(&qbh);
			hpfs_brelse4(&qbh);
		} else
			hpfs_error(i->i_sb,
				"directory %08lx doesn't have '.' entry",
				(unsigned long)i->i_ino);
	}
	mark_buffer_dirty(bh);
	brelse(bh);
}

int hpfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error = -EINVAL;

	hpfs_lock(inode->i_sb);
	if (inode->i_ino == hpfs_sb(inode->i_sb)->sb_root)
		goto out_unlock;
	if ((attr->ia_valid & ATTR_UID) &&
	    from_kuid(&init_user_ns, attr->ia_uid) >= 0x10000)
		goto out_unlock;
	if ((attr->ia_valid & ATTR_GID) &&
	    from_kgid(&init_user_ns, attr->ia_gid) >= 0x10000)
		goto out_unlock;
	if ((attr->ia_valid & ATTR_SIZE) && attr->ia_size > inode->i_size)
		goto out_unlock;

	error = inode_change_ok(inode, attr);
	if (error)
		goto out_unlock;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		error = vmtruncate(inode, attr->ia_size);
		if (error)
			goto out_unlock;
	}

	setattr_copy(inode, attr);

	hpfs_write_inode(inode);

 out_unlock:
	hpfs_unlock(inode->i_sb);
	return error;
}

void hpfs_write_if_changed(struct inode *inode)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(inode);

	if (hpfs_inode->i_dirty)
		hpfs_write_inode(inode);
}

void hpfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
	if (!inode->i_nlink) {
		hpfs_lock(inode->i_sb);
		hpfs_remove_fnode(inode->i_sb, inode->i_ino);
		hpfs_unlock(inode->i_sb);
	}
}
