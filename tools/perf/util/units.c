// SPDX-License-Identifier: GPL-2.0
#include "units.h"
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/time64.h>

unsigned long parse_tag_value(const char *str, struct parse_tag *tags)
{
	struct parse_tag *i = tags;

	while (i->tag) {
		char *s = strchr(str, i->tag);

		if (s) {
			unsigned long int value;
			char *endptr;

			value = strtoul(str, &endptr, 10);
			if (s != endptr)
				break;

			if (value > ULONG_MAX / i->mult)
				break;
			value *= i->mult;
			return value;
		}
		i++;
	}

	return (unsigned long) -1;
}

unsigned long convert_unit(unsigned long value, char *unit)
{
	*unit = ' ';

	if (value > 1000) {
		value /= 1000;
		*unit = 'K';
	}

	if (value > 1000) {
		value /= 1000;
		*unit = 'M';
	}

	if (value > 1000) {
		value /= 1000;
		*unit = 'G';
	}

	return value;
}

int unit_number__scnprintf(char *buf, size_t size, u64 n)
{
	char unit[4] = "BKMG";
	int i = 0;

	while (((n / 1024) > 1) && (i < 3)) {
		n /= 1024;
		i++;
	}

	return scnprintf(buf, size, "%" PRIu64 "%c", n, unit[i]);
}
