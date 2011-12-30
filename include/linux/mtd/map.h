/*
 * Copyright © 2000-2010 David Woodhouse <dwmw2@infradead.org> et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/* Overhauled routines for dealing with different mmap regions of flash */

#ifndef __LINUX_MTD_MAP_H__
#define __LINUX_MTD_MAP_H__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <linux/kernel.h>

#include <asm/unaligned.h>
#include <asm/system.h>
#include <asm/io.h>

#ifdef CONFIG_MTD_MAP_BANK_WIDTH_1
#define map_bankwidth(map) 1
#define map_bankwidth_is_1(map) (map_bankwidth(map) == 1)
#define map_bankwidth_is_large(map) (0)
#define map_words(map) (1)
#define MAX_MAP_BANKWIDTH 1
#else
#define map_bankwidth_is_1(map) (0)
#endif

#ifdef CONFIG_MTD_MAP_BANK_WIDTH_2
# ifdef map_bankwidth
#  undef map_bankwidth
#  define map_bankwidth(map) ((map)->bankwidth)
# else
#  define map_bankwidth(map) 2
#  define map_bankwidth_is_large(map) (0)
#  define map_words(map) (1)
# endif
#define map_bankwidth_is_2(map) (map_bankwidth(map) == 2)
#undef MAX_MAP_BANKWIDTH
#define MAX_MAP_BANKWIDTH 2
#else
#define map_bankwidth_is_2(map) (0)
#endif

#ifdef CONFIG_MTD_MAP_BANK_WIDTH_4
# ifdef map_bankwidth
#  undef map_bankwidth
#  define map_bankwidth(map) ((map)->bankwidth)
# else
#  define map_bankwidth(map) 4
#  define map_bankwidth_is_large(map) (0)
#  define map_words(map) (1)
# endif
#define map_bankwidth_is_4(map) (map_bankwidth(map) == 4)
#undef MAX_MAP_BANKWIDTH
#define MAX_MAP_BANKWIDTH 4
#else
#define map_bankwidth_is_4(map) (0)
#endif

/* ensure we never evaluate anything shorted than an unsigned long
 * to zero, and ensure we'll never miss the end of an comparison (bjd) */

#define map_calc_words(map) ((map_bankwidth(map) + (sizeof(unsigned long)-1))/ sizeof(unsigned long))

#ifdef CONFIG_MTD_MAP_BANK_WIDTH_8
# ifdef map_bankwidth
#  undef map_bankwidth
#  define map_bankwidth(map) ((map)->bankwidth)
#  if BITS_PER_LONG < 64
#   undef map_bankwidth_is_large
#   define map_bankwidth_is_large(map) (map_bankwidth(map) > BITS_PER_LONG/8)
#   undef map_words
#   define map_words(map) map_calc_words(map)
#  endif
# else
#  define map_bankwidth(map) 8
#  define map_bankwidth_is_large(map) (BITS_PER_LONG < 64)
#  define map_words(map) map_calc_words(map)
# endif
#define map_bankwidth_is_8(map) (map_bankwidth(map) == 8)
#undef MAX_MAP_BANKWIDTH
#define MAX_MAP_BANKWIDTH 8
#else
#define map_bankwidth_is_8(map) (0)
#endif

#ifdef CONFIG_MTD_MAP_BANK_WIDTH_16
# ifdef map_bankwidth
#  undef map_bankwidth
#  define map_bankwidth(map) ((map)->bankwidth)
#  undef map_bankwidth_is_large
#  define map_bankwidth_is_large(map) (map_bankwidth(map) > BITS_PER_LONG/8)
#  undef map_words
#  define map_words(map) map_calc_words(map)
# else
#  define map_bankwidth(map) 16
#  define map_bankwidth_is_large(map) (1)
#  define map_words(map) map_calc_words(map)
# endif
#define map_bankwidth_is_16(map) (map_bankwidth(map) == 16)
#undef MAX_MAP_BANKWIDTH
#define MAX_MAP_BANKWIDTH 16
#else
#define map_bankwidth_is_16(map) (0)
#endif

#ifdef CONFIG_MTD_MAP_BANK_WIDTH_32
# ifdef map_bankwidth
#  undef map_bankwidth
#  define map_bankwidth(map) ((map)->bankwidth)
#  undef map_bankwidth_is_large
#  define map_bankwidth_is_large(map) (map_bankwidth(map) > BITS_PER_LONG/8)
#  undef map_words
#  define map_words(map) map_calc_words(map)
# else
#  define map_bankwidth(map) 32
#  define map_bankwidth_is_large(map) (1)
#  define map_words(map) map_calc_words(map)
# endif
#define map_bankwidth_is_32(map) (map_bankwidth(map) == 32)
#undef MAX_MAP_BANKWIDTH
#define MAX_MAP_BANKWIDTH 32
#else
#define map_bankwidth_is_32(map) (0)
#endif

#ifndef map_bankwidth
#warning "No CONFIG_MTD_MAP_BANK_WIDTH_xx selected. No NOR chip support can work"
static inline int map_bankwidth(void *map)
{
	BUG();
	return 0;
}
#define map_bankwidth_is_large(map) (0)
#define map_words(map) (0)
#define MAX_MAP_BANKWIDTH 1
#endif

static inline int map_bankwidth_supported(int w)
{
	switch (w) {
#ifdef CONFIG_MTD_MAP_BANK_WIDTH_1
	case 1:
#endif
#ifdef CONFIG_MTD_MAP_BANK_WIDTH_2
	case 2:
#endif
#ifdef CONFIG_MTD_MAP_BANK_WIDTH_4
	case 4:
#endif
#ifdef CONFIG_MTD_MAP_BANK_WIDTH_8
	case 8:
#endif
#ifdef CONFIG_MTD_MAP_BANK_WIDTH_16
	case 16:
#endif
#ifdef CONFIG_MTD_MAP_BANK_WIDTH_32
	case 32:
#endif
		return 1;

	default:
		return 0;
	}
}

#define MAX_MAP_LONGS ( ((MAX_MAP_BANKWIDTH*8) + BITS_PER_LONG - 1) / BITS_PER_LONG )

typedef union {
	unsigned long x[MAX_MAP_LONGS];
} map_word;

/* The map stuff is very simple. You fill in your struct map_info with
   a handful of routines for accessing the device, making sure they handle
   paging etc. correctly if your device needs it. Then you pass it off
   to a chip probe routine -- either JEDEC or CFI probe or both -- via
   do_map_probe(). If a chip is recognised, the probe code will invoke the
   appropriate chip driver (if present) and return a struct mtd_info.
   At which point, you fill in the mtd->module with your own module
   address, and register it with the MTD core code. Or you could partition
   it and register the partitions instead, or keep it for your own private
   use; whatever.

   The mtd->priv field will point to the struct map_info, and any further
   private data required by the chip driver is linked from the
   mtd->priv->fldrv_priv field. This allows the map driver to get at
   the destructor function map->fldrv_destroy() when it's tired
   of living.
*/

struct map_info {
	const char *name;
	unsigned long size;
	resource_size_t phys;
#define NO_XIP (-1UL)

	void __iomem *virt;
	void *cached;

	int swap; /* this mapping's byte-swapping requirement */
	int bankwidth; /* in octets. This isn't necessarily the width
		       of actual bus cycles -- it's the repeat interval
		      in bytes, before you are talking to the first chip again.
		      */

#ifdef CONFIG_MTD_COMPLEX_MAPPINGS
	map_word (*read)(struct map_info *, unsigned long);
	void (*copy_from)(struct map_info *, void *, unsigned long, ssize_t);

	void (*write)(struct map_info *, const map_word, unsigned long);
	void (*copy_to)(struct map_info *, unsigned long, const void *, ssize_t);

	/* We can perhaps put in 'point' and 'unpoint' methods, if we really
	   want to enable XIP for non-linear mappings. Not yet though. */
#endif
	/* It's possible for the map driver to use cached memory in its
	   copy_from implementation (and _only_ with copy_from).  However,
	   when the chip driver knows some flash area has changed contents,
	   it will signal it to the map driver through this routine to let
	   the map driver invalidate the corresponding cache as needed.
	   If there is no cache to care about this can be set to NULL. */
	void (*inval_cache)(struct map_info *, unsigned long, ssize_t);

	/* set_vpp() must handle being reentered -- enable, enable, disable
	   must leave it enabled. */
	void (*set_vpp)(struct map_info *, int);

	unsigned long pfow_base;
	unsigned long map_priv_1;
	unsigned long map_priv_2;
	void *fldrv_priv;
	struct mtd_chip_driver *fldrv;
};

struct mtd_chip_driver {
	struct mtd_info *(*probe)(struct map_info *map);
	void (*destroy)(struct mtd_info *);
	struct module *module;
	char *name;
	struct list_head list;
};

void register_mtd_chip_driver(struct mtd_chip_driver *);
void unregister_mtd_chip_driver(struct mtd_chip_driver *);

struct mtd_info *do_map_probe(const char *name, struct map_info *map);
void map_destroy(struct mtd_info *mtd);

#define ENABLE_VPP(map) do { if(map->set_vpp) map->set_vpp(map, 1); } while(0)
#define DISABLE_VPP(map) do { if(map->set_vpp) map->set_vpp(map, 0); } while(0)

#define INVALIDATE_CACHED_RANGE(map, from, size) \
	do { if(map->inval_cache) map->inval_cache(map, from, size); } while(0)


static inline int map_word_equal(struct map_info *map, map_word val1, map_word val2)
{
	int i;
	for (i=0; i<map_words(map); i++) {
		if (val1.x[i] != val2.x[i])
			return 0;
	}
	return 1;
}

static inline map_word map_word_and(struct map_info *map, map_word val1, map_word val2)
{
	map_word r;
	int i;

	for (i=0; i<map_words(map); i++) {
		r.x[i] = val1.x[i] & val2.x[i];
	}
	return r;
}

static inline map_word map_word_clr(struct map_info *map, map_word val1, map_word val2)
{
	map_word r;
	int i;

	for (i=0; i<map_words(map); i++) {
		r.x[i] = val1.x[i] & ~val2.x[i];
	}
	return r;
}

static inline map_word map_word_or(struct map_info *map, map_word val1, map_word val2)
{
	map_word r;
	int i;

	for (i=0; i<map_words(map); i++) {
		r.x[i] = val1.x[i] | val2.x[i];
	}
	return r;
}

#define map_word_andequal(m, a, b, z) map_word_equal(m, z, map_word_and(m, a, b))

static inline int map_word_bitsset(struct map_info *map, map_word val1, map_word val2)
{
	int i;

	for (i=0; i<map_words(map); i++) {
		if (val1.x[i] & val2.x[i])
			return 1;
	}
	return 0;
}

static inline map_word map_word_load(struct map_info *map, const void *ptr)
{
	map_word r;

	if (map_bankwidth_is_1(map))
		r.x[0] = *(unsigned char *)ptr;
	else if (map_bankwidth_is_2(map))
		r.x[0] = get_unaligned((uint16_t *)ptr);
	else if (map_bankwidth_is_4(map))
		r.x[0] = get_unaligned((uint32_t *)ptr);
#if BITS_PER_LONG >= 64
	else if (map_bankwidth_is_8(map))
		r.x[0] = get_unaligned((uint64_t *)ptr);
#endif
	else if (map_bankwidth_is_large(map))
		memcpy(r.x, ptr, map->bankwidth);

	return r;
}

static inline map_word map_word_load_partial(struct map_info *map, map_word orig, const unsigned char *buf, int start, int len)
{
	int i;

	if (map_bankwidth_is_large(map)) {
		char *dest = (char *)&orig;
		memcpy(dest+start, buf, len);
	} else {
		for (i=start; i < start+len; i++) {
			int bitpos;
#ifdef __LITTLE_ENDIAN
			bitpos = i*8;
#else /* __BIG_ENDIAN */
			bitpos = (map_bankwidth(map)-1-i)*8;
#endif
			orig.x[0] &= ~(0xff << bitpos);
			orig.x[0] |= buf[i-start] << bitpos;
		}
	}
	return orig;
}

#if BITS_PER_LONG < 64
#define MAP_FF_LIMIT 4
#else
#define MAP_FF_LIMIT 8
#endif

static inline map_word map_word_ff(struct map_info *map)
{
	map_word r;
	int i;

	if (map_bankwidth(map) < MAP_FF_LIMIT) {
		int bw = 8 * map_bankwidth(map);
		r.x[0] = (1 << bw) - 1;
	} else {
		for (i=0; i<map_words(map); i++)
			r.x[i] = ~0UL;
	}
	return r;
}

static inline map_word inline_map_read(struct map_info *map, unsigned long ofs)
{
	map_word r;

	if (map_bankwidth_is_1(map))
		r.x[0] = __raw_readb(map->virt + ofs);
	else if (map_bankwidth_is_2(map))
		r.x[0] = __raw_readw(map->virt + ofs);
	else if (map_bankwidth_is_4(map))
		r.x[0] = __raw_readl(map->virt + ofs);
#if BITS_PER_LONG >= 64
	else if (map_bankwidth_is_8(map))
		r.x[0] = __raw_readq(map->virt + ofs);
#endif
	else if (map_bankwidth_is_large(map))
		memcpy_fromio(r.x, map->virt+ofs, map->bankwidth);
	else
		BUG();

	return r;
}

static inline void inline_map_write(struct map_info *map, const map_word datum, unsigned long ofs)
{
	if (map_bankwidth_is_1(map))
		__raw_writeb(datum.x[0], map->virt + ofs);
	else if (map_bankwidth_is_2(map))
		__raw_writew(datum.x[0], map->virt + ofs);
	else if (map_bankwidth_is_4(map))
		__raw_writel(datum.x[0], map->virt + ofs);
#if BITS_PER_LONG >= 64
	else if (map_bankwidth_is_8(map))
		__raw_writeq(datum.x[0], map->virt + ofs);
#endif
	else if (map_bankwidth_is_large(map))
		memcpy_toio(map->virt+ofs, datum.x, map->bankwidth);
	mb();
}

static inline void inline_map_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	if (map->cached)
		memcpy(to, (char *)map->cached + from, len);
	else
		memcpy_fromio(to, map->virt + from, len);
}

static inline void inline_map_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	memcpy_toio(map->virt + to, from, len);
}

#ifdef CONFIG_MTD_COMPLEX_MAPPINGS
#define map_read(map, ofs) (map)->read(map, ofs)
#define map_copy_from(map, to, from, len) (map)->copy_from(map, to, from, len)
#define map_write(map, datum, ofs) (map)->write(map, datum, ofs)
#define map_copy_to(map, to, from, len) (map)->copy_to(map, to, from, len)

extern void simple_map_init(struct map_info *);
#define map_is_linear(map) (map->phys != NO_XIP)

#else
#define map_read(map, ofs) inline_map_read(map, ofs)
#define map_copy_from(map, to, from, len) inline_map_copy_from(map, to, from, len)
#define map_write(map, datum, ofs) inline_map_write(map, datum, ofs)
#define map_copy_to(map, to, from, len) inline_map_copy_to(map, to, from, len)


#define simple_map_init(map) BUG_ON(!map_bankwidth_supported((map)->bankwidth))
#define map_is_linear(map) ({ (void)(map); 1; })

#endif /* !CONFIG_MTD_COMPLEX_MAPPINGS */

#endif /* __LINUX_MTD_MAP_H__ */
