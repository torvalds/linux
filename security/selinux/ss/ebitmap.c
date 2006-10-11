/*
 * Implementation of the extensible bitmap type.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
/*
 * Updated: Hewlett-Packard <paul.moore@hp.com>
 *
 *      Added ebitmap_export() and ebitmap_import()
 *
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include "ebitmap.h"
#include "policydb.h"

int ebitmap_cmp(struct ebitmap *e1, struct ebitmap *e2)
{
	struct ebitmap_node *n1, *n2;

	if (e1->highbit != e2->highbit)
		return 0;

	n1 = e1->node;
	n2 = e2->node;
	while (n1 && n2 &&
	       (n1->startbit == n2->startbit) &&
	       (n1->map == n2->map)) {
		n1 = n1->next;
		n2 = n2->next;
	}

	if (n1 || n2)
		return 0;

	return 1;
}

int ebitmap_cpy(struct ebitmap *dst, struct ebitmap *src)
{
	struct ebitmap_node *n, *new, *prev;

	ebitmap_init(dst);
	n = src->node;
	prev = NULL;
	while (n) {
		new = kzalloc(sizeof(*new), GFP_ATOMIC);
		if (!new) {
			ebitmap_destroy(dst);
			return -ENOMEM;
		}
		new->startbit = n->startbit;
		new->map = n->map;
		new->next = NULL;
		if (prev)
			prev->next = new;
		else
			dst->node = new;
		prev = new;
		n = n->next;
	}

	dst->highbit = src->highbit;
	return 0;
}

/**
 * ebitmap_export - Export an ebitmap to a unsigned char bitmap string
 * @src: the ebitmap to export
 * @dst: the resulting bitmap string
 * @dst_len: length of dst in bytes
 *
 * Description:
 * Allocate a buffer at least src->highbit bits long and export the extensible
 * bitmap into the buffer.  The bitmap string will be in little endian format,
 * i.e. LSB first.  The value returned in dst_len may not the true size of the
 * buffer as the length of the buffer is rounded up to a multiple of MAPTYPE.
 * The caller must free the buffer when finished. Returns zero on success,
 * negative values on failure.
 *
 */
int ebitmap_export(const struct ebitmap *src,
		   unsigned char **dst,
		   size_t *dst_len)
{
	size_t bitmap_len;
	unsigned char *bitmap;
	struct ebitmap_node *iter_node;
	MAPTYPE node_val;
	size_t bitmap_byte;
	unsigned char bitmask;

	if (src->highbit == 0) {
		*dst = NULL;
		*dst_len = 0;
		return 0;
	}

	bitmap_len = src->highbit / 8;
	if (src->highbit % 7)
		bitmap_len += 1;

	bitmap = kzalloc((bitmap_len & ~(sizeof(MAPTYPE) - 1)) +
			 sizeof(MAPTYPE),
			 GFP_ATOMIC);
	if (bitmap == NULL)
		return -ENOMEM;

	iter_node = src->node;
	do {
		bitmap_byte = iter_node->startbit / 8;
		bitmask = 0x80;
		node_val = iter_node->map;
		do {
			if (bitmask == 0) {
				bitmap_byte++;
				bitmask = 0x80;
			}
			if (node_val & (MAPTYPE)0x01)
				bitmap[bitmap_byte] |= bitmask;
			node_val >>= 1;
			bitmask >>= 1;
		} while (node_val > 0);
		iter_node = iter_node->next;
	} while (iter_node);

	*dst = bitmap;
	*dst_len = bitmap_len;
	return 0;
}

/**
 * ebitmap_import - Import an unsigned char bitmap string into an ebitmap
 * @src: the bitmap string
 * @src_len: the bitmap length in bytes
 * @dst: the empty ebitmap
 *
 * Description:
 * This function takes a little endian bitmap string in src and imports it into
 * the ebitmap pointed to by dst.  Returns zero on success, negative values on
 * failure.
 *
 */
int ebitmap_import(const unsigned char *src,
		   size_t src_len,
		   struct ebitmap *dst)
{
	size_t src_off = 0;
	size_t node_limit;
	struct ebitmap_node *node_new;
	struct ebitmap_node *node_last = NULL;
	u32 i_byte;
	u32 i_bit;
	unsigned char src_byte;

	while (src_off < src_len) {
		if (src_len - src_off >= sizeof(MAPTYPE)) {
			if (*(MAPTYPE *)&src[src_off] == 0) {
				src_off += sizeof(MAPTYPE);
				continue;
			}
			node_limit = sizeof(MAPTYPE);
		} else {
			for (src_byte = 0, i_byte = src_off;
			     i_byte < src_len && src_byte == 0;
			     i_byte++)
				src_byte |= src[i_byte];
			if (src_byte == 0)
				break;
			node_limit = src_len - src_off;
		}

		node_new = kzalloc(sizeof(*node_new), GFP_ATOMIC);
		if (unlikely(node_new == NULL)) {
			ebitmap_destroy(dst);
			return -ENOMEM;
		}
		node_new->startbit = src_off * 8;
		for (i_byte = 0; i_byte < node_limit; i_byte++) {
			src_byte = src[src_off++];
			for (i_bit = i_byte * 8; src_byte != 0; i_bit++) {
				if (src_byte & 0x80)
					node_new->map |= MAPBIT << i_bit;
				src_byte <<= 1;
			}
		}

		if (node_last != NULL)
			node_last->next = node_new;
		else
			dst->node = node_new;
		node_last = node_new;
	}

	if (likely(node_last != NULL))
		dst->highbit = node_last->startbit + MAPSIZE;
	else
		ebitmap_init(dst);

	return 0;
}

int ebitmap_contains(struct ebitmap *e1, struct ebitmap *e2)
{
	struct ebitmap_node *n1, *n2;

	if (e1->highbit < e2->highbit)
		return 0;

	n1 = e1->node;
	n2 = e2->node;
	while (n1 && n2 && (n1->startbit <= n2->startbit)) {
		if (n1->startbit < n2->startbit) {
			n1 = n1->next;
			continue;
		}
		if ((n1->map & n2->map) != n2->map)
			return 0;

		n1 = n1->next;
		n2 = n2->next;
	}

	if (n2)
		return 0;

	return 1;
}

int ebitmap_get_bit(struct ebitmap *e, unsigned long bit)
{
	struct ebitmap_node *n;

	if (e->highbit < bit)
		return 0;

	n = e->node;
	while (n && (n->startbit <= bit)) {
		if ((n->startbit + MAPSIZE) > bit) {
			if (n->map & (MAPBIT << (bit - n->startbit)))
				return 1;
			else
				return 0;
		}
		n = n->next;
	}

	return 0;
}

int ebitmap_set_bit(struct ebitmap *e, unsigned long bit, int value)
{
	struct ebitmap_node *n, *prev, *new;

	prev = NULL;
	n = e->node;
	while (n && n->startbit <= bit) {
		if ((n->startbit + MAPSIZE) > bit) {
			if (value) {
				n->map |= (MAPBIT << (bit - n->startbit));
			} else {
				n->map &= ~(MAPBIT << (bit - n->startbit));
				if (!n->map) {
					/* drop this node from the bitmap */

					if (!n->next) {
						/*
						 * this was the highest map
						 * within the bitmap
						 */
						if (prev)
							e->highbit = prev->startbit + MAPSIZE;
						else
							e->highbit = 0;
					}
					if (prev)
						prev->next = n->next;
					else
						e->node = n->next;

					kfree(n);
				}
			}
			return 0;
		}
		prev = n;
		n = n->next;
	}

	if (!value)
		return 0;

	new = kzalloc(sizeof(*new), GFP_ATOMIC);
	if (!new)
		return -ENOMEM;

	new->startbit = bit & ~(MAPSIZE - 1);
	new->map = (MAPBIT << (bit - new->startbit));

	if (!n)
		/* this node will be the highest map within the bitmap */
		e->highbit = new->startbit + MAPSIZE;

	if (prev) {
		new->next = prev->next;
		prev->next = new;
	} else {
		new->next = e->node;
		e->node = new;
	}

	return 0;
}

void ebitmap_destroy(struct ebitmap *e)
{
	struct ebitmap_node *n, *temp;

	if (!e)
		return;

	n = e->node;
	while (n) {
		temp = n;
		n = n->next;
		kfree(temp);
	}

	e->highbit = 0;
	e->node = NULL;
	return;
}

int ebitmap_read(struct ebitmap *e, void *fp)
{
	int rc;
	struct ebitmap_node *n, *l;
	__le32 buf[3];
	u32 mapsize, count, i;
	__le64 map;

	ebitmap_init(e);

	rc = next_entry(buf, fp, sizeof buf);
	if (rc < 0)
		goto out;

	mapsize = le32_to_cpu(buf[0]);
	e->highbit = le32_to_cpu(buf[1]);
	count = le32_to_cpu(buf[2]);

	if (mapsize != MAPSIZE) {
		printk(KERN_ERR "security: ebitmap: map size %u does not "
		       "match my size %Zd (high bit was %d)\n", mapsize,
		       MAPSIZE, e->highbit);
		goto bad;
	}
	if (!e->highbit) {
		e->node = NULL;
		goto ok;
	}
	if (e->highbit & (MAPSIZE - 1)) {
		printk(KERN_ERR "security: ebitmap: high bit (%d) is not a "
		       "multiple of the map size (%Zd)\n", e->highbit, MAPSIZE);
		goto bad;
	}
	l = NULL;
	for (i = 0; i < count; i++) {
		rc = next_entry(buf, fp, sizeof(u32));
		if (rc < 0) {
			printk(KERN_ERR "security: ebitmap: truncated map\n");
			goto bad;
		}
		n = kzalloc(sizeof(*n), GFP_KERNEL);
		if (!n) {
			printk(KERN_ERR "security: ebitmap: out of memory\n");
			rc = -ENOMEM;
			goto bad;
		}

		n->startbit = le32_to_cpu(buf[0]);

		if (n->startbit & (MAPSIZE - 1)) {
			printk(KERN_ERR "security: ebitmap start bit (%d) is "
			       "not a multiple of the map size (%Zd)\n",
			       n->startbit, MAPSIZE);
			goto bad_free;
		}
		if (n->startbit > (e->highbit - MAPSIZE)) {
			printk(KERN_ERR "security: ebitmap start bit (%d) is "
			       "beyond the end of the bitmap (%Zd)\n",
			       n->startbit, (e->highbit - MAPSIZE));
			goto bad_free;
		}
		rc = next_entry(&map, fp, sizeof(u64));
		if (rc < 0) {
			printk(KERN_ERR "security: ebitmap: truncated map\n");
			goto bad_free;
		}
		n->map = le64_to_cpu(map);

		if (!n->map) {
			printk(KERN_ERR "security: ebitmap: null map in "
			       "ebitmap (startbit %d)\n", n->startbit);
			goto bad_free;
		}
		if (l) {
			if (n->startbit <= l->startbit) {
				printk(KERN_ERR "security: ebitmap: start "
				       "bit %d comes after start bit %d\n",
				       n->startbit, l->startbit);
				goto bad_free;
			}
			l->next = n;
		} else
			e->node = n;

		l = n;
	}

ok:
	rc = 0;
out:
	return rc;
bad_free:
	kfree(n);
bad:
	if (!rc)
		rc = -EINVAL;
	ebitmap_destroy(e);
	goto out;
}
