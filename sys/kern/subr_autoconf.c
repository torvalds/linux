/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)subr_autoconf.c	8.1 (Berkeley) 6/10/93
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/systm.h>

/*
 * Autoconfiguration subroutines.
 */

/*
 * "Interrupt driven config" functions.
 */
static TAILQ_HEAD(, intr_config_hook) intr_config_hook_list =
	TAILQ_HEAD_INITIALIZER(intr_config_hook_list);
static struct intr_config_hook *next_to_notify;
static struct mtx intr_config_hook_lock;
MTX_SYSINIT(intr_config_hook, &intr_config_hook_lock, "intr config", MTX_DEF);

/* ARGSUSED */
static void run_interrupt_driven_config_hooks(void);

/*
 * Private data and a shim function for implementing config_interhook_oneshot().
 */
struct oneshot_config_hook {
	struct intr_config_hook 
			och_hook;		/* Must be first */
	ich_func_t	och_func;
	void		*och_arg;
};

static void
config_intrhook_oneshot_func(void *arg)
{
	struct oneshot_config_hook *ohook;

	ohook = arg;
	ohook->och_func(ohook->och_arg);
	config_intrhook_disestablish(&ohook->och_hook);
	free(ohook, M_DEVBUF);
}

/*
 * If we wait too long for an interrupt-driven config hook to return, print
 * a diagnostic.
 */
#define	WARNING_INTERVAL_SECS	60
static void
run_interrupt_driven_config_hooks_warning(int warned)
{
	struct intr_config_hook *hook_entry;
	char namebuf[64];
	long offset;

	if (warned < 6) {
		printf("run_interrupt_driven_hooks: still waiting after %d "
		    "seconds for", warned * WARNING_INTERVAL_SECS);
		TAILQ_FOREACH(hook_entry, &intr_config_hook_list, ich_links) {
			if (linker_search_symbol_name(
			    (caddr_t)hook_entry->ich_func, namebuf,
			    sizeof(namebuf), &offset) == 0)
				printf(" %s", namebuf);
			else
				printf(" %p", hook_entry->ich_func);
		}
		printf("\n");
	}
	KASSERT(warned < 6,
	    ("run_interrupt_driven_config_hooks: waited too long"));
}

static void
run_interrupt_driven_config_hooks()
{
	static int running;
	struct intr_config_hook *hook_entry;

	mtx_lock(&intr_config_hook_lock);

	/*
	 * If hook processing is already active, any newly
	 * registered hooks will eventually be notified.
	 * Let the currently running session issue these
	 * notifications.
	 */
	if (running != 0) {
		mtx_unlock(&intr_config_hook_lock);
		return;
	}
	running = 1;

	while (next_to_notify != NULL) {
		hook_entry = next_to_notify;
		next_to_notify = TAILQ_NEXT(hook_entry, ich_links);
		mtx_unlock(&intr_config_hook_lock);
		(*hook_entry->ich_func)(hook_entry->ich_arg);
		mtx_lock(&intr_config_hook_lock);
	}

	running = 0;
	mtx_unlock(&intr_config_hook_lock);
}

static void
boot_run_interrupt_driven_config_hooks(void *dummy)
{
	int warned;

	run_interrupt_driven_config_hooks();

	/* Block boot processing until all hooks are disestablished. */
	TSWAIT("config hooks");
	mtx_lock(&intr_config_hook_lock);
	warned = 0;
	while (!TAILQ_EMPTY(&intr_config_hook_list)) {
		if (msleep(&intr_config_hook_list, &intr_config_hook_lock,
		    0, "conifhk", WARNING_INTERVAL_SECS * hz) ==
		    EWOULDBLOCK) {
			mtx_unlock(&intr_config_hook_lock);
			warned++;
			run_interrupt_driven_config_hooks_warning(warned);
			mtx_lock(&intr_config_hook_lock);
		}
	}
	mtx_unlock(&intr_config_hook_lock);
	TSUNWAIT("config hooks");
}

SYSINIT(intr_config_hooks, SI_SUB_INT_CONFIG_HOOKS, SI_ORDER_FIRST,
	boot_run_interrupt_driven_config_hooks, NULL);

/*
 * Register a hook that will be called after "cold"
 * autoconfiguration is complete and interrupts can
 * be used to complete initialization.
 */
int
config_intrhook_establish(struct intr_config_hook *hook)
{
	struct intr_config_hook *hook_entry;

	TSHOLD("config hooks");
	mtx_lock(&intr_config_hook_lock);
	TAILQ_FOREACH(hook_entry, &intr_config_hook_list, ich_links)
		if (hook_entry == hook)
			break;
	if (hook_entry != NULL) {
		mtx_unlock(&intr_config_hook_lock);
		printf("config_intrhook_establish: establishing an "
		       "already established hook.\n");
		return (1);
	}
	TAILQ_INSERT_TAIL(&intr_config_hook_list, hook, ich_links);
	if (next_to_notify == NULL)
		next_to_notify = hook;
	mtx_unlock(&intr_config_hook_lock);
	if (cold == 0)
		/*
		 * XXX Call from a task since not all drivers expect
		 *     to be re-entered at the time a hook is established.
		 */
		/* XXX Sufficient for modules loaded after initial config??? */
		run_interrupt_driven_config_hooks();	
	return (0);
}

/*
 * Register a hook function that is automatically unregistered after it runs.
 */
void
config_intrhook_oneshot(ich_func_t func, void *arg)
{
	struct oneshot_config_hook *ohook;

	ohook = malloc(sizeof(*ohook), M_DEVBUF, M_WAITOK);
	ohook->och_func = func;
	ohook->och_arg  = arg;
	ohook->och_hook.ich_func = config_intrhook_oneshot_func;
	ohook->och_hook.ich_arg  = ohook;
	config_intrhook_establish(&ohook->och_hook);
}

void
config_intrhook_disestablish(struct intr_config_hook *hook)
{
	struct intr_config_hook *hook_entry;

	mtx_lock(&intr_config_hook_lock);
	TAILQ_FOREACH(hook_entry, &intr_config_hook_list, ich_links)
		if (hook_entry == hook)
			break;
	if (hook_entry == NULL)
		panic("config_intrhook_disestablish: disestablishing an "
		      "unestablished hook");

	if (next_to_notify == hook)
		next_to_notify = TAILQ_NEXT(hook, ich_links);
	TAILQ_REMOVE(&intr_config_hook_list, hook, ich_links);
	TSRELEASE("config hooks");

	/* Wakeup anyone watching the list */
	wakeup(&intr_config_hook_list);
	mtx_unlock(&intr_config_hook_lock);
}

#ifdef DDB
#include <ddb/ddb.h>

DB_SHOW_COMMAND(conifhk, db_show_conifhk)
{
	struct intr_config_hook *hook_entry;
	char namebuf[64];
	long offset;

	TAILQ_FOREACH(hook_entry, &intr_config_hook_list, ich_links) {
		if (linker_ddb_search_symbol_name(
		    (caddr_t)hook_entry->ich_func, namebuf, sizeof(namebuf),
		    &offset) == 0) {
			db_printf("hook: %p at %s+%#lx arg: %p\n",
			    hook_entry->ich_func, namebuf, offset,
			    hook_entry->ich_arg);
		} else {
			db_printf("hook: %p at ??+?? arg %p\n",
			    hook_entry->ich_func, hook_entry->ich_arg);
		}
	}
}
#endif /* DDB */
