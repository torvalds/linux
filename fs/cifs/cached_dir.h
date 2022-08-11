/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Functions to handle the cached directory entries
 *
 *  Copyright (c) 2022, Ronnie Sahlberg <lsahlber@redhat.com>
 */

#ifndef _CACHED_DIR_H
#define _CACHED_DIR_H


extern int open_cached_dir(unsigned int xid, struct cifs_tcon *tcon,
			   const char *path,
			   struct cifs_sb_info *cifs_sb,
			   struct cached_fid **cfid);
extern int open_cached_dir_by_dentry(struct cifs_tcon *tcon,
				     struct dentry *dentry,
				     struct cached_fid **cfid);
extern void close_cached_dir(struct cached_fid *cfid);
extern void close_cached_dir_lease(struct cached_fid *cfid);
extern void close_cached_dir_lease_locked(struct cached_fid *cfid);
extern void close_all_cached_dirs(struct cifs_sb_info *cifs_sb);
extern void invalidate_all_cached_dirs(struct cifs_tcon *tcon);
extern int cached_dir_lease_break(struct cifs_tcon *tcon, __u8 lease_key[16]);

#endif			/* _CACHED_DIR_H */
