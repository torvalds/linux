// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/user_namespace.h>
#include <linux/proc_ns.h>
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
#include <linux/fs_struct.h>
#include <linux/bsearch.h>
#include <linux/sort.h>

static struct kmem_cache *user_ns_cachep __read_mostly;
static DEFINE_MUTEX(userns_state_mutex);

static bool new_idmap_permitted(const struct file *file,
				struct user_namespace *ns, int cap_setid,
				struct uid_gid_map *map);
static void free_user_ns(struct work_struct *work);

static struct ucounts *inc_user_namespaces(struct user_namespace *ns, kuid_t uid)
{
	return inc_ucount(ns, uid, UCOUNT_USER_NAMESPACES);
}

static void dec_user_namespaces(struct ucounts *ucounts)
{
	return dec_ucount(ucounts, UCOUNT_USER_NAMESPACES);
}

static void set_cred_user_ns(struct cred *cred, struct user_namespace *user_ns)
{
	/* Start with the same capabilities as init but useless for doing
	 * anything as the capabilities are bound to the new user namespace.
	 */
	cred->securebits = SECUREBITS_DEFAULT;
	cred->cap_inheritable = CAP_EMPTY_SET;
	cred->cap_permitted = CAP_FULL_SET;
	cred->cap_effective = CAP_FULL_SET;
	cred->cap_ambient = CAP_EMPTY_SET;
	cred->cap_bset = CAP_FULL_SET;
#ifdef CONFIG_KEYS
	key_put(cred->request_key_auth);
	cred->request_key_auth = NULL;
#endif
	/* tgcred will be cleared in our caller bc CLONE_THREAD won't be set */
	cred->user_ns = user_ns;
}

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
	struct ucounts *ucounts;
	int ret, i;

	ret = -ENOSPC;
	if (parent_ns->level > 32)
		goto fail;

	ucounts = inc_user_namespaces(parent_ns, owner);
	if (!ucounts)
		goto fail;

	/*
	 * Verify that we can not violate the policy of which files
	 * may be accessed that is specified by the root directory,
	 * by verifing that the root directory is at the root of the
	 * mount namespace which allows all files to be accessed.
	 */
	ret = -EPERM;
	if (current_chrooted())
		goto fail_dec;

	/* The creator needs a mapping in the parent user namespace
	 * or else we won't be able to reasonably tell userspace who
	 * created a user_namespace.
	 */
	ret = -EPERM;
	if (!kuid_has_mapping(parent_ns, owner) ||
	    !kgid_has_mapping(parent_ns, group))
		goto fail_dec;

	ret = -ENOMEM;
	ns = kmem_cache_zalloc(user_ns_cachep, GFP_KERNEL);
	if (!ns)
		goto fail_dec;

	ns->parent_could_setfcap = cap_raised(new->cap_effective, CAP_SETFCAP);
	ret = ns_alloc_inum(&ns->ns);
	if (ret)
		goto fail_free;
	ns->ns.ops = &userns_operations;

	refcount_set(&ns->ns.count, 1);
	/* Leave the new->user_ns reference with the new user namespace. */
	ns->parent = parent_ns;
	ns->level = parent_ns->level + 1;
	ns->owner = owner;
	ns->group = group;
	INIT_WORK(&ns->work, free_user_ns);
	for (i = 0; i < UCOUNT_COUNTS; i++) {
		ns->ucount_max[i] = INT_MAX;
	}
	ns->ucounts = ucounts;

	/* Inherit USERNS_SETGROUPS_ALLOWED from our parent */
	mutex_lock(&userns_state_mutex);
	ns->flags = parent_ns->flags;
	mutex_unlock(&userns_state_mutex);

#ifdef CONFIG_KEYS
	INIT_LIST_HEAD(&ns->keyring_name_list);
	init_rwsem(&ns->keyring_sem);
#endif
	ret = -ENOMEM;
	if (!setup_userns_sysctls(ns))
		goto fail_keyring;

	set_cred_user_ns(new, ns);
	return 0;
fail_keyring:
#ifdef CONFIG_PERSISTENT_KEYRINGS
	key_put(ns->persistent_keyring_register);
#endif
	ns_free_inum(&ns->ns);
fail_free:
	kmem_cache_free(user_ns_cachep, ns);
fail_dec:
	dec_user_namespaces(ucounts);
fail:
	return ret;
}

int unshare_userns(unsigned long unshare_flags, struct cred **new_cred)
{
	struct cred *cred;
	int err = -ENOMEM;

	if (!(unshare_flags & CLONE_NEWUSER))
		return 0;

	cred = prepare_creds();
	if (cred) {
		err = create_user_ns(cred);
		if (err)
			put_cred(cred);
		else
			*new_cred = cred;
	}

	return err;
}

static void free_user_ns(struct work_struct *work)
{
	struct user_namespace *parent, *ns =
		container_of(work, struct user_namespace, work);

	do {
		struct ucounts *ucounts = ns->ucounts;
		parent = ns->parent;
		if (ns->gid_map.nr_extents > UID_GID_MAP_MAX_BASE_EXTENTS) {
			kfree(ns->gid_map.forward);
			kfree(ns->gid_map.reverse);
		}
		if (ns->uid_map.nr_extents > UID_GID_MAP_MAX_BASE_EXTENTS) {
			kfree(ns->uid_map.forward);
			kfree(ns->uid_map.reverse);
		}
		if (ns->projid_map.nr_extents > UID_GID_MAP_MAX_BASE_EXTENTS) {
			kfree(ns->projid_map.forward);
			kfree(ns->projid_map.reverse);
		}
		retire_userns_sysctls(ns);
		key_free_user_ns(ns);
		ns_free_inum(&ns->ns);
		kmem_cache_free(user_ns_cachep, ns);
		dec_user_namespaces(ucounts);
		ns = parent;
	} while (refcount_dec_and_test(&parent->ns.count));
}

void __put_user_ns(struct user_namespace *ns)
{
	schedule_work(&ns->work);
}
EXPORT_SYMBOL(__put_user_ns);

/**
 * idmap_key struct holds the information necessary to find an idmapping in a
 * sorted idmap array. It is passed to cmp_map_id() as first argument.
 */
struct idmap_key {
	bool map_up; /* true  -> id from kid; false -> kid from id */
	u32 id; /* id to find */
	u32 count; /* == 0 unless used with map_id_range_down() */
};

/**
 * cmp_map_id - Function to be passed to bsearch() to find the requested
 * idmapping. Expects struct idmap_key to be passed via @k.
 */
static int cmp_map_id(const void *k, const void *e)
{
	u32 first, last, id2;
	const struct idmap_key *key = k;
	const struct uid_gid_extent *el = e;

	id2 = key->id + key->count - 1;

	/* handle map_id_{down,up}() */
	if (key->map_up)
		first = el->lower_first;
	else
		first = el->first;

	last = first + el->count - 1;

	if (key->id >= first && key->id <= last &&
	    (id2 >= first && id2 <= last))
		return 0;

	if (key->id < first || id2 < first)
		return -1;

	return 1;
}

/**
 * map_id_range_down_max - Find idmap via binary search in ordered idmap array.
 * Can only be called if number of mappings exceeds UID_GID_MAP_MAX_BASE_EXTENTS.
 */
static struct uid_gid_extent *
map_id_range_down_max(unsigned extents, struct uid_gid_map *map, u32 id, u32 count)
{
	struct idmap_key key;

	key.map_up = false;
	key.count = count;
	key.id = id;

	return bsearch(&key, map->forward, extents,
		       sizeof(struct uid_gid_extent), cmp_map_id);
}

/**
 * map_id_range_down_base - Find idmap via binary search in static extent array.
 * Can only be called if number of mappings is equal or less than
 * UID_GID_MAP_MAX_BASE_EXTENTS.
 */
static struct uid_gid_extent *
map_id_range_down_base(unsigned extents, struct uid_gid_map *map, u32 id, u32 count)
{
	unsigned idx;
	u32 first, last, id2;

	id2 = id + count - 1;

	/* Find the matching extent */
	for (idx = 0; idx < extents; idx++) {
		first = map->extent[idx].first;
		last = first + map->extent[idx].count - 1;
		if (id >= first && id <= last &&
		    (id2 >= first && id2 <= last))
			return &map->extent[idx];
	}
	return NULL;
}

static u32 map_id_range_down(struct uid_gid_map *map, u32 id, u32 count)
{
	struct uid_gid_extent *extent;
	unsigned extents = map->nr_extents;
	smp_rmb();

	if (extents <= UID_GID_MAP_MAX_BASE_EXTENTS)
		extent = map_id_range_down_base(extents, map, id, count);
	else
		extent = map_id_range_down_max(extents, map, id, count);

	/* Map the id or note failure */
	if (extent)
		id = (id - extent->first) + extent->lower_first;
	else
		id = (u32) -1;

	return id;
}

static u32 map_id_down(struct uid_gid_map *map, u32 id)
{
	return map_id_range_down(map, id, 1);
}

/**
 * map_id_up_base - Find idmap via binary search in static extent array.
 * Can only be called if number of mappings is equal or less than
 * UID_GID_MAP_MAX_BASE_EXTENTS.
 */
static struct uid_gid_extent *
map_id_up_base(unsigned extents, struct uid_gid_map *map, u32 id)
{
	unsigned idx;
	u32 first, last;

	/* Find the matching extent */
	for (idx = 0; idx < extents; idx++) {
		first = map->extent[idx].lower_first;
		last = first + map->extent[idx].count - 1;
		if (id >= first && id <= last)
			return &map->extent[idx];
	}
	return NULL;
}

/**
 * map_id_up_max - Find idmap via binary search in ordered idmap array.
 * Can only be called if number of mappings exceeds UID_GID_MAP_MAX_BASE_EXTENTS.
 */
static struct uid_gid_extent *
map_id_up_max(unsigned extents, struct uid_gid_map *map, u32 id)
{
	struct idmap_key key;

	key.map_up = true;
	key.count = 1;
	key.id = id;

	return bsearch(&key, map->reverse, extents,
		       sizeof(struct uid_gid_extent), cmp_map_id);
}

static u32 map_id_up(struct uid_gid_map *map, u32 id)
{
	struct uid_gid_extent *extent;
	unsigned extents = map->nr_extents;
	smp_rmb();

	if (extents <= UID_GID_MAP_MAX_BASE_EXTENTS)
		extent = map_id_up_base(extents, map, id);
	else
		extent = map_id_up_max(extents, map, id);

	/* Map the id or note failure */
	if (extent)
		id = (id - extent->lower_first) + extent->first;
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
 *	for and handle INVALID_UID being returned.  INVALID_UID
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
 *	@gid: group identifier
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
 *	for and handle INVALID_PROJID being returned.  INVALID_PROJID
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

	lower_ns = seq_user_ns(seq);
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

	lower_ns = seq_user_ns(seq);
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

static void *m_start(struct seq_file *seq, loff_t *ppos,
		     struct uid_gid_map *map)
{
	loff_t pos = *ppos;
	unsigned extents = map->nr_extents;
	smp_rmb();

	if (pos >= extents)
		return NULL;

	if (extents <= UID_GID_MAP_MAX_BASE_EXTENTS)
		return &map->extent[pos];

	return &map->forward[pos];
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

const struct seq_operations proc_uid_seq_operations = {
	.start = uid_m_start,
	.stop = m_stop,
	.next = m_next,
	.show = uid_m_show,
};

const struct seq_operations proc_gid_seq_operations = {
	.start = gid_m_start,
	.stop = m_stop,
	.next = m_next,
	.show = gid_m_show,
};

const struct seq_operations proc_projid_seq_operations = {
	.start = projid_m_start,
	.stop = m_stop,
	.next = m_next,
	.show = projid_m_show,
};

static bool mappings_overlap(struct uid_gid_map *new_map,
			     struct uid_gid_extent *extent)
{
	u32 upper_first, lower_first, upper_last, lower_last;
	unsigned idx;

	upper_first = extent->first;
	lower_first = extent->lower_first;
	upper_last = upper_first + extent->count - 1;
	lower_last = lower_first + extent->count - 1;

	for (idx = 0; idx < new_map->nr_extents; idx++) {
		u32 prev_upper_first, prev_lower_first;
		u32 prev_upper_last, prev_lower_last;
		struct uid_gid_extent *prev;

		if (new_map->nr_extents <= UID_GID_MAP_MAX_BASE_EXTENTS)
			prev = &new_map->extent[idx];
		else
			prev = &new_map->forward[idx];

		prev_upper_first = prev->first;
		prev_lower_first = prev->lower_first;
		prev_upper_last = prev_upper_first + prev->count - 1;
		prev_lower_last = prev_lower_first + prev->count - 1;

		/* Does the upper range intersect a previous extent? */
		if ((prev_upper_first <= upper_last) &&
		    (prev_upper_last >= upper_first))
			return true;

		/* Does the lower range intersect a previous extent? */
		if ((prev_lower_first <= lower_last) &&
		    (prev_lower_last >= lower_first))
			return true;
	}
	return false;
}

/**
 * insert_extent - Safely insert a new idmap extent into struct uid_gid_map.
 * Takes care to allocate a 4K block of memory if the number of mappings exceeds
 * UID_GID_MAP_MAX_BASE_EXTENTS.
 */
static int insert_extent(struct uid_gid_map *map, struct uid_gid_extent *extent)
{
	struct uid_gid_extent *dest;

	if (map->nr_extents == UID_GID_MAP_MAX_BASE_EXTENTS) {
		struct uid_gid_extent *forward;

		/* Allocate memory for 340 mappings. */
		forward = kmalloc_array(UID_GID_MAP_MAX_EXTENTS,
					sizeof(struct uid_gid_extent),
					GFP_KERNEL);
		if (!forward)
			return -ENOMEM;

		/* Copy over memory. Only set up memory for the forward pointer.
		 * Defer the memory setup for the reverse pointer.
		 */
		memcpy(forward, map->extent,
		       map->nr_extents * sizeof(map->extent[0]));

		map->forward = forward;
		map->reverse = NULL;
	}

	if (map->nr_extents < UID_GID_MAP_MAX_BASE_EXTENTS)
		dest = &map->extent[map->nr_extents];
	else
		dest = &map->forward[map->nr_extents];

	*dest = *extent;
	map->nr_extents++;
	return 0;
}

/* cmp function to sort() forward mappings */
static int cmp_extents_forward(const void *a, const void *b)
{
	const struct uid_gid_extent *e1 = a;
	const struct uid_gid_extent *e2 = b;

	if (e1->first < e2->first)
		return -1;

	if (e1->first > e2->first)
		return 1;

	return 0;
}

/* cmp function to sort() reverse mappings */
static int cmp_extents_reverse(const void *a, const void *b)
{
	const struct uid_gid_extent *e1 = a;
	const struct uid_gid_extent *e2 = b;

	if (e1->lower_first < e2->lower_first)
		return -1;

	if (e1->lower_first > e2->lower_first)
		return 1;

	return 0;
}

/**
 * sort_idmaps - Sorts an array of idmap entries.
 * Can only be called if number of mappings exceeds UID_GID_MAP_MAX_BASE_EXTENTS.
 */
static int sort_idmaps(struct uid_gid_map *map)
{
	if (map->nr_extents <= UID_GID_MAP_MAX_BASE_EXTENTS)
		return 0;

	/* Sort forward array. */
	sort(map->forward, map->nr_extents, sizeof(struct uid_gid_extent),
	     cmp_extents_forward, NULL);

	/* Only copy the memory from forward we actually need. */
	map->reverse = kmemdup(map->forward,
			       map->nr_extents * sizeof(struct uid_gid_extent),
			       GFP_KERNEL);
	if (!map->reverse)
		return -ENOMEM;

	/* Sort reverse array. */
	sort(map->reverse, map->nr_extents, sizeof(struct uid_gid_extent),
	     cmp_extents_reverse, NULL);

	return 0;
}

/**
 * verify_root_map() - check the uid 0 mapping
 * @file: idmapping file
 * @map_ns: user namespace of the target process
 * @new_map: requested idmap
 *
 * If a process requests mapping parent uid 0 into the new ns, verify that the
 * process writing the map had the CAP_SETFCAP capability as the target process
 * will be able to write fscaps that are valid in ancestor user namespaces.
 *
 * Return: true if the mapping is allowed, false if not.
 */
static bool verify_root_map(const struct file *file,
			    struct user_namespace *map_ns,
			    struct uid_gid_map *new_map)
{
	int idx;
	const struct user_namespace *file_ns = file->f_cred->user_ns;
	struct uid_gid_extent *extent0 = NULL;

	for (idx = 0; idx < new_map->nr_extents; idx++) {
		if (new_map->nr_extents <= UID_GID_MAP_MAX_BASE_EXTENTS)
			extent0 = &new_map->extent[idx];
		else
			extent0 = &new_map->forward[idx];
		if (extent0->lower_first == 0)
			break;

		extent0 = NULL;
	}

	if (!extent0)
		return true;

	if (map_ns == file_ns) {
		/* The process unshared its ns and is writing to its own
		 * /proc/self/uid_map.  User already has full capabilites in
		 * the new namespace.  Verify that the parent had CAP_SETFCAP
		 * when it unshared.
		 * */
		if (!file_ns->parent_could_setfcap)
			return false;
	} else {
		/* Process p1 is writing to uid_map of p2, who is in a child
		 * user namespace to p1's.  Verify that the opener of the map
		 * file has CAP_SETFCAP against the parent of the new map
		 * namespace */
		if (!file_ns_capable(file, map_ns->parent, CAP_SETFCAP))
			return false;
	}

	return true;
}

static ssize_t map_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos,
			 int cap_setid,
			 struct uid_gid_map *map,
			 struct uid_gid_map *parent_map)
{
	struct seq_file *seq = file->private_data;
	struct user_namespace *map_ns = seq->private;
	struct uid_gid_map new_map;
	unsigned idx;
	struct uid_gid_extent extent;
	char *kbuf = NULL, *pos, *next_line;
	ssize_t ret;

	/* Only allow < page size writes at the beginning of the file */
	if ((*ppos != 0) || (count >= PAGE_SIZE))
		return -EINVAL;

	/* Slurp in the user data */
	kbuf = memdup_user_nul(buf, count);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	/*
	 * The userns_state_mutex serializes all writes to any given map.
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
	 * order and smp_rmb() is guaranteed that we don't have crazy
	 * architectures returning stale data.
	 */
	mutex_lock(&userns_state_mutex);

	memset(&new_map, 0, sizeof(struct uid_gid_map));

	ret = -EPERM;
	/* Only allow one successful write to the map */
	if (map->nr_extents != 0)
		goto out;

	/*
	 * Adjusting namespace settings requires capabilities on the target.
	 */
	if (cap_valid(cap_setid) && !file_ns_capable(file, map_ns, CAP_SYS_ADMIN))
		goto out;

	/* Parse the user data */
	ret = -EINVAL;
	pos = kbuf;
	for (; pos; pos = next_line) {

		/* Find the end of line and ensure I don't look past it */
		next_line = strchr(pos, '\n');
		if (next_line) {
			*next_line = '\0';
			next_line++;
			if (*next_line == '\0')
				next_line = NULL;
		}

		pos = skip_spaces(pos);
		extent.first = simple_strtoul(pos, &pos, 10);
		if (!isspace(*pos))
			goto out;

		pos = skip_spaces(pos);
		extent.lower_first = simple_strtoul(pos, &pos, 10);
		if (!isspace(*pos))
			goto out;

		pos = skip_spaces(pos);
		extent.count = simple_strtoul(pos, &pos, 10);
		if (*pos && !isspace(*pos))
			goto out;

		/* Verify there is not trailing junk on the line */
		pos = skip_spaces(pos);
		if (*pos != '\0')
			goto out;

		/* Verify we have been given valid starting values */
		if ((extent.first == (u32) -1) ||
		    (extent.lower_first == (u32) -1))
			goto out;

		/* Verify count is not zero and does not cause the
		 * extent to wrap
		 */
		if ((extent.first + extent.count) <= extent.first)
			goto out;
		if ((extent.lower_first + extent.count) <=
		     extent.lower_first)
			goto out;

		/* Do the ranges in extent overlap any previous extents? */
		if (mappings_overlap(&new_map, &extent))
			goto out;

		if ((new_map.nr_extents + 1) == UID_GID_MAP_MAX_EXTENTS &&
		    (next_line != NULL))
			goto out;

		ret = insert_extent(&new_map, &extent);
		if (ret < 0)
			goto out;
		ret = -EINVAL;
	}
	/* Be very certaint the new map actually exists */
	if (new_map.nr_extents == 0)
		goto out;

	ret = -EPERM;
	/* Validate the user is allowed to use user id's mapped to. */
	if (!new_idmap_permitted(file, map_ns, cap_setid, &new_map))
		goto out;

	ret = -EPERM;
	/* Map the lower ids from the parent user namespace to the
	 * kernel global id space.
	 */
	for (idx = 0; idx < new_map.nr_extents; idx++) {
		struct uid_gid_extent *e;
		u32 lower_first;

		if (new_map.nr_extents <= UID_GID_MAP_MAX_BASE_EXTENTS)
			e = &new_map.extent[idx];
		else
			e = &new_map.forward[idx];

		lower_first = map_id_range_down(parent_map,
						e->lower_first,
						e->count);

		/* Fail if we can not map the specified extent to
		 * the kernel global id space.
		 */
		if (lower_first == (u32) -1)
			goto out;

		e->lower_first = lower_first;
	}

	/*
	 * If we want to use binary search for lookup, this clones the extent
	 * array and sorts both copies.
	 */
	ret = sort_idmaps(&new_map);
	if (ret < 0)
		goto out;

	/* Install the map */
	if (new_map.nr_extents <= UID_GID_MAP_MAX_BASE_EXTENTS) {
		memcpy(map->extent, new_map.extent,
		       new_map.nr_extents * sizeof(new_map.extent[0]));
	} else {
		map->forward = new_map.forward;
		map->reverse = new_map.reverse;
	}
	smp_wmb();
	map->nr_extents = new_map.nr_extents;

	*ppos = count;
	ret = count;
out:
	if (ret < 0 && new_map.nr_extents > UID_GID_MAP_MAX_BASE_EXTENTS) {
		kfree(new_map.forward);
		kfree(new_map.reverse);
		map->forward = NULL;
		map->reverse = NULL;
		map->nr_extents = 0;
	}

	mutex_unlock(&userns_state_mutex);
	kfree(kbuf);
	return ret;
}

ssize_t proc_uid_map_write(struct file *file, const char __user *buf,
			   size_t size, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct user_namespace *ns = seq->private;
	struct user_namespace *seq_ns = seq_user_ns(seq);

	if (!ns->parent)
		return -EPERM;

	if ((seq_ns != ns) && (seq_ns != ns->parent))
		return -EPERM;

	return map_write(file, buf, size, ppos, CAP_SETUID,
			 &ns->uid_map, &ns->parent->uid_map);
}

ssize_t proc_gid_map_write(struct file *file, const char __user *buf,
			   size_t size, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct user_namespace *ns = seq->private;
	struct user_namespace *seq_ns = seq_user_ns(seq);

	if (!ns->parent)
		return -EPERM;

	if ((seq_ns != ns) && (seq_ns != ns->parent))
		return -EPERM;

	return map_write(file, buf, size, ppos, CAP_SETGID,
			 &ns->gid_map, &ns->parent->gid_map);
}

ssize_t proc_projid_map_write(struct file *file, const char __user *buf,
			      size_t size, loff_t *ppos)
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

static bool new_idmap_permitted(const struct file *file,
				struct user_namespace *ns, int cap_setid,
				struct uid_gid_map *new_map)
{
	const struct cred *cred = file->f_cred;

	if (cap_setid == CAP_SETUID && !verify_root_map(file, ns, new_map))
		return false;

	/* Don't allow mappings that would allow anything that wouldn't
	 * be allowed without the establishment of unprivileged mappings.
	 */
	if ((new_map->nr_extents == 1) && (new_map->extent[0].count == 1) &&
	    uid_eq(ns->owner, cred->euid)) {
		u32 id = new_map->extent[0].lower_first;
		if (cap_setid == CAP_SETUID) {
			kuid_t uid = make_kuid(ns->parent, id);
			if (uid_eq(uid, cred->euid))
				return true;
		} else if (cap_setid == CAP_SETGID) {
			kgid_t gid = make_kgid(ns->parent, id);
			if (!(ns->flags & USERNS_SETGROUPS_ALLOWED) &&
			    gid_eq(gid, cred->egid))
				return true;
		}
	}

	/* Allow anyone to set a mapping that doesn't require privilege */
	if (!cap_valid(cap_setid))
		return true;

	/* Allow the specified ids if we have the appropriate capability
	 * (CAP_SETUID or CAP_SETGID) over the parent user namespace.
	 * And the opener of the id file also had the approprpiate capability.
	 */
	if (ns_capable(ns->parent, cap_setid) &&
	    file_ns_capable(file, ns->parent, cap_setid))
		return true;

	return false;
}

int proc_setgroups_show(struct seq_file *seq, void *v)
{
	struct user_namespace *ns = seq->private;
	unsigned long userns_flags = READ_ONCE(ns->flags);

	seq_printf(seq, "%s\n",
		   (userns_flags & USERNS_SETGROUPS_ALLOWED) ?
		   "allow" : "deny");
	return 0;
}

ssize_t proc_setgroups_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	struct user_namespace *ns = seq->private;
	char kbuf[8], *pos;
	bool setgroups_allowed;
	ssize_t ret;

	/* Only allow a very narrow range of strings to be written */
	ret = -EINVAL;
	if ((*ppos != 0) || (count >= sizeof(kbuf)))
		goto out;

	/* What was written? */
	ret = -EFAULT;
	if (copy_from_user(kbuf, buf, count))
		goto out;
	kbuf[count] = '\0';
	pos = kbuf;

	/* What is being requested? */
	ret = -EINVAL;
	if (strncmp(pos, "allow", 5) == 0) {
		pos += 5;
		setgroups_allowed = true;
	}
	else if (strncmp(pos, "deny", 4) == 0) {
		pos += 4;
		setgroups_allowed = false;
	}
	else
		goto out;

	/* Verify there is not trailing junk on the line */
	pos = skip_spaces(pos);
	if (*pos != '\0')
		goto out;

	ret = -EPERM;
	mutex_lock(&userns_state_mutex);
	if (setgroups_allowed) {
		/* Enabling setgroups after setgroups has been disabled
		 * is not allowed.
		 */
		if (!(ns->flags & USERNS_SETGROUPS_ALLOWED))
			goto out_unlock;
	} else {
		/* Permanently disabling setgroups after setgroups has
		 * been enabled by writing the gid_map is not allowed.
		 */
		if (ns->gid_map.nr_extents != 0)
			goto out_unlock;
		ns->flags &= ~USERNS_SETGROUPS_ALLOWED;
	}
	mutex_unlock(&userns_state_mutex);

	/* Report a successful write */
	*ppos = count;
	ret = count;
out:
	return ret;
out_unlock:
	mutex_unlock(&userns_state_mutex);
	goto out;
}

bool userns_may_setgroups(const struct user_namespace *ns)
{
	bool allowed;

	mutex_lock(&userns_state_mutex);
	/* It is not safe to use setgroups until a gid mapping in
	 * the user namespace has been established.
	 */
	allowed = ns->gid_map.nr_extents != 0;
	/* Is setgroups allowed? */
	allowed = allowed && (ns->flags & USERNS_SETGROUPS_ALLOWED);
	mutex_unlock(&userns_state_mutex);

	return allowed;
}

/*
 * Returns true if @child is the same namespace or a descendant of
 * @ancestor.
 */
bool in_userns(const struct user_namespace *ancestor,
	       const struct user_namespace *child)
{
	const struct user_namespace *ns;
	for (ns = child; ns->level > ancestor->level; ns = ns->parent)
		;
	return (ns == ancestor);
}

bool current_in_userns(const struct user_namespace *target_ns)
{
	return in_userns(target_ns, current_user_ns());
}
EXPORT_SYMBOL(current_in_userns);

static inline struct user_namespace *to_user_ns(struct ns_common *ns)
{
	return container_of(ns, struct user_namespace, ns);
}

static struct ns_common *userns_get(struct task_struct *task)
{
	struct user_namespace *user_ns;

	rcu_read_lock();
	user_ns = get_user_ns(__task_cred(task)->user_ns);
	rcu_read_unlock();

	return user_ns ? &user_ns->ns : NULL;
}

static void userns_put(struct ns_common *ns)
{
	put_user_ns(to_user_ns(ns));
}

static int userns_install(struct nsset *nsset, struct ns_common *ns)
{
	struct user_namespace *user_ns = to_user_ns(ns);
	struct cred *cred;

	/* Don't allow gaining capabilities by reentering
	 * the same user namespace.
	 */
	if (user_ns == current_user_ns())
		return -EINVAL;

	/* Tasks that share a thread group must share a user namespace */
	if (!thread_group_empty(current))
		return -EINVAL;

	if (current->fs->users != 1)
		return -EINVAL;

	if (!ns_capable(user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	cred = nsset_cred(nsset);
	if (!cred)
		return -EINVAL;

	put_user_ns(cred->user_ns);
	set_cred_user_ns(cred, get_user_ns(user_ns));

	return 0;
}

struct ns_common *ns_get_owner(struct ns_common *ns)
{
	struct user_namespace *my_user_ns = current_user_ns();
	struct user_namespace *owner, *p;

	/* See if the owner is in the current user namespace */
	owner = p = ns->ops->owner(ns);
	for (;;) {
		if (!p)
			return ERR_PTR(-EPERM);
		if (p == my_user_ns)
			break;
		p = p->parent;
	}

	return &get_user_ns(owner)->ns;
}

static struct user_namespace *userns_owner(struct ns_common *ns)
{
	return to_user_ns(ns)->parent;
}

const struct proc_ns_operations userns_operations = {
	.name		= "user",
	.type		= CLONE_NEWUSER,
	.get		= userns_get,
	.put		= userns_put,
	.install	= userns_install,
	.owner		= userns_owner,
	.get_parent	= ns_get_owner,
};

static __init int user_namespaces_init(void)
{
	user_ns_cachep = KMEM_CACHE(user_namespace, SLAB_PANIC);
	return 0;
}
subsys_initcall(user_namespaces_init);
