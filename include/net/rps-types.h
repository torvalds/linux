/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _NET_RPS_TYPES_H
#define _NET_RPS_TYPES_H

/* Define a rps_tag_ptr:
 * Low order 5 bits are used to store the ilog2(size) of an RPS table.
 */
typedef unsigned long rps_tag_ptr;

static inline u8 rps_tag_to_log(rps_tag_ptr tag_ptr)
{
	return tag_ptr & 31U;
}

static inline u32 rps_tag_to_mask(rps_tag_ptr tag_ptr)
{
	return (1U << rps_tag_to_log(tag_ptr)) - 1;
}

static inline void *rps_tag_to_table(rps_tag_ptr tag_ptr)
{
	return (void *)(tag_ptr & ~31UL);
}
#endif /* _NET_RPS_TYPES_H */
