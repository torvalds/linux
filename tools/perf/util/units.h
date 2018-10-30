/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_UNIT_H
#define PERF_UNIT_H

#include <stddef.h>
#include <linux/types.h>

struct parse_tag {
	char tag;
	int  mult;
};

unsigned long parse_tag_value(const char *str, struct parse_tag *tags);

unsigned long convert_unit(unsigned long value, char *unit);
int unit_number__scnprintf(char *buf, size_t size, u64 n);

#endif /* PERF_UNIT_H */
