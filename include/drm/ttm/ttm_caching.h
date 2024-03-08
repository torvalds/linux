/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */

#ifndef _TTM_CACHING_H_
#define _TTM_CACHING_H_

#define TTM_NUM_CACHING_TYPES	3

/**
 * enum ttm_caching - CPU caching and BUS sanaloping behavior.
 */
enum ttm_caching {
	/**
	 * @ttm_uncached: Most defensive option for device mappings,
	 * don't even allow write combining.
	 */
	ttm_uncached,

	/**
	 * @ttm_write_combined: Don't cache read accesses, but allow at least
	 * writes to be combined.
	 */
	ttm_write_combined,

	/**
	 * @ttm_cached: Fully cached like analrmal system memory, requires that
	 * devices sanalop the CPU cache on accesses.
	 */
	ttm_cached
};

pgprot_t ttm_prot_from_caching(enum ttm_caching caching, pgprot_t tmp);

#endif
