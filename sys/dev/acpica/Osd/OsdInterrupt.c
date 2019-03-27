/*-
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2011 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
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
 * 6.5 : Interrupt handling
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>

#define _COMPONENT	ACPI_OS_SERVICES
ACPI_MODULE_NAME("INTERRUPT")

static MALLOC_DEFINE(M_ACPIINTR, "acpiintr", "ACPI interrupt");

struct acpi_intr {
	SLIST_ENTRY(acpi_intr)	ai_link;
	struct resource		*ai_irq;
	int			ai_rid;
	void			*ai_handle;
	int			ai_number;
	ACPI_OSD_HANDLER	ai_handler;
	void			*ai_context;
};
static SLIST_HEAD(, acpi_intr) acpi_intr_list =
    SLIST_HEAD_INITIALIZER(acpi_intr_list);
static struct mtx	acpi_intr_lock;

static UINT32		InterruptOverride;

static void
acpi_intr_init(struct mtx *lock)
{

	mtx_init(lock, "ACPI interrupt lock", NULL, MTX_DEF);
}

SYSINIT(acpi_intr, SI_SUB_DRIVERS, SI_ORDER_FIRST, acpi_intr_init,
    &acpi_intr_lock);

static int
acpi_intr_handler(void *arg)
{
	struct acpi_intr *ai;

	ai = arg;
	KASSERT(ai != NULL && ai->ai_handler != NULL,
	    ("invalid ACPI interrupt handler"));
	if (ai->ai_handler(ai->ai_context) == ACPI_INTERRUPT_HANDLED)
		return (FILTER_HANDLED);
	return (FILTER_STRAY);
}

static void
acpi_intr_destroy(device_t dev, struct acpi_intr *ai)
{

	if (ai->ai_handle != NULL)
		bus_teardown_intr(dev, ai->ai_irq, ai->ai_handle);
	if (ai->ai_irq != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, ai->ai_rid, ai->ai_irq);
	bus_delete_resource(dev, SYS_RES_IRQ, ai->ai_rid);
	free(ai, M_ACPIINTR);
}

ACPI_STATUS
AcpiOsInstallInterruptHandler(UINT32 InterruptNumber,
    ACPI_OSD_HANDLER ServiceRoutine, void *Context)
{
	struct acpi_softc *sc;
	struct acpi_intr *ai, *ap;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	sc = devclass_get_softc(devclass_find("acpi"), 0);
	KASSERT(sc != NULL && sc->acpi_dev != NULL,
	    ("can't find ACPI device to register interrupt"));

	if (InterruptNumber > 255 || ServiceRoutine == NULL)
		return_ACPI_STATUS (AE_BAD_PARAMETER);

	ai = malloc(sizeof(*ai), M_ACPIINTR, M_WAITOK | M_ZERO);
	mtx_lock(&acpi_intr_lock);
	SLIST_FOREACH(ap, &acpi_intr_list, ai_link) {
		if (InterruptNumber == ap->ai_number ||
		    (InterruptNumber == InterruptOverride &&
		    InterruptNumber != AcpiGbl_FADT.SciInterrupt)) {
			mtx_unlock(&acpi_intr_lock);
			free(ai, M_ACPIINTR);
			return_ACPI_STATUS (AE_ALREADY_EXISTS);
		}
		if (ai->ai_rid <= ap->ai_rid)
			ai->ai_rid = ap->ai_rid + 1;
	}
	ai->ai_number = InterruptNumber;
	ai->ai_handler = ServiceRoutine;
	ai->ai_context = Context;
	SLIST_INSERT_HEAD(&acpi_intr_list, ai, ai_link);
	mtx_unlock(&acpi_intr_lock);

	/*
	 * If the MADT contained an interrupt override directive for the SCI,
	 * we use that value instead of the one from the FADT.
	 */
	if (InterruptOverride != 0 &&
	    InterruptNumber == AcpiGbl_FADT.SciInterrupt) {
		device_printf(sc->acpi_dev,
		    "Overriding SCI from IRQ %u to IRQ %u\n",
		    InterruptNumber, InterruptOverride);
		InterruptNumber = InterruptOverride;
	}

	/* Set up the interrupt resource. */
	bus_set_resource(sc->acpi_dev, SYS_RES_IRQ, ai->ai_rid,
	    InterruptNumber, 1);
	ai->ai_irq = bus_alloc_resource_any(sc->acpi_dev, SYS_RES_IRQ,
	    &ai->ai_rid, RF_SHAREABLE | RF_ACTIVE);
	if (ai->ai_irq == NULL) {
		device_printf(sc->acpi_dev, "could not allocate interrupt\n");
		goto error;
	}
	if (bus_setup_intr(sc->acpi_dev, ai->ai_irq,
	    INTR_TYPE_MISC | INTR_MPSAFE, acpi_intr_handler, NULL, ai,
	    &ai->ai_handle) != 0) {
		device_printf(sc->acpi_dev, "could not set up interrupt\n");
		goto error;
	}
	return_ACPI_STATUS (AE_OK);

error:
	mtx_lock(&acpi_intr_lock);
	SLIST_REMOVE(&acpi_intr_list, ai, acpi_intr, ai_link);
	mtx_unlock(&acpi_intr_lock);
	acpi_intr_destroy(sc->acpi_dev, ai);
	return_ACPI_STATUS (AE_ALREADY_EXISTS);
}

ACPI_STATUS
AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber,
    ACPI_OSD_HANDLER ServiceRoutine)
{
	struct acpi_softc *sc;
	struct acpi_intr *ai;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	sc = devclass_get_softc(devclass_find("acpi"), 0);
	KASSERT(sc != NULL && sc->acpi_dev != NULL,
	    ("can't find ACPI device to deregister interrupt"));

	if (InterruptNumber > 255 || ServiceRoutine == NULL)
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	mtx_lock(&acpi_intr_lock);
	SLIST_FOREACH(ai, &acpi_intr_list, ai_link)
		if (InterruptNumber == ai->ai_number) {
			if (ServiceRoutine != ai->ai_handler) {
				mtx_unlock(&acpi_intr_lock);
				return_ACPI_STATUS (AE_BAD_PARAMETER);
			}
			SLIST_REMOVE(&acpi_intr_list, ai, acpi_intr, ai_link);
			break;
		}
	mtx_unlock(&acpi_intr_lock);
	if (ai == NULL)
		return_ACPI_STATUS (AE_NOT_EXIST);
	acpi_intr_destroy(sc->acpi_dev, ai);
	return_ACPI_STATUS (AE_OK);
}

ACPI_STATUS
acpi_OverrideInterruptLevel(UINT32 InterruptNumber)
{

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (InterruptOverride != 0)
		return_ACPI_STATUS (AE_ALREADY_EXISTS);
	InterruptOverride = InterruptNumber;
	return_ACPI_STATUS (AE_OK);
}
