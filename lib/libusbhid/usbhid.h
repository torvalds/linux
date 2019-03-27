/*	$NetBSD: usb.h,v 1.8 2000/08/13 22:22:02 augustss Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1999 Lennart Augustsson <augustss@netbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#include <stdint.h>

typedef struct report_desc *report_desc_t;

typedef struct hid_data *hid_data_t;

typedef enum hid_kind {
	hid_input, hid_output, hid_feature, hid_collection, hid_endcollection
} hid_kind_t;

typedef struct hid_item {
	/* Global */
	uint32_t _usage_page;
	int32_t logical_minimum;
	int32_t logical_maximum;
	int32_t physical_minimum;
	int32_t physical_maximum;
	int32_t unit_exponent;
	int32_t unit;
	int32_t report_size;
	int32_t report_ID;
#define NO_REPORT_ID 0
	int32_t report_count;
	/* Local */
	uint32_t usage;
	int32_t usage_minimum;
	int32_t usage_maximum;
	int32_t designator_index;
	int32_t designator_minimum;
	int32_t designator_maximum;
	int32_t string_index;
	int32_t string_minimum;
	int32_t string_maximum;
	int32_t set_delimiter;
	/* Misc */
	int32_t collection;
	int	collevel;
	enum hid_kind kind;
	uint32_t flags;
	/* Location */
	uint32_t pos;
	/* unused */
	struct hid_item *next;
} hid_item_t;

#define HID_PAGE(u) (((u) >> 16) & 0xffff)
#define HID_USAGE(u) ((u) & 0xffff)
#define HID_HAS_GET_SET_REPORT 1

__BEGIN_DECLS

/* Obtaining a report descriptor, descr.c: */
report_desc_t hid_get_report_desc(int file);
report_desc_t hid_use_report_desc(unsigned char *data, unsigned int size);
void hid_dispose_report_desc(report_desc_t);
int hid_get_report_id(int file);
int hid_set_immed(int fd, int enable);

/* Parsing of a HID report descriptor, parse.c: */
hid_data_t hid_start_parse(report_desc_t d, int kindset, int id);
void hid_end_parse(hid_data_t s);
int hid_get_item(hid_data_t s, hid_item_t *h);
int hid_report_size(report_desc_t d, enum hid_kind k, int id);
int hid_locate(report_desc_t d, unsigned int usage, enum hid_kind k,
    hid_item_t *h, int id);

/* Conversion to/from usage names, usage.c: */
const char *hid_usage_page(int i);
const char *hid_usage_in_page(unsigned int u);
void hid_init(const char *file);
int hid_parse_usage_in_page(const char *name);
int hid_parse_usage_page(const char *name);

/* Extracting/insertion of data, data.c: */
int32_t hid_get_data(const void *p, const hid_item_t *h);
void hid_set_data(void *p, const hid_item_t *h, int32_t data);
int hid_get_report(int fd, enum hid_kind k,
    unsigned char *data, unsigned int size);
int hid_set_report(int fd, enum hid_kind k,
    unsigned char *data, unsigned int size);

__END_DECLS
