// SPDX-License-Identifier: GPL-2.0
#include "print_binary.h"
#include <linux/log2.h>
#include <linux/ctype.h>

int binary__fprintf(unsigned char *data, size_t len,
		    size_t bytes_per_line, binary__fprintf_t printer,
		    void *extra, FILE *fp)
{
	size_t i, j, mask;
	int printed = 0;

	if (!printer)
		return 0;

	bytes_per_line = roundup_pow_of_two(bytes_per_line);
	mask = bytes_per_line - 1;

	printed += printer(BINARY_PRINT_DATA_BEGIN, 0, extra, fp);
	for (i = 0; i < len; i++) {
		if ((i & mask) == 0) {
			printed += printer(BINARY_PRINT_LINE_BEGIN, -1, extra, fp);
			printed += printer(BINARY_PRINT_ADDR, i, extra, fp);
		}

		printed += printer(BINARY_PRINT_NUM_DATA, data[i], extra, fp);

		if (((i & mask) == mask) || i == len - 1) {
			for (j = 0; j < mask-(i & mask); j++)
				printed += printer(BINARY_PRINT_NUM_PAD, -1, extra, fp);

			printer(BINARY_PRINT_SEP, i, extra, fp);
			for (j = i & ~mask; j <= i; j++)
				printed += printer(BINARY_PRINT_CHAR_DATA, data[j], extra, fp);
			for (j = 0; j < mask-(i & mask); j++)
				printed += printer(BINARY_PRINT_CHAR_PAD, i, extra, fp);
			printed += printer(BINARY_PRINT_LINE_END, -1, extra, fp);
		}
	}
	printed += printer(BINARY_PRINT_DATA_END, -1, extra, fp);
	return printed;
}

int is_printable_array(char *p, unsigned int len)
{
	unsigned int i;

	if (!p || !len || p[len - 1] != 0)
		return 0;

	len--;

	for (i = 0; i < len && p[i]; i++) {
		if (!isprint(p[i]) && !isspace(p[i]))
			return 0;
	}
	return 1;
}
