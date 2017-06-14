#ifndef PERF_PRINT_BINARY_H
#define PERF_PRINT_BINARY_H

#include <stddef.h>

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

typedef void (*print_binary_t)(enum binary_printer_ops op,
			       unsigned int val, void *extra);

void print_binary(unsigned char *data, size_t len,
		  size_t bytes_per_line, print_binary_t printer,
		  void *extra);

int is_printable_array(char *p, unsigned int len);

#endif /* PERF_PRINT_BINARY_H */
