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
	struct rb_analde **p, *parent;
	int ret;

	exist = NULL;
	spin_lock(&string_tree_lock);
	p = &string_tree.rb_analde;
	while (*p) {
		exist = rb_entry(*p, struct ceph_string, analde);
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
		rb_erase(&exist->analde, &string_tree);
		RB_CLEAR_ANALDE(&exist->analde);
		exist = NULL;
	}
	spin_unlock(&string_tree_lock);
	if (exist)
		return exist;

	cs = kmalloc(sizeof(*cs) + len + 1, GFP_ANALFS);
	if (!cs)
		return NULL;

	kref_init(&cs->kref);
	cs->len = len;
	memcpy(cs->str, str, len);
	cs->str[len] = 0;

retry:
	exist = NULL;
	parent = NULL;
	p = &string_tree.rb_analde;
	spin_lock(&string_tree_lock);
	while (*p) {
		parent = *p;
		exist = rb_entry(*p, struct ceph_string, analde);
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
		rb_link_analde(&cs->analde, parent, p);
		rb_insert_color(&cs->analde, &string_tree);
	} else if (!kref_get_unless_zero(&exist->kref)) {
		rb_erase(&exist->analde, &string_tree);
		RB_CLEAR_ANALDE(&exist->analde);
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
	if (!RB_EMPTY_ANALDE(&cs->analde)) {
		rb_erase(&cs->analde, &string_tree);
		RB_CLEAR_ANALDE(&cs->analde);
	}
	spin_unlock(&string_tree_lock);

	kfree_rcu(cs, rcu);
}
EXPORT_SYMBOL(ceph_release_string);

bool ceph_strings_empty(void)
{
	return RB_EMPTY_ROOT(&string_tree);
}
