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
	struct sysfs_dirent	* s_sibling;
	struct sysfs_dirent	* s_children;
	const char		* s_name;

	union {
		struct sysfs_elem_dir		dir;
		struct sysfs_elem_symlink	symlink;
		struct sysfs_elem_attr		attr;
		struct sysfs_elem_bin_attr	bin_attr;
	}			s_elem;

	unsigned int		s_flags;
	umode_t			s_mode;
	ino_t			s_ino;
	struct dentry		* s_dentry;
	struct iattr		* s_iattr;
	atomic_t		s_event;
};

#define SD_DEACTIVATED_BIAS	INT_MIN

struct sysfs_addrm_cxt {
	struct sysfs_dirent	*parent_sd;
	struct inode		*parent_inode;
	struct sysfs_dirent	*removed;
	int			cnt;
};

extern struct vfsmount * sysfs_mount;
extern struct sysfs_dirent sysfs_root;
extern struct kmem_cache *sysfs_dir_cachep;

extern struct dentry *sysfs_get_dentry(struct sysfs_dirent *sd);
extern void sysfs_link_sibling(struct sysfs_dirent *sd);
extern void sysfs_unlink_sibling(struct sysfs_dirent *sd);
extern struct sysfs_dirent *sysfs_get_active(struct sysfs_dirent *sd);
extern void sysfs_put_active(struct sysfs_dirent *sd);
extern struct sysfs_dirent *sysfs_get_active_two(struct sysfs_dirent *sd);
extern void sysfs_put_active_two(struct sysfs_dirent *sd);
extern void sysfs_addrm_start(struct sysfs_addrm_cxt *acxt,
			      struct sysfs_dirent *parent_sd);
extern void sysfs_add_one(struct sysfs_addrm_cxt *acxt,
			  struct sysfs_dirent *sd);
extern void sysfs_remove_one(struct sysfs_addrm_cxt *acxt,
			     struct sysfs_dirent *sd);
extern int sysfs_addrm_finish(struct sysfs_addrm_cxt *acxt);

extern void sysfs_delete_inode(struct inode *inode);
extern struct inode * sysfs_get_inode(struct sysfs_dirent *sd);
extern void sysfs_instantiate(struct dentry *dentry, struct inode *inode);

extern void release_sysfs_dirent(struct sysfs_dirent * sd);
extern struct sysfs_dirent *sysfs_find_dirent(struct sysfs_dirent *parent_sd,
					      const unsigned char *name);
extern struct sysfs_dirent *sysfs_get_dirent(struct sysfs_dirent *parent_sd,
					     const unsigned char *name);
extern struct sysfs_dirent *sysfs_new_dirent(const char *name, umode_t mode,
					     int type);

extern int sysfs_add_file(struct sysfs_dirent *dir_sd,
			  const struct attribute *attr, int type);
extern int sysfs_hash_and_remove(struct sysfs_dirent *dir_sd, const char *name);
extern struct sysfs_dirent *sysfs_find(struct sysfs_dirent *dir, const char * name);

extern int sysfs_create_subdir(struct kobject *kobj, const char *name,
			       struct sysfs_dirent **p_sd);
extern void sysfs_remove_subdir(struct sysfs_dirent *sd);

extern int sysfs_setattr(struct dentry *dentry, struct iattr *iattr);

extern spinlock_t sysfs_assoc_lock;
extern struct mutex sysfs_mutex;
extern struct super_block * sysfs_sb;
extern const struct file_operations sysfs_dir_operations;
extern const struct file_operations sysfs_file_operations;
extern const struct file_operations bin_fops;
extern const struct inode_operations sysfs_dir_inode_operations;
extern const struct inode_operations sysfs_symlink_inode_operations;

static inline unsigned int sysfs_type(struct sysfs_dirent *sd)
{
	return sd->s_flags & SYSFS_TYPE_MASK;
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
