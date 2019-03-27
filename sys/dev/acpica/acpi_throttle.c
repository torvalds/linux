/*-
 * Copyright (c) 2003-2005 Nate Lawson (SDG)
 * Copyright (c) 2001 Michael Smith
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>
#include <dev/pci/pcivar.h>

#include "cpufreq_if.h"

/*
 * Throttling provides relative frequency control.  It involves modulating
 * the clock so that the CPU is active for only a fraction of the normal
 * clock cycle.  It does not change voltage and so is less efficient than
 * other mechanisms.  Since it is relative, it can be used in addition to
 * absolute cpufreq drivers.  We support the ACPI 2.0 specification.
 */

struct acpi_throttle_softc {
	device_t	 cpu_dev;
	ACPI_HANDLE	 cpu_handle;
	uint32_t	 cpu_p_blk;	/* ACPI P_BLK location */
	uint32_t	 cpu_p_blk_len;	/* P_BLK length (must be 6). */
	struct resource	*cpu_p_cnt;	/* Throttling control register */
	int		 cpu_p_type;	/* Resource type for cpu_p_cnt. */
	uint32_t	 cpu_thr_state;	/* Current throttle setting. */
};

#define THR_GET_REG(reg) 					\
	(bus_space_read_4(rman_get_bustag((reg)), 		\
			  rman_get_bushandle((reg)), 0))
#define THR_SET_REG(reg, val)					\
	(bus_space_write_4(rman_get_bustag((reg)), 		\
			   rman_get_bushandle((reg)), 0, (val)))

/*
 * Speeds are stored in counts, from 1 to CPU_MAX_SPEED, and
 * reported to the user in hundredths of a percent.
 */
#define CPU_MAX_SPEED		(1 << cpu_duty_width)
#define CPU_SPEED_PERCENT(x)	((10000 * (x)) / CPU_MAX_SPEED)
#define CPU_SPEED_PRINTABLE(x)	(CPU_SPEED_PERCENT(x) / 10),	\
				(CPU_SPEED_PERCENT(x) % 10)
#define CPU_P_CNT_THT_EN	(1<<4)
#define CPU_QUIRK_NO_THROTTLE	(1<<1)	/* Throttling is not usable. */

#define PCI_VENDOR_INTEL	0x8086
#define PCI_DEVICE_82371AB_3	0x7113	/* PIIX4 chipset for quirks. */
#define PCI_REVISION_A_STEP	0
#define PCI_REVISION_B_STEP	1

static uint32_t	cpu_duty_offset;	/* Offset in P_CNT of throttle val. */
static uint32_t	cpu_duty_width;		/* Bit width of throttle value. */
static int	thr_rid;		/* Driver-wide resource id. */
static int	thr_quirks;		/* Indicate any hardware bugs. */

static void	acpi_throttle_identify(driver_t *driver, device_t parent);
static int	acpi_throttle_probe(device_t dev);
static int	acpi_throttle_attach(device_t dev);
static int	acpi_throttle_evaluate(struct acpi_throttle_softc *sc);
static void	acpi_throttle_quirks(struct acpi_throttle_softc *sc);
static int	acpi_thr_settings(device_t dev, struct cf_setting *sets,
		    int *count);
static int	acpi_thr_set(device_t dev, const struct cf_setting *set);
static int	acpi_thr_get(device_t dev, struct cf_setting *set);
static int	acpi_thr_type(device_t dev, int *type);

static device_method_t acpi_throttle_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	acpi_throttle_identify),
	DEVMETHOD(device_probe,		acpi_throttle_probe),
	DEVMETHOD(device_attach,	acpi_throttle_attach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	acpi_thr_set),
	DEVMETHOD(cpufreq_drv_get,	acpi_thr_get),
	DEVMETHOD(cpufreq_drv_type,	acpi_thr_type),
	DEVMETHOD(cpufreq_drv_settings,	acpi_thr_settings),
	DEVMETHOD_END
};

static driver_t acpi_throttle_driver = {
	"acpi_throttle",
	acpi_throttle_methods,
	sizeof(struct acpi_throttle_softc),
};

static devclass_t acpi_throttle_devclass;
DRIVER_MODULE(acpi_throttle, cpu, acpi_throttle_driver, acpi_throttle_devclass,
    0, 0);

static void
acpi_throttle_identify(driver_t *driver, device_t parent)
{
	ACPI_BUFFER buf;
	ACPI_HANDLE handle;
	ACPI_OBJECT *obj;

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "acpi_throttle", -1))
		return;

	/* Check for a valid duty width and parent CPU type. */
	handle = acpi_get_handle(parent);
	if (handle == NULL)
		return;
	if (AcpiGbl_FADT.DutyWidth == 0 ||
	    acpi_get_type(parent) != ACPI_TYPE_PROCESSOR)
		return;

	/*
	 * Add a child if there's a non-NULL P_BLK and correct length, or
	 * if the _PTC method is present.
	 */
	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	if (ACPI_FAILURE(AcpiEvaluateObject(handle, NULL, NULL, &buf)))
		return;
	obj = (ACPI_OBJECT *)buf.Pointer;
	if ((obj->Processor.PblkAddress && obj->Processor.PblkLength >= 4) ||
	    ACPI_SUCCESS(AcpiEvaluateObject(handle, "_PTC", NULL, NULL))) {
		if (BUS_ADD_CHILD(parent, 0, "acpi_throttle", -1) == NULL)
			device_printf(parent, "add throttle child failed\n");
	}
	AcpiOsFree(obj);
}

static int
acpi_throttle_probe(device_t dev)
{

	if (resource_disabled("acpi_throttle", 0))
		return (ENXIO);

	/*
	 * On i386 platforms at least, ACPI throttling is accomplished by
	 * the chipset modulating the STPCLK# pin based on the duty cycle.
	 * Since p4tcc uses the same mechanism (but internal to the CPU),
	 * we disable acpi_throttle when p4tcc is also present.
	 */
	if (device_find_child(device_get_parent(dev), "p4tcc", -1) &&
	    !resource_disabled("p4tcc", 0))
		return (ENXIO);

	device_set_desc(dev, "ACPI CPU Throttling");
	return (0);
}

static int
acpi_throttle_attach(device_t dev)
{
	struct acpi_throttle_softc *sc;
	struct cf_setting set;
	ACPI_BUFFER buf;
	ACPI_OBJECT *obj;
	ACPI_STATUS status;
	int error;

	sc = device_get_softc(dev);
	sc->cpu_dev = dev;
	sc->cpu_handle = acpi_get_handle(dev);

	buf.Pointer = NULL;
	buf.Length = ACPI_ALLOCATE_BUFFER;
	status = AcpiEvaluateObject(sc->cpu_handle, NULL, NULL, &buf);
	if (ACPI_FAILURE(status)) {
		device_printf(dev, "attach failed to get Processor obj - %s\n",
		    AcpiFormatException(status));
		return (ENXIO);
	}
	obj = (ACPI_OBJECT *)buf.Pointer;
	sc->cpu_p_blk = obj->Processor.PblkAddress;
	sc->cpu_p_blk_len = obj->Processor.PblkLength;
	AcpiOsFree(obj);

	/* If this is the first device probed, check for quirks. */
	if (device_get_unit(dev) == 0)
		acpi_throttle_quirks(sc);

	/* Attempt to attach the actual throttling register. */
	error = acpi_throttle_evaluate(sc);
	if (error)
		return (error);

	/*
	 * Set our initial frequency to the highest since some systems
	 * seem to boot with this at the lowest setting.
	 */
	set.freq = 10000;
	acpi_thr_set(dev, &set);

	/* Everything went ok, register with cpufreq(4). */
	cpufreq_register(dev);
	return (0);
}

static int
acpi_throttle_evaluate(struct acpi_throttle_softc *sc)
{
	uint32_t duty_end;
	ACPI_BUFFER buf;
	ACPI_OBJECT obj;
	ACPI_GENERIC_ADDRESS gas;
	ACPI_STATUS status;

	/* Get throttling parameters from the FADT.  0 means not supported. */
	if (device_get_unit(sc->cpu_dev) == 0) {
		cpu_duty_offset = AcpiGbl_FADT.DutyOffset;
		cpu_duty_width = AcpiGbl_FADT.DutyWidth;
	}
	if (cpu_duty_width == 0 || (thr_quirks & CPU_QUIRK_NO_THROTTLE) != 0)
		return (ENXIO);

	/* Validate the duty offset/width. */
	duty_end = cpu_duty_offset + cpu_duty_width - 1;
	if (duty_end > 31) {
		device_printf(sc->cpu_dev,
		    "CLK_VAL field overflows P_CNT register\n");
		return (ENXIO);
	}
	if (cpu_duty_offset <= 4 && duty_end >= 4) {
		device_printf(sc->cpu_dev,
		    "CLK_VAL field overlaps THT_EN bit\n");
		return (ENXIO);
	}

	/*
	 * If not present, fall back to using the processor's P_BLK to find
	 * the P_CNT register.
	 *
	 * Note that some systems seem to duplicate the P_BLK pointer
	 * across multiple CPUs, so not getting the resource is not fatal.
	 */
	buf.Pointer = &obj;
	buf.Length = sizeof(obj);
	status = AcpiEvaluateObject(sc->cpu_handle, "_PTC", NULL, &buf);
	if (ACPI_SUCCESS(status)) {
		if (obj.Buffer.Length < sizeof(ACPI_GENERIC_ADDRESS) + 3) {
			device_printf(sc->cpu_dev, "_PTC buffer too small\n");
			return (ENXIO);
		}
		memcpy(&gas, obj.Buffer.Pointer + 3, sizeof(gas));
		acpi_bus_alloc_gas(sc->cpu_dev, &sc->cpu_p_type, &thr_rid,
		    &gas, &sc->cpu_p_cnt, 0);
		if (sc->cpu_p_cnt != NULL && bootverbose) {
			device_printf(sc->cpu_dev, "P_CNT from _PTC %#jx\n",
			    gas.Address);
		}
	}

	/* If _PTC not present or other failure, try the P_BLK. */
	if (sc->cpu_p_cnt == NULL) {
		/* 
		 * The spec says P_BLK must be 6 bytes long.  However, some
		 * systems use it to indicate a fractional set of features
		 * present so we take anything >= 4.
		 */
		if (sc->cpu_p_blk_len < 4)
			return (ENXIO);
		gas.Address = sc->cpu_p_blk;
		gas.SpaceId = ACPI_ADR_SPACE_SYSTEM_IO;
		gas.BitWidth = 32;
		acpi_bus_alloc_gas(sc->cpu_dev, &sc->cpu_p_type, &thr_rid,
		    &gas, &sc->cpu_p_cnt, 0);
		if (sc->cpu_p_cnt != NULL) {
			if (bootverbose)
				device_printf(sc->cpu_dev,
				    "P_CNT from P_BLK %#x\n", sc->cpu_p_blk);
		} else {
			device_printf(sc->cpu_dev, "failed to attach P_CNT\n");
			return (ENXIO);
		}
	}
	thr_rid++;

	return (0);
}

static void
acpi_throttle_quirks(struct acpi_throttle_softc *sc)
{
#ifdef __i386__
	device_t acpi_dev;

	/* Look for various quirks of the PIIX4 part. */
	acpi_dev = pci_find_device(PCI_VENDOR_INTEL, PCI_DEVICE_82371AB_3);
	if (acpi_dev) {
		switch (pci_get_revid(acpi_dev)) {
		/*
		 * Disable throttling control on PIIX4 A and B-step.
		 * See specification changes #13 ("Manual Throttle Duty Cycle")
		 * and #14 ("Enabling and Disabling Manual Throttle"), plus
		 * erratum #5 ("STPCLK# Deassertion Time") from the January
		 * 2002 PIIX4 specification update.  Note that few (if any)
		 * mobile systems ever used this part.
		 */
		case PCI_REVISION_A_STEP:
		case PCI_REVISION_B_STEP:
			thr_quirks |= CPU_QUIRK_NO_THROTTLE;
			break;
		default:
			break;
		}
	}
#endif
}

static int
acpi_thr_settings(device_t dev, struct cf_setting *sets, int *count)
{
	int i, speed;

	if (sets == NULL || count == NULL)
		return (EINVAL);
	if (*count < CPU_MAX_SPEED)
		return (E2BIG);

	/* Return a list of valid settings for this driver. */
	memset(sets, CPUFREQ_VAL_UNKNOWN, sizeof(*sets) * CPU_MAX_SPEED);
	for (i = 0, speed = CPU_MAX_SPEED; speed != 0; i++, speed--) {
		sets[i].freq = CPU_SPEED_PERCENT(speed);
		sets[i].dev = dev;
	}
	*count = CPU_MAX_SPEED;

	return (0);
}

static int
acpi_thr_set(device_t dev, const struct cf_setting *set)
{
	struct acpi_throttle_softc *sc;
	uint32_t clk_val, p_cnt, speed;

	if (set == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);

	/*
	 * Validate requested state converts to a duty cycle that is an
	 * integer from [1 .. CPU_MAX_SPEED].
	 */
	speed = set->freq * CPU_MAX_SPEED / 10000;
	if (speed * 10000 != set->freq * CPU_MAX_SPEED ||
	    speed < 1 || speed > CPU_MAX_SPEED)
		return (EINVAL);

	/* If we're at this setting, don't bother applying it again. */
	if (speed == sc->cpu_thr_state)
		return (0);

	/* Get the current P_CNT value and disable throttling */
	p_cnt = THR_GET_REG(sc->cpu_p_cnt);
	p_cnt &= ~CPU_P_CNT_THT_EN;
	THR_SET_REG(sc->cpu_p_cnt, p_cnt);

	/* If we're at maximum speed, that's all */
	if (speed < CPU_MAX_SPEED) {
		/* Mask the old CLK_VAL off and OR in the new value */
		clk_val = (CPU_MAX_SPEED - 1) << cpu_duty_offset;
		p_cnt &= ~clk_val;
		p_cnt |= (speed << cpu_duty_offset);

		/* Write the new P_CNT value and then enable throttling */
		THR_SET_REG(sc->cpu_p_cnt, p_cnt);
		p_cnt |= CPU_P_CNT_THT_EN;
		THR_SET_REG(sc->cpu_p_cnt, p_cnt);
	}
	sc->cpu_thr_state = speed;

	return (0);
}

static int
acpi_thr_get(device_t dev, struct cf_setting *set)
{
	struct acpi_throttle_softc *sc;
	uint32_t p_cnt, clk_val;

	if (set == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);

	/* Get the current throttling setting from P_CNT. */
	p_cnt = THR_GET_REG(sc->cpu_p_cnt);
	clk_val = (p_cnt >> cpu_duty_offset) & (CPU_MAX_SPEED - 1);
	sc->cpu_thr_state = clk_val;

	memset(set, CPUFREQ_VAL_UNKNOWN, sizeof(*set));
	set->freq = CPU_SPEED_PERCENT(clk_val);
	set->dev = dev;

	return (0);
}

static int
acpi_thr_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_RELATIVE;
	return (0);
}
