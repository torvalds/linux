/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998, 1999 Nicolas Souchu
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
 *
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include "opt_ppb_1284.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <machine/resource.h>

#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppb_1284.h>

#include "ppbus_if.h"

#define DEVTOSOFTC(dev) ((struct ppb_data *)device_get_softc(dev))

static MALLOC_DEFINE(M_PPBUSDEV, "ppbusdev", "Parallel Port bus device");


static int	ppbus_intr(void *arg);

/*
 * Device methods
 */

static int
ppbus_print_child(device_t bus, device_t dev)
{
	struct ppb_device *ppbdev;
	int retval;

	retval = bus_print_child_header(bus, dev);

	ppbdev = (struct ppb_device *)device_get_ivars(dev);

	if (ppbdev->flags != 0)
		retval += printf(" flags 0x%x", ppbdev->flags);

	retval += bus_print_child_footer(bus, dev);

	return (retval);
}

static int
ppbus_probe(device_t dev)
{
	device_set_desc(dev, "Parallel port bus");

	return (0);
}

/*
 * ppbus_add_child()
 *
 * Add a ppbus device, allocate/initialize the ivars
 */
static device_t
ppbus_add_child(device_t dev, u_int order, const char *name, int unit)
{
	struct ppb_device *ppbdev;
	device_t child;

	/* allocate ivars for the new ppbus child */
	ppbdev = malloc(sizeof(struct ppb_device), M_PPBUSDEV,
		M_NOWAIT | M_ZERO);
	if (!ppbdev)
		return (NULL);

	/* initialize the ivars */
	ppbdev->name = name;

	/* add the device as a child to the ppbus bus with the allocated
	 * ivars */
	child = device_add_child_ordered(dev, order, name, unit);
	device_set_ivars(child, ppbdev);

	return (child);
}

static int
ppbus_read_ivar(device_t bus, device_t dev, int index, uintptr_t* val)
{

	switch (index) {
	case PPBUS_IVAR_MODE:
		/* XXX yet device mode = ppbus mode = chipset mode */
		*val = (u_long)ppb_get_mode(bus);
		break;
	default:
		return (ENOENT);
	}

	return (0);
}

static int
ppbus_write_ivar(device_t bus, device_t dev, int index, uintptr_t val)
{

	switch (index) {
	case PPBUS_IVAR_MODE:
		/* XXX yet device mode = ppbus mode = chipset mode */
		ppb_set_mode(bus, val);
		break;
	default:
		return (ENOENT);
  	}

	return (0);
}

#define PPB_PNP_PRINTER		0
#define PPB_PNP_MODEM		1
#define PPB_PNP_NET		2
#define PPB_PNP_HDC		3
#define PPB_PNP_PCMCIA		4
#define PPB_PNP_MEDIA		5
#define PPB_PNP_FDC		6
#define PPB_PNP_PORTS		7
#define PPB_PNP_SCANNER		8
#define PPB_PNP_DIGICAM		9

#ifndef DONTPROBE_1284

static char *pnp_tokens[] = {
	"PRINTER", "MODEM", "NET", "HDC", "PCMCIA", "MEDIA",
	"FDC", "PORTS", "SCANNER", "DIGICAM", "", NULL };

#if 0
static char *pnp_classes[] = {
	"printer", "modem", "network device",
	"hard disk", "PCMCIA", "multimedia device",
	"floppy disk", "ports", "scanner",
	"digital camera", "unknown device", NULL };
#endif

/*
 * search_token()
 *
 * Search the first occurrence of a token within a string
 */
static char *
search_token(char *str, int slen, char *token)
{
	int tlen, i;

#define UNKNOWN_LENGTH	-1

	if (slen == UNKNOWN_LENGTH)
		/* get string's length */
		slen = strlen(str);

	/* get token's length */
	tlen = strlen(token);
	if (tlen == 0)
		return (str);

	for (i = 0; i <= slen-tlen; i++) {
		if (strncmp(str + i, token, tlen) == 0)
			return (&str[i]);
	}

	return (NULL);
}

/*
 * ppb_pnp_detect()
 *
 * Returns the class id. of the peripherial, -1 otherwise
 */
static int
ppb_pnp_detect(device_t bus)
{
	char *token, *class = NULL;
	int i, len, error;
	int class_id = -1;
	char str[PPB_PnP_STRING_SIZE+1];

	device_printf(bus, "Probing for PnP devices:\n");

	if ((error = ppb_1284_read_id(bus, PPB_NIBBLE, str,
					PPB_PnP_STRING_SIZE, &len)))
		goto end_detect;

#ifdef DEBUG_1284
	device_printf(bus, "<PnP> %d characters: ", len);
	for (i = 0; i < len; i++)
		printf("%c(0x%x) ", str[i], str[i]);
	printf("\n");
#endif

	/* replace ';' characters by '\0' */
	for (i = 0; i < len; i++)
		str[i] = (str[i] == ';') ? '\0' : str[i];

	if ((token = search_token(str, len, "MFG")) != NULL ||
		(token = search_token(str, len, "MANUFACTURER")) != NULL)
		device_printf(bus, "<%s",
			search_token(token, UNKNOWN_LENGTH, ":") + 1);
	else
		device_printf(bus, "<unknown");

	if ((token = search_token(str, len, "MDL")) != NULL ||
		(token = search_token(str, len, "MODEL")) != NULL)
		printf(" %s",
			search_token(token, UNKNOWN_LENGTH, ":") + 1);
	else
		printf(" unknown");

	if ((token = search_token(str, len, "VER")) != NULL)
		printf("/%s",
			search_token(token, UNKNOWN_LENGTH, ":") + 1);

	if ((token = search_token(str, len, "REV")) != NULL)
		printf(".%s",
			search_token(token, UNKNOWN_LENGTH, ":") + 1);

	printf(">");

	if ((token = search_token(str, len, "CLS")) != NULL) {
		class = search_token(token, UNKNOWN_LENGTH, ":") + 1;
		printf(" %s", class);
	}

	if ((token = search_token(str, len, "CMD")) != NULL ||
		(token = search_token(str, len, "COMMAND")) != NULL)
		printf(" %s",
			search_token(token, UNKNOWN_LENGTH, ":") + 1);

	printf("\n");

	if (class)
		/* identify class ident */
		for (i = 0; pnp_tokens[i] != NULL; i++) {
			if (search_token(class, len, pnp_tokens[i]) != NULL) {
				class_id = i;
				goto end_detect;
			}
		}

	class_id = PPB_PnP_UNKNOWN;

end_detect:
	return (class_id);
}

/*
 * ppb_scan_bus()
 *
 * Scan the ppbus for IEEE1284 compliant devices
 */
static int
ppb_scan_bus(device_t bus)
{
	struct ppb_data * ppb = (struct ppb_data *)device_get_softc(bus);
	int error = 0;

	/* try all IEEE1284 modes, for one device only
	 *
	 * XXX We should implement the IEEE1284.3 standard to detect
	 * daisy chained devices
	 */

	error = ppb_1284_negociate(bus, PPB_NIBBLE, PPB_REQUEST_ID);

	if ((ppb->state == PPB_ERROR) && (ppb->error == PPB_NOT_IEEE1284))
		goto end_scan;

	ppb_1284_terminate(bus);

	device_printf(bus, "IEEE1284 device found ");

	if (!(error = ppb_1284_negociate(bus, PPB_NIBBLE, 0))) {
		printf("/NIBBLE");
		ppb_1284_terminate(bus);
	}

	if (!(error = ppb_1284_negociate(bus, PPB_PS2, 0))) {
		printf("/PS2");
		ppb_1284_terminate(bus);
	}

	if (!(error = ppb_1284_negociate(bus, PPB_ECP, 0))) {
		printf("/ECP");
		ppb_1284_terminate(bus);
	}

	if (!(error = ppb_1284_negociate(bus, PPB_ECP, PPB_USE_RLE))) {
		printf("/ECP_RLE");
		ppb_1284_terminate(bus);
	}

	if (!(error = ppb_1284_negociate(bus, PPB_EPP, 0))) {
		printf("/EPP");
		ppb_1284_terminate(bus);
	}

	/* try more IEEE1284 modes */
	if (bootverbose) {
		if (!(error = ppb_1284_negociate(bus, PPB_NIBBLE,
				PPB_REQUEST_ID))) {
			printf("/NIBBLE_ID");
			ppb_1284_terminate(bus);
		}

		if (!(error = ppb_1284_negociate(bus, PPB_PS2,
				PPB_REQUEST_ID))) {
			printf("/PS2_ID");
			ppb_1284_terminate(bus);
		}

		if (!(error = ppb_1284_negociate(bus, PPB_ECP,
				PPB_REQUEST_ID))) {
			printf("/ECP_ID");
			ppb_1284_terminate(bus);
		}

		if (!(error = ppb_1284_negociate(bus, PPB_ECP,
				PPB_REQUEST_ID | PPB_USE_RLE))) {
			printf("/ECP_RLE_ID");
			ppb_1284_terminate(bus);
		}

		if (!(error = ppb_1284_negociate(bus, PPB_COMPATIBLE,
				PPB_EXTENSIBILITY_LINK))) {
			printf("/Extensibility Link");
			ppb_1284_terminate(bus);
		}
	}

	printf("\n");

	/* detect PnP devices */
	ppb->class_id = ppb_pnp_detect(bus);

	return (0);

end_scan:
	return (error);
}

#endif /* !DONTPROBE_1284 */

static int
ppbus_attach(device_t dev)
{
	struct ppb_data *ppb = device_get_softc(dev);
	int error, rid;

	error = BUS_READ_IVAR(device_get_parent(dev), dev, PPC_IVAR_LOCK,
	    (uintptr_t *)&ppb->ppc_lock);
	if (error) {
		device_printf(dev, "Unable to fetch parent's lock\n");
		return (error);
	}

	rid = 0;
	ppb->ppc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE);
	if (ppb->ppc_irq_res != NULL) {
		mtx_lock(ppb->ppc_lock);
		error = BUS_WRITE_IVAR(device_get_parent(dev), dev,
		    PPC_IVAR_INTR_HANDLER, (uintptr_t)&ppbus_intr);
		mtx_unlock(ppb->ppc_lock);
		if (error) {
			device_printf(dev, "Unable to set interrupt handler\n");
			return (error);
		}
	}

	/* Locate our children */
	bus_generic_probe(dev);

#ifndef DONTPROBE_1284
	/* detect IEEE1284 compliant devices */
	mtx_lock(ppb->ppc_lock);
	ppb_scan_bus(dev);
	mtx_unlock(ppb->ppc_lock);
#endif /* !DONTPROBE_1284 */

	/* launch attachment of the added children */
	bus_generic_attach(dev);

	return (0);
}

static int
ppbus_detach(device_t dev)
{
	int error;

	error = bus_generic_detach(dev);
	if (error)
		return (error);

	/* detach & delete all children */
	device_delete_children(dev);

	return (0);
}

static int
ppbus_intr(void *arg)
{
	struct ppb_device *ppbdev;
	struct ppb_data *ppb = arg;

	mtx_assert(ppb->ppc_lock, MA_OWNED);
	if (ppb->ppb_owner == NULL)
		return (ENOENT);

	ppbdev = device_get_ivars(ppb->ppb_owner);
	if (ppbdev->intr_hook == NULL)
		return (ENOENT);

	ppbdev->intr_hook(ppbdev->intr_arg);
	return (0);
}

static int
ppbus_setup_intr(device_t bus, device_t child, struct resource *r, int flags,
    driver_filter_t *filt, void (*ihand)(void *), void *arg, void **cookiep)
{
	struct ppb_device *ppbdev = device_get_ivars(child);
	struct ppb_data *ppb = DEVTOSOFTC(bus);

	/* We do not support filters. */
	if (filt != NULL || ihand == NULL)
		return (EINVAL);

	/* Can only attach handlers to the parent device's resource. */
	if (ppb->ppc_irq_res != r)
		return (EINVAL);

	mtx_lock(ppb->ppc_lock);
	ppbdev->intr_hook = ihand;
	ppbdev->intr_arg = arg;
	*cookiep = ppbdev;
	mtx_unlock(ppb->ppc_lock);

	return (0);
}

static int
ppbus_teardown_intr(device_t bus, device_t child, struct resource *r, void *ih)
{
	struct ppb_device *ppbdev = device_get_ivars(child);
	struct ppb_data *ppb = DEVTOSOFTC(bus);

	mtx_lock(ppb->ppc_lock);
	if (ppbdev != ih || ppb->ppc_irq_res != r) {
		mtx_unlock(ppb->ppc_lock);
		return (EINVAL);
	}

	ppbdev->intr_hook = NULL;
	mtx_unlock(ppb->ppc_lock);

	return (0);
}

/*
 * ppb_request_bus()
 *
 * Allocate the device to perform transfers.
 *
 * how	: PPB_WAIT or PPB_DONTWAIT
 */
int
ppb_request_bus(device_t bus, device_t dev, int how)
{
	struct ppb_data *ppb = DEVTOSOFTC(bus);
	struct ppb_device *ppbdev = (struct ppb_device *)device_get_ivars(dev);
	int error = 0;

	mtx_assert(ppb->ppc_lock, MA_OWNED);
	while (!error) {
		if (ppb->ppb_owner) {
			switch (how) {
			case PPB_WAIT | PPB_INTR:
				error = mtx_sleep(ppb, ppb->ppc_lock,
				    PPBPRI | PCATCH, "ppbreq", 0);
				break;

			case PPB_WAIT | PPB_NOINTR:
				error = mtx_sleep(ppb, ppb->ppc_lock, PPBPRI,
				    "ppbreq", 0);
				break;

			default:
				return (EWOULDBLOCK);
			}

		} else {
			ppb->ppb_owner = dev;

			/* restore the context of the device
			 * The first time, ctx.valid is certainly false
			 * then do not change anything. This is useful for
			 * drivers that do not set there operating mode
			 * during attachement
			 */
			if (ppbdev->ctx.valid)
				ppb_set_mode(bus, ppbdev->ctx.mode);

			return (0);
		}
	}

	return (error);
}

/*
 * ppb_release_bus()
 *
 * Release the device allocated with ppb_request_bus()
 */
int
ppb_release_bus(device_t bus, device_t dev)
{
	struct ppb_data *ppb = DEVTOSOFTC(bus);
	struct ppb_device *ppbdev = (struct ppb_device *)device_get_ivars(dev);

	mtx_assert(ppb->ppc_lock, MA_OWNED);
	if (ppb->ppb_owner != dev)
		return (EACCES);

	/* save the context of the device */
	ppbdev->ctx.mode = ppb_get_mode(bus);

	/* ok, now the context of the device is valid */
	ppbdev->ctx.valid = 1;

	ppb->ppb_owner = 0;

	/* wakeup waiting processes */
	wakeup(ppb);

	return (0);
}

static devclass_t ppbus_devclass;

static device_method_t ppbus_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		ppbus_probe),
	DEVMETHOD(device_attach,	ppbus_attach),
	DEVMETHOD(device_detach,	ppbus_detach),

	/* bus interface */
	DEVMETHOD(bus_add_child,	ppbus_add_child),
	DEVMETHOD(bus_print_child,	ppbus_print_child),
	DEVMETHOD(bus_read_ivar,	ppbus_read_ivar),
	DEVMETHOD(bus_write_ivar,	ppbus_write_ivar),
	DEVMETHOD(bus_setup_intr,	ppbus_setup_intr),
	DEVMETHOD(bus_teardown_intr,	ppbus_teardown_intr),
	DEVMETHOD(bus_alloc_resource,	bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),

	{ 0, 0 }
};

static driver_t ppbus_driver = {
	"ppbus",
	ppbus_methods,
	sizeof(struct ppb_data),
};
DRIVER_MODULE(ppbus, ppc, ppbus_driver, ppbus_devclass, 0, 0);
