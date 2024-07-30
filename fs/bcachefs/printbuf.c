// SPDX-License-Identifier: LGPL-2.1+
/* Copyright (C) 2022 Kent Overstreet */

#include <linux/bitmap.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>

#include "printbuf.h"

static inline unsigned __printbuf_linelen(struct printbuf *buf, unsigned pos)
{
	return pos - buf->last_newline;
}

static inline unsigned printbuf_linelen(struct printbuf *buf)
{
	return __printbuf_linelen(buf, buf->pos);
}

/*
 * Returns spaces from start of line, if set, or 0 if unset:
 */
static inline unsigned cur_tabstop(struct printbuf *buf)
{
	return buf->cur_tabstop < buf->nr_tabstops
		? buf->_tabstops[buf->cur_tabstop]
		: 0;
}

int bch2_printbuf_make_room(struct printbuf *out, unsigned extra)
{
	/* Reserved space for terminating nul: */
	extra += 1;

	if (out->pos + extra <= out->size)
		return 0;

	if (!out->heap_allocated) {
		out->overflow = true;
		return 0;
	}

	unsigned new_size = roundup_pow_of_two(out->size + extra);

	/* Sanity check... */
	if (new_size > PAGE_SIZE << MAX_PAGE_ORDER) {
		out->allocation_failure = true;
		out->overflow = true;
		return -ENOMEM;
	}

	/*
	 * Note: output buffer must be freeable with kfree(), it's not required
	 * that the user use printbuf_exit().
	 */
	char *buf = krealloc(out->buf, new_size, !out->atomic ? GFP_KERNEL : GFP_NOWAIT);

	if (!buf) {
		out->allocation_failure = true;
		out->overflow = true;
		return -ENOMEM;
	}

	out->buf	= buf;
	out->size	= new_size;
	return 0;
}

static void printbuf_advance_pos(struct printbuf *out, unsigned len)
{
	out->pos += min(len, printbuf_remaining(out));
}

static void printbuf_insert_spaces(struct printbuf *out, unsigned pos, unsigned nr)
{
	unsigned move = out->pos - pos;

	bch2_printbuf_make_room(out, nr);

	if (pos + nr < out->size)
		memmove(out->buf + pos + nr,
			out->buf + pos,
			min(move, out->size - 1 - pos - nr));

	if (pos < out->size)
		memset(out->buf + pos, ' ', min(nr, out->size - pos));

	printbuf_advance_pos(out, nr);
	printbuf_nul_terminate_reserved(out);
}

static void __printbuf_do_indent(struct printbuf *out, unsigned pos)
{
	while (true) {
		int pad;
		unsigned len = out->pos - pos;
		char *p = out->buf + pos;
		char *n = memscan(p, '\n', len);
		if (cur_tabstop(out)) {
			n = min(n, (char *) memscan(p, '\r', len));
			n = min(n, (char *) memscan(p, '\t', len));
		}

		pos = n - out->buf;
		if (pos == out->pos)
			break;

		switch (*n) {
		case '\n':
			pos++;
			out->last_newline = pos;

			printbuf_insert_spaces(out, pos, out->indent);

			pos = min(pos + out->indent, out->pos);
			out->last_field = pos;
			out->cur_tabstop = 0;
			break;
		case '\r':
			memmove(n, n + 1, out->pos - pos);
			--out->pos;
			pad = (int) cur_tabstop(out) - (int) __printbuf_linelen(out, pos);
			if (pad > 0) {
				printbuf_insert_spaces(out, out->last_field, pad);
				pos += pad;
			}

			out->last_field = pos;
			out->cur_tabstop++;
			break;
		case '\t':
			pad = (int) cur_tabstop(out) - (int) __printbuf_linelen(out, pos) - 1;
			if (pad > 0) {
				*n = ' ';
				printbuf_insert_spaces(out, pos, pad - 1);
				pos += pad;
			} else {
				memmove(n, n + 1, out->pos - pos);
				--out->pos;
			}

			out->last_field = pos;
			out->cur_tabstop++;
			break;
		}
	}
}

static inline void printbuf_do_indent(struct printbuf *out, unsigned pos)
{
	if (out->has_indent_or_tabstops && !out->suppress_indent_tabstop_handling)
		__printbuf_do_indent(out, pos);
}

void bch2_prt_vprintf(struct printbuf *out, const char *fmt, va_list args)
{
	int len;

	do {
		va_list args2;

		va_copy(args2, args);
		len = vsnprintf(out->buf + out->pos, printbuf_remaining_size(out), fmt, args2);
		va_end(args2);
	} while (len > printbuf_remaining(out) &&
		 !bch2_printbuf_make_room(out, len));

	unsigned indent_pos = out->pos;
	printbuf_advance_pos(out, len);
	printbuf_do_indent(out, indent_pos);
}

void bch2_prt_printf(struct printbuf *out, const char *fmt, ...)
{
	va_list args;
	int len;

	do {
		va_start(args, fmt);
		len = vsnprintf(out->buf + out->pos, printbuf_remaining_size(out), fmt, args);
		va_end(args);
	} while (len > printbuf_remaining(out) &&
		 !bch2_printbuf_make_room(out, len));

	unsigned indent_pos = out->pos;
	printbuf_advance_pos(out, len);
	printbuf_do_indent(out, indent_pos);
}

/**
 * bch2_printbuf_str() - returns printbuf's buf as a C string, guaranteed to be
 * null terminated
 * @buf:	printbuf to terminate
 * Returns:	Printbuf contents, as a nul terminated C string
 */
const char *bch2_printbuf_str(const struct printbuf *buf)
{
	/*
	 * If we've written to a printbuf then it's guaranteed to be a null
	 * terminated string - but if we haven't, then we might not have
	 * allocated a buffer at all:
	 */
	return buf->pos
		? buf->buf
		: "";
}

/**
 * bch2_printbuf_exit() - exit a printbuf, freeing memory it owns and poisoning it
 * against accidental use.
 * @buf:	printbuf to exit
 */
void bch2_printbuf_exit(struct printbuf *buf)
{
	if (buf->heap_allocated) {
		kfree(buf->buf);
		buf->buf = ERR_PTR(-EINTR); /* poison value */
	}
}

void bch2_printbuf_tabstops_reset(struct printbuf *buf)
{
	buf->nr_tabstops = 0;
}

void bch2_printbuf_tabstop_pop(struct printbuf *buf)
{
	if (buf->nr_tabstops)
		--buf->nr_tabstops;
}

/*
 * bch2_printbuf_tabstop_set() - add a tabstop, n spaces from the previous tabstop
 *
 * @buf: printbuf to control
 * @spaces: number of spaces from previous tabpstop
 *
 * In the future this function may allocate memory if setting more than
 * PRINTBUF_INLINE_TABSTOPS or setting tabstops more than 255 spaces from start
 * of line.
 */
int bch2_printbuf_tabstop_push(struct printbuf *buf, unsigned spaces)
{
	unsigned prev_tabstop = buf->nr_tabstops
		? buf->_tabstops[buf->nr_tabstops - 1]
		: 0;

	if (WARN_ON(buf->nr_tabstops >= ARRAY_SIZE(buf->_tabstops)))
		return -EINVAL;

	buf->_tabstops[buf->nr_tabstops++] = prev_tabstop + spaces;
	buf->has_indent_or_tabstops = true;
	return 0;
}

/**
 * bch2_printbuf_indent_add() - add to the current indent level
 *
 * @buf: printbuf to control
 * @spaces: number of spaces to add to the current indent level
 *
 * Subsequent lines, and the current line if the output position is at the start
 * of the current line, will be indented by @spaces more spaces.
 */
void bch2_printbuf_indent_add(struct printbuf *buf, unsigned spaces)
{
	if (WARN_ON_ONCE(buf->indent + spaces < buf->indent))
		spaces = 0;

	buf->indent += spaces;
	prt_chars(buf, ' ', spaces);

	buf->has_indent_or_tabstops = true;
}

/**
 * bch2_printbuf_indent_sub() - subtract from the current indent level
 *
 * @buf: printbuf to control
 * @spaces: number of spaces to subtract from the current indent level
 *
 * Subsequent lines, and the current line if the output position is at the start
 * of the current line, will be indented by @spaces less spaces.
 */
void bch2_printbuf_indent_sub(struct printbuf *buf, unsigned spaces)
{
	if (WARN_ON_ONCE(spaces > buf->indent))
		spaces = buf->indent;

	if (buf->last_newline + buf->indent == buf->pos) {
		buf->pos -= spaces;
		printbuf_nul_terminate(buf);
	}
	buf->indent -= spaces;

	if (!buf->indent && !buf->nr_tabstops)
		buf->has_indent_or_tabstops = false;
}

void bch2_prt_newline(struct printbuf *buf)
{
	bch2_printbuf_make_room(buf, 1 + buf->indent);

	__prt_char_reserved(buf, '\n');

	buf->last_newline	= buf->pos;

	__prt_chars_reserved(buf, ' ', buf->indent);

	printbuf_nul_terminate_reserved(buf);

	buf->last_field		= buf->pos;
	buf->cur_tabstop	= 0;
}

void bch2_printbuf_strip_trailing_newline(struct printbuf *out)
{
	for (int p = out->pos - 1; p >= 0; --p) {
		if (out->buf[p] == '\n') {
			out->pos = p;
			break;
		}
		if (out->buf[p] != ' ')
			break;
	}

	printbuf_nul_terminate_reserved(out);
}

static void __prt_tab(struct printbuf *out)
{
	int spaces = max_t(int, 0, cur_tabstop(out) - printbuf_linelen(out));

	prt_chars(out, ' ', spaces);

	out->last_field = out->pos;
	out->cur_tabstop++;
}

/**
 * bch2_prt_tab() - Advance printbuf to the next tabstop
 * @out:	printbuf to control
 *
 * Advance output to the next tabstop by printing spaces.
 */
void bch2_prt_tab(struct printbuf *out)
{
	if (WARN_ON(!cur_tabstop(out)))
		return;

	__prt_tab(out);
}

static void __prt_tab_rjust(struct printbuf *buf)
{
	int pad = (int) cur_tabstop(buf) - (int) printbuf_linelen(buf);
	if (pad > 0)
		printbuf_insert_spaces(buf, buf->last_field, pad);

	buf->last_field = buf->pos;
	buf->cur_tabstop++;
}

/**
 * bch2_prt_tab_rjust - Advance printbuf to the next tabstop, right justifying
 * previous output
 *
 * @buf: printbuf to control
 *
 * Advance output to the next tabstop by inserting spaces immediately after the
 * previous tabstop, right justifying previously outputted text.
 */
void bch2_prt_tab_rjust(struct printbuf *buf)
{
	if (WARN_ON(!cur_tabstop(buf)))
		return;

	__prt_tab_rjust(buf);
}

/**
 * bch2_prt_bytes_indented() - Print an array of chars, handling embedded control characters
 *
 * @out:	output printbuf
 * @str:	string to print
 * @count:	number of bytes to print
 *
 * The following contol characters are handled as so:
 *   \n: prt_newline	newline that obeys current indent level
 *   \t: prt_tab	advance to next tabstop
 *   \r: prt_tab_rjust	advance to next tabstop, with right justification
 */
void bch2_prt_bytes_indented(struct printbuf *out, const char *str, unsigned count)
{
	unsigned indent_pos = out->pos;
	prt_bytes(out, str, count);
	printbuf_do_indent(out, indent_pos);
}

/**
 * bch2_prt_human_readable_u64() - Print out a u64 in human readable units
 * @out:	output printbuf
 * @v:		integer to print
 *
 * Units of 2^10 (default) or 10^3 are controlled via @out->si_units
 */
void bch2_prt_human_readable_u64(struct printbuf *out, u64 v)
{
	bch2_printbuf_make_room(out, 10);
	unsigned len = string_get_size(v, 1, !out->si_units,
				       out->buf + out->pos,
				       printbuf_remaining_size(out));
	printbuf_advance_pos(out, len);
}

/**
 * bch2_prt_human_readable_s64() - Print out a s64 in human readable units
 * @out:	output printbuf
 * @v:		integer to print
 *
 * Units of 2^10 (default) or 10^3 are controlled via @out->si_units
 */
void bch2_prt_human_readable_s64(struct printbuf *out, s64 v)
{
	if (v < 0)
		prt_char(out, '-');
	bch2_prt_human_readable_u64(out, abs(v));
}

/**
 * bch2_prt_units_u64() - Print out a u64 according to printbuf unit options
 * @out:	output printbuf
 * @v:		integer to print
 *
 * Units are either raw (default), or human reabable units (controlled via
 * @buf->human_readable_units)
 */
void bch2_prt_units_u64(struct printbuf *out, u64 v)
{
	if (out->human_readable_units)
		bch2_prt_human_readable_u64(out, v);
	else
		bch2_prt_printf(out, "%llu", v);
}

/**
 * bch2_prt_units_s64() - Print out a s64 according to printbuf unit options
 * @out:	output printbuf
 * @v:		integer to print
 *
 * Units are either raw (default), or human reabable units (controlled via
 * @buf->human_readable_units)
 */
void bch2_prt_units_s64(struct printbuf *out, s64 v)
{
	if (v < 0)
		prt_char(out, '-');
	bch2_prt_units_u64(out, abs(v));
}

void bch2_prt_string_option(struct printbuf *out,
			    const char * const list[],
			    size_t selected)
{
	for (size_t i = 0; list[i]; i++)
		bch2_prt_printf(out, i == selected ? "[%s] " : "%s ", list[i]);
}

void bch2_prt_bitflags(struct printbuf *out,
		       const char * const list[], u64 flags)
{
	unsigned bit, nr = 0;
	bool first = true;

	while (list[nr])
		nr++;

	while (flags && (bit = __ffs64(flags)) < nr) {
		if (!first)
			bch2_prt_printf(out, ",");
		first = false;
		bch2_prt_printf(out, "%s", list[bit]);
		flags ^= BIT_ULL(bit);
	}
}

void bch2_prt_bitflags_vector(struct printbuf *out,
			      const char * const list[],
			      unsigned long *v, unsigned nr)
{
	bool first = true;
	unsigned i;

	for (i = 0; i < nr; i++)
		if (!list[i]) {
			nr = i - 1;
			break;
		}

	for_each_set_bit(i, v, nr) {
		if (!first)
			bch2_prt_printf(out, ",");
		first = false;
		bch2_prt_printf(out, "%s", list[i]);
	}
}
