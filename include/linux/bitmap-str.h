/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BITMAP_STR_H
#define __LINUX_BITMAP_STR_H

#include <linux/types.h>

int bitmap_parse_user(const char __user *ubuf, unsigned int ulen, unsigned long *dst, int nbits);
int bitmap_print_to_pagebuf(bool list, char *buf, const unsigned long *maskp, int nmaskbits);
int bitmap_print_bitmask_to_buf(char *buf, const unsigned long *maskp, int nmaskbits,
				loff_t off, size_t count);
int bitmap_print_list_to_buf(char *buf, const unsigned long *maskp, int nmaskbits,
			     loff_t off, size_t count);
int bitmap_parse(const char *buf, unsigned int buflen, unsigned long *dst, int nbits);
int bitmap_parselist(const char *buf, unsigned long *maskp, int nmaskbits);
int bitmap_parselist_user(const char __user *ubuf, unsigned int ulen,
					unsigned long *dst, int nbits);

#endif /* __LINUX_BITMAP_STR_H */
