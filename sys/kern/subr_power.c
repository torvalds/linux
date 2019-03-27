/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Mitsuru IWASAKI
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <sys/power.h>
#include <sys/taskqueue.h>

static u_int		 power_pm_type	= POWER_PM_TYPE_NONE;
static power_pm_fn_t	 power_pm_fn	= NULL;
static void		*power_pm_arg	= NULL;
static struct task	 power_pm_task;

static void
power_pm_deferred_fn(void *arg, int pending)
{
	int state = (intptr_t)arg;

	power_pm_fn(POWER_CMD_SUSPEND, power_pm_arg, state);
}

int
power_pm_register(u_int pm_type, power_pm_fn_t pm_fn, void *pm_arg)
{
	int	error;

	if (power_pm_type == POWER_PM_TYPE_NONE ||
	    power_pm_type == pm_type) {
		power_pm_type	= pm_type;
		power_pm_fn	= pm_fn;
		power_pm_arg	= pm_arg;
		error = 0;
		TASK_INIT(&power_pm_task, 0, power_pm_deferred_fn, NULL);
	} else {
		error = ENXIO;
	}

	return (error);
}

u_int
power_pm_get_type(void)
{

	return (power_pm_type);
}

void
power_pm_suspend(int state)
{
	if (power_pm_fn == NULL)
		return;

	if (state != POWER_SLEEP_STATE_STANDBY &&
	    state != POWER_SLEEP_STATE_SUSPEND &&
	    state != POWER_SLEEP_STATE_HIBERNATE)
		return;
	power_pm_task.ta_context = (void *)(intptr_t)state;
	taskqueue_enqueue(taskqueue_thread, &power_pm_task);
}

/*
 * Power profile.
 */

static int	power_profile_state = POWER_PROFILE_PERFORMANCE;

int
power_profile_get_state(void)
{
	return (power_profile_state);
}

void
power_profile_set_state(int state) 
{
	int		changed;
    
	if (state != power_profile_state) {
		power_profile_state = state;
		changed = 1;
		if (bootverbose) {
			printf("system power profile changed to '%s'\n",
				(state == POWER_PROFILE_PERFORMANCE) ?
				"performance" : "economy");
		}
	} else {
		changed = 0;
	}

	if (changed)
		EVENTHANDLER_INVOKE(power_profile_change, 0);
}

