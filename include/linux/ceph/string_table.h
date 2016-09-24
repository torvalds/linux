#ifndef _FS_CEPH_STRING_TABLE_H
#define _FS_CEPH_STRING_TABLE_H

#include <linux/types.h>
#include <linux/kref.h>
#include <linux/rbtree.h>
#include <linux/rcupdate.h>

struct ceph_string {
	struct kref kref;
	union {
		struct rb_node node;
		struct rcu_head rcu;
	};
	size_t len;
	char str[];
};

extern void ceph_release_string(struct kref *ref);
extern struct ceph_string *ceph_find_or_create_string(const char *str,
						      size_t len);
extern bool ceph_strings_empty(void);

static inline struct ceph_string *ceph_get_string(struct ceph_string *str)
{
	kref_get(&str->kref);
	return str;
}

static inline void ceph_put_string(struct ceph_string *str)
{
	if (!str)
		return;
	kref_put(&str->kref, ceph_release_string);
}

static inline int ceph_compare_string(struct ceph_string *cs,
				      const char* str, size_t len)
{
	size_t cs_len = cs ? cs->len : 0;
	if (cs_len != len)
		return cs_len - len;
	if (len == 0)
		return 0;
	return strncmp(cs->str, str, len);
}

#define ceph_try_get_string(x)					\
({								\
	struct ceph_string *___str;				\
	rcu_read_lock();					\
	for (;;) {						\
		___str = rcu_dereference(x);			\
		if (!___str ||					\
		    kref_get_unless_zero(&___str->kref))	\
			break;					\
	}							\
	rcu_read_unlock();					\
	(___str);						\
})

#endif
