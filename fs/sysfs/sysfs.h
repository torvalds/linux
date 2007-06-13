struct sysfs_elem_dir {
	struct kobject		* kobj;
};

struct sysfs_elem_symlink {
	struct sysfs_dirent	* target_sd;
};

struct sysfs_elem_attr {
	struct attribute	* attr;
};

struct sysfs_elem_bin_attr {
	struct bin_attribute	* bin_attr;
};

/*
 * As long as s_count reference is held, the sysfs_dirent itself is
 * accessible.  Dereferencing s_elem or any other outer entity
 * requires s_active reference.
 */
struct sysfs_dirent {
	atomic_t		s_count;
	atomic_t		s_active;
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

#define SD_DEACTIVATED_BIAS	INT_MIN

extern struct vfsmount * sysfs_mount;
extern struct kmem_cache *sysfs_dir_cachep;

extern struct sysfs_dirent *sysfs_get_active(struct sysfs_dirent *sd);
extern void sysfs_put_active(struct sysfs_dirent *sd);
extern struct sysfs_dirent *sysfs_get_active_two(struct sysfs_dirent *sd);
extern void sysfs_put_active_two(struct sysfs_dirent *sd);
extern void sysfs_deactivate(struct sysfs_dirent *sd);

extern void sysfs_delete_inode(struct inode *inode);
extern void sysfs_init_inode(struct sysfs_dirent *sd, struct inode *inode);
extern struct inode * sysfs_get_inode(struct sysfs_dirent *sd);
extern void sysfs_instantiate(struct dentry *dentry, struct inode *inode);

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

extern void sysfs_drop_dentry(struct sysfs_dirent *sd);
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
