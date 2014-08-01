/*
 * Implementation of the extensible bitmap type.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
/*
 * Updated: Hewlett-Packard <paul@paul-moore.com>
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

#define BITS_PER_U64	(sizeof(u64) * 8)

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
	unsigned long e_map;
	u32 offset;
	unsigned int iter;
	int rc;

	if (e_iter == NULL) {
		*catmap = NULL;
		return 0;
	}

	if (*catmap != NULL)
		netlbl_secattr_catmap_free(*catmap);
	*catmap = NULL;

	while (e_iter) {
		offset = e_iter->startbit;
		for (iter = 0; iter < EBITMAP_UNIT_NUMS; iter++) {
			e_map = e_iter->maps[iter];
			if (e_map != 0) {
				rc = netlbl_secattr_catmap_setlong(catmap,
								   offset,
								   e_map,
								   GFP_ATOMIC);
				if (rc != 0)
					goto netlbl_export_failure;
			}
			offset += EBITMAP_UNIT_SIZE;
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
	int rc;
	struct ebitmap_node *e_iter = NULL;
	struct ebitmap_node *e_prev = NULL;
	u32 offset = 0, idx;
	unsigned long bitmap;

	for (;;) {
		rc = netlbl_secattr_catmap_getlong(catmap, &offset, &bitmap);
		if (rc < 0)
			goto netlbl_import_failure;
		if (offset == (u32)-1)
			return 0;

		if (e_iter == NULL ||
		    offset >= e_iter->startbit + EBITMAP_SIZE) {
			e_prev = e_iter;
			e_iter = kzalloc(sizeof(*e_iter), GFP_ATOMIC);
			if (e_iter == NULL)
				goto netlbl_import_failure;
			e_iter->startbit = offset & ~(EBITMAP_SIZE - 1);
			if (e_prev == NULL)
				ebmap->node = e_iter;
			else
				e_prev->next = e_iter;
			ebmap->highbit = e_iter->startbit + EBITMAP_SIZE;
		}

		/* offset will always be aligned to an unsigned long */
		idx = EBITMAP_NODE_INDEX(e_iter, offset);
		e_iter->maps[idx] = bitmap;

		/* next */
		offset += EBITMAP_UNIT_SIZE;
	}

	/* NOTE: we should never reach this return */
	return 0;

netlbl_import_failure:
	ebitmap_destroy(ebmap);
	return -ENOMEM;
}
#endif /* CONFIG_NETLABEL */

/*
 * Check to see if all the bits set in e2 are also set in e1. Optionally,
 * if last_e2bit is non-zero, the highest set bit in e2 cannot exceed
 * last_e2bit.
 */
int ebitmap_contains(struct ebitmap *e1, struct ebitmap *e2, u32 last_e2bit)
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
		for (i = EBITMAP_UNIT_NUMS - 1; (i >= 0) && !n2->maps[i]; )
			i--;	/* Skip trailing NULL map entries */
		if (last_e2bit && (i >= 0)) {
			u32 lastsetbit = n2->startbit + i * EBITMAP_UNIT_SIZE +
					 __fls(n2->maps[i]);
			if (lastsetbit > last_e2bit)
				return 0;
		}

		while (i >= 0) {
			if ((n1->maps[i] & n2->maps[i]) != n2->maps[i])
				return 0;
			i--;
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

	if (mapunit != BITS_PER_U64) {
		printk(KERN_ERR "SELinux: ebitmap: map size %u does not "
		       "match my size %Zd (high bit was %d)\n",
		       mapunit, BITS_PER_U64, e->highbit);
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

int ebitmap_write(struct ebitmap *e, void *fp)
{
	struct ebitmap_node *n;
	u32 count;
	__le32 buf[3];
	u64 map;
	int bit, last_bit, last_startbit, rc;

	buf[0] = cpu_to_le32(BITS_PER_U64);

	count = 0;
	last_bit = 0;
	last_startbit = -1;
	ebitmap_for_each_positive_bit(e, n, bit) {
		if (rounddown(bit, (int)BITS_PER_U64) > last_startbit) {
			count++;
			last_startbit = rounddown(bit, BITS_PER_U64);
		}
		last_bit = roundup(bit + 1, BITS_PER_U64);
	}
	buf[1] = cpu_to_le32(last_bit);
	buf[2] = cpu_to_le32(count);

	rc = put_entry(buf, sizeof(u32), 3, fp);
	if (rc)
		return rc;

	map = 0;
	last_startbit = INT_MIN;
	ebitmap_for_each_positive_bit(e, n, bit) {
		if (rounddown(bit, (int)BITS_PER_U64) > last_startbit) {
			__le64 buf64[1];

			/* this is the very first bit */
			if (!map) {
				last_startbit = rounddown(bit, BITS_PER_U64);
				map = (u64)1 << (bit - last_startbit);
				continue;
			}

			/* write the last node */
			buf[0] = cpu_to_le32(last_startbit);
			rc = put_entry(buf, sizeof(u32), 1, fp);
			if (rc)
				return rc;

			buf64[0] = cpu_to_le64(map);
			rc = put_entry(buf64, sizeof(u64), 1, fp);
			if (rc)
				return rc;

			/* set up for the next node */
			map = 0;
			last_startbit = rounddown(bit, BITS_PER_U64);
		}
		map |= (u64)1 << (bit - last_startbit);
	}
	/* write the last node */
	if (map) {
		__le64 buf64[1];

		/* write the last node */
		buf[0] = cpu_to_le32(last_startbit);
		rc = put_entry(buf, sizeof(u32), 1, fp);
		if (rc)
			return rc;

		buf64[0] = cpu_to_le64(map);
		rc = put_entry(buf64, sizeof(u64), 1, fp);
		if (rc)
			return rc;
	}
	return 0;
}
