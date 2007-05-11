/*
 * lib/hexdump.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */

#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/module.h>

/**
 * hex_dump_to_buffer - convert a blob of data to "hex ASCII" in memory
 * @buf: data blob to dump
 * @len: number of bytes in the @buf
 * @linebuf: where to put the converted data
 * @linebuflen: total size of @linebuf, including space for terminating NUL
 *
 * hex_dump_to_buffer() works on one "line" of output at a time, i.e.,
 * 16 bytes of input data converted to hex + ASCII output.
 *
 * Given a buffer of u8 data, hex_dump_to_buffer() converts the input data
 * to a hex + ASCII dump at the supplied memory location.
 * The converted output is always NUL-terminated.
 *
 * E.g.:
 *	hex_dump_to_buffer(frame->data, frame->len, linebuf, sizeof(linebuf));
 *
 * example output buffer:
 * 40414243 44454647 48494a4b 4c4d4e4f  @ABCDEFGHIJKLMNO
 */
void hex_dump_to_buffer(const void *buf, size_t len, char *linebuf,
			size_t linebuflen)
{
	const u8 *ptr = buf;
	u8 ch;
	int j, lx = 0;

	for (j = 0; (j < 16) && (j < len) && (lx + 3) < linebuflen; j++) {
		if (j && !(j % 4))
			linebuf[lx++] = ' ';
		ch = ptr[j];
		linebuf[lx++] = hex_asc(ch >> 4);
		linebuf[lx++] = hex_asc(ch & 0x0f);
	}
	if ((lx + 2) < linebuflen) {
		linebuf[lx++] = ' ';
		linebuf[lx++] = ' ';
	}
	for (j = 0; (j < 16) && (j < len) && (lx + 2) < linebuflen; j++)
		linebuf[lx++] = isprint(ptr[j]) ? ptr[j] : '.';
	linebuf[lx++] = '\0';
}
EXPORT_SYMBOL(hex_dump_to_buffer);

/**
 * print_hex_dump - print a text hex dump to syslog for a binary blob of data
 * @level: kernel log level (e.g. KERN_DEBUG)
 * @prefix_type: controls whether prefix of an offset, address, or none
 *  is printed (%DUMP_PREFIX_OFFSET, %DUMP_PREFIX_ADDRESS, %DUMP_PREFIX_NONE)
 * @buf: data blob to dump
 * @len: number of bytes in the @buf
 *
 * Given a buffer of u8 data, print_hex_dump() prints a hex + ASCII dump
 * to the kernel log at the specified kernel log level, with an optional
 * leading prefix.
 *
 * E.g.:
 *   print_hex_dump(KERN_DEBUG, DUMP_PREFIX_ADDRESS, frame->data, frame->len);
 *
 * Example output using %DUMP_PREFIX_OFFSET:
 * 0009ab42: 40414243 44454647 48494a4b 4c4d4e4f  @ABCDEFGHIJKLMNO
 * Example output using %DUMP_PREFIX_ADDRESS:
 * ffffffff88089af0: 70717273 74757677 78797a7b 7c7d7e7f  pqrstuvwxyz{|}~.
 */
void print_hex_dump(const char *level, int prefix_type, void *buf, size_t len)
{
	u8 *ptr = buf;
	int i, linelen, remaining = len;
	unsigned char linebuf[100];

	for (i = 0; i < len; i += 16) {
		linelen = min(remaining, 16);
		remaining -= 16;
		hex_dump_to_buffer(ptr + i, linelen, linebuf, sizeof(linebuf));

		switch (prefix_type) {
		case DUMP_PREFIX_ADDRESS:
			printk("%s%*p: %s\n", level,
				(int)(2 * sizeof(void *)), ptr + i, linebuf);
			break;
		case DUMP_PREFIX_OFFSET:
			printk("%s%.8x: %s\n", level, i, linebuf);
			break;
		default:
			printk("%s%s\n", level, linebuf);
			break;
		}
	}
}
EXPORT_SYMBOL(print_hex_dump);
