//SPDX-License-Identifier: GPL-2.0
#include <linux/bpf-cgroup.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/bug.h>
#include <linux/filter.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <uapi/linux/btf.h>

DEFINE_PER_CPU(struct bpf_cgroup_storage*, bpf_cgroup_storage[MAX_BPF_CGROUP_STORAGE_TYPE]);

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

	if (unlikely(flags & ~(BPF_F_LOCK | BPF_EXIST | BPF_NOEXIST)))
		return -EINVAL;

	if (unlikely(flags & BPF_NOEXIST))
		return -EINVAL;

	if (unlikely((flags & BPF_F_LOCK) &&
		     !map_value_has_spin_lock(map)))
		return -EINVAL;

	storage = cgroup_storage_lookup((struct bpf_cgroup_storage_map *)map,
					key, false);
	if (!storage)
		return -ENOENT;

	if (flags & BPF_F_LOCK) {
		copy_map_value_locked(map, storage->buf->data, value, false);
		return 0;
	}

	new = kmalloc_node(sizeof(struct bpf_storage_buffer) +
			   map->value_size,
			   __GFP_ZERO | GFP_ATOMIC | __GFP_NOWARN,
			   map->numa_node);
	if (!new)
		return -ENOMEM;

	memcpy(&new->data[0], value, map->value_size);
	check_and_init_map_lock(map, new->data);

	new = xchg(&storage->buf, new);
	kfree_rcu(new, rcu);

	return 0;
}

int bpf_percpu_cgroup_storage_copy(struct bpf_map *_map, void *_key,
				   void *value)
{
	struct bpf_cgroup_storage_map *map = map_to_storage(_map);
	struct bpf_cgroup_storage_key *key = _key;
	struct bpf_cgroup_storage *storage;
	int cpu, off = 0;
	u32 size;

	rcu_read_lock();
	storage = cgroup_storage_lookup(map, key, false);
	if (!storage) {
		rcu_read_unlock();
		return -ENOENT;
	}

	/* per_cpu areas are zero-filled and bpf programs can only
	 * access 'value_size' of them, so copying rounded areas
	 * will not leak any kernel data
	 */
	size = round_up(_map->value_size, 8);
	for_each_possible_cpu(cpu) {
		bpf_long_memcpy(value + off,
				per_cpu_ptr(storage->percpu_buf, cpu), size);
		off += size;
	}
	rcu_read_unlock();
	return 0;
}

int bpf_percpu_cgroup_storage_update(struct bpf_map *_map, void *_key,
				     void *value, u64 map_flags)
{
	struct bpf_cgroup_storage_map *map = map_to_storage(_map);
	struct bpf_cgroup_storage_key *key = _key;
	struct bpf_cgroup_storage *storage;
	int cpu, off = 0;
	u32 size;

	if (map_flags != BPF_ANY && map_flags != BPF_EXIST)
		return -EINVAL;

	rcu_read_lock();
	storage = cgroup_storage_lookup(map, key, false);
	if (!storage) {
		rcu_read_unlock();
		return -ENOENT;
	}

	/* the user space will provide round_up(value_size, 8) bytes that
	 * will be copied into per-cpu area. bpf programs can only access
	 * value_size of it. During lookup the same extra bytes will be
	 * returned or zeros which were zero-filled by percpu_alloc,
	 * so no kernel data leaks possible
	 */
	size = round_up(_map->value_size, 8);
	for_each_possible_cpu(cpu) {
		bpf_long_memcpy(per_cpu_ptr(storage->percpu_buf, cpu),
				value + off, size);
		off += size;
	}
	rcu_read_unlock();
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

static int cgroup_storage_check_btf(const struct bpf_map *map,
				    const struct btf *btf,
				    const struct btf_type *key_type,
				    const struct btf_type *value_type)
{
	struct btf_member *m;
	u32 offset, size;

	/* Key is expected to be of struct bpf_cgroup_storage_key type,
	 * which is:
	 * struct bpf_cgroup_storage_key {
	 *	__u64	cgroup_inode_id;
	 *	__u32	attach_type;
	 * };
	 */

	/*
	 * Key_type must be a structure with two fields.
	 */
	if (BTF_INFO_KIND(key_type->info) != BTF_KIND_STRUCT ||
	    BTF_INFO_VLEN(key_type->info) != 2)
		return -EINVAL;

	/*
	 * The first field must be a 64 bit integer at 0 offset.
	 */
	m = (struct btf_member *)(key_type + 1);
	size = FIELD_SIZEOF(struct bpf_cgroup_storage_key, cgroup_inode_id);
	if (!btf_member_is_reg_int(btf, key_type, m, 0, size))
		return -EINVAL;

	/*
	 * The second field must be a 32 bit integer at 64 bit offset.
	 */
	m++;
	offset = offsetof(struct bpf_cgroup_storage_key, attach_type);
	size = FIELD_SIZEOF(struct bpf_cgroup_storage_key, attach_type);
	if (!btf_member_is_reg_int(btf, key_type, m, offset, size))
		return -EINVAL;

	return 0;
}

static void cgroup_storage_seq_show_elem(struct bpf_map *map, void *_key,
					 struct seq_file *m)
{
	enum bpf_cgroup_storage_type stype = cgroup_storage_type(map);
	struct bpf_cgroup_storage_key *key = _key;
	struct bpf_cgroup_storage *storage;
	int cpu;

	rcu_read_lock();
	storage = cgroup_storage_lookup(map_to_storage(map), key, false);
	if (!storage) {
		rcu_read_unlock();
		return;
	}

	btf_type_seq_show(map->btf, map->btf_key_type_id, key, m);
	stype = cgroup_storage_type(map);
	if (stype == BPF_CGROUP_STORAGE_SHARED) {
		seq_puts(m, ": ");
		btf_type_seq_show(map->btf, map->btf_value_type_id,
				  &READ_ONCE(storage->buf)->data[0], m);
		seq_puts(m, "\n");
	} else {
		seq_puts(m, ": {\n");
		for_each_possible_cpu(cpu) {
			seq_printf(m, "\tcpu%d: ", cpu);
			btf_type_seq_show(map->btf, map->btf_value_type_id,
					  per_cpu_ptr(storage->percpu_buf, cpu),
					  m);
			seq_puts(m, "\n");
		}
		seq_puts(m, "}\n");
	}
	rcu_read_unlock();
}

const struct bpf_map_ops cgroup_storage_map_ops = {
	.map_alloc = cgroup_storage_map_alloc,
	.map_free = cgroup_storage_map_free,
	.map_get_next_key = cgroup_storage_get_next_key,
	.map_lookup_elem = cgroup_storage_lookup_elem,
	.map_update_elem = cgroup_storage_update_elem,
	.map_delete_elem = cgroup_storage_delete_elem,
	.map_check_btf = cgroup_storage_check_btf,
	.map_seq_show_elem = cgroup_storage_seq_show_elem,
};

int bpf_cgroup_storage_assign(struct bpf_prog *prog, struct bpf_map *_map)
{
	enum bpf_cgroup_storage_type stype = cgroup_storage_type(_map);
	struct bpf_cgroup_storage_map *map = map_to_storage(_map);
	int ret = -EBUSY;

	spin_lock_bh(&map->lock);

	if (map->prog && map->prog != prog)
		goto unlock;
	if (prog->aux->cgroup_storage[stype] &&
	    prog->aux->cgroup_storage[stype] != _map)
		goto unlock;

	map->prog = prog;
	prog->aux->cgroup_storage[stype] = _map;
	ret = 0;
unlock:
	spin_unlock_bh(&map->lock);

	return ret;
}

void bpf_cgroup_storage_release(struct bpf_prog *prog, struct bpf_map *_map)
{
	enum bpf_cgroup_storage_type stype = cgroup_storage_type(_map);
	struct bpf_cgroup_storage_map *map = map_to_storage(_map);

	spin_lock_bh(&map->lock);
	if (map->prog == prog) {
		WARN_ON(prog->aux->cgroup_storage[stype] != _map);
		map->prog = NULL;
		prog->aux->cgroup_storage[stype] = NULL;
	}
	spin_unlock_bh(&map->lock);
}

static size_t bpf_cgroup_storage_calculate_size(struct bpf_map *map, u32 *pages)
{
	size_t size;

	if (cgroup_storage_type(map) == BPF_CGROUP_STORAGE_SHARED) {
		size = sizeof(struct bpf_storage_buffer) + map->value_size;
		*pages = round_up(sizeof(struct bpf_cgroup_storage) + size,
				  PAGE_SIZE) >> PAGE_SHIFT;
	} else {
		size = map->value_size;
		*pages = round_up(round_up(size, 8) * num_possible_cpus(),
				  PAGE_SIZE) >> PAGE_SHIFT;
	}

	return size;
}

struct bpf_cgroup_storage *bpf_cgroup_storage_alloc(struct bpf_prog *prog,
					enum bpf_cgroup_storage_type stype)
{
	struct bpf_cgroup_storage *storage;
	struct bpf_map *map;
	gfp_t flags;
	size_t size;
	u32 pages;

	map = prog->aux->cgroup_storage[stype];
	if (!map)
		return NULL;

	size = bpf_cgroup_storage_calculate_size(map, &pages);

	if (bpf_map_charge_memlock(map, pages))
		return ERR_PTR(-EPERM);

	storage = kmalloc_node(sizeof(struct bpf_cgroup_storage),
			       __GFP_ZERO | GFP_USER, map->numa_node);
	if (!storage)
		goto enomem;

	flags = __GFP_ZERO | GFP_USER;

	if (stype == BPF_CGROUP_STORAGE_SHARED) {
		storage->buf = kmalloc_node(size, flags, map->numa_node);
		if (!storage->buf)
			goto enomem;
		check_and_init_map_lock(map, storage->buf->data);
	} else {
		storage->percpu_buf = __alloc_percpu_gfp(size, 8, flags);
		if (!storage->percpu_buf)
			goto enomem;
	}

	storage->map = (struct bpf_cgroup_storage_map *)map;

	return storage;

enomem:
	bpf_map_uncharge_memlock(map, pages);
	kfree(storage);
	return ERR_PTR(-ENOMEM);
}

static void free_shared_cgroup_storage_rcu(struct rcu_head *rcu)
{
	struct bpf_cgroup_storage *storage =
		container_of(rcu, struct bpf_cgroup_storage, rcu);

	kfree(storage->buf);
	kfree(storage);
}

static void free_percpu_cgroup_storage_rcu(struct rcu_head *rcu)
{
	struct bpf_cgroup_storage *storage =
		container_of(rcu, struct bpf_cgroup_storage, rcu);

	free_percpu(storage->percpu_buf);
	kfree(storage);
}

void bpf_cgroup_storage_free(struct bpf_cgroup_storage *storage)
{
	enum bpf_cgroup_storage_type stype;
	struct bpf_map *map;
	u32 pages;

	if (!storage)
		return;

	map = &storage->map->map;

	bpf_cgroup_storage_calculate_size(map, &pages);
	bpf_map_uncharge_memlock(map, pages);

	stype = cgroup_storage_type(map);
	if (stype == BPF_CGROUP_STORAGE_SHARED)
		call_rcu(&storage->rcu, free_shared_cgroup_storage_rcu);
	else
		call_rcu(&storage->rcu, free_percpu_cgroup_storage_rcu);
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
