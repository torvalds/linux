/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Bruno Ducrot
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
 */

/*
 * This driver is based upon information found by examining speedstep-0.5
 * from Marc Lehman, which includes all the reverse engineering effort of
 * Malik Martin (function 1 and 2 of the GSI).
 *
 * The correct way for the OS to take ownership from the BIOS was found by
 * Hiroshi Miura (function 0 of the GSI).
 *
 * Finally, the int 15h call interface was (partially) documented by Intel.
 *
 * Many thanks to Jon Noack for testing and debugging this driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/vm86.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include "cpufreq_if.h"

#if 0
#define DPRINT(dev, x...)	device_printf(dev, x)
#else
#define DPRINT(dev, x...)
#endif

struct smist_softc {
	device_t		 dev;
	int			 smi_cmd;
	int			 smi_data;
	int			 command;
	int			 flags;
	struct cf_setting	 sets[2];	/* Only two settings. */
};

static char smist_magic[] = "Copyright (c) 1999 Intel Corporation";

static void	smist_identify(driver_t *driver, device_t parent);
static int	smist_probe(device_t dev);
static int	smist_attach(device_t dev);
static int	smist_detach(device_t dev);
static int	smist_settings(device_t dev, struct cf_setting *sets,
		    int *count);
static int	smist_set(device_t dev, const struct cf_setting *set);
static int	smist_get(device_t dev, struct cf_setting *set);
static int	smist_type(device_t dev, int *type);

static device_method_t smist_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	smist_identify),
	DEVMETHOD(device_probe,		smist_probe),
	DEVMETHOD(device_attach,	smist_attach),
	DEVMETHOD(device_detach,	smist_detach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	smist_set),
	DEVMETHOD(cpufreq_drv_get,	smist_get),
	DEVMETHOD(cpufreq_drv_type,	smist_type),
	DEVMETHOD(cpufreq_drv_settings,	smist_settings),

	{0, 0}
};

static driver_t smist_driver = {
	"smist", smist_methods, sizeof(struct smist_softc)
};
static devclass_t smist_devclass;
DRIVER_MODULE(smist, cpu, smist_driver, smist_devclass, 0, 0);

struct piix4_pci_device {
	uint16_t		 vendor;
	uint16_t		 device;
	char			*desc;
};

static struct piix4_pci_device piix4_pci_devices[] = {
	{0x8086, 0x7113, "Intel PIIX4 ISA bridge"},
	{0x8086, 0x719b, "Intel PIIX4 ISA bridge (embedded in MX440 chipset)"},

	{0, 0, NULL},
};

#define SET_OWNERSHIP		0
#define GET_STATE		1
#define SET_STATE		2

static int
int15_gsic_call(int *sig, int *smi_cmd, int *command, int *smi_data, int *flags)
{
	struct vm86frame vmf;

	bzero(&vmf, sizeof(vmf));
	vmf.vmf_eax = 0x0000E980;	/* IST support */
	vmf.vmf_edx = 0x47534943;	/* 'GSIC' in ASCII */
	vm86_intcall(0x15, &vmf);

	if (vmf.vmf_eax == 0x47534943) {
		*sig = vmf.vmf_eax;
		*smi_cmd = vmf.vmf_ebx & 0xff;
		*command = (vmf.vmf_ebx >> 16) & 0xff;
		*smi_data = vmf.vmf_ecx;
		*flags = vmf.vmf_edx;
	} else {
		*sig = -1;
		*smi_cmd = -1;
		*command = -1;
		*smi_data = -1;
		*flags = -1;
	}

	return (0);
}

/* Temporary structure to hold mapped page and status. */
struct set_ownership_data {
	int	smi_cmd;
	int	command;
	int	result;
	void	*buf;
};

/* Perform actual SMI call to enable SpeedStep. */
static void
set_ownership_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct set_ownership_data *data;

	data = arg;
	if (error) {
		data->result = error;
		return;
	}

	/* Copy in the magic string and send it by writing to the SMI port. */
	strlcpy(data->buf, smist_magic, PAGE_SIZE);
	__asm __volatile(
	    "movl $-1, %%edi\n\t"
	    "out %%al, (%%dx)\n"
	    : "=D" (data->result)
	    : "a" (data->command),
	      "b" (0),
	      "c" (0),
	      "d" (data->smi_cmd),
	      "S" ((uint32_t)segs[0].ds_addr)
	);
}

static int
set_ownership(device_t dev)
{
	struct smist_softc *sc;
	struct set_ownership_data cb_data;
	bus_dma_tag_t tag;
	bus_dmamap_t map;

	/*
	 * Specify the region to store the magic string.  Since its address is
	 * passed to the BIOS in a 32-bit register, we have to make sure it is
	 * located in a physical page below 4 GB (i.e., for PAE.)
	 */
	sc = device_get_softc(dev);
	if (bus_dma_tag_create(/*parent*/ NULL,
	    /*alignment*/ PAGE_SIZE, /*no boundary*/ 0,
	    /*lowaddr*/ BUS_SPACE_MAXADDR_32BIT, /*highaddr*/ BUS_SPACE_MAXADDR,
	    NULL, NULL, /*maxsize*/ PAGE_SIZE, /*segments*/ 1,
	    /*maxsegsize*/ PAGE_SIZE, 0, busdma_lock_mutex, &Giant,
	    &tag) != 0) {
		device_printf(dev, "can't create mem tag\n");
		return (ENXIO);
	}
	if (bus_dmamem_alloc(tag, &cb_data.buf, BUS_DMA_NOWAIT, &map) != 0) {
		bus_dma_tag_destroy(tag);
		device_printf(dev, "can't alloc mapped mem\n");
		return (ENXIO);
	}

	/* Load the physical page map and take ownership in the callback. */
	cb_data.smi_cmd = sc->smi_cmd;
	cb_data.command = sc->command;
	if (bus_dmamap_load(tag, map, cb_data.buf, PAGE_SIZE, set_ownership_cb,
	    &cb_data, BUS_DMA_NOWAIT) != 0) {
		bus_dmamem_free(tag, cb_data.buf, map);
		bus_dma_tag_destroy(tag);
		device_printf(dev, "can't load mem\n");
		return (ENXIO);
	}
	DPRINT(dev, "taking ownership over BIOS return %d\n", cb_data.result);
	bus_dmamap_unload(tag, map);
	bus_dmamem_free(tag, cb_data.buf, map);
	bus_dma_tag_destroy(tag);
	return (cb_data.result ? ENXIO : 0);
}

static int
getset_state(struct smist_softc *sc, int *state, int function)
{
	int new_state;
	int result;
	int eax;

	if (!sc)
		return (ENXIO);

	if (function != GET_STATE && function != SET_STATE)
		return (EINVAL);

	DPRINT(sc->dev, "calling GSI\n");

	__asm __volatile(
	     "movl $-1, %%edi\n\t"
	     "out %%al, (%%dx)\n"
	   : "=a" (eax),
	     "=b" (new_state),
	     "=D" (result)
	   : "a" (sc->command),
	     "b" (function),
	     "c" (*state),
	     "d" (sc->smi_cmd)
	);

	DPRINT(sc->dev, "GSI returned: eax %.8x ebx %.8x edi %.8x\n",
	    eax, new_state, result);

	*state = new_state & 1;

	switch (function) {
	case GET_STATE:
		if (eax)
			return (ENXIO);
		break;
	case SET_STATE:
		if (result)
			return (ENXIO);
		break;
	}
	return (0);
}

static void
smist_identify(driver_t *driver, device_t parent)
{
	struct piix4_pci_device *id;
	device_t piix4 = NULL;

	if (resource_disabled("ichst", 0))
		return;

	/* Check for a supported processor */
	if (cpu_vendor_id != CPU_VENDOR_INTEL)
		return;
	switch (cpu_id & 0xff0) {
	case 0x680:	/* Pentium III [coppermine] */
	case 0x6a0:	/* Pentium III [Tualatin] */
		break;
	default:
		return;
	}

	/* Check for a supported PCI-ISA bridge */
	for (id = piix4_pci_devices; id->desc != NULL; ++id) {
		if ((piix4 = pci_find_device(id->vendor, id->device)) != NULL)
			break;
	}
	if (!piix4)
		return;

	if (bootverbose)
		printf("smist: found supported isa bridge %s\n", id->desc);

	if (device_find_child(parent, "smist", -1) != NULL)
		return;
	if (BUS_ADD_CHILD(parent, 30, "smist", -1) == NULL)
		device_printf(parent, "smist: add child failed\n");
}

static int
smist_probe(device_t dev)
{
	struct smist_softc *sc;
	device_t ichss_dev, perf_dev;
	int sig, smi_cmd, command, smi_data, flags;
	int type;
	int rv;

	if (resource_disabled("smist", 0))
		return (ENXIO);

	sc = device_get_softc(dev);

	/*
	 * If the ACPI perf or ICH SpeedStep drivers have attached and not
	 * just offering info, let them manage things.
	 */
	perf_dev = device_find_child(device_get_parent(dev), "acpi_perf", -1);
	if (perf_dev && device_is_attached(perf_dev)) {
		rv = CPUFREQ_DRV_TYPE(perf_dev, &type);
		if (rv == 0 && (type & CPUFREQ_FLAG_INFO_ONLY) == 0)
			return (ENXIO);
	}
	ichss_dev = device_find_child(device_get_parent(dev), "ichss", -1);
	if (ichss_dev && device_is_attached(ichss_dev))
		return (ENXIO);

	int15_gsic_call(&sig, &smi_cmd, &command, &smi_data, &flags);
	if (bootverbose)
		device_printf(dev, "sig %.8x smi_cmd %.4x command %.2x "
		    "smi_data %.4x flags %.8x\n",
		    sig, smi_cmd, command, smi_data, flags);

	if (sig != -1) {
		sc->smi_cmd = smi_cmd;
		sc->smi_data = smi_data;

		/*
		 * Sometimes int 15h 'GSIC' returns 0x80 for command, when
		 * it is actually 0x82.  The Windows driver will overwrite
		 * this value given by the registry.
		 */
		if (command == 0x80) {
			device_printf(dev,
			    "GSIC returned cmd 0x80, should be 0x82\n");
			command = 0x82;
		}
		sc->command = (sig & 0xffffff00) | (command & 0xff);
		sc->flags = flags;
	} else {
		/* Give some default values */
		sc->smi_cmd = 0xb2;
		sc->smi_data = 0xb3;
		sc->command = 0x47534982;
		sc->flags = 0;
	}

	device_set_desc(dev, "SpeedStep SMI");

	return (-1500);
}

static int
smist_attach(device_t dev)
{
	struct smist_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* If we can't take ownership over BIOS, then bail out */
	if (set_ownership(dev) != 0)
		return (ENXIO);

	/* Setup some defaults for our exported settings. */
	sc->sets[0].freq = CPUFREQ_VAL_UNKNOWN;
	sc->sets[0].volts = CPUFREQ_VAL_UNKNOWN;
	sc->sets[0].power = CPUFREQ_VAL_UNKNOWN;
	sc->sets[0].lat = 1000;
	sc->sets[0].dev = dev;
	sc->sets[1] = sc->sets[0];

	cpufreq_register(dev);

	return (0);
}

static int
smist_detach(device_t dev)
{

	return (cpufreq_unregister(dev));
}

static int
smist_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct smist_softc *sc;
	struct cf_setting set;
	int first, i;

	if (sets == NULL || count == NULL)
		return (EINVAL);
	if (*count < 2) {
		*count = 2;
		return (E2BIG);
	}
	sc = device_get_softc(dev);

	/*
	 * Estimate frequencies for both levels, temporarily switching to
	 * the other one if we haven't calibrated it yet.
	 */
	for (i = 0; i < 2; i++) {
		if (sc->sets[i].freq == CPUFREQ_VAL_UNKNOWN) {
			first = (i == 0) ? 1 : 0;
			smist_set(dev, &sc->sets[i]);
			smist_get(dev, &set);
			smist_set(dev, &sc->sets[first]);
		}
	}

	bcopy(sc->sets, sets, sizeof(sc->sets));
	*count = 2;

	return (0);
}

static int
smist_set(device_t dev, const struct cf_setting *set)
{
	struct smist_softc *sc;
	int rv, state, req_state, try;

	/* Look up appropriate bit value based on frequency. */
	sc = device_get_softc(dev);
	if (CPUFREQ_CMP(set->freq, sc->sets[0].freq))
		req_state = 0;
	else if (CPUFREQ_CMP(set->freq, sc->sets[1].freq))
		req_state = 1;
	else
		return (EINVAL);

	DPRINT(dev, "requested setting %d\n", req_state);

	rv = getset_state(sc, &state, GET_STATE);
	if (state == req_state)
		return (0);

	try = 3;
	do {
		rv = getset_state(sc, &req_state, SET_STATE);

		/* Sleep for 200 microseconds.  This value is just a guess. */
		if (rv)
			DELAY(200);
	} while (rv && --try);
	DPRINT(dev, "set_state return %d, tried %d times\n",
	    rv, 4 - try);

	return (rv);
}

static int
smist_get(device_t dev, struct cf_setting *set)
{
	struct smist_softc *sc;
	uint64_t rate;
	int state;
	int rv;

	sc = device_get_softc(dev);
	rv = getset_state(sc, &state, GET_STATE);
	if (rv != 0)
		return (rv);

	/* If we haven't changed settings yet, estimate the current value. */
	if (sc->sets[state].freq == CPUFREQ_VAL_UNKNOWN) {
		cpu_est_clockrate(0, &rate);
		sc->sets[state].freq = rate / 1000000;
		DPRINT(dev, "get calibrated new rate of %d\n",
		    sc->sets[state].freq);
	}
	*set = sc->sets[state];

	return (0);
}

static int
smist_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_ABSOLUTE;
	return (0);
}
