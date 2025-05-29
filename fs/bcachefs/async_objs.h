/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ASYNC_OBJS_H
#define _BCACHEFS_ASYNC_OBJS_H

#ifdef CONFIG_BCACHEFS_ASYNC_OBJECT_LISTS
static inline void __async_object_list_del(struct fast_list *head, unsigned idx)
{
	fast_list_remove(head, idx);
}

static inline int __async_object_list_add(struct fast_list *head, void *obj, unsigned *idx)
{
	int ret = fast_list_add(head, obj);
	*idx = ret > 0 ? ret : 0;
	return ret < 0 ? ret : 0;
}

#define async_object_list_del(_c, _list, idx)		\
	__async_object_list_del(&(_c)->async_objs[BCH_ASYNC_OBJ_LIST_##_list].list, idx)

#define async_object_list_add(_c, _list, obj, idx)		\
	__async_object_list_add(&(_c)->async_objs[BCH_ASYNC_OBJ_LIST_##_list].list, obj, idx)

void bch2_fs_async_obj_debugfs_init(struct bch_fs *);
void bch2_fs_async_obj_exit(struct bch_fs *);
int bch2_fs_async_obj_init(struct bch_fs *);

#else /* CONFIG_BCACHEFS_ASYNC_OBJECT_LISTS */

#define async_object_list_del(_c, _n, idx)		do {} while (0)

static inline int __async_object_list_add(void)
{
	return 0;
}
#define async_object_list_add(_c, _n, obj, idx)		__async_object_list_add()

static inline void bch2_fs_async_obj_debugfs_init(struct bch_fs *c) {}
static inline void bch2_fs_async_obj_exit(struct bch_fs *c) {}
static inline int bch2_fs_async_obj_init(struct bch_fs *c) { return 0; }

#endif /* CONFIG_BCACHEFS_ASYNC_OBJECT_LISTS */

#endif /* _BCACHEFS_ASYNC_OBJS_H */
