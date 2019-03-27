/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2011 Nathan Whitehorn
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
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <sys/types.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>
#include <sys/queue.h>

#include "powermac_thermal.h"

/* A 10 second timer for spinning down fans. */
#define FAN_HYSTERESIS_TIMER	10

static void fan_management_proc(void);
static void pmac_therm_manage_fans(void);

static struct proc *pmac_them_proc;
static int enable_pmac_thermal = 1;

static struct kproc_desc pmac_therm_kp = {
	"pmac_thermal",
	fan_management_proc,
	&pmac_them_proc
};

SYSINIT(pmac_therm_setup, SI_SUB_KTHREAD_IDLE, SI_ORDER_ANY, kproc_start,
    &pmac_therm_kp);
SYSCTL_INT(_machdep, OID_AUTO, manage_fans, CTLFLAG_RW | CTLFLAG_TUN,
    &enable_pmac_thermal, 1, "Enable automatic fan management");
static MALLOC_DEFINE(M_PMACTHERM, "pmactherm", "Powermac Thermal Management");

struct pmac_fan_le {
	struct pmac_fan			*fan;
	int				last_val;
	int				timer;
	SLIST_ENTRY(pmac_fan_le)	entries;
};
struct pmac_sens_le {
	struct pmac_therm		*sensor;
	int				last_val;
#define MAX_CRITICAL_COUNT 6
	int				critical_count;
	SLIST_ENTRY(pmac_sens_le)	entries;
};
static SLIST_HEAD(pmac_fans, pmac_fan_le) fans = SLIST_HEAD_INITIALIZER(fans);
static SLIST_HEAD(pmac_sensors, pmac_sens_le) sensors =
    SLIST_HEAD_INITIALIZER(sensors);

static void
fan_management_proc(void)
{
	/* Nothing to manage? */
	if (SLIST_EMPTY(&fans))
		kproc_exit(0);
	
	while (1) {
		pmac_therm_manage_fans();
		pause("pmac_therm", hz);
	}
}

static void
pmac_therm_manage_fans(void)
{
	struct pmac_sens_le *sensor;
	struct pmac_fan_le *fan;
	int average_excess, max_excess_zone, frac_excess;
	int fan_speed;
	int nsens, nsens_zone;
	int temp;

	if (!enable_pmac_thermal)
		return;

	/* Read all the sensors */
	SLIST_FOREACH(sensor, &sensors, entries) {
		temp = sensor->sensor->read(sensor->sensor);
		if (temp > 0) /* Use the previous temp in case of error */
			sensor->last_val = temp;

		if (sensor->last_val > sensor->sensor->max_temp) {
			sensor->critical_count++;
			printf("WARNING: Current temperature (%s: %d.%d C) "
			    "exceeds critical temperature (%d.%d C); "
			    "count=%d\n",
			    sensor->sensor->name,
			    (sensor->last_val - ZERO_C_TO_K) / 10,
			    (sensor->last_val - ZERO_C_TO_K) % 10,
			    (sensor->sensor->max_temp - ZERO_C_TO_K) / 10,
			    (sensor->sensor->max_temp - ZERO_C_TO_K) % 10,
			    sensor->critical_count);
			if (sensor->critical_count >= MAX_CRITICAL_COUNT) {
				printf("WARNING: %s temperature exceeded "
				    "critical temperature %d times in a row; "
				    "shutting down!\n",
				    sensor->sensor->name,
				    sensor->critical_count);
				shutdown_nice(RB_POWEROFF);
			}
		} else {
			if (sensor->critical_count > 0)
				sensor->critical_count--;
		}
	}

	/* Set all the fans */
	SLIST_FOREACH(fan, &fans, entries) {
		nsens = nsens_zone = 0;
		average_excess = max_excess_zone = 0;
		SLIST_FOREACH(sensor, &sensors, entries) {
			temp = imin(sensor->last_val,
			    sensor->sensor->max_temp);
			frac_excess = (temp -
			    sensor->sensor->target_temp)*100 /
			    (sensor->sensor->max_temp - temp + 1);
			if (frac_excess < 0)
				frac_excess = 0;
			if (sensor->sensor->zone == fan->fan->zone) {
				max_excess_zone = imax(max_excess_zone,
				    frac_excess);
				nsens_zone++;
			}
			average_excess += frac_excess;
			nsens++;
		}
		average_excess /= nsens;

		/* If there are no sensors in this zone, use the average */
		if (nsens_zone == 0)
			max_excess_zone = average_excess;
		/* No sensors at all? Use default */
		if (nsens == 0) {
			fan->fan->set(fan->fan, fan->fan->default_rpm);
			continue;
		}

		/*
		 * Scale the fan linearly in the max temperature in its
		 * thermal zone.
		 */
		max_excess_zone = imin(max_excess_zone, 100);
		fan_speed = max_excess_zone * 
		    (fan->fan->max_rpm - fan->fan->min_rpm)/100 +
		    fan->fan->min_rpm;
		if (fan_speed >= fan->last_val) {
		    fan->timer = FAN_HYSTERESIS_TIMER;
		    fan->last_val = fan_speed;
		} else {
		    fan->timer--;
		    if (fan->timer == 0) {
		    	fan->last_val = fan_speed;
		    	fan->timer = FAN_HYSTERESIS_TIMER;
		    }
		}
		fan->fan->set(fan->fan, fan->last_val);
	}
}

void
pmac_thermal_fan_register(struct pmac_fan *fan)
{
	struct pmac_fan_le *list_entry;

	list_entry = malloc(sizeof(struct pmac_fan_le), M_PMACTHERM,
	    M_ZERO | M_WAITOK);
	list_entry->fan = fan;

	SLIST_INSERT_HEAD(&fans, list_entry, entries);
}

void
pmac_thermal_sensor_register(struct pmac_therm *sensor)
{
	struct pmac_sens_le *list_entry;

	list_entry = malloc(sizeof(struct pmac_sens_le), M_PMACTHERM,
	    M_ZERO | M_WAITOK);
	list_entry->sensor = sensor;
	list_entry->last_val = 0;
	list_entry->critical_count = 0;

	SLIST_INSERT_HEAD(&sensors, list_entry, entries);
}

