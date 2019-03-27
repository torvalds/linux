#!/bin/sh
# This writes a skeleton driver and puts it into the kernel tree for you.
# It also adds FOO and files.FOO configuration files so you can compile
# a kernel with your FOO driver linked in.
# To do so:
# cd /usr/src; make buildkernel KERNCONF=FOO
#
# More interestingly, it creates a modules/foo directory
# which it populates, to allow you to compile a FOO module
# which can be linked with your presently running kernel (if you feel brave).
# To do so:
# cd /sys/modules/foo; make depend; make; make install; kldload foo
#
# arg1 to this script is expected to be lowercase "foo"
# arg2 path to the kernel sources, "/sys" if omitted
#
# Trust me, RUN THIS SCRIPT :)
#
# TODO:
#   o generate foo_isa.c, foo_pci.c, foo_pccard.c, foo_cardbus.c, and foovar.h
#   o Put pccard stuff in here.
#
# $FreeBSD$"
#
#
if [ "X${1}" = "X" ]; then
	echo "Hey, how about some help here... give me a device name!"
	exit 1
fi
if [ "X${2}" = "X" ]; then
	TOP=`cd /sys; pwd -P`
	echo "Using ${TOP} as the path to the kernel sources!"
else
	TOP=${2}
fi
UPPER=`echo ${1} |tr "[:lower:]" "[:upper:]"`

RCS_KEYWORD=FreeBSD

if [ -d ${TOP}/modules/${1} ]; then
	echo "There appears to already be a module called ${1}"
	echo -n "Should it be overwritten? [Y]"
	read VAL
	if [ "-z" "$VAL" ]; then
		VAL=YES
	fi
	case ${VAL} in
	[yY]*)
		echo "Cleaning up from prior runs"
		rm -rf ${TOP}/dev/${1}
		rm -rf ${TOP}/modules/${1}
		rm ${TOP}/conf/files.${UPPER}
		rm ${TOP}/i386/conf/${UPPER}
		rm ${TOP}/sys/${1}io.h
		;;
	*)
		exit 1
		;;
	esac
fi

echo "The following files will be created:"
echo ${TOP}/modules/${1}
echo ${TOP}/conf/files.${UPPER}
echo ${TOP}/i386/conf/${UPPER}
echo ${TOP}/dev/${1}
echo ${TOP}/dev/${1}/${1}.c
echo ${TOP}/sys/${1}io.h
echo ${TOP}/modules/${1}
echo ${TOP}/modules/${1}/Makefile

	mkdir ${TOP}/modules/${1}

#######################################################################
#######################################################################
#
# Create configuration information needed to create a kernel
# containing this driver.
#
# Not really needed if we are going to do this as a module.
#######################################################################
# First add the file to a local file list.
#######################################################################

cat >${TOP}/conf/files.${UPPER} <<DONE
dev/${1}/${1}.c	 optional ${1}
DONE

#######################################################################
# Then create a configuration file for a kernel that contains this driver.
#######################################################################
cat >${TOP}/i386/conf/${UPPER} <<DONE
# Configuration file for kernel type: ${UPPER}
# \$${RCS_KEYWORD}$

files		"${TOP}/conf/files.${UPPER}"

include		GENERIC

ident		${UPPER}

DONE

cat >>${TOP}/i386/conf/${UPPER} <<DONE
# trust me, you'll need this
options		KDB
options		DDB
device		${1}
DONE

if [ ! -d ${TOP}/dev/${1} ]; then
	mkdir -p ${TOP}/dev/${1}
fi

cat >${TOP}/dev/${1}/${1}.c <<DONE
/*
 * Copyright (c) [year] [your name]
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
 * http://www.daemonnews.org/200008/isa.html is required reading.
 * hopefully it will make it's way into the handbook.
 */

#include <sys/cdefs.h>
__FBSDID("\$${RCS_KEYWORD}$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>		/* cdevsw stuff */
#include <sys/kernel.h>		/* SYSINIT stuff */
#include <sys/uio.h>		/* SYSINIT stuff */
#include <sys/malloc.h>		/* malloc region definitions */
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/${1}io.h>		/* ${1} IOCTL definitions */

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <isa/isavar.h>

#include "isa_if.h"

/* XXX These should be defined in terms of bus-space ops. */
#define ${UPPER}_INB(port) inb(port_start)
#define ${UPPER}_OUTB(port, val) ( port_start, (val))
#define SOME_PORT 123
#define EXPECTED_VALUE 0x42

/*
 * The softc is automatically allocated by the parent bus using the
 * size specified in the driver_t declaration below.
 */
#define DEV2SOFTC(dev)	((struct ${1}_softc *) (dev)->si_drv1)
#define DEVICE2SOFTC(dev) ((struct ${1}_softc *) device_get_softc(dev))

/*
 * Device specific misc defines.
 */
#define BUFFERSIZE	1024
#define NUMPORTS	4
#define MEMSIZE		(4 * 1024) /* Imaginable h/w buffer size. */

/*
 * One of these per allocated device.
 */
struct ${1}_softc {
	bus_space_tag_t bt;
	bus_space_handle_t bh;
	int rid_ioport;
	int rid_memory;
	int rid_irq;
	int rid_drq;
	struct resource* res_ioport;	/* Resource for port range. */
	struct resource* res_memory;	/* Resource for mem range. */
	struct resource* res_irq;	/* Resource for irq range. */
	struct resource* res_drq;	/* Resource for dma channel. */
	device_t device;
	struct cdev *dev;
	void	*intr_cookie;
	void	*vaddr;			/* Virtual address of mem resource. */
	char	buffer[BUFFERSIZE];	/* If we need to buffer something. */
};

/* Function prototypes (these should all be static). */
static int ${1}_deallocate_resources(device_t device);
static int ${1}_allocate_resources(device_t device);
static int ${1}_attach(device_t device, struct ${1}_softc *scp);
static int ${1}_detach(device_t device, struct ${1}_softc *scp);

static d_open_t		${1}open;
static d_close_t	${1}close;
static d_read_t		${1}read;
static d_write_t	${1}write;
static d_ioctl_t	${1}ioctl;
static d_mmap_t		${1}mmap;
static d_poll_t		${1}poll;
static	void		${1}intr(void *arg);

static struct cdevsw ${1}_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	${1}open,
	.d_close =	${1}close,
	.d_read =	${1}read,
	.d_write =	${1}write,
	.d_ioctl =	${1}ioctl,
	.d_poll =	${1}poll,
	.d_mmap =	${1}mmap,
	.d_name =	"${1}",
};

static devclass_t ${1}_devclass;

/*
 ******************************************
 * ISA Attachment structures and functions.
 ******************************************
 */
static void ${1}_isa_identify (driver_t *, device_t);
static int ${1}_isa_probe (device_t);
static int ${1}_isa_attach (device_t);
static int ${1}_isa_detach (device_t);

static struct isa_pnp_id ${1}_ids[] = {
	{0x12345678,	"ABCco Widget"},
	{0xfedcba98,	"shining moon Widget ripoff"},
	{0,		NULL}
};

static device_method_t ${1}_methods[] = {
	DEVMETHOD(device_identify,	${1}_isa_identify),
	DEVMETHOD(device_probe,		${1}_isa_probe),
	DEVMETHOD(device_attach,	${1}_isa_attach),
	DEVMETHOD(device_detach,	${1}_isa_detach),
	DEVMETHOD_END
};

static driver_t ${1}_isa_driver = {
	"${1}",
	${1}_methods,
	sizeof (struct ${1}_softc)
};

DRIVER_MODULE(${1}, isa, ${1}_isa_driver, ${1}_devclass, 0, 0);

/*
 * Here list some port addresses we might expect our widget to appear at:
 * This list should only be used for cards that have some non-destructive
 * (to other cards) way of probing these address.  Otherwise the driver
 * should not go looking for instances of itself, but instead rely on
 * the hints file.  Strange failures for people with other cards might
 * result.
 */
static struct localhints {
	int ioport;
	int irq;
	int drq;
	int mem;
} res[] = {
	{ 0x210, 11, 2, 0xcd000},
	{ 0x310, 12, 3, 0xdd000},
	{ 0x320, 9, 6, 0xd4000},
	{0,0,0,0}
};

#define MAXHINTS 10 /* Just an arbitrary safety limit. */
/*
 * Called once when the driver is somehow connected with the bus,
 * (Either linked in and the bus is started, or loaded as a module).
 *
 * The aim of this routine in an ISA driver is to add child entries to
 * the parent bus so that it looks as if the devices were detected by
 * some pnp-like method, or at least mentioned in the hints.
 *
 * For NON-PNP "dumb" devices:
 * Add entries into the bus's list of likely devices, so that
 * our 'probe routine' will be called for them.
 * This is similar to what the 'hints' code achieves, except this is
 * loadable with the driver.
 * In the 'dumb' case we end up with more children than needed but
 * some (or all) of them will fail probe() and only waste a little memory.
 *
 * For NON-PNP "Smart" devices:
 * If the device has a NON-PNP way of being detected and setting/sensing
 * the card, then do that here and add a child for each set of
 * hardware found.
 *
 * For PNP devices:
 * If the device is always PNP capable then this function can be removed.
 * The ISA PNP system will have automatically added it to the system and
 * so your identify routine needn't do anything.
 *
 * If the device is mentioned in the 'hints' file then this
 * function can be removed. All devices mentioned in the hints
 * file get added as children for probing, whether or not the
 * driver is linked in. So even as a module it MAY still be there.
 * See isa/isahint.c for hints being added in.
 */
static void
${1}_isa_identify (driver_t *driver, device_t parent)
{
	u_int32_t	irq=0;
	u_int32_t	ioport;
	device_t	child;
	int i;

	/*
	 * If we've already got ${UPPER} attached somehow, don't try again.
	 * Maybe it was in the hints file. or it was loaded before.
	 */
	if (device_find_child(parent, "${1}", 0)) {
		printf("${UPPER}: already attached\n");
		return;
	}
/* XXX Look at dev/acpica/acpi_isa.c for use of ISA_ADD_CONFIG() macro. */
/* XXX What is ISA_SET_CONFIG_CALLBACK(parent, child, pnpbios_set_config, 0)? */
	for (i = 0; i < MAXHINTS; i++) {

		ioport = res[i].ioport;
		irq = res[i].irq;
		if ((ioport == 0) && (irq == 0))
			return; /* We've added all our local hints. */

		child = BUS_ADD_CHILD(parent, ISA_ORDER_SPECULATIVE, "${1}", -1);
		bus_set_resource(child, SYS_RES_IOPORT,	0, ioport, NUMPORTS);
		bus_set_resource(child, SYS_RES_IRQ,	0, irq, 1);
		bus_set_resource(child, SYS_RES_DRQ,	0, res[i].drq, 1);
		bus_set_resource(child, SYS_RES_MEMORY,	0, res[i].mem, MEMSIZE);

#if 0
		/*
		 * If we wanted to pretend PNP found it
		 * we could do this, and put matching entries
		 * in the PNP table, but I think it's probably too hacky.
		 * As you see, some people have done it though.
		 * Basically EISA (remember that?) would do this I think.
		 */
		isa_set_vendorid(child, PNP_EISAID("ESS1888"));
		isa_set_logicalid(child, PNP_EISAID("ESS1888"));
#endif
	}
#if 0
	/*
	 * Do some smart probing (e.g. like the lnc driver)
	 * and add a child for each one found.
	 */
#endif

	return;
}
/*
 * The ISA code calls this for each device it knows about,
 * whether via the PNP code or via the hints etc.
 * If the device nas no PNP capabilities, remove all the
 * PNP entries, but keep the call to ISA_PNP_PROBE()
 * As it will guard against accidentally recognising
 * foreign hardware. This is because we will be called to check against
 * ALL PNP hardware.
 */
static int
${1}_isa_probe (device_t device)
{
	int error;
	device_t parent = device_get_parent(device);
	struct ${1}_softc *scp = DEVICE2SOFTC(device);
	u_long	port_start, port_count;

	bzero(scp, sizeof(*scp));
	scp->device = device;

	/*
	 * Check this device for a PNP match in our table.
	 * There are several possible outcomes.
	 * error == 0		We match a PNP.
	 * error == ENXIO,	It is a PNP device but not in our table.
	 * error == ENOENT,	It is not a PNP device.. try heuristic probes.
	 *    -- logic from if_ed_isa.c, added info from isa/isa_if.m:
	 *
	 * If we had a list of devices that we could handle really well,
	 * and a list which we could handle only basic functions, then
	 * we would call this twice, once for each list,
	 * and return a value of '-2' or something if we could
	 * only handle basic functions. This would allow a specific
	 * Widgetplus driver to make a better offer if it knows how to
	 * do all the extended functions. (See non-pnp part for more info).
	 */
	error = ISA_PNP_PROBE(parent, device, ${1}_ids);
	switch (error) {
	case 0:
		/*
		 * We found a PNP device.
		 * Do nothing, as it's all done in attach().
		 */
		break;
	case ENOENT:
		/*
		 * Well it didn't show up in the PNP tables
		 * so look directly at known ports (if we have any)
		 * in case we are looking for an old pre-PNP card.
		 *
		 * Hopefully the  'identify' routine will have picked these
		 * up for us first if they use some proprietary detection
		 * method.
		 *
		 * The ports, irqs etc should come from a 'hints' section
		 * which is read in by code in isa/isahint.c
		 * and kern/subr_bus.c to create resource entries,
		 * or have been added by the 'identify routine above.
		 * Note that HINTS based resource requests have NO
		 * SIZE for the memory or ports requests  (just a base)
		 * so we may need to 'correct' this before we
		 * do any probing.
		 */
		/*
		 * Find out the values of any resources we
		 * need for our dumb probe. Also check we have enough ports
		 * in the request. (could be hints based).
		 * Should probably do the same for memory regions too.
		 */
		error = bus_get_resource(device, SYS_RES_IOPORT, 0,
		    &port_start, &port_count);
		if (port_count != NUMPORTS) {
			bus_set_resource(device, SYS_RES_IOPORT, 0,
			    port_start, NUMPORTS);
		}

		/*
		 * Make a temporary resource reservation.
		 * If we can't get the resources we need then
		 * we need to abort.  Possibly this indicates
		 * the resources were used by another device
		 * in which case the probe would have failed anyhow.
		 */
		if ((error = (${1}_allocate_resources(device)))) {
			error = ENXIO;
			goto errexit;
		}

		/* Dummy heuristic type probe. */
		if (inb(port_start) != EXPECTED_VALUE) {
			/*
			 * It isn't what we hoped, so quit looking for it.
			 */
			error = ENXIO;
		} else {
			u_long membase = bus_get_resource_start(device,
					SYS_RES_MEMORY, 0 /*rid*/);
			u_long memsize;
			/*
			 * If we discover in some way that the device has
			 * XXX bytes of memory window, we can override
			 * or set the memory size in the child resource list.
			 */
			memsize = inb(port_start + 1) * 1024; /* for example */
			error = bus_set_resource(device, SYS_RES_MEMORY,
				/*rid*/0, membase, memsize);
			/*
			 * We found one, return non-positive numbers..
			 * Return -N if we can't handle it, but not well.
			 * Return -2 if we would LIKE the device.
			 * Return -1 if we want it a lot.
			 * Return 0 if we MUST get the device.
			 * This allows drivers to 'bid' for a device.
			 */
			device_set_desc(device, "ACME Widget model 1234");
			error = -1; /* We want it but someone else
					may be even better. */
		}
		/*
		 * Unreserve the resources for now because
		 * another driver may bid for device too.
		 * If we lose the bid, but still hold the resources, we will
		 * effectively have disabled the other driver from getting them
		 * which will result in neither driver getting the device.
		 * We will ask for them again in attach if we win.
		 */
		${1}_deallocate_resources(device);
		break;
	case  ENXIO:
		/* It was PNP but not ours, leave immediately. */
	default:
		error = ENXIO;
	}
errexit:
	return (error);
}

/*
 * Called if the probe succeeded and our bid won the device.
 * We can be destructive here as we know we have the device.
 * This is the first place we can be sure we have a softc structure.
 * You would do ISA specific attach things here, but generically there aren't
 * any (yay new-bus!).
 */
static int
${1}_isa_attach (device_t device)
{
        int	error;
	struct ${1}_softc *scp = DEVICE2SOFTC(device);

        error =  ${1}_attach(device, scp);
        if (error)
                ${1}_isa_detach(device);
        return (error);
}

/*
 * Detach the driver (e.g. module unload),
 * call the bus independent version
 * and undo anything we did in the ISA attach routine.
 */
static int
${1}_isa_detach (device_t device)
{
        int	error;
	struct ${1}_softc *scp = DEVICE2SOFTC(device);

        error =  ${1}_detach(device, scp);
        return (error);
}

/*
 ***************************************
 * PCI Attachment structures and code
 ***************************************
 */

static int	${1}_pci_probe(device_t);
static int	${1}_pci_attach(device_t);
static int	${1}_pci_detach(device_t);

static device_method_t ${1}_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		${1}_pci_probe),
	DEVMETHOD(device_attach,	${1}_pci_attach),
	DEVMETHOD(device_detach,	${1}_pci_detach),
	{ 0, 0 }
};

static driver_t ${1}_pci_driver = {
	"${1}",
	${1}_pci_methods,
	sizeof(struct ${1}_softc),
};

DRIVER_MODULE(${1}, pci, ${1}_pci_driver, ${1}_devclass, 0, 0);
/*
 * Cardbus is a pci bus plus extra, so use the pci driver unless special
 * things need to be done only in the cardbus case.
 */
DRIVER_MODULE(${1}, cardbus, ${1}_pci_driver, ${1}_devclass, 0, 0);

static struct _pcsid
{
	u_int32_t	type;
	const char	*desc;
} pci_ids[] = {
	{ 0x1234abcd,	"ACME PCI Widgetplus"	},
	{ 0x1243fedc,	"Happy moon brand RIPOFFplus"	},
	{ 0x00000000,	NULL					}
};

/*
 * See if this card is specifically mentioned in our list of known devices.
 * Theoretically we might also put in a weak bid for some devices that
 * report themselves to be some generic type of device if we can handle
 * that generic type. (other PCI_XXX calls give that info).
 * This would allow a specific driver to over-ride us.
 *
 * See the comments in the ISA section regarding returning non-positive
 * values from probe routines.
 */
static int
${1}_pci_probe (device_t device)
{
	u_int32_t	type = pci_get_devid(device);
	struct _pcsid	*ep =pci_ids;

	while (ep->type && ep->type != type)
		++ep;
	if (ep->desc) {
		device_set_desc(device, ep->desc);
		return 0; /* If there might be a better driver, return -2 */
	} else
		return ENXIO;
}

static int
${1}_pci_attach(device_t device)
{
        int	error;
	struct ${1}_softc *scp = DEVICE2SOFTC(device);

        error =  ${1}_attach(device, scp);
        if (error)
                ${1}_pci_detach(device);
        return (error);
}

static int
${1}_pci_detach (device_t device)
{
        int	error;
	struct ${1}_softc *scp = DEVICE2SOFTC(device);

        error =  ${1}_detach(device, scp);
        return (error);
}

/*
 ****************************************
 *  Common Attachment sub-functions
 ****************************************
 */
static int
${1}_attach(device_t device, struct ${1}_softc * scp)
{
	device_t parent	= device_get_parent(device);
	int	unit	= device_get_unit(device);

	scp->dev = make_dev(&${1}_cdevsw, 0,
			UID_ROOT, GID_OPERATOR, 0600, "${1}%d", unit);
	scp->dev->si_drv1 = scp;

	if (${1}_allocate_resources(device))
		goto errexit;

	scp->bt = rman_get_bustag(scp->res_ioport);
	scp->bh = rman_get_bushandle(scp->res_ioport);

	/* Register the interrupt handler. */
	/*
	 * The type should be one of:
	 *	INTR_TYPE_TTY
	 *	INTR_TYPE_BIO
	 *	INTR_TYPE_CAM
	 *	INTR_TYPE_NET
	 *	INTR_TYPE_MISC
	 * This will probably change with SMPng.  INTR_TYPE_FAST may be
	 * OR'd into this type to mark the interrupt fast.  However, fast
	 * interrupts cannot be shared at all so special precautions are
	 * necessary when coding fast interrupt routines.
	 */
	if (scp->res_irq) {
		/* Default to the tty mask for registration. */  /* XXX */
		if (BUS_SETUP_INTR(parent, device, scp->res_irq, INTR_TYPE_TTY,
				${1}intr, scp, &scp->intr_cookie) == 0) {
			/* Do something if successful. */
		} else
			goto errexit;
	}

	/*
	 * If we want to access the memory we will need
	 * to know where it was mapped.
	 *
	 * Use of this function is discouraged, however.  You should
	 * be accessing the device with the bus_space API if at all
	 * possible.
	 */
	scp->vaddr = rman_get_virtual(scp->res_memory);
	return 0;

errexit:
	/*
	 * Undo anything we may have done.
	 */
	${1}_detach(device, scp);
	return (ENXIO);
}

static int
${1}_detach(device_t device, struct ${1}_softc *scp)
{
	device_t parent = device_get_parent(device);

	/*
	 * At this point stick a strong piece of wood into the device
	 * to make sure it is stopped safely. The alternative is to
	 * simply REFUSE to detach if it's busy. What you do depends on
	 * your specific situation.
	 *
	 * Sometimes the parent bus will detach you anyway, even if you
	 * are busy.  You must cope with that possibility.  Your hardware
	 * might even already be gone in the case of cardbus or pccard
	 * devices.
	 */
	/* ZAP some register */

	/*
	 * Take our interrupt handler out of the list of handlers
	 * that can handle this irq.
	 */
	if (scp->intr_cookie != NULL) {
		if (BUS_TEARDOWN_INTR(parent, device,
			scp->res_irq, scp->intr_cookie) != 0)
				printf("intr teardown failed.. continuing\n");
		scp->intr_cookie = NULL;
	}

	/*
	 * Deallocate any system resources we may have
	 * allocated on behalf of this driver.
	 */
	scp->vaddr = NULL;
	return ${1}_deallocate_resources(device);
}

static int
${1}_allocate_resources(device_t device)
{
	int error;
	struct ${1}_softc *scp = DEVICE2SOFTC(device);
	int	size = 16; /* SIZE of port range used. */

	scp->res_ioport = bus_alloc_resource(device, SYS_RES_IOPORT,
			&scp->rid_ioport, 0ul, ~0ul, size, RF_ACTIVE);
	if (scp->res_ioport == NULL)
		goto errexit;

	scp->res_irq = bus_alloc_resource(device, SYS_RES_IRQ,
			&scp->rid_irq, 0ul, ~0ul, 1, RF_SHAREABLE|RF_ACTIVE);
	if (scp->res_irq == NULL)
		goto errexit;

	scp->res_drq = bus_alloc_resource(device, SYS_RES_DRQ,
			&scp->rid_drq, 0ul, ~0ul, 1, RF_ACTIVE);
	if (scp->res_drq == NULL)
		goto errexit;

	scp->res_memory = bus_alloc_resource(device, SYS_RES_MEMORY,
			&scp->rid_memory, 0ul, ~0ul, MSIZE, RF_ACTIVE);
	if (scp->res_memory == NULL)
		goto errexit;
	return (0);

errexit:
	error = ENXIO;
	/* Cleanup anything we may have assigned. */
	${1}_deallocate_resources(device);
	return (ENXIO); /* For want of a better idea. */
}

static int
${1}_deallocate_resources(device_t device)
{
	struct ${1}_softc *scp = DEVICE2SOFTC(device);

	if (scp->res_irq != 0) {
		bus_deactivate_resource(device, SYS_RES_IRQ,
			scp->rid_irq, scp->res_irq);
		bus_release_resource(device, SYS_RES_IRQ,
			scp->rid_irq, scp->res_irq);
		scp->res_irq = 0;
	}
	if (scp->res_ioport != 0) {
		bus_deactivate_resource(device, SYS_RES_IOPORT,
			scp->rid_ioport, scp->res_ioport);
		bus_release_resource(device, SYS_RES_IOPORT,
			scp->rid_ioport, scp->res_ioport);
		scp->res_ioport = 0;
	}
	if (scp->res_memory != 0) {
		bus_deactivate_resource(device, SYS_RES_MEMORY,
			scp->rid_memory, scp->res_memory);
		bus_release_resource(device, SYS_RES_MEMORY,
			scp->rid_memory, scp->res_memory);
		scp->res_memory = 0;
	}
	if (scp->res_drq != 0) {
		bus_deactivate_resource(device, SYS_RES_DRQ,
			scp->rid_drq, scp->res_drq);
		bus_release_resource(device, SYS_RES_DRQ,
			scp->rid_drq, scp->res_drq);
		scp->res_drq = 0;
	}
	if (scp->dev)
		destroy_dev(scp->dev);
	return (0);
}

static void
${1}intr(void *arg)
{
	struct ${1}_softc *scp = (struct ${1}_softc *) arg;

	/*
	 * Well we got an interrupt, now what?
	 *
	 * Make sure that the interrupt routine will always terminate,
	 * even in the face of "bogus" data from the card.
	 */
	(void)scp; /* Delete this line after using scp. */
	return;
}

static int
${1}ioctl (struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	struct ${1}_softc *scp = DEV2SOFTC(dev);

	(void)scp; /* Delete this line after using scp. */
	switch (cmd) {
	case DHIOCRESET:
		/* Whatever resets it. */
#if 0
		${UPPER}_OUTB(SOME_PORT, 0xff);
#endif
		break;
	default:
		return ENXIO;
	}
	return (0);
}
/*
 * You also need read, write, open, close routines.
 * This should get you started.
 */
static int
${1}open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct ${1}_softc *scp = DEV2SOFTC(dev);

	/*
	 * Do processing.
	 */
	(void)scp; /* Delete this line after using scp. */
	return (0);
}

static int
${1}close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct ${1}_softc *scp = DEV2SOFTC(dev);

	/*
	 * Do processing.
	 */
	(void)scp; /* Delete this line after using scp. */
	return (0);
}

static int
${1}read(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct ${1}_softc *scp = DEV2SOFTC(dev);
	int	 toread;

	/*
	 * Do processing.
	 * Read from buffer.
	 */
	(void)scp; /* Delete this line after using scp. */
	toread = (min(uio->uio_resid, sizeof(scp->buffer)));
	return(uiomove(scp->buffer, toread, uio));
}

static int
${1}write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct ${1}_softc *scp = DEV2SOFTC(dev);
	int	towrite;

	/*
	 * Do processing.
	 * Write to buffer.
	 */
	(void)scp; /* Delete this line after using scp. */
	towrite = (min(uio->uio_resid, sizeof(scp->buffer)));
	return(uiomove(scp->buffer, towrite, uio));
}

static int
${1}mmap(struct cdev *dev, vm_offset_t offset, vm_paddr_t *paddr, int nprot)
{
	struct ${1}_softc *scp = DEV2SOFTC(dev);

	/*
	 * Given a byte offset into your device, return the PHYSICAL
	 * page number that it would map to.
	 */
	(void)scp; /* Delete this line after using scp. */
#if 0	/* If we had a frame buffer or whatever... do this. */
	if (offset > FRAMEBUFFERSIZE - PAGE_SIZE)
		return (-1);
	return i386_btop((FRAMEBASE + offset));
#else
	return (-1);
#endif
}

static int
${1}poll(struct cdev *dev, int which, struct thread *td)
{
	struct ${1}_softc *scp = DEV2SOFTC(dev);

	/*
	 * Do processing.
	 */
	(void)scp; /* Delete this line after using scp. */
	return (0); /* This is the wrong value I'm sure. */
}

DONE

cat >${TOP}/sys/${1}io.h <<DONE
/*
 * Definitions needed to access the ${1} device (ioctls etc)
 * see mtio.h, ioctl.h as examples.
 */
#ifndef SYS_DHIO_H
#define SYS_DHIO_H

#ifndef KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

/*
 * Define an ioctl here.
 */
#define DHIOCRESET _IO('D', 0) /* Reset the ${1} device. */
#endif
DONE

if [ ! -d ${TOP}/modules/${1} ]; then
	mkdir -p ${TOP}/modules/${1}
fi

cat >${TOP}/modules/${1}/Makefile <<DONE
#	${UPPER} Loadable Kernel Module
#
# \$${RCS_KEYWORD}: $

.PATH:  \${.CURDIR}/../../dev/${1}
KMOD    = ${1}
SRCS    = ${1}.c
SRCS    += opt_inet.h device_if.h bus_if.h pci_if.h isa_if.h

# You may need to do this is your device is an if_xxx driver.
opt_inet.h:
	echo "#define INET 1" > opt_inet.h

.include <bsd.kmod.mk>
DONE

echo -n "Do you want to build the '${1}' module? [Y]"
read VAL
if [ "-z" "$VAL" ]; then
	VAL=YES
fi
case ${VAL} in
[yY]*)
	(cd ${TOP}/modules/${1}; make depend; make )
	;;
*)
#	exit
	;;
esac

echo ""
echo -n "Do you want to build the '${UPPER}' kernel? [Y]"
read VAL
if [ "-z" "$VAL" ]; then
	VAL=YES
fi
case ${VAL} in
[yY]*)
	(
	 cd ${TOP}/i386/conf; \
	 config ${UPPER}; \
	 cd ${TOP}/i386/compile/${UPPER}; \
	 make depend; \
	 make; \
	)
	;;
*)
#	exit
	;;
esac

#--------------end of script---------------
#
# Edit to your taste...
#
#
