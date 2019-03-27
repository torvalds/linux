/*-
 * Copyright (C) 2013-2015 Daisuke Aoyama <aoyama@peach.ne.jp>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sema.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/broadcom/bcm2835/bcm2835_mbox.h>
#include <arm/broadcom/bcm2835/bcm2835_mbox_prop.h>
#include <arm/broadcom/bcm2835/bcm2835_vcbus.h>

#include "cpufreq_if.h"
#include "mbox_if.h"

#ifdef DEBUG
#define DPRINTF(fmt, ...) do {			\
	printf("%s:%u: ", __func__, __LINE__);	\
	printf(fmt, ##__VA_ARGS__);		\
} while (0)
#else
#define DPRINTF(fmt, ...)
#endif

#define	HZ2MHZ(freq) ((freq) / (1000 * 1000))
#define	MHZ2HZ(freq) ((freq) * (1000 * 1000))

#ifdef SOC_BCM2835
#define	OFFSET2MVOLT(val) (1200 + ((val) * 25))
#define	MVOLT2OFFSET(val) (((val) - 1200) / 25)
#define	DEFAULT_ARM_FREQUENCY	 700
#define	DEFAULT_LOWEST_FREQ	 300
#else
#define	OFFSET2MVOLT(val) (((val) / 1000))
#define	MVOLT2OFFSET(val) (((val) * 1000))
#define	DEFAULT_ARM_FREQUENCY	 600
#define	DEFAULT_LOWEST_FREQ	 600
#endif
#define	DEFAULT_CORE_FREQUENCY	 250
#define	DEFAULT_SDRAM_FREQUENCY	 400
#define	TRANSITION_LATENCY	1000
#define	MIN_OVER_VOLTAGE	 -16
#define	MAX_OVER_VOLTAGE	   6
#define	MSG_ERROR	  -999999999
#define	MHZSTEP			 100
#define	HZSTEP	   (MHZ2HZ(MHZSTEP))
#define	TZ_ZEROC		2731

#define VC_LOCK(sc) do {			\
		sema_wait(&vc_sema);		\
	} while (0)
#define VC_UNLOCK(sc) do {			\
		sema_post(&vc_sema);		\
	} while (0)

/* ARM->VC mailbox property semaphore */
static struct sema vc_sema;

static struct sysctl_ctx_list bcm2835_sysctl_ctx;

struct bcm2835_cpufreq_softc {
	device_t	dev;
	int		arm_max_freq;
	int		arm_min_freq;
	int		core_max_freq;
	int		core_min_freq;
	int		sdram_max_freq;
	int		sdram_min_freq;
	int		max_voltage_core;
	int		min_voltage_core;

	/* the values written in mbox */
	int		voltage_core;
	int		voltage_sdram;
	int		voltage_sdram_c;
	int		voltage_sdram_i;
	int		voltage_sdram_p;
	int		turbo_mode;

	/* initial hook for waiting mbox intr */
	struct intr_config_hook	init_hook;
};

static struct ofw_compat_data compat_data[] = {
	{ "broadcom,bcm2835-vc",	1 },
	{ "broadcom,bcm2708-vc",	1 },
	{ "brcm,bcm2709",	1 },
	{ "brcm,bcm2836",	1 },
	{ "brcm,bcm2837",	1 },
	{ NULL, 0 }
};

static int cpufreq_verbose = 0;
TUNABLE_INT("hw.bcm2835.cpufreq.verbose", &cpufreq_verbose);
static int cpufreq_lowest_freq = DEFAULT_LOWEST_FREQ;
TUNABLE_INT("hw.bcm2835.cpufreq.lowest_freq", &cpufreq_lowest_freq);

#ifdef PROP_DEBUG
static void
bcm2835_dump(const void *data, int len)
{
	const uint8_t *p = (const uint8_t*)data;
	int i;

	printf("dump @ %p:\n", data);
	for (i = 0; i < len; i++) {
		printf("%2.2x ", p[i]);
		if ((i % 4) == 3)
			printf(" ");
		if ((i % 16) == 15)
			printf("\n");
	}
	printf("\n");
}
#endif

static int
bcm2835_cpufreq_get_clock_rate(struct bcm2835_cpufreq_softc *sc,
    uint32_t clock_id)
{
	struct msg_get_clock_rate msg;
	int rate;
	int err;

	/*
	 * Get clock rate
	 *   Tag: 0x00030002
	 *   Request:
	 *     Length: 4
	 *     Value:
	 *       u32: clock id
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: clock id
	 *       u32: rate (in Hz)
	 */

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_GET_CLOCK_RATE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.clock_id = clock_id;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't get clock rate (id=%u)\n",
		    clock_id);
		return (MSG_ERROR);
	}

	/* result (Hz) */
	rate = (int)msg.body.resp.rate_hz;
	DPRINTF("clock = %d(Hz)\n", rate);
	return (rate);
}

static int
bcm2835_cpufreq_get_max_clock_rate(struct bcm2835_cpufreq_softc *sc,
    uint32_t clock_id)
{
	struct msg_get_max_clock_rate msg;
	int rate;
	int err;

	/*
	 * Get max clock rate
	 *   Tag: 0x00030004
	 *   Request:
	 *     Length: 4
	 *     Value:
	 *       u32: clock id
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: clock id
	 *       u32: rate (in Hz)
	 */

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_GET_MAX_CLOCK_RATE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.clock_id = clock_id;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't get max clock rate (id=%u)\n",
		    clock_id);
		return (MSG_ERROR);
	}

	/* result (Hz) */
	rate = (int)msg.body.resp.rate_hz;
	DPRINTF("clock = %d(Hz)\n", rate);
	return (rate);
}

static int
bcm2835_cpufreq_get_min_clock_rate(struct bcm2835_cpufreq_softc *sc,
    uint32_t clock_id)
{
	struct msg_get_min_clock_rate msg;
	int rate;
	int err;

	/*
	 * Get min clock rate
	 *   Tag: 0x00030007
	 *   Request:
	 *     Length: 4
	 *     Value:
	 *       u32: clock id
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: clock id
	 *       u32: rate (in Hz)
	 */

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_GET_MIN_CLOCK_RATE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.clock_id = clock_id;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't get min clock rate (id=%u)\n",
		    clock_id);
		return (MSG_ERROR);
	}

	/* result (Hz) */
	rate = (int)msg.body.resp.rate_hz;
	DPRINTF("clock = %d(Hz)\n", rate);
	return (rate);
}

static int
bcm2835_cpufreq_set_clock_rate(struct bcm2835_cpufreq_softc *sc,
    uint32_t clock_id, uint32_t rate_hz)
{
	struct msg_set_clock_rate msg;
	int rate;
	int err;

	/*
	 * Set clock rate
	 *   Tag: 0x00038002
	 *   Request:
	 *     Length: 8
	 *     Value:
	 *       u32: clock id
	 *       u32: rate (in Hz)
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: clock id
	 *       u32: rate (in Hz)
	 */

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_SET_CLOCK_RATE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.clock_id = clock_id;
	msg.body.req.rate_hz = rate_hz;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't set clock rate (id=%u)\n",
		    clock_id);
		return (MSG_ERROR);
	}

	/* workaround for core clock */
	if (clock_id == BCM2835_MBOX_CLOCK_ID_CORE) {
		/* for safety (may change voltage without changing clock) */
		DELAY(TRANSITION_LATENCY);

		/*
		 * XXX: the core clock is unable to change at once,
		 * to change certainly, write it twice now.
		 */

		/* setup single tag buffer */
		memset(&msg, 0, sizeof(msg));
		msg.hdr.buf_size = sizeof(msg);
		msg.hdr.code = BCM2835_MBOX_CODE_REQ;
		msg.tag_hdr.tag = BCM2835_MBOX_TAG_SET_CLOCK_RATE;
		msg.tag_hdr.val_buf_size = sizeof(msg.body);
		msg.tag_hdr.val_len = sizeof(msg.body.req);
		msg.body.req.clock_id = clock_id;
		msg.body.req.rate_hz = rate_hz;
		msg.end_tag = 0;

		/* call mailbox property */
		err = bcm2835_mbox_property(&msg, sizeof(msg));
		if (err) {
			device_printf(sc->dev,
			    "can't set clock rate (id=%u)\n", clock_id);
			return (MSG_ERROR);
		}
	}

	/* result (Hz) */
	rate = (int)msg.body.resp.rate_hz;
	DPRINTF("clock = %d(Hz)\n", rate);
	return (rate);
}

static int
bcm2835_cpufreq_get_turbo(struct bcm2835_cpufreq_softc *sc)
{
	struct msg_get_turbo msg;
	int level;
	int err;

	/*
	 * Get turbo
	 *   Tag: 0x00030009
	 *   Request:
	 *     Length: 4
	 *     Value:
	 *       u32: id
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: id
	 *       u32: level
	 */

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_GET_TURBO;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.id = 0;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't get turbo\n");
		return (MSG_ERROR);
	}

	/* result 0=non-turbo, 1=turbo */
	level = (int)msg.body.resp.level;
	DPRINTF("level = %d\n", level);
	return (level);
}

static int
bcm2835_cpufreq_set_turbo(struct bcm2835_cpufreq_softc *sc, uint32_t level)
{
	struct msg_set_turbo msg;
	int value;
	int err;

	/*
	 * Set turbo
	 *   Tag: 0x00038009
	 *   Request:
	 *     Length: 8
	 *     Value:
	 *       u32: id
	 *       u32: level
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: id
	 *       u32: level
	 */

	/* replace unknown value to OFF */
	if (level != BCM2835_MBOX_TURBO_ON && level != BCM2835_MBOX_TURBO_OFF)
		level = BCM2835_MBOX_TURBO_OFF;

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_SET_TURBO;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.id = 0;
	msg.body.req.level = level;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't set turbo\n");
		return (MSG_ERROR);
	}

	/* result 0=non-turbo, 1=turbo */
	value = (int)msg.body.resp.level;
	DPRINTF("level = %d\n", value);
	return (value);
}

static int
bcm2835_cpufreq_get_voltage(struct bcm2835_cpufreq_softc *sc,
    uint32_t voltage_id)
{
	struct msg_get_voltage msg;
	int value;
	int err;

	/*
	 * Get voltage
	 *   Tag: 0x00030003
	 *   Request:
	 *     Length: 4
	 *     Value:
	 *       u32: voltage id
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: voltage id
	 *       u32: value (offset from 1.2V in units of 0.025V)
	 */

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_GET_VOLTAGE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.voltage_id = voltage_id;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't get voltage\n");
		return (MSG_ERROR);
	}

	/* result (offset from 1.2V) */
	value = (int)msg.body.resp.value;
	DPRINTF("value = %d\n", value);
	return (value);
}

static int
bcm2835_cpufreq_get_max_voltage(struct bcm2835_cpufreq_softc *sc,
    uint32_t voltage_id)
{
	struct msg_get_max_voltage msg;
	int value;
	int err;

	/*
	 * Get voltage
	 *   Tag: 0x00030005
	 *   Request:
	 *     Length: 4
	 *     Value:
	 *       u32: voltage id
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: voltage id
	 *       u32: value (offset from 1.2V in units of 0.025V)
	 */

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_GET_MAX_VOLTAGE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.voltage_id = voltage_id;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't get max voltage\n");
		return (MSG_ERROR);
	}

	/* result (offset from 1.2V) */
	value = (int)msg.body.resp.value;
	DPRINTF("value = %d\n", value);
	return (value);
}
static int
bcm2835_cpufreq_get_min_voltage(struct bcm2835_cpufreq_softc *sc,
    uint32_t voltage_id)
{
	struct msg_get_min_voltage msg;
	int value;
	int err;

	/*
	 * Get voltage
	 *   Tag: 0x00030008
	 *   Request:
	 *     Length: 4
	 *     Value:
	 *       u32: voltage id
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: voltage id
	 *       u32: value (offset from 1.2V in units of 0.025V)
	 */

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_GET_MIN_VOLTAGE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.voltage_id = voltage_id;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't get min voltage\n");
		return (MSG_ERROR);
	}

	/* result (offset from 1.2V) */
	value = (int)msg.body.resp.value;
	DPRINTF("value = %d\n", value);
	return (value);
}

static int
bcm2835_cpufreq_set_voltage(struct bcm2835_cpufreq_softc *sc,
    uint32_t voltage_id, int32_t value)
{
	struct msg_set_voltage msg;
	int err;

	/*
	 * Set voltage
	 *   Tag: 0x00038003
	 *   Request:
	 *     Length: 4
	 *     Value:
	 *       u32: voltage id
	 *       u32: value (offset from 1.2V in units of 0.025V)
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: voltage id
	 *       u32: value (offset from 1.2V in units of 0.025V)
	 */

	/*
	 * over_voltage:
	 * 0 (1.2 V). Values above 6 are only allowed when force_turbo or
	 * current_limit_override are specified (which set the warranty bit).
	 */
	if (value > MAX_OVER_VOLTAGE || value < MIN_OVER_VOLTAGE) {
		/* currently not supported */
		device_printf(sc->dev, "not supported voltage: %d\n", value);
		return (MSG_ERROR);
	}

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_SET_VOLTAGE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.voltage_id = voltage_id;
	msg.body.req.value = (uint32_t)value;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't set voltage\n");
		return (MSG_ERROR);
	}

	/* result (offset from 1.2V) */
	value = (int)msg.body.resp.value;
	DPRINTF("value = %d\n", value);
	return (value);
}

static int
bcm2835_cpufreq_get_temperature(struct bcm2835_cpufreq_softc *sc)
{
	struct msg_get_temperature msg;
	int value;
	int err;

	/*
	 * Get temperature
	 *   Tag: 0x00030006
	 *   Request:
	 *     Length: 4
	 *     Value:
	 *       u32: temperature id
	 *   Response:
	 *     Length: 8
	 *     Value:
	 *       u32: temperature id
	 *       u32: value
	 */

	/* setup single tag buffer */
	memset(&msg, 0, sizeof(msg));
	msg.hdr.buf_size = sizeof(msg);
	msg.hdr.code = BCM2835_MBOX_CODE_REQ;
	msg.tag_hdr.tag = BCM2835_MBOX_TAG_GET_TEMPERATURE;
	msg.tag_hdr.val_buf_size = sizeof(msg.body);
	msg.tag_hdr.val_len = sizeof(msg.body.req);
	msg.body.req.temperature_id = 0;
	msg.end_tag = 0;

	/* call mailbox property */
	err = bcm2835_mbox_property(&msg, sizeof(msg));
	if (err) {
		device_printf(sc->dev, "can't get temperature\n");
		return (MSG_ERROR);
	}

	/* result (temperature of degree C) */
	value = (int)msg.body.resp.value;
	DPRINTF("value = %d\n", value);
	return (value);
}



static int
sysctl_bcm2835_cpufreq_arm_freq(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* get realtime value */
	VC_LOCK(sc);
	val = bcm2835_cpufreq_get_clock_rate(sc, BCM2835_MBOX_CLOCK_ID_ARM);
	VC_UNLOCK(sc);
	if (val == MSG_ERROR)
		return (EIO);

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	/* write request */
	VC_LOCK(sc);
	err = bcm2835_cpufreq_set_clock_rate(sc, BCM2835_MBOX_CLOCK_ID_ARM,
	    val);
	VC_UNLOCK(sc);
	if (err == MSG_ERROR) {
		device_printf(sc->dev, "set clock arm_freq error\n");
		return (EIO);
	}
	DELAY(TRANSITION_LATENCY);

	return (0);
}

static int
sysctl_bcm2835_cpufreq_core_freq(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* get realtime value */
	VC_LOCK(sc);
	val = bcm2835_cpufreq_get_clock_rate(sc, BCM2835_MBOX_CLOCK_ID_CORE);
	VC_UNLOCK(sc);
	if (val == MSG_ERROR)
		return (EIO);

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	/* write request */
	VC_LOCK(sc);
	err = bcm2835_cpufreq_set_clock_rate(sc, BCM2835_MBOX_CLOCK_ID_CORE,
	    val);
	if (err == MSG_ERROR) {
		VC_UNLOCK(sc);
		device_printf(sc->dev, "set clock core_freq error\n");
		return (EIO);
	}
	VC_UNLOCK(sc);
	DELAY(TRANSITION_LATENCY);

	return (0);
}

static int
sysctl_bcm2835_cpufreq_sdram_freq(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* get realtime value */
	VC_LOCK(sc);
	val = bcm2835_cpufreq_get_clock_rate(sc, BCM2835_MBOX_CLOCK_ID_SDRAM);
	VC_UNLOCK(sc);
	if (val == MSG_ERROR)
		return (EIO);

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	/* write request */
	VC_LOCK(sc);
	err = bcm2835_cpufreq_set_clock_rate(sc, BCM2835_MBOX_CLOCK_ID_SDRAM,
	    val);
	VC_UNLOCK(sc);
	if (err == MSG_ERROR) {
		device_printf(sc->dev, "set clock sdram_freq error\n");
		return (EIO);
	}
	DELAY(TRANSITION_LATENCY);

	return (0);
}

static int
sysctl_bcm2835_cpufreq_turbo(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* get realtime value */
	VC_LOCK(sc);
	val = bcm2835_cpufreq_get_turbo(sc);
	VC_UNLOCK(sc);
	if (val == MSG_ERROR)
		return (EIO);

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	/* write request */
	if (val > 0)
		sc->turbo_mode = BCM2835_MBOX_TURBO_ON;
	else
		sc->turbo_mode = BCM2835_MBOX_TURBO_OFF;

	VC_LOCK(sc);
	err = bcm2835_cpufreq_set_turbo(sc, sc->turbo_mode);
	VC_UNLOCK(sc);
	if (err == MSG_ERROR) {
		device_printf(sc->dev, "set turbo error\n");
		return (EIO);
	}
	DELAY(TRANSITION_LATENCY);

	return (0);
}

static int
sysctl_bcm2835_cpufreq_voltage_core(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* get realtime value */
	VC_LOCK(sc);
	val = bcm2835_cpufreq_get_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_CORE);
	VC_UNLOCK(sc);
	if (val == MSG_ERROR)
		return (EIO);

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	/* write request */
	if (val > MAX_OVER_VOLTAGE || val < MIN_OVER_VOLTAGE)
		return (EINVAL);
	sc->voltage_core = val;

	VC_LOCK(sc);
	err = bcm2835_cpufreq_set_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_CORE,
	    sc->voltage_core);
	VC_UNLOCK(sc);
	if (err == MSG_ERROR) {
		device_printf(sc->dev, "set voltage core error\n");
		return (EIO);
	}
	DELAY(TRANSITION_LATENCY);

	return (0);
}

static int
sysctl_bcm2835_cpufreq_voltage_sdram_c(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* get realtime value */
	VC_LOCK(sc);
	val = bcm2835_cpufreq_get_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_SDRAM_C);
	VC_UNLOCK(sc);
	if (val == MSG_ERROR)
		return (EIO);

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	/* write request */
	if (val > MAX_OVER_VOLTAGE || val < MIN_OVER_VOLTAGE)
		return (EINVAL);
	sc->voltage_sdram_c = val;

	VC_LOCK(sc);
	err = bcm2835_cpufreq_set_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_SDRAM_C,
	   sc->voltage_sdram_c);
	VC_UNLOCK(sc);
	if (err == MSG_ERROR) {
		device_printf(sc->dev, "set voltage sdram_c error\n");
		return (EIO);
	}
	DELAY(TRANSITION_LATENCY);

	return (0);
}

static int
sysctl_bcm2835_cpufreq_voltage_sdram_i(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* get realtime value */
	VC_LOCK(sc);
	val = bcm2835_cpufreq_get_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_SDRAM_I);
	VC_UNLOCK(sc);
	if (val == MSG_ERROR)
		return (EIO);

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	/* write request */
	if (val > MAX_OVER_VOLTAGE || val < MIN_OVER_VOLTAGE)
		return (EINVAL);
	sc->voltage_sdram_i = val;

	VC_LOCK(sc);
	err = bcm2835_cpufreq_set_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_SDRAM_I,
	    sc->voltage_sdram_i);
	VC_UNLOCK(sc);
	if (err == MSG_ERROR) {
		device_printf(sc->dev, "set voltage sdram_i error\n");
		return (EIO);
	}
	DELAY(TRANSITION_LATENCY);

	return (0);
}

static int
sysctl_bcm2835_cpufreq_voltage_sdram_p(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* get realtime value */
	VC_LOCK(sc);
	val = bcm2835_cpufreq_get_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_SDRAM_P);
	VC_UNLOCK(sc);
	if (val == MSG_ERROR)
		return (EIO);

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	/* write request */
	if (val > MAX_OVER_VOLTAGE || val < MIN_OVER_VOLTAGE)
		return (EINVAL);
	sc->voltage_sdram_p = val;

	VC_LOCK(sc);
	err = bcm2835_cpufreq_set_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_SDRAM_P,
	    sc->voltage_sdram_p);
	VC_UNLOCK(sc);
	if (err == MSG_ERROR) {
		device_printf(sc->dev, "set voltage sdram_p error\n");
		return (EIO);
	}
	DELAY(TRANSITION_LATENCY);

	return (0);
}

static int
sysctl_bcm2835_cpufreq_voltage_sdram(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* multiple write only */
	if (!req->newptr)
		return (EINVAL);
	val = 0;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err)
		return (err);

	/* write request */
	if (val > MAX_OVER_VOLTAGE || val < MIN_OVER_VOLTAGE)
		return (EINVAL);
	sc->voltage_sdram = val;

	VC_LOCK(sc);
	err = bcm2835_cpufreq_set_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_SDRAM_C,
	    val);
	if (err == MSG_ERROR) {
		VC_UNLOCK(sc);
		device_printf(sc->dev, "set voltage sdram_c error\n");
		return (EIO);
	}
	err = bcm2835_cpufreq_set_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_SDRAM_I,
	    val);
	if (err == MSG_ERROR) {
		VC_UNLOCK(sc);
		device_printf(sc->dev, "set voltage sdram_i error\n");
		return (EIO);
	}
	err = bcm2835_cpufreq_set_voltage(sc, BCM2835_MBOX_VOLTAGE_ID_SDRAM_P,
	    val);
	if (err == MSG_ERROR) {
		VC_UNLOCK(sc);
		device_printf(sc->dev, "set voltage sdram_p error\n");
		return (EIO);
	}
	VC_UNLOCK(sc);
	DELAY(TRANSITION_LATENCY);

	return (0);
}

static int
sysctl_bcm2835_cpufreq_temperature(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* get realtime value */
	VC_LOCK(sc);
	val = bcm2835_cpufreq_get_temperature(sc);
	VC_UNLOCK(sc);
	if (val == MSG_ERROR)
		return (EIO);

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	/* write request */
	return (EINVAL);
}

static int
sysctl_bcm2835_devcpu_temperature(SYSCTL_HANDLER_ARGS)
{
	struct bcm2835_cpufreq_softc *sc = arg1;
	int val;
	int err;

	/* get realtime value */
	VC_LOCK(sc);
	val = bcm2835_cpufreq_get_temperature(sc);
	VC_UNLOCK(sc);
	if (val == MSG_ERROR)
		return (EIO);

	/* 1/1000 celsius (raw) to 1/10 kelvin */
	val = val / 100 + TZ_ZEROC;

	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	/* write request */
	return (EINVAL);
}


static void
bcm2835_cpufreq_init(void *arg)
{
	struct bcm2835_cpufreq_softc *sc = arg;
	struct sysctl_ctx_list *ctx;
	device_t cpu;
	int arm_freq, core_freq, sdram_freq;
	int arm_max_freq, arm_min_freq, core_max_freq, core_min_freq;
	int sdram_max_freq, sdram_min_freq;
	int voltage_core, voltage_sdram_c, voltage_sdram_i, voltage_sdram_p;
	int max_voltage_core, min_voltage_core;
	int max_voltage_sdram_c, min_voltage_sdram_c;
	int max_voltage_sdram_i, min_voltage_sdram_i;
	int max_voltage_sdram_p, min_voltage_sdram_p;
	int turbo, temperature;

	VC_LOCK(sc);

	/* current clock */
	arm_freq = bcm2835_cpufreq_get_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_ARM);
	core_freq = bcm2835_cpufreq_get_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_CORE);
	sdram_freq = bcm2835_cpufreq_get_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_SDRAM);

	/* max/min clock */
	arm_max_freq = bcm2835_cpufreq_get_max_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_ARM);
	arm_min_freq = bcm2835_cpufreq_get_min_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_ARM);
	core_max_freq = bcm2835_cpufreq_get_max_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_CORE);
	core_min_freq = bcm2835_cpufreq_get_min_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_CORE);
	sdram_max_freq = bcm2835_cpufreq_get_max_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_SDRAM);
	sdram_min_freq = bcm2835_cpufreq_get_min_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_SDRAM);

	/* turbo mode */
	turbo = bcm2835_cpufreq_get_turbo(sc);
	if (turbo > 0)
		sc->turbo_mode = BCM2835_MBOX_TURBO_ON;
	else
		sc->turbo_mode = BCM2835_MBOX_TURBO_OFF;

	/* voltage */
	voltage_core = bcm2835_cpufreq_get_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_CORE);
	voltage_sdram_c = bcm2835_cpufreq_get_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_SDRAM_C);
	voltage_sdram_i = bcm2835_cpufreq_get_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_SDRAM_I);
	voltage_sdram_p = bcm2835_cpufreq_get_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_SDRAM_P);

	/* current values (offset from 1.2V) */
	sc->voltage_core = voltage_core;
	sc->voltage_sdram = voltage_sdram_c;
	sc->voltage_sdram_c = voltage_sdram_c;
	sc->voltage_sdram_i = voltage_sdram_i;
	sc->voltage_sdram_p = voltage_sdram_p;

	/* max/min voltage */
	max_voltage_core = bcm2835_cpufreq_get_max_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_CORE);
	min_voltage_core = bcm2835_cpufreq_get_min_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_CORE);
	max_voltage_sdram_c = bcm2835_cpufreq_get_max_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_SDRAM_C);
	max_voltage_sdram_i = bcm2835_cpufreq_get_max_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_SDRAM_I);
	max_voltage_sdram_p = bcm2835_cpufreq_get_max_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_SDRAM_P);
	min_voltage_sdram_c = bcm2835_cpufreq_get_min_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_SDRAM_C);
	min_voltage_sdram_i = bcm2835_cpufreq_get_min_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_SDRAM_I);
	min_voltage_sdram_p = bcm2835_cpufreq_get_min_voltage(sc,
	    BCM2835_MBOX_VOLTAGE_ID_SDRAM_P);

	/* temperature */
	temperature = bcm2835_cpufreq_get_temperature(sc);

	/* show result */
	if (cpufreq_verbose || bootverbose) {
		device_printf(sc->dev, "Boot settings:\n");
		device_printf(sc->dev,
		    "current ARM %dMHz, Core %dMHz, SDRAM %dMHz, Turbo %s\n",
		    HZ2MHZ(arm_freq), HZ2MHZ(core_freq), HZ2MHZ(sdram_freq),
		    (sc->turbo_mode == BCM2835_MBOX_TURBO_ON) ? "ON" : "OFF");

		device_printf(sc->dev,
		    "max/min ARM %d/%dMHz, Core %d/%dMHz, SDRAM %d/%dMHz\n",
		    HZ2MHZ(arm_max_freq), HZ2MHZ(arm_min_freq),
		    HZ2MHZ(core_max_freq), HZ2MHZ(core_min_freq),
		    HZ2MHZ(sdram_max_freq), HZ2MHZ(sdram_min_freq));

		device_printf(sc->dev,
		    "current Core %dmV, SDRAM_C %dmV, SDRAM_I %dmV, "
		    "SDRAM_P %dmV\n",
		    OFFSET2MVOLT(voltage_core), OFFSET2MVOLT(voltage_sdram_c),
		    OFFSET2MVOLT(voltage_sdram_i), 
		    OFFSET2MVOLT(voltage_sdram_p));

		device_printf(sc->dev,
		    "max/min Core %d/%dmV, SDRAM_C %d/%dmV, SDRAM_I %d/%dmV, "
		    "SDRAM_P %d/%dmV\n",
		    OFFSET2MVOLT(max_voltage_core),
		    OFFSET2MVOLT(min_voltage_core),
		    OFFSET2MVOLT(max_voltage_sdram_c),
		    OFFSET2MVOLT(min_voltage_sdram_c),
		    OFFSET2MVOLT(max_voltage_sdram_i),
		    OFFSET2MVOLT(min_voltage_sdram_i),
		    OFFSET2MVOLT(max_voltage_sdram_p),
		    OFFSET2MVOLT(min_voltage_sdram_p));

		device_printf(sc->dev,
		    "Temperature %d.%dC\n", (temperature / 1000),
		    (temperature % 1000) / 100);
	} else { /* !cpufreq_verbose && !bootverbose */
		device_printf(sc->dev,
		    "ARM %dMHz, Core %dMHz, SDRAM %dMHz, Turbo %s\n",
		    HZ2MHZ(arm_freq), HZ2MHZ(core_freq), HZ2MHZ(sdram_freq),
		    (sc->turbo_mode == BCM2835_MBOX_TURBO_ON) ? "ON" : "OFF");
	}

	/* keep in softc (MHz/mV) */
	sc->arm_max_freq = HZ2MHZ(arm_max_freq);
	sc->arm_min_freq = HZ2MHZ(arm_min_freq);
	sc->core_max_freq = HZ2MHZ(core_max_freq);
	sc->core_min_freq = HZ2MHZ(core_min_freq);
	sc->sdram_max_freq = HZ2MHZ(sdram_max_freq);
	sc->sdram_min_freq = HZ2MHZ(sdram_min_freq);
	sc->max_voltage_core = OFFSET2MVOLT(max_voltage_core);
	sc->min_voltage_core = OFFSET2MVOLT(min_voltage_core);

	/* if turbo is on, set to max values */
	if (sc->turbo_mode == BCM2835_MBOX_TURBO_ON) {
		bcm2835_cpufreq_set_clock_rate(sc, BCM2835_MBOX_CLOCK_ID_ARM,
		    arm_max_freq);
		DELAY(TRANSITION_LATENCY);
		bcm2835_cpufreq_set_clock_rate(sc, BCM2835_MBOX_CLOCK_ID_CORE,
		    core_max_freq);
		DELAY(TRANSITION_LATENCY);
		bcm2835_cpufreq_set_clock_rate(sc,
		    BCM2835_MBOX_CLOCK_ID_SDRAM, sdram_max_freq);
		DELAY(TRANSITION_LATENCY);
	} else {
		bcm2835_cpufreq_set_clock_rate(sc, BCM2835_MBOX_CLOCK_ID_ARM,
		    arm_min_freq);
		DELAY(TRANSITION_LATENCY);
		bcm2835_cpufreq_set_clock_rate(sc, BCM2835_MBOX_CLOCK_ID_CORE,
		    core_min_freq);
		DELAY(TRANSITION_LATENCY);
		bcm2835_cpufreq_set_clock_rate(sc,
		    BCM2835_MBOX_CLOCK_ID_SDRAM, sdram_min_freq);
		DELAY(TRANSITION_LATENCY);
	}

	VC_UNLOCK(sc);

	/* add human readable temperature to dev.cpu node */
	cpu = device_get_parent(sc->dev);
	if (cpu != NULL) {
		ctx = device_get_sysctl_ctx(cpu);
		SYSCTL_ADD_PROC(ctx,
		    SYSCTL_CHILDREN(device_get_sysctl_tree(cpu)), OID_AUTO,
		    "temperature", CTLTYPE_INT | CTLFLAG_RD, sc, 0,
		    sysctl_bcm2835_devcpu_temperature, "IK",
		    "Current SoC temperature");
	}

	/* release this hook (continue boot) */
	config_intrhook_disestablish(&sc->init_hook);
}

static void
bcm2835_cpufreq_identify(driver_t *driver, device_t parent)
{
	const struct ofw_compat_data *compat;
	phandle_t root;

	root = OF_finddevice("/");
	for (compat = compat_data; compat->ocd_str != NULL; compat++)
		if (ofw_bus_node_is_compatible(root, compat->ocd_str))
			break;

	if (compat->ocd_data == 0)
		return;

	DPRINTF("driver=%p, parent=%p\n", driver, parent);
	if (device_find_child(parent, "bcm2835_cpufreq", -1) != NULL)
		return;
	if (BUS_ADD_CHILD(parent, 0, "bcm2835_cpufreq", -1) == NULL)
		device_printf(parent, "add child failed\n");
}

static int
bcm2835_cpufreq_probe(device_t dev)
{

	if (device_get_unit(dev) != 0)
		return (ENXIO);
	device_set_desc(dev, "CPU Frequency Control");

	return (0);
}

static int
bcm2835_cpufreq_attach(device_t dev)
{
	struct bcm2835_cpufreq_softc *sc;
	struct sysctl_oid *oid;

	/* set self dev */
	sc = device_get_softc(dev);
	sc->dev = dev;

	/* initial values */
	sc->arm_max_freq = -1;
	sc->arm_min_freq = -1;
	sc->core_max_freq = -1;
	sc->core_min_freq = -1;
	sc->sdram_max_freq = -1;
	sc->sdram_min_freq = -1;
	sc->max_voltage_core = 0;
	sc->min_voltage_core = 0;

	/* setup sysctl at first device */
	if (device_get_unit(dev) == 0) {
		sysctl_ctx_init(&bcm2835_sysctl_ctx);
		/* create node for hw.cpufreq */
		oid = SYSCTL_ADD_NODE(&bcm2835_sysctl_ctx,
		    SYSCTL_STATIC_CHILDREN(_hw), OID_AUTO, "cpufreq",
		    CTLFLAG_RD, NULL, "");

		/* Frequency (Hz) */
		SYSCTL_ADD_PROC(&bcm2835_sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "arm_freq", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		    sysctl_bcm2835_cpufreq_arm_freq, "IU",
		    "ARM frequency (Hz)");
		SYSCTL_ADD_PROC(&bcm2835_sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "core_freq", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		    sysctl_bcm2835_cpufreq_core_freq, "IU",
		    "Core frequency (Hz)");
		SYSCTL_ADD_PROC(&bcm2835_sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "sdram_freq", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		    sysctl_bcm2835_cpufreq_sdram_freq, "IU",
		    "SDRAM frequency (Hz)");

		/* Turbo state */
		SYSCTL_ADD_PROC(&bcm2835_sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "turbo", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		    sysctl_bcm2835_cpufreq_turbo, "IU",
		    "Disables dynamic clocking");

		/* Voltage (offset from 1.2V in units of 0.025V) */
		SYSCTL_ADD_PROC(&bcm2835_sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "voltage_core", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		    sysctl_bcm2835_cpufreq_voltage_core, "I",
		    "ARM/GPU core voltage"
		    "(offset from 1.2V in units of 0.025V)");
		SYSCTL_ADD_PROC(&bcm2835_sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "voltage_sdram", CTLTYPE_INT | CTLFLAG_WR, sc,
		    0, sysctl_bcm2835_cpufreq_voltage_sdram, "I",
		    "SDRAM voltage (offset from 1.2V in units of 0.025V)");

		/* Voltage individual SDRAM */
		SYSCTL_ADD_PROC(&bcm2835_sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "voltage_sdram_c", CTLTYPE_INT | CTLFLAG_RW, sc,
		    0, sysctl_bcm2835_cpufreq_voltage_sdram_c, "I",
		    "SDRAM controller voltage"
		    "(offset from 1.2V in units of 0.025V)");
		SYSCTL_ADD_PROC(&bcm2835_sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "voltage_sdram_i", CTLTYPE_INT | CTLFLAG_RW, sc,
		    0, sysctl_bcm2835_cpufreq_voltage_sdram_i, "I",
		    "SDRAM I/O voltage (offset from 1.2V in units of 0.025V)");
		SYSCTL_ADD_PROC(&bcm2835_sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "voltage_sdram_p", CTLTYPE_INT | CTLFLAG_RW, sc,
		    0, sysctl_bcm2835_cpufreq_voltage_sdram_p, "I",
		    "SDRAM phy voltage (offset from 1.2V in units of 0.025V)");

		/* Temperature */
		SYSCTL_ADD_PROC(&bcm2835_sysctl_ctx, SYSCTL_CHILDREN(oid),
		    OID_AUTO, "temperature", CTLTYPE_INT | CTLFLAG_RD, sc, 0,
		    sysctl_bcm2835_cpufreq_temperature, "I",
		    "SoC temperature (thousandths of a degree C)");
	}

	/* ARM->VC lock */
	sema_init(&vc_sema, 1, "vcsema");

	/* register callback for using mbox when interrupts are enabled */
	sc->init_hook.ich_func = bcm2835_cpufreq_init;
	sc->init_hook.ich_arg = sc;

	if (config_intrhook_establish(&sc->init_hook) != 0) {
		device_printf(dev, "config_intrhook_establish failed\n");
		return (ENOMEM);
	}

	/* this device is controlled by cpufreq(4) */
	cpufreq_register(dev);

	return (0);
}

static int
bcm2835_cpufreq_detach(device_t dev)
{

	sema_destroy(&vc_sema);

	return (cpufreq_unregister(dev));
}

static int
bcm2835_cpufreq_set(device_t dev, const struct cf_setting *cf)
{
	struct bcm2835_cpufreq_softc *sc;
	uint32_t rate_hz, rem;
	int resp_freq, arm_freq, min_freq, core_freq;
#ifdef DEBUG
	int cur_freq;
#endif

	if (cf == NULL || cf->freq < 0)
		return (EINVAL);

	sc = device_get_softc(dev);

	/* setting clock (Hz) */
	rate_hz = (uint32_t)MHZ2HZ(cf->freq);
	rem = rate_hz % HZSTEP;
	rate_hz -= rem;
	if (rate_hz == 0)
		return (EINVAL);

	/* adjust min freq */
	min_freq = sc->arm_min_freq;
	if (sc->turbo_mode != BCM2835_MBOX_TURBO_ON)
		if (min_freq > cpufreq_lowest_freq)
			min_freq = cpufreq_lowest_freq;

	if (rate_hz < MHZ2HZ(min_freq) || rate_hz > MHZ2HZ(sc->arm_max_freq))
		return (EINVAL);

	/* set new value and verify it */
	VC_LOCK(sc);
#ifdef DEBUG
	cur_freq = bcm2835_cpufreq_get_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_ARM);
#endif
	resp_freq = bcm2835_cpufreq_set_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_ARM, rate_hz);
	DELAY(TRANSITION_LATENCY);
	arm_freq = bcm2835_cpufreq_get_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_ARM);

	/*
	 * if non-turbo and lower than or equal min_freq,
	 * clock down core and sdram to default first.
	 */
	if (sc->turbo_mode != BCM2835_MBOX_TURBO_ON) {
		core_freq = bcm2835_cpufreq_get_clock_rate(sc,
		    BCM2835_MBOX_CLOCK_ID_CORE);
		if (rate_hz > MHZ2HZ(sc->arm_min_freq)) {
			bcm2835_cpufreq_set_clock_rate(sc,
			    BCM2835_MBOX_CLOCK_ID_CORE,
			    MHZ2HZ(sc->core_max_freq));
			DELAY(TRANSITION_LATENCY);
			bcm2835_cpufreq_set_clock_rate(sc,
			    BCM2835_MBOX_CLOCK_ID_SDRAM,
			    MHZ2HZ(sc->sdram_max_freq));
			DELAY(TRANSITION_LATENCY);
		} else {
			if (sc->core_min_freq < DEFAULT_CORE_FREQUENCY &&
			    core_freq > DEFAULT_CORE_FREQUENCY) {
				/* first, down to 250, then down to min */
				DELAY(TRANSITION_LATENCY);
				bcm2835_cpufreq_set_clock_rate(sc,
				    BCM2835_MBOX_CLOCK_ID_CORE,
				    MHZ2HZ(DEFAULT_CORE_FREQUENCY));
				DELAY(TRANSITION_LATENCY);
				/* reset core voltage */
				bcm2835_cpufreq_set_voltage(sc,
				    BCM2835_MBOX_VOLTAGE_ID_CORE, 0);
				DELAY(TRANSITION_LATENCY);
			}
			bcm2835_cpufreq_set_clock_rate(sc,
			    BCM2835_MBOX_CLOCK_ID_CORE,
			    MHZ2HZ(sc->core_min_freq));
			DELAY(TRANSITION_LATENCY);
			bcm2835_cpufreq_set_clock_rate(sc,
			    BCM2835_MBOX_CLOCK_ID_SDRAM,
			    MHZ2HZ(sc->sdram_min_freq));
			DELAY(TRANSITION_LATENCY);
		}
	}

	VC_UNLOCK(sc);

	if (resp_freq < 0 || arm_freq < 0 || resp_freq != arm_freq) {
		device_printf(dev, "wrong freq\n");
		return (EIO);
	}
	DPRINTF("cpufreq: %d -> %d\n", cur_freq, arm_freq);

	return (0);
}

static int
bcm2835_cpufreq_get(device_t dev, struct cf_setting *cf)
{
	struct bcm2835_cpufreq_softc *sc;
	int arm_freq;

	if (cf == NULL)
		return (EINVAL);

	sc = device_get_softc(dev);
	memset(cf, CPUFREQ_VAL_UNKNOWN, sizeof(*cf));
	cf->dev = NULL;

	/* get cuurent value */
	VC_LOCK(sc);
	arm_freq = bcm2835_cpufreq_get_clock_rate(sc,
	    BCM2835_MBOX_CLOCK_ID_ARM);
	VC_UNLOCK(sc);
	if (arm_freq < 0) {
		device_printf(dev, "can't get clock\n");
		return (EINVAL);
	}

	/* CPU clock in MHz or 100ths of a percent. */
	cf->freq = HZ2MHZ(arm_freq);
	/* Voltage in mV. */
	cf->volts = CPUFREQ_VAL_UNKNOWN;
	/* Power consumed in mW. */
	cf->power = CPUFREQ_VAL_UNKNOWN;
	/* Transition latency in us. */
	cf->lat = TRANSITION_LATENCY;
	/* Driver providing this setting. */
	cf->dev = dev;

	return (0);
}

static int
bcm2835_cpufreq_make_freq_list(device_t dev, struct cf_setting *sets,
    int *count)
{
	struct bcm2835_cpufreq_softc *sc;
	int freq, min_freq, volts, rem;
	int idx;

	sc = device_get_softc(dev);
	freq = sc->arm_max_freq;
	min_freq = sc->arm_min_freq;

	/* adjust head freq to STEP */
	rem = freq % MHZSTEP;
	freq -= rem;
	if (freq < min_freq)
		freq = min_freq;

	/* if non-turbo, add extra low freq */
	if (sc->turbo_mode != BCM2835_MBOX_TURBO_ON)
		if (min_freq > cpufreq_lowest_freq)
			min_freq = cpufreq_lowest_freq;

#ifdef SOC_BCM2835
	/* from freq to min_freq */
	for (idx = 0; idx < *count && freq >= min_freq; idx++) {
		if (freq > sc->arm_min_freq)
			volts = sc->max_voltage_core;
		else
			volts = sc->min_voltage_core;
		sets[idx].freq = freq;
		sets[idx].volts = volts;
		sets[idx].lat = TRANSITION_LATENCY;
		sets[idx].dev = dev;
		freq -= MHZSTEP;
	}
#else
	/* XXX RPi2 have only 900/600MHz */
	idx = 0;
	volts = sc->min_voltage_core;
	sets[idx].freq = freq;
	sets[idx].volts = volts;
	sets[idx].lat = TRANSITION_LATENCY;
	sets[idx].dev = dev;
	idx++;
	if (freq != min_freq) {
		sets[idx].freq = min_freq;
		sets[idx].volts = volts;
		sets[idx].lat = TRANSITION_LATENCY;
		sets[idx].dev = dev;
		idx++;
	}
#endif
	*count = idx;

	return (0);
}

static int
bcm2835_cpufreq_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct bcm2835_cpufreq_softc *sc;

	if (sets == NULL || count == NULL)
		return (EINVAL);

	sc = device_get_softc(dev);
	if (sc->arm_min_freq < 0 || sc->arm_max_freq < 0) {
		printf("device is not configured\n");
		return (EINVAL);
	}

	/* fill data with unknown value */
	memset(sets, CPUFREQ_VAL_UNKNOWN, sizeof(*sets) * (*count));
	/* create new array up to count */
	bcm2835_cpufreq_make_freq_list(dev, sets, count);

	return (0);
}

static int
bcm2835_cpufreq_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);
	*type = CPUFREQ_TYPE_ABSOLUTE;

	return (0);
}

static device_method_t bcm2835_cpufreq_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	bcm2835_cpufreq_identify),
	DEVMETHOD(device_probe,		bcm2835_cpufreq_probe),
	DEVMETHOD(device_attach,	bcm2835_cpufreq_attach),
	DEVMETHOD(device_detach,	bcm2835_cpufreq_detach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	bcm2835_cpufreq_set),
	DEVMETHOD(cpufreq_drv_get,	bcm2835_cpufreq_get),
	DEVMETHOD(cpufreq_drv_settings,	bcm2835_cpufreq_settings),
	DEVMETHOD(cpufreq_drv_type,	bcm2835_cpufreq_type),

	DEVMETHOD_END
};

static devclass_t bcm2835_cpufreq_devclass;
static driver_t bcm2835_cpufreq_driver = {
	"bcm2835_cpufreq",
	bcm2835_cpufreq_methods,
	sizeof(struct bcm2835_cpufreq_softc),
};

DRIVER_MODULE(bcm2835_cpufreq, cpu, bcm2835_cpufreq_driver,
    bcm2835_cpufreq_devclass, 0, 0);
