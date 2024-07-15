// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "disk_groups.h"
#include "sb-members.h"
#include "super-io.h"

#include <linux/sort.h>

static int group_cmp(const void *_l, const void *_r)
{
	const struct bch_disk_group *l = _l;
	const struct bch_disk_group *r = _r;

	return ((BCH_GROUP_DELETED(l) > BCH_GROUP_DELETED(r)) -
		(BCH_GROUP_DELETED(l) < BCH_GROUP_DELETED(r))) ?:
		((BCH_GROUP_PARENT(l) > BCH_GROUP_PARENT(r)) -
		 (BCH_GROUP_PARENT(l) < BCH_GROUP_PARENT(r))) ?:
		strncmp(l->label, r->label, sizeof(l->label));
}

static int bch2_sb_disk_groups_validate(struct bch_sb *sb,
					struct bch_sb_field *f,
					struct printbuf *err)
{
	struct bch_sb_field_disk_groups *groups =
		field_to_type(f, disk_groups);
	struct bch_disk_group *g, *sorted = NULL;
	unsigned nr_groups = disk_groups_nr(groups);
	unsigned i, len;
	int ret = 0;

	for (i = 0; i < sb->nr_devices; i++) {
		struct bch_member m = bch2_sb_member_get(sb, i);
		unsigned group_id;

		if (!BCH_MEMBER_GROUP(&m))
			continue;

		group_id = BCH_MEMBER_GROUP(&m) - 1;

		if (group_id >= nr_groups) {
			prt_printf(err, "disk %u has invalid label %u (have %u)",
				   i, group_id, nr_groups);
			return -BCH_ERR_invalid_sb_disk_groups;
		}

		if (BCH_GROUP_DELETED(&groups->entries[group_id])) {
			prt_printf(err, "disk %u has deleted label %u", i, group_id);
			return -BCH_ERR_invalid_sb_disk_groups;
		}
	}

	if (!nr_groups)
		return 0;

	for (i = 0; i < nr_groups; i++) {
		g = groups->entries + i;

		if (BCH_GROUP_DELETED(g))
			continue;

		len = strnlen(g->label, sizeof(g->label));
		if (!len) {
			prt_printf(err, "label %u empty", i);
			return -BCH_ERR_invalid_sb_disk_groups;
		}
	}

	sorted = kmalloc_array(nr_groups, sizeof(*sorted), GFP_KERNEL);
	if (!sorted)
		return -BCH_ERR_ENOMEM_disk_groups_validate;

	memcpy(sorted, groups->entries, nr_groups * sizeof(*sorted));
	sort(sorted, nr_groups, sizeof(*sorted), group_cmp, NULL);

	for (g = sorted; g + 1 < sorted + nr_groups; g++)
		if (!BCH_GROUP_DELETED(g) &&
		    !group_cmp(&g[0], &g[1])) {
			prt_printf(err, "duplicate label %llu.%.*s",
			       BCH_GROUP_PARENT(g),
			       (int) sizeof(g->label), g->label);
			ret = -BCH_ERR_invalid_sb_disk_groups;
			goto err;
		}
err:
	kfree(sorted);
	return ret;
}

void bch2_disk_groups_to_text(struct printbuf *out, struct bch_fs *c)
{
	out->atomic++;
	rcu_read_lock();

	struct bch_disk_groups_cpu *g = rcu_dereference(c->disk_groups);
	if (!g)
		goto out;

	for (unsigned i = 0; i < g->nr; i++) {
		if (i)
			prt_printf(out, " ");

		if (g->entries[i].deleted) {
			prt_printf(out, "[deleted]");
			continue;
		}

		prt_printf(out, "[parent %d devs", g->entries[i].parent);
		for_each_member_device_rcu(c, ca, &g->entries[i].devs)
			prt_printf(out, " %s", ca->name);
		prt_printf(out, "]");
	}

out:
	rcu_read_unlock();
	out->atomic--;
}

static void bch2_sb_disk_groups_to_text(struct printbuf *out,
					struct bch_sb *sb,
					struct bch_sb_field *f)
{
	struct bch_sb_field_disk_groups *groups =
		field_to_type(f, disk_groups);
	struct bch_disk_group *g;
	unsigned nr_groups = disk_groups_nr(groups);

	for (g = groups->entries;
	     g < groups->entries + nr_groups;
	     g++) {
		if (g != groups->entries)
			prt_printf(out, " ");

		if (BCH_GROUP_DELETED(g))
			prt_printf(out, "[deleted]");
		else
			prt_printf(out, "[parent %llu name %s]",
			       BCH_GROUP_PARENT(g), g->label);
	}
}

const struct bch_sb_field_ops bch_sb_field_ops_disk_groups = {
	.validate	= bch2_sb_disk_groups_validate,
	.to_text	= bch2_sb_disk_groups_to_text
};

int bch2_sb_disk_groups_to_cpu(struct bch_fs *c)
{
	struct bch_sb_field_disk_groups *groups;
	struct bch_disk_groups_cpu *cpu_g, *old_g;
	unsigned i, g, nr_groups;

	lockdep_assert_held(&c->sb_lock);

	groups		= bch2_sb_field_get(c->disk_sb.sb, disk_groups);
	nr_groups	= disk_groups_nr(groups);

	if (!groups)
		return 0;

	cpu_g = kzalloc(struct_size(cpu_g, entries, nr_groups), GFP_KERNEL);
	if (!cpu_g)
		return -BCH_ERR_ENOMEM_disk_groups_to_cpu;

	cpu_g->nr = nr_groups;

	for (i = 0; i < nr_groups; i++) {
		struct bch_disk_group *src	= &groups->entries[i];
		struct bch_disk_group_cpu *dst	= &cpu_g->entries[i];

		dst->deleted	= BCH_GROUP_DELETED(src);
		dst->parent	= BCH_GROUP_PARENT(src);
		memcpy(dst->label, src->label, sizeof(dst->label));
	}

	for (i = 0; i < c->disk_sb.sb->nr_devices; i++) {
		struct bch_member m = bch2_sb_member_get(c->disk_sb.sb, i);
		struct bch_disk_group_cpu *dst;

		if (!bch2_member_exists(&m))
			continue;

		g = BCH_MEMBER_GROUP(&m);
		while (g) {
			dst = &cpu_g->entries[g - 1];
			__set_bit(i, dst->devs.d);
			g = dst->parent;
		}
	}

	old_g = rcu_dereference_protected(c->disk_groups,
				lockdep_is_held(&c->sb_lock));
	rcu_assign_pointer(c->disk_groups, cpu_g);
	if (old_g)
		kfree_rcu(old_g, rcu);

	return 0;
}

const struct bch_devs_mask *bch2_target_to_mask(struct bch_fs *c, unsigned target)
{
	struct target t = target_decode(target);
	struct bch_devs_mask *devs;

	rcu_read_lock();

	switch (t.type) {
	case TARGET_NULL:
		devs = NULL;
		break;
	case TARGET_DEV: {
		struct bch_dev *ca = t.dev < c->sb.nr_devices
			? rcu_dereference(c->devs[t.dev])
			: NULL;
		devs = ca ? &ca->self : NULL;
		break;
	}
	case TARGET_GROUP: {
		struct bch_disk_groups_cpu *g = rcu_dereference(c->disk_groups);

		devs = g && t.group < g->nr && !g->entries[t.group].deleted
			? &g->entries[t.group].devs
			: NULL;
		break;
	}
	default:
		BUG();
	}

	rcu_read_unlock();

	return devs;
}

bool bch2_dev_in_target(struct bch_fs *c, unsigned dev, unsigned target)
{
	struct target t = target_decode(target);

	switch (t.type) {
	case TARGET_NULL:
		return false;
	case TARGET_DEV:
		return dev == t.dev;
	case TARGET_GROUP: {
		struct bch_disk_groups_cpu *g;
		const struct bch_devs_mask *m;
		bool ret;

		rcu_read_lock();
		g = rcu_dereference(c->disk_groups);
		m = g && t.group < g->nr && !g->entries[t.group].deleted
			? &g->entries[t.group].devs
			: NULL;

		ret = m ? test_bit(dev, m->d) : false;
		rcu_read_unlock();

		return ret;
	}
	default:
		BUG();
	}
}

static int __bch2_disk_group_find(struct bch_sb_field_disk_groups *groups,
				  unsigned parent,
				  const char *name, unsigned namelen)
{
	unsigned i, nr_groups = disk_groups_nr(groups);

	if (!namelen || namelen > BCH_SB_LABEL_SIZE)
		return -EINVAL;

	for (i = 0; i < nr_groups; i++) {
		struct bch_disk_group *g = groups->entries + i;

		if (BCH_GROUP_DELETED(g))
			continue;

		if (!BCH_GROUP_DELETED(g) &&
		    BCH_GROUP_PARENT(g) == parent &&
		    strnlen(g->label, sizeof(g->label)) == namelen &&
		    !memcmp(name, g->label, namelen))
			return i;
	}

	return -1;
}

static int __bch2_disk_group_add(struct bch_sb_handle *sb, unsigned parent,
				 const char *name, unsigned namelen)
{
	struct bch_sb_field_disk_groups *groups =
		bch2_sb_field_get(sb->sb, disk_groups);
	unsigned i, nr_groups = disk_groups_nr(groups);
	struct bch_disk_group *g;

	if (!namelen || namelen > BCH_SB_LABEL_SIZE)
		return -EINVAL;

	for (i = 0;
	     i < nr_groups && !BCH_GROUP_DELETED(&groups->entries[i]);
	     i++)
		;

	if (i == nr_groups) {
		unsigned u64s =
			(sizeof(struct bch_sb_field_disk_groups) +
			 sizeof(struct bch_disk_group) * (nr_groups + 1)) /
			sizeof(u64);

		groups = bch2_sb_field_resize(sb, disk_groups, u64s);
		if (!groups)
			return -BCH_ERR_ENOSPC_disk_label_add;

		nr_groups = disk_groups_nr(groups);
	}

	BUG_ON(i >= nr_groups);

	g = &groups->entries[i];

	memcpy(g->label, name, namelen);
	if (namelen < sizeof(g->label))
		g->label[namelen] = '\0';
	SET_BCH_GROUP_DELETED(g, 0);
	SET_BCH_GROUP_PARENT(g, parent);
	SET_BCH_GROUP_DATA_ALLOWED(g, ~0);

	return i;
}

int bch2_disk_path_find(struct bch_sb_handle *sb, const char *name)
{
	struct bch_sb_field_disk_groups *groups =
		bch2_sb_field_get(sb->sb, disk_groups);
	int v = -1;

	do {
		const char *next = strchrnul(name, '.');
		unsigned len = next - name;

		if (*next == '.')
			next++;

		v = __bch2_disk_group_find(groups, v + 1, name, len);
		name = next;
	} while (*name && v >= 0);

	return v;
}

int bch2_disk_path_find_or_create(struct bch_sb_handle *sb, const char *name)
{
	struct bch_sb_field_disk_groups *groups;
	unsigned parent = 0;
	int v = -1;

	do {
		const char *next = strchrnul(name, '.');
		unsigned len = next - name;

		if (*next == '.')
			next++;

		groups = bch2_sb_field_get(sb->sb, disk_groups);

		v = __bch2_disk_group_find(groups, parent, name, len);
		if (v < 0)
			v = __bch2_disk_group_add(sb, parent, name, len);
		if (v < 0)
			return v;

		parent = v + 1;
		name = next;
	} while (*name && v >= 0);

	return v;
}

void bch2_disk_path_to_text(struct printbuf *out, struct bch_fs *c, unsigned v)
{
	struct bch_disk_groups_cpu *groups;
	struct bch_disk_group_cpu *g;
	unsigned nr = 0;
	u16 path[32];

	out->atomic++;
	rcu_read_lock();
	groups = rcu_dereference(c->disk_groups);
	if (!groups)
		goto invalid;

	while (1) {
		if (nr == ARRAY_SIZE(path))
			goto invalid;

		if (v >= groups->nr)
			goto invalid;

		g = groups->entries + v;

		if (g->deleted)
			goto invalid;

		path[nr++] = v;

		if (!g->parent)
			break;

		v = g->parent - 1;
	}

	while (nr) {
		v = path[--nr];
		g = groups->entries + v;

		prt_printf(out, "%.*s", (int) sizeof(g->label), g->label);
		if (nr)
			prt_printf(out, ".");
	}
out:
	rcu_read_unlock();
	out->atomic--;
	return;
invalid:
	prt_printf(out, "invalid label %u", v);
	goto out;
}

void bch2_disk_path_to_text_sb(struct printbuf *out, struct bch_sb *sb, unsigned v)
{
	struct bch_sb_field_disk_groups *groups =
		bch2_sb_field_get(sb, disk_groups);
	struct bch_disk_group *g;
	unsigned nr = 0;
	u16 path[32];

	while (1) {
		if (nr == ARRAY_SIZE(path))
			goto inval;

		if (v >= disk_groups_nr(groups))
			goto inval;

		g = groups->entries + v;

		if (BCH_GROUP_DELETED(g))
			goto inval;

		path[nr++] = v;

		if (!BCH_GROUP_PARENT(g))
			break;

		v = BCH_GROUP_PARENT(g) - 1;
	}

	while (nr) {
		v = path[--nr];
		g = groups->entries + v;

		prt_printf(out, "%.*s", (int) sizeof(g->label), g->label);
		if (nr)
			prt_printf(out, ".");
	}
	return;
inval:
	prt_printf(out, "invalid label %u", v);
}

int __bch2_dev_group_set(struct bch_fs *c, struct bch_dev *ca, const char *name)
{
	struct bch_member *mi;
	int ret, v = -1;

	if (!strlen(name) || !strcmp(name, "none"))
		return 0;

	v = bch2_disk_path_find_or_create(&c->disk_sb, name);
	if (v < 0)
		return v;

	ret = bch2_sb_disk_groups_to_cpu(c);
	if (ret)
		return ret;

	mi = bch2_members_v2_get_mut(c->disk_sb.sb, ca->dev_idx);
	SET_BCH_MEMBER_GROUP(mi, v + 1);
	return 0;
}

int bch2_dev_group_set(struct bch_fs *c, struct bch_dev *ca, const char *name)
{
	int ret;

	mutex_lock(&c->sb_lock);
	ret = __bch2_dev_group_set(c, ca, name) ?:
		bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	return ret;
}

int bch2_opt_target_parse(struct bch_fs *c, const char *val, u64 *res,
			  struct printbuf *err)
{
	struct bch_dev *ca;
	int g;

	if (!val)
		return -EINVAL;

	if (!c)
		return 0;

	if (!strlen(val) || !strcmp(val, "none")) {
		*res = 0;
		return 0;
	}

	/* Is it a device? */
	ca = bch2_dev_lookup(c, val);
	if (!IS_ERR(ca)) {
		*res = dev_to_target(ca->dev_idx);
		percpu_ref_put(&ca->ref);
		return 0;
	}

	mutex_lock(&c->sb_lock);
	g = bch2_disk_path_find(&c->disk_sb, val);
	mutex_unlock(&c->sb_lock);

	if (g >= 0) {
		*res = group_to_target(g);
		return 0;
	}

	return -EINVAL;
}

void bch2_target_to_text(struct printbuf *out, struct bch_fs *c, unsigned v)
{
	struct target t = target_decode(v);

	switch (t.type) {
	case TARGET_NULL:
		prt_printf(out, "none");
		break;
	case TARGET_DEV: {
		struct bch_dev *ca;

		out->atomic++;
		rcu_read_lock();
		ca = t.dev < c->sb.nr_devices
			? rcu_dereference(c->devs[t.dev])
			: NULL;

		if (ca && percpu_ref_tryget(&ca->io_ref)) {
			prt_printf(out, "/dev/%s", ca->name);
			percpu_ref_put(&ca->io_ref);
		} else if (ca) {
			prt_printf(out, "offline device %u", t.dev);
		} else {
			prt_printf(out, "invalid device %u", t.dev);
		}

		rcu_read_unlock();
		out->atomic--;
		break;
	}
	case TARGET_GROUP:
		bch2_disk_path_to_text(out, c, t.group);
		break;
	default:
		BUG();
	}
}

static void bch2_target_to_text_sb(struct printbuf *out, struct bch_sb *sb, unsigned v)
{
	struct target t = target_decode(v);

	switch (t.type) {
	case TARGET_NULL:
		prt_printf(out, "none");
		break;
	case TARGET_DEV: {
		struct bch_member m = bch2_sb_member_get(sb, t.dev);

		if (bch2_dev_exists(sb, t.dev)) {
			prt_printf(out, "Device ");
			pr_uuid(out, m.uuid.b);
			prt_printf(out, " (%u)", t.dev);
		} else {
			prt_printf(out, "Bad device %u", t.dev);
		}
		break;
	}
	case TARGET_GROUP:
		bch2_disk_path_to_text_sb(out, sb, t.group);
		break;
	default:
		BUG();
	}
}

void bch2_opt_target_to_text(struct printbuf *out,
			     struct bch_fs *c,
			     struct bch_sb *sb,
			     u64 v)
{
	if (c)
		bch2_target_to_text(out, c, v);
	else
		bch2_target_to_text_sb(out, sb, v);
}
