// SPDX-License-Identifier: GPL-2.0
#include "print_binary.h"
#include <linux/log2.h>
#include "sane_ctype.h"

void print_binary(unsigned char *data, size_t len,
		  size_t bytes_per_line, print_binary_t printer,
		  void *extra)
{
	size_t i, j, mask;

	if (!printer)
		return;

	bytes_per_line = roundup_pow_of_two(bytes_per_line);
	mask = bytes_per_line - 1;

	printer(BINARY_PRINT_DATA_BEGIN, 0, extra);
	for (i = 0; i < len; i++) {
		if ((i & mask) == 0) {
			printer(BINARY_PRINT_LINE_BEGIN, -1, extra);
			printer(BINARY_PRINT_ADDR, i, extra);
		}

		printer(BINARY_PRINT_NUM_DATA, data[i], extra);

		if (((i & mask) == mask) || i == len - 1) {
			for (j = 0; j < mask-(i & mask); j++)
				printer(BINARY_PRINT_NUM_PAD, -1, extra);

			printer(BINARY_PRINT_SEP, i, extra);
			for (j = i & ~mask; j <= i; j++)
				printer(BINARY_PRINT_CHAR_DATA, data[j], extra);
			for (j = 0; j < mask-(i & mask); j++)
				printer(BINARY_PRINT_CHAR_PAD, i, extra);
			printer(BINARY_PRINT_LINE_END, -1, extra);
		}
	}
	printer(BINARY_PRINT_DATA_END, -1, extra);
}

int is_printable_array(char *p, unsigned int len)
{
	unsigned int i;

	if (!p || !len || p[len - 1] != 0)
		return 0;

	len--;

	for (i = 0; i < len; i++) {
		if (!isprint(p[i]) && !isspace(p[i]))
			return 0;
	}
	return 1;
}
