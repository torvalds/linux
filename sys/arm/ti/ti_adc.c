/*-
 * Copyright 2014 Luiz Otavio O Souza <loos@freebsd.org>
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

#include "opt_evdev.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/selinfo.h>
#include <sys/poll.h>
#include <sys/uio.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#ifdef EVDEV_SUPPORT
#include <dev/evdev/input.h>
#include <dev/evdev/evdev.h>
#endif

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_adcreg.h>
#include <arm/ti/ti_adcvar.h>

#undef	DEBUG_TSC

#define	DEFAULT_CHARGE_DELAY	0x400
#define	STEPDLY_OPEN		0x98

#define	ORDER_XP	0
#define	ORDER_XN	1
#define	ORDER_YP	2
#define	ORDER_YN	3

/* Define our 8 steps, one for each input channel. */
static struct ti_adc_input ti_adc_inputs[TI_ADC_NPINS] = {
	{ .stepconfig = ADC_STEPCFG(1), .stepdelay = ADC_STEPDLY(1) },
	{ .stepconfig = ADC_STEPCFG(2), .stepdelay = ADC_STEPDLY(2) },
	{ .stepconfig = ADC_STEPCFG(3), .stepdelay = ADC_STEPDLY(3) },
	{ .stepconfig = ADC_STEPCFG(4), .stepdelay = ADC_STEPDLY(4) },
	{ .stepconfig = ADC_STEPCFG(5), .stepdelay = ADC_STEPDLY(5) },
	{ .stepconfig = ADC_STEPCFG(6), .stepdelay = ADC_STEPDLY(6) },
	{ .stepconfig = ADC_STEPCFG(7), .stepdelay = ADC_STEPDLY(7) },
	{ .stepconfig = ADC_STEPCFG(8), .stepdelay = ADC_STEPDLY(8) },
};

static int ti_adc_samples[5] = { 0, 2, 4, 8, 16 };

static int ti_adc_detach(device_t dev);

#ifdef EVDEV_SUPPORT
static void
ti_adc_ev_report(struct ti_adc_softc *sc)
{

	evdev_push_event(sc->sc_evdev, EV_ABS, ABS_X, sc->sc_x);
	evdev_push_event(sc->sc_evdev, EV_ABS, ABS_Y, sc->sc_y);
	evdev_push_event(sc->sc_evdev, EV_KEY, BTN_TOUCH, sc->sc_pen_down);
	evdev_sync(sc->sc_evdev);
}
#endif /* EVDEV */

static void
ti_adc_enable(struct ti_adc_softc *sc)
{
	uint32_t reg;

	TI_ADC_LOCK_ASSERT(sc);

	if (sc->sc_last_state == 1)
		return;

	/* Enable the FIFO0 threshold and the end of sequence interrupt. */
	ADC_WRITE4(sc, ADC_IRQENABLE_SET,
	    ADC_IRQ_FIFO0_THRES | ADC_IRQ_FIFO1_THRES | ADC_IRQ_END_OF_SEQ);

	reg = ADC_CTRL_STEP_WP | ADC_CTRL_STEP_ID;
	if (sc->sc_tsc_wires > 0) {
		reg |= ADC_CTRL_TSC_ENABLE;
		switch (sc->sc_tsc_wires) {
		case 4:
			reg |= ADC_CTRL_TSC_4WIRE;
			break;
		case 5:
			reg |= ADC_CTRL_TSC_5WIRE;
			break;
		case 8:
			reg |= ADC_CTRL_TSC_8WIRE;
			break;
		default:
			break;
		}
	}
	reg |= ADC_CTRL_ENABLE;
	/* Enable the ADC.  Run thru enabled steps, start the conversions. */
	ADC_WRITE4(sc, ADC_CTRL, reg);

	sc->sc_last_state = 1;
}

static void
ti_adc_disable(struct ti_adc_softc *sc)
{
	int count;
	uint32_t data;

	TI_ADC_LOCK_ASSERT(sc);

	if (sc->sc_last_state == 0)
		return;

	/* Disable all the enabled steps. */
	ADC_WRITE4(sc, ADC_STEPENABLE, 0);

	/* Disable the ADC. */
	ADC_WRITE4(sc, ADC_CTRL, ADC_READ4(sc, ADC_CTRL) & ~ADC_CTRL_ENABLE);

	/* Disable the FIFO0 threshold and the end of sequence interrupt. */
	ADC_WRITE4(sc, ADC_IRQENABLE_CLR,
	    ADC_IRQ_FIFO0_THRES | ADC_IRQ_FIFO1_THRES | ADC_IRQ_END_OF_SEQ);

	/* ACK any pending interrupt. */
	ADC_WRITE4(sc, ADC_IRQSTATUS, ADC_READ4(sc, ADC_IRQSTATUS));

	/* Drain the FIFO data. */
	count = ADC_READ4(sc, ADC_FIFO0COUNT) & ADC_FIFO_COUNT_MSK;
	while (count > 0) {
		data = ADC_READ4(sc, ADC_FIFO0DATA);
		count = ADC_READ4(sc, ADC_FIFO0COUNT) & ADC_FIFO_COUNT_MSK;
	}

	count = ADC_READ4(sc, ADC_FIFO1COUNT) & ADC_FIFO_COUNT_MSK;
	while (count > 0) {
		data = ADC_READ4(sc, ADC_FIFO1DATA);
		count = ADC_READ4(sc, ADC_FIFO1COUNT) & ADC_FIFO_COUNT_MSK;
	}

	sc->sc_last_state = 0;
}

static int
ti_adc_setup(struct ti_adc_softc *sc)
{
	int ain, i;
	uint32_t enabled;

	TI_ADC_LOCK_ASSERT(sc);

	/* Check for enabled inputs. */
	enabled = sc->sc_tsc_enabled;
	for (i = 0; i < sc->sc_adc_nchannels; i++) {
		ain = sc->sc_adc_channels[i];
		if (ti_adc_inputs[ain].enable)
			enabled |= (1U << (ain + 1));
	}

	/* Set the ADC global status. */
	if (enabled != 0) {
		ti_adc_enable(sc);
		/* Update the enabled steps. */
		if (enabled != ADC_READ4(sc, ADC_STEPENABLE))
			ADC_WRITE4(sc, ADC_STEPENABLE, enabled);
	} else
		ti_adc_disable(sc);

	return (0);
}

static void
ti_adc_input_setup(struct ti_adc_softc *sc, int32_t ain)
{
	struct ti_adc_input *input;
	uint32_t reg, val;

	TI_ADC_LOCK_ASSERT(sc);

	input = &ti_adc_inputs[ain];
	reg = input->stepconfig;
	val = ADC_READ4(sc, reg);

	/* Set single ended operation. */
	val &= ~ADC_STEP_DIFF_CNTRL;

	/* Set the negative voltage reference. */
	val &= ~ADC_STEP_RFM_MSK;

	/* Set the positive voltage reference. */
	val &= ~ADC_STEP_RFP_MSK;

	/* Set the samples average. */
	val &= ~ADC_STEP_AVG_MSK;
	val |= input->samples << ADC_STEP_AVG_SHIFT;

	/* Select the desired input. */
	val &= ~ADC_STEP_INP_MSK;
	val |= ain << ADC_STEP_INP_SHIFT;

	/* Set the ADC to one-shot mode. */
	val &= ~ADC_STEP_MODE_MSK;

	ADC_WRITE4(sc, reg, val);
}

static void
ti_adc_reset(struct ti_adc_softc *sc)
{
	int ain, i;

	TI_ADC_LOCK_ASSERT(sc);

	/* Disable all the inputs. */
	for (i = 0; i < sc->sc_adc_nchannels; i++) {
		ain = sc->sc_adc_channels[i];
		ti_adc_inputs[ain].enable = 0;
	}
}

static int
ti_adc_clockdiv_proc(SYSCTL_HANDLER_ARGS)
{
	int error, reg;
	struct ti_adc_softc *sc;

	sc = (struct ti_adc_softc *)arg1;

	TI_ADC_LOCK(sc);
	reg = (int)ADC_READ4(sc, ADC_CLKDIV) + 1;
	TI_ADC_UNLOCK(sc);

	error = sysctl_handle_int(oidp, &reg, sizeof(reg), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/*
	 * The actual written value is the prescaler setting - 1.
	 * Enforce a minimum value of 10 (i.e. 9) which limits the maximum
	 * ADC clock to ~2.4Mhz (CLK_M_OSC / 10).
	 */
	reg--;
	if (reg < 9)
		reg = 9;
	if (reg > USHRT_MAX)
		reg = USHRT_MAX;

	TI_ADC_LOCK(sc);
	/* Disable the ADC. */
	ti_adc_disable(sc);
	/* Update the ADC prescaler setting. */
	ADC_WRITE4(sc, ADC_CLKDIV, reg);
	/* Enable the ADC again. */
	ti_adc_setup(sc);
	TI_ADC_UNLOCK(sc);

	return (0);
}

static int
ti_adc_enable_proc(SYSCTL_HANDLER_ARGS)
{
	int error;
	int32_t enable;
	struct ti_adc_softc *sc;
	struct ti_adc_input *input;

	input = (struct ti_adc_input *)arg1;
	sc = input->sc;

	enable = input->enable;
	error = sysctl_handle_int(oidp, &enable, sizeof(enable),
	    req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (enable)
		enable = 1;

	TI_ADC_LOCK(sc);
	/* Setup the ADC as needed. */
	if (input->enable != enable) {
		input->enable = enable;
		ti_adc_setup(sc);
		if (input->enable == 0)
			input->value = 0;
	}
	TI_ADC_UNLOCK(sc);

	return (0);
}

static int
ti_adc_open_delay_proc(SYSCTL_HANDLER_ARGS)
{
	int error, reg;
	struct ti_adc_softc *sc;
	struct ti_adc_input *input;

	input = (struct ti_adc_input *)arg1;
	sc = input->sc;

	TI_ADC_LOCK(sc);
	reg = (int)ADC_READ4(sc, input->stepdelay) & ADC_STEP_OPEN_DELAY;
	TI_ADC_UNLOCK(sc);

	error = sysctl_handle_int(oidp, &reg, sizeof(reg), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (reg < 0)
		reg = 0;

	TI_ADC_LOCK(sc);
	ADC_WRITE4(sc, input->stepdelay, reg & ADC_STEP_OPEN_DELAY);
	TI_ADC_UNLOCK(sc);

	return (0);
}

static int
ti_adc_samples_avg_proc(SYSCTL_HANDLER_ARGS)
{
	int error, samples, i;
	struct ti_adc_softc *sc;
	struct ti_adc_input *input;

	input = (struct ti_adc_input *)arg1;
	sc = input->sc;

	if (input->samples > nitems(ti_adc_samples))
		input->samples = nitems(ti_adc_samples);
	samples = ti_adc_samples[input->samples];

	error = sysctl_handle_int(oidp, &samples, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	TI_ADC_LOCK(sc);
	if (samples != ti_adc_samples[input->samples]) {
		input->samples = 0;
		for (i = 0; i < nitems(ti_adc_samples); i++)
			if (samples >= ti_adc_samples[i])
				input->samples = i;
		ti_adc_input_setup(sc, input->input);
	}
	TI_ADC_UNLOCK(sc);

	return (error);
}

static void
ti_adc_read_data(struct ti_adc_softc *sc)
{
	int count, ain;
	struct ti_adc_input *input;
	uint32_t data;

	TI_ADC_LOCK_ASSERT(sc);

	/* Read the available data. */
	count = ADC_READ4(sc, ADC_FIFO0COUNT) & ADC_FIFO_COUNT_MSK;
	while (count > 0) {
		data = ADC_READ4(sc, ADC_FIFO0DATA);
		ain = (data & ADC_FIFO_STEP_ID_MSK) >> ADC_FIFO_STEP_ID_SHIFT;
		input = &ti_adc_inputs[ain];
		if (input->enable == 0)
			input->value = 0;
		else
			input->value = (int32_t)(data & ADC_FIFO_DATA_MSK);
		count = ADC_READ4(sc, ADC_FIFO0COUNT) & ADC_FIFO_COUNT_MSK;
	}
}

static int
cmp_values(const void *a, const void *b)
{
	const uint32_t *v1, *v2;
	v1 = a;
	v2 = b;
	if (*v1 < *v2)
		return -1;
	if (*v1 > *v2)
		return 1;

	return (0);
}

static void
ti_adc_tsc_read_data(struct ti_adc_softc *sc)
{
	int count;
	uint32_t data[16];
	uint32_t x, y;
	int i, start, end;

	TI_ADC_LOCK_ASSERT(sc);

	/* Read the available data. */
	count = ADC_READ4(sc, ADC_FIFO1COUNT) & ADC_FIFO_COUNT_MSK;
	if (count == 0)
		return;

	i = 0;
	while (count > 0) {
		data[i++] = ADC_READ4(sc, ADC_FIFO1DATA) & ADC_FIFO_DATA_MSK;
		count = ADC_READ4(sc, ADC_FIFO1COUNT) & ADC_FIFO_COUNT_MSK;
	}

	if (sc->sc_coord_readouts > 3) {
		start = 1;
		end = sc->sc_coord_readouts - 1;
		qsort(data, sc->sc_coord_readouts,
			sizeof(data[0]), &cmp_values);
		qsort(&data[sc->sc_coord_readouts + 2],
			sc->sc_coord_readouts,
			sizeof(data[0]), &cmp_values);
	}
	else {
		start = 0;
		end = sc->sc_coord_readouts;
	}

	x = y = 0;
	for (i = start; i < end; i++)
		y += data[i];
	y /= (end - start);

	for (i = sc->sc_coord_readouts + 2 + start; i < sc->sc_coord_readouts + 2 + end; i++)
		x += data[i];
	x /= (end - start);

#ifdef DEBUG_TSC
	device_printf(sc->sc_dev, "touchscreen x: %d, y: %d\n", x, y);
#endif

#ifdef EVDEV_SUPPORT
	if ((sc->sc_x != x) || (sc->sc_y != y)) {
		sc->sc_x = x;
		sc->sc_y = y;
		ti_adc_ev_report(sc);
	}
#endif
}

static void
ti_adc_intr_locked(struct ti_adc_softc *sc, uint32_t status)
{
	/* Read the available data. */
	if (status & ADC_IRQ_FIFO0_THRES)
		ti_adc_read_data(sc);
}

static void
ti_adc_tsc_intr_locked(struct ti_adc_softc *sc, uint32_t status)
{
	/* Read the available data. */
	if (status & ADC_IRQ_FIFO1_THRES)
		ti_adc_tsc_read_data(sc);

}

static void
ti_adc_intr(void *arg)
{
	struct ti_adc_softc *sc;
	uint32_t status, rawstatus;

	sc = (struct ti_adc_softc *)arg;

	TI_ADC_LOCK(sc);

	rawstatus = ADC_READ4(sc, ADC_IRQSTATUS_RAW);
	status = ADC_READ4(sc, ADC_IRQSTATUS);

	if (rawstatus & ADC_IRQ_HW_PEN_ASYNC) {
		sc->sc_pen_down = 1;
		status |= ADC_IRQ_HW_PEN_ASYNC;
		ADC_WRITE4(sc, ADC_IRQENABLE_CLR,
			ADC_IRQ_HW_PEN_ASYNC);
#ifdef EVDEV_SUPPORT
		ti_adc_ev_report(sc);
#endif
	}

	if (rawstatus & ADC_IRQ_PEN_UP) {
		sc->sc_pen_down = 0;
		status |= ADC_IRQ_PEN_UP;
#ifdef EVDEV_SUPPORT
		ti_adc_ev_report(sc);
#endif
	}

	if (status & ADC_IRQ_FIFO0_THRES)
		ti_adc_intr_locked(sc, status);

	if (status & ADC_IRQ_FIFO1_THRES)
		ti_adc_tsc_intr_locked(sc, status);

	if (status) {
		/* ACK the interrupt. */
		ADC_WRITE4(sc, ADC_IRQSTATUS, status);
	}

	/* Start the next conversion ? */
	if (status & ADC_IRQ_END_OF_SEQ)
		ti_adc_setup(sc);

	TI_ADC_UNLOCK(sc);
}

static void
ti_adc_sysctl_init(struct ti_adc_softc *sc)
{
	char pinbuf[3];
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree_node, *inp_node, *inpN_node;
	struct sysctl_oid_list *tree, *inp_tree, *inpN_tree;
	int ain, i;

	/*
	 * Add per-pin sysctl tree/handlers.
	 */
	ctx = device_get_sysctl_ctx(sc->sc_dev);
	tree_node = device_get_sysctl_tree(sc->sc_dev);
	tree = SYSCTL_CHILDREN(tree_node);
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "clockdiv",
	    CTLFLAG_RW | CTLTYPE_UINT,  sc, 0,
	    ti_adc_clockdiv_proc, "IU", "ADC clock prescaler");
	inp_node = SYSCTL_ADD_NODE(ctx, tree, OID_AUTO, "ain",
	    CTLFLAG_RD, NULL, "ADC inputs");
	inp_tree = SYSCTL_CHILDREN(inp_node);

	for (i = 0; i < sc->sc_adc_nchannels; i++) {
		ain = sc->sc_adc_channels[i];

		snprintf(pinbuf, sizeof(pinbuf), "%d", ain);
		inpN_node = SYSCTL_ADD_NODE(ctx, inp_tree, OID_AUTO, pinbuf,
		    CTLFLAG_RD, NULL, "ADC input");
		inpN_tree = SYSCTL_CHILDREN(inpN_node);

		SYSCTL_ADD_PROC(ctx, inpN_tree, OID_AUTO, "enable",
		    CTLFLAG_RW | CTLTYPE_UINT, &ti_adc_inputs[ain], 0,
		    ti_adc_enable_proc, "IU", "Enable ADC input");
		SYSCTL_ADD_PROC(ctx, inpN_tree, OID_AUTO, "open_delay",
		    CTLFLAG_RW | CTLTYPE_UINT,  &ti_adc_inputs[ain], 0,
		    ti_adc_open_delay_proc, "IU", "ADC open delay");
		SYSCTL_ADD_PROC(ctx, inpN_tree, OID_AUTO, "samples_avg",
		    CTLFLAG_RW | CTLTYPE_UINT,  &ti_adc_inputs[ain], 0,
		    ti_adc_samples_avg_proc, "IU", "ADC samples average");
		SYSCTL_ADD_INT(ctx, inpN_tree, OID_AUTO, "input",
		    CTLFLAG_RD, &ti_adc_inputs[ain].value, 0,
		    "Converted raw value for the ADC input");
	}
}

static void
ti_adc_inputs_init(struct ti_adc_softc *sc)
{
	int ain, i;
	struct ti_adc_input *input;

	TI_ADC_LOCK(sc);
	for (i = 0; i < sc->sc_adc_nchannels; i++) {
		ain = sc->sc_adc_channels[i];
		input = &ti_adc_inputs[ain];
		input->sc = sc;
		input->input = ain;
		input->value = 0;
		input->enable = 0;
		input->samples = 0;
		ti_adc_input_setup(sc, ain);
	}
	TI_ADC_UNLOCK(sc);
}

static void
ti_adc_tsc_init(struct ti_adc_softc *sc)
{
	int i, start_step, end_step;
	uint32_t stepconfig, val;

	TI_ADC_LOCK(sc);

	/* X coordinates */
	stepconfig = ADC_STEP_FIFO1 | (4 << ADC_STEP_AVG_SHIFT) |
	    ADC_STEP_MODE_HW_ONESHOT | sc->sc_xp_bit;
	if (sc->sc_tsc_wires == 4)
		stepconfig |= ADC_STEP_INP(sc->sc_yp_inp) | sc->sc_xn_bit;
	else if (sc->sc_tsc_wires == 5)
		stepconfig |= ADC_STEP_INP(4) |
			sc->sc_xn_bit | sc->sc_yn_bit | sc->sc_yp_bit;
	else if (sc->sc_tsc_wires == 8)
		stepconfig |= ADC_STEP_INP(sc->sc_yp_inp) | sc->sc_xn_bit;

	start_step = ADC_STEPS - sc->sc_coord_readouts + 1;
	end_step = start_step + sc->sc_coord_readouts - 1;
	for (i = start_step; i <= end_step; i++) {
		ADC_WRITE4(sc, ADC_STEPCFG(i), stepconfig);
		ADC_WRITE4(sc, ADC_STEPDLY(i), STEPDLY_OPEN);
	}

	/* Y coordinates */
	stepconfig = ADC_STEP_FIFO1 | (4 << ADC_STEP_AVG_SHIFT) |
	    ADC_STEP_MODE_HW_ONESHOT | sc->sc_yn_bit |
	    ADC_STEP_INM(8);
	if (sc->sc_tsc_wires == 4)
		stepconfig |= ADC_STEP_INP(sc->sc_xp_inp) | sc->sc_yp_bit;
	else if (sc->sc_tsc_wires == 5)
		stepconfig |= ADC_STEP_INP(4) |
			sc->sc_xp_bit | sc->sc_xn_bit | sc->sc_yp_bit;
	else if (sc->sc_tsc_wires == 8)
		stepconfig |= ADC_STEP_INP(sc->sc_xp_inp) | sc->sc_yp_bit;

	start_step = ADC_STEPS - (sc->sc_coord_readouts*2 + 2) + 1;
	end_step = start_step + sc->sc_coord_readouts - 1;
	for (i = start_step; i <= end_step; i++) {
		ADC_WRITE4(sc, ADC_STEPCFG(i), stepconfig);
		ADC_WRITE4(sc, ADC_STEPDLY(i), STEPDLY_OPEN);
	}

	/* Charge config */
	val = ADC_READ4(sc, ADC_IDLECONFIG);
	ADC_WRITE4(sc, ADC_TC_CHARGE_STEPCONFIG, val);
	ADC_WRITE4(sc, ADC_TC_CHARGE_DELAY, sc->sc_charge_delay);

	/* 2 steps for Z */
	start_step = ADC_STEPS - (sc->sc_coord_readouts + 2) + 1;
	stepconfig = ADC_STEP_FIFO1 | (4 << ADC_STEP_AVG_SHIFT) |
	    ADC_STEP_MODE_HW_ONESHOT | sc->sc_yp_bit |
	    sc->sc_xn_bit | ADC_STEP_INP(sc->sc_xp_inp) |
	    ADC_STEP_INM(8);
	ADC_WRITE4(sc, ADC_STEPCFG(start_step), stepconfig);
	ADC_WRITE4(sc, ADC_STEPDLY(start_step), STEPDLY_OPEN);
	start_step++;
	stepconfig |= ADC_STEP_INP(sc->sc_yn_inp);
	ADC_WRITE4(sc, ADC_STEPCFG(start_step), stepconfig);
	ADC_WRITE4(sc, ADC_STEPDLY(start_step), STEPDLY_OPEN);

	ADC_WRITE4(sc, ADC_FIFO1THRESHOLD, (sc->sc_coord_readouts*2 + 2) - 1);

	sc->sc_tsc_enabled = 1;
	start_step = ADC_STEPS - (sc->sc_coord_readouts*2 + 2) + 1;
	end_step = ADC_STEPS;
	for (i = start_step; i <= end_step; i++) {
		sc->sc_tsc_enabled |= (1 << i);
	}


	TI_ADC_UNLOCK(sc);
}

static void
ti_adc_idlestep_init(struct ti_adc_softc *sc)
{
	uint32_t val;

	val = ADC_STEP_YNN_SW | ADC_STEP_INM(8) | ADC_STEP_INP(8) | ADC_STEP_YPN_SW;

	ADC_WRITE4(sc, ADC_IDLECONFIG, val);
}

static int
ti_adc_config_wires(struct ti_adc_softc *sc, int *wire_configs, int nwire_configs)
{
	int i;
	int wire, ai;

	for (i = 0; i < nwire_configs; i++) {
		wire = wire_configs[i] & 0xf;
		ai = (wire_configs[i] >> 4) & 0xf;
		switch (wire) {
		case ORDER_XP:
			sc->sc_xp_bit = ADC_STEP_XPP_SW;
			sc->sc_xp_inp = ai;
			break;
		case ORDER_XN:
			sc->sc_xn_bit = ADC_STEP_XNN_SW;
			sc->sc_xn_inp = ai;
			break;
		case ORDER_YP:
			sc->sc_yp_bit = ADC_STEP_YPP_SW;
			sc->sc_yp_inp = ai;
			break;
		case ORDER_YN:
			sc->sc_yn_bit = ADC_STEP_YNN_SW;
			sc->sc_yn_inp = ai;
			break;
		default:
			device_printf(sc->sc_dev, "Invalid wire config\n");
			return (-1);
		}
	}
	return (0);
}

static int
ti_adc_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "ti,am3359-tscadc"))
		return (ENXIO);
	device_set_desc(dev, "TI ADC controller");

	return (BUS_PROBE_DEFAULT);
}

static int
ti_adc_attach(device_t dev)
{
	int err, rid, i;
	struct ti_adc_softc *sc;
	uint32_t rev, reg;
	phandle_t node, child;
	pcell_t cell;
	int *channels;
	int nwire_configs;
	int *wire_configs;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	node = ofw_bus_get_node(dev);

	sc->sc_tsc_wires = 0;
	sc->sc_coord_readouts = 1;
	sc->sc_x_plate_resistance = 0;
	sc->sc_charge_delay = DEFAULT_CHARGE_DELAY;
	/* Read "tsc" node properties */
	child = ofw_bus_find_child(node, "tsc");
	if (child != 0 && OF_hasprop(child, "ti,wires")) {
		if ((OF_getencprop(child, "ti,wires", &cell, sizeof(cell))) > 0)
			sc->sc_tsc_wires = cell;
		if ((OF_getencprop(child, "ti,coordinate-readouts", &cell,
		    sizeof(cell))) > 0)
			sc->sc_coord_readouts = cell;
		if ((OF_getencprop(child, "ti,x-plate-resistance", &cell,
		    sizeof(cell))) > 0)
			sc->sc_x_plate_resistance = cell;
		if ((OF_getencprop(child, "ti,charge-delay", &cell,
		    sizeof(cell))) > 0)
			sc->sc_charge_delay = cell;
		nwire_configs = OF_getencprop_alloc_multi(child,
		    "ti,wire-config", sizeof(*wire_configs),
		    (void **)&wire_configs);
		if (nwire_configs != sc->sc_tsc_wires) {
			device_printf(sc->sc_dev,
			    "invalid number of ti,wire-config: %d (should be %d)\n",
			    nwire_configs, sc->sc_tsc_wires);
			OF_prop_free(wire_configs);
			return (EINVAL);
		}
		err = ti_adc_config_wires(sc, wire_configs, nwire_configs);
		OF_prop_free(wire_configs);
		if (err)
			return (EINVAL);
	}

	/* Read "adc" node properties */
	child = ofw_bus_find_child(node, "adc");
	if (child != 0) {
		sc->sc_adc_nchannels = OF_getencprop_alloc_multi(child,
		    "ti,adc-channels", sizeof(*channels), (void **)&channels);
		if (sc->sc_adc_nchannels > 0) {
			for (i = 0; i < sc->sc_adc_nchannels; i++)
				sc->sc_adc_channels[i] = channels[i];
			OF_prop_free(channels);
		}
	}

	/* Sanity check FDT data */
	if (sc->sc_tsc_wires + sc->sc_adc_nchannels > TI_ADC_NPINS) {
		device_printf(dev, "total number of chanels (%d) is larger than %d\n",
		    sc->sc_tsc_wires + sc->sc_adc_nchannels, TI_ADC_NPINS);
		return (ENXIO);
	}

	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "cannot allocate memory window\n");
		return (ENXIO);
	}

	/* Activate the ADC_TSC module. */
	err = ti_prcm_clk_enable(TSC_ADC_CLK);
	if (err)
		return (err);

	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (!sc->sc_irq_res) {
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "cannot allocate interrupt\n");
		return (ENXIO);
	}

	if (bus_setup_intr(dev, sc->sc_irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, ti_adc_intr, sc, &sc->sc_intrhand) != 0) {
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);
		device_printf(dev, "Unable to setup the irq handler.\n");
		return (ENXIO);
	}

	/* Check the ADC revision. */
	rev = ADC_READ4(sc, ADC_REVISION);
	device_printf(dev,
	    "scheme: %#x func: %#x rtl: %d rev: %d.%d custom rev: %d\n",
	    (rev & ADC_REV_SCHEME_MSK) >> ADC_REV_SCHEME_SHIFT,
	    (rev & ADC_REV_FUNC_MSK) >> ADC_REV_FUNC_SHIFT,
	    (rev & ADC_REV_RTL_MSK) >> ADC_REV_RTL_SHIFT,
	    (rev & ADC_REV_MAJOR_MSK) >> ADC_REV_MAJOR_SHIFT,
	    rev & ADC_REV_MINOR_MSK,
	    (rev & ADC_REV_CUSTOM_MSK) >> ADC_REV_CUSTOM_SHIFT);

	reg = ADC_READ4(sc, ADC_CTRL);
	ADC_WRITE4(sc, ADC_CTRL, reg | ADC_CTRL_STEP_WP | ADC_CTRL_STEP_ID);

	/*
	 * Set the ADC prescaler to 2400 if touchscreen is not enabled
	 * and to 24 if it is.  This sets the ADC clock to ~10Khz and
	 * ~1Mhz respectively (CLK_M_OSC / prescaler).
	 */
	if (sc->sc_tsc_wires)
		ADC_WRITE4(sc, ADC_CLKDIV, 24 - 1);
	else
		ADC_WRITE4(sc, ADC_CLKDIV, 2400 - 1);

	TI_ADC_LOCK_INIT(sc);

	ti_adc_idlestep_init(sc);
	ti_adc_inputs_init(sc);
	ti_adc_sysctl_init(sc);
	ti_adc_tsc_init(sc);

	TI_ADC_LOCK(sc);
	ti_adc_setup(sc);
	TI_ADC_UNLOCK(sc);

#ifdef EVDEV_SUPPORT
	if (sc->sc_tsc_wires > 0) {
		sc->sc_evdev = evdev_alloc();
		evdev_set_name(sc->sc_evdev, device_get_desc(dev));
		evdev_set_phys(sc->sc_evdev, device_get_nameunit(dev));
		evdev_set_id(sc->sc_evdev, BUS_VIRTUAL, 0, 0, 0);
		evdev_support_prop(sc->sc_evdev, INPUT_PROP_DIRECT);
		evdev_support_event(sc->sc_evdev, EV_SYN);
		evdev_support_event(sc->sc_evdev, EV_ABS);
		evdev_support_event(sc->sc_evdev, EV_KEY);

		evdev_support_abs(sc->sc_evdev, ABS_X, 0, 0,
		    ADC_MAX_VALUE, 0, 0, 0);
		evdev_support_abs(sc->sc_evdev, ABS_Y, 0, 0,
		    ADC_MAX_VALUE, 0, 0, 0);

		evdev_support_key(sc->sc_evdev, BTN_TOUCH);

		err = evdev_register(sc->sc_evdev);
		if (err) {
			device_printf(dev,
			    "failed to register evdev: error=%d\n", err);
			ti_adc_detach(dev);
			return (err);
		}

		sc->sc_pen_down = 0;
		sc->sc_x = -1;
		sc->sc_y = -1;
	}
#endif /* EVDEV */

	return (0);
}

static int
ti_adc_detach(device_t dev)
{
	struct ti_adc_softc *sc;

	sc = device_get_softc(dev);

	/* Turn off the ADC. */
	TI_ADC_LOCK(sc);
	ti_adc_reset(sc);
	ti_adc_setup(sc);

#ifdef EVDEV_SUPPORT
	evdev_free(sc->sc_evdev);
#endif

	TI_ADC_UNLOCK(sc);

	TI_ADC_LOCK_DESTROY(sc);

	if (sc->sc_intrhand)
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_intrhand);
	if (sc->sc_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sc_irq_res);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->sc_mem_res);

	return (bus_generic_detach(dev));
}

static device_method_t ti_adc_methods[] = {
	DEVMETHOD(device_probe,		ti_adc_probe),
	DEVMETHOD(device_attach,	ti_adc_attach),
	DEVMETHOD(device_detach,	ti_adc_detach),

	DEVMETHOD_END
};

static driver_t ti_adc_driver = {
	"ti_adc",
	ti_adc_methods,
	sizeof(struct ti_adc_softc),
};

static devclass_t ti_adc_devclass;

DRIVER_MODULE(ti_adc, simplebus, ti_adc_driver, ti_adc_devclass, 0, 0);
MODULE_VERSION(ti_adc, 1);
MODULE_DEPEND(ti_adc, simplebus, 1, 1, 1);
#ifdef EVDEV_SUPPORT
MODULE_DEPEND(ti_adc, evdev, 1, 1, 1);
#endif
