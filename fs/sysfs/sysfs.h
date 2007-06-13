struct sysfs_elem_dir {
	struct kobject		* kobj;
};

struct sysfs_elem_symlink {
	struct kobject		* target_kobj;
};

struct sysfs_elem_attr {
	struct attribute	* attr;
};

struct sysfs_elem_bin_attr {
	struct bin_attribute	* bin_attr;
};

struct sysfs_dirent {
	atomic_t		s_count;
	struct sysfs_dirent	* s_parent;
	struct list_head	s_sibling;
	struct list_head	s_children;
	const char		* s_name;

	union {
		struct sysfs_elem_dir		dir;
		struct sysfs_elem_symlink	symlink;
		struct sysfs_elem_attr		attr;
		struct sysfs_elem_bin_attr	bin_attr;
	}			s_elem;

	int			s_type;
	umode_t			s_mode;
	ino_t			s_ino;
	struct dentry		* s_dentry;
	struct iattr		* s_iattr;
	atomic_t		s_event;
};

extern struct vfsmount * sysfs_mount;
extern struct kmem_cache *sysfs_dir_cachep;

extern void sysfs_delete_inode(struct inode *inode);
extern struct inode * sysfs_new_inode(mode_t mode, struct sysfs_dirent *);
extern int sysfs_create(struct dentry *, int mode, int (*init)(struct inode *));

extern void release_sysfs_dirent(struct sysfs_dirent * sd);
extern int sysfs_dirent_exist(struct sysfs_dirent *, const unsigned char *);
extern struct sysfs_dirent *sysfs_new_dirent(const char *name, umode_t mode,
					     int type);
extern void sysfs_attach_dirent(struct sysfs_dirent *sd,
				struct sysfs_dirent *parent_sd,
				struct dentry *dentry);

extern int sysfs_add_file(struct dentry *, const struct attribute *, int);
extern int sysfs_hash_and_remove(struct dentry * dir, const char * name);
extern struct sysfs_dirent *sysfs_find(struct sysfs_dirent *dir, const char * name);

extern int sysfs_create_subdir(struct kobject *, const char *, struct dentry **);
extern void sysfs_remove_subdir(struct dentry *);

extern void sysfs_drop_dentry(struct sysfs_dirent *sd, struct dentry *parent);
extern int sysfs_setattr(struct dentry *dentry, struct iattr *iattr);

extern spinlock_t sysfs_lock;
extern spinlock_t kobj_sysfs_assoc_lock;
extern struct rw_semaphore sysfs_rename_sem;
extern struct super_block * sysfs_sb;
extern const struct file_operations sysfs_dir_operations;
extern const struct file_operations sysfs_file_operations;
extern const struct file_operations bin_fops;
extern const struct inode_operations sysfs_dir_inode_operations;
extern const struct inode_operations sysfs_symlink_inode_operations;

struct sysfs_buffer {
	struct list_head		associates;
	size_t				count;
	loff_t				pos;
	char				* page;
	struct sysfs_ops		* ops;
	struct semaphore		sem;
	int				orphaned;
	int				needs_read_fill;
	int				event;
};

struct sysfs_buffer_collection {
	struct list_head	associates;
};

static inline struct kobject * to_kobj(struct dentry * dentry)
{
	struct sysfs_dirent * sd = dentry->d_fsdata;
	return sd->s_elem.dir.kobj;
}

static inline struct kobject *sysfs_get_kobject(struct dentry *dentry)
{
	struct kobject * kobj = NULL;

	spin_lock(&dcache_lock);
	if (!d_unhashed(dentry)) {
		struct sysfs_dirent * sd = dentry->d_fsdata;
		if (sd->s_type & SYSFS_KOBJ_LINK)
			kobj = kobject_get(sd->s_elem.symlink.target_kobj);
		else
			kobj = kobject_get(sd->s_elem.dir.kobj);
	}
	spin_unlock(&dcache_lock);

	return kobj;
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
	if (sd && atomic_dec_and_test(&sd->s_count))
		release_sysfs_dirent(sd);
}

static inline int sysfs_is_shadowed_inode(struct inode *inode)
{
	return S_ISDIR(inode->i_mode) && inode->i_op->follow_link;
}
