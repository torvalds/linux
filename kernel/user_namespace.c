/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2 of the
 *  License.
 */

#include <linux/export.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/highuid.h>
#include <linux/cred.h>
#include <linux/securebits.h>
#include <linux/keyctl.h>
#include <linux/key-type.h>
#include <keys/user-type.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/projid.h>

static struct kmem_cache *user_ns_cachep __read_mostly;

static bool new_idmap_permitted(struct user_namespace *ns, int cap_setid,
				struct uid_gid_map *map);

/*
 * Create a new user namespace, deriving the creator from the user in the
 * passed credentials, and replacing that user with the new root user for the
 * new namespace.
 *
 * This is called by copy_creds(), which will finish setting the target task's
 * credentials.
 */
int create_user_ns(struct cred *new)
{
	struct user_namespace *ns, *parent_ns = new->user_ns;
	kuid_t owner = new->euid;
	kgid_t group = new->egid;

	/* The creator needs a mapping in the parent user namespace
	 * or else we won't be able to reasonably tell userspace who
	 * created a user_namespace.
	 */
	if (!kuid_has_mapping(parent_ns, owner) ||
	    !kgid_has_mapping(parent_ns, group))
		return -EPERM;

	ns = kmem_cache_zalloc(user_ns_cachep, GFP_KERNEL);
	if (!ns)
		return -ENOMEM;

	kref_init(&ns->kref);
	ns->parent = parent_ns;
	ns->owner = owner;
	ns->group = group;

	/* Start with the same capabilities as init but useless for doing
	 * anything as the capabilities are bound to the new user namespace.
	 */
	new->securebits = SECUREBITS_DEFAULT;
	new->cap_inheritable = CAP_EMPTY_SET;
	new->cap_permitted = CAP_FULL_SET;
	new->cap_effective = CAP_FULL_SET;
	new->cap_bset = CAP_FULL_SET;
#ifdef CONFIG_KEYS
	key_put(new->request_key_auth);
	new->request_key_auth = NULL;
#endif
	/* tgcred will be cleared in our caller bc CLONE_THREAD won't be set */

	/* Leave the new->user_ns reference with the new user namespace. */
	/* Leave the reference to our user_ns with the new cred. */
	new->user_ns = ns;

	return 0;
}

void free_user_ns(struct kref *kref)
{
	struct user_namespace *parent, *ns =
		container_of(kref, struct user_namespace, kref);

	parent = ns->parent;
	kmem_cache_free(user_ns_cachep, ns);
	put_user_ns(parent);
}
EXPORT_SYMBOL(free_user_ns);

static u32 map_id_range_down(struct uid_gid_map *map, u32 id, u32 count)
{
	unsigned idx, extents;
	u32 first, last, id2;

	id2 = id + count - 1;

	/* Find the matching extent */
	extents = map->nr_extents;
	smp_read_barrier_depends();
	for (idx = 0; idx < extents; idx++) {
		first = map->extent[idx].first;
		last = first + map->extent[idx].count - 1;
		if (id >= first && id <= last &&
		    (id2 >= first && id2 <= last))
			break;
	}
	/* Map the id or note failure */
	if (idx < extents)
		id = (id - first) + map->extent[idx].lower_first;
	else
		id = (u32) -1;

	return id;
}

static u32 map_id_down(struct uid_gid_map *map, u32 id)
{
	unsigned idx, extents;
	u32 first, last;

	/* Find the matching extent */
	extents = map->nr_extents;
	smp_read_barrier_depends();
	for (idx = 0; idx < extents; idx++) {
		first = map->extent[idx].first;
		last = first + map->extent[idx].count - 1;
		if (id >= first && id <= last)
			break;
	}
	/* Map the id or note failure */
	if (idx < extents)
		id = (id - first) + map->extent[idx].lower_first;
	else
		id = (u32) -1;

	return id;
}

static u32 map_id_up(struct uid_gid_map *map, u32 id)
{
	unsigned idx, extents;
	u32 first, last;

	/* Find the matching extent */
	extents = map->nr_extents;
	smp_read_barrier_depends();
	for (idx = 0; idx < extents; idx++) {
		first = map->extent[idx].lower_first;
		last = first + map->extent[idx].count - 1;
		if (id >= first && id <= last)
			break;
	}
	/* Map the id or note failure */
	if (idx < extents)
		id = (id - first) + map->extent[idx].first;
	else
		id = (u32) -1;

	return id;
}

/**
 *	make_kuid - Map a user-namespace uid pair into a kuid.
 *	@ns:  User namespace that the uid is in
 *	@uid: User identifier
 *
 *	Maps a user-namespace uid pair into a kernel internal kuid,
 *	and returns that kuid.
 *
 *	When there is no mapping defined for the user-namespace uid
 *	pair INVALID_UID is returned.  Callers are expected to test
 *	for and handle handle INVALID_UID being returned.  INVALID_UID
 *	may be tested for using uid_valid().
 */
kuid_t make_kuid(struct user_namespace *ns, uid_t uid)
{
	/* Map the uid to a global kernel uid */
	return KUIDT_INIT(map_id_down(&ns->uid_map, uid));
}
EXPORT_SYMBOL(make_kuid);

/**
 *	from_kuid - Create a uid from a kuid user-namespace pair.
 *	@targ: The user namespace we want a uid in.
 *	@kuid: The kernel internal uid to start with.
 *
 *	Map @kuid into the user-namespace specified by @targ and
 *	return the resulting uid.
 *
 *	There is always a mapping into the initial user_namespace.
 *
 *	If @kuid has no mapping in @targ (uid_t)-1 is returned.
 */
uid_t from_kuid(struct user_namespace *targ, kuid_t kuid)
{
	/* Map the uid from a global kernel uid */
	return map_id_up(&targ->uid_map, __kuid_val(kuid));
}
EXPORT_SYMBOL(from_kuid);

/**
 *	from_kuid_munged - Create a uid from a kuid user-namespace pair.
 *	@targ: The user namespace we want a uid in.
 *	@kuid: The kernel internal uid to start with.
 *
 *	Map @kuid into the user-namespace specified by @targ and
 *	return the resulting uid.
 *
 *	There is always a mapping into the initial user_namespace.
 *
 *	Unlike from_kuid from_kuid_munged never fails and always
 *	returns a valid uid.  This makes from_kuid_munged appropriate
 *	for use in syscalls like stat and getuid where failing the
 *	system call and failing to provide a valid uid are not an
 *	options.
 *
 *	If @kuid has no mapping in @targ overflowuid is returned.
 */
uid_t from_kuid_munged(struct user_namespace *targ, kuid_t kuid)
{
	uid_t uid;
	uid = from_kuid(targ, kuid);

	if (uid == (uid_t) -1)
		uid = overflowuid;
	return uid;
}
EXPORT_SYMBOL(from_kuid_munged);

/**
 *	make_kgid - Map a user-namespace gid pair into a kgid.
 *	@ns:  User namespace that the gid is in
 *	@uid: group identifier
 *
 *	Maps a user-namespace gid pair into a kernel internal kgid,
 *	and returns that kgid.
 *
 *	When there is no mapping defined for the user-namespace gid
 *	pair INVALID_GID is returned.  Callers are expected to test
 *	for and handle INVALID_GID being returned.  INVALID_GID may be
 *	tested for using gid_valid().
 */
kgid_t make_kgid(struct user_namespace *ns, gid_t gid)
{
	/* Map the gid to a global kernel gid */
	return KGIDT_INIT(map_id_down(&ns->gid_map, gid));
}
EXPORT_SYMBOL(make_kgid);

/**
 *	from_kgid - Create a gid from a kgid user-namespace pair.
 *	@targ: The user namespace we want a gid in.
 *	@kgid: The kernel internal gid to start with.
 *
 *	Map @kgid into the user-namespace specified by @targ and
 *	return the resulting gid.
 *
 *	There is always a mapping into the initial user_namespace.
 *
 *	If @kgid has no mapping in @targ (gid_t)-1 is returned.
 */
gid_t from_kgid(struct user_namespace *targ, kgid_t kgid)
{
	/* Map the gid from a global kernel gid */
	return map_id_up(&targ->gid_map, __kgid_val(kgid));
}
EXPORT_SYMBOL(from_kgid);

/**
 *	from_kgid_munged - Create a gid from a kgid user-namespace pair.
 *	@targ: The user namespace we want a gid in.
 *	@kgid: The kernel internal gid to start with.
 *
 *	Map @kgid into the user-namespace specified by @targ and
 *	return the resulting gid.
 *
 *	There is always a mapping into the initial user_namespace.
 *
 *	Unlike from_kgid from_kgid_munged never fails and always
 *	returns a valid gid.  This makes from_kgid_munged appropriate
 *	for use in syscalls like stat and getgid where failing the
 *	system call and failing to provide a valid gid are not options.
 *
 *	If @kgid has no mapping in @targ overflowgid is returned.
 */
gid_t from_kgid_munged(struct user_namespace *targ, kgid_t kgid)
{
	gid_t gid;
	gid = from_kgid(targ, kgid);

	if (gid == (gid_t) -1)
		gid = overflowgid;
	return gid;
}
EXPORT_SYMBOL(from_kgid_munged);

/**
 *	make_kprojid - Map a user-namespace projid pair into a kprojid.
 *	@ns:  User namespace that the projid is in
 *	@projid: Project identifier
 *
 *	Maps a user-namespace uid pair into a kernel internal kuid,
 *	and returns that kuid.
 *
 *	When there is no mapping defined for the user-namespace projid
 *	pair INVALID_PROJID is returned.  Callers are expected to test
 *	for and handle handle INVALID_PROJID being returned.  INVALID_PROJID
 *	may be tested for using projid_valid().
 */
kprojid_t make_kprojid(struct user_namespace *ns, projid_t projid)
{
	/* Map the uid to a global kernel uid */
	return KPROJIDT_INIT(map_id_down(&ns->projid_map, projid));
}
EXPORT_SYMBOL(make_kprojid);

/**
 *	from_kprojid - Create a projid from a kprojid user-namespace pair.
 *	@targ: The user namespace we want a projid in.
 *	@kprojid: The kernel internal project identifier to start with.
 *
 *	Map @kprojid into the user-namespace specified by @targ and
 *	return the resulting projid.
 *
 *	There is always a mapping into the initial user_namespace.
 *
 *	If @kprojid has no mapping in @targ (projid_t)-1 is returned.
 */
projid_t from_kprojid(struct user_namespace *targ, kprojid_t kprojid)
{
	/* Map the uid from a global kernel uid */
	return map_id_up(&targ->projid_map, __kprojid_val(kprojid));
}
EXPORT_SYMBOL(from_kprojid);

/**
 *	from_kprojid_munged - Create a projiid from a kprojid user-namespace pair.
 *	@targ: The user namespace we want a projid in.
 *	@kprojid: The kernel internal projid to start with.
 *
 *	Map @kprojid into the user-namespace specified by @targ and
 *	return the resulting projid.
 *
 *	There is always a mapping into the initial user_namespace.
 *
 *	Unlike from_kprojid from_kprojid_munged never fails and always
 *	returns a valid projid.  This makes from_kprojid_munged
 *	appropriate for use in syscalls like stat and where
 *	failing the system call and failing to provide a valid projid are
 *	not an options.
 *
 *	If @kprojid has no mapping in @targ OVERFLOW_PROJID is returned.
 */
projid_t from_kprojid_munged(struct user_namespace *targ, kprojid_t kprojid)
{
	projid_t projid;
	projid = from_kprojid(targ, kprojid);

	if (projid == (projid_t) -1)
		projid = OVERFLOW_PROJID;
	return projid;
}
EXPORT_SYMBOL(from_kprojid_munged);


static int uid_m_show(struct seq_file *seq, void *v)
{
	struct user_namespace *ns = seq->private;
	struct uid_gid_extent *extent = v;
	struct user_namespace *lower_ns;
	uid_t lower;

	lower_ns = current_user_ns();
	if ((lower_ns == ns) && lower_ns->parent)
		lower_ns = lower_ns->parent;

	lower = from_kuid(lower_ns, KUIDT_INIT(extent->lower_first));

	seq_printf(seq, "%10u %10u %10u\n",
		extent->first,
		lower,
		extent->count);

	return 0;
}

static int gid_m_show(struct seq_file *seq, void *v)
{
	struct user_namespace *ns = seq->private;
	struct uid_gid_extent *extent = v;
	struct user_namespace *lower_ns;
	gid_t lower;

	lower_ns = current_user_ns();
	if ((lower_ns == ns) && lower_ns->parent)
		lower_ns = lower_ns->parent;

	lower = from_kgid(lower_ns, KGIDT_INIT(extent->lower_first));

	seq_printf(seq, "%10u %10u %10u\n",
		extent->first,
		lower,
		extent->count);

	return 0;
}

static int projid_m_show(struct seq_file *seq, void *v)
{
	struct user_namespace *ns = seq->private;
	struct uid_gid_extent *extent = v;
	struct user_namespace *lower_ns;
	projid_t lower;

	lower_ns = seq_user_ns(seq);
	if ((lower_ns == ns) && lower_ns->parent)
		lower_ns = lower_ns->parent;

	lower = from_kprojid(lower_ns, KPROJIDT_INIT(extent->lower_first));

	seq_printf(seq, "%10u %10u %10u\n",
		extent->first,
		lower,
		extent->count);

	return 0;
}

static void *m_start(struct seq_file *seq, loff_t *ppos, struct uid_gid_map *map)
{
	struct uid_gid_extent *extent = NULL;
	loff_t pos = *ppos;

	if (pos < map->nr_extents)
		extent = &map->extent[pos];

	return extent;
}

static void *uid_m_start(struct seq_file *seq, loff_t *ppos)
{
	struct user_namespace *ns = seq->private;

	return m_start(seq, ppos, &ns->uid_map);
}

static void *gid_m_start(struct seq_file *seq, loff_t *ppos)
{
	struct user_namespace *ns = seq->private;

	return m_start(seq, ppos, &ns->gid_map);
}

static void *projid_m_start(struct seq_file *seq, loff_t *ppos)
{
	struct user_namespace *ns = seq->private;

	return m_start(seq, ppos, &ns->projid_map);
}

static void *m_next(struct seq_file *seq, void *v, loff_t *pos)
{
	(*pos)++;
	return seq->op->start(seq, pos);
}

static void m_stop(struct seq_file *seq, void *v)
{
	return;
}

struct seq_operations proc_uid_seq_operations = {
	.start = uid_m_start,
	.stop = m_stop,
	.next = m_next,
	.show = uid_m_show,
};

struct seq_operations proc_gid_seq_operations = {
	.start = gid_m_start,
	.stop = m_stop,
	.next = m_next,
	.show = gid_m_show,
};

struct seq_operations proc_projid_seq_operations = {
	.start = projid_m_start,
	.stop = m_stop,
	.next = m_next,
	.show = projid_m_show,
};

static DEFINE_MUTEX(id_map_mutex);

static ssize_t map_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos,
			 int cap_setid,
			 struct uid_gid_map *map,
			 struct uid_gid_map *parent_map)
{
	struct seq_file *seq = file->private_data;
	struct user_namespace *ns = seq->private;
	struct uid_gid_map new_map;
	unsigned idx;
	struct uid_gid_extent *extent, *last = NULL;
	unsigned long page = 0;
	char *kbuf, *pos, *next_line;
	ssize_t ret = -EINVAL;

	/*
	 * The id_map_mutex serializes all writes to any given map.
	 *
	 * Any map is only ever written once.
	 *
	 * An id map fits within 1 cache line on most architectures.
	 *
	 * On read nothing needs to be done unless you are on an
	 * architecture with a crazy cache coherency model like alpha.
	 *
	 * There is a one time data dependency between reading the
	 * count of the extents and the values of the extents.  The
	 * desired behavior is to see the values of the extents that
	 * were written before the count of the extents.
	 *
	 * To achieve this smp_wmb() is used on guarantee the write
	 * order and smp_read_barrier_depends() is guaranteed that we
	 * don't have crazy architectures returning stale data.
	 *
	 */
	mutex_lock(&id_map_mutex);

	ret = -EPERM;
	/* Only allow one successful write to the map */
	if (map->nr_extents != 0)
		goto out;

	/* Require the appropriate privilege CAP_SETUID or CAP_SETGID
	 * over the user namespace in order to set the id mapping.
	 */
	if (cap_valid(cap_setid) && !ns_capable(ns, cap_setid))
		goto out;

	/* Get a buffer */
	ret = -ENOMEM;
	page = __get_free_page(GFP_TEMPORARY);
	kbuf = (char *) page;
	if (!page)
		goto out;

	/* Only allow <= page size writes at the beginning of the file */
	ret = -EINVAL;
	if ((*ppos != 0) || (count >= PAGE_SIZE))
		goto out;

	/* Slurp in the user data */
	ret = -EFAULT;
	if (copy_from_user(kbuf, buf, count))
		goto out;
	kbuf[count] = '\0';

	/* Parse the user data */
	ret = -EINVAL;
	pos = kbuf;
	new_map.nr_extents = 0;
	for (;pos; pos = next_line) {
		extent = &new_map.extent[new_map.nr_extents];

		/* Find the end of line and ensure I don't look past it */
		next_line = strchr(pos, '\n');
		if (next_line) {
			*next_line = '\0';
			next_line++;
			if (*next_line == '\0')
				next_line = NULL;
		}

		pos = skip_spaces(pos);
		extent->first = simple_strtoul(pos, &pos, 10);
		if (!isspace(*pos))
			goto out;

		pos = skip_spaces(pos);
		extent->lower_first = simple_strtoul(pos, &pos, 10);
		if (!isspace(*pos))
			goto out;

		pos = skip_spaces(pos);
		extent->count = simple_strtoul(pos, &pos, 10);
		if (*pos && !isspace(*pos))
			goto out;

		/* Verify there is not trailing junk on the line */
		pos = skip_spaces(pos);
		if (*pos != '\0')
			goto out;

		/* Verify we have been given valid starting values */
		if ((extent->first == (u32) -1) ||
		    (extent->lower_first == (u32) -1 ))
			goto out;

		/* Verify count is not zero and does not cause the extent to wrap */
		if ((extent->first + extent->count) <= extent->first)
			goto out;
		if ((extent->lower_first + extent->count) <= extent->lower_first)
			goto out;

		/* For now only accept extents that are strictly in order */
		if (last &&
		    (((last->first + last->count) > extent->first) ||
		     ((last->lower_first + last->count) > extent->lower_first)))
			goto out;

		new_map.nr_extents++;
		last = extent;

		/* Fail if the file contains too many extents */
		if ((new_map.nr_extents == UID_GID_MAP_MAX_EXTENTS) &&
		    (next_line != NULL))
			goto out;
	}
	/* Be very certaint the new map actually exists */
	if (new_map.nr_extents == 0)
		goto out;

	ret = -EPERM;
	/* Validate the user is allowed to use user id's mapped to. */
	if (!new_idmap_permitted(ns, cap_setid, &new_map))
		goto out;

	/* Map the lower ids from the parent user namespace to the
	 * kernel global id space.
	 */
	for (idx = 0; idx < new_map.nr_extents; idx++) {
		u32 lower_first;
		extent = &new_map.extent[idx];

		lower_first = map_id_range_down(parent_map,
						extent->lower_first,
						extent->count);

		/* Fail if we can not map the specified extent to
		 * the kernel global id space.
		 */
		if (lower_first == (u32) -1)
			goto out;

		extent->lower_first = lower_first;
	}

	/* Install the map */
	memcpy(map->extent, new_map.extent,
		new_map.nr_extents*sizeof(new_map.extent[0]));
	smp_wmb();
	map->nr_extents = new_map.nr_extents;

	*ppos = count;
	ret = count;
out:
	mutex_unlock(&id_map_mutex);
	if (page)
		free_page(page);
	return ret;
}

ssize_t proc_uid_map_write(struct file *file, const char __user *buf, size_t size, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct user_namespace *ns = seq->private;

	if (!ns->parent)
		return -EPERM;

	return map_write(file, buf, size, ppos, CAP_SETUID,
			 &ns->uid_map, &ns->parent->uid_map);
}

ssize_t proc_gid_map_write(struct file *file, const char __user *buf, size_t size, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct user_namespace *ns = seq->private;

	if (!ns->parent)
		return -EPERM;

	return map_write(file, buf, size, ppos, CAP_SETGID,
			 &ns->gid_map, &ns->parent->gid_map);
}

ssize_t proc_projid_map_write(struct file *file, const char __user *buf, size_t size, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct user_namespace *ns = seq->private;
	struct user_namespace *seq_ns = seq_user_ns(seq);

	if (!ns->parent)
		return -EPERM;

	if ((seq_ns != ns) && (seq_ns != ns->parent))
		return -EPERM;

	/* Anyone can set any valid project id no capability needed */
	return map_write(file, buf, size, ppos, -1,
			 &ns->projid_map, &ns->parent->projid_map);
}

static bool new_idmap_permitted(struct user_namespace *ns, int cap_setid,
				struct uid_gid_map *new_map)
{
	/* Allow anyone to set a mapping that doesn't require privilege */
	if (!cap_valid(cap_setid))
		return true;

	/* Allow the specified ids if we have the appropriate capability
	 * (CAP_SETUID or CAP_SETGID) over the parent user namespace.
	 */
	if (ns_capable(ns->parent, cap_setid))
		return true;

	return false;
}

static __init int user_namespaces_init(void)
{
	user_ns_cachep = KMEM_CACHE(user_namespace, SLAB_PANIC);
	return 0;
}
module_init(user_namespaces_init);
