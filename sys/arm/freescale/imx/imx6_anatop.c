/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ian Lepore <ian@freebsd.org>
 * Copyright (c) 2014 Steven Lawrance <stl@koffein.net>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Analog PLL and power regulator driver for Freescale i.MX6 family of SoCs.
 * Also, temperature montoring and cpu frequency control.  It was Freescale who
 * kitchen-sinked this device, not us. :)
 *
 * We don't really do anything with analog PLLs, but the registers for
 * controlling them belong to the same block as the power regulator registers.
 * Since the newbus hierarchy makes it hard for anyone other than us to get at
 * them, we just export a couple public functions to allow the imx6 CCM clock
 * driver to read and write those registers.
 *
 * We also don't do anything about power regulation yet, but when the need
 * arises, this would be the place for that code to live.
 *
 * I have no idea where the "anatop" name comes from.  It's in the standard DTS
 * source describing i.MX6 SoCs, and in the linux and u-boot code which comes
 * from Freescale, but it's not in the SoC manual.
 *
 * Note that temperature values throughout this code are handled in two types of
 * units.  Items with '_cnt' in the name use the hardware temperature count
 * units (higher counts are lower temperatures).  Items with '_val' in the name
 * are deci-Celcius, which are converted to/from deci-Kelvins in the sysctl
 * handlers (dK is the standard unit for temperature in sysctl).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <arm/arm/mpcore_timervar.h>
#include <arm/freescale/fsl_ocotpreg.h>
#include <arm/freescale/fsl_ocotpvar.h>
#include <arm/freescale/imx/imx_ccmvar.h>
#include <arm/freescale/imx/imx_machdep.h>
#include <arm/freescale/imx/imx6_anatopreg.h>
#include <arm/freescale/imx/imx6_anatopvar.h>

static struct resource_spec imx6_anatop_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};
#define	MEMRES	0
#define	IRQRES	1

struct imx6_anatop_softc {
	device_t	dev;
	struct resource	*res[2];
	struct intr_config_hook
			intr_setup_hook;
	uint32_t	cpu_curmhz;
	uint32_t	cpu_curmv;
	uint32_t	cpu_minmhz;
	uint32_t	cpu_minmv;
	uint32_t	cpu_maxmhz;
	uint32_t	cpu_maxmv;
	uint32_t	cpu_maxmhz_hw;
	boolean_t	cpu_overclock_enable;
	boolean_t	cpu_init_done;
	uint32_t	refosc_mhz;
	void		*temp_intrhand;
	uint32_t	temp_high_val;
	uint32_t	temp_high_cnt;
	uint32_t	temp_last_cnt;
	uint32_t	temp_room_cnt;
	struct callout	temp_throttle_callout;
	sbintime_t	temp_throttle_delay;
	uint32_t	temp_throttle_reset_cnt;
	uint32_t	temp_throttle_trigger_cnt;
	uint32_t	temp_throttle_val;
};

static struct imx6_anatop_softc *imx6_anatop_sc;

/*
 * Table of "operating points".
 * These are combinations of frequency and voltage blessed by Freescale.
 * While the datasheet says the ARM voltage can be as low as 925mV at
 * 396MHz, it also says that the ARM and SOC voltages can't differ by
 * more than 200mV, and the minimum SOC voltage is 1150mV, so that
 * dictates the 950mV entry in this table.
 */
static struct oppt {
	uint32_t	mhz;
	uint32_t	mv;
} imx6_oppt_table[] = {
	{ 396,	 950},
	{ 792,	1150},
	{ 852,	1225},
	{ 996,	1225},
	{1200,	1275},
};

/*
 * Table of CPU max frequencies.  This is used to translate the max frequency
 * value (0-3) from the ocotp CFG3 register into a mhz value that can be looked
 * up in the operating points table.
 */
static uint32_t imx6_ocotp_mhz_tab[] = {792, 852, 996, 1200};

#define	TZ_ZEROC	2731	/* deci-Kelvin <-> deci-Celcius offset. */

uint32_t
imx6_anatop_read_4(bus_size_t offset)
{

	KASSERT(imx6_anatop_sc != NULL, ("imx6_anatop_read_4 sc NULL"));

	return (bus_read_4(imx6_anatop_sc->res[MEMRES], offset));
}

void
imx6_anatop_write_4(bus_size_t offset, uint32_t value)
{

	KASSERT(imx6_anatop_sc != NULL, ("imx6_anatop_write_4 sc NULL"));

	bus_write_4(imx6_anatop_sc->res[MEMRES], offset, value);
}

static void
vdd_set(struct imx6_anatop_softc *sc, int mv)
{
	int newtarg, newtargSoc, oldtarg;
	uint32_t delay, pmureg;
	static boolean_t init_done = false;

	/*
	 * The datasheet says VDD_PU and VDD_SOC must be equal, and VDD_ARM
	 * can't be more than 50mV above or 200mV below them.  We keep them the
	 * same except in the case of the lowest operating point, which is
	 * handled as a special case below.
	 */

	pmureg = imx6_anatop_read_4(IMX6_ANALOG_PMU_REG_CORE);
	oldtarg = pmureg & IMX6_ANALOG_PMU_REG0_TARG_MASK;

	/* Convert mV to target value.  Clamp target to valid range. */
	if (mv < 725)
		newtarg = 0x00;
	else if (mv > 1450)
		newtarg = 0x1F;
	else
		newtarg = (mv - 700) / 25;

	/*
	 * The SOC voltage can't go below 1150mV, and thus because of the 200mV
	 * rule, the ARM voltage can't go below 950mV.  The 950 is encoded in
	 * our oppt table, here we handle the SOC 1150 rule as a special case.
	 * (1150-700/25=18).
	 */
	newtargSoc = (newtarg < 18) ? 18 : newtarg;

	/*
	 * The first time through the 3 voltages might not be equal so use a
	 * long conservative delay.  After that we need to delay 3uS for every
	 * 25mV step upward; we actually delay 6uS because empirically, it works
	 * and the 3uS per step recommended by the docs doesn't (3uS fails when
	 * going from 400->1200, but works for smaller changes).
	 */
	if (init_done) {
		if (newtarg == oldtarg)
			return;
		else if (newtarg > oldtarg)
			delay = (newtarg - oldtarg) * 6;
		else
			delay = 0;
	} else {
		delay = (700 / 25) * 6;
		init_done = true;
	}

	/*
	 * Make the change and wait for it to take effect.
	 */
	pmureg &= ~(IMX6_ANALOG_PMU_REG0_TARG_MASK |
	    IMX6_ANALOG_PMU_REG1_TARG_MASK |
	    IMX6_ANALOG_PMU_REG2_TARG_MASK);

	pmureg |= newtarg << IMX6_ANALOG_PMU_REG0_TARG_SHIFT;
	pmureg |= newtarg << IMX6_ANALOG_PMU_REG1_TARG_SHIFT;
	pmureg |= newtargSoc << IMX6_ANALOG_PMU_REG2_TARG_SHIFT;

	imx6_anatop_write_4(IMX6_ANALOG_PMU_REG_CORE, pmureg);
	DELAY(delay);
	sc->cpu_curmv = newtarg * 25 + 700;
}

static inline uint32_t
cpufreq_mhz_from_div(struct imx6_anatop_softc *sc, uint32_t corediv, 
    uint32_t plldiv)
{

	return ((sc->refosc_mhz * (plldiv / 2)) / (corediv + 1));
}

static inline void
cpufreq_mhz_to_div(struct imx6_anatop_softc *sc, uint32_t cpu_mhz,
    uint32_t *corediv, uint32_t *plldiv)
{

	*corediv = (cpu_mhz < 650) ? 1 : 0;
	*plldiv = ((*corediv + 1) * cpu_mhz) / (sc->refosc_mhz / 2);
}

static inline uint32_t
cpufreq_actual_mhz(struct imx6_anatop_softc *sc, uint32_t cpu_mhz)
{
	uint32_t corediv, plldiv;

	cpufreq_mhz_to_div(sc, cpu_mhz, &corediv, &plldiv);
	return (cpufreq_mhz_from_div(sc, corediv, plldiv));
}

static struct oppt *
cpufreq_nearest_oppt(struct imx6_anatop_softc *sc, uint32_t cpu_newmhz)
{
	int d, diff, i, nearest;

	if (cpu_newmhz > sc->cpu_maxmhz_hw && !sc->cpu_overclock_enable)
		cpu_newmhz = sc->cpu_maxmhz_hw;

	diff = INT_MAX;
	nearest = 0;
	for (i = 0; i < nitems(imx6_oppt_table); ++i) {
		d = abs((int)cpu_newmhz - (int)imx6_oppt_table[i].mhz);
		if (diff > d) {
			diff = d;
			nearest = i;
		}
	}
	return (&imx6_oppt_table[nearest]);
}

static void 
cpufreq_set_clock(struct imx6_anatop_softc * sc, struct oppt *op)
{
	uint32_t corediv, plldiv, timeout, wrk32;

	/* If increasing the frequency, we must first increase the voltage. */
	if (op->mhz > sc->cpu_curmhz) {
		vdd_set(sc, op->mv);
	}

	/*
	 * I can't find a documented procedure for changing the ARM PLL divisor,
	 * but some trial and error came up with this:
	 *  - Set the bypass clock source to REF_CLK_24M (source #0).
	 *  - Set the PLL into bypass mode; cpu should now be running at 24mhz.
	 *  - Change the divisor.
	 *  - Wait for the LOCK bit to come on; it takes ~50 loop iterations.
	 *  - Turn off bypass mode; cpu should now be running at the new speed.
	 */
	cpufreq_mhz_to_div(sc, op->mhz, &corediv, &plldiv);
	imx6_anatop_write_4(IMX6_ANALOG_CCM_PLL_ARM_CLR, 
	    IMX6_ANALOG_CCM_PLL_ARM_CLK_SRC_MASK);
	imx6_anatop_write_4(IMX6_ANALOG_CCM_PLL_ARM_SET, 
	    IMX6_ANALOG_CCM_PLL_ARM_BYPASS);

	wrk32 = imx6_anatop_read_4(IMX6_ANALOG_CCM_PLL_ARM);
	wrk32 &= ~IMX6_ANALOG_CCM_PLL_ARM_DIV_MASK;
	wrk32 |= plldiv;
	imx6_anatop_write_4(IMX6_ANALOG_CCM_PLL_ARM, wrk32);

	timeout = 10000;
	while ((imx6_anatop_read_4(IMX6_ANALOG_CCM_PLL_ARM) &
	    IMX6_ANALOG_CCM_PLL_ARM_LOCK) == 0)
		if (--timeout == 0)
			panic("imx6_set_cpu_clock(): PLL never locked");

	imx6_anatop_write_4(IMX6_ANALOG_CCM_PLL_ARM_CLR, 
	    IMX6_ANALOG_CCM_PLL_ARM_BYPASS);
	imx_ccm_set_cacrr(corediv);

	/* If lowering the frequency, it is now safe to lower the voltage. */
	if (op->mhz < sc->cpu_curmhz)
		vdd_set(sc, op->mv);
	sc->cpu_curmhz = op->mhz;

	/* Tell the mpcore timer that its frequency has changed. */
	arm_tmr_change_frequency(
	    cpufreq_actual_mhz(sc, sc->cpu_curmhz) * 1000000 / 2);
}

static int
cpufreq_sysctl_minmhz(SYSCTL_HANDLER_ARGS)
{
	struct imx6_anatop_softc *sc;
	struct oppt * op;
	uint32_t temp;
	int err;

	sc = arg1;

	temp = sc->cpu_minmhz;
	err = sysctl_handle_int(oidp, &temp, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	op = cpufreq_nearest_oppt(sc, temp);
	if (op->mhz > sc->cpu_maxmhz)
		return (ERANGE);
	else if (op->mhz == sc->cpu_minmhz)
		return (0);

	/*
	 * Value changed, update softc.  If the new min is higher than the
	 * current speed, raise the current speed to match.
	 */
	sc->cpu_minmhz = op->mhz;
	if (sc->cpu_minmhz > sc->cpu_curmhz) {
		cpufreq_set_clock(sc, op);
	}
	return (err);
}

static int
cpufreq_sysctl_maxmhz(SYSCTL_HANDLER_ARGS)
{
	struct imx6_anatop_softc *sc;
	struct oppt * op;
	uint32_t temp;
	int err;

	sc = arg1;

	temp = sc->cpu_maxmhz;
	err = sysctl_handle_int(oidp, &temp, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	op = cpufreq_nearest_oppt(sc, temp);
	if (op->mhz < sc->cpu_minmhz)
		return (ERANGE);
	else if (op->mhz == sc->cpu_maxmhz)
		return (0);

	/*
	 *  Value changed, update softc and hardware.  The hardware update is
	 *  unconditional.  We always try to run at max speed, so any change of
	 *  the max means we need to change the current speed too, regardless of
	 *  whether it is higher or lower than the old max.
	 */
	sc->cpu_maxmhz = op->mhz;
	cpufreq_set_clock(sc, op);

	return (err);
}

static void
cpufreq_initialize(struct imx6_anatop_softc *sc)
{
	uint32_t cfg3speed;
	struct oppt * op;

	SYSCTL_ADD_INT(NULL, SYSCTL_STATIC_CHILDREN(_hw_imx),
	    OID_AUTO, "cpu_mhz", CTLFLAG_RD, &sc->cpu_curmhz, 0, 
	    "CPU frequency");

	SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_hw_imx), 
	    OID_AUTO, "cpu_minmhz", CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_NOFETCH,
	    sc, 0, cpufreq_sysctl_minmhz, "IU", "Minimum CPU frequency");

	SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_hw_imx),
	    OID_AUTO, "cpu_maxmhz", CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_NOFETCH,
	    sc, 0, cpufreq_sysctl_maxmhz, "IU", "Maximum CPU frequency");

	SYSCTL_ADD_INT(NULL, SYSCTL_STATIC_CHILDREN(_hw_imx),
	    OID_AUTO, "cpu_maxmhz_hw", CTLFLAG_RD, &sc->cpu_maxmhz_hw, 0, 
	    "Maximum CPU frequency allowed by hardware");

	SYSCTL_ADD_INT(NULL, SYSCTL_STATIC_CHILDREN(_hw_imx),
	    OID_AUTO, "cpu_overclock_enable", CTLFLAG_RWTUN, 
	    &sc->cpu_overclock_enable, 0, 
	    "Allow setting CPU frequency higher than cpu_maxmhz_hw");

	/*
	 * XXX 24mhz shouldn't be hard-coded, should get this from imx6_ccm
	 * (even though in the real world it will always be 24mhz).  Oh wait a
	 * sec, I never wrote imx6_ccm.
	 */
	sc->refosc_mhz = 24;

	/*
	 * Get the maximum speed this cpu can be set to.  The values in the
	 * OCOTP CFG3 register are not documented in the reference manual.
	 * The following info was in an archived email found via web search:
	 *   - 2b'11: 1200000000Hz;
	 *   - 2b'10: 996000000Hz;
	 *   - 2b'01: 852000000Hz; -- i.MX6Q Only, exclusive with 996MHz.
	 *   - 2b'00: 792000000Hz;
	 * The default hardware max speed can be overridden by a tunable.
	 */
	cfg3speed = (fsl_ocotp_read_4(FSL_OCOTP_CFG3) & 
	    FSL_OCOTP_CFG3_SPEED_MASK) >> FSL_OCOTP_CFG3_SPEED_SHIFT;
	sc->cpu_maxmhz_hw = imx6_ocotp_mhz_tab[cfg3speed];
	sc->cpu_maxmhz = sc->cpu_maxmhz_hw;

	TUNABLE_INT_FETCH("hw.imx6.cpu_minmhz", &sc->cpu_minmhz);
	op = cpufreq_nearest_oppt(sc, sc->cpu_minmhz);
	sc->cpu_minmhz = op->mhz;
	sc->cpu_minmv = op->mv;

	TUNABLE_INT_FETCH("hw.imx6.cpu_maxmhz", &sc->cpu_maxmhz);
	op = cpufreq_nearest_oppt(sc, sc->cpu_maxmhz);
	sc->cpu_maxmhz = op->mhz;
	sc->cpu_maxmv = op->mv;

	/*
	 * Set the CPU to maximum speed.
	 *
	 * We won't have thermal throttling until interrupts are enabled, but we
	 * want to run at full speed through all the device init stuff.  This
	 * basically assumes that a single core can't overheat before interrupts
	 * are enabled; empirical testing shows that to be a safe assumption.
	 */
	cpufreq_set_clock(sc, op);
}

static inline uint32_t
temp_from_count(struct imx6_anatop_softc *sc, uint32_t count)
{

	return (((sc->temp_high_val - (count - sc->temp_high_cnt) *
	    (sc->temp_high_val - 250) / 
	    (sc->temp_room_cnt - sc->temp_high_cnt))));
}

static inline uint32_t
temp_to_count(struct imx6_anatop_softc *sc, uint32_t temp)
{

	return ((sc->temp_room_cnt - sc->temp_high_cnt) * 
	    (sc->temp_high_val - temp) / (sc->temp_high_val - 250) + 
	    sc->temp_high_cnt);
}

static void
temp_update_count(struct imx6_anatop_softc *sc)
{
	uint32_t val;

	val = imx6_anatop_read_4(IMX6_ANALOG_TEMPMON_TEMPSENSE0);
	if (!(val & IMX6_ANALOG_TEMPMON_TEMPSENSE0_VALID))
		return;
	sc->temp_last_cnt =
	    (val & IMX6_ANALOG_TEMPMON_TEMPSENSE0_TEMP_CNT_MASK) >>
	    IMX6_ANALOG_TEMPMON_TEMPSENSE0_TEMP_CNT_SHIFT;
}

static int
temp_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	struct imx6_anatop_softc *sc = arg1;
	uint32_t t;

	temp_update_count(sc);

	t = temp_from_count(sc, sc->temp_last_cnt) + TZ_ZEROC;

	return (sysctl_handle_int(oidp, &t, 0, req));
}

static int
temp_throttle_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	struct imx6_anatop_softc *sc = arg1;
	int err;
	uint32_t temp;

	temp = sc->temp_throttle_val + TZ_ZEROC;
	err = sysctl_handle_int(oidp, &temp, 0, req);
	if (temp < TZ_ZEROC)
		return (ERANGE);
	temp -= TZ_ZEROC;
	if (err != 0 || req->newptr == NULL || temp == sc->temp_throttle_val)
		return (err);

	/* Value changed, update counts in softc and hardware. */
	sc->temp_throttle_val = temp;
	sc->temp_throttle_trigger_cnt = temp_to_count(sc, sc->temp_throttle_val);
	sc->temp_throttle_reset_cnt = temp_to_count(sc, sc->temp_throttle_val - 100);
	imx6_anatop_write_4(IMX6_ANALOG_TEMPMON_TEMPSENSE0_CLR,
	    IMX6_ANALOG_TEMPMON_TEMPSENSE0_ALARM_MASK);
	imx6_anatop_write_4(IMX6_ANALOG_TEMPMON_TEMPSENSE0_SET,
	    (sc->temp_throttle_trigger_cnt <<
	     IMX6_ANALOG_TEMPMON_TEMPSENSE0_ALARM_SHIFT));
	return (err);
}

static void
tempmon_gofast(struct imx6_anatop_softc *sc)
{

	if (sc->cpu_curmhz < sc->cpu_maxmhz) {
		cpufreq_set_clock(sc, cpufreq_nearest_oppt(sc, sc->cpu_maxmhz));
	}
}

static void
tempmon_goslow(struct imx6_anatop_softc *sc)
{

	if (sc->cpu_curmhz > sc->cpu_minmhz) {
		cpufreq_set_clock(sc, cpufreq_nearest_oppt(sc, sc->cpu_minmhz));
	}
}

static int
tempmon_intr(void *arg)
{
	struct imx6_anatop_softc *sc = arg;

	/*
	 * XXX Note that this code doesn't currently run (for some mysterious
	 * reason we just never get an interrupt), so the real monitoring is
	 * done by tempmon_throttle_check().
	 */
	tempmon_goslow(sc);
	/* XXX Schedule callout to speed back up eventually. */
	return (FILTER_HANDLED);
}

static void
tempmon_throttle_check(void *arg)
{
	struct imx6_anatop_softc *sc = arg;

	/* Lower counts are higher temperatures. */
	if (sc->temp_last_cnt < sc->temp_throttle_trigger_cnt)
		tempmon_goslow(sc);
	else if (sc->temp_last_cnt > (sc->temp_throttle_reset_cnt))
		tempmon_gofast(sc);

	callout_reset_sbt(&sc->temp_throttle_callout, sc->temp_throttle_delay,
		0, tempmon_throttle_check, sc, 0);

}

static void
initialize_tempmon(struct imx6_anatop_softc *sc)
{
	uint32_t cal;

	/*
	 * Fetch calibration data: a sensor count at room temperature (25C),
	 * a sensor count at a high temperature, and that temperature
	 */
	cal = fsl_ocotp_read_4(FSL_OCOTP_ANA1);
	sc->temp_room_cnt = (cal & 0xFFF00000) >> 20;
	sc->temp_high_cnt = (cal & 0x000FFF00) >> 8;
	sc->temp_high_val = (cal & 0x000000FF) * 10;

	/*
	 * Throttle to a lower cpu freq at 10C below the "hot" temperature, and
	 * reset back to max cpu freq at 5C below the trigger.
	 */
	sc->temp_throttle_val = sc->temp_high_val - 100;
	sc->temp_throttle_trigger_cnt =
	    temp_to_count(sc, sc->temp_throttle_val);
	sc->temp_throttle_reset_cnt = 
	    temp_to_count(sc, sc->temp_throttle_val - 50);

	/*
	 * Set the sensor to sample automatically at 16Hz (32.768KHz/0x800), set
	 * the throttle count, and begin making measurements.
	 */
	imx6_anatop_write_4(IMX6_ANALOG_TEMPMON_TEMPSENSE1, 0x0800);
	imx6_anatop_write_4(IMX6_ANALOG_TEMPMON_TEMPSENSE0,
	    (sc->temp_throttle_trigger_cnt << 
	    IMX6_ANALOG_TEMPMON_TEMPSENSE0_ALARM_SHIFT) |
	    IMX6_ANALOG_TEMPMON_TEMPSENSE0_MEASURE);

	/*
	 * XXX Note that the alarm-interrupt feature isn't working yet, so
	 * we'll use a callout handler to check at 10Hz.  Make sure we have an
	 * initial temperature reading before starting up the callouts so we
	 * don't get a bogus reading of zero.
	 */
	while (sc->temp_last_cnt == 0)
		temp_update_count(sc);
	sc->temp_throttle_delay = 100 * SBT_1MS;
	callout_init(&sc->temp_throttle_callout, 0);
	callout_reset_sbt(&sc->temp_throttle_callout, sc->temp_throttle_delay, 
	    0, tempmon_throttle_check, sc, 0);

	SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_hw_imx), 
	    OID_AUTO, "temperature", CTLTYPE_INT | CTLFLAG_RD, sc, 0,
	    temp_sysctl_handler, "IK", "Current die temperature");
	SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_hw_imx), 
	    OID_AUTO, "throttle_temperature", CTLTYPE_INT | CTLFLAG_RW, sc,
	    0, temp_throttle_sysctl_handler, "IK", 
	    "Throttle CPU when exceeding this temperature");
}

static void
intr_setup(void *arg)
{
	int rid;
	struct imx6_anatop_softc *sc;

	sc = arg;
	rid = 0;
	sc->res[IRQRES] = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->res[IRQRES] != NULL) {
		bus_setup_intr(sc->dev, sc->res[IRQRES],
		    INTR_TYPE_MISC | INTR_MPSAFE, tempmon_intr, NULL, sc,
		    &sc->temp_intrhand);
	} else {
		device_printf(sc->dev, "Cannot allocate IRQ resource\n");
	}
	config_intrhook_disestablish(&sc->intr_setup_hook);
}

static void
imx6_anatop_new_pass(device_t dev)
{
	struct imx6_anatop_softc *sc;
	const int cpu_init_pass = BUS_PASS_CPU + BUS_PASS_ORDER_MIDDLE;

	/*
	 * We attach during BUS_PASS_BUS (because some day we will be a
	 * simplebus that has regulator devices as children), but some of our
	 * init work cannot be done until BUS_PASS_CPU (we rely on other devices
	 * that attach on the CPU pass).
	 */
	sc = device_get_softc(dev);
	if (!sc->cpu_init_done && bus_current_pass >= cpu_init_pass) {
		sc->cpu_init_done = true;
		cpufreq_initialize(sc);
		initialize_tempmon(sc);
		if (bootverbose) {
			device_printf(sc->dev, "CPU %uMHz @ %umV\n", 
			    sc->cpu_curmhz, sc->cpu_curmv);
		}
	}
	bus_generic_new_pass(dev);
}

static int
imx6_anatop_detach(device_t dev)
{

	/* This device can never detach. */
	return (EBUSY);
}

static int
imx6_anatop_attach(device_t dev)
{
	struct imx6_anatop_softc *sc;
	int err;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Allocate bus_space resources. */
	if (bus_alloc_resources(dev, imx6_anatop_spec, sc->res)) {
		device_printf(dev, "Cannot allocate resources\n");
		err = ENXIO;
		goto out;
	}

	sc->intr_setup_hook.ich_func = intr_setup;
	sc->intr_setup_hook.ich_arg = sc;
	config_intrhook_establish(&sc->intr_setup_hook);

	SYSCTL_ADD_UINT(device_get_sysctl_ctx(sc->dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)),
	    OID_AUTO, "cpu_voltage", CTLFLAG_RD,
	    &sc->cpu_curmv, 0, "Current CPU voltage in millivolts");

	imx6_anatop_sc = sc;

	/*
	 * Other code seen on the net sets this SELFBIASOFF flag around the same
	 * time the temperature sensor is set up, although it's unclear how the
	 * two are related (if at all).
	 */
	imx6_anatop_write_4(IMX6_ANALOG_PMU_MISC0_SET, 
	    IMX6_ANALOG_PMU_MISC0_SELFBIASOFF);

	/*
	 * Some day, when we're ready to deal with the actual anatop regulators
	 * that are described in fdt data as children of this "bus", this would
	 * be the place to invoke a simplebus helper routine to instantiate the
	 * children from the fdt data.
	 */

	err = 0;

out:

	if (err != 0) {
		bus_release_resources(dev, imx6_anatop_spec, sc->res);
	}

	return (err);
}

uint32_t
pll4_configure_output(uint32_t mfi, uint32_t mfn, uint32_t mfd)
{
	int reg;

	/*
	 * Audio PLL (PLL4).
	 * PLL output frequency = Fref * (DIV_SELECT + NUM/DENOM)
	 */

	reg = (IMX6_ANALOG_CCM_PLL_AUDIO_ENABLE);
	reg &= ~(IMX6_ANALOG_CCM_PLL_AUDIO_DIV_SELECT_MASK << \
		IMX6_ANALOG_CCM_PLL_AUDIO_DIV_SELECT_SHIFT);
	reg |= (mfi << IMX6_ANALOG_CCM_PLL_AUDIO_DIV_SELECT_SHIFT);
	imx6_anatop_write_4(IMX6_ANALOG_CCM_PLL_AUDIO, reg);
	imx6_anatop_write_4(IMX6_ANALOG_CCM_PLL_AUDIO_NUM, mfn);
	imx6_anatop_write_4(IMX6_ANALOG_CCM_PLL_AUDIO_DENOM, mfd);

	return (0);
}

static int
imx6_anatop_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "fsl,imx6q-anatop") == 0)
		return (ENXIO);

	device_set_desc(dev, "Freescale i.MX6 Analog PLLs and Power");

	return (BUS_PROBE_DEFAULT);
}

uint32_t 
imx6_get_cpu_clock(void)
{
	uint32_t corediv, plldiv;

	corediv = imx_ccm_get_cacrr();
	plldiv = imx6_anatop_read_4(IMX6_ANALOG_CCM_PLL_ARM) &
	    IMX6_ANALOG_CCM_PLL_ARM_DIV_MASK;
	return (cpufreq_mhz_from_div(imx6_anatop_sc, corediv, plldiv));
}

static device_method_t imx6_anatop_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,  imx6_anatop_probe),
	DEVMETHOD(device_attach, imx6_anatop_attach),
	DEVMETHOD(device_detach, imx6_anatop_detach),

	/* Bus interface */
	DEVMETHOD(bus_new_pass,  imx6_anatop_new_pass),

	DEVMETHOD_END
};

static driver_t imx6_anatop_driver = {
	"imx6_anatop",
	imx6_anatop_methods,
	sizeof(struct imx6_anatop_softc)
};

static devclass_t imx6_anatop_devclass;

EARLY_DRIVER_MODULE(imx6_anatop, simplebus, imx6_anatop_driver,
    imx6_anatop_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);
EARLY_DRIVER_MODULE(imx6_anatop, ofwbus, imx6_anatop_driver,
    imx6_anatop_devclass, 0, 0, BUS_PASS_BUS + BUS_PASS_ORDER_MIDDLE);

