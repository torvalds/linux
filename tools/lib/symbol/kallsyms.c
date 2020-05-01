// SPDX-License-Identifier: GPL-2.0
#include "symbol/kallsyms.h"
#include "api/io.h"
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>

u8 kallsyms2elf_type(char type)
{
	type = tolower(type);
	return (type == 't' || type == 'w') ? STT_FUNC : STT_OBJECT;
}

/*
 * While we find nice hex chars, build a long_val.
 * Return number of chars processed.
 */
int hex2u64(const char *ptr, u64 *long_val)
{
	char *p;

	*long_val = strtoull(ptr, &p, 16);

	return p - ptr;
}

bool kallsyms__is_function(char symbol_type)
{
	symbol_type = toupper(symbol_type);
	return symbol_type == 'T' || symbol_type == 'W';
}

static void read_to_eol(struct io *io)
{
	int ch;

	for (;;) {
		ch = io__get_char(io);
		if (ch < 0 || ch == '\n')
			return;
	}
}

int kallsyms__parse(const char *filename, void *arg,
		    int (*process_symbol)(void *arg, const char *name,
					  char type, u64 start))
{
	struct io io;
	char bf[BUFSIZ];
	int err;

	io.fd = open(filename, O_RDONLY, 0);

	if (io.fd < 0)
		return -1;

	io__init(&io, io.fd, bf, sizeof(bf));

	err = 0;
	while (!io.eof) {
		__u64 start;
		int ch;
		size_t i;
		char symbol_type;
		char symbol_name[KSYM_NAME_LEN + 1];

		if (io__get_hex(&io, &start) != ' ') {
			read_to_eol(&io);
			continue;
		}
		symbol_type = io__get_char(&io);
		if (io__get_char(&io) != ' ') {
			read_to_eol(&io);
			continue;
		}
		for (i = 0; i < sizeof(symbol_name); i++) {
			ch = io__get_char(&io);
			if (ch < 0 || ch == '\n')
				break;
			symbol_name[i]  = ch;
		}
		symbol_name[i]  = '\0';

		err = process_symbol(arg, symbol_name, symbol_type, start);
		if (err)
			break;
	}

	close(io.fd);
	return err;
}
