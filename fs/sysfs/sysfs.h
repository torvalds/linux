
extern struct vfsmount * sysfs_mount;
extern kmem_cache_t *sysfs_dir_cachep;

extern struct inode * sysfs_new_inode(mode_t mode, struct sysfs_dirent *);
extern int sysfs_create(struct dentry *, int mode, int (*init)(struct inode *));

extern int sysfs_make_dirent(struct sysfs_dirent *, struct dentry *, void *,
				umode_t, int);

extern int sysfs_add_file(struct dentry *, const struct attribute *, int);
extern void sysfs_hash_and_remove(struct dentry * dir, const char * name);

extern int sysfs_create_subdir(struct kobject *, const char *, struct dentry **);
extern void sysfs_remove_subdir(struct dentry *);

extern const unsigned char * sysfs_get_name(struct sysfs_dirent *sd);
extern void sysfs_drop_dentry(struct sysfs_dirent *sd, struct dentry *parent);
extern int sysfs_setattr(struct dentry *dentry, struct iattr *iattr);

extern struct rw_semaphore sysfs_rename_sem;
extern struct super_block * sysfs_sb;
extern struct file_operations sysfs_dir_operations;
extern struct file_operations sysfs_file_operations;
extern struct file_operations bin_fops;
extern struct inode_operations sysfs_dir_inode_operations;
extern struct inode_operations sysfs_symlink_inode_operations;

struct sysfs_symlink {
	char * link_name;
	struct kobject * target_kobj;
};

static inline struct kobject * to_kobj(struct dentry * dentry)
{
	struct sysfs_dirent * sd = dentry->d_fsdata;
	return ((struct kobject *) sd->s_element);
}

static inline struct attribute * to_attr(struct dentry * dentry)
{
	struct sysfs_dirent * sd = dentry->d_fsdata;
	return ((struct attribute *) sd->s_element);
}

static inline struct bin_attribute * to_bin_attr(struct dentry * dentry)
{
	struct sysfs_dirent * sd = dentry->d_fsdata;
	return ((struct bin_attribute *) sd->s_element);
}

static inline struct kobject *sysfs_get_kobject(struct dentry *dentry)
{
	struct kobject * kobj = NULL;

	spin_lock(&dcache_lock);
	if (!d_unhashed(dentry)) {
		struct sysfs_dirent * sd = dentry->d_fsdata;
		if (sd->s_type & SYSFS_KOBJ_LINK) {
			struct sysfs_symlink * sl = sd->s_element;
			kobj = kobject_get(sl->target_kobj);
		} else
			kobj = kobject_get(sd->s_element);
	}
	spin_unlock(&dcache_lock);

	return kobj;
}

static inline void release_sysfs_dirent(struct sysfs_dirent * sd)
{
	if (sd->s_type & SYSFS_KOBJ_LINK) {
		struct sysfs_symlink * sl = sd->s_element;
		kfree(sl->link_name);
		kobject_put(sl->target_kobj);
		kfree(sl);
	}
	kfree(sd->s_iattr);
	kmem_cache_free(sysfs_dir_cachep, sd);
}

static inline struct sysfs_dirent * sysfs_get(struct sysfs_dirent * sd)
{
	if (sd) {
		WARN_ON(!atomic_read(&sd->s_count));
		atomic_inc(&sd->s_count);
	}
	return sd;
}

static inline void sysfs_put(struct sysfs_dirent * sd)
{
	if (atomic_dec_and_test(&sd->s_count))
		release_sysfs_dirent(sd);
}

