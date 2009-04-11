/*
 * trace_events_filter - generic event filtering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2009 Tom Zanussi <tzanussi@gmail.com>
 */

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ctype.h>

#include "trace.h"
#include "trace_output.h"

static int filter_pred_64(struct filter_pred *pred, void *event)
{
	u64 *addr = (u64 *)(event + pred->offset);
	u64 val = (u64)pred->val;
	int match;

	match = (val == *addr) ^ pred->not;

	return match;
}

static int filter_pred_32(struct filter_pred *pred, void *event)
{
	u32 *addr = (u32 *)(event + pred->offset);
	u32 val = (u32)pred->val;
	int match;

	match = (val == *addr) ^ pred->not;

	return match;
}

static int filter_pred_16(struct filter_pred *pred, void *event)
{
	u16 *addr = (u16 *)(event + pred->offset);
	u16 val = (u16)pred->val;
	int match;

	match = (val == *addr) ^ pred->not;

	return match;
}

static int filter_pred_8(struct filter_pred *pred, void *event)
{
	u8 *addr = (u8 *)(event + pred->offset);
	u8 val = (u8)pred->val;
	int match;

	match = (val == *addr) ^ pred->not;

	return match;
}

static int filter_pred_string(struct filter_pred *pred, void *event)
{
	char *addr = (char *)(event + pred->offset);
	int cmp, match;

	cmp = strncmp(addr, pred->str_val, pred->str_len);

	match = (!cmp) ^ pred->not;

	return match;
}

/* return 1 if event matches, 0 otherwise (discard) */
int filter_match_preds(struct ftrace_event_call *call, void *rec)
{
	int i, matched, and_failed = 0;
	struct filter_pred *pred;

	for (i = 0; i < MAX_FILTER_PRED; i++) {
		if (call->preds[i]) {
			pred = call->preds[i];
			if (and_failed && !pred->or)
				continue;
			matched = pred->fn(pred, rec);
			if (!matched && !pred->or) {
				and_failed = 1;
				continue;
			} else if (matched && pred->or)
				return 1;
		} else
			break;
	}

	if (and_failed)
		return 0;

	return 1;
}

void filter_print_preds(struct filter_pred **preds, struct trace_seq *s)
{
	char *field_name;
	struct filter_pred *pred;
	int i;

	if (!preds) {
		trace_seq_printf(s, "none\n");
		return;
	}

	for (i = 0; i < MAX_FILTER_PRED; i++) {
		if (preds[i]) {
			pred = preds[i];
			field_name = pred->field_name;
			if (i)
				trace_seq_printf(s, pred->or ? "|| " : "&& ");
			trace_seq_printf(s, "%s ", field_name);
			trace_seq_printf(s, pred->not ? "!= " : "== ");
			if (pred->str_val)
				trace_seq_printf(s, "%s\n", pred->str_val);
			else
				trace_seq_printf(s, "%llu\n", pred->val);
		} else
			break;
	}
}

static struct ftrace_event_field *
find_event_field(struct ftrace_event_call *call, char *name)
{
	struct ftrace_event_field *field;

	list_for_each_entry(field, &call->fields, link) {
		if (!strcmp(field->name, name))
			return field;
	}

	return NULL;
}

void filter_free_pred(struct filter_pred *pred)
{
	if (!pred)
		return;

	kfree(pred->field_name);
	kfree(pred->str_val);
	kfree(pred);
}

void filter_free_preds(struct ftrace_event_call *call)
{
	int i;

	if (call->preds) {
		for (i = 0; i < MAX_FILTER_PRED; i++)
			filter_free_pred(call->preds[i]);
		kfree(call->preds);
		call->preds = NULL;
	}
}

void filter_free_subsystem_preds(struct event_subsystem *system)
{
	struct ftrace_event_call *call = __start_ftrace_events;
	int i;

	if (system->preds) {
		for (i = 0; i < MAX_FILTER_PRED; i++)
			filter_free_pred(system->preds[i]);
		kfree(system->preds);
		system->preds = NULL;
	}

	events_for_each(call) {
		if (!call->name || !call->regfunc)
			continue;

		if (!strcmp(call->system, system->name))
			filter_free_preds(call);
	}
}

static int __filter_add_pred(struct ftrace_event_call *call,
			     struct filter_pred *pred)
{
	int i;

	if (call->preds && !pred->compound)
		filter_free_preds(call);

	if (!call->preds) {
		call->preds = kzalloc(MAX_FILTER_PRED * sizeof(pred),
				      GFP_KERNEL);
		if (!call->preds)
			return -ENOMEM;
	}

	for (i = 0; i < MAX_FILTER_PRED; i++) {
		if (!call->preds[i]) {
			call->preds[i] = pred;
			return 0;
		}
	}

	return -ENOMEM;
}

static int is_string_field(const char *type)
{
	if (strchr(type, '[') && strstr(type, "char"))
		return 1;

	return 0;
}

int filter_add_pred(struct ftrace_event_call *call, struct filter_pred *pred)
{
	struct ftrace_event_field *field;

	field = find_event_field(call, pred->field_name);
	if (!field)
		return -EINVAL;

	pred->offset = field->offset;

	if (is_string_field(field->type)) {
		if (!pred->str_val)
			return -EINVAL;
		pred->fn = filter_pred_string;
		pred->str_len = field->size;
		return __filter_add_pred(call, pred);
	} else {
		if (pred->str_val)
			return -EINVAL;
	}

	switch (field->size) {
	case 8:
		pred->fn = filter_pred_64;
		break;
	case 4:
		pred->fn = filter_pred_32;
		break;
	case 2:
		pred->fn = filter_pred_16;
		break;
	case 1:
		pred->fn = filter_pred_8;
		break;
	default:
		return -EINVAL;
	}

	return __filter_add_pred(call, pred);
}

static struct filter_pred *copy_pred(struct filter_pred *pred)
{
	struct filter_pred *new_pred = kmalloc(sizeof(*pred), GFP_KERNEL);
	if (!new_pred)
		return NULL;

	memcpy(new_pred, pred, sizeof(*pred));

	if (pred->field_name) {
		new_pred->field_name = kstrdup(pred->field_name, GFP_KERNEL);
		if (!new_pred->field_name) {
			kfree(new_pred);
			return NULL;
		}
	}

	if (pred->str_val) {
		new_pred->str_val = kstrdup(pred->str_val, GFP_KERNEL);
		if (!new_pred->str_val) {
			filter_free_pred(new_pred);
			return NULL;
		}
	}

	return new_pred;
}

int filter_add_subsystem_pred(struct event_subsystem *system,
			      struct filter_pred *pred)
{
	struct ftrace_event_call *call = __start_ftrace_events;
	struct filter_pred *event_pred;
	int i;

	if (system->preds && !pred->compound)
		filter_free_subsystem_preds(system);

	if (!system->preds) {
		system->preds = kzalloc(MAX_FILTER_PRED * sizeof(pred),
					GFP_KERNEL);
		if (!system->preds)
			return -ENOMEM;
	}

	for (i = 0; i < MAX_FILTER_PRED; i++) {
		if (!system->preds[i]) {
			system->preds[i] = pred;
			break;
		}
	}

	if (i == MAX_FILTER_PRED)
		return -EINVAL;

	events_for_each(call) {
		int err;

		if (!call->name || !call->regfunc)
			continue;

		if (strcmp(call->system, system->name))
			continue;

		if (!find_event_field(call, pred->field_name))
			continue;

		event_pred = copy_pred(pred);
		if (!event_pred)
			goto oom;

		err = filter_add_pred(call, event_pred);
		if (err)
			filter_free_pred(event_pred);
		if (err == -ENOMEM)
			goto oom;
	}

	return 0;

oom:
	system->preds[i] = NULL;
	return -ENOMEM;
}

int filter_parse(char **pbuf, struct filter_pred *pred)
{
	char *tmp, *tok, *val_str = NULL;
	int tok_n = 0;

	/* field ==/!= number, or/and field ==/!= number, number */
	while ((tok = strsep(pbuf, " \n"))) {
		if (tok_n == 0) {
			if (!strcmp(tok, "0")) {
				pred->clear = 1;
				return 0;
			} else if (!strcmp(tok, "&&")) {
				pred->or = 0;
				pred->compound = 1;
			} else if (!strcmp(tok, "||")) {
				pred->or = 1;
				pred->compound = 1;
			} else
				pred->field_name = tok;
			tok_n = 1;
			continue;
		}
		if (tok_n == 1) {
			if (!pred->field_name)
				pred->field_name = tok;
			else if (!strcmp(tok, "!="))
				pred->not = 1;
			else if (!strcmp(tok, "=="))
				pred->not = 0;
			else {
				pred->field_name = NULL;
				return -EINVAL;
			}
			tok_n = 2;
			continue;
		}
		if (tok_n == 2) {
			if (pred->compound) {
				if (!strcmp(tok, "!="))
					pred->not = 1;
				else if (!strcmp(tok, "=="))
					pred->not = 0;
				else {
					pred->field_name = NULL;
					return -EINVAL;
				}
			} else {
				val_str = tok;
				break; /* done */
			}
			tok_n = 3;
			continue;
		}
		if (tok_n == 3) {
			val_str = tok;
			break; /* done */
		}
	}

	if (!val_str) {
		pred->field_name = NULL;
		return -EINVAL;
	}

	pred->field_name = kstrdup(pred->field_name, GFP_KERNEL);
	if (!pred->field_name)
		return -ENOMEM;

	pred->val = simple_strtoull(val_str, &tmp, 10);
	if (tmp == val_str) {
		pred->str_val = kstrdup(val_str, GFP_KERNEL);
		if (!pred->str_val)
			return -ENOMEM;
	}

	return 0;
}


