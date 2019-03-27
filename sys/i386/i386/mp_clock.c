/*-
 * SPDX-License-Identifier: Beerware
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * Just when we thought life were beautiful, reality pops its grim face over
 * the edge again:
 *
 * ] 20. ACPI Timer Errata
 * ]
 * ]   Problem: The power management timer may return improper result when
 * ]   read. Although the timer value settles properly after incrementing,
 * ]   while incrementing there is a 3nS window every 69.8nS where the
 * ]   timer value is indeterminate (a 4.2% chance that the data will be
 * ]   incorrect when read). As a result, the ACPI free running count up
 * ]   timer specification is violated due to erroneous reads.  Implication:
 * ]   System hangs due to the "inaccuracy" of the timer when used by
 * ]   software for time critical events and delays.
 * ] 
 * ] Workaround: Read the register twice and compare.
 * ] Status: This will not be fixed in the PIIX4 or PIIX4E.
 *
 * The counter is in other words not latched to the PCI bus clock when
 * read.  Notice the workaround isn't:  We need to read until we have
 * three monotonic samples and then use the middle one, otherwise we are
 * not protected against the fact that the bits can be wrong in two
 * directions.  If we only cared about monosity two reads would be enough.
 */

/* #include "opt_bus.h" */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timetc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

static unsigned piix_get_timecount(struct timecounter *tc);

static u_int32_t piix_timecounter_address;
static u_int piix_freq = 14318182/4;

static struct timecounter piix_timecounter = {
	piix_get_timecount,	/* get_timecount */
	0,			/* no poll_pps */
	0xffffff,		/* counter_mask */
	0,			/* frequency */
	"PIIX"			/* name */
};


static int
sysctl_machdep_piix_freq(SYSCTL_HANDLER_ARGS)
{
	int error;
	u_int freq;

	if (piix_timecounter.tc_frequency == 0)
		return (EOPNOTSUPP);
	freq = piix_freq;
	error = sysctl_handle_int(oidp, &freq, 0, req);
	if (error == 0 && req->newptr != NULL) {
		piix_freq = freq;
		piix_timecounter.tc_frequency = piix_freq;
	}
	return (error);
}

SYSCTL_PROC(_machdep, OID_AUTO, piix_freq, CTLTYPE_INT | CTLFLAG_RW,
    0, sizeof(u_int), sysctl_machdep_piix_freq, "I", "");

static unsigned
piix_get_timecount(struct timecounter *tc)
{
	unsigned u1, u2, u3;

	u2 = inl(piix_timecounter_address);
	u3 = inl(piix_timecounter_address);
	do {
		u1 = u2;
		u2 = u3;
		u3 = inl(piix_timecounter_address);
	} while (u1 > u2 || u2 > u3);
	return (u2);
}

static int
piix_probe(device_t dev)
{
	u_int32_t d;

	if (devclass_get_device(devclass_find("acpi"), 0) != NULL)
		return (ENXIO);
	switch (pci_get_devid(dev)) {
	case 0x71138086:
		device_set_desc(dev, "PIIX Timecounter");
		break;
	default:
		return (ENXIO);
	}

	d = pci_read_config(dev, PCIR_COMMAND, 2);
	if (!(d & PCIM_CMD_PORTEN)) {
		device_printf(dev, "PIIX I/O space not mapped\n");
		return (ENXIO);
	}
	return (0);
}

static int
piix_attach(device_t dev)
{
	u_int32_t d;

	d = pci_read_config(dev, 0x40, 4);
	piix_timecounter_address = (d & 0xffc0) + 8;
	piix_timecounter.tc_frequency = piix_freq;
	tc_init(&piix_timecounter);
	return (0);
}

static device_method_t piix_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		piix_probe),
	DEVMETHOD(device_attach,	piix_attach),
	{ 0, 0 }
};

static driver_t piix_driver = {
	"piix",
	piix_methods,
	1,
};

static devclass_t piix_devclass;

DRIVER_MODULE(piix, pci, piix_driver, piix_devclass, 0, 0);
