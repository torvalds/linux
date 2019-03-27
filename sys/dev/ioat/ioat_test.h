/*-
 * Copyright (C) 2012 Intel Corporation
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

__FBSDID("$FreeBSD$");

#ifndef __IOAT_TEST_H__
#define __IOAT_TEST_H__

enum ioat_res {
	IOAT_TEST_OK = 0,
	IOAT_TEST_NO_DMA_ENGINE,
	IOAT_TEST_NO_MEMORY,
	IOAT_TEST_MISCOMPARE,
	IOAT_TEST_INVALID_INPUT,
	IOAT_NUM_RES
};

enum ioat_test_kind {
	IOAT_TEST_FILL = 0,
	IOAT_TEST_DMA,
	IOAT_TEST_RAW_DMA,
	IOAT_TEST_DMA_8K,
	IOAT_TEST_MEMCPY,
	IOAT_NUM_TESTKINDS
};

struct test_transaction;

struct ioat_test {
	volatile uint32_t status[IOAT_NUM_RES];
	uint32_t channel_index;

	enum ioat_test_kind testkind;

	/* HW max of 1MB */
	uint32_t buffer_size;
	uint32_t chain_depth;
	uint32_t transactions;

	/*
	 * If non-zero, duration is time in ms;
	 * If zero, bounded by 'transactions' above.
	 */
	uint32_t duration;

	/* If true, check for miscompares after a copy. */
	bool verify;

	/* DMA directly to/from some memory address */
	uint64_t raw_target;
	void *raw_vtarget;
	bool raw_write;
	bool raw_is_virtual;

	bool zero_stats;
	/* Configure coalesce period */
	uint16_t coalesce_period;

	/* Internal usage -- not test inputs */
	TAILQ_HEAD(, test_transaction) free_q;
	TAILQ_HEAD(, test_transaction) pend_q;
	volatile bool too_late;
};

#define	IOAT_DMATEST	_IOWR('i', 0, struct ioat_test)

#endif /* __IOAT_TEST_H__ */
