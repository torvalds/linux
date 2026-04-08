/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RING_BUFFER_TYPES_H
#define _LINUX_RING_BUFFER_TYPES_H

#include <asm/local.h>

#define TS_SHIFT        27
#define TS_MASK         ((1ULL << TS_SHIFT) - 1)
#define TS_DELTA_TEST   (~TS_MASK)

/*
 * We need to fit the time_stamp delta into 27 bits.
 */
static inline bool test_time_stamp(u64 delta)
{
	return !!(delta & TS_DELTA_TEST);
}

#define BUF_PAGE_HDR_SIZE offsetof(struct buffer_data_page, data)

#define RB_EVNT_HDR_SIZE (offsetof(struct ring_buffer_event, array))
#define RB_ALIGNMENT		4U
#define RB_MAX_SMALL_DATA	(RB_ALIGNMENT * RINGBUF_TYPE_DATA_TYPE_LEN_MAX)
#define RB_EVNT_MIN_SIZE	8U	/* two 32bit words */

#ifndef CONFIG_HAVE_64BIT_ALIGNED_ACCESS
# define RB_FORCE_8BYTE_ALIGNMENT	0
# define RB_ARCH_ALIGNMENT		RB_ALIGNMENT
#else
# define RB_FORCE_8BYTE_ALIGNMENT	1
# define RB_ARCH_ALIGNMENT		8U
#endif

#define RB_ALIGN_DATA		__aligned(RB_ARCH_ALIGNMENT)

struct buffer_data_page {
	u64		 time_stamp;	/* page time stamp */
	local_t		 commit;	/* write committed index */
	unsigned char	 data[] RB_ALIGN_DATA;	/* data of buffer page */
};
#endif
