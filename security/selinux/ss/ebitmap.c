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
/*
 * Updated: KaiGai Kohei <kaigai@ak.jp.nec.com>
 *      Applied standard bit operations to improve bitmap scanning.
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
	       !memcmp(n1->maps, n2->maps, EBITMAP_SIZE / 8)) {
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
		memcpy(new->maps, n->maps, EBITMAP_SIZE / 8);
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
	u32 cmap_idx, cmap_sft;
	int i;

	/* NetLabel's NETLBL_CATMAP_MAPTYPE is defined as an array of u64,
	 * however, it is not always compatible with an array of unsigned long
	 * in ebitmap_node.
	 * In addition, you should pay attention the following implementation
	 * assumes unsigned long has a width equal with or less than 64-bit.
	 */

	if (e_iter == NULL) {
		*catmap = NULL;
		return 0;
	}

	c_iter = netlbl_secattr_catmap_alloc(GFP_ATOMIC);
	if (c_iter == NULL)
		return -ENOMEM;
	*catmap = c_iter;
	c_iter->startbit = e_iter->startbit & ~(NETLBL_CATMAP_SIZE - 1);

	while (e_iter) {
		for (i = 0; i < EBITMAP_UNIT_NUMS; i++) {
			unsigned int delta, e_startbit, c_endbit;

			e_startbit = e_iter->startbit + i * EBITMAP_UNIT_SIZE;
			c_endbit = c_iter->startbit + NETLBL_CATMAP_SIZE;
			if (e_startbit >= c_endbit) {
				c_iter->next
				  = netlbl_secattr_catmap_alloc(GFP_ATOMIC);
				if (c_iter->next == NULL)
					goto netlbl_export_failure;
				c_iter = c_iter->next;
				c_iter->startbit
				  = e_startbit & ~(NETLBL_CATMAP_SIZE - 1);
			}
			delta = e_startbit - c_iter->startbit;
			cmap_idx = delta / NETLBL_CATMAP_MAPSIZE;
			cmap_sft = delta % NETLBL_CATMAP_MAPSIZE;
			c_iter->bitmap[cmap_idx]
				|= e_iter->maps[i] << cmap_sft;
		}
		e_iter = e_iter->next;
	}

	return 0;

netlbl_export_failure:
	netlbl_secattr_catmap_free(*catmap);
	return -ENOMEM;
}

/**
 * ebitmap_netlbl_import - Import a NetLabel category bitmap into an ebitmap
 * @ebmap: the ebitmap to import
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
	u32 c_idx, c_pos, e_idx, e_sft;

	/* NetLabel's NETLBL_CATMAP_MAPTYPE is defined as an array of u64,
	 * however, it is not always compatible with an array of unsigned long
	 * in ebitmap_node.
	 * In addition, you should pay attention the following implementation
	 * assumes unsigned long has a width equal with or less than 64-bit.
	 */

	do {
		for (c_idx = 0; c_idx < NETLBL_CATMAP_MAPCNT; c_idx++) {
			unsigned int delta;
			u64 map = c_iter->bitmap[c_idx];

			if (!map)
				continue;

			c_pos = c_iter->startbit
				+ c_idx * NETLBL_CATMAP_MAPSIZE;
			if (!e_iter
			    || c_pos >= e_iter->startbit + EBITMAP_SIZE) {
				e_iter = kzalloc(sizeof(*e_iter), GFP_ATOMIC);
				if (!e_iter)
					goto netlbl_import_failure;
				e_iter->startbit
					= c_pos - (c_pos % EBITMAP_SIZE);
				if (emap_prev == NULL)
					ebmap->node = e_iter;
				else
					emap_prev->next = e_iter;
				emap_prev = e_iter;
			}
			delta = c_pos - e_iter->startbit;
			e_idx = delta / EBITMAP_UNIT_SIZE;
			e_sft = delta % EBITMAP_UNIT_SIZE;
			while (map) {
				e_iter->maps[e_idx++] |= map & (-1UL);
				map = EBITMAP_SHIFT_UNIT_SIZE(map);
			}
		}
		c_iter = c_iter->next;
	} while (c_iter);
	if (e_iter != NULL)
		ebmap->highbit = e_iter->startbit + EBITMAP_SIZE;
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
	int i;

	if (e1->highbit < e2->highbit)
		return 0;

	n1 = e1->node;
	n2 = e2->node;
	while (n1 && n2 && (n1->startbit <= n2->startbit)) {
		if (n1->startbit < n2->startbit) {
			n1 = n1->next;
			continue;
		}
		for (i = 0; i < EBITMAP_UNIT_NUMS; i++) {
			if ((n1->maps[i] & n2->maps[i]) != n2->maps[i])
				return 0;
		}

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
		if ((n->startbit + EBITMAP_SIZE) > bit)
			return ebitmap_node_get_bit(n, bit);
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
		if ((n->startbit + EBITMAP_SIZE) > bit) {
			if (value) {
				ebitmap_node_set_bit(n, bit);
			} else {
				unsigned int s;

				ebitmap_node_clr_bit(n, bit);

				s = find_first_bit(n->maps, EBITMAP_SIZE);
				if (s < EBITMAP_SIZE)
					return 0;

				/* drop this node from the bitmap */
				if (!n->next) {
					/*
					 * this was the highest map
					 * within the bitmap
					 */
					if (prev)
						e->highbit = prev->startbit
							     + EBITMAP_SIZE;
					else
						e->highbit = 0;
				}
				if (prev)
					prev->next = n->next;
				else
					e->node = n->next;
				kfree(n);
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

	new->startbit = bit - (bit % EBITMAP_SIZE);
	ebitmap_node_set_bit(new, bit);

	if (!n)
		/* this node will be the highest map within the bitmap */
		e->highbit = new->startbit + EBITMAP_SIZE;

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
	struct ebitmap_node *n = NULL;
	u32 mapunit, count, startbit, index;
	u64 map;
	__le32 buf[3];
	int rc, i;

	ebitmap_init(e);

	rc = next_entry(buf, fp, sizeof buf);
	if (rc < 0)
		goto out;

	mapunit = le32_to_cpu(buf[0]);
	e->highbit = le32_to_cpu(buf[1]);
	count = le32_to_cpu(buf[2]);

	if (mapunit != sizeof(u64) * 8) {
		printk(KERN_ERR "SELinux: ebitmap: map size %u does not "
		       "match my size %Zd (high bit was %d)\n",
		       mapunit, sizeof(u64) * 8, e->highbit);
		goto bad;
	}

	/* round up e->highbit */
	e->highbit += EBITMAP_SIZE - 1;
	e->highbit -= (e->highbit % EBITMAP_SIZE);

	if (!e->highbit) {
		e->node = NULL;
		goto ok;
	}

	for (i = 0; i < count; i++) {
		rc = next_entry(&startbit, fp, sizeof(u32));
		if (rc < 0) {
			printk(KERN_ERR "SELinux: ebitmap: truncated map\n");
			goto bad;
		}
		startbit = le32_to_cpu(startbit);

		if (startbit & (mapunit - 1)) {
			printk(KERN_ERR "SELinux: ebitmap start bit (%d) is "
			       "not a multiple of the map unit size (%u)\n",
			       startbit, mapunit);
			goto bad;
		}
		if (startbit > e->highbit - mapunit) {
			printk(KERN_ERR "SELinux: ebitmap start bit (%d) is "
			       "beyond the end of the bitmap (%u)\n",
			       startbit, (e->highbit - mapunit));
			goto bad;
		}

		if (!n || startbit >= n->startbit + EBITMAP_SIZE) {
			struct ebitmap_node *tmp;
			tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
			if (!tmp) {
				printk(KERN_ERR
				       "SELinux: ebitmap: out of memory\n");
				rc = -ENOMEM;
				goto bad;
			}
			/* round down */
			tmp->startbit = startbit - (startbit % EBITMAP_SIZE);
			if (n)
				n->next = tmp;
			else
				e->node = tmp;
			n = tmp;
		} else if (startbit <= n->startbit) {
			printk(KERN_ERR "SELinux: ebitmap: start bit %d"
			       " comes after start bit %d\n",
			       startbit, n->startbit);
			goto bad;
		}

		rc = next_entry(&map, fp, sizeof(u64));
		if (rc < 0) {
			printk(KERN_ERR "SELinux: ebitmap: truncated map\n");
			goto bad;
		}
		map = le64_to_cpu(map);

		index = (startbit - n->startbit) / EBITMAP_UNIT_SIZE;
		while (map) {
			n->maps[index++] = map & (-1UL);
			map = EBITMAP_SHIFT_UNIT_SIZE(map);
		}
	}
ok:
	rc = 0;
out:
	return rc;
bad:
	if (!rc)
		rc = -EINVAL;
	ebitmap_destroy(e);
	goto out;
}
