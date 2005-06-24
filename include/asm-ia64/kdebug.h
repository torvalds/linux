#ifndef _IA64_KDEBUG_H
#define _IA64_KDEBUG_H 1
/*
 * include/asm-ia64/kdebug.h
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
 * Copyright (C) Intel Corporation, 2005
 *
 * 2005-Apr     Rusty Lynch <rusty.lynch@intel.com> and Anil S Keshavamurthy
 *              <anil.s.keshavamurthy@intel.com> adopted from
 *              include/asm-x86_64/kdebug.h
 */
#include <linux/notifier.h>

struct pt_regs;

struct die_args {
	struct pt_regs *regs;
	const char *str;
	long err;
	int trapnr;
	int signr;
};

int register_die_notifier(struct notifier_block *nb);
extern struct notifier_block *ia64die_chain;

enum die_val {
	DIE_BREAK = 1,
	DIE_SS,
	DIE_PAGE_FAULT,
};

static inline int notify_die(enum die_val val, char *str, struct pt_regs *regs,
			     long err, int trap, int sig)
{
	struct die_args args = {
		.regs   = regs,
		.str    = str,
		.err    = err,
		.trapnr = trap,
		.signr  = sig
	};

	return notifier_call_chain(&ia64die_chain, val, &args);
}

#endif
