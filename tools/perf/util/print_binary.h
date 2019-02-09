/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_PRINT_BINARY_H
#define PERF_PRINT_BINARY_H

#include <stddef.h>
#include <stdio.h>

enum binary_printer_ops {
	BINARY_PRINT_DATA_BEGIN,
	BINARY_PRINT_LINE_BEGIN,
	BINARY_PRINT_ADDR,
	BINARY_PRINT_NUM_DATA,
	BINARY_PRINT_NUM_PAD,
	BINARY_PRINT_SEP,
	BINARY_PRINT_CHAR_DATA,
	BINARY_PRINT_CHAR_PAD,
	BINARY_PRINT_LINE_END,
	BINARY_PRINT_DATA_END,
};

typedef int (*binary__fprintf_t)(enum binary_printer_ops op,
				 unsigned int val, void *extra, FILE *fp);

int binary__fprintf(unsigned char *data, size_t len,
		    size_t bytes_per_line, binary__fprintf_t printer,
		    void *extra, FILE *fp);

static inline void print_binary(unsigned char *data, size_t len,
				size_t bytes_per_line, binary__fprintf_t printer,
				void *extra)
{
	binary__fprintf(data, len, bytes_per_line, printer, extra, stdout);
}

int is_printable_array(char *p, unsigned int len);

#endif /* PERF_PRINT_BINARY_H */
