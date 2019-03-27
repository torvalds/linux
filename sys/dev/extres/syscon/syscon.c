/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Kyle Evans <kevans@FreeBSD.org>
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
 */

/*
 * This is a generic syscon driver, whose purpose is to provide access to
 * various unrelated bits packed in a single register space. It is usually used
 * as a fallback to more specific driver, but works well enough for simple
 * access.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sx.h>
#include <sys/queue.h>

#include <machine/bus.h>

#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include "syscon_if.h"
#include "syscon.h"

/*
 * Syscon interface details
 */
typedef TAILQ_HEAD(syscon_list, syscon) syscon_list_t;

/*
 * Declarations
 */
static int syscon_method_init(struct syscon *syscon);
static int syscon_method_uninit(struct syscon *syscon);

MALLOC_DEFINE(M_SYSCON, "syscon", "Syscon driver");

static syscon_list_t syscon_list = TAILQ_HEAD_INITIALIZER(syscon_list);
static struct sx		syscon_topo_lock;
SX_SYSINIT(syscon_topology, &syscon_topo_lock, "Syscon topology lock");

/*
 * Syscon methods.
 */
static syscon_method_t syscon_methods[] = {
	SYSCONMETHOD(syscon_init,	syscon_method_init),
	SYSCONMETHOD(syscon_uninit,	syscon_method_uninit),

	SYSCONMETHOD_END
};
DEFINE_CLASS_0(syscon, syscon_class, syscon_methods, 0);

#define SYSCON_TOPO_SLOCK()	sx_slock(&syscon_topo_lock)
#define SYSCON_TOPO_XLOCK()	sx_xlock(&syscon_topo_lock)
#define SYSCON_TOPO_UNLOCK()	sx_unlock(&syscon_topo_lock)
#define SYSCON_TOPO_ASSERT()	sx_assert(&syscon_topo_lock, SA_LOCKED)
#define SYSCON_TOPO_XASSERT()	sx_assert(&syscon_topo_lock, SA_XLOCKED)

/*
 * Default syscon methods for base class.
 */
static int
syscon_method_init(struct syscon *syscon)
{

	return (0);
};

static int
syscon_method_uninit(struct syscon *syscon)
{

	return (0);
};

void *
syscon_get_softc(struct syscon *syscon)
{

	return (syscon->softc);
};

/*
 * Create and initialize syscon object, but do not register it.
 */
struct syscon *
syscon_create(device_t pdev, syscon_class_t syscon_class)
{
	struct syscon *syscon;

	/* Create object and initialize it. */
	syscon = malloc(sizeof(struct syscon), M_SYSCON,
	    M_WAITOK | M_ZERO);
	kobj_init((kobj_t)syscon, (kobj_class_t)syscon_class);

	/* Allocate softc if required. */
	if (syscon_class->size > 0)
		syscon->softc = malloc(syscon_class->size, M_SYSCON,
		    M_WAITOK | M_ZERO);

	/* Rest of init. */
	syscon->pdev = pdev;
	return (syscon);
}

/* Register syscon object. */
struct syscon *
syscon_register(struct syscon *syscon)
{
	int rv;

#ifdef FDT
	if (syscon->ofw_node <= 0)
		syscon->ofw_node = ofw_bus_get_node(syscon->pdev);
	if (syscon->ofw_node <= 0)
		return (NULL);
#endif

	rv = SYSCON_INIT(syscon);
	if (rv != 0) {
		printf("SYSCON_INIT failed: %d\n", rv);
		return (NULL);
	}

#ifdef FDT
	OF_device_register_xref(OF_xref_from_node(syscon->ofw_node),
	    syscon->pdev);
#endif
	SYSCON_TOPO_XLOCK();
	TAILQ_INSERT_TAIL(&syscon_list, syscon, syscon_link);
	SYSCON_TOPO_UNLOCK();
	return (syscon);
}

int
syscon_unregister(struct syscon *syscon)
{

	SYSCON_TOPO_XLOCK();
	TAILQ_REMOVE(&syscon_list, syscon, syscon_link);
	SYSCON_TOPO_UNLOCK();
#ifdef FDT
	OF_device_register_xref(OF_xref_from_node(syscon->ofw_node), NULL);
#endif
	return (SYSCON_UNINIT(syscon));
}

/**
 * Provider methods
 */
#ifdef FDT
static struct syscon *
syscon_find_by_ofw_node(phandle_t node)
{
	struct syscon *entry;

	SYSCON_TOPO_ASSERT();

	TAILQ_FOREACH(entry, &syscon_list, syscon_link) {
		if (entry->ofw_node == node)
			return (entry);
	}

	return (NULL);
}

struct syscon *
syscon_create_ofw_node(device_t pdev, syscon_class_t syscon_class,
    phandle_t node)
{
	struct syscon *syscon;

	syscon = syscon_create(pdev, syscon_class);
	if (syscon == NULL)
		return (NULL);
	syscon->ofw_node = node;
	if (syscon_register(syscon) == NULL)
		return (NULL);
	return (syscon);
}

phandle_t
syscon_get_ofw_node(struct syscon *syscon)
{

	return (syscon->ofw_node);
}

int
syscon_get_by_ofw_property(device_t cdev, phandle_t cnode, char *name,
    struct syscon **syscon)
{
	pcell_t *cells;
	int ncells;

	if (cnode <= 0)
		cnode = ofw_bus_get_node(cdev);
	if (cnode <= 0) {
		device_printf(cdev,
		    "%s called on not ofw based device\n", __func__);
		return (ENXIO);
	}
	ncells = OF_getencprop_alloc_multi(cnode, name, sizeof(pcell_t),
	    (void **)&cells);
	if (ncells < 1)
		return (ENOENT);

	/* Translate to syscon node. */
	SYSCON_TOPO_SLOCK();
	*syscon = syscon_find_by_ofw_node(OF_node_from_xref(cells[0]));
	if (*syscon == NULL) {
		SYSCON_TOPO_UNLOCK();
		device_printf(cdev, "Failed to find syscon node\n");
		OF_prop_free(cells);
		return (ENODEV);
	}
	SYSCON_TOPO_UNLOCK();
	OF_prop_free(cells);
	return (0);
}
#endif
