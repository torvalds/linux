/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001 Tamotsu Hattori.
 * Copyright (c) 2001 Mitsuru IWASAKI.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/power.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

/*
 * Transmeta Crusoe LongRun Support by Tamotsu Hattori.
 */

#define MSR_TMx86_LONGRUN		0x80868010
#define MSR_TMx86_LONGRUN_FLAGS		0x80868011

#define LONGRUN_MODE_MASK(x)		((x) & 0x000000007f)
#define LONGRUN_MODE_RESERVED(x)	((x) & 0xffffff80)
#define LONGRUN_MODE_WRITE(x, y)	(LONGRUN_MODE_RESERVED(x) | LONGRUN_MODE_MASK(y))

#define LONGRUN_MODE_MINFREQUENCY	0x00
#define LONGRUN_MODE_ECONOMY		0x01
#define LONGRUN_MODE_PERFORMANCE	0x02
#define LONGRUN_MODE_MAXFREQUENCY	0x03
#define LONGRUN_MODE_UNKNOWN		0x04
#define LONGRUN_MODE_MAX		0x04

union msrinfo {
	u_int64_t	msr;
	u_int32_t	regs[2];
};

static u_int32_t longrun_modes[LONGRUN_MODE_MAX][3] = {
	/*  MSR low, MSR high, flags bit0 */
	{	  0,	  0,		0},	/* LONGRUN_MODE_MINFREQUENCY */
	{	  0,	100,		0},	/* LONGRUN_MODE_ECONOMY */
	{	  0,	100,		1},	/* LONGRUN_MODE_PERFORMANCE */
	{	100,	100,		1},	/* LONGRUN_MODE_MAXFREQUENCY */
};

static u_int
tmx86_get_longrun_mode(void)
{
	register_t	saveintr;
	union msrinfo	msrinfo;
	u_int		low, high, flags, mode;

	saveintr = intr_disable();

	msrinfo.msr = rdmsr(MSR_TMx86_LONGRUN);
	low = LONGRUN_MODE_MASK(msrinfo.regs[0]);
	high = LONGRUN_MODE_MASK(msrinfo.regs[1]);
	flags = rdmsr(MSR_TMx86_LONGRUN_FLAGS) & 0x01;

	for (mode = 0; mode < LONGRUN_MODE_MAX; mode++) {
		if (low   == longrun_modes[mode][0] &&
		    high  == longrun_modes[mode][1] &&
		    flags == longrun_modes[mode][2]) {
			goto out;
		}
	}
	mode = LONGRUN_MODE_UNKNOWN;
out:
	intr_restore(saveintr);
	return (mode);
}

static u_int
tmx86_get_longrun_status(u_int * frequency, u_int * voltage, u_int * percentage)
{
	register_t	saveintr;
	u_int		regs[4];

	saveintr = intr_disable();

	do_cpuid(0x80860007, regs);
	*frequency = regs[0];
	*voltage = regs[1];
	*percentage = regs[2];

	intr_restore(saveintr);
	return (1);
}

static u_int
tmx86_set_longrun_mode(u_int mode)
{
	register_t	saveintr;
	union msrinfo	msrinfo;

	if (mode >= LONGRUN_MODE_UNKNOWN) {
		return (0);
	}

	saveintr = intr_disable();

	/* Write LongRun mode values to Model Specific Register. */
	msrinfo.msr = rdmsr(MSR_TMx86_LONGRUN);
	msrinfo.regs[0] = LONGRUN_MODE_WRITE(msrinfo.regs[0],
					     longrun_modes[mode][0]);
	msrinfo.regs[1] = LONGRUN_MODE_WRITE(msrinfo.regs[1],
					     longrun_modes[mode][1]);
	wrmsr(MSR_TMx86_LONGRUN, msrinfo.msr);

	/* Write LongRun mode flags to Model Specific Register. */
	msrinfo.msr = rdmsr(MSR_TMx86_LONGRUN_FLAGS);
	msrinfo.regs[0] = (msrinfo.regs[0] & ~0x01) | longrun_modes[mode][2];
	wrmsr(MSR_TMx86_LONGRUN_FLAGS, msrinfo.msr);

	intr_restore(saveintr);
	return (1);
}

static u_int			 crusoe_longrun;
static u_int			 crusoe_frequency;
static u_int	 		 crusoe_voltage;
static u_int	 		 crusoe_percentage;
static u_int	 		 crusoe_performance_longrun = LONGRUN_MODE_PERFORMANCE;
static u_int	 		 crusoe_economy_longrun = LONGRUN_MODE_ECONOMY;
static struct sysctl_ctx_list	 crusoe_sysctl_ctx;
static struct sysctl_oid	*crusoe_sysctl_tree;

static void
tmx86_longrun_power_profile(void *arg)
{
	int	state;
	u_int	new;

	state = power_profile_get_state();
	if (state != POWER_PROFILE_PERFORMANCE &&
	    state != POWER_PROFILE_ECONOMY) {
		return;
	}

	switch (state) {
	case POWER_PROFILE_PERFORMANCE:
		new =crusoe_performance_longrun;
		break;
	case POWER_PROFILE_ECONOMY:
		new = crusoe_economy_longrun;
		break;
	default:
		new = tmx86_get_longrun_mode();
		break;
	}

	if (tmx86_get_longrun_mode() != new) {
		tmx86_set_longrun_mode(new);
	}
}

static int
tmx86_longrun_sysctl(SYSCTL_HANDLER_ARGS)
{
	u_int	mode;
	int	error;

	crusoe_longrun = tmx86_get_longrun_mode();
	mode = crusoe_longrun;
	error = sysctl_handle_int(oidp, &mode, 0, req);
	if (error || !req->newptr) {
		return (error);
	}
	if (mode >= LONGRUN_MODE_UNKNOWN) {
		error = EINVAL;
		return (error);
	}
	if (crusoe_longrun != mode) {
		crusoe_longrun = mode;
		tmx86_set_longrun_mode(crusoe_longrun);
	}

	return (error);
}

static int
tmx86_status_sysctl(SYSCTL_HANDLER_ARGS)
{
	u_int	val;
	int	error;

	tmx86_get_longrun_status(&crusoe_frequency,
				 &crusoe_voltage, &crusoe_percentage);
	val = *(u_int *)oidp->oid_arg1;
	error = sysctl_handle_int(oidp, &val, 0, req);
	return (error);
}

static int
tmx86_longrun_profile_sysctl(SYSCTL_HANDLER_ARGS)
{
	u_int32_t *argp;
	u_int32_t arg;
	int	error;

	argp = (u_int32_t *)oidp->oid_arg1;
	arg = *argp;
	error = sysctl_handle_int(oidp, &arg, 0, req);

	/* error or no new value */
	if ((error != 0) || (req->newptr == NULL))
		return (error);

	/* range check */
	if (arg >= LONGRUN_MODE_UNKNOWN)
		return (EINVAL);

	/* set new value and possibly switch */
	*argp = arg;

	tmx86_longrun_power_profile(NULL);

	return (0);

}

static void
setup_tmx86_longrun(void *dummy __unused)
{

	if (cpu_vendor_id != CPU_VENDOR_TRANSMETA)
		return;

	crusoe_longrun = tmx86_get_longrun_mode();
	tmx86_get_longrun_status(&crusoe_frequency,
				 &crusoe_voltage, &crusoe_percentage);
	printf("Crusoe LongRun support enabled, current mode: %d "
	       "<%dMHz %dmV %d%%>\n", crusoe_longrun, crusoe_frequency,
	       crusoe_voltage, crusoe_percentage);

	sysctl_ctx_init(&crusoe_sysctl_ctx);
	crusoe_sysctl_tree = SYSCTL_ADD_NODE(&crusoe_sysctl_ctx,
				SYSCTL_STATIC_CHILDREN(_hw), OID_AUTO,
				"crusoe", CTLFLAG_RD, 0,
				"Transmeta Crusoe LongRun support");
	SYSCTL_ADD_PROC(&crusoe_sysctl_ctx, SYSCTL_CHILDREN(crusoe_sysctl_tree),
		OID_AUTO, "longrun", CTLTYPE_INT | CTLFLAG_RW,
		&crusoe_longrun, 0, tmx86_longrun_sysctl, "I",
		"LongRun mode [0-3]");
	SYSCTL_ADD_PROC(&crusoe_sysctl_ctx, SYSCTL_CHILDREN(crusoe_sysctl_tree),
		OID_AUTO, "frequency", CTLTYPE_INT | CTLFLAG_RD,
		&crusoe_frequency, 0, tmx86_status_sysctl, "I",
		"Current frequency (MHz)");
	SYSCTL_ADD_PROC(&crusoe_sysctl_ctx, SYSCTL_CHILDREN(crusoe_sysctl_tree),
		OID_AUTO, "voltage", CTLTYPE_INT | CTLFLAG_RD,
		&crusoe_voltage, 0, tmx86_status_sysctl, "I",
		"Current voltage (mV)");
	SYSCTL_ADD_PROC(&crusoe_sysctl_ctx, SYSCTL_CHILDREN(crusoe_sysctl_tree),
		OID_AUTO, "percentage", CTLTYPE_INT | CTLFLAG_RD,
		&crusoe_percentage, 0, tmx86_status_sysctl, "I",
		"Processing performance (%)");
	SYSCTL_ADD_PROC(&crusoe_sysctl_ctx, SYSCTL_CHILDREN(crusoe_sysctl_tree),
		OID_AUTO, "performance_longrun", CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_RW,
		&crusoe_performance_longrun, 0, tmx86_longrun_profile_sysctl, "I", "");
	SYSCTL_ADD_PROC(&crusoe_sysctl_ctx, SYSCTL_CHILDREN(crusoe_sysctl_tree),
		OID_AUTO, "economy_longrun", CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_RW,
		&crusoe_economy_longrun, 0, tmx86_longrun_profile_sysctl, "I", "");

	/* register performance profile change handler */
	EVENTHANDLER_REGISTER(power_profile_change, tmx86_longrun_power_profile, NULL, 0);
}
SYSINIT(setup_tmx86_longrun, SI_SUB_CPU, SI_ORDER_ANY, setup_tmx86_longrun,
    NULL);
