#ifndef _FLEX_ARRAY_H
#define _FLEX_ARRAY_H

#include <linux/types.h>
#include <asm/page.h>

#define FLEX_ARRAY_PART_SIZE PAGE_SIZE
#define FLEX_ARRAY_BASE_SIZE PAGE_SIZE

struct flex_array_part;

/*
 * This is meant to replace cases where an array-like
 * structure has gotten too big to fit into kmalloc()
 * and the developer is getting tempted to use
 * vmalloc().
 */

struct flex_array {
	union {
		struct {
			int element_size;
			int total_nr_elements;
			struct flex_array_part *parts[0];
		};
		/*
		 * This little trick makes sure that
		 * sizeof(flex_array) == PAGE_SIZE
		 */
		char padding[FLEX_ARRAY_BASE_SIZE];
	};
};

#define FLEX_ARRAY_INIT(size, total) { { {\
	.element_size = (size),		\
	.total_nr_elements = (total),	\
} } }

struct flex_array *flex_array_alloc(int element_size, int total, gfp_t flags);
int flex_array_prealloc(struct flex_array *fa, int start, int end, gfp_t flags);
void flex_array_free(struct flex_array *fa);
void flex_array_free_parts(struct flex_array *fa);
int flex_array_put(struct flex_array *fa, int element_nr, void *src,
		gfp_t flags);
void *flex_array_get(struct flex_array *fa, int element_nr);

#endif /* _FLEX_ARRAY_H */
