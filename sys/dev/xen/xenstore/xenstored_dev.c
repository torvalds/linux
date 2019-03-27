/*
 * Copyright (c) 2014 Roger Pau Monné <roger.pau@citrix.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/module.h>

#include <xen/xen-os.h>

#include <xen/hypervisor.h>
#include <xen/xenstore/xenstorevar.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#define XSD_READ_SIZE		20

static int xsd_dev_read(struct cdev *dev, struct uio *uio, int ioflag);
static int xsd_dev_mmap(struct cdev *dev, vm_ooffset_t offset,
    vm_paddr_t *paddr, int nprot, vm_memattr_t *memattr);


static struct cdevsw xsd_dev_cdevsw = {
	.d_version = D_VERSION,
	.d_read = xsd_dev_read,
	.d_mmap = xsd_dev_mmap,
	.d_name = "xsd_dev",
};

static int
xsd_dev_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	char evtchn[XSD_READ_SIZE];
	int error, len;

	len = snprintf(evtchn, sizeof(evtchn), "%u", xs_evtchn());
	if (len < 0 || len > uio->uio_resid)
		return (EINVAL);

	error = uiomove(evtchn, len, uio);
	if (error)
		return (error);

	return (0);
}

static int
xsd_dev_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{

	if (offset != 0)
		return (EINVAL);

	*paddr = xs_address();

	return (0);
}

/*------------------ Private Device Attachment Functions  --------------------*/
/**
 * \brief Identify instances of this device type in the system.
 *
 * \param driver  The driver performing this identify action.
 * \param parent  The NewBus parent device for any devices this method adds.
 */
static void
xsd_dev_identify(driver_t *driver __unused, device_t parent)
{

	if (!xen_domain() || xs_initialized())
		return;

	/*
	 * Only attach if xenstore is not available, because we are the
	 * domain that's supposed to run it.
	 */
	BUS_ADD_CHILD(parent, 0, driver->name, 0);
}

/**
 * \brief Probe for the existence of the Xenstored device
 *
 * \param dev  NewBus device_t for this instance.
 *
 * \return  Always returns 0 indicating success.
 */
static int 
xsd_dev_probe(device_t dev)
{

	device_set_desc(dev, "Xenstored user-space device");
	return (BUS_PROBE_NOWILDCARD);
}

/**
 * \brief Attach the Xenstored device.
 *
 * \param dev  NewBus device_t for this instance.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
static int
xsd_dev_attach(device_t dev)
{
	struct cdev *xsd_cdev;

	xsd_cdev = make_dev(&xsd_dev_cdevsw, 0, UID_ROOT, GID_WHEEL, 0400,
	    "xen/xenstored");
	if (xsd_cdev == NULL)
		return (EINVAL);

	return (0);
}

/*-------------------- Private Device Attachment Data  -----------------------*/
static device_method_t xsd_dev_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	xsd_dev_identify),
	DEVMETHOD(device_probe,         xsd_dev_probe),
	DEVMETHOD(device_attach,        xsd_dev_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_0(xsd_dev, xsd_dev_driver, xsd_dev_methods, 0);
devclass_t xsd_dev_devclass;

DRIVER_MODULE(xsd_dev, xenpv, xsd_dev_driver, xsd_dev_devclass,
    NULL, NULL);
