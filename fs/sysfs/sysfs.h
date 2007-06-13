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
	struct rw_semaphore	s_active;
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

/*
 * A sysfs file which deletes another file when written to need to
 * write lock the s_active of the victim while its s_active is read
 * locked for the write operation.  Tell lockdep that this is okay.
 */
enum sysfs_s_active_class
{
	SYSFS_S_ACTIVE_NORMAL,		/* file r/w access, etc - default */
	SYSFS_S_ACTIVE_DEACTIVATE,	/* file deactivation */
};

extern struct vfsmount * sysfs_mount;
extern struct kmem_cache *sysfs_dir_cachep;

extern void sysfs_delete_inode(struct inode *inode);
extern struct inode * sysfs_new_inode(mode_t mode, struct sysfs_dirent *);
extern int sysfs_create(struct sysfs_dirent *sd, struct dentry *dentry,
			int mode, int (*init)(struct inode *));

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

/**
 *	sysfs_get_active - get an active reference to sysfs_dirent
 *	@sd: sysfs_dirent to get an active reference to
 *
 *	Get an active reference of @sd.  This function is noop if @sd
 *	is NULL.
 *
 *	RETURNS:
 *	Pointer to @sd on success, NULL on failure.
 */
static inline struct sysfs_dirent *sysfs_get_active(struct sysfs_dirent *sd)
{
	if (sd) {
		if (unlikely(!down_read_trylock(&sd->s_active)))
			sd = NULL;
	}
	return sd;
}

/**
 *	sysfs_put_active - put an active reference to sysfs_dirent
 *	@sd: sysfs_dirent to put an active reference to
 *
 *	Put an active reference to @sd.  This function is noop if @sd
 *	is NULL.
 */
static inline void sysfs_put_active(struct sysfs_dirent *sd)
{
	if (sd)
		up_read(&sd->s_active);
}

/**
 *	sysfs_get_active_two - get active references to sysfs_dirent and parent
 *	@sd: sysfs_dirent of interest
 *
 *	Get active reference to @sd and its parent.  Parent's active
 *	reference is grabbed first.  This function is noop if @sd is
 *	NULL.
 *
 *	RETURNS:
 *	Pointer to @sd on success, NULL on failure.
 */
static inline struct sysfs_dirent *sysfs_get_active_two(struct sysfs_dirent *sd)
{
	if (sd) {
		if (sd->s_parent && unlikely(!sysfs_get_active(sd->s_parent)))
			return NULL;
		if (unlikely(!sysfs_get_active(sd))) {
			sysfs_put_active(sd->s_parent);
			return NULL;
		}
	}
	return sd;
}

/**
 *	sysfs_put_active_two - put active references to sysfs_dirent and parent
 *	@sd: sysfs_dirent of interest
 *
 *	Put active references to @sd and its parent.  This function is
 *	noop if @sd is NULL.
 */
static inline void sysfs_put_active_two(struct sysfs_dirent *sd)
{
	if (sd) {
		sysfs_put_active(sd);
		sysfs_put_active(sd->s_parent);
	}
}

/**
 *	sysfs_deactivate - deactivate sysfs_dirent
 *	@sd: sysfs_dirent to deactivate
 *
 *	Deny new active references and drain existing ones.  s_active
 *	will be unlocked when the sysfs_dirent is released.
 */
static inline void sysfs_deactivate(struct sysfs_dirent *sd)
{
	down_write_nested(&sd->s_active, SYSFS_S_ACTIVE_DEACTIVATE);

	/* s_active will be unlocked by the thread doing the final put
	 * on @sd.  Lie to lockdep.
	 */
	rwsem_release(&sd->s_active.dep_map, 1, _RET_IP_);
}

static inline int sysfs_is_shadowed_inode(struct inode *inode)
{
	return S_ISDIR(inode->i_mode) && inode->i_op->follow_link;
}
