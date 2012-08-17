/*
 * Copyright 2012  Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/atomic.h>
#include <asm/barrier.h>
#include "internal.h"

void notrace pstore_ftrace_call(unsigned long ip, unsigned long parent_ip)
{
	struct pstore_ftrace_record rec = {};

	if (unlikely(oops_in_progress))
		return;

	rec.ip = ip;
	rec.parent_ip = parent_ip;
	pstore_ftrace_encode_cpu(&rec, raw_smp_processor_id());
	psinfo->write_buf(PSTORE_TYPE_FTRACE, 0, NULL, 0, (void *)&rec,
			  sizeof(rec), psinfo);
}
