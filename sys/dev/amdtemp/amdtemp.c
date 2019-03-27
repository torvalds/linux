/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, 2009 Rui Paulo <rpaulo@FreeBSD.org>
 * Copyright (c) 2009 Norikatsu Shigemura <nork@FreeBSD.org>
 * Copyright (c) 2009-2012 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
 * Copyright (c) 2017-2019 Conrad Meyer <cem@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for the AMD CPU on-die thermal sensors.
 * Initially based on the k8temp Linux driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <machine/cpufunc.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

#include <dev/pci/pcivar.h>
#include <x86/pci_cfgreg.h>

#include <dev/amdsmn/amdsmn.h>

typedef enum {
	CORE0_SENSOR0,
	CORE0_SENSOR1,
	CORE1_SENSOR0,
	CORE1_SENSOR1,
	CORE0,
	CORE1
} amdsensor_t;

struct amdtemp_softc {
	int		sc_ncores;
	int		sc_ntemps;
	int		sc_flags;
#define	AMDTEMP_FLAG_CS_SWAP	0x01	/* ThermSenseCoreSel is inverted. */
#define	AMDTEMP_FLAG_CT_10BIT	0x02	/* CurTmp is 10-bit wide. */
#define	AMDTEMP_FLAG_ALT_OFFSET	0x04	/* CurTmp starts at -28C. */
	int32_t		sc_offset;
	int32_t		(*sc_gettemp)(device_t, amdsensor_t);
	struct sysctl_oid *sc_sysctl_cpu[MAXCPU];
	struct intr_config_hook sc_ich;
	device_t	sc_smn;
};

/*
 * N.B. The numbers in macro names below are significant and represent CPU
 * family and model numbers.  Do not make up fictitious family or model numbers
 * when adding support for new devices.
 */
#define	VENDORID_AMD		0x1022
#define	DEVICEID_AMD_MISC0F	0x1103
#define	DEVICEID_AMD_MISC10	0x1203
#define	DEVICEID_AMD_MISC11	0x1303
#define	DEVICEID_AMD_MISC14	0x1703
#define	DEVICEID_AMD_MISC15	0x1603
#define	DEVICEID_AMD_MISC15_M10H	0x1403
#define	DEVICEID_AMD_MISC15_M30H	0x141d
#define	DEVICEID_AMD_MISC15_M60H_ROOT	0x1576
#define	DEVICEID_AMD_MISC16	0x1533
#define	DEVICEID_AMD_MISC16_M30H	0x1583
#define	DEVICEID_AMD_HOSTB17H_ROOT	0x1450
#define	DEVICEID_AMD_HOSTB17H_M10H_ROOT	0x15d0

static const struct amdtemp_product {
	uint16_t	amdtemp_vendorid;
	uint16_t	amdtemp_deviceid;
	/*
	 * 0xFC register is only valid on the D18F3 PCI device; SMN temp
	 * drivers do not attach to that device.
	 */
	bool		amdtemp_has_cpuid;
} amdtemp_products[] = {
	{ VENDORID_AMD,	DEVICEID_AMD_MISC0F, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC10, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC11, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC14, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC15, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC15_M10H, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC15_M30H, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC15_M60H_ROOT, false },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC16, true },
	{ VENDORID_AMD,	DEVICEID_AMD_MISC16_M30H, true },
	{ VENDORID_AMD,	DEVICEID_AMD_HOSTB17H_ROOT, false },
	{ VENDORID_AMD,	DEVICEID_AMD_HOSTB17H_M10H_ROOT, false },
};

/*
 * Reported Temperature Control Register, family 0Fh-15h (some models), 16h.
 */
#define	AMDTEMP_REPTMP_CTRL	0xa4

#define	AMDTEMP_REPTMP10H_CURTMP_MASK	0x7ff
#define	AMDTEMP_REPTMP10H_CURTMP_SHIFT	21
#define	AMDTEMP_REPTMP10H_TJSEL_MASK	0x3
#define	AMDTEMP_REPTMP10H_TJSEL_SHIFT	16

/*
 * Reported Temperature, Family 15h, M60+
 *
 * Same register bit definitions as other Family 15h CPUs, but access is
 * indirect via SMN, like Family 17h.
 */
#define	AMDTEMP_15H_M60H_REPTMP_CTRL	0xd8200ca4

/*
 * Reported Temperature, Family 17h
 *
 * According to AMD OSRR for 17H, section 4.2.1, bits 31-21 of this register
 * provide the current temp.  bit 19, when clear, means the temp is reported in
 * a range 0.."225C" (probable typo for 255C), and when set changes the range
 * to -49..206C.
 */
#define	AMDTEMP_17H_CUR_TMP		0x59800
#define	AMDTEMP_17H_CUR_TMP_RANGE_SEL	(1 << 19)

/*
 * AMD temperature range adjustment, in deciKelvins (i.e., 49.0 Celsius).
 */
#define	AMDTEMP_CURTMP_RANGE_ADJUST	490

/*
 * Thermaltrip Status Register (Family 0Fh only)
 */
#define	AMDTEMP_THERMTP_STAT	0xe4
#define	AMDTEMP_TTSR_SELCORE	0x04
#define	AMDTEMP_TTSR_SELSENSOR	0x40

/*
 * DRAM Configuration High Register
 */
#define	AMDTEMP_DRAM_CONF_HIGH	0x94	/* Function 2 */
#define	AMDTEMP_DRAM_MODE_DDR3	0x0100

/*
 * CPU Family/Model Register
 */
#define	AMDTEMP_CPUID		0xfc

/*
 * Device methods.
 */
static void 	amdtemp_identify(driver_t *driver, device_t parent);
static int	amdtemp_probe(device_t dev);
static int	amdtemp_attach(device_t dev);
static void	amdtemp_intrhook(void *arg);
static int	amdtemp_detach(device_t dev);
static int32_t	amdtemp_gettemp0f(device_t dev, amdsensor_t sensor);
static int32_t	amdtemp_gettemp(device_t dev, amdsensor_t sensor);
static int32_t	amdtemp_gettemp15hm60h(device_t dev, amdsensor_t sensor);
static int32_t	amdtemp_gettemp17h(device_t dev, amdsensor_t sensor);
static int	amdtemp_sysctl(SYSCTL_HANDLER_ARGS);

static device_method_t amdtemp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	amdtemp_identify),
	DEVMETHOD(device_probe,		amdtemp_probe),
	DEVMETHOD(device_attach,	amdtemp_attach),
	DEVMETHOD(device_detach,	amdtemp_detach),

	DEVMETHOD_END
};

static driver_t amdtemp_driver = {
	"amdtemp",
	amdtemp_methods,
	sizeof(struct amdtemp_softc),
};

static devclass_t amdtemp_devclass;
DRIVER_MODULE(amdtemp, hostb, amdtemp_driver, amdtemp_devclass, NULL, NULL);
MODULE_VERSION(amdtemp, 1);
MODULE_DEPEND(amdtemp, amdsmn, 1, 1, 1);
MODULE_PNP_INFO("U16:vendor;U16:device", pci, amdtemp, amdtemp_products,
    nitems(amdtemp_products));

static bool
amdtemp_match(device_t dev, const struct amdtemp_product **product_out)
{
	int i;
	uint16_t vendor, devid;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);

	for (i = 0; i < nitems(amdtemp_products); i++) {
		if (vendor == amdtemp_products[i].amdtemp_vendorid &&
		    devid == amdtemp_products[i].amdtemp_deviceid) {
			if (product_out != NULL)
				*product_out = &amdtemp_products[i];
			return (true);
		}
	}
	return (false);
}

static void
amdtemp_identify(driver_t *driver, device_t parent)
{
	device_t child;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "amdtemp", -1) != NULL)
		return;

	if (amdtemp_match(parent, NULL)) {
		child = device_add_child(parent, "amdtemp", -1);
		if (child == NULL)
			device_printf(parent, "add amdtemp child failed\n");
	}
}

static int
amdtemp_probe(device_t dev)
{
	uint32_t family, model;

	if (resource_disabled("amdtemp", 0))
		return (ENXIO);
	if (!amdtemp_match(device_get_parent(dev), NULL))
		return (ENXIO);

	family = CPUID_TO_FAMILY(cpu_id);
	model = CPUID_TO_MODEL(cpu_id);

	switch (family) {
	case 0x0f:
		if ((model == 0x04 && (cpu_id & CPUID_STEPPING) == 0) ||
		    (model == 0x05 && (cpu_id & CPUID_STEPPING) <= 1))
			return (ENXIO);
		break;
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x14:
	case 0x15:
	case 0x16:
	case 0x17:
		break;
	default:
		return (ENXIO);
	}
	device_set_desc(dev, "AMD CPU On-Die Thermal Sensors");

	return (BUS_PROBE_GENERIC);
}

static int
amdtemp_attach(device_t dev)
{
	char tn[32];
	u_int regs[4];
	const struct amdtemp_product *product;
	struct amdtemp_softc *sc;
	struct sysctl_ctx_list *sysctlctx;
	struct sysctl_oid *sysctlnode;
	uint32_t cpuid, family, model;
	u_int bid;
	int erratum319, unit;
	bool needsmn;

	sc = device_get_softc(dev);
	erratum319 = 0;
	needsmn = false;

	if (!amdtemp_match(device_get_parent(dev), &product))
		return (ENXIO);

	cpuid = cpu_id;
	family = CPUID_TO_FAMILY(cpuid);
	model = CPUID_TO_MODEL(cpuid);

	/*
	 * This checks for the byzantine condition of running a heterogenous
	 * revision multi-socket system where the attach thread is potentially
	 * probing a remote socket's PCI device.
	 *
	 * Currently, such scenarios are unsupported on models using the SMN
	 * (because on those models, amdtemp(4) attaches to a different PCI
	 * device than the one that contains AMDTEMP_CPUID).
	 *
	 * The ancient 0x0F family of devices only supports this register from
	 * models 40h+.
	 */
	if (product->amdtemp_has_cpuid && (family > 0x0f ||
	    (family == 0x0f && model >= 0x40))) {
		cpuid = pci_read_config(device_get_parent(dev), AMDTEMP_CPUID,
		    4);
		family = CPUID_TO_FAMILY(cpuid);
		model = CPUID_TO_MODEL(cpuid);
	}

	switch (family) {
	case 0x0f:
		/*
		 * Thermaltrip Status Register
		 *
		 * - ThermSenseCoreSel
		 *
		 * Revision F & G:	0 - Core1, 1 - Core0
		 * Other:		0 - Core0, 1 - Core1
		 *
		 * - CurTmp
		 *
		 * Revision G:		bits 23-14
		 * Other:		bits 23-16
		 *
		 * XXX According to the BKDG, CurTmp, ThermSenseSel and
		 * ThermSenseCoreSel bits were introduced in Revision F
		 * but CurTmp seems working fine as early as Revision C.
		 * However, it is not clear whether ThermSenseSel and/or
		 * ThermSenseCoreSel work in undocumented cases as well.
		 * In fact, the Linux driver suggests it may not work but
		 * we just assume it does until we find otherwise.
		 *
		 * XXX According to Linux, CurTmp starts at -28C on
		 * Socket AM2 Revision G processors, which is not
		 * documented anywhere.
		 */
		if (model >= 0x40)
			sc->sc_flags |= AMDTEMP_FLAG_CS_SWAP;
		if (model >= 0x60 && model != 0xc1) {
			do_cpuid(0x80000001, regs);
			bid = (regs[1] >> 9) & 0x1f;
			switch (model) {
			case 0x68: /* Socket S1g1 */
			case 0x6c:
			case 0x7c:
				break;
			case 0x6b: /* Socket AM2 and ASB1 (2 cores) */
				if (bid != 0x0b && bid != 0x0c)
					sc->sc_flags |=
					    AMDTEMP_FLAG_ALT_OFFSET;
				break;
			case 0x6f: /* Socket AM2 and ASB1 (1 core) */
			case 0x7f:
				if (bid != 0x07 && bid != 0x09 &&
				    bid != 0x0c)
					sc->sc_flags |=
					    AMDTEMP_FLAG_ALT_OFFSET;
				break;
			default:
				sc->sc_flags |= AMDTEMP_FLAG_ALT_OFFSET;
			}
			sc->sc_flags |= AMDTEMP_FLAG_CT_10BIT;
		}

		/*
		 * There are two sensors per core.
		 */
		sc->sc_ntemps = 2;

		sc->sc_gettemp = amdtemp_gettemp0f;
		break;
	case 0x10:
		/*
		 * Erratum 319 Inaccurate Temperature Measurement
		 *
		 * http://support.amd.com/us/Processor_TechDocs/41322.pdf
		 */
		do_cpuid(0x80000001, regs);
		switch ((regs[1] >> 28) & 0xf) {
		case 0:	/* Socket F */
			erratum319 = 1;
			break;
		case 1:	/* Socket AM2+ or AM3 */
			if ((pci_cfgregread(pci_get_bus(dev),
			    pci_get_slot(dev), 2, AMDTEMP_DRAM_CONF_HIGH, 2) &
			    AMDTEMP_DRAM_MODE_DDR3) != 0 || model > 0x04 ||
			    (model == 0x04 && (cpuid & CPUID_STEPPING) >= 3))
				break;
			/* XXX 00100F42h (RB-C2) exists in both formats. */
			erratum319 = 1;
			break;
		}
		/* FALLTHROUGH */
	case 0x11:
	case 0x12:
	case 0x14:
	case 0x15:
	case 0x16:
		sc->sc_ntemps = 1;
		/*
		 * Some later (60h+) models of family 15h use a similar SMN
		 * network as family 17h.  (However, the register index differs
		 * from 17h and the decoding matches other 10h-15h models,
		 * which differ from 17h.)
		 */
		if (family == 0x15 && model >= 0x60) {
			sc->sc_gettemp = amdtemp_gettemp15hm60h;
			needsmn = true;
		} else
			sc->sc_gettemp = amdtemp_gettemp;
		break;
	case 0x17:
		sc->sc_ntemps = 1;
		sc->sc_gettemp = amdtemp_gettemp17h;
		needsmn = true;
		break;
	default:
		device_printf(dev, "Bogus family 0x%x\n", family);
		return (ENXIO);
	}

	if (needsmn) {
		sc->sc_smn = device_find_child(
		    device_get_parent(dev), "amdsmn", -1);
		if (sc->sc_smn == NULL) {
			if (bootverbose)
				device_printf(dev, "No SMN device found\n");
			return (ENXIO);
		}
	}

	/* Find number of cores per package. */
	sc->sc_ncores = (amd_feature2 & AMDID2_CMP) != 0 ?
	    (cpu_procinfo2 & AMDID_CMP_CORES) + 1 : 1;
	if (sc->sc_ncores > MAXCPU)
		return (ENXIO);

	if (erratum319)
		device_printf(dev,
		    "Erratum 319: temperature measurement may be inaccurate\n");
	if (bootverbose)
		device_printf(dev, "Found %d cores and %d sensors.\n",
		    sc->sc_ncores,
		    sc->sc_ntemps > 1 ? sc->sc_ntemps * sc->sc_ncores : 1);

	/*
	 * dev.amdtemp.N tree.
	 */
	unit = device_get_unit(dev);
	snprintf(tn, sizeof(tn), "dev.amdtemp.%d.sensor_offset", unit);
	TUNABLE_INT_FETCH(tn, &sc->sc_offset);

	sysctlctx = device_get_sysctl_ctx(dev);
	SYSCTL_ADD_INT(sysctlctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "sensor_offset", CTLFLAG_RW, &sc->sc_offset, 0,
	    "Temperature sensor offset");
	sysctlnode = SYSCTL_ADD_NODE(sysctlctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "core0", CTLFLAG_RD, 0, "Core 0");

	SYSCTL_ADD_PROC(sysctlctx,
	    SYSCTL_CHILDREN(sysctlnode),
	    OID_AUTO, "sensor0", CTLTYPE_INT | CTLFLAG_RD,
	    dev, CORE0_SENSOR0, amdtemp_sysctl, "IK",
	    "Core 0 / Sensor 0 temperature");

	if (sc->sc_ntemps > 1) {
		SYSCTL_ADD_PROC(sysctlctx,
		    SYSCTL_CHILDREN(sysctlnode),
		    OID_AUTO, "sensor1", CTLTYPE_INT | CTLFLAG_RD,
		    dev, CORE0_SENSOR1, amdtemp_sysctl, "IK",
		    "Core 0 / Sensor 1 temperature");

		if (sc->sc_ncores > 1) {
			sysctlnode = SYSCTL_ADD_NODE(sysctlctx,
			    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
			    OID_AUTO, "core1", CTLFLAG_RD, 0, "Core 1");

			SYSCTL_ADD_PROC(sysctlctx,
			    SYSCTL_CHILDREN(sysctlnode),
			    OID_AUTO, "sensor0", CTLTYPE_INT | CTLFLAG_RD,
			    dev, CORE1_SENSOR0, amdtemp_sysctl, "IK",
			    "Core 1 / Sensor 0 temperature");

			SYSCTL_ADD_PROC(sysctlctx,
			    SYSCTL_CHILDREN(sysctlnode),
			    OID_AUTO, "sensor1", CTLTYPE_INT | CTLFLAG_RD,
			    dev, CORE1_SENSOR1, amdtemp_sysctl, "IK",
			    "Core 1 / Sensor 1 temperature");
		}
	}

	/*
	 * Try to create dev.cpu sysctl entries and setup intrhook function.
	 * This is needed because the cpu driver may be loaded late on boot,
	 * after us.
	 */
	amdtemp_intrhook(dev);
	sc->sc_ich.ich_func = amdtemp_intrhook;
	sc->sc_ich.ich_arg = dev;
	if (config_intrhook_establish(&sc->sc_ich) != 0) {
		device_printf(dev, "config_intrhook_establish failed!\n");
		return (ENXIO);
	}

	return (0);
}

void
amdtemp_intrhook(void *arg)
{
	struct amdtemp_softc *sc;
	struct sysctl_ctx_list *sysctlctx;
	device_t dev = (device_t)arg;
	device_t acpi, cpu, nexus;
	amdsensor_t sensor;
	int i;

	sc = device_get_softc(dev);

	/*
	 * dev.cpu.N.temperature.
	 */
	nexus = device_find_child(root_bus, "nexus", 0);
	acpi = device_find_child(nexus, "acpi", 0);

	for (i = 0; i < sc->sc_ncores; i++) {
		if (sc->sc_sysctl_cpu[i] != NULL)
			continue;
		cpu = device_find_child(acpi, "cpu",
		    device_get_unit(dev) * sc->sc_ncores + i);
		if (cpu != NULL) {
			sysctlctx = device_get_sysctl_ctx(cpu);

			sensor = sc->sc_ntemps > 1 ?
			    (i == 0 ? CORE0 : CORE1) : CORE0_SENSOR0;
			sc->sc_sysctl_cpu[i] = SYSCTL_ADD_PROC(sysctlctx,
			    SYSCTL_CHILDREN(device_get_sysctl_tree(cpu)),
			    OID_AUTO, "temperature", CTLTYPE_INT | CTLFLAG_RD,
			    dev, sensor, amdtemp_sysctl, "IK",
			    "Current temparature");
		}
	}
	if (sc->sc_ich.ich_arg != NULL)
		config_intrhook_disestablish(&sc->sc_ich);
}

int
amdtemp_detach(device_t dev)
{
	struct amdtemp_softc *sc = device_get_softc(dev);
	int i;

	for (i = 0; i < sc->sc_ncores; i++)
		if (sc->sc_sysctl_cpu[i] != NULL)
			sysctl_remove_oid(sc->sc_sysctl_cpu[i], 1, 0);

	/* NewBus removes the dev.amdtemp.N tree by itself. */

	return (0);
}

static int
amdtemp_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	struct amdtemp_softc *sc = device_get_softc(dev);
	amdsensor_t sensor = (amdsensor_t)arg2;
	int32_t auxtemp[2], temp;
	int error;

	switch (sensor) {
	case CORE0:
		auxtemp[0] = sc->sc_gettemp(dev, CORE0_SENSOR0);
		auxtemp[1] = sc->sc_gettemp(dev, CORE0_SENSOR1);
		temp = imax(auxtemp[0], auxtemp[1]);
		break;
	case CORE1:
		auxtemp[0] = sc->sc_gettemp(dev, CORE1_SENSOR0);
		auxtemp[1] = sc->sc_gettemp(dev, CORE1_SENSOR1);
		temp = imax(auxtemp[0], auxtemp[1]);
		break;
	default:
		temp = sc->sc_gettemp(dev, sensor);
		break;
	}
	error = sysctl_handle_int(oidp, &temp, 0, req);

	return (error);
}

#define	AMDTEMP_ZERO_C_TO_K	2731

static int32_t
amdtemp_gettemp0f(device_t dev, amdsensor_t sensor)
{
	struct amdtemp_softc *sc = device_get_softc(dev);
	uint32_t mask, offset, temp;

	/* Set Sensor/Core selector. */
	temp = pci_read_config(dev, AMDTEMP_THERMTP_STAT, 1);
	temp &= ~(AMDTEMP_TTSR_SELCORE | AMDTEMP_TTSR_SELSENSOR);
	switch (sensor) {
	case CORE0_SENSOR1:
		temp |= AMDTEMP_TTSR_SELSENSOR;
		/* FALLTHROUGH */
	case CORE0_SENSOR0:
	case CORE0:
		if ((sc->sc_flags & AMDTEMP_FLAG_CS_SWAP) != 0)
			temp |= AMDTEMP_TTSR_SELCORE;
		break;
	case CORE1_SENSOR1:
		temp |= AMDTEMP_TTSR_SELSENSOR;
		/* FALLTHROUGH */
	case CORE1_SENSOR0:
	case CORE1:
		if ((sc->sc_flags & AMDTEMP_FLAG_CS_SWAP) == 0)
			temp |= AMDTEMP_TTSR_SELCORE;
		break;
	}
	pci_write_config(dev, AMDTEMP_THERMTP_STAT, temp, 1);

	mask = (sc->sc_flags & AMDTEMP_FLAG_CT_10BIT) != 0 ? 0x3ff : 0x3fc;
	offset = (sc->sc_flags & AMDTEMP_FLAG_ALT_OFFSET) != 0 ? 28 : 49;
	temp = pci_read_config(dev, AMDTEMP_THERMTP_STAT, 4);
	temp = ((temp >> 14) & mask) * 5 / 2;
	temp += AMDTEMP_ZERO_C_TO_K + (sc->sc_offset - offset) * 10;

	return (temp);
}

static uint32_t
amdtemp_decode_fam10h_to_16h(int32_t sc_offset, uint32_t val)
{
	uint32_t temp;

	/* Convert raw register subfield units (0.125C) to units of 0.1C. */
	temp = ((val >> AMDTEMP_REPTMP10H_CURTMP_SHIFT) &
	    AMDTEMP_REPTMP10H_CURTMP_MASK) * 5 / 4;

	/*
	 * On Family 15h and higher, if CurTmpTjSel is 11b, the range is
	 * adjusted down by 49.0 degrees Celsius.  (This adjustment is not
	 * documented in BKDGs prior to family 15h model 00h.)
	 */
	if (CPUID_TO_FAMILY(cpu_id) >= 0x15 &&
	    ((val >> AMDTEMP_REPTMP10H_TJSEL_SHIFT) &
	    AMDTEMP_REPTMP10H_TJSEL_MASK) == 0x3)
		temp -= AMDTEMP_CURTMP_RANGE_ADJUST;

	temp += AMDTEMP_ZERO_C_TO_K + sc_offset * 10;
	return (temp);
}

static int32_t
amdtemp_gettemp(device_t dev, amdsensor_t sensor)
{
	struct amdtemp_softc *sc = device_get_softc(dev);
	uint32_t temp;

	temp = pci_read_config(dev, AMDTEMP_REPTMP_CTRL, 4);
	return (amdtemp_decode_fam10h_to_16h(sc->sc_offset, temp));
}

static int32_t
amdtemp_gettemp15hm60h(device_t dev, amdsensor_t sensor)
{
	struct amdtemp_softc *sc = device_get_softc(dev);
	uint32_t val;
	int error;

	error = amdsmn_read(sc->sc_smn, AMDTEMP_15H_M60H_REPTMP_CTRL, &val);
	KASSERT(error == 0, ("amdsmn_read"));
	return (amdtemp_decode_fam10h_to_16h(sc->sc_offset, val));
}

static int32_t
amdtemp_gettemp17h(device_t dev, amdsensor_t sensor)
{
	struct amdtemp_softc *sc = device_get_softc(dev);
	uint32_t temp, val;
	int error;

	error = amdsmn_read(sc->sc_smn, AMDTEMP_17H_CUR_TMP, &val);
	KASSERT(error == 0, ("amdsmn_read"));

	temp = ((val >> 21) & 0x7ff) * 5 / 4;
	if ((val & AMDTEMP_17H_CUR_TMP_RANGE_SEL) != 0)
		temp -= AMDTEMP_CURTMP_RANGE_ADJUST;
	temp += AMDTEMP_ZERO_C_TO_K + sc->sc_offset * 10;

	return (temp);
}
