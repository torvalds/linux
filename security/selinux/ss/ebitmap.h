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

#define MAPTYPE u64			/* portion of bitmap in each node */
#define MAPSIZE (sizeof(MAPTYPE) * 8)	/* number of bits in node bitmap */
#define MAPBIT  1ULL			/* a bit in the node bitmap */

struct ebitmap_node {
	u32 startbit;		/* starting position in the total bitmap */
	MAPTYPE map;		/* this node's portion of the bitmap */
	struct ebitmap_node *next;
};

struct ebitmap {
	struct ebitmap_node *node;	/* first node in the bitmap */
	u32 highbit;	/* highest position in the total bitmap */
};

#define ebitmap_length(e) ((e)->highbit)
#define ebitmap_startbit(e) ((e)->node ? (e)->node->startbit : 0)

static inline unsigned int ebitmap_start(struct ebitmap *e,
					 struct ebitmap_node **n)
{
	*n = e->node;
	return ebitmap_startbit(e);
}

static inline void ebitmap_init(struct ebitmap *e)
{
	memset(e, 0, sizeof(*e));
}

static inline unsigned int ebitmap_next(struct ebitmap_node **n,
					unsigned int bit)
{
	if ((bit == ((*n)->startbit + MAPSIZE - 1)) &&
	    (*n)->next) {
		*n = (*n)->next;
		return (*n)->startbit;
	}

	return (bit+1);
}

static inline int ebitmap_node_get_bit(struct ebitmap_node * n,
				       unsigned int bit)
{
	if (n->map & (MAPBIT << (bit - n->startbit)))
		return 1;
	return 0;
}

#define ebitmap_for_each_bit(e, n, bit) \
	for (bit = ebitmap_start(e, &n); bit < ebitmap_length(e); bit = ebitmap_next(&n, bit)) \

int ebitmap_cmp(struct ebitmap *e1, struct ebitmap *e2);
int ebitmap_cpy(struct ebitmap *dst, struct ebitmap *src);
int ebitmap_contains(struct ebitmap *e1, struct ebitmap *e2);
int ebitmap_get_bit(struct ebitmap *e, unsigned long bit);
int ebitmap_set_bit(struct ebitmap *e, unsigned long bit, int value);
void ebitmap_destroy(struct ebitmap *e);
int ebitmap_read(struct ebitmap *e, void *fp);

#ifdef CONFIG_NETLABEL
int ebitmap_netlbl_export(struct ebitmap *ebmap,
			  struct netlbl_lsm_secattr_catmap **catmap);
int ebitmap_netlbl_import(struct ebitmap *ebmap,
			  struct netlbl_lsm_secattr_catmap *catmap);
#else
static inline int ebitmap_netlbl_export(struct ebitmap *ebmap,
				struct netlbl_lsm_secattr_catmap **catmap)
{
	return -ENOMEM;
}
static inline int ebitmap_netlbl_import(struct ebitmap *ebmap,
				struct netlbl_lsm_secattr_catmap *catmap)
{
	return -ENOMEM;
}
#endif

#endif	/* _SS_EBITMAP_H_ */
