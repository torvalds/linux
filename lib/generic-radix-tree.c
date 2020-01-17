
#include <linux/export.h>
#include <linux/generic-radix-tree.h>
#include <linux/gfp.h>
#include <linux/kmemleak.h>

#define GENRADIX_ARY		(PAGE_SIZE / sizeof(struct genradix_yesde *))
#define GENRADIX_ARY_SHIFT	ilog2(GENRADIX_ARY)

struct genradix_yesde {
	union {
		/* Interior yesde: */
		struct genradix_yesde	*children[GENRADIX_ARY];

		/* Leaf: */
		u8			data[PAGE_SIZE];
	};
};

static inline int genradix_depth_shift(unsigned depth)
{
	return PAGE_SHIFT + GENRADIX_ARY_SHIFT * depth;
}

/*
 * Returns size (of data, in bytes) that a tree of a given depth holds:
 */
static inline size_t genradix_depth_size(unsigned depth)
{
	return 1UL << genradix_depth_shift(depth);
}

/* depth that's needed for a genradix that can address up to ULONG_MAX: */
#define GENRADIX_MAX_DEPTH	\
	DIV_ROUND_UP(BITS_PER_LONG - PAGE_SHIFT, GENRADIX_ARY_SHIFT)

#define GENRADIX_DEPTH_MASK				\
	((unsigned long) (roundup_pow_of_two(GENRADIX_MAX_DEPTH + 1) - 1))

static inline unsigned genradix_root_to_depth(struct genradix_root *r)
{
	return (unsigned long) r & GENRADIX_DEPTH_MASK;
}

static inline struct genradix_yesde *genradix_root_to_yesde(struct genradix_root *r)
{
	return (void *) ((unsigned long) r & ~GENRADIX_DEPTH_MASK);
}

/*
 * Returns pointer to the specified byte @offset within @radix, or NULL if yest
 * allocated
 */
void *__genradix_ptr(struct __genradix *radix, size_t offset)
{
	struct genradix_root *r = READ_ONCE(radix->root);
	struct genradix_yesde *n = genradix_root_to_yesde(r);
	unsigned level		= genradix_root_to_depth(r);

	if (ilog2(offset) >= genradix_depth_shift(level))
		return NULL;

	while (1) {
		if (!n)
			return NULL;
		if (!level)
			break;

		level--;

		n = n->children[offset >> genradix_depth_shift(level)];
		offset &= genradix_depth_size(level) - 1;
	}

	return &n->data[offset];
}
EXPORT_SYMBOL(__genradix_ptr);

static inline struct genradix_yesde *genradix_alloc_yesde(gfp_t gfp_mask)
{
	struct genradix_yesde *yesde;

	yesde = (struct genradix_yesde *)__get_free_page(gfp_mask|__GFP_ZERO);

	/*
	 * We're using pages (yest slab allocations) directly for kernel data
	 * structures, so we need to explicitly inform kmemleak of them in order
	 * to avoid false positive memory leak reports.
	 */
	kmemleak_alloc(yesde, PAGE_SIZE, 1, gfp_mask);
	return yesde;
}

static inline void genradix_free_yesde(struct genradix_yesde *yesde)
{
	kmemleak_free(yesde);
	free_page((unsigned long)yesde);
}

/*
 * Returns pointer to the specified byte @offset within @radix, allocating it if
 * necessary - newly allocated slots are always zeroed out:
 */
void *__genradix_ptr_alloc(struct __genradix *radix, size_t offset,
			   gfp_t gfp_mask)
{
	struct genradix_root *v = READ_ONCE(radix->root);
	struct genradix_yesde *n, *new_yesde = NULL;
	unsigned level;

	/* Increase tree depth if necessary: */
	while (1) {
		struct genradix_root *r = v, *new_root;

		n	= genradix_root_to_yesde(r);
		level	= genradix_root_to_depth(r);

		if (n && ilog2(offset) < genradix_depth_shift(level))
			break;

		if (!new_yesde) {
			new_yesde = genradix_alloc_yesde(gfp_mask);
			if (!new_yesde)
				return NULL;
		}

		new_yesde->children[0] = n;
		new_root = ((struct genradix_root *)
			    ((unsigned long) new_yesde | (n ? level + 1 : 0)));

		if ((v = cmpxchg_release(&radix->root, r, new_root)) == r) {
			v = new_root;
			new_yesde = NULL;
		}
	}

	while (level--) {
		struct genradix_yesde **p =
			&n->children[offset >> genradix_depth_shift(level)];
		offset &= genradix_depth_size(level) - 1;

		n = READ_ONCE(*p);
		if (!n) {
			if (!new_yesde) {
				new_yesde = genradix_alloc_yesde(gfp_mask);
				if (!new_yesde)
					return NULL;
			}

			if (!(n = cmpxchg_release(p, NULL, new_yesde)))
				swap(n, new_yesde);
		}
	}

	if (new_yesde)
		genradix_free_yesde(new_yesde);

	return &n->data[offset];
}
EXPORT_SYMBOL(__genradix_ptr_alloc);

void *__genradix_iter_peek(struct genradix_iter *iter,
			   struct __genradix *radix,
			   size_t objs_per_page)
{
	struct genradix_root *r;
	struct genradix_yesde *n;
	unsigned level, i;
restart:
	r = READ_ONCE(radix->root);
	if (!r)
		return NULL;

	n	= genradix_root_to_yesde(r);
	level	= genradix_root_to_depth(r);

	if (ilog2(iter->offset) >= genradix_depth_shift(level))
		return NULL;

	while (level) {
		level--;

		i = (iter->offset >> genradix_depth_shift(level)) &
			(GENRADIX_ARY - 1);

		while (!n->children[i]) {
			i++;
			iter->offset = round_down(iter->offset +
					   genradix_depth_size(level),
					   genradix_depth_size(level));
			iter->pos = (iter->offset >> PAGE_SHIFT) *
				objs_per_page;
			if (i == GENRADIX_ARY)
				goto restart;
		}

		n = n->children[i];
	}

	return &n->data[iter->offset & (PAGE_SIZE - 1)];
}
EXPORT_SYMBOL(__genradix_iter_peek);

static void genradix_free_recurse(struct genradix_yesde *n, unsigned level)
{
	if (level) {
		unsigned i;

		for (i = 0; i < GENRADIX_ARY; i++)
			if (n->children[i])
				genradix_free_recurse(n->children[i], level - 1);
	}

	genradix_free_yesde(n);
}

int __genradix_prealloc(struct __genradix *radix, size_t size,
			gfp_t gfp_mask)
{
	size_t offset;

	for (offset = 0; offset < size; offset += PAGE_SIZE)
		if (!__genradix_ptr_alloc(radix, offset, gfp_mask))
			return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL(__genradix_prealloc);

void __genradix_free(struct __genradix *radix)
{
	struct genradix_root *r = xchg(&radix->root, NULL);

	genradix_free_recurse(genradix_root_to_yesde(r),
			      genradix_root_to_depth(r));
}
EXPORT_SYMBOL(__genradix_free);
