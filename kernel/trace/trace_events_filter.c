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

static int filter_pred_none(struct filter_pred *pred, void *event)
{
	return 0;
}

/* return 1 if event matches, 0 otherwise (discard) */
int filter_match_preds(struct ftrace_event_call *call, void *rec)
{
	int i, matched, and_failed = 0;
	struct filter_pred *pred;

	for (i = 0; i < call->n_preds; i++) {
		pred = call->preds[i];
		if (and_failed && !pred->or)
			continue;
		matched = pred->fn(pred, rec);
		if (!matched && !pred->or) {
			and_failed = 1;
			continue;
		} else if (matched && pred->or)
			return 1;
	}

	if (and_failed)
		return 0;

	return 1;
}
EXPORT_SYMBOL_GPL(filter_match_preds);

void filter_print_preds(struct filter_pred **preds, int n_preds,
			struct trace_seq *s)
{
	char *field_name;
	struct filter_pred *pred;
	int i;

	if (!n_preds) {
		trace_seq_printf(s, "none\n");
		return;
	}

	for (i = 0; i < n_preds; i++) {
		pred = preds[i];
		field_name = pred->field_name;
		if (i)
			trace_seq_printf(s, pred->or ? "|| " : "&& ");
		trace_seq_printf(s, "%s ", field_name);
		trace_seq_printf(s, pred->not ? "!= " : "== ");
		if (pred->str_len)
			trace_seq_printf(s, "%s\n", pred->str_val);
		else
			trace_seq_printf(s, "%llu\n", pred->val);
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
	kfree(pred);
}

static void filter_clear_pred(struct filter_pred *pred)
{
	kfree(pred->field_name);
	pred->field_name = NULL;
	pred->str_len = 0;
}

static int filter_set_pred(struct filter_pred *dest,
			   struct filter_pred *src,
			   filter_pred_fn_t fn)
{
	*dest = *src;
	dest->field_name = kstrdup(src->field_name, GFP_KERNEL);
	if (!dest->field_name)
		return -ENOMEM;
	dest->fn = fn;

	return 0;
}

void filter_disable_preds(struct ftrace_event_call *call)
{
	int i;

	call->n_preds = 0;

	for (i = 0; i < MAX_FILTER_PRED; i++)
		call->preds[i]->fn = filter_pred_none;
}

int init_preds(struct ftrace_event_call *call)
{
	struct filter_pred *pred;
	int i;

	call->n_preds = 0;

	call->preds = kzalloc(MAX_FILTER_PRED * sizeof(pred), GFP_KERNEL);
	if (!call->preds)
		return -ENOMEM;

	for (i = 0; i < MAX_FILTER_PRED; i++) {
		pred = kzalloc(sizeof(*pred), GFP_KERNEL);
		if (!pred)
			goto oom;
		pred->fn = filter_pred_none;
		call->preds[i] = pred;
	}

	return 0;

oom:
	for (i = 0; i < MAX_FILTER_PRED; i++) {
		if (call->preds[i])
			filter_free_pred(call->preds[i]);
	}
	kfree(call->preds);
	call->preds = NULL;

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(init_preds);

void filter_free_subsystem_preds(struct event_subsystem *system)
{
	struct ftrace_event_call *call;
	int i;

	if (system->n_preds) {
		for (i = 0; i < system->n_preds; i++)
			filter_free_pred(system->preds[i]);
		kfree(system->preds);
		system->preds = NULL;
		system->n_preds = 0;
	}

	list_for_each_entry(call, &ftrace_events, list) {
		if (!call->define_fields)
			continue;

		if (!strcmp(call->system, system->name))
			filter_disable_preds(call);
	}
}

static int __filter_add_pred(struct ftrace_event_call *call,
			     struct filter_pred *pred,
			     filter_pred_fn_t fn)
{
	int idx, err;

	if (call->n_preds && !pred->compound)
		filter_disable_preds(call);

	if (call->n_preds == MAX_FILTER_PRED)
		return -ENOSPC;

	idx = call->n_preds;
	filter_clear_pred(call->preds[idx]);
	err = filter_set_pred(call->preds[idx], pred, fn);
	if (err)
		return err;

	call->n_preds++;

	return 0;
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
	filter_pred_fn_t fn;

	field = find_event_field(call, pred->field_name);
	if (!field)
		return -EINVAL;

	pred->fn = filter_pred_none;
	pred->offset = field->offset;

	if (is_string_field(field->type)) {
		if (!pred->str_len)
			return -EINVAL;
		fn = filter_pred_string;
		pred->str_len = field->size;
		return __filter_add_pred(call, pred, fn);
	} else {
		if (pred->str_len)
			return -EINVAL;
	}

	switch (field->size) {
	case 8:
		fn = filter_pred_64;
		break;
	case 4:
		fn = filter_pred_32;
		break;
	case 2:
		fn = filter_pred_16;
		break;
	case 1:
		fn = filter_pred_8;
		break;
	default:
		return -EINVAL;
	}

	return __filter_add_pred(call, pred, fn);
}

int filter_add_subsystem_pred(struct event_subsystem *system,
			      struct filter_pred *pred)
{
	struct ftrace_event_call *call;

	if (system->n_preds && !pred->compound)
		filter_free_subsystem_preds(system);

	if (!system->n_preds) {
		system->preds = kzalloc(MAX_FILTER_PRED * sizeof(pred),
					GFP_KERNEL);
		if (!system->preds)
			return -ENOMEM;
	}

	if (system->n_preds == MAX_FILTER_PRED)
		return -ENOSPC;

	system->preds[system->n_preds] = pred;

	list_for_each_entry(call, &ftrace_events, list) {
		int err;

		if (!call->define_fields)
			continue;

		if (strcmp(call->system, system->name))
			continue;

		if (!find_event_field(call, pred->field_name))
			continue;

		err = filter_add_pred(call, pred);
		if (err == -ENOMEM) {
			system->preds[system->n_preds] = NULL;
			return err;
		}
	}

	system->n_preds++;

	return 0;
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

	if (!val_str || !strlen(val_str)
	    || strlen(val_str) >= MAX_FILTER_STR_VAL) {
		pred->field_name = NULL;
		return -EINVAL;
	}

	pred->field_name = kstrdup(pred->field_name, GFP_KERNEL);
	if (!pred->field_name)
		return -ENOMEM;

	pred->str_len = 0;
	pred->val = simple_strtoull(val_str, &tmp, 0);
	if (tmp == val_str) {
		strncpy(pred->str_val, val_str, MAX_FILTER_STR_VAL);
		pred->str_len = strlen(val_str);
		pred->str_val[pred->str_len] = '\0';
	} else if (*tmp != '\0')
		return -EINVAL;

	return 0;
}


