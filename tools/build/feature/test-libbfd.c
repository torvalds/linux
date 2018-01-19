// SPDX-License-Identifier: GPL-2.0
#include <bfd.h>

extern int printf(const char *format, ...);

int main(void)
{
	char symbol[4096] = "FieldName__9ClassNameFd";
	char *tmp;

	tmp = bfd_demangle(0, symbol, 0);

	printf("demangled symbol: {%s}\n", tmp);

	return 0;
}
