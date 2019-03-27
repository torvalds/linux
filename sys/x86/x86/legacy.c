/*-
 * Copyright 1998 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 * 
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This code implements a system driver for legacy systems that do not
 * support ACPI or when ACPI support is not present in the kernel.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/pcpu.h>
#include <sys/rman.h>
#include <sys/smp.h>

#include <machine/clock.h>
#include <machine/resource.h>
#include <x86/legacyvar.h>

static MALLOC_DEFINE(M_LEGACYDEV, "legacydrv", "legacy system device");
struct legacy_device {
	int	lg_pcibus;
	int	lg_pcislot;
	int	lg_pcifunc;
};

#define DEVTOAT(dev)	((struct legacy_device *)device_get_ivars(dev))

static	int legacy_probe(device_t);
static	int legacy_attach(device_t);
static	int legacy_print_child(device_t, device_t);
static device_t legacy_add_child(device_t bus, u_int order, const char *name,
				int unit);
static	int legacy_read_ivar(device_t, device_t, int, uintptr_t *);
static	int legacy_write_ivar(device_t, device_t, int, uintptr_t);

static device_method_t legacy_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		legacy_probe),
	DEVMETHOD(device_attach,	legacy_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	legacy_print_child),
	DEVMETHOD(bus_add_child,	legacy_add_child),
	DEVMETHOD(bus_read_ivar,	legacy_read_ivar),
	DEVMETHOD(bus_write_ivar,	legacy_write_ivar),
	DEVMETHOD(bus_alloc_resource,	bus_generic_alloc_resource),
	DEVMETHOD(bus_adjust_resource,	bus_generic_adjust_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	{ 0, 0 }
};

static driver_t legacy_driver = {
	"legacy",
	legacy_methods,
	1,			/* no softc */
};
static devclass_t legacy_devclass;

DRIVER_MODULE(legacy, nexus, legacy_driver, legacy_devclass, 0, 0);

static int
legacy_probe(device_t dev)
{

	device_set_desc(dev, "legacy system");
	device_quiet(dev);
	return (0);
}

static int
legacy_attach(device_t dev)
{
	device_t child;

	/*
	 * Let our child drivers identify any child devices that they
	 * can find.  Once that is done attach any devices that we
	 * found.
	 */
	bus_generic_probe(dev);
	bus_generic_attach(dev);

	/*
	 * If we didn't see ISA on a pci bridge, create some
	 * connection points now so they show up "on motherboard".
	 */
	if (!devclass_get_device(devclass_find("isa"), 0)) {
		child = BUS_ADD_CHILD(dev, 0, "isa", 0);
		if (child == NULL)
			panic("legacy_attach isa");
		device_probe_and_attach(child);
	}

	return 0;
}

static int
legacy_print_child(device_t bus, device_t child)
{
	struct legacy_device *atdev = DEVTOAT(child);
	int retval = 0;

	retval += bus_print_child_header(bus, child);
	if (atdev->lg_pcibus != -1)
		retval += printf(" pcibus %d", atdev->lg_pcibus);
	retval += printf(" on motherboard\n");	/* XXX "motherboard", ick */

	return (retval);
}

static device_t
legacy_add_child(device_t bus, u_int order, const char *name, int unit)
{
	device_t child;
	struct legacy_device *atdev;

	atdev = malloc(sizeof(struct legacy_device), M_LEGACYDEV,
	    M_NOWAIT | M_ZERO);
	if (atdev == NULL)
		return(NULL);
	atdev->lg_pcibus = -1;
	atdev->lg_pcislot = -1;
	atdev->lg_pcifunc = -1;

	child = device_add_child_ordered(bus, order, name, unit);
	if (child == NULL)
		free(atdev, M_LEGACYDEV);
	else
		/* should we free this in legacy_child_detached? */
		device_set_ivars(child, atdev);

	return (child);
}

static int
legacy_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct legacy_device *atdev = DEVTOAT(child);

	switch (which) {
	case LEGACY_IVAR_PCIDOMAIN:
		*result = 0;
		break;
	case LEGACY_IVAR_PCIBUS:
		*result = atdev->lg_pcibus;
		break;
	case LEGACY_IVAR_PCISLOT:
		*result = atdev->lg_pcislot;
		break;
	case LEGACY_IVAR_PCIFUNC:
		*result = atdev->lg_pcifunc;
		break;
	default:
		return ENOENT;
	}
	return 0;
}
	

static int
legacy_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{
	struct legacy_device *atdev = DEVTOAT(child);

	switch (which) {
	case LEGACY_IVAR_PCIDOMAIN:
		return EINVAL;
	case LEGACY_IVAR_PCIBUS:
		atdev->lg_pcibus = value;
		break;
	case LEGACY_IVAR_PCISLOT:
		atdev->lg_pcislot = value;
		break;
	case LEGACY_IVAR_PCIFUNC:
		atdev->lg_pcifunc = value;
		break;
	default:
		return ENOENT;
	}
	return 0;
}

/*
 * Legacy CPU attachment when ACPI is not available.  Drivers like
 * cpufreq(4) hang off this.
 */
static void	cpu_identify(driver_t *driver, device_t parent);
static int	cpu_read_ivar(device_t dev, device_t child, int index,
		    uintptr_t *result);
static device_t cpu_add_child(device_t bus, u_int order, const char *name,
		    int unit);
static struct resource_list *cpu_get_rlist(device_t dev, device_t child);

struct cpu_device {
	struct resource_list cd_rl;
	struct pcpu *cd_pcpu;
};

static device_method_t cpu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	cpu_identify),
	DEVMETHOD(device_probe,		bus_generic_probe),
	DEVMETHOD(device_attach,	bus_generic_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_add_child,	cpu_add_child),
	DEVMETHOD(bus_read_ivar,	cpu_read_ivar),
	DEVMETHOD(bus_get_resource_list, cpu_get_rlist),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_set_resource,	bus_generic_rl_set_resource),
	DEVMETHOD(bus_alloc_resource,	bus_generic_rl_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_rl_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	DEVMETHOD_END
};

static driver_t cpu_driver = {
	"cpu",
	cpu_methods,
	1,		/* no softc */
};
static devclass_t cpu_devclass;
DRIVER_MODULE(cpu, legacy, cpu_driver, cpu_devclass, 0, 0);

static void
cpu_identify(driver_t *driver, device_t parent)
{
	device_t child;
	int i;

	/*
	 * Attach a cpuX device for each CPU.  We use an order of 150
	 * so that these devices are attached after the Host-PCI
	 * bridges (which are added at order 100).
	 */
	CPU_FOREACH(i) {
		child = BUS_ADD_CHILD(parent, 150, "cpu", i);
		if (child == NULL)
			panic("legacy_attach cpu");
	}
}

static device_t
cpu_add_child(device_t bus, u_int order, const char *name, int unit)
{
	struct cpu_device *cd;
	device_t child;
	struct pcpu *pc;

	if ((cd = malloc(sizeof(*cd), M_DEVBUF, M_NOWAIT | M_ZERO)) == NULL)
		return (NULL);

	resource_list_init(&cd->cd_rl);
	pc = pcpu_find(device_get_unit(bus));
	cd->cd_pcpu = pc;

	child = device_add_child_ordered(bus, order, name, unit);
	if (child != NULL) {
		pc->pc_device = child;
		device_set_ivars(child, cd);
	} else
		free(cd, M_DEVBUF);
	return (child);
}

static struct resource_list *
cpu_get_rlist(device_t dev, device_t child)
{
	struct cpu_device *cpdev;

	cpdev = device_get_ivars(child);
	return (&cpdev->cd_rl);
}

static int
cpu_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	struct cpu_device *cpdev;

	switch (index) {
	case CPU_IVAR_PCPU:
		cpdev = device_get_ivars(child);
		*result = (uintptr_t)cpdev->cd_pcpu;
		break;
	case CPU_IVAR_NOMINAL_MHZ:
		if (tsc_is_invariant) {
			*result = (uintptr_t)(atomic_load_acq_64(&tsc_freq) /
			    1000000);
			break;
		}
		/* FALLTHROUGH */
	default:
		return (ENOENT);
	}
	return (0);
}
