/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/sparsebit.h
 *
 * Copyright (C) 2018, Google LLC.
 *
 * Header file that describes API to the sparsebit library.
 * This library provides a memory efficient means of storing
 * the settings of bits indexed via a uint64_t.  Memory usage
 * is reasonable, significantly less than (2^64 / 8) bytes, as
 * long as bits that are mostly set or mostly cleared are close
 * to each other.  This library is efficient in memory usage
 * even in the case where most bits are set.
 */

#ifndef SELFTEST_KVM_SPARSEBIT_H
#define SELFTEST_KVM_SPARSEBIT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sparsebit;
typedef uint64_t sparsebit_idx_t;
typedef uint64_t sparsebit_num_t;

struct sparsebit *sparsebit_alloc(void);
void sparsebit_free(struct sparsebit **sbitp);
void sparsebit_copy(struct sparsebit *dstp, const struct sparsebit *src);

bool sparsebit_is_set(const struct sparsebit *sbit, sparsebit_idx_t idx);
bool sparsebit_is_set_num(const struct sparsebit *sbit,
			  sparsebit_idx_t idx, sparsebit_num_t num);
bool sparsebit_is_clear(const struct sparsebit *sbit, sparsebit_idx_t idx);
bool sparsebit_is_clear_num(const struct sparsebit *sbit,
			    sparsebit_idx_t idx, sparsebit_num_t num);
sparsebit_num_t sparsebit_num_set(const struct sparsebit *sbit);
bool sparsebit_any_set(const struct sparsebit *sbit);
bool sparsebit_any_clear(const struct sparsebit *sbit);
bool sparsebit_all_set(const struct sparsebit *sbit);
bool sparsebit_all_clear(const struct sparsebit *sbit);
sparsebit_idx_t sparsebit_first_set(const struct sparsebit *sbit);
sparsebit_idx_t sparsebit_first_clear(const struct sparsebit *sbit);
sparsebit_idx_t sparsebit_next_set(const struct sparsebit *sbit, sparsebit_idx_t prev);
sparsebit_idx_t sparsebit_next_clear(const struct sparsebit *sbit, sparsebit_idx_t prev);
sparsebit_idx_t sparsebit_next_set_num(const struct sparsebit *sbit,
				       sparsebit_idx_t start, sparsebit_num_t num);
sparsebit_idx_t sparsebit_next_clear_num(const struct sparsebit *sbit,
					 sparsebit_idx_t start, sparsebit_num_t num);

void sparsebit_set(struct sparsebit *sbitp, sparsebit_idx_t idx);
void sparsebit_set_num(struct sparsebit *sbitp, sparsebit_idx_t start,
		       sparsebit_num_t num);
void sparsebit_set_all(struct sparsebit *sbitp);

void sparsebit_clear(struct sparsebit *sbitp, sparsebit_idx_t idx);
void sparsebit_clear_num(struct sparsebit *sbitp,
			 sparsebit_idx_t start, sparsebit_num_t num);
void sparsebit_clear_all(struct sparsebit *sbitp);

void sparsebit_dump(FILE *stream, const struct sparsebit *sbit,
		    unsigned int indent);
void sparsebit_validate_internal(const struct sparsebit *sbit);

/*
 * Iterate over an inclusive ranges within sparsebit @s. In each iteration,
 * @range_begin and @range_end will take the beginning and end of the set
 * range, which are of type sparsebit_idx_t.
 *
 * For example, if the range [3, 7] (inclusive) is set, within the
 * iteration,@range_begin will take the value 3 and @range_end will take
 * the value 7.
 *
 * Ensure that there is at least one bit set before using this macro with
 * sparsebit_any_set(), because sparsebit_first_set() will abort if none
 * are set.
 */
#define sparsebit_for_each_set_range(s, range_begin, range_end)         \
	for (range_begin = sparsebit_first_set(s),                      \
	     range_end = sparsebit_next_clear(s, range_begin) - 1;	\
	     range_begin && range_end;                                  \
	     range_begin = sparsebit_next_set(s, range_end),            \
	     range_end = sparsebit_next_clear(s, range_begin) - 1)

#ifdef __cplusplus
}
#endif

#endif /* SELFTEST_KVM_SPARSEBIT_H */
