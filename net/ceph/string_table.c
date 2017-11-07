// SPDX-License-Identifier: GPL-2.0
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/ceph/string_table.h>

static DEFINE_SPINLOCK(string_tree_lock);
static struct rb_root string_tree = RB_ROOT;

struct ceph_string *ceph_find_or_create_string(const char* str, size_t len)
{
	struct ceph_string *cs, *exist;
	struct rb_node **p, *parent;
	int ret;

	exist = NULL;
	spin_lock(&string_tree_lock);
	p = &string_tree.rb_node;
	while (*p) {
		exist = rb_entry(*p, struct ceph_string, node);
		ret = ceph_compare_string(exist, str, len);
		if (ret > 0)
			p = &(*p)->rb_left;
		else if (ret < 0)
			p = &(*p)->rb_right;
		else
			break;
		exist = NULL;
	}
	if (exist && !kref_get_unless_zero(&exist->kref)) {
		rb_erase(&exist->node, &string_tree);
		RB_CLEAR_NODE(&exist->node);
		exist = NULL;
	}
	spin_unlock(&string_tree_lock);
	if (exist)
		return exist;

	cs = kmalloc(sizeof(*cs) + len + 1, GFP_NOFS);
	if (!cs)
		return NULL;

	kref_init(&cs->kref);
	cs->len = len;
	memcpy(cs->str, str, len);
	cs->str[len] = 0;

retry:
	exist = NULL;
	parent = NULL;
	p = &string_tree.rb_node;
	spin_lock(&string_tree_lock);
	while (*p) {
		parent = *p;
		exist = rb_entry(*p, struct ceph_string, node);
		ret = ceph_compare_string(exist, str, len);
		if (ret > 0)
			p = &(*p)->rb_left;
		else if (ret < 0)
			p = &(*p)->rb_right;
		else
			break;
		exist = NULL;
	}
	ret = 0;
	if (!exist) {
		rb_link_node(&cs->node, parent, p);
		rb_insert_color(&cs->node, &string_tree);
	} else if (!kref_get_unless_zero(&exist->kref)) {
		rb_erase(&exist->node, &string_tree);
		RB_CLEAR_NODE(&exist->node);
		ret = -EAGAIN;
	}
	spin_unlock(&string_tree_lock);
	if (ret == -EAGAIN)
		goto retry;

	if (exist) {
		kfree(cs);
		cs = exist;
	}

	return cs;
}
EXPORT_SYMBOL(ceph_find_or_create_string);

void ceph_release_string(struct kref *ref)
{
	struct ceph_string *cs = container_of(ref, struct ceph_string, kref);

	spin_lock(&string_tree_lock);
	if (!RB_EMPTY_NODE(&cs->node)) {
		rb_erase(&cs->node, &string_tree);
		RB_CLEAR_NODE(&cs->node);
	}
	spin_unlock(&string_tree_lock);

	kfree_rcu(cs, rcu);
}
EXPORT_SYMBOL(ceph_release_string);

bool ceph_strings_empty(void)
{
	return RB_EMPTY_ROOT(&string_tree);
}
