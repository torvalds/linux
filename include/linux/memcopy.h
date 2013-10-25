/*
 * memcopy.h -- definitions for memory copy functions.  Generic C version.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The code is derived from the GNU C Library.
 * Copyright (C) 1991, 1992, 1993, 1997, 2004 Free Software Foundation, Inc.
 */
#ifndef _LINUX_MEMCOPY_H_
#define _LINUX_MEMCOPY_H_

/*
 * The strategy of the memory functions is:
 *
 *   1. Copy bytes until the destination pointer is aligned.
 *
 *   2. Copy words in unrolled loops.  If the source and destination
 *      are not aligned in the same way, use word memory operations,
 *      but shift and merge two read words before writing.
 *
 *   3. Copy the few remaining bytes.
 *
 * This is fast on processors that have at least 10 registers for
 * allocation by GCC, and that can access memory at reg+const in one
 * instruction.
 */

#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>

/*
 * The macros defined in this file are:
 *
 * BYTE_COPY_FWD(dst_beg_ptr, src_beg_ptr, nbytes_to_copy)
 * 
 * BYTE_COPY_BWD(dst_end_ptr, src_end_ptr, nbytes_to_copy)
 * 
 * WORD_COPY_FWD(dst_beg_ptr, src_beg_ptr, nbytes_remaining, nbytes_to_copy)
 * 
 * WORD_COPY_BWD(dst_end_ptr, src_end_ptr, nbytes_remaining, nbytes_to_copy)
 * 
 * MERGE(old_word, sh_1, new_word, sh_2)
 * 
 * MEM_COPY_FWD(dst_beg_ptr, src_beg_ptr, nbytes_to_copy)
 * 
 * MEM_COPY_BWD(dst_end_ptr, src_end_ptr, nbytes_to_copy)
 */

#define OP_T_THRESHOLD	16

/*
 * Type to use for aligned memory operations.
 * This should normally be the biggest type supported by a single load
 * and store.
 */
#define	op_t	unsigned long int
#define OPSIZ	(sizeof(op_t))

/* Type to use for unaligned operations.  */
typedef unsigned char byte;

#ifndef MERGE
# ifdef __LITTLE_ENDIAN
#  define MERGE(w0, sh_1, w1, sh_2) (((w0) >> (sh_1)) | ((w1) << (sh_2)))
# elif defined(__BIG_ENDIAN)
#  define MERGE(w0, sh_1, w1, sh_2) (((w0) << (sh_1)) | ((w1) >> (sh_2)))
# else
#  error "Macro MERGE() hasn't defined!"
# endif
#endif

/*
 * Copy exactly NBYTES bytes from SRC_BP to DST_BP,
 * without any assumptions about alignment of the pointers.
 */
#ifndef BYTE_COPY_FWD
#define BYTE_COPY_FWD(dst_bp, src_bp, nbytes)				      \
do {									      \
	size_t __nbytes = (nbytes);					      \
	while (__nbytes > 0) {						      \
		byte __x = ((byte *) src_bp)[0];			      \
		src_bp += 1;						      \
		__nbytes -= 1;						      \
		((byte *) dst_bp)[0] = __x;				      \
		dst_bp += 1;						      \
	}								      \
} while (0)
#endif

/*
 * Copy exactly NBYTES_TO_COPY bytes from SRC_END_PTR to DST_END_PTR,
 * beginning at the bytes right before the pointers and continuing towards
 * smaller addresses.  Don't assume anything about alignment of the
 * pointers.
 */
#ifndef BYTE_COPY_BWD
#define BYTE_COPY_BWD(dst_ep, src_ep, nbytes)				      \
do {									      \
	size_t __nbytes = (nbytes);					      \
	while (__nbytes > 0) {						      \
		byte __x;						      \
		src_ep -= 1;						      \
		__x = ((byte *) src_ep)[0];				      \
		dst_ep -= 1;						      \
		__nbytes -= 1;						      \
		((byte *) dst_ep)[0] = __x;				      \
	}								      \
} while (0)
#endif
/*
 * Copy *up to* NBYTES bytes from SRC_BP to DST_BP, with
 * the assumption that DST_BP is aligned on an OPSIZ multiple.  If
 * not all bytes could be easily copied, store remaining number of bytes
 * in NBYTES_LEFT, otherwise store 0.
 */
extern void _wordcopy_fwd_aligned(long int, long int, size_t);
extern void _wordcopy_fwd_dest_aligned(long int, long int, size_t);
#ifndef WORD_COPY_FWD
#define WORD_COPY_FWD(dst_bp, src_bp, nbytes_left, nbytes)		      \
do {									      \
	if (src_bp % OPSIZ == 0)					      \
		_wordcopy_fwd_aligned (dst_bp, src_bp, (nbytes) / OPSIZ);     \
	else								      \
		_wordcopy_fwd_dest_aligned (dst_bp, src_bp, (nbytes) / OPSIZ);\
									      \
	src_bp += (nbytes) & -OPSIZ;					      \
	dst_bp += (nbytes) & -OPSIZ;					      \
	(nbytes_left) = (nbytes) % OPSIZ;				      \
} while (0)
#endif

/*
 * Copy *up to* NBYTES_TO_COPY bytes from SRC_END_PTR to DST_END_PTR,
 * beginning at the words (of type op_t) right before the pointers and
 * continuing towards smaller addresses.  May take advantage of that
 * DST_END_PTR is aligned on an OPSIZ multiple.  If not all bytes could be
 * easily copied, store remaining number of bytes in NBYTES_REMAINING,
 * otherwise store 0.
 */
extern void _wordcopy_bwd_aligned(long int, long int, size_t);
extern void _wordcopy_bwd_dest_aligned(long int, long int, size_t);
#ifndef WORD_COPY_BWD
#define WORD_COPY_BWD(dst_ep, src_ep, nbytes_left, nbytes)		      \
do {									      \
	if (src_ep % OPSIZ == 0)					      \
		_wordcopy_bwd_aligned (dst_ep, src_ep, (nbytes) / OPSIZ);     \
	else								      \
		_wordcopy_bwd_dest_aligned (dst_ep, src_ep, (nbytes) / OPSIZ);\
									      \
	src_ep -= (nbytes) & -OPSIZ;					      \
	dst_ep -= (nbytes) & -OPSIZ;					      \
	(nbytes_left) = (nbytes) % OPSIZ;				      \
} while (0)
#endif

/* Copy memory from the beginning to the end */
#ifndef MEM_COPY_FWD
static __always_inline void mem_copy_fwd(unsigned long dstp,
					unsigned long srcp,
					size_t count)
{
	/* If there not too few bytes to copy, use word copy. */
	if (count >= OP_T_THRESHOLD) {
		/* Copy just a few bytes to make dstp aligned. */
		count -= (-dstp) % OPSIZ;
		BYTE_COPY_FWD(dstp, srcp, (-dstp) % OPSIZ);

		/*
		 * Copy from srcp to dstp taking advantage of the known
		 * alignment of dstp. Number if bytes remaining is put in
		 * the third argument.
		 */
		WORD_COPY_FWD(dstp, srcp, count, count);

		/* Fall out and copy the tail. */
	}

	/* There are just a few bytes to copy. Use byte memory operations. */
	BYTE_COPY_FWD(dstp, srcp, count);
}
#endif

/* Copy memory from the end to the beginning.  */
#ifndef MEM_COPY_BWD
static __always_inline void mem_copy_bwd(unsigned long dstp,
					unsigned long srcp,
					size_t count)
{
	srcp += count;
	dstp += count;

	/* If there not too few bytes to copy, use word copy. */
	if (count >= OP_T_THRESHOLD) {
		/* Copy just a few bytes to make dstp aligned. */
		count -= dstp % OPSIZ;
		BYTE_COPY_BWD(dstp, srcp, dstp % OPSIZ);

		/*
		 * Copy from srcp to dstp taking advantage of the known
		 * alignment of dstp. Number if bytes remaining is put in
		 * the third argument.
		 */
		WORD_COPY_BWD(dstp, srcp, count, count);

		/* Fall out and copy the tail. */
	}

	/* There are just a few bytes to copy. Use byte memory operations. */
	BYTE_COPY_BWD (dstp, srcp, count);
}
#endif

#endif
