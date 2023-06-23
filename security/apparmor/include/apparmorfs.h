/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor filesystem definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#ifndef __AA_APPARMORFS_H
#define __AA_APPARMORFS_H

extern struct path aa_null;

enum aa_sfs_type {
	AA_SFS_TYPE_BOOLEAN,
	AA_SFS_TYPE_STRING,
	AA_SFS_TYPE_U64,
	AA_SFS_TYPE_FOPS,
	AA_SFS_TYPE_DIR,
};

struct aa_sfs_entry;

struct aa_sfs_entry {
	const char *name;
	struct dentry *dentry;
	umode_t mode;
	enum aa_sfs_type v_type;
	union {
		bool boolean;
		char *string;
		unsigned long u64;
		struct aa_sfs_entry *files;
	} v;
	const struct file_operations *file_ops;
};

extern const struct file_operations aa_sfs_seq_file_ops;

#define AA_SFS_FILE_BOOLEAN(_name, _value) \
	{ .name = (_name), .mode = 0444, \
	  .v_type = AA_SFS_TYPE_BOOLEAN, .v.boolean = (_value), \
	  .file_ops = &aa_sfs_seq_file_ops }
#define AA_SFS_FILE_STRING(_name, _value) \
	{ .name = (_name), .mode = 0444, \
	  .v_type = AA_SFS_TYPE_STRING, .v.string = (_value), \
	  .file_ops = &aa_sfs_seq_file_ops }
#define AA_SFS_FILE_U64(_name, _value) \
	{ .name = (_name), .mode = 0444, \
	  .v_type = AA_SFS_TYPE_U64, .v.u64 = (_value), \
	  .file_ops = &aa_sfs_seq_file_ops }
#define AA_SFS_FILE_FOPS(_name, _mode, _fops) \
	{ .name = (_name), .v_type = AA_SFS_TYPE_FOPS, \
	  .mode = (_mode), .file_ops = (_fops) }
#define AA_SFS_DIR(_name, _value) \
	{ .name = (_name), .v_type = AA_SFS_TYPE_DIR, .v.files = (_value) }

extern void __init aa_destroy_aafs(void);

struct aa_profile;
struct aa_ns;

enum aafs_ns_type {
	AAFS_NS_DIR,
	AAFS_NS_PROFS,
	AAFS_NS_NS,
	AAFS_NS_RAW_DATA,
	AAFS_NS_LOAD,
	AAFS_NS_REPLACE,
	AAFS_NS_REMOVE,
	AAFS_NS_REVISION,
	AAFS_NS_COUNT,
	AAFS_NS_MAX_COUNT,
	AAFS_NS_SIZE,
	AAFS_NS_MAX_SIZE,
	AAFS_NS_OWNER,
	AAFS_NS_SIZEOF,
};

enum aafs_prof_type {
	AAFS_PROF_DIR,
	AAFS_PROF_PROFS,
	AAFS_PROF_NAME,
	AAFS_PROF_MODE,
	AAFS_PROF_ATTACH,
	AAFS_PROF_HASH,
	AAFS_PROF_RAW_DATA,
	AAFS_PROF_RAW_HASH,
	AAFS_PROF_RAW_ABI,
	AAFS_PROF_SIZEOF,
};

#define ns_dir(X) ((X)->dents[AAFS_NS_DIR])
#define ns_subns_dir(X) ((X)->dents[AAFS_NS_NS])
#define ns_subprofs_dir(X) ((X)->dents[AAFS_NS_PROFS])
#define ns_subdata_dir(X) ((X)->dents[AAFS_NS_RAW_DATA])
#define ns_subload(X) ((X)->dents[AAFS_NS_LOAD])
#define ns_subreplace(X) ((X)->dents[AAFS_NS_REPLACE])
#define ns_subremove(X) ((X)->dents[AAFS_NS_REMOVE])
#define ns_subrevision(X) ((X)->dents[AAFS_NS_REVISION])

#define prof_dir(X) ((X)->dents[AAFS_PROF_DIR])
#define prof_child_dir(X) ((X)->dents[AAFS_PROF_PROFS])

void __aa_bump_ns_revision(struct aa_ns *ns);
void __aafs_profile_rmdir(struct aa_profile *profile);
void __aafs_profile_migrate_dents(struct aa_profile *old,
				   struct aa_profile *new);
int __aafs_profile_mkdir(struct aa_profile *profile, struct dentry *parent);
void __aafs_ns_rmdir(struct aa_ns *ns);
int __aafs_ns_mkdir(struct aa_ns *ns, struct dentry *parent, const char *name,
		     struct dentry *dent);

struct aa_loaddata;

#ifdef CONFIG_SECURITY_APPARMOR_EXPORT_BINARY
void __aa_fs_remove_rawdata(struct aa_loaddata *rawdata);
int __aa_fs_create_rawdata(struct aa_ns *ns, struct aa_loaddata *rawdata);
#else
static inline void __aa_fs_remove_rawdata(struct aa_loaddata *rawdata)
{
	/* empty stub */
}

static inline int __aa_fs_create_rawdata(struct aa_ns *ns,
					 struct aa_loaddata *rawdata)
{
	return 0;
}
#endif /* CONFIG_SECURITY_APPARMOR_EXPORT_BINARY */

#endif /* __AA_APPARMORFS_H */
