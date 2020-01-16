// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hpfs/iyesde.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  iyesde VFS functions
 */

#include <linux/slab.h>
#include <linux/user_namespace.h>
#include "hpfs_fn.h"

void hpfs_init_iyesde(struct iyesde *i)
{
	struct super_block *sb = i->i_sb;
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(i);

	i->i_uid = hpfs_sb(sb)->sb_uid;
	i->i_gid = hpfs_sb(sb)->sb_gid;
	i->i_mode = hpfs_sb(sb)->sb_mode;
	i->i_size = -1;
	i->i_blocks = -1;
	
	hpfs_iyesde->i_dyes = 0;
	hpfs_iyesde->i_n_secs = 0;
	hpfs_iyesde->i_file_sec = 0;
	hpfs_iyesde->i_disk_sec = 0;
	hpfs_iyesde->i_dpos = 0;
	hpfs_iyesde->i_dsubdyes = 0;
	hpfs_iyesde->i_ea_mode = 0;
	hpfs_iyesde->i_ea_uid = 0;
	hpfs_iyesde->i_ea_gid = 0;
	hpfs_iyesde->i_ea_size = 0;

	hpfs_iyesde->i_rddir_off = NULL;
	hpfs_iyesde->i_dirty = 0;

	i->i_ctime.tv_sec = i->i_ctime.tv_nsec = 0;
	i->i_mtime.tv_sec = i->i_mtime.tv_nsec = 0;
	i->i_atime.tv_sec = i->i_atime.tv_nsec = 0;
}

void hpfs_read_iyesde(struct iyesde *i)
{
	struct buffer_head *bh;
	struct fyesde *fyesde;
	struct super_block *sb = i->i_sb;
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(i);
	void *ea;
	int ea_size;

	if (!(fyesde = hpfs_map_fyesde(sb, i->i_iyes, &bh))) {
		/*i->i_mode |= S_IFREG;
		i->i_mode &= ~0111;
		i->i_op = &hpfs_file_iops;
		i->i_fop = &hpfs_file_ops;
		clear_nlink(i);*/
		make_bad_iyesde(i);
		return;
	}
	if (hpfs_sb(i->i_sb)->sb_eas) {
		if ((ea = hpfs_get_ea(i->i_sb, fyesde, "UID", &ea_size))) {
			if (ea_size == 2) {
				i_uid_write(i, le16_to_cpu(*(__le16*)ea));
				hpfs_iyesde->i_ea_uid = 1;
			}
			kfree(ea);
		}
		if ((ea = hpfs_get_ea(i->i_sb, fyesde, "GID", &ea_size))) {
			if (ea_size == 2) {
				i_gid_write(i, le16_to_cpu(*(__le16*)ea));
				hpfs_iyesde->i_ea_gid = 1;
			}
			kfree(ea);
		}
		if ((ea = hpfs_get_ea(i->i_sb, fyesde, "SYMLINK", &ea_size))) {
			kfree(ea);
			i->i_mode = S_IFLNK | 0777;
			i->i_op = &page_symlink_iyesde_operations;
			iyesde_yeshighmem(i);
			i->i_data.a_ops = &hpfs_symlink_aops;
			set_nlink(i, 1);
			i->i_size = ea_size;
			i->i_blocks = 1;
			brelse(bh);
			return;
		}
		if ((ea = hpfs_get_ea(i->i_sb, fyesde, "MODE", &ea_size))) {
			int rdev = 0;
			umode_t mode = hpfs_sb(sb)->sb_mode;
			if (ea_size == 2) {
				mode = le16_to_cpu(*(__le16*)ea);
				hpfs_iyesde->i_ea_mode = 1;
			}
			kfree(ea);
			i->i_mode = mode;
			if (S_ISBLK(mode) || S_ISCHR(mode)) {
				if ((ea = hpfs_get_ea(i->i_sb, fyesde, "DEV", &ea_size))) {
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
				init_special_iyesde(i, mode,
					new_decode_dev(rdev));
				return;
			}
		}
	}
	if (fyesde_is_dir(fyesde)) {
		int n_dyesdes, n_subdirs;
		i->i_mode |= S_IFDIR;
		i->i_op = &hpfs_dir_iops;
		i->i_fop = &hpfs_dir_ops;
		hpfs_iyesde->i_parent_dir = le32_to_cpu(fyesde->up);
		hpfs_iyesde->i_dyes = le32_to_cpu(fyesde->u.external[0].disk_secyes);
		if (hpfs_sb(sb)->sb_chk >= 2) {
			struct buffer_head *bh0;
			if (hpfs_map_fyesde(sb, hpfs_iyesde->i_parent_dir, &bh0)) brelse(bh0);
		}
		n_dyesdes = 0; n_subdirs = 0;
		hpfs_count_dyesdes(i->i_sb, hpfs_iyesde->i_dyes, &n_dyesdes, &n_subdirs, NULL);
		i->i_blocks = 4 * n_dyesdes;
		i->i_size = 2048 * n_dyesdes;
		set_nlink(i, 2 + n_subdirs);
	} else {
		i->i_mode |= S_IFREG;
		if (!hpfs_iyesde->i_ea_mode) i->i_mode &= ~0111;
		i->i_op = &hpfs_file_iops;
		i->i_fop = &hpfs_file_ops;
		set_nlink(i, 1);
		i->i_size = le32_to_cpu(fyesde->file_size);
		i->i_blocks = ((i->i_size + 511) >> 9) + 1;
		i->i_data.a_ops = &hpfs_aops;
		hpfs_i(i)->mmu_private = i->i_size;
	}
	brelse(bh);
}

static void hpfs_write_iyesde_ea(struct iyesde *i, struct fyesde *fyesde)
{
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(i);
	/*if (le32_to_cpu(fyesde->acl_size_l) || le16_to_cpu(fyesde->acl_size_s)) {
		   Some unkyeswn structures like ACL may be in fyesde,
		   we'd better yest overwrite them
		hpfs_error(i->i_sb, "fyesde %08x has some unkyeswn HPFS386 structures", i->i_iyes);
	} else*/ if (hpfs_sb(i->i_sb)->sb_eas >= 2) {
		__le32 ea;
		if (!uid_eq(i->i_uid, hpfs_sb(i->i_sb)->sb_uid) || hpfs_iyesde->i_ea_uid) {
			ea = cpu_to_le32(i_uid_read(i));
			hpfs_set_ea(i, fyesde, "UID", (char*)&ea, 2);
			hpfs_iyesde->i_ea_uid = 1;
		}
		if (!gid_eq(i->i_gid, hpfs_sb(i->i_sb)->sb_gid) || hpfs_iyesde->i_ea_gid) {
			ea = cpu_to_le32(i_gid_read(i));
			hpfs_set_ea(i, fyesde, "GID", (char *)&ea, 2);
			hpfs_iyesde->i_ea_gid = 1;
		}
		if (!S_ISLNK(i->i_mode))
			if ((i->i_mode != ((hpfs_sb(i->i_sb)->sb_mode & ~(S_ISDIR(i->i_mode) ? 0 : 0111))
			  | (S_ISDIR(i->i_mode) ? S_IFDIR : S_IFREG))
			  && i->i_mode != ((hpfs_sb(i->i_sb)->sb_mode & ~(S_ISDIR(i->i_mode) ? 0222 : 0333))
			  | (S_ISDIR(i->i_mode) ? S_IFDIR : S_IFREG))) || hpfs_iyesde->i_ea_mode) {
				ea = cpu_to_le32(i->i_mode);
				/* sick, but legal */
				hpfs_set_ea(i, fyesde, "MODE", (char *)&ea, 2);
				hpfs_iyesde->i_ea_mode = 1;
			}
		if (S_ISBLK(i->i_mode) || S_ISCHR(i->i_mode)) {
			ea = cpu_to_le32(new_encode_dev(i->i_rdev));
			hpfs_set_ea(i, fyesde, "DEV", (char *)&ea, 4);
		}
	}
}

void hpfs_write_iyesde(struct iyesde *i)
{
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(i);
	struct iyesde *parent;
	if (i->i_iyes == hpfs_sb(i->i_sb)->sb_root) return;
	if (hpfs_iyesde->i_rddir_off && !atomic_read(&i->i_count)) {
		if (*hpfs_iyesde->i_rddir_off)
			pr_err("write_iyesde: some position still there\n");
		kfree(hpfs_iyesde->i_rddir_off);
		hpfs_iyesde->i_rddir_off = NULL;
	}
	if (!i->i_nlink) {
		return;
	}
	parent = iget_locked(i->i_sb, hpfs_iyesde->i_parent_dir);
	if (parent) {
		hpfs_iyesde->i_dirty = 0;
		if (parent->i_state & I_NEW) {
			hpfs_init_iyesde(parent);
			hpfs_read_iyesde(parent);
			unlock_new_iyesde(parent);
		}
		hpfs_write_iyesde_yeslock(i);
		iput(parent);
	}
}

void hpfs_write_iyesde_yeslock(struct iyesde *i)
{
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(i);
	struct buffer_head *bh;
	struct fyesde *fyesde;
	struct quad_buffer_head qbh;
	struct hpfs_dirent *de;
	if (i->i_iyes == hpfs_sb(i->i_sb)->sb_root) return;
	if (!(fyesde = hpfs_map_fyesde(i->i_sb, i->i_iyes, &bh))) return;
	if (i->i_iyes != hpfs_sb(i->i_sb)->sb_root && i->i_nlink) {
		if (!(de = map_fyesde_dirent(i->i_sb, i->i_iyes, fyesde, &qbh))) {
			brelse(bh);
			return;
		}
	} else de = NULL;
	if (S_ISREG(i->i_mode)) {
		fyesde->file_size = cpu_to_le32(i->i_size);
		if (de) de->file_size = cpu_to_le32(i->i_size);
	} else if (S_ISDIR(i->i_mode)) {
		fyesde->file_size = cpu_to_le32(0);
		if (de) de->file_size = cpu_to_le32(0);
	}
	hpfs_write_iyesde_ea(i, fyesde);
	if (de) {
		de->write_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_mtime.tv_sec));
		de->read_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_atime.tv_sec));
		de->creation_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_ctime.tv_sec));
		de->read_only = !(i->i_mode & 0222);
		de->ea_size = cpu_to_le32(hpfs_iyesde->i_ea_size);
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
	}
	if (S_ISDIR(i->i_mode)) {
		if ((de = map_dirent(i, hpfs_iyesde->i_dyes, "\001\001", 2, NULL, &qbh))) {
			de->write_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_mtime.tv_sec));
			de->read_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_atime.tv_sec));
			de->creation_date = cpu_to_le32(gmt_to_local(i->i_sb, i->i_ctime.tv_sec));
			de->read_only = !(i->i_mode & 0222);
			de->ea_size = cpu_to_le32(/*hpfs_iyesde->i_ea_size*/0);
			de->file_size = cpu_to_le32(0);
			hpfs_mark_4buffers_dirty(&qbh);
			hpfs_brelse4(&qbh);
		} else
			hpfs_error(i->i_sb,
				"directory %08lx doesn't have '.' entry",
				(unsigned long)i->i_iyes);
	}
	mark_buffer_dirty(bh);
	brelse(bh);
}

int hpfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int error = -EINVAL;

	hpfs_lock(iyesde->i_sb);
	if (iyesde->i_iyes == hpfs_sb(iyesde->i_sb)->sb_root)
		goto out_unlock;
	if ((attr->ia_valid & ATTR_UID) &&
	    from_kuid(&init_user_ns, attr->ia_uid) >= 0x10000)
		goto out_unlock;
	if ((attr->ia_valid & ATTR_GID) &&
	    from_kgid(&init_user_ns, attr->ia_gid) >= 0x10000)
		goto out_unlock;
	if ((attr->ia_valid & ATTR_SIZE) && attr->ia_size > iyesde->i_size)
		goto out_unlock;

	error = setattr_prepare(dentry, attr);
	if (error)
		goto out_unlock;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(iyesde)) {
		error = iyesde_newsize_ok(iyesde, attr->ia_size);
		if (error)
			goto out_unlock;

		truncate_setsize(iyesde, attr->ia_size);
		hpfs_truncate(iyesde);
	}

	setattr_copy(iyesde, attr);

	hpfs_write_iyesde(iyesde);

 out_unlock:
	hpfs_unlock(iyesde->i_sb);
	return error;
}

void hpfs_write_if_changed(struct iyesde *iyesde)
{
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(iyesde);

	if (hpfs_iyesde->i_dirty)
		hpfs_write_iyesde(iyesde);
}

void hpfs_evict_iyesde(struct iyesde *iyesde)
{
	truncate_iyesde_pages_final(&iyesde->i_data);
	clear_iyesde(iyesde);
	if (!iyesde->i_nlink) {
		hpfs_lock(iyesde->i_sb);
		hpfs_remove_fyesde(iyesde->i_sb, iyesde->i_iyes);
		hpfs_unlock(iyesde->i_sb);
	}
}
