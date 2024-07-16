// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock LSM - Filesystem management and hooks
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2021-2022 Microsoft Corporation
 */

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/compiler_types.h>
#include <linux/dcache.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/lsm_hooks.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/wait_bit.h>
#include <linux/workqueue.h>
#include <uapi/linux/landlock.h>

#include "common.h"
#include "cred.h"
#include "fs.h"
#include "limits.h"
#include "object.h"
#include "ruleset.h"
#include "setup.h"

/* Underlying object management */

static void release_inode(struct landlock_object *const object)
	__releases(object->lock)
{
	struct inode *const inode = object->underobj;
	struct super_block *sb;

	if (!inode) {
		spin_unlock(&object->lock);
		return;
	}

	/*
	 * Protects against concurrent use by hook_sb_delete() of the reference
	 * to the underlying inode.
	 */
	object->underobj = NULL;
	/*
	 * Makes sure that if the filesystem is concurrently unmounted,
	 * hook_sb_delete() will wait for us to finish iput().
	 */
	sb = inode->i_sb;
	atomic_long_inc(&landlock_superblock(sb)->inode_refs);
	spin_unlock(&object->lock);
	/*
	 * Because object->underobj was not NULL, hook_sb_delete() and
	 * get_inode_object() guarantee that it is safe to reset
	 * landlock_inode(inode)->object while it is not NULL.  It is therefore
	 * not necessary to lock inode->i_lock.
	 */
	rcu_assign_pointer(landlock_inode(inode)->object, NULL);
	/*
	 * Now, new rules can safely be tied to @inode with get_inode_object().
	 */

	iput(inode);
	if (atomic_long_dec_and_test(&landlock_superblock(sb)->inode_refs))
		wake_up_var(&landlock_superblock(sb)->inode_refs);
}

static const struct landlock_object_underops landlock_fs_underops = {
	.release = release_inode
};

/* Ruleset management */

static struct landlock_object *get_inode_object(struct inode *const inode)
{
	struct landlock_object *object, *new_object;
	struct landlock_inode_security *inode_sec = landlock_inode(inode);

	rcu_read_lock();
retry:
	object = rcu_dereference(inode_sec->object);
	if (object) {
		if (likely(refcount_inc_not_zero(&object->usage))) {
			rcu_read_unlock();
			return object;
		}
		/*
		 * We are racing with release_inode(), the object is going
		 * away.  Wait for release_inode(), then retry.
		 */
		spin_lock(&object->lock);
		spin_unlock(&object->lock);
		goto retry;
	}
	rcu_read_unlock();

	/*
	 * If there is no object tied to @inode, then create a new one (without
	 * holding any locks).
	 */
	new_object = landlock_create_object(&landlock_fs_underops, inode);
	if (IS_ERR(new_object))
		return new_object;

	/*
	 * Protects against concurrent calls to get_inode_object() or
	 * hook_sb_delete().
	 */
	spin_lock(&inode->i_lock);
	if (unlikely(rcu_access_pointer(inode_sec->object))) {
		/* Someone else just created the object, bail out and retry. */
		spin_unlock(&inode->i_lock);
		kfree(new_object);

		rcu_read_lock();
		goto retry;
	}

	/*
	 * @inode will be released by hook_sb_delete() on its superblock
	 * shutdown, or by release_inode() when no more ruleset references the
	 * related object.
	 */
	ihold(inode);
	rcu_assign_pointer(inode_sec->object, new_object);
	spin_unlock(&inode->i_lock);
	return new_object;
}

/* All access rights that can be tied to files. */
/* clang-format off */
#define ACCESS_FILE ( \
	LANDLOCK_ACCESS_FS_EXECUTE | \
	LANDLOCK_ACCESS_FS_WRITE_FILE | \
	LANDLOCK_ACCESS_FS_READ_FILE)
/* clang-format on */

/*
 * All access rights that are denied by default whether they are handled or not
 * by a ruleset/layer.  This must be ORed with all ruleset->fs_access_masks[]
 * entries when we need to get the absolute handled access masks.
 */
/* clang-format off */
#define ACCESS_INITIALLY_DENIED ( \
	LANDLOCK_ACCESS_FS_REFER)
/* clang-format on */

/*
 * @path: Should have been checked by get_path_from_fd().
 */
int landlock_append_fs_rule(struct landlock_ruleset *const ruleset,
			    const struct path *const path,
			    access_mask_t access_rights)
{
	int err;
	struct landlock_object *object;

	/* Files only get access rights that make sense. */
	if (!d_is_dir(path->dentry) &&
	    (access_rights | ACCESS_FILE) != ACCESS_FILE)
		return -EINVAL;
	if (WARN_ON_ONCE(ruleset->num_layers != 1))
		return -EINVAL;

	/* Transforms relative access rights to absolute ones. */
	access_rights |=
		LANDLOCK_MASK_ACCESS_FS &
		~(ruleset->fs_access_masks[0] | ACCESS_INITIALLY_DENIED);
	object = get_inode_object(d_backing_inode(path->dentry));
	if (IS_ERR(object))
		return PTR_ERR(object);
	mutex_lock(&ruleset->lock);
	err = landlock_insert_rule(ruleset, object, access_rights);
	mutex_unlock(&ruleset->lock);
	/*
	 * No need to check for an error because landlock_insert_rule()
	 * increments the refcount for the new object if needed.
	 */
	landlock_put_object(object);
	return err;
}

/* Access-control management */

/*
 * The lifetime of the returned rule is tied to @domain.
 *
 * Returns NULL if no rule is found or if @dentry is negative.
 */
static inline const struct landlock_rule *
find_rule(const struct landlock_ruleset *const domain,
	  const struct dentry *const dentry)
{
	const struct landlock_rule *rule;
	const struct inode *inode;

	/* Ignores nonexistent leafs. */
	if (d_is_negative(dentry))
		return NULL;

	inode = d_backing_inode(dentry);
	rcu_read_lock();
	rule = landlock_find_rule(
		domain, rcu_dereference(landlock_inode(inode)->object));
	rcu_read_unlock();
	return rule;
}

/*
 * @layer_masks is read and may be updated according to the access request and
 * the matching rule.
 *
 * Returns true if the request is allowed (i.e. relevant layer masks for the
 * request are empty).
 */
static inline bool
unmask_layers(const struct landlock_rule *const rule,
	      const access_mask_t access_request,
	      layer_mask_t (*const layer_masks)[LANDLOCK_NUM_ACCESS_FS])
{
	size_t layer_level;

	if (!access_request || !layer_masks)
		return true;
	if (!rule)
		return false;

	/*
	 * An access is granted if, for each policy layer, at least one rule
	 * encountered on the pathwalk grants the requested access,
	 * regardless of its position in the layer stack.  We must then check
	 * the remaining layers for each inode, from the first added layer to
	 * the last one.  When there is multiple requested accesses, for each
	 * policy layer, the full set of requested accesses may not be granted
	 * by only one rule, but by the union (binary OR) of multiple rules.
	 * E.g. /a/b <execute> + /a <read> => /a/b <execute + read>
	 */
	for (layer_level = 0; layer_level < rule->num_layers; layer_level++) {
		const struct landlock_layer *const layer =
			&rule->layers[layer_level];
		const layer_mask_t layer_bit = BIT_ULL(layer->level - 1);
		const unsigned long access_req = access_request;
		unsigned long access_bit;
		bool is_empty;

		/*
		 * Records in @layer_masks which layer grants access to each
		 * requested access.
		 */
		is_empty = true;
		for_each_set_bit(access_bit, &access_req,
				 ARRAY_SIZE(*layer_masks)) {
			if (layer->access & BIT_ULL(access_bit))
				(*layer_masks)[access_bit] &= ~layer_bit;
			is_empty = is_empty && !(*layer_masks)[access_bit];
		}
		if (is_empty)
			return true;
	}
	return false;
}

/*
 * Allows access to pseudo filesystems that will never be mountable (e.g.
 * sockfs, pipefs), but can still be reachable through
 * /proc/<pid>/fd/<file-descriptor>
 */
static inline bool is_nouser_or_private(const struct dentry *dentry)
{
	return (dentry->d_sb->s_flags & SB_NOUSER) ||
	       (d_is_positive(dentry) &&
		unlikely(IS_PRIVATE(d_backing_inode(dentry))));
}

static inline access_mask_t
get_handled_accesses(const struct landlock_ruleset *const domain)
{
	access_mask_t access_dom = ACCESS_INITIALLY_DENIED;
	size_t layer_level;

	for (layer_level = 0; layer_level < domain->num_layers; layer_level++)
		access_dom |= domain->fs_access_masks[layer_level];
	return access_dom & LANDLOCK_MASK_ACCESS_FS;
}

static inline access_mask_t
init_layer_masks(const struct landlock_ruleset *const domain,
		 const access_mask_t access_request,
		 layer_mask_t (*const layer_masks)[LANDLOCK_NUM_ACCESS_FS])
{
	access_mask_t handled_accesses = 0;
	size_t layer_level;

	memset(layer_masks, 0, sizeof(*layer_masks));
	/* An empty access request can happen because of O_WRONLY | O_RDWR. */
	if (!access_request)
		return 0;

	/* Saves all handled accesses per layer. */
	for (layer_level = 0; layer_level < domain->num_layers; layer_level++) {
		const unsigned long access_req = access_request;
		unsigned long access_bit;

		for_each_set_bit(access_bit, &access_req,
				 ARRAY_SIZE(*layer_masks)) {
			/*
			 * Artificially handles all initially denied by default
			 * access rights.
			 */
			if (BIT_ULL(access_bit) &
			    (domain->fs_access_masks[layer_level] |
			     ACCESS_INITIALLY_DENIED)) {
				(*layer_masks)[access_bit] |=
					BIT_ULL(layer_level);
				handled_accesses |= BIT_ULL(access_bit);
			}
		}
	}
	return handled_accesses;
}

/*
 * Check that a destination file hierarchy has more restrictions than a source
 * file hierarchy.  This is only used for link and rename actions.
 *
 * @layer_masks_child2: Optional child masks.
 */
static inline bool no_more_access(
	const layer_mask_t (*const layer_masks_parent1)[LANDLOCK_NUM_ACCESS_FS],
	const layer_mask_t (*const layer_masks_child1)[LANDLOCK_NUM_ACCESS_FS],
	const bool child1_is_directory,
	const layer_mask_t (*const layer_masks_parent2)[LANDLOCK_NUM_ACCESS_FS],
	const layer_mask_t (*const layer_masks_child2)[LANDLOCK_NUM_ACCESS_FS],
	const bool child2_is_directory)
{
	unsigned long access_bit;

	for (access_bit = 0; access_bit < ARRAY_SIZE(*layer_masks_parent2);
	     access_bit++) {
		/* Ignores accesses that only make sense for directories. */
		const bool is_file_access =
			!!(BIT_ULL(access_bit) & ACCESS_FILE);

		if (child1_is_directory || is_file_access) {
			/*
			 * Checks if the destination restrictions are a
			 * superset of the source ones (i.e. inherited access
			 * rights without child exceptions):
			 * restrictions(parent2) >= restrictions(child1)
			 */
			if ((((*layer_masks_parent1)[access_bit] &
			      (*layer_masks_child1)[access_bit]) |
			     (*layer_masks_parent2)[access_bit]) !=
			    (*layer_masks_parent2)[access_bit])
				return false;
		}

		if (!layer_masks_child2)
			continue;
		if (child2_is_directory || is_file_access) {
			/*
			 * Checks inverted restrictions for RENAME_EXCHANGE:
			 * restrictions(parent1) >= restrictions(child2)
			 */
			if ((((*layer_masks_parent2)[access_bit] &
			      (*layer_masks_child2)[access_bit]) |
			     (*layer_masks_parent1)[access_bit]) !=
			    (*layer_masks_parent1)[access_bit])
				return false;
		}
	}
	return true;
}

/*
 * Removes @layer_masks accesses that are not requested.
 *
 * Returns true if the request is allowed, false otherwise.
 */
static inline bool
scope_to_request(const access_mask_t access_request,
		 layer_mask_t (*const layer_masks)[LANDLOCK_NUM_ACCESS_FS])
{
	const unsigned long access_req = access_request;
	unsigned long access_bit;

	if (WARN_ON_ONCE(!layer_masks))
		return true;

	for_each_clear_bit(access_bit, &access_req, ARRAY_SIZE(*layer_masks))
		(*layer_masks)[access_bit] = 0;
	return !memchr_inv(layer_masks, 0, sizeof(*layer_masks));
}

/*
 * Returns true if there is at least one access right different than
 * LANDLOCK_ACCESS_FS_REFER.
 */
static inline bool
is_eacces(const layer_mask_t (*const layer_masks)[LANDLOCK_NUM_ACCESS_FS],
	  const access_mask_t access_request)
{
	unsigned long access_bit;
	/* LANDLOCK_ACCESS_FS_REFER alone must return -EXDEV. */
	const unsigned long access_check = access_request &
					   ~LANDLOCK_ACCESS_FS_REFER;

	if (!layer_masks)
		return false;

	for_each_set_bit(access_bit, &access_check, ARRAY_SIZE(*layer_masks)) {
		if ((*layer_masks)[access_bit])
			return true;
	}
	return false;
}

/**
 * check_access_path_dual - Check accesses for requests with a common path
 *
 * @domain: Domain to check against.
 * @path: File hierarchy to walk through.
 * @access_request_parent1: Accesses to check, once @layer_masks_parent1 is
 *     equal to @layer_masks_parent2 (if any).  This is tied to the unique
 *     requested path for most actions, or the source in case of a refer action
 *     (i.e. rename or link), or the source and destination in case of
 *     RENAME_EXCHANGE.
 * @layer_masks_parent1: Pointer to a matrix of layer masks per access
 *     masks, identifying the layers that forbid a specific access.  Bits from
 *     this matrix can be unset according to the @path walk.  An empty matrix
 *     means that @domain allows all possible Landlock accesses (i.e. not only
 *     those identified by @access_request_parent1).  This matrix can
 *     initially refer to domain layer masks and, when the accesses for the
 *     destination and source are the same, to requested layer masks.
 * @dentry_child1: Dentry to the initial child of the parent1 path.  This
 *     pointer must be NULL for non-refer actions (i.e. not link nor rename).
 * @access_request_parent2: Similar to @access_request_parent1 but for a
 *     request involving a source and a destination.  This refers to the
 *     destination, except in case of RENAME_EXCHANGE where it also refers to
 *     the source.  Must be set to 0 when using a simple path request.
 * @layer_masks_parent2: Similar to @layer_masks_parent1 but for a refer
 *     action.  This must be NULL otherwise.
 * @dentry_child2: Dentry to the initial child of the parent2 path.  This
 *     pointer is only set for RENAME_EXCHANGE actions and must be NULL
 *     otherwise.
 *
 * This helper first checks that the destination has a superset of restrictions
 * compared to the source (if any) for a common path.  Because of
 * RENAME_EXCHANGE actions, source and destinations may be swapped.  It then
 * checks that the collected accesses and the remaining ones are enough to
 * allow the request.
 *
 * Returns:
 * - 0 if the access request is granted;
 * - -EACCES if it is denied because of access right other than
 *   LANDLOCK_ACCESS_FS_REFER;
 * - -EXDEV if the renaming or linking would be a privileged escalation
 *   (according to each layered policies), or if LANDLOCK_ACCESS_FS_REFER is
 *   not allowed by the source or the destination.
 */
static int check_access_path_dual(
	const struct landlock_ruleset *const domain,
	const struct path *const path,
	const access_mask_t access_request_parent1,
	layer_mask_t (*const layer_masks_parent1)[LANDLOCK_NUM_ACCESS_FS],
	const struct dentry *const dentry_child1,
	const access_mask_t access_request_parent2,
	layer_mask_t (*const layer_masks_parent2)[LANDLOCK_NUM_ACCESS_FS],
	const struct dentry *const dentry_child2)
{
	bool allowed_parent1 = false, allowed_parent2 = false, is_dom_check,
	     child1_is_directory = true, child2_is_directory = true;
	struct path walker_path;
	access_mask_t access_masked_parent1, access_masked_parent2;
	layer_mask_t _layer_masks_child1[LANDLOCK_NUM_ACCESS_FS],
		_layer_masks_child2[LANDLOCK_NUM_ACCESS_FS];
	layer_mask_t(*layer_masks_child1)[LANDLOCK_NUM_ACCESS_FS] = NULL,
	(*layer_masks_child2)[LANDLOCK_NUM_ACCESS_FS] = NULL;

	if (!access_request_parent1 && !access_request_parent2)
		return 0;
	if (WARN_ON_ONCE(!domain || !path))
		return 0;
	if (is_nouser_or_private(path->dentry))
		return 0;
	if (WARN_ON_ONCE(domain->num_layers < 1 || !layer_masks_parent1))
		return -EACCES;

	if (unlikely(layer_masks_parent2)) {
		if (WARN_ON_ONCE(!dentry_child1))
			return -EACCES;
		/*
		 * For a double request, first check for potential privilege
		 * escalation by looking at domain handled accesses (which are
		 * a superset of the meaningful requested accesses).
		 */
		access_masked_parent1 = access_masked_parent2 =
			get_handled_accesses(domain);
		is_dom_check = true;
	} else {
		if (WARN_ON_ONCE(dentry_child1 || dentry_child2))
			return -EACCES;
		/* For a simple request, only check for requested accesses. */
		access_masked_parent1 = access_request_parent1;
		access_masked_parent2 = access_request_parent2;
		is_dom_check = false;
	}

	if (unlikely(dentry_child1)) {
		unmask_layers(find_rule(domain, dentry_child1),
			      init_layer_masks(domain, LANDLOCK_MASK_ACCESS_FS,
					       &_layer_masks_child1),
			      &_layer_masks_child1);
		layer_masks_child1 = &_layer_masks_child1;
		child1_is_directory = d_is_dir(dentry_child1);
	}
	if (unlikely(dentry_child2)) {
		unmask_layers(find_rule(domain, dentry_child2),
			      init_layer_masks(domain, LANDLOCK_MASK_ACCESS_FS,
					       &_layer_masks_child2),
			      &_layer_masks_child2);
		layer_masks_child2 = &_layer_masks_child2;
		child2_is_directory = d_is_dir(dentry_child2);
	}

	walker_path = *path;
	path_get(&walker_path);
	/*
	 * We need to walk through all the hierarchy to not miss any relevant
	 * restriction.
	 */
	while (true) {
		struct dentry *parent_dentry;
		const struct landlock_rule *rule;

		/*
		 * If at least all accesses allowed on the destination are
		 * already allowed on the source, respectively if there is at
		 * least as much as restrictions on the destination than on the
		 * source, then we can safely refer files from the source to
		 * the destination without risking a privilege escalation.
		 * This also applies in the case of RENAME_EXCHANGE, which
		 * implies checks on both direction.  This is crucial for
		 * standalone multilayered security policies.  Furthermore,
		 * this helps avoid policy writers to shoot themselves in the
		 * foot.
		 */
		if (unlikely(is_dom_check &&
			     no_more_access(
				     layer_masks_parent1, layer_masks_child1,
				     child1_is_directory, layer_masks_parent2,
				     layer_masks_child2,
				     child2_is_directory))) {
			allowed_parent1 = scope_to_request(
				access_request_parent1, layer_masks_parent1);
			allowed_parent2 = scope_to_request(
				access_request_parent2, layer_masks_parent2);

			/* Stops when all accesses are granted. */
			if (allowed_parent1 && allowed_parent2)
				break;

			/*
			 * Now, downgrades the remaining checks from domain
			 * handled accesses to requested accesses.
			 */
			is_dom_check = false;
			access_masked_parent1 = access_request_parent1;
			access_masked_parent2 = access_request_parent2;
		}

		rule = find_rule(domain, walker_path.dentry);
		allowed_parent1 = unmask_layers(rule, access_masked_parent1,
						layer_masks_parent1);
		allowed_parent2 = unmask_layers(rule, access_masked_parent2,
						layer_masks_parent2);

		/* Stops when a rule from each layer grants access. */
		if (allowed_parent1 && allowed_parent2)
			break;

jump_up:
		if (walker_path.dentry == walker_path.mnt->mnt_root) {
			if (follow_up(&walker_path)) {
				/* Ignores hidden mount points. */
				goto jump_up;
			} else {
				/*
				 * Stops at the real root.  Denies access
				 * because not all layers have granted access.
				 */
				break;
			}
		}
		if (unlikely(IS_ROOT(walker_path.dentry))) {
			/*
			 * Stops at disconnected root directories.  Only allows
			 * access to internal filesystems (e.g. nsfs, which is
			 * reachable through /proc/<pid>/ns/<namespace>).
			 */
			allowed_parent1 = allowed_parent2 =
				!!(walker_path.mnt->mnt_flags & MNT_INTERNAL);
			break;
		}
		parent_dentry = dget_parent(walker_path.dentry);
		dput(walker_path.dentry);
		walker_path.dentry = parent_dentry;
	}
	path_put(&walker_path);

	if (allowed_parent1 && allowed_parent2)
		return 0;

	/*
	 * This prioritizes EACCES over EXDEV for all actions, including
	 * renames with RENAME_EXCHANGE.
	 */
	if (likely(is_eacces(layer_masks_parent1, access_request_parent1) ||
		   is_eacces(layer_masks_parent2, access_request_parent2)))
		return -EACCES;

	/*
	 * Gracefully forbids reparenting if the destination directory
	 * hierarchy is not a superset of restrictions of the source directory
	 * hierarchy, or if LANDLOCK_ACCESS_FS_REFER is not allowed by the
	 * source or the destination.
	 */
	return -EXDEV;
}

static inline int check_access_path(const struct landlock_ruleset *const domain,
				    const struct path *const path,
				    access_mask_t access_request)
{
	layer_mask_t layer_masks[LANDLOCK_NUM_ACCESS_FS] = {};

	access_request = init_layer_masks(domain, access_request, &layer_masks);
	return check_access_path_dual(domain, path, access_request,
				      &layer_masks, NULL, 0, NULL, NULL);
}

static inline int current_check_access_path(const struct path *const path,
					    const access_mask_t access_request)
{
	const struct landlock_ruleset *const dom =
		landlock_get_current_domain();

	if (!dom)
		return 0;
	return check_access_path(dom, path, access_request);
}

static inline access_mask_t get_mode_access(const umode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFLNK:
		return LANDLOCK_ACCESS_FS_MAKE_SYM;
	case 0:
		/* A zero mode translates to S_IFREG. */
	case S_IFREG:
		return LANDLOCK_ACCESS_FS_MAKE_REG;
	case S_IFDIR:
		return LANDLOCK_ACCESS_FS_MAKE_DIR;
	case S_IFCHR:
		return LANDLOCK_ACCESS_FS_MAKE_CHAR;
	case S_IFBLK:
		return LANDLOCK_ACCESS_FS_MAKE_BLOCK;
	case S_IFIFO:
		return LANDLOCK_ACCESS_FS_MAKE_FIFO;
	case S_IFSOCK:
		return LANDLOCK_ACCESS_FS_MAKE_SOCK;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

static inline access_mask_t maybe_remove(const struct dentry *const dentry)
{
	if (d_is_negative(dentry))
		return 0;
	return d_is_dir(dentry) ? LANDLOCK_ACCESS_FS_REMOVE_DIR :
				  LANDLOCK_ACCESS_FS_REMOVE_FILE;
}

/**
 * collect_domain_accesses - Walk through a file path and collect accesses
 *
 * @domain: Domain to check against.
 * @mnt_root: Last directory to check.
 * @dir: Directory to start the walk from.
 * @layer_masks_dom: Where to store the collected accesses.
 *
 * This helper is useful to begin a path walk from the @dir directory to a
 * @mnt_root directory used as a mount point.  This mount point is the common
 * ancestor between the source and the destination of a renamed and linked
 * file.  While walking from @dir to @mnt_root, we record all the domain's
 * allowed accesses in @layer_masks_dom.
 *
 * This is similar to check_access_path_dual() but much simpler because it only
 * handles walking on the same mount point and only checks one set of accesses.
 *
 * Returns:
 * - true if all the domain access rights are allowed for @dir;
 * - false if the walk reached @mnt_root.
 */
static bool collect_domain_accesses(
	const struct landlock_ruleset *const domain,
	const struct dentry *const mnt_root, struct dentry *dir,
	layer_mask_t (*const layer_masks_dom)[LANDLOCK_NUM_ACCESS_FS])
{
	unsigned long access_dom;
	bool ret = false;

	if (WARN_ON_ONCE(!domain || !mnt_root || !dir || !layer_masks_dom))
		return true;
	if (is_nouser_or_private(dir))
		return true;

	access_dom = init_layer_masks(domain, LANDLOCK_MASK_ACCESS_FS,
				      layer_masks_dom);

	dget(dir);
	while (true) {
		struct dentry *parent_dentry;

		/* Gets all layers allowing all domain accesses. */
		if (unmask_layers(find_rule(domain, dir), access_dom,
				  layer_masks_dom)) {
			/*
			 * Stops when all handled accesses are allowed by at
			 * least one rule in each layer.
			 */
			ret = true;
			break;
		}

		/* We should not reach a root other than @mnt_root. */
		if (dir == mnt_root || WARN_ON_ONCE(IS_ROOT(dir)))
			break;

		parent_dentry = dget_parent(dir);
		dput(dir);
		dir = parent_dentry;
	}
	dput(dir);
	return ret;
}

/**
 * current_check_refer_path - Check if a rename or link action is allowed
 *
 * @old_dentry: File or directory requested to be moved or linked.
 * @new_dir: Destination parent directory.
 * @new_dentry: Destination file or directory.
 * @removable: Sets to true if it is a rename operation.
 * @exchange: Sets to true if it is a rename operation with RENAME_EXCHANGE.
 *
 * Because of its unprivileged constraints, Landlock relies on file hierarchies
 * (and not only inodes) to tie access rights to files.  Being able to link or
 * rename a file hierarchy brings some challenges.  Indeed, moving or linking a
 * file (i.e. creating a new reference to an inode) can have an impact on the
 * actions allowed for a set of files if it would change its parent directory
 * (i.e. reparenting).
 *
 * To avoid trivial access right bypasses, Landlock first checks if the file or
 * directory requested to be moved would gain new access rights inherited from
 * its new hierarchy.  Before returning any error, Landlock then checks that
 * the parent source hierarchy and the destination hierarchy would allow the
 * link or rename action.  If it is not the case, an error with EACCES is
 * returned to inform user space that there is no way to remove or create the
 * requested source file type.  If it should be allowed but the new inherited
 * access rights would be greater than the source access rights, then the
 * kernel returns an error with EXDEV.  Prioritizing EACCES over EXDEV enables
 * user space to abort the whole operation if there is no way to do it, or to
 * manually copy the source to the destination if this remains allowed, e.g.
 * because file creation is allowed on the destination directory but not direct
 * linking.
 *
 * To achieve this goal, the kernel needs to compare two file hierarchies: the
 * one identifying the source file or directory (including itself), and the
 * destination one.  This can be seen as a multilayer partial ordering problem.
 * The kernel walks through these paths and collects in a matrix the access
 * rights that are denied per layer.  These matrices are then compared to see
 * if the destination one has more (or the same) restrictions as the source
 * one.  If this is the case, the requested action will not return EXDEV, which
 * doesn't mean the action is allowed.  The parent hierarchy of the source
 * (i.e. parent directory), and the destination hierarchy must also be checked
 * to verify that they explicitly allow such action (i.e.  referencing,
 * creation and potentially removal rights).  The kernel implementation is then
 * required to rely on potentially four matrices of access rights: one for the
 * source file or directory (i.e. the child), a potentially other one for the
 * other source/destination (in case of RENAME_EXCHANGE), one for the source
 * parent hierarchy and a last one for the destination hierarchy.  These
 * ephemeral matrices take some space on the stack, which limits the number of
 * layers to a deemed reasonable number: 16.
 *
 * Returns:
 * - 0 if access is allowed;
 * - -EXDEV if @old_dentry would inherit new access rights from @new_dir;
 * - -EACCES if file removal or creation is denied.
 */
static int current_check_refer_path(struct dentry *const old_dentry,
				    const struct path *const new_dir,
				    struct dentry *const new_dentry,
				    const bool removable, const bool exchange)
{
	const struct landlock_ruleset *const dom =
		landlock_get_current_domain();
	bool allow_parent1, allow_parent2;
	access_mask_t access_request_parent1, access_request_parent2;
	struct path mnt_dir;
	layer_mask_t layer_masks_parent1[LANDLOCK_NUM_ACCESS_FS] = {},
		     layer_masks_parent2[LANDLOCK_NUM_ACCESS_FS] = {};

	if (!dom)
		return 0;
	if (WARN_ON_ONCE(dom->num_layers < 1))
		return -EACCES;
	if (unlikely(d_is_negative(old_dentry)))
		return -ENOENT;
	if (exchange) {
		if (unlikely(d_is_negative(new_dentry)))
			return -ENOENT;
		access_request_parent1 =
			get_mode_access(d_backing_inode(new_dentry)->i_mode);
	} else {
		access_request_parent1 = 0;
	}
	access_request_parent2 =
		get_mode_access(d_backing_inode(old_dentry)->i_mode);
	if (removable) {
		access_request_parent1 |= maybe_remove(old_dentry);
		access_request_parent2 |= maybe_remove(new_dentry);
	}

	/* The mount points are the same for old and new paths, cf. EXDEV. */
	if (old_dentry->d_parent == new_dir->dentry) {
		/*
		 * The LANDLOCK_ACCESS_FS_REFER access right is not required
		 * for same-directory referer (i.e. no reparenting).
		 */
		access_request_parent1 = init_layer_masks(
			dom, access_request_parent1 | access_request_parent2,
			&layer_masks_parent1);
		return check_access_path_dual(dom, new_dir,
					      access_request_parent1,
					      &layer_masks_parent1, NULL, 0,
					      NULL, NULL);
	}

	access_request_parent1 |= LANDLOCK_ACCESS_FS_REFER;
	access_request_parent2 |= LANDLOCK_ACCESS_FS_REFER;

	/* Saves the common mount point. */
	mnt_dir.mnt = new_dir->mnt;
	mnt_dir.dentry = new_dir->mnt->mnt_root;

	/* new_dir->dentry is equal to new_dentry->d_parent */
	allow_parent1 = collect_domain_accesses(dom, mnt_dir.dentry,
						old_dentry->d_parent,
						&layer_masks_parent1);
	allow_parent2 = collect_domain_accesses(
		dom, mnt_dir.dentry, new_dir->dentry, &layer_masks_parent2);

	if (allow_parent1 && allow_parent2)
		return 0;

	/*
	 * To be able to compare source and destination domain access rights,
	 * take into account the @old_dentry access rights aggregated with its
	 * parent access rights.  This will be useful to compare with the
	 * destination parent access rights.
	 */
	return check_access_path_dual(dom, &mnt_dir, access_request_parent1,
				      &layer_masks_parent1, old_dentry,
				      access_request_parent2,
				      &layer_masks_parent2,
				      exchange ? new_dentry : NULL);
}

/* Inode hooks */

static void hook_inode_free_security(struct inode *const inode)
{
	/*
	 * All inodes must already have been untied from their object by
	 * release_inode() or hook_sb_delete().
	 */
	WARN_ON_ONCE(landlock_inode(inode)->object);
}

/* Super-block hooks */

/*
 * Release the inodes used in a security policy.
 *
 * Cf. fsnotify_unmount_inodes() and invalidate_inodes()
 */
static void hook_sb_delete(struct super_block *const sb)
{
	struct inode *inode, *prev_inode = NULL;

	if (!landlock_initialized)
		return;

	spin_lock(&sb->s_inode_list_lock);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		struct landlock_object *object;

		/* Only handles referenced inodes. */
		if (!atomic_read(&inode->i_count))
			continue;

		/*
		 * Protects against concurrent modification of inode (e.g.
		 * from get_inode_object()).
		 */
		spin_lock(&inode->i_lock);
		/*
		 * Checks I_FREEING and I_WILL_FREE  to protect against a race
		 * condition when release_inode() just called iput(), which
		 * could lead to a NULL dereference of inode->security or a
		 * second call to iput() for the same Landlock object.  Also
		 * checks I_NEW because such inode cannot be tied to an object.
		 */
		if (inode->i_state & (I_FREEING | I_WILL_FREE | I_NEW)) {
			spin_unlock(&inode->i_lock);
			continue;
		}

		rcu_read_lock();
		object = rcu_dereference(landlock_inode(inode)->object);
		if (!object) {
			rcu_read_unlock();
			spin_unlock(&inode->i_lock);
			continue;
		}
		/* Keeps a reference to this inode until the next loop walk. */
		__iget(inode);
		spin_unlock(&inode->i_lock);

		/*
		 * If there is no concurrent release_inode() ongoing, then we
		 * are in charge of calling iput() on this inode, otherwise we
		 * will just wait for it to finish.
		 */
		spin_lock(&object->lock);
		if (object->underobj == inode) {
			object->underobj = NULL;
			spin_unlock(&object->lock);
			rcu_read_unlock();

			/*
			 * Because object->underobj was not NULL,
			 * release_inode() and get_inode_object() guarantee
			 * that it is safe to reset
			 * landlock_inode(inode)->object while it is not NULL.
			 * It is therefore not necessary to lock inode->i_lock.
			 */
			rcu_assign_pointer(landlock_inode(inode)->object, NULL);
			/*
			 * At this point, we own the ihold() reference that was
			 * originally set up by get_inode_object() and the
			 * __iget() reference that we just set in this loop
			 * walk.  Therefore the following call to iput() will
			 * not sleep nor drop the inode because there is now at
			 * least two references to it.
			 */
			iput(inode);
		} else {
			spin_unlock(&object->lock);
			rcu_read_unlock();
		}

		if (prev_inode) {
			/*
			 * At this point, we still own the __iget() reference
			 * that we just set in this loop walk.  Therefore we
			 * can drop the list lock and know that the inode won't
			 * disappear from under us until the next loop walk.
			 */
			spin_unlock(&sb->s_inode_list_lock);
			/*
			 * We can now actually put the inode reference from the
			 * previous loop walk, which is not needed anymore.
			 */
			iput(prev_inode);
			cond_resched();
			spin_lock(&sb->s_inode_list_lock);
		}
		prev_inode = inode;
	}
	spin_unlock(&sb->s_inode_list_lock);

	/* Puts the inode reference from the last loop walk, if any. */
	if (prev_inode)
		iput(prev_inode);
	/* Waits for pending iput() in release_inode(). */
	wait_var_event(&landlock_superblock(sb)->inode_refs,
		       !atomic_long_read(&landlock_superblock(sb)->inode_refs));
}

/*
 * Because a Landlock security policy is defined according to the filesystem
 * topology (i.e. the mount namespace), changing it may grant access to files
 * not previously allowed.
 *
 * To make it simple, deny any filesystem topology modification by landlocked
 * processes.  Non-landlocked processes may still change the namespace of a
 * landlocked process, but this kind of threat must be handled by a system-wide
 * access-control security policy.
 *
 * This could be lifted in the future if Landlock can safely handle mount
 * namespace updates requested by a landlocked process.  Indeed, we could
 * update the current domain (which is currently read-only) by taking into
 * account the accesses of the source and the destination of a new mount point.
 * However, it would also require to make all the child domains dynamically
 * inherit these new constraints.  Anyway, for backward compatibility reasons,
 * a dedicated user space option would be required (e.g. as a ruleset flag).
 */
static int hook_sb_mount(const char *const dev_name,
			 const struct path *const path, const char *const type,
			 const unsigned long flags, void *const data)
{
	if (!landlock_get_current_domain())
		return 0;
	return -EPERM;
}

static int hook_move_mount(const struct path *const from_path,
			   const struct path *const to_path)
{
	if (!landlock_get_current_domain())
		return 0;
	return -EPERM;
}

/*
 * Removing a mount point may reveal a previously hidden file hierarchy, which
 * may then grant access to files, which may have previously been forbidden.
 */
static int hook_sb_umount(struct vfsmount *const mnt, const int flags)
{
	if (!landlock_get_current_domain())
		return 0;
	return -EPERM;
}

static int hook_sb_remount(struct super_block *const sb, void *const mnt_opts)
{
	if (!landlock_get_current_domain())
		return 0;
	return -EPERM;
}

/*
 * pivot_root(2), like mount(2), changes the current mount namespace.  It must
 * then be forbidden for a landlocked process.
 *
 * However, chroot(2) may be allowed because it only changes the relative root
 * directory of the current process.  Moreover, it can be used to restrict the
 * view of the filesystem.
 */
static int hook_sb_pivotroot(const struct path *const old_path,
			     const struct path *const new_path)
{
	if (!landlock_get_current_domain())
		return 0;
	return -EPERM;
}

/* Path hooks */

static int hook_path_link(struct dentry *const old_dentry,
			  const struct path *const new_dir,
			  struct dentry *const new_dentry)
{
	return current_check_refer_path(old_dentry, new_dir, new_dentry, false,
					false);
}

static int hook_path_rename(const struct path *const old_dir,
			    struct dentry *const old_dentry,
			    const struct path *const new_dir,
			    struct dentry *const new_dentry,
			    const unsigned int flags)
{
	/* old_dir refers to old_dentry->d_parent and new_dir->mnt */
	return current_check_refer_path(old_dentry, new_dir, new_dentry, true,
					!!(flags & RENAME_EXCHANGE));
}

static int hook_path_mkdir(const struct path *const dir,
			   struct dentry *const dentry, const umode_t mode)
{
	return current_check_access_path(dir, LANDLOCK_ACCESS_FS_MAKE_DIR);
}

static int hook_path_mknod(const struct path *const dir,
			   struct dentry *const dentry, const umode_t mode,
			   const unsigned int dev)
{
	const struct landlock_ruleset *const dom =
		landlock_get_current_domain();

	if (!dom)
		return 0;
	return check_access_path(dom, dir, get_mode_access(mode));
}

static int hook_path_symlink(const struct path *const dir,
			     struct dentry *const dentry,
			     const char *const old_name)
{
	return current_check_access_path(dir, LANDLOCK_ACCESS_FS_MAKE_SYM);
}

static int hook_path_unlink(const struct path *const dir,
			    struct dentry *const dentry)
{
	return current_check_access_path(dir, LANDLOCK_ACCESS_FS_REMOVE_FILE);
}

static int hook_path_rmdir(const struct path *const dir,
			   struct dentry *const dentry)
{
	return current_check_access_path(dir, LANDLOCK_ACCESS_FS_REMOVE_DIR);
}

/* File hooks */

static inline access_mask_t get_file_access(const struct file *const file)
{
	access_mask_t access = 0;

	if (file->f_mode & FMODE_READ) {
		/* A directory can only be opened in read mode. */
		if (S_ISDIR(file_inode(file)->i_mode))
			return LANDLOCK_ACCESS_FS_READ_DIR;
		access = LANDLOCK_ACCESS_FS_READ_FILE;
	}
	if (file->f_mode & FMODE_WRITE)
		access |= LANDLOCK_ACCESS_FS_WRITE_FILE;
	/* __FMODE_EXEC is indeed part of f_flags, not f_mode. */
	if (file->f_flags & __FMODE_EXEC)
		access |= LANDLOCK_ACCESS_FS_EXECUTE;
	return access;
}

static int hook_file_open(struct file *const file)
{
	const struct landlock_ruleset *const dom =
		landlock_get_current_domain();

	if (!dom)
		return 0;
	/*
	 * Because a file may be opened with O_PATH, get_file_access() may
	 * return 0.  This case will be handled with a future Landlock
	 * evolution.
	 */
	return check_access_path(dom, &file->f_path, get_file_access(file));
}

static struct security_hook_list landlock_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(inode_free_security, hook_inode_free_security),

	LSM_HOOK_INIT(sb_delete, hook_sb_delete),
	LSM_HOOK_INIT(sb_mount, hook_sb_mount),
	LSM_HOOK_INIT(move_mount, hook_move_mount),
	LSM_HOOK_INIT(sb_umount, hook_sb_umount),
	LSM_HOOK_INIT(sb_remount, hook_sb_remount),
	LSM_HOOK_INIT(sb_pivotroot, hook_sb_pivotroot),

	LSM_HOOK_INIT(path_link, hook_path_link),
	LSM_HOOK_INIT(path_rename, hook_path_rename),
	LSM_HOOK_INIT(path_mkdir, hook_path_mkdir),
	LSM_HOOK_INIT(path_mknod, hook_path_mknod),
	LSM_HOOK_INIT(path_symlink, hook_path_symlink),
	LSM_HOOK_INIT(path_unlink, hook_path_unlink),
	LSM_HOOK_INIT(path_rmdir, hook_path_rmdir),

	LSM_HOOK_INIT(file_open, hook_file_open),
};

__init void landlock_add_fs_hooks(void)
{
	security_add_hooks(landlock_hooks, ARRAY_SIZE(landlock_hooks),
			   LANDLOCK_NAME);
}
