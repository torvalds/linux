// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause)
/*
 * Simple streaming JSON writer
 *
 * This takes care of the annoying bits of JSON syntax like the commas
 * after elements
 *
 * Authors:	Stephen Hemminger <stephen@networkplumber.org>
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>
#include <malloc.h>
#include <inttypes.h>
#include <stdint.h>

#include "json_writer.h"

struct json_writer {
	FILE		*out;
	unsigned	depth;
	bool		pretty;
	char		sep;
};

static void jsonw_indent(json_writer_t *self)
{
	unsigned i;
	for (i = 0; i < self->depth; ++i)
		fputs("    ", self->out);
}

static void jsonw_eol(json_writer_t *self)
{
	if (!self->pretty)
		return;

	putc('\n', self->out);
	jsonw_indent(self);
}

static void jsonw_eor(json_writer_t *self)
{
	if (self->sep != '\0')
		putc(self->sep, self->out);
	self->sep = ',';
}

static void jsonw_puts(json_writer_t *self, const char *str)
{
	putc('"', self->out);
	for (; *str; ++str)
		switch (*str) {
		case '\t':
			fputs("\\t", self->out);
			break;
		case '\n':
			fputs("\\n", self->out);
			break;
		case '\r':
			fputs("\\r", self->out);
			break;
		case '\f':
			fputs("\\f", self->out);
			break;
		case '\b':
			fputs("\\b", self->out);
			break;
		case '\\':
			fputs("\\\\", self->out);
			break;
		case '"':
			fputs("\\\"", self->out);
			break;
		default:
			putc(*str, self->out);
		}
	putc('"', self->out);
}

json_writer_t *jsonw_new(FILE *f)
{
	json_writer_t *self = malloc(sizeof(*self));
	if (self) {
		self->out = f;
		self->depth = 0;
		self->pretty = false;
		self->sep = '\0';
	}
	return self;
}

void jsonw_destroy(json_writer_t **self_p)
{
	json_writer_t *self = *self_p;

	assert(self->depth == 0);
	fputs("\n", self->out);
	fflush(self->out);
	free(self);
	*self_p = NULL;
}

void jsonw_pretty(json_writer_t *self, bool on)
{
	self->pretty = on;
}

void jsonw_reset(json_writer_t *self)
{
	assert(self->depth == 0);
	self->sep = '\0';
}

static void jsonw_begin(json_writer_t *self, int c)
{
	jsonw_eor(self);
	putc(c, self->out);
	++self->depth;
	self->sep = '\0';
}

static void jsonw_end(json_writer_t *self, int c)
{
	assert(self->depth > 0);

	--self->depth;
	if (self->sep != '\0')
		jsonw_eol(self);
	putc(c, self->out);
	self->sep = ',';
}

void jsonw_name(json_writer_t *self, const char *name)
{
	jsonw_eor(self);
	jsonw_eol(self);
	self->sep = '\0';
	jsonw_puts(self, name);
	putc(':', self->out);
	if (self->pretty)
		putc(' ', self->out);
}

void jsonw_vprintf_enquote(json_writer_t *self, const char *fmt, va_list ap)
{
	jsonw_eor(self);
	putc('"', self->out);
	vfprintf(self->out, fmt, ap);
	putc('"', self->out);
}

void jsonw_printf(json_writer_t *self, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	jsonw_eor(self);
	vfprintf(self->out, fmt, ap);
	va_end(ap);
}

void jsonw_start_object(json_writer_t *self)
{
	jsonw_begin(self, '{');
}

void jsonw_end_object(json_writer_t *self)
{
	jsonw_end(self, '}');
}

void jsonw_start_array(json_writer_t *self)
{
	jsonw_begin(self, '[');
}

void jsonw_end_array(json_writer_t *self)
{
	jsonw_end(self, ']');
}

void jsonw_string(json_writer_t *self, const char *value)
{
	jsonw_eor(self);
	jsonw_puts(self, value);
}

void jsonw_bool(json_writer_t *self, bool val)
{
	jsonw_printf(self, "%s", val ? "true" : "false");
}

void jsonw_null(json_writer_t *self)
{
	jsonw_printf(self, "null");
}

void jsonw_float_fmt(json_writer_t *self, const char *fmt, double num)
{
	jsonw_printf(self, fmt, num);
}

void jsonw_float(json_writer_t *self, double num)
{
	jsonw_printf(self, "%g", num);
}

void jsonw_hu(json_writer_t *self, unsigned short num)
{
	jsonw_printf(self, "%hu", num);
}

void jsonw_uint(json_writer_t *self, uint64_t num)
{
	jsonw_printf(self, "%"PRIu64, num);
}

void jsonw_lluint(json_writer_t *self, unsigned long long int num)
{
	jsonw_printf(self, "%llu", num);
}

void jsonw_int(json_writer_t *self, int64_t num)
{
	jsonw_printf(self, "%"PRId64, num);
}

void jsonw_string_field(json_writer_t *self, const char *prop, const char *val)
{
	jsonw_name(self, prop);
	jsonw_string(self, val);
}

void jsonw_bool_field(json_writer_t *self, const char *prop, bool val)
{
	jsonw_name(self, prop);
	jsonw_bool(self, val);
}

void jsonw_float_field(json_writer_t *self, const char *prop, double val)
{
	jsonw_name(self, prop);
	jsonw_float(self, val);
}

void jsonw_float_field_fmt(json_writer_t *self,
			   const char *prop,
			   const char *fmt,
			   double val)
{
	jsonw_name(self, prop);
	jsonw_float_fmt(self, fmt, val);
}

void jsonw_uint_field(json_writer_t *self, const char *prop, uint64_t num)
{
	jsonw_name(self, prop);
	jsonw_uint(self, num);
}

void jsonw_hu_field(json_writer_t *self, const char *prop, unsigned short num)
{
	jsonw_name(self, prop);
	jsonw_hu(self, num);
}

void jsonw_lluint_field(json_writer_t *self,
			const char *prop,
			unsigned long long int num)
{
	jsonw_name(self, prop);
	jsonw_lluint(self, num);
}

void jsonw_int_field(json_writer_t *self, const char *prop, int64_t num)
{
	jsonw_name(self, prop);
	jsonw_int(self, num);
}

void jsonw_null_field(json_writer_t *self, const char *prop)
{
	jsonw_name(self, prop);
	jsonw_null(self);
}
