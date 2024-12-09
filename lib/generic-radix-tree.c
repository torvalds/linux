
#include <linux/atomic.h>
#include <linux/export.h>
#include <linux/generic-radix-tree.h>
#include <linux/gfp.h>
#include <linux/kmemleak.h>

/*
 * Returns pointer to the specified byte @offset within @radix, or NULL if not
 * allocated
 */
void *__genradix_ptr(struct __genradix *radix, size_t offset)
{
	return __genradix_ptr_inlined(radix, offset);
}
EXPORT_SYMBOL(__genradix_ptr);

/*
 * Returns pointer to the specified byte @offset within @radix, allocating it if
 * necessary - newly allocated slots are always zeroed out:
 */
void *__genradix_ptr_alloc(struct __genradix *radix, size_t offset,
			   struct genradix_node **preallocated,
			   gfp_t gfp_mask)
{
	struct genradix_root *v = READ_ONCE(radix->root);
	struct genradix_node *n, *new_node = NULL;
	unsigned level;

	if (preallocated)
		swap(new_node, *preallocated);

	/* Increase tree depth if necessary: */
	while (1) {
		struct genradix_root *r = v, *new_root;

		n	= genradix_root_to_node(r);
		level	= genradix_root_to_depth(r);

		if (n && ilog2(offset) < genradix_depth_shift(level))
			break;

		if (!new_node) {
			new_node = genradix_alloc_node(gfp_mask);
			if (!new_node)
				return NULL;
		}

		new_node->children[0] = n;
		new_root = ((struct genradix_root *)
			    ((unsigned long) new_node | (n ? level + 1 : 0)));

		if ((v = cmpxchg_release(&radix->root, r, new_root)) == r) {
			v = new_root;
			new_node = NULL;
		} else {
			new_node->children[0] = NULL;
		}
	}

	while (level--) {
		struct genradix_node **p =
			&n->children[offset >> genradix_depth_shift(level)];
		offset &= genradix_depth_size(level) - 1;

		n = READ_ONCE(*p);
		if (!n) {
			if (!new_node) {
				new_node = genradix_alloc_node(gfp_mask);
				if (!new_node)
					return NULL;
			}

			if (!(n = cmpxchg_release(p, NULL, new_node)))
				swap(n, new_node);
		}
	}

	if (new_node)
		genradix_free_node(new_node);

	return &n->data[offset];
}
EXPORT_SYMBOL(__genradix_ptr_alloc);

void *__genradix_iter_peek(struct genradix_iter *iter,
			   struct __genradix *radix,
			   size_t objs_per_page)
{
	struct genradix_root *r;
	struct genradix_node *n;
	unsigned level, i;

	if (iter->offset == SIZE_MAX)
		return NULL;

restart:
	r = READ_ONCE(radix->root);
	if (!r)
		return NULL;

	n	= genradix_root_to_node(r);
	level	= genradix_root_to_depth(r);

	if (ilog2(iter->offset) >= genradix_depth_shift(level))
		return NULL;

	while (level) {
		level--;

		i = (iter->offset >> genradix_depth_shift(level)) &
			(GENRADIX_ARY - 1);

		while (!n->children[i]) {
			size_t objs_per_ptr = genradix_depth_size(level);

			if (iter->offset + objs_per_ptr < iter->offset) {
				iter->offset	= SIZE_MAX;
				iter->pos	= SIZE_MAX;
				return NULL;
			}

			i++;
			iter->offset = round_down(iter->offset + objs_per_ptr,
						  objs_per_ptr);
			iter->pos = (iter->offset >> GENRADIX_NODE_SHIFT) *
				objs_per_page;
			if (i == GENRADIX_ARY)
				goto restart;
		}

		n = n->children[i];
	}

	return &n->data[iter->offset & (GENRADIX_NODE_SIZE - 1)];
}
EXPORT_SYMBOL(__genradix_iter_peek);

void *__genradix_iter_peek_prev(struct genradix_iter *iter,
				struct __genradix *radix,
				size_t objs_per_page,
				size_t obj_size_plus_page_remainder)
{
	struct genradix_root *r;
	struct genradix_node *n;
	unsigned level, i;

	if (iter->offset == SIZE_MAX)
		return NULL;

restart:
	r = READ_ONCE(radix->root);
	if (!r)
		return NULL;

	n	= genradix_root_to_node(r);
	level	= genradix_root_to_depth(r);

	if (ilog2(iter->offset) >= genradix_depth_shift(level)) {
		iter->offset = genradix_depth_size(level);
		iter->pos = (iter->offset >> GENRADIX_NODE_SHIFT) * objs_per_page;

		iter->offset -= obj_size_plus_page_remainder;
		iter->pos--;
	}

	while (level) {
		level--;

		i = (iter->offset >> genradix_depth_shift(level)) &
			(GENRADIX_ARY - 1);

		while (!n->children[i]) {
			size_t objs_per_ptr = genradix_depth_size(level);

			iter->offset = round_down(iter->offset, objs_per_ptr);
			iter->pos = (iter->offset >> GENRADIX_NODE_SHIFT) * objs_per_page;

			if (!iter->offset)
				return NULL;

			iter->offset -= obj_size_plus_page_remainder;
			iter->pos--;

			if (!i)
				goto restart;
			--i;
		}

		n = n->children[i];
	}

	return &n->data[iter->offset & (GENRADIX_NODE_SIZE - 1)];
}
EXPORT_SYMBOL(__genradix_iter_peek_prev);

static void genradix_free_recurse(struct genradix_node *n, unsigned level)
{
	if (level) {
		unsigned i;

		for (i = 0; i < GENRADIX_ARY; i++)
			if (n->children[i])
				genradix_free_recurse(n->children[i], level - 1);
	}

	genradix_free_node(n);
}

int __genradix_prealloc(struct __genradix *radix, size_t size,
			gfp_t gfp_mask)
{
	size_t offset;

	for (offset = 0; offset < size; offset += GENRADIX_NODE_SIZE)
		if (!__genradix_ptr_alloc(radix, offset, NULL, gfp_mask))
			return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(__genradix_prealloc);

void __genradix_free(struct __genradix *radix)
{
	struct genradix_root *r = xchg(&radix->root, NULL);

	genradix_free_recurse(genradix_root_to_node(r),
			      genradix_root_to_depth(r));
}
EXPORT_SYMBOL(__genradix_free);
