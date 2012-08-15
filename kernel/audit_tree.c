#include "audit.h"
#include <linux/fsnotify_backend.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/kthread.h>
#include <linux/slab.h>

struct audit_tree;
struct audit_chunk;

struct audit_tree {
	atomic_t count;
	int goner;
	struct audit_chunk *root;
	struct list_head chunks;
	struct list_head rules;
	struct list_head list;
	struct list_head same_root;
	struct rcu_head head;
	char pathname[];
};

struct audit_chunk {
	struct list_head hash;
	struct fsnotify_mark mark;
	struct list_head trees;		/* with root here */
	int dead;
	int count;
	atomic_long_t refs;
	struct rcu_head head;
	struct node {
		struct list_head list;
		struct audit_tree *owner;
		unsigned index;		/* index; upper bit indicates 'will prune' */
	} owners[];
};

static LIST_HEAD(tree_list);
static LIST_HEAD(prune_list);

/*
 * One struct chunk is attached to each inode of interest.
 * We replace struct chunk on tagging/untagging.
 * Rules have pointer to struct audit_tree.
 * Rules have struct list_head rlist forming a list of rules over
 * the same tree.
 * References to struct chunk are collected at audit_inode{,_child}()
 * time and used in AUDIT_TREE rule matching.
 * These references are dropped at the same time we are calling
 * audit_free_names(), etc.
 *
 * Cyclic lists galore:
 * tree.chunks anchors chunk.owners[].list			hash_lock
 * tree.rules anchors rule.rlist				audit_filter_mutex
 * chunk.trees anchors tree.same_root				hash_lock
 * chunk.hash is a hash with middle bits of watch.inode as
 * a hash function.						RCU, hash_lock
 *
 * tree is refcounted; one reference for "some rules on rules_list refer to
 * it", one for each chunk with pointer to it.
 *
 * chunk is refcounted by embedded fsnotify_mark + .refs (non-zero refcount
 * of watch contributes 1 to .refs).
 *
 * node.index allows to get from node.list to containing chunk.
 * MSB of that sucker is stolen to mark taggings that we might have to
 * revert - several operations have very unpleasant cleanup logics and
 * that makes a difference.  Some.
 */

static struct fsnotify_group *audit_tree_group;

static struct audit_tree *alloc_tree(const char *s)
{
	struct audit_tree *tree;

	tree = kmalloc(sizeof(struct audit_tree) + strlen(s) + 1, GFP_KERNEL);
	if (tree) {
		atomic_set(&tree->count, 1);
		tree->goner = 0;
		INIT_LIST_HEAD(&tree->chunks);
		INIT_LIST_HEAD(&tree->rules);
		INIT_LIST_HEAD(&tree->list);
		INIT_LIST_HEAD(&tree->same_root);
		tree->root = NULL;
		strcpy(tree->pathname, s);
	}
	return tree;
}

static inline void get_tree(struct audit_tree *tree)
{
	atomic_inc(&tree->count);
}

static void __put_tree(struct rcu_head *rcu)
{
	struct audit_tree *tree = container_of(rcu, struct audit_tree, head);
	kfree(tree);
}

static inline void put_tree(struct audit_tree *tree)
{
	if (atomic_dec_and_test(&tree->count))
		call_rcu(&tree->head, __put_tree);
}

/* to avoid bringing the entire thing in audit.h */
const char *audit_tree_path(struct audit_tree *tree)
{
	return tree->pathname;
}

static void free_chunk(struct audit_chunk *chunk)
{
	int i;

	for (i = 0; i < chunk->count; i++) {
		if (chunk->owners[i].owner)
			put_tree(chunk->owners[i].owner);
	}
	kfree(chunk);
}

void audit_put_chunk(struct audit_chunk *chunk)
{
	if (atomic_long_dec_and_test(&chunk->refs))
		free_chunk(chunk);
}

static void __put_chunk(struct rcu_head *rcu)
{
	struct audit_chunk *chunk = container_of(rcu, struct audit_chunk, head);
	audit_put_chunk(chunk);
}

static void audit_tree_destroy_watch(struct fsnotify_mark *entry)
{
	struct audit_chunk *chunk = container_of(entry, struct audit_chunk, mark);
	call_rcu(&chunk->head, __put_chunk);
}

static struct audit_chunk *alloc_chunk(int count)
{
	struct audit_chunk *chunk;
	size_t size;
	int i;

	size = offsetof(struct audit_chunk, owners) + count * sizeof(struct node);
	chunk = kzalloc(size, GFP_KERNEL);
	if (!chunk)
		return NULL;

	INIT_LIST_HEAD(&chunk->hash);
	INIT_LIST_HEAD(&chunk->trees);
	chunk->count = count;
	atomic_long_set(&chunk->refs, 1);
	for (i = 0; i < count; i++) {
		INIT_LIST_HEAD(&chunk->owners[i].list);
		chunk->owners[i].index = i;
	}
	fsnotify_init_mark(&chunk->mark, audit_tree_destroy_watch);
	return chunk;
}

enum {HASH_SIZE = 128};
static struct list_head chunk_hash_heads[HASH_SIZE];
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(hash_lock);

static inline struct list_head *chunk_hash(const struct inode *inode)
{
	unsigned long n = (unsigned long)inode / L1_CACHE_BYTES;
	return chunk_hash_heads + n % HASH_SIZE;
}

/* hash_lock & entry->lock is held by caller */
static void insert_hash(struct audit_chunk *chunk)
{
	struct fsnotify_mark *entry = &chunk->mark;
	struct list_head *list;

	if (!entry->i.inode)
		return;
	list = chunk_hash(entry->i.inode);
	list_add_rcu(&chunk->hash, list);
}

/* called under rcu_read_lock */
struct audit_chunk *audit_tree_lookup(const struct inode *inode)
{
	struct list_head *list = chunk_hash(inode);
	struct audit_chunk *p;

	list_for_each_entry_rcu(p, list, hash) {
		/* mark.inode may have gone NULL, but who cares? */
		if (p->mark.i.inode == inode) {
			atomic_long_inc(&p->refs);
			return p;
		}
	}
	return NULL;
}

int audit_tree_match(struct audit_chunk *chunk, struct audit_tree *tree)
{
	int n;
	for (n = 0; n < chunk->count; n++)
		if (chunk->owners[n].owner == tree)
			return 1;
	return 0;
}

/* tagging and untagging inodes with trees */

static struct audit_chunk *find_chunk(struct node *p)
{
	int index = p->index & ~(1U<<31);
	p -= index;
	return container_of(p, struct audit_chunk, owners[0]);
}

static void untag_chunk(struct node *p)
{
	struct audit_chunk *chunk = find_chunk(p);
	struct fsnotify_mark *entry = &chunk->mark;
	struct audit_chunk *new = NULL;
	struct audit_tree *owner;
	int size = chunk->count - 1;
	int i, j;

	fsnotify_get_mark(entry);

	spin_unlock(&hash_lock);

	if (size)
		new = alloc_chunk(size);

	spin_lock(&entry->lock);
	if (chunk->dead || !entry->i.inode) {
		spin_unlock(&entry->lock);
		if (new)
			free_chunk(new);
		goto out;
	}

	owner = p->owner;

	if (!size) {
		chunk->dead = 1;
		spin_lock(&hash_lock);
		list_del_init(&chunk->trees);
		if (owner->root == chunk)
			owner->root = NULL;
		list_del_init(&p->list);
		list_del_rcu(&chunk->hash);
		spin_unlock(&hash_lock);
		spin_unlock(&entry->lock);
		fsnotify_destroy_mark(entry);
		fsnotify_put_mark(entry);
		goto out;
	}

	if (!new)
		goto Fallback;

	fsnotify_duplicate_mark(&new->mark, entry);
	if (fsnotify_add_mark(&new->mark, new->mark.group, new->mark.i.inode, NULL, 1)) {
		fsnotify_put_mark(&new->mark);
		goto Fallback;
	}

	chunk->dead = 1;
	spin_lock(&hash_lock);
	list_replace_init(&chunk->trees, &new->trees);
	if (owner->root == chunk) {
		list_del_init(&owner->same_root);
		owner->root = NULL;
	}

	for (i = j = 0; j <= size; i++, j++) {
		struct audit_tree *s;
		if (&chunk->owners[j] == p) {
			list_del_init(&p->list);
			i--;
			continue;
		}
		s = chunk->owners[j].owner;
		new->owners[i].owner = s;
		new->owners[i].index = chunk->owners[j].index - j + i;
		if (!s) /* result of earlier fallback */
			continue;
		get_tree(s);
		list_replace_init(&chunk->owners[j].list, &new->owners[i].list);
	}

	list_replace_rcu(&chunk->hash, &new->hash);
	list_for_each_entry(owner, &new->trees, same_root)
		owner->root = new;
	spin_unlock(&hash_lock);
	spin_unlock(&entry->lock);
	fsnotify_destroy_mark(entry);
	fsnotify_put_mark(entry);
	goto out;

Fallback:
	// do the best we can
	spin_lock(&hash_lock);
	if (owner->root == chunk) {
		list_del_init(&owner->same_root);
		owner->root = NULL;
	}
	list_del_init(&p->list);
	p->owner = NULL;
	put_tree(owner);
	spin_unlock(&hash_lock);
	spin_unlock(&entry->lock);
out:
	fsnotify_put_mark(entry);
	spin_lock(&hash_lock);
}

static int create_chunk(struct inode *inode, struct audit_tree *tree)
{
	struct fsnotify_mark *entry;
	struct audit_chunk *chunk = alloc_chunk(1);
	if (!chunk)
		return -ENOMEM;

	entry = &chunk->mark;
	if (fsnotify_add_mark(entry, audit_tree_group, inode, NULL, 0)) {
		fsnotify_put_mark(entry);
		return -ENOSPC;
	}

	spin_lock(&entry->lock);
	spin_lock(&hash_lock);
	if (tree->goner) {
		spin_unlock(&hash_lock);
		chunk->dead = 1;
		spin_unlock(&entry->lock);
		fsnotify_destroy_mark(entry);
		fsnotify_put_mark(entry);
		return 0;
	}
	chunk->owners[0].index = (1U << 31);
	chunk->owners[0].owner = tree;
	get_tree(tree);
	list_add(&chunk->owners[0].list, &tree->chunks);
	if (!tree->root) {
		tree->root = chunk;
		list_add(&tree->same_root, &chunk->trees);
	}
	insert_hash(chunk);
	spin_unlock(&hash_lock);
	spin_unlock(&entry->lock);
	return 0;
}

/* the first tagged inode becomes root of tree */
static int tag_chunk(struct inode *inode, struct audit_tree *tree)
{
	struct fsnotify_mark *old_entry, *chunk_entry;
	struct audit_tree *owner;
	struct audit_chunk *chunk, *old;
	struct node *p;
	int n;

	old_entry = fsnotify_find_inode_mark(audit_tree_group, inode);
	if (!old_entry)
		return create_chunk(inode, tree);

	old = container_of(old_entry, struct audit_chunk, mark);

	/* are we already there? */
	spin_lock(&hash_lock);
	for (n = 0; n < old->count; n++) {
		if (old->owners[n].owner == tree) {
			spin_unlock(&hash_lock);
			fsnotify_put_mark(old_entry);
			return 0;
		}
	}
	spin_unlock(&hash_lock);

	chunk = alloc_chunk(old->count + 1);
	if (!chunk) {
		fsnotify_put_mark(old_entry);
		return -ENOMEM;
	}

	chunk_entry = &chunk->mark;

	spin_lock(&old_entry->lock);
	if (!old_entry->i.inode) {
		/* old_entry is being shot, lets just lie */
		spin_unlock(&old_entry->lock);
		fsnotify_put_mark(old_entry);
		free_chunk(chunk);
		return -ENOENT;
	}

	fsnotify_duplicate_mark(chunk_entry, old_entry);
	if (fsnotify_add_mark(chunk_entry, chunk_entry->group, chunk_entry->i.inode, NULL, 1)) {
		spin_unlock(&old_entry->lock);
		fsnotify_put_mark(chunk_entry);
		fsnotify_put_mark(old_entry);
		return -ENOSPC;
	}

	/* even though we hold old_entry->lock, this is safe since chunk_entry->lock could NEVER have been grabbed before */
	spin_lock(&chunk_entry->lock);
	spin_lock(&hash_lock);

	/* we now hold old_entry->lock, chunk_entry->lock, and hash_lock */
	if (tree->goner) {
		spin_unlock(&hash_lock);
		chunk->dead = 1;
		spin_unlock(&chunk_entry->lock);
		spin_unlock(&old_entry->lock);

		fsnotify_destroy_mark(chunk_entry);

		fsnotify_put_mark(chunk_entry);
		fsnotify_put_mark(old_entry);
		return 0;
	}
	list_replace_init(&old->trees, &chunk->trees);
	for (n = 0, p = chunk->owners; n < old->count; n++, p++) {
		struct audit_tree *s = old->owners[n].owner;
		p->owner = s;
		p->index = old->owners[n].index;
		if (!s) /* result of fallback in untag */
			continue;
		get_tree(s);
		list_replace_init(&old->owners[n].list, &p->list);
	}
	p->index = (chunk->count - 1) | (1U<<31);
	p->owner = tree;
	get_tree(tree);
	list_add(&p->list, &tree->chunks);
	list_replace_rcu(&old->hash, &chunk->hash);
	list_for_each_entry(owner, &chunk->trees, same_root)
		owner->root = chunk;
	old->dead = 1;
	if (!tree->root) {
		tree->root = chunk;
		list_add(&tree->same_root, &chunk->trees);
	}
	spin_unlock(&hash_lock);
	spin_unlock(&chunk_entry->lock);
	spin_unlock(&old_entry->lock);
	fsnotify_destroy_mark(old_entry);
	fsnotify_put_mark(old_entry); /* pair to fsnotify_find mark_entry */
	fsnotify_put_mark(old_entry); /* and kill it */
	return 0;
}

static void kill_rules(struct audit_tree *tree)
{
	struct audit_krule *rule, *next;
	struct audit_entry *entry;
	struct audit_buffer *ab;

	list_for_each_entry_safe(rule, next, &tree->rules, rlist) {
		entry = container_of(rule, struct audit_entry, rule);

		list_del_init(&rule->rlist);
		if (rule->tree) {
			/* not a half-baked one */
			ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE);
			audit_log_format(ab, "op=");
			audit_log_string(ab, "remove rule");
			audit_log_format(ab, " dir=");
			audit_log_untrustedstring(ab, rule->tree->pathname);
			audit_log_key(ab, rule->filterkey);
			audit_log_format(ab, " list=%d res=1", rule->listnr);
			audit_log_end(ab);
			rule->tree = NULL;
			list_del_rcu(&entry->list);
			list_del(&entry->rule.list);
			call_rcu(&entry->rcu, audit_free_rule_rcu);
		}
	}
}

/*
 * finish killing struct audit_tree
 */
static void prune_one(struct audit_tree *victim)
{
	spin_lock(&hash_lock);
	while (!list_empty(&victim->chunks)) {
		struct node *p;

		p = list_entry(victim->chunks.next, struct node, list);

		untag_chunk(p);
	}
	spin_unlock(&hash_lock);
	put_tree(victim);
}

/* trim the uncommitted chunks from tree */

static void trim_marked(struct audit_tree *tree)
{
	struct list_head *p, *q;
	spin_lock(&hash_lock);
	if (tree->goner) {
		spin_unlock(&hash_lock);
		return;
	}
	/* reorder */
	for (p = tree->chunks.next; p != &tree->chunks; p = q) {
		struct node *node = list_entry(p, struct node, list);
		q = p->next;
		if (node->index & (1U<<31)) {
			list_del_init(p);
			list_add(p, &tree->chunks);
		}
	}

	while (!list_empty(&tree->chunks)) {
		struct node *node;

		node = list_entry(tree->chunks.next, struct node, list);

		/* have we run out of marked? */
		if (!(node->index & (1U<<31)))
			break;

		untag_chunk(node);
	}
	if (!tree->root && !tree->goner) {
		tree->goner = 1;
		spin_unlock(&hash_lock);
		mutex_lock(&audit_filter_mutex);
		kill_rules(tree);
		list_del_init(&tree->list);
		mutex_unlock(&audit_filter_mutex);
		prune_one(tree);
	} else {
		spin_unlock(&hash_lock);
	}
}

static void audit_schedule_prune(void);

/* called with audit_filter_mutex */
int audit_remove_tree_rule(struct audit_krule *rule)
{
	struct audit_tree *tree;
	tree = rule->tree;
	if (tree) {
		spin_lock(&hash_lock);
		list_del_init(&rule->rlist);
		if (list_empty(&tree->rules) && !tree->goner) {
			tree->root = NULL;
			list_del_init(&tree->same_root);
			tree->goner = 1;
			list_move(&tree->list, &prune_list);
			rule->tree = NULL;
			spin_unlock(&hash_lock);
			audit_schedule_prune();
			return 1;
		}
		rule->tree = NULL;
		spin_unlock(&hash_lock);
		return 1;
	}
	return 0;
}

static int compare_root(struct vfsmount *mnt, void *arg)
{
	return mnt->mnt_root->d_inode == arg;
}

void audit_trim_trees(void)
{
	struct list_head cursor;

	mutex_lock(&audit_filter_mutex);
	list_add(&cursor, &tree_list);
	while (cursor.next != &tree_list) {
		struct audit_tree *tree;
		struct path path;
		struct vfsmount *root_mnt;
		struct node *node;
		int err;

		tree = container_of(cursor.next, struct audit_tree, list);
		get_tree(tree);
		list_del(&cursor);
		list_add(&cursor, &tree->list);
		mutex_unlock(&audit_filter_mutex);

		err = kern_path(tree->pathname, 0, &path);
		if (err)
			goto skip_it;

		root_mnt = collect_mounts(&path);
		path_put(&path);
		if (!root_mnt)
			goto skip_it;

		spin_lock(&hash_lock);
		list_for_each_entry(node, &tree->chunks, list) {
			struct audit_chunk *chunk = find_chunk(node);
			/* this could be NULL if the watch is dying else where... */
			struct inode *inode = chunk->mark.i.inode;
			node->index |= 1U<<31;
			if (iterate_mounts(compare_root, inode, root_mnt))
				node->index &= ~(1U<<31);
		}
		spin_unlock(&hash_lock);
		trim_marked(tree);
		put_tree(tree);
		drop_collected_mounts(root_mnt);
skip_it:
		mutex_lock(&audit_filter_mutex);
	}
	list_del(&cursor);
	mutex_unlock(&audit_filter_mutex);
}

int audit_make_tree(struct audit_krule *rule, char *pathname, u32 op)
{

	if (pathname[0] != '/' ||
	    rule->listnr != AUDIT_FILTER_EXIT ||
	    op != Audit_equal ||
	    rule->inode_f || rule->watch || rule->tree)
		return -EINVAL;
	rule->tree = alloc_tree(pathname);
	if (!rule->tree)
		return -ENOMEM;
	return 0;
}

void audit_put_tree(struct audit_tree *tree)
{
	put_tree(tree);
}

static int tag_mount(struct vfsmount *mnt, void *arg)
{
	return tag_chunk(mnt->mnt_root->d_inode, arg);
}

/* called with audit_filter_mutex */
int audit_add_tree_rule(struct audit_krule *rule)
{
	struct audit_tree *seed = rule->tree, *tree;
	struct path path;
	struct vfsmount *mnt;
	int err;

	list_for_each_entry(tree, &tree_list, list) {
		if (!strcmp(seed->pathname, tree->pathname)) {
			put_tree(seed);
			rule->tree = tree;
			list_add(&rule->rlist, &tree->rules);
			return 0;
		}
	}
	tree = seed;
	list_add(&tree->list, &tree_list);
	list_add(&rule->rlist, &tree->rules);
	/* do not set rule->tree yet */
	mutex_unlock(&audit_filter_mutex);

	err = kern_path(tree->pathname, 0, &path);
	if (err)
		goto Err;
	mnt = collect_mounts(&path);
	path_put(&path);
	if (!mnt) {
		err = -ENOMEM;
		goto Err;
	}

	get_tree(tree);
	err = iterate_mounts(tag_mount, tree, mnt);
	drop_collected_mounts(mnt);

	if (!err) {
		struct node *node;
		spin_lock(&hash_lock);
		list_for_each_entry(node, &tree->chunks, list)
			node->index &= ~(1U<<31);
		spin_unlock(&hash_lock);
	} else {
		trim_marked(tree);
		goto Err;
	}

	mutex_lock(&audit_filter_mutex);
	if (list_empty(&rule->rlist)) {
		put_tree(tree);
		return -ENOENT;
	}
	rule->tree = tree;
	put_tree(tree);

	return 0;
Err:
	mutex_lock(&audit_filter_mutex);
	list_del_init(&tree->list);
	list_del_init(&tree->rules);
	put_tree(tree);
	return err;
}

int audit_tag_tree(char *old, char *new)
{
	struct list_head cursor, barrier;
	int failed = 0;
	struct path path1, path2;
	struct vfsmount *tagged;
	int err;

	err = kern_path(new, 0, &path2);
	if (err)
		return err;
	tagged = collect_mounts(&path2);
	path_put(&path2);
	if (!tagged)
		return -ENOMEM;

	err = kern_path(old, 0, &path1);
	if (err) {
		drop_collected_mounts(tagged);
		return err;
	}

	mutex_lock(&audit_filter_mutex);
	list_add(&barrier, &tree_list);
	list_add(&cursor, &barrier);

	while (cursor.next != &tree_list) {
		struct audit_tree *tree;
		int good_one = 0;

		tree = container_of(cursor.next, struct audit_tree, list);
		get_tree(tree);
		list_del(&cursor);
		list_add(&cursor, &tree->list);
		mutex_unlock(&audit_filter_mutex);

		err = kern_path(tree->pathname, 0, &path2);
		if (!err) {
			good_one = path_is_under(&path1, &path2);
			path_put(&path2);
		}

		if (!good_one) {
			put_tree(tree);
			mutex_lock(&audit_filter_mutex);
			continue;
		}

		failed = iterate_mounts(tag_mount, tree, tagged);
		if (failed) {
			put_tree(tree);
			mutex_lock(&audit_filter_mutex);
			break;
		}

		mutex_lock(&audit_filter_mutex);
		spin_lock(&hash_lock);
		if (!tree->goner) {
			list_del(&tree->list);
			list_add(&tree->list, &tree_list);
		}
		spin_unlock(&hash_lock);
		put_tree(tree);
	}

	while (barrier.prev != &tree_list) {
		struct audit_tree *tree;

		tree = container_of(barrier.prev, struct audit_tree, list);
		get_tree(tree);
		list_del(&tree->list);
		list_add(&tree->list, &barrier);
		mutex_unlock(&audit_filter_mutex);

		if (!failed) {
			struct node *node;
			spin_lock(&hash_lock);
			list_for_each_entry(node, &tree->chunks, list)
				node->index &= ~(1U<<31);
			spin_unlock(&hash_lock);
		} else {
			trim_marked(tree);
		}

		put_tree(tree);
		mutex_lock(&audit_filter_mutex);
	}
	list_del(&barrier);
	list_del(&cursor);
	mutex_unlock(&audit_filter_mutex);
	path_put(&path1);
	drop_collected_mounts(tagged);
	return failed;
}

/*
 * That gets run when evict_chunk() ends up needing to kill audit_tree.
 * Runs from a separate thread.
 */
static int prune_tree_thread(void *unused)
{
	mutex_lock(&audit_cmd_mutex);
	mutex_lock(&audit_filter_mutex);

	while (!list_empty(&prune_list)) {
		struct audit_tree *victim;

		victim = list_entry(prune_list.next, struct audit_tree, list);
		list_del_init(&victim->list);

		mutex_unlock(&audit_filter_mutex);

		prune_one(victim);

		mutex_lock(&audit_filter_mutex);
	}

	mutex_unlock(&audit_filter_mutex);
	mutex_unlock(&audit_cmd_mutex);
	return 0;
}

static void audit_schedule_prune(void)
{
	kthread_run(prune_tree_thread, NULL, "audit_prune_tree");
}

/*
 * ... and that one is done if evict_chunk() decides to delay until the end
 * of syscall.  Runs synchronously.
 */
void audit_kill_trees(struct list_head *list)
{
	mutex_lock(&audit_cmd_mutex);
	mutex_lock(&audit_filter_mutex);

	while (!list_empty(list)) {
		struct audit_tree *victim;

		victim = list_entry(list->next, struct audit_tree, list);
		kill_rules(victim);
		list_del_init(&victim->list);

		mutex_unlock(&audit_filter_mutex);

		prune_one(victim);

		mutex_lock(&audit_filter_mutex);
	}

	mutex_unlock(&audit_filter_mutex);
	mutex_unlock(&audit_cmd_mutex);
}

/*
 *  Here comes the stuff asynchronous to auditctl operations
 */

static void evict_chunk(struct audit_chunk *chunk)
{
	struct audit_tree *owner;
	struct list_head *postponed = audit_killed_trees();
	int need_prune = 0;
	int n;

	if (chunk->dead)
		return;

	chunk->dead = 1;
	mutex_lock(&audit_filter_mutex);
	spin_lock(&hash_lock);
	while (!list_empty(&chunk->trees)) {
		owner = list_entry(chunk->trees.next,
				   struct audit_tree, same_root);
		owner->goner = 1;
		owner->root = NULL;
		list_del_init(&owner->same_root);
		spin_unlock(&hash_lock);
		if (!postponed) {
			kill_rules(owner);
			list_move(&owner->list, &prune_list);
			need_prune = 1;
		} else {
			list_move(&owner->list, postponed);
		}
		spin_lock(&hash_lock);
	}
	list_del_rcu(&chunk->hash);
	for (n = 0; n < chunk->count; n++)
		list_del_init(&chunk->owners[n].list);
	spin_unlock(&hash_lock);
	if (need_prune)
		audit_schedule_prune();
	mutex_unlock(&audit_filter_mutex);
}

static int audit_tree_handle_event(struct fsnotify_group *group,
				   struct fsnotify_mark *inode_mark,
				   struct fsnotify_mark *vfsmonut_mark,
				   struct fsnotify_event *event)
{
	BUG();
	return -EOPNOTSUPP;
}

static void audit_tree_freeing_mark(struct fsnotify_mark *entry, struct fsnotify_group *group)
{
	struct audit_chunk *chunk = container_of(entry, struct audit_chunk, mark);

	evict_chunk(chunk);
	fsnotify_put_mark(entry);
}

static bool audit_tree_send_event(struct fsnotify_group *group, struct inode *inode,
				  struct fsnotify_mark *inode_mark,
				  struct fsnotify_mark *vfsmount_mark,
				  __u32 mask, void *data, int data_type)
{
	return false;
}

static const struct fsnotify_ops audit_tree_ops = {
	.handle_event = audit_tree_handle_event,
	.should_send_event = audit_tree_send_event,
	.free_group_priv = NULL,
	.free_event_priv = NULL,
	.freeing_mark = audit_tree_freeing_mark,
};

static int __init audit_tree_init(void)
{
	int i;

	audit_tree_group = fsnotify_alloc_group(&audit_tree_ops);
	if (IS_ERR(audit_tree_group))
		audit_panic("cannot initialize fsnotify group for rectree watches");

	for (i = 0; i < HASH_SIZE; i++)
		INIT_LIST_HEAD(&chunk_hash_heads[i]);

	return 0;
}
__initcall(audit_tree_init);
