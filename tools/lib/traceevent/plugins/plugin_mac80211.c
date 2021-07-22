// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2009 Johannes Berg <johannes@sipsolutions.net>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event-parse.h"
#include "trace-seq.h"

#define INDENT 65

static void print_string(struct trace_seq *s, struct tep_event *event,
			 const char *name, const void *data)
{
	struct tep_format_field *f = tep_find_field(event, name);
	int offset;
	int length;

	if (!f) {
		trace_seq_printf(s, "NOTFOUND:%s", name);
		return;
	}

	offset = f->offset;
	length = f->size;

	if (!strncmp(f->type, "__data_loc", 10)) {
		unsigned long long v;
		if (tep_read_number_field(f, data, &v)) {
			trace_seq_printf(s, "invalid_data_loc");
			return;
		}
		offset = v & 0xffff;
		length = v >> 16;
	}

	trace_seq_printf(s, "%.*s", length, (char *)data + offset);
}

#define SF(fn)	tep_print_num_field(s, fn ":%d", event, fn, record, 0)
#define SFX(fn)	tep_print_num_field(s, fn ":%#x", event, fn, record, 0)
#define SP()	trace_seq_putc(s, ' ')

static int drv_bss_info_changed(struct trace_seq *s,
				struct tep_record *record,
				struct tep_event *event, void *context)
{
	void *data = record->data;

	print_string(s, event, "wiphy_name", data);
	trace_seq_printf(s, " vif:");
	print_string(s, event, "vif_name", data);
	tep_print_num_field(s, "(%d)", event, "vif_type", record, 1);

	trace_seq_printf(s, "\n%*s", INDENT, "");
	SF("assoc"); SP();
	SF("aid"); SP();
	SF("cts"); SP();
	SF("shortpre"); SP();
	SF("shortslot"); SP();
	SF("dtimper"); SP();
	trace_seq_printf(s, "\n%*s", INDENT, "");
	SF("bcnint"); SP();
	SFX("assoc_cap"); SP();
	SFX("basic_rates"); SP();
	SF("enable_beacon");
	trace_seq_printf(s, "\n%*s", INDENT, "");
	SF("ht_operation_mode");

	return 0;
}

int TEP_PLUGIN_LOADER(struct tep_handle *tep)
{
	tep_register_event_handler(tep, -1, "mac80211",
				   "drv_bss_info_changed",
				   drv_bss_info_changed, NULL);
	return 0;
}

void TEP_PLUGIN_UNLOADER(struct tep_handle *tep)
{
	tep_unregister_event_handler(tep, -1, "mac80211",
				     "drv_bss_info_changed",
				     drv_bss_info_changed, NULL);
}
