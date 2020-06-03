// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (C) 2020 Facebook, Inc. */
#include <stdlib.h>
#include <errno.h>
#include "testing_helpers.h"

int parse_num_list(const char *s, bool **num_set, int *num_set_len)
{
	int i, set_len = 0, new_len, num, start = 0, end = -1;
	bool *set = NULL, *tmp, parsing_end = false;
	char *next;

	while (s[0]) {
		errno = 0;
		num = strtol(s, &next, 10);
		if (errno)
			return -errno;

		if (parsing_end)
			end = num;
		else
			start = num;

		if (!parsing_end && *next == '-') {
			s = next + 1;
			parsing_end = true;
			continue;
		} else if (*next == ',') {
			parsing_end = false;
			s = next + 1;
			end = num;
		} else if (*next == '\0') {
			parsing_end = false;
			s = next;
			end = num;
		} else {
			return -EINVAL;
		}

		if (start > end)
			return -EINVAL;

		if (end + 1 > set_len) {
			new_len = end + 1;
			tmp = realloc(set, new_len);
			if (!tmp) {
				free(set);
				return -ENOMEM;
			}
			for (i = set_len; i < start; i++)
				tmp[i] = false;
			set = tmp;
			set_len = new_len;
		}
		for (i = start; i <= end; i++)
			set[i] = true;
	}

	if (!set)
		return -EINVAL;

	*num_set = set;
	*num_set_len = set_len;

	return 0;
}
