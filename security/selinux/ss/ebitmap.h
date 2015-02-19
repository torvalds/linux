/*
 * An extensible bitmap is a bitmap that supports an
 * arbitrary number of bits.  Extensible bitmaps are
 * used to represent sets of values, such as types,
 * roles, categories, and classes.
 *
 * Each extensible bitmap is implemented as a linked
 * list of bitmap nodes, where each bitmap node has
 * an explicitly specified starting bit position within
 * the total bitmap.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#ifndef _SS_EBITMAP_H_
#define _SS_EBITMAP_H_

#include <net/netlabel.h>

#ifdef CONFIG_64BIT
#define	EBITMAP_NODE_SIZE	64
#else
#define	EBITMAP_NODE_SIZE	32
#endif

#define EBITMAP_UNIT_NUMS	((EBITMAP_NODE_SIZE-sizeof(void *)-sizeof(u32))\
					/ sizeof(unsigned long))
#define EBITMAP_UNIT_SIZE	BITS_PER_LONG
#define EBITMAP_SIZE		(EBITMAP_UNIT_NUMS * EBITMAP_UNIT_SIZE)
#define EBITMAP_BIT		1ULL
#define EBITMAP_SHIFT_UNIT_SIZE(x)					\
	(((x) >> EBITMAP_UNIT_SIZE / 2) >> EBITMAP_UNIT_SIZE / 2)

struct ebitmap_node {
	struct ebitmap_node *next;
	unsigned long maps[EBITMAP_UNIT_NUMS];
	u32 startbit;
};

struct ebitmap {
	struct ebitmap_node *node;	/* first node in the bitmap */
	u32 highbit;	/* highest position in the total bitmap */
};

#define ebitmap_length(e) ((e)->highbit)

static inline unsigned int ebitmap_start_positive(struct ebitmap *e,
						  struct ebitmap_node **n)
{
	unsigned int ofs;

	for (*n = e->node; *n; *n = (*n)->next) {
		ofs = find_first_bit((*n)->maps, EBITMAP_SIZE);
		if (ofs < EBITMAP_SIZE)
			return (*n)->startbit + ofs;
	}
	return ebitmap_length(e);
}

static inline void ebitmap_init(struct ebitmap *e)
{
	memset(e, 0, sizeof(*e));
}

static inline unsigned int ebitmap_next_positive(struct ebitmap *e,
						 struct ebitmap_node **n,
						 unsigned int bit)
{
	unsigned int ofs;

	ofs = find_next_bit((*n)->maps, EBITMAP_SIZE, bit - (*n)->startbit + 1);
	if (ofs < EBITMAP_SIZE)
		return ofs + (*n)->startbit;

	for (*n = (*n)->next; *n; *n = (*n)->next) {
		ofs = find_first_bit((*n)->maps, EBITMAP_SIZE);
		if (ofs < EBITMAP_SIZE)
			return ofs + (*n)->startbit;
	}
	return ebitmap_length(e);
}

#define EBITMAP_NODE_INDEX(node, bit)	\
	(((bit) - (node)->startbit) / EBITMAP_UNIT_SIZE)
#define EBITMAP_NODE_OFFSET(node, bit)	\
	(((bit) - (node)->startbit) % EBITMAP_UNIT_SIZE)

static inline int ebitmap_node_get_bit(struct ebitmap_node *n,
				       unsigned int bit)
{
	unsigned int index = EBITMAP_NODE_INDEX(n, bit);
	unsigned int ofs = EBITMAP_NODE_OFFSET(n, bit);

	BUG_ON(index >= EBITMAP_UNIT_NUMS);
	if ((n->maps[index] & (EBITMAP_BIT << ofs)))
		return 1;
	return 0;
}

static inline void ebitmap_node_set_bit(struct ebitmap_node *n,
					unsigned int bit)
{
	unsigned int index = EBITMAP_NODE_INDEX(n, bit);
	unsigned int ofs = EBITMAP_NODE_OFFSET(n, bit);

	BUG_ON(index >= EBITMAP_UNIT_NUMS);
	n->maps[index] |= (EBITMAP_BIT << ofs);
}

static inline void ebitmap_node_clr_bit(struct ebitmap_node *n,
					unsigned int bit)
{
	unsigned int index = EBITMAP_NODE_INDEX(n, bit);
	unsigned int ofs = EBITMAP_NODE_OFFSET(n, bit);

	BUG_ON(index >= EBITMAP_UNIT_NUMS);
	n->maps[index] &= ~(EBITMAP_BIT << ofs);
}

#define ebitmap_for_each_positive_bit(e, n, bit)	\
	for (bit = ebitmap_start_positive(e, &n);	\
	     bit < ebitmap_length(e);			\
	     bit = ebitmap_next_positive(e, &n, bit))	\

int ebitmap_cmp(struct ebitmap *e1, struct ebitmap *e2);
int ebitmap_cpy(struct ebitmap *dst, struct ebitmap *src);
int ebitmap_contains(struct ebitmap *e1, struct ebitmap *e2, u32 last_e2bit);
int ebitmap_get_bit(struct ebitmap *e, unsigned long bit);
int ebitmap_set_bit(struct ebitmap *e, unsigned long bit, int value);
void ebitmap_destroy(struct ebitmap *e);
int ebitmap_read(struct ebitmap *e, void *fp);
int ebitmap_write(struct ebitmap *e, void *fp);

#ifdef CONFIG_NETLABEL
int ebitmap_netlbl_export(struct ebitmap *ebmap,
			  struct netlbl_lsm_catmap **catmap);
int ebitmap_netlbl_import(struct ebitmap *ebmap,
			  struct netlbl_lsm_catmap *catmap);
#else
static inline int ebitmap_netlbl_export(struct ebitmap *ebmap,
					struct netlbl_lsm_catmap **catmap)
{
	return -ENOMEM;
}
static inline int ebitmap_netlbl_import(struct ebitmap *ebmap,
					struct netlbl_lsm_catmap *catmap)
{
	return -ENOMEM;
}
#endif

#endif	/* _SS_EBITMAP_H_ */
