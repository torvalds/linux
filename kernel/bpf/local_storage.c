//SPDX-License-Identifier: GPL-2.0
#include <linux/bpf-cgroup.h>
#include <linux/bpf.h>
#include <linux/bug.h>
#include <linux/filter.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/slab.h>

DEFINE_PER_CPU(void*, bpf_cgroup_storage);

#ifdef CONFIG_CGROUP_BPF

#define LOCAL_STORAGE_CREATE_FLAG_MASK					\
	(BPF_F_NUMA_NODE | BPF_F_RDONLY | BPF_F_WRONLY)

struct bpf_cgroup_storage_map {
	struct bpf_map map;

	spinlock_t lock;
	struct bpf_prog *prog;
	struct rb_root root;
	struct list_head list;
};

static struct bpf_cgroup_storage_map *map_to_storage(struct bpf_map *map)
{
	return container_of(map, struct bpf_cgroup_storage_map, map);
}

static int bpf_cgroup_storage_key_cmp(
	const struct bpf_cgroup_storage_key *key1,
	const struct bpf_cgroup_storage_key *key2)
{
	if (key1->cgroup_inode_id < key2->cgroup_inode_id)
		return -1;
	else if (key1->cgroup_inode_id > key2->cgroup_inode_id)
		return 1;
	else if (key1->attach_type < key2->attach_type)
		return -1;
	else if (key1->attach_type > key2->attach_type)
		return 1;
	return 0;
}

static struct bpf_cgroup_storage *cgroup_storage_lookup(
	struct bpf_cgroup_storage_map *map, struct bpf_cgroup_storage_key *key,
	bool locked)
{
	struct rb_root *root = &map->root;
	struct rb_node *node;

	if (!locked)
		spin_lock_bh(&map->lock);

	node = root->rb_node;
	while (node) {
		struct bpf_cgroup_storage *storage;

		storage = container_of(node, struct bpf_cgroup_storage, node);

		switch (bpf_cgroup_storage_key_cmp(key, &storage->key)) {
		case -1:
			node = node->rb_left;
			break;
		case 1:
			node = node->rb_right;
			break;
		default:
			if (!locked)
				spin_unlock_bh(&map->lock);
			return storage;
		}
	}

	if (!locked)
		spin_unlock_bh(&map->lock);

	return NULL;
}

static int cgroup_storage_insert(struct bpf_cgroup_storage_map *map,
				 struct bpf_cgroup_storage *storage)
{
	struct rb_root *root = &map->root;
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	while (*new) {
		struct bpf_cgroup_storage *this;

		this = container_of(*new, struct bpf_cgroup_storage, node);

		parent = *new;
		switch (bpf_cgroup_storage_key_cmp(&storage->key, &this->key)) {
		case -1:
			new = &((*new)->rb_left);
			break;
		case 1:
			new = &((*new)->rb_right);
			break;
		default:
			return -EEXIST;
		}
	}

	rb_link_node(&storage->node, parent, new);
	rb_insert_color(&storage->node, root);

	return 0;
}

static void *cgroup_storage_lookup_elem(struct bpf_map *_map, void *_key)
{
	struct bpf_cgroup_storage_map *map = map_to_storage(_map);
	struct bpf_cgroup_storage_key *key = _key;
	struct bpf_cgroup_storage *storage;

	storage = cgroup_storage_lookup(map, key, false);
	if (!storage)
		return NULL;

	return &READ_ONCE(storage->buf)->data[0];
}

static int cgroup_storage_update_elem(struct bpf_map *map, void *_key,
				      void *value, u64 flags)
{
	struct bpf_cgroup_storage_key *key = _key;
	struct bpf_cgroup_storage *storage;
	struct bpf_storage_buffer *new;

	if (flags != BPF_ANY && flags != BPF_EXIST)
		return -EINVAL;

	storage = cgroup_storage_lookup((struct bpf_cgroup_storage_map *)map,
					key, false);
	if (!storage)
		return -ENOENT;

	new = kmalloc_node(sizeof(struct bpf_storage_buffer) +
			   map->value_size, __GFP_ZERO | GFP_USER,
			   map->numa_node);
	if (!new)
		return -ENOMEM;

	memcpy(&new->data[0], value, map->value_size);

	new = xchg(&storage->buf, new);
	kfree_rcu(new, rcu);

	return 0;
}

static int cgroup_storage_get_next_key(struct bpf_map *_map, void *_key,
				       void *_next_key)
{
	struct bpf_cgroup_storage_map *map = map_to_storage(_map);
	struct bpf_cgroup_storage_key *key = _key;
	struct bpf_cgroup_storage_key *next = _next_key;
	struct bpf_cgroup_storage *storage;

	spin_lock_bh(&map->lock);

	if (list_empty(&map->list))
		goto enoent;

	if (key) {
		storage = cgroup_storage_lookup(map, key, true);
		if (!storage)
			goto enoent;

		storage = list_next_entry(storage, list);
		if (!storage)
			goto enoent;
	} else {
		storage = list_first_entry(&map->list,
					 struct bpf_cgroup_storage, list);
	}

	spin_unlock_bh(&map->lock);
	next->attach_type = storage->key.attach_type;
	next->cgroup_inode_id = storage->key.cgroup_inode_id;
	return 0;

enoent:
	spin_unlock_bh(&map->lock);
	return -ENOENT;
}

static struct bpf_map *cgroup_storage_map_alloc(union bpf_attr *attr)
{
	int numa_node = bpf_map_attr_numa_node(attr);
	struct bpf_cgroup_storage_map *map;

	if (attr->key_size != sizeof(struct bpf_cgroup_storage_key))
		return ERR_PTR(-EINVAL);

	if (attr->value_size == 0)
		return ERR_PTR(-EINVAL);

	if (attr->value_size > PAGE_SIZE)
		return ERR_PTR(-E2BIG);

	if (attr->map_flags & ~LOCAL_STORAGE_CREATE_FLAG_MASK)
		/* reserved bits should not be used */
		return ERR_PTR(-EINVAL);

	if (attr->max_entries)
		/* max_entries is not used and enforced to be 0 */
		return ERR_PTR(-EINVAL);

	map = kmalloc_node(sizeof(struct bpf_cgroup_storage_map),
			   __GFP_ZERO | GFP_USER, numa_node);
	if (!map)
		return ERR_PTR(-ENOMEM);

	map->map.pages = round_up(sizeof(struct bpf_cgroup_storage_map),
				  PAGE_SIZE) >> PAGE_SHIFT;

	/* copy mandatory map attributes */
	bpf_map_init_from_attr(&map->map, attr);

	spin_lock_init(&map->lock);
	map->root = RB_ROOT;
	INIT_LIST_HEAD(&map->list);

	return &map->map;
}

static void cgroup_storage_map_free(struct bpf_map *_map)
{
	struct bpf_cgroup_storage_map *map = map_to_storage(_map);

	WARN_ON(!RB_EMPTY_ROOT(&map->root));
	WARN_ON(!list_empty(&map->list));

	kfree(map);
}

static int cgroup_storage_delete_elem(struct bpf_map *map, void *key)
{
	return -EINVAL;
}

const struct bpf_map_ops cgroup_storage_map_ops = {
	.map_alloc = cgroup_storage_map_alloc,
	.map_free = cgroup_storage_map_free,
	.map_get_next_key = cgroup_storage_get_next_key,
	.map_lookup_elem = cgroup_storage_lookup_elem,
	.map_update_elem = cgroup_storage_update_elem,
	.map_delete_elem = cgroup_storage_delete_elem,
	.map_check_btf = map_check_no_btf,
};

int bpf_cgroup_storage_assign(struct bpf_prog *prog, struct bpf_map *_map)
{
	struct bpf_cgroup_storage_map *map = map_to_storage(_map);
	int ret = -EBUSY;

	spin_lock_bh(&map->lock);

	if (map->prog && map->prog != prog)
		goto unlock;
	if (prog->aux->cgroup_storage && prog->aux->cgroup_storage != _map)
		goto unlock;

	map->prog = prog;
	prog->aux->cgroup_storage = _map;
	ret = 0;
unlock:
	spin_unlock_bh(&map->lock);

	return ret;
}

void bpf_cgroup_storage_release(struct bpf_prog *prog, struct bpf_map *_map)
{
	struct bpf_cgroup_storage_map *map = map_to_storage(_map);

	spin_lock_bh(&map->lock);
	if (map->prog == prog) {
		WARN_ON(prog->aux->cgroup_storage != _map);
		map->prog = NULL;
		prog->aux->cgroup_storage = NULL;
	}
	spin_unlock_bh(&map->lock);
}

struct bpf_cgroup_storage *bpf_cgroup_storage_alloc(struct bpf_prog *prog)
{
	struct bpf_cgroup_storage *storage;
	struct bpf_map *map;
	u32 pages;

	map = prog->aux->cgroup_storage;
	if (!map)
		return NULL;

	pages = round_up(sizeof(struct bpf_cgroup_storage) +
			 sizeof(struct bpf_storage_buffer) +
			 map->value_size, PAGE_SIZE) >> PAGE_SHIFT;
	if (bpf_map_charge_memlock(map, pages))
		return ERR_PTR(-EPERM);

	storage = kmalloc_node(sizeof(struct bpf_cgroup_storage),
			       __GFP_ZERO | GFP_USER, map->numa_node);
	if (!storage) {
		bpf_map_uncharge_memlock(map, pages);
		return ERR_PTR(-ENOMEM);
	}

	storage->buf = kmalloc_node(sizeof(struct bpf_storage_buffer) +
				    map->value_size, __GFP_ZERO | GFP_USER,
				    map->numa_node);
	if (!storage->buf) {
		bpf_map_uncharge_memlock(map, pages);
		kfree(storage);
		return ERR_PTR(-ENOMEM);
	}

	storage->map = (struct bpf_cgroup_storage_map *)map;

	return storage;
}

void bpf_cgroup_storage_free(struct bpf_cgroup_storage *storage)
{
	u32 pages;
	struct bpf_map *map;

	if (!storage)
		return;

	map = &storage->map->map;
	pages = round_up(sizeof(struct bpf_cgroup_storage) +
			 sizeof(struct bpf_storage_buffer) +
			 map->value_size, PAGE_SIZE) >> PAGE_SHIFT;
	bpf_map_uncharge_memlock(map, pages);

	kfree_rcu(storage->buf, rcu);
	kfree_rcu(storage, rcu);
}

void bpf_cgroup_storage_link(struct bpf_cgroup_storage *storage,
			     struct cgroup *cgroup,
			     enum bpf_attach_type type)
{
	struct bpf_cgroup_storage_map *map;

	if (!storage)
		return;

	storage->key.attach_type = type;
	storage->key.cgroup_inode_id = cgroup->kn->id.id;

	map = storage->map;

	spin_lock_bh(&map->lock);
	WARN_ON(cgroup_storage_insert(map, storage));
	list_add(&storage->list, &map->list);
	spin_unlock_bh(&map->lock);
}

void bpf_cgroup_storage_unlink(struct bpf_cgroup_storage *storage)
{
	struct bpf_cgroup_storage_map *map;
	struct rb_root *root;

	if (!storage)
		return;

	map = storage->map;

	spin_lock_bh(&map->lock);
	root = &map->root;
	rb_erase(&storage->node, root);

	list_del(&storage->list);
	spin_unlock_bh(&map->lock);
}

#endif
