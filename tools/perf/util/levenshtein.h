/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_LEVENSHTEIN_H
#define __PERF_LEVENSHTEIN_H

int levenshtein(const char *string1, const char *string2,
	int swap_penalty, int substition_penalty,
	int insertion_penalty, int deletion_penalty);

#endif /* __PERF_LEVENSHTEIN_H */
