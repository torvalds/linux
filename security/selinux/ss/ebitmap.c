/*
 * Implementation of the extensible bitmap type.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
/*
 * Updated: Hewlett-Packard <paul.moore@hp.com>
 *
 *      Added support to import/export the NetLabel category bitmap
 *
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <net/netlabel.h>
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

#ifdef CONFIG_NETLABEL
/**
 * ebitmap_netlbl_export - Export an ebitmap into a NetLabel category bitmap
 * @ebmap: the ebitmap to export
 * @catmap: the NetLabel category bitmap
 *
 * Description:
 * Export a SELinux extensibile bitmap into a NetLabel category bitmap.
 * Returns zero on success, negative values on error.
 *
 */
int ebitmap_netlbl_export(struct ebitmap *ebmap,
			  struct netlbl_lsm_secattr_catmap **catmap)
{
	struct ebitmap_node *e_iter = ebmap->node;
	struct netlbl_lsm_secattr_catmap *c_iter;
	u32 cmap_idx;

	/* This function is a much simpler because SELinux's MAPTYPE happens
	 * to be the same as NetLabel's NETLBL_CATMAP_MAPTYPE, if MAPTYPE is
	 * changed from a u64 this function will most likely need to be changed
	 * as well.  It's not ideal but I think the tradeoff in terms of
	 * neatness and speed is worth it. */

	if (e_iter == NULL) {
		*catmap = NULL;
		return 0;
	}

	c_iter = netlbl_secattr_catmap_alloc(GFP_ATOMIC);
	if (c_iter == NULL)
		return -ENOMEM;
	*catmap = c_iter;
	c_iter->startbit = e_iter->startbit & ~(NETLBL_CATMAP_SIZE - 1);

	while (e_iter != NULL) {
		if (e_iter->startbit >=
		    (c_iter->startbit + NETLBL_CATMAP_SIZE)) {
			c_iter->next = netlbl_secattr_catmap_alloc(GFP_ATOMIC);
			if (c_iter->next == NULL)
				goto netlbl_export_failure;
			c_iter = c_iter->next;
			c_iter->startbit = e_iter->startbit &
				           ~(NETLBL_CATMAP_SIZE - 1);
		}
		cmap_idx = (e_iter->startbit - c_iter->startbit) /
			   NETLBL_CATMAP_MAPSIZE;
		c_iter->bitmap[cmap_idx] = e_iter->map;
		e_iter = e_iter->next;
	}

	return 0;

netlbl_export_failure:
	netlbl_secattr_catmap_free(*catmap);
	return -ENOMEM;
}

/**
 * ebitmap_netlbl_import - Import a NetLabel category bitmap into an ebitmap
 * @ebmap: the ebitmap to export
 * @catmap: the NetLabel category bitmap
 *
 * Description:
 * Import a NetLabel category bitmap into a SELinux extensibile bitmap.
 * Returns zero on success, negative values on error.
 *
 */
int ebitmap_netlbl_import(struct ebitmap *ebmap,
			  struct netlbl_lsm_secattr_catmap *catmap)
{
	struct ebitmap_node *e_iter = NULL;
	struct ebitmap_node *emap_prev = NULL;
	struct netlbl_lsm_secattr_catmap *c_iter = catmap;
	u32 c_idx;

	/* This function is a much simpler because SELinux's MAPTYPE happens
	 * to be the same as NetLabel's NETLBL_CATMAP_MAPTYPE, if MAPTYPE is
	 * changed from a u64 this function will most likely need to be changed
	 * as well.  It's not ideal but I think the tradeoff in terms of
	 * neatness and speed is worth it. */

	do {
		for (c_idx = 0; c_idx < NETLBL_CATMAP_MAPCNT; c_idx++) {
			if (c_iter->bitmap[c_idx] == 0)
				continue;

			e_iter = kzalloc(sizeof(*e_iter), GFP_ATOMIC);
			if (e_iter == NULL)
				goto netlbl_import_failure;
			if (emap_prev == NULL)
				ebmap->node = e_iter;
			else
				emap_prev->next = e_iter;
			emap_prev = e_iter;

			e_iter->startbit = c_iter->startbit +
				           NETLBL_CATMAP_MAPSIZE * c_idx;
			e_iter->map = c_iter->bitmap[c_idx];
		}
		c_iter = c_iter->next;
	} while (c_iter != NULL);
	if (e_iter != NULL)
		ebmap->highbit = e_iter->startbit + MAPSIZE;
	else
		ebitmap_destroy(ebmap);

	return 0;

netlbl_import_failure:
	ebitmap_destroy(ebmap);
	return -ENOMEM;
}
#endif /* CONFIG_NETLABEL */

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
