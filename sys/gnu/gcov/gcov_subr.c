/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019, Matthew Macy <mmacy@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/sbuf.h>

#include <sys/queue.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <gnu/gcov/gcov.h>
#include <sys/queue.h>
#include "linker_if.h"


static void gcov_invoke_ctors(void);
static int gcov_ctors_done;
int gcov_events_enabled;

static int
gcov_stats_reset_sysctl(SYSCTL_HANDLER_ARGS)
{
	int error, v;

	v = 0;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error)
		return (error);
	if (req->newptr == NULL)
		return (error);
	if (v == 0)
		return (0);
	gcov_stats_reset();

	return (0);
}

static int
gcov_stats_enable_sysctl(SYSCTL_HANDLER_ARGS)
{
	int error, v;

	v = gcov_events_enabled;
	error = sysctl_handle_int(oidp, &v, v, req);
	if (error)
		return (error);
	if (req->newptr == NULL)
		return (error);
	if (v == gcov_events_enabled)
		return (0);
	//gcov_events_reset();
	gcov_events_enabled = !!v;
	if (!gcov_ctors_done)
		gcov_invoke_ctors();
	if (gcov_events_enabled)
		gcov_enable_events();

	return (0);
}

int
within_module(vm_offset_t addr, module_t mod)
{
	linker_file_t link_info;
	vm_offset_t mod_addr;
	size_t mod_size;

	link_info = module_file(mod);
	mod_addr = (vm_offset_t)link_info->address;
	mod_size = link_info->size;
	if (addr >= mod_addr && addr < mod_addr + mod_size)
		return (1);
	return (0);
}



#define GCOV_PREFIX "_GLOBAL__sub_I_65535_0_"

static int
gcov_invoke_ctor(const char *name, void *arg)
{
	void (*ctor)(void);
	c_linker_sym_t sym;
	linker_symval_t symval;
	linker_file_t lf;

	if (strstr(name, GCOV_PREFIX) == NULL)
		return (0);
	lf = arg;
	LINKER_LOOKUP_SYMBOL(lf, name, &sym);
	LINKER_SYMBOL_VALUES(lf, sym, &symval);
	ctor = (void *)symval.value;
	ctor();
	return (0);
}

static int
gcov_invoke_lf_ctors(linker_file_t lf, void *arg __unused)
{

	printf("%s processing file: %s\n", __func__, lf->filename);
	LINKER_EACH_FUNCTION_NAME(lf, gcov_invoke_ctor, lf);
	return (0);
}

static void
gcov_invoke_ctors(void)
{

	gcov_fs_init();

	linker_file_foreach(gcov_invoke_lf_ctors, NULL);
	gcov_ctors_done = 1;
}

static int
gcov_init(void *arg __unused)
{
	EVENTHANDLER_REGISTER(module_unload, gcov_module_unload, NULL, 0);
	gcov_enable_events();
	return (0);
}

SYSINIT(gcov_init, SI_SUB_EVENTHANDLER, SI_ORDER_ANY, gcov_init, NULL);

static SYSCTL_NODE(_debug, OID_AUTO, gcov, CTLFLAG_RD, NULL,
    "gcov code coverage");
SYSCTL_PROC(_debug_gcov, OID_AUTO, reset, CTLTYPE_INT | CTLFLAG_RW,
    NULL, 0, gcov_stats_reset_sysctl, "I", "Reset all profiling counts");
SYSCTL_PROC(_debug_gcov, OID_AUTO, enable, CTLTYPE_INT | CTLFLAG_RW,
    NULL, 0, gcov_stats_enable_sysctl, "I", "Enable code coverage");
