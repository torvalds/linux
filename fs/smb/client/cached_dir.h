/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Functions to handle the cached directory entries
 *
 *  Copyright (c) 2022, Ronnie Sahlberg <lsahlber@redhat.com>
 */

#ifndef _CACHED_DIR_H
#define _CACHED_DIR_H


struct cached_dirent {
	struct list_head entry;
	char *name;
	int namelen;
	loff_t pos;
	struct cifs_fattr fattr;
};

struct cached_dirents {
	bool is_valid:1;
	bool is_failed:1;
	struct file *file; /*
			    * Used to associate the cache with a single
			    * open file instance.
			    */
	struct mutex de_mutex;
	loff_t pos;		 /* Expected ctx->pos */
	struct list_head entries;
	/* accounting for cached entries in this directory */
	unsigned long entries_count;
	unsigned long bytes_used;
};

struct cached_fid {
	struct list_head entry;
	struct cached_fids *cfids;
	const char *path;
	bool has_lease:1;
	bool is_open:1;
	bool on_list:1;
	bool file_all_info_is_valid:1;
	unsigned long time; /* jiffies of when lease was taken */
	unsigned long last_access_time; /* jiffies of when last accessed */
	struct kref refcount;
	struct cifs_fid fid;
	spinlock_t fid_lock;
	struct cifs_tcon *tcon;
	struct dentry *dentry;
	struct work_struct put_work;
	struct work_struct close_work;
	struct smb2_file_all_info file_all_info;
	struct cached_dirents dirents;
};

/* default MAX_CACHED_FIDS is 16 */
struct cached_fids {
	/* Must be held when:
	 * - accessing the cfids->entries list
	 * - accessing the cfids->dying list
	 */
	spinlock_t cfid_list_lock;
	int num_entries;
	struct list_head entries;
	struct list_head dying;
	struct work_struct invalidation_work;
	struct delayed_work laundromat_work;
	/* aggregate accounting for all cached dirents under this tcon */
	atomic_long_t total_dirents_entries;
	atomic64_t total_dirents_bytes;
};

/* Module-wide directory cache accounting (defined in cifsfs.c) */
extern atomic64_t cifs_dircache_bytes_used; /* bytes across all mounts */

extern struct cached_fids *init_cached_dirs(void);
extern void free_cached_dirs(struct cached_fids *cfids);
extern int open_cached_dir(unsigned int xid, struct cifs_tcon *tcon,
			   const char *path,
			   struct cifs_sb_info *cifs_sb,
			   bool lookup_only, struct cached_fid **cfid);
extern int open_cached_dir_by_dentry(struct cifs_tcon *tcon,
				     struct dentry *dentry,
				     struct cached_fid **cfid);
extern void close_cached_dir(struct cached_fid *cfid);
extern void drop_cached_dir_by_name(const unsigned int xid,
				    struct cifs_tcon *tcon,
				    const char *name,
				    struct cifs_sb_info *cifs_sb);
extern void close_all_cached_dirs(struct cifs_sb_info *cifs_sb);
extern void invalidate_all_cached_dirs(struct cifs_tcon *tcon);
extern bool cached_dir_lease_break(struct cifs_tcon *tcon, __u8 lease_key[16]);

#endif			/* _CACHED_DIR_H */
