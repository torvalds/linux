/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_MODULE_H_
#define	_LINUX_MODULE_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/module.h>

#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/kmod.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/export.h>

#define MODULE_AUTHOR(name)
#define MODULE_DESCRIPTION(name)
#define MODULE_LICENSE(name)
#define	MODULE_INFO(tag, info)
#define	MODULE_FIRMWARE(firmware)

#define	THIS_MODULE	((struct module *)0)

#define	__MODULE_STRING(x) __stringify(x)

/* OFED pre-module initialization */
#define	SI_SUB_OFED_PREINIT	(SI_SUB_ROOT_CONF - 2)
/* OFED default module initialization */
#define	SI_SUB_OFED_MODINIT	(SI_SUB_ROOT_CONF - 1)

#include <sys/linker.h>

static inline void
_module_run(void *arg)
{
	void (*fn)(void);
#ifdef OFED_DEBUG_INIT
	char name[1024];
	caddr_t pc;
	long offset;

	pc = (caddr_t)arg;
	if (linker_search_symbol_name(pc, name, sizeof(name), &offset) != 0)
		printf("Running ??? (%p)\n", pc);
	else
		printf("Running %s (%p)\n", name, pc);
#endif
	fn = arg;
	fn();
}

#define	module_init(fn)							\
	SYSINIT(fn, SI_SUB_OFED_MODINIT, SI_ORDER_FIRST, _module_run, (fn))

#define	module_exit(fn)						\
	SYSUNINIT(fn, SI_SUB_OFED_MODINIT, SI_ORDER_SECOND, _module_run, (fn))

/*
 * The following two macros are a workaround for not having a module
 * load and unload order resolver:
 */
#define	module_init_order(fn, order)					\
	SYSINIT(fn, SI_SUB_OFED_MODINIT, (order), _module_run, (fn))

#define	module_exit_order(fn, order)				\
	SYSUNINIT(fn, SI_SUB_OFED_MODINIT, (order), _module_run, (fn))

#define	module_get(module)
#define	module_put(module)
#define	try_module_get(module)	1

#define	postcore_initcall(fn)	module_init(fn)

#endif	/* _LINUX_MODULE_H_ */
