// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2017 National Instruments Corp.
 *
 * Author: Julia Cartwright <julia@ni.com>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/futex.h>

#include "event-parse.h"

#define ARRAY_SIZE(_a) (sizeof(_a) / sizeof((_a)[0]))

struct futex_args {
	unsigned long long	uaddr;
	unsigned long long	op;
	unsigned long long	val;
	unsigned long long	utime; /* or val2 */
	unsigned long long	uaddr2;
	unsigned long long	val3;
};

struct futex_op {
	const char	*name;
	const char	*fmt_val;
	const char	*fmt_utime;
	const char	*fmt_uaddr2;
	const char	*fmt_val3;
};

static const struct futex_op futex_op_tbl[] = {
	{            "FUTEX_WAIT", " val=0x%08llx", " utime=0x%08llx",               NULL,             NULL },
	{            "FUTEX_WAKE",     " val=%llu",              NULL,               NULL,             NULL },
	{              "FUTEX_FD",     " val=%llu",              NULL,               NULL,             NULL },
	{         "FUTEX_REQUEUE",     " val=%llu",      " val2=%llu", " uaddr2=0x%08llx",             NULL },
	{     "FUTEX_CMP_REQUEUE",     " val=%llu",      " val2=%llu", " uaddr2=0x%08llx", " val3=0x%08llx" },
	{         "FUTEX_WAKE_OP",     " val=%llu",      " val2=%llu", " uaddr2=0x%08llx", " val3=0x%08llx" },
	{         "FUTEX_LOCK_PI",            NULL, " utime=0x%08llx",               NULL,             NULL },
	{       "FUTEX_UNLOCK_PI",            NULL,              NULL,               NULL,             NULL },
	{      "FUTEX_TRYLOCK_PI",            NULL,              NULL,               NULL,             NULL },
	{     "FUTEX_WAIT_BITSET", " val=0x%08llx", " utime=0x%08llx",               NULL, " val3=0x%08llx" },
	{     "FUTEX_WAKE_BITSET",     " val=%llu",              NULL,               NULL, " val3=0x%08llx" },
	{ "FUTEX_WAIT_REQUEUE_PI", " val=0x%08llx", " utime=0x%08llx", " uaddr2=0x%08llx", " val3=0x%08llx" },
	{  "FUTEX_CMP_REQUEUE_PI",     " val=%llu",      " val2=%llu", " uaddr2=0x%08llx", " val3=0x%08llx" },
};


static void futex_print(struct trace_seq *s, const struct futex_args *args,
			const struct futex_op *fop)
{
	trace_seq_printf(s, " uaddr=0x%08llx", args->uaddr);

	if (fop->fmt_val)
		trace_seq_printf(s, fop->fmt_val, args->val);

	if (fop->fmt_utime)
		trace_seq_printf(s,fop->fmt_utime, args->utime);

	if (fop->fmt_uaddr2)
		trace_seq_printf(s, fop->fmt_uaddr2, args->uaddr2);

	if (fop->fmt_val3)
		trace_seq_printf(s, fop->fmt_val3, args->val3);
}

static int futex_handler(struct trace_seq *s, struct tep_record *record,
			 struct tep_event *event, void *context)
{
	const struct futex_op *fop;
	struct futex_args args;
	unsigned long long cmd;

	if (tep_get_field_val(s, event, "uaddr", record, &args.uaddr, 1))
		return 1;

	if (tep_get_field_val(s, event, "op", record, &args.op, 1))
		return 1;

	if (tep_get_field_val(s, event, "val", record, &args.val, 1))
		return 1;

	if (tep_get_field_val(s, event, "utime", record, &args.utime, 1))
		return 1;

	if (tep_get_field_val(s, event, "uaddr2", record, &args.uaddr2, 1))
		return 1;

	if (tep_get_field_val(s, event, "val3", record, &args.val3, 1))
		return 1;

	cmd = args.op & FUTEX_CMD_MASK;
	if (cmd >= ARRAY_SIZE(futex_op_tbl))
		return 1;

	fop = &futex_op_tbl[cmd];

	trace_seq_printf(s, "op=%s", fop->name);

	if (args.op & FUTEX_PRIVATE_FLAG)
		trace_seq_puts(s, "|FUTEX_PRIVATE_FLAG");

	if (args.op & FUTEX_CLOCK_REALTIME)
		trace_seq_puts(s, "|FUTEX_CLOCK_REALTIME");

	futex_print(s, &args, fop);
	return 0;
}

int TEP_PLUGIN_LOADER(struct tep_handle *tep)
{
	tep_register_event_handler(tep, -1, "syscalls", "sys_enter_futex",
				   futex_handler, NULL);
	return 0;
}

void TEP_PLUGIN_UNLOADER(struct tep_handle *tep)
{
	tep_unregister_event_handler(tep, -1, "syscalls", "sys_enter_futex",
				     futex_handler, NULL);
}
