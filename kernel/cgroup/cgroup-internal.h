#ifndef __CGROUP_INTERNAL_H
#define __CGROUP_INTERNAL_H

#include <linux/cgroup.h>
#include <linux/kernfs.h>
#include <linux/workqueue.h>
#include <linux/list.h>

/*
 * A cgroup can be associated with multiple css_sets as different tasks may
 * belong to different cgroups on different hierarchies.  In the other
 * direction, a css_set is naturally associated with multiple cgroups.
 * This M:N relationship is represented by the following link structure
 * which exists for each association and allows traversing the associations
 * from both sides.
 */
struct cgrp_cset_link {
	/* the cgroup and css_set this link associates */
	struct cgroup		*cgrp;
	struct css_set		*cset;

	/* list of cgrp_cset_links anchored at cgrp->cset_links */
	struct list_head	cset_link;

	/* list of cgrp_cset_links anchored at css_set->cgrp_links */
	struct list_head	cgrp_link;
};

struct cgroup_sb_opts {
	u16 subsys_mask;
	unsigned int flags;
	char *release_agent;
	bool cpuset_clone_children;
	char *name;
	/* User explicitly requested empty subsystem */
	bool none;
};

extern struct mutex cgroup_mutex;
extern spinlock_t css_set_lock;
extern struct cgroup_subsys *cgroup_subsys[];
extern struct list_head cgroup_roots;
extern struct file_system_type cgroup_fs_type;

/* iterate across the hierarchies */
#define for_each_root(root)						\
	list_for_each_entry((root), &cgroup_roots, root_list)

/**
 * for_each_subsys - iterate all enabled cgroup subsystems
 * @ss: the iteration cursor
 * @ssid: the index of @ss, CGROUP_SUBSYS_COUNT after reaching the end
 */
#define for_each_subsys(ss, ssid)					\
	for ((ssid) = 0; (ssid) < CGROUP_SUBSYS_COUNT &&		\
	     (((ss) = cgroup_subsys[ssid]) || true); (ssid)++)

static inline bool cgroup_is_dead(const struct cgroup *cgrp)
{
	return !(cgrp->self.flags & CSS_ONLINE);
}

static inline bool notify_on_release(const struct cgroup *cgrp)
{
	return test_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);
}

bool cgroup_ssid_enabled(int ssid);
bool cgroup_on_dfl(const struct cgroup *cgrp);

struct cgroup_root *cgroup_root_from_kf(struct kernfs_root *kf_root);
struct cgroup *task_cgroup_from_root(struct task_struct *task,
				     struct cgroup_root *root);
struct cgroup *cgroup_kn_lock_live(struct kernfs_node *kn, bool drain_offline);
void cgroup_kn_unlock(struct kernfs_node *kn);
int cgroup_path_ns_locked(struct cgroup *cgrp, char *buf, size_t buflen,
			  struct cgroup_namespace *ns);

void cgroup_free_root(struct cgroup_root *root);
void init_cgroup_root(struct cgroup_root *root, struct cgroup_sb_opts *opts);
int cgroup_setup_root(struct cgroup_root *root, u16 ss_mask);
int rebind_subsystems(struct cgroup_root *dst_root, u16 ss_mask);
struct dentry *cgroup_do_mount(struct file_system_type *fs_type, int flags,
			       struct cgroup_root *root, unsigned long magic,
			       struct cgroup_namespace *ns);

bool cgroup_may_migrate_to(struct cgroup *dst_cgrp);
void cgroup_migrate_finish(struct list_head *preloaded_csets);
void cgroup_migrate_add_src(struct css_set *src_cset,
			    struct cgroup *dst_cgrp,
			    struct list_head *preloaded_csets);
int cgroup_migrate_prepare_dst(struct list_head *preloaded_csets);
int cgroup_migrate(struct task_struct *leader, bool threadgroup,
		   struct cgroup_root *root);

int cgroup_attach_task(struct cgroup *dst_cgrp, struct task_struct *leader,
		       bool threadgroup);
ssize_t __cgroup_procs_write(struct kernfs_open_file *of, char *buf,
			     size_t nbytes, loff_t off, bool threadgroup);
ssize_t cgroup_procs_write(struct kernfs_open_file *of, char *buf, size_t nbytes,
			   loff_t off);

void cgroup_lock_and_drain_offline(struct cgroup *cgrp);

int cgroup_mkdir(struct kernfs_node *parent_kn, const char *name, umode_t mode);
int cgroup_rmdir(struct kernfs_node *kn);
int cgroup_show_path(struct seq_file *sf, struct kernfs_node *kf_node,
		     struct kernfs_root *kf_root);

/*
 * cgroup-v1.c
 */
extern struct cftype cgroup1_base_files[];
extern const struct file_operations proc_cgroupstats_operations;
extern struct kernfs_syscall_ops cgroup1_kf_syscall_ops;

bool cgroup1_ssid_disabled(int ssid);
void cgroup1_pidlist_destroy_all(struct cgroup *cgrp);
void cgroup1_release_agent(struct work_struct *work);
void cgroup1_check_for_release(struct cgroup *cgrp);
struct dentry *cgroup1_mount(struct file_system_type *fs_type, int flags,
			     void *data, unsigned long magic,
			     struct cgroup_namespace *ns);

#endif /* __CGROUP_INTERNAL_H */
