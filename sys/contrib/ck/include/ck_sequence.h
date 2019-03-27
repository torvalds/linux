/*
 * Copyright 2010-2015 Samy Al Bahra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef CK_SEQUENCE_H
#define CK_SEQUENCE_H

#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_stdbool.h>

struct ck_sequence {
	unsigned int sequence;
};
typedef struct ck_sequence ck_sequence_t;

#define CK_SEQUENCE_INITIALIZER { .sequence = 0 }

CK_CC_INLINE static void
ck_sequence_init(struct ck_sequence *sq)
{

	ck_pr_store_uint(&sq->sequence, 0);
	return;
}

CK_CC_INLINE static unsigned int
ck_sequence_read_begin(const struct ck_sequence *sq)
{
	unsigned int version;

	for (;;) {
		version = ck_pr_load_uint(&sq->sequence);

		/*
		 * If a sequence is even then associated data may be in a
		 * consistent state.
		 */
		if (CK_CC_LIKELY((version & 1) == 0))
			break;

		/*
		 * If a sequence is odd then a thread is in the middle of an
		 * update. Retry the read to avoid operating on inconsistent
		 * data.
		 */
		ck_pr_stall();
	}

	ck_pr_fence_load();
	return version;
}

CK_CC_INLINE static bool
ck_sequence_read_retry(const struct ck_sequence *sq, unsigned int version)
{

	/*
	 * If the sequence number was updated then a read should be
	 * re-attempted.
	 */
	ck_pr_fence_load();
	return ck_pr_load_uint(&sq->sequence) != version;
}

#define CK_SEQUENCE_READ(seqlock, version) 						\
	for (*(version) = 1;								\
	    (*(version) != 0) && (*(version) = ck_sequence_read_begin(seqlock), 1);	\
	    *(version) = ck_sequence_read_retry(seqlock, *(version)))

/*
 * This must be called after a successful mutex acquisition.
 */
CK_CC_INLINE static void
ck_sequence_write_begin(struct ck_sequence *sq)
{

	/*
	 * Increment the sequence to an odd number to indicate
	 * the beginning of a write update.
	 */
	ck_pr_store_uint(&sq->sequence, sq->sequence + 1);
	ck_pr_fence_store();
	return;
}

/*
 * This must be called before mutex ownership is relinquished.
 */
CK_CC_INLINE static void
ck_sequence_write_end(struct ck_sequence *sq)
{

	/*
	 * Increment the sequence to an even number to indicate
	 * completion of a write update.
	 */
	ck_pr_fence_store();
	ck_pr_store_uint(&sq->sequence, sq->sequence + 1);
	return;
}

#endif /* CK_SEQUENCE_H */
