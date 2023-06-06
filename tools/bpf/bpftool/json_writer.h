/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Simple streaming JSON writer
 *
 * This takes care of the annoying bits of JSON syntax like the commas
 * after elements
 *
 * Authors:	Stephen Hemminger <stephen@networkplumber.org>
 */

#ifndef _JSON_WRITER_H_
#define _JSON_WRITER_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <linux/compiler.h>

/* Opaque class structure */
typedef struct json_writer json_writer_t;

/* Create a new JSON stream */
json_writer_t *jsonw_new(FILE *f);
/* End output to JSON stream */
void jsonw_destroy(json_writer_t **self_p);

/* Cause output to have pretty whitespace */
void jsonw_pretty(json_writer_t *self, bool on);

/* Reset separator to create new JSON */
void jsonw_reset(json_writer_t *self);

/* Add property name */
void jsonw_name(json_writer_t *self, const char *name);

/* Add value  */
void __printf(2, 0) jsonw_vprintf_enquote(json_writer_t *self, const char *fmt,
					  va_list ap);
void __printf(2, 3) jsonw_printf(json_writer_t *self, const char *fmt, ...);
void jsonw_string(json_writer_t *self, const char *value);
void jsonw_bool(json_writer_t *self, bool value);
void jsonw_float(json_writer_t *self, double number);
void jsonw_float_fmt(json_writer_t *self, const char *fmt, double num);
void jsonw_uint(json_writer_t *self, uint64_t number);
void jsonw_hu(json_writer_t *self, unsigned short number);
void jsonw_int(json_writer_t *self, int64_t number);
void jsonw_null(json_writer_t *self);
void jsonw_lluint(json_writer_t *self, unsigned long long int num);

/* Useful Combinations of name and value */
void jsonw_string_field(json_writer_t *self, const char *prop, const char *val);
void jsonw_bool_field(json_writer_t *self, const char *prop, bool value);
void jsonw_float_field(json_writer_t *self, const char *prop, double num);
void jsonw_uint_field(json_writer_t *self, const char *prop, uint64_t num);
void jsonw_hu_field(json_writer_t *self, const char *prop, unsigned short num);
void jsonw_int_field(json_writer_t *self, const char *prop, int64_t num);
void jsonw_null_field(json_writer_t *self, const char *prop);
void jsonw_lluint_field(json_writer_t *self, const char *prop,
			unsigned long long int num);
void jsonw_float_field_fmt(json_writer_t *self, const char *prop,
			   const char *fmt, double val);

/* Collections */
void jsonw_start_object(json_writer_t *self);
void jsonw_end_object(json_writer_t *self);

void jsonw_start_array(json_writer_t *self);
void jsonw_end_array(json_writer_t *self);

/* Override default exception handling */
typedef void (jsonw_err_handler_fn)(const char *);

#endif /* _JSON_WRITER_H_ */
