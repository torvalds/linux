/*	$OpenBSD: thermal.c,v 1.6 2019/10/08 13:21:38 cheloha Exp $ */

/*-
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#include <sys/malloc.h>
#include <sys/reboot.h>
#include <sys/sensors.h>
#include <sys/kthread.h>

#include <macppc/dev/thermal.h>

/* A 10 second timer for spinning down fans. */
#define FAN_HYSTERESIS_TIMER	10

void thermal_thread_init(void);
void thermal_thread_create(void *);
void thermal_thread_loop(void *);
void thermal_manage_fans(void);

int thermal_enable = 0;

struct thermal_fan_le {
	struct thermal_fan		*fan;
	int				last_val;
	int				timer;
	SLIST_ENTRY(thermal_fan_le)	entries;
};
struct thermal_sens_le {
	struct thermal_temp		*sensor;
	int				last_val;
#define MAX_CRITICAL_COUNT 6
	int				critical_count;
	SLIST_ENTRY(thermal_sens_le)	entries;
};

SLIST_HEAD(thermal_fans, thermal_fan_le) fans =
    SLIST_HEAD_INITIALIZER(fans);
SLIST_HEAD(thermal_sensors, thermal_sens_le) sensors =
    SLIST_HEAD_INITIALIZER(sensors);

void
thermal_thread_init(void)
{
	if (thermal_enable)
		return;		/* we're already running */
	thermal_enable = 1;

	kthread_create_deferred(thermal_thread_create, &thermal_enable);
}

void
thermal_thread_create(void *arg)
{
	if (kthread_create(thermal_thread_loop, &thermal_enable, NULL,
	    "thermal")) {
		printf("thermal kernel thread can't be created!\n");
		thermal_enable = 0;
	}
}

void
thermal_thread_loop(void *arg)
{
	while (thermal_enable) {
		thermal_manage_fans();
		tsleep_nsec(&thermal_enable, 0, "thermal", SEC_TO_NSEC(1));
	}
	kthread_exit(0);
}

void
thermal_manage_fans(void)
{
	struct thermal_sens_le *sensor;
	struct thermal_fan_le *fan;
	int64_t average_excess, max_excess_zone, frac_excess;
	int fan_speed;
	int nsens, nsens_zone;
	int temp;

	/* Read all the sensors */
	SLIST_FOREACH(sensor, &sensors, entries) {
		temp = sensor->sensor->read(sensor->sensor);
		if (temp > 0) /* Use the previous temp in case of error */
			sensor->last_val = temp;

		if (sensor->last_val > sensor->sensor->max_temp) {
			sensor->critical_count++;
			printf("WARNING: Current temperature (%s: %d.%d C) "
			    "exceeds critical temperature (%lld.%lld C); "
			    "count=%d\n",
			    sensor->sensor->name,
			    (sensor->last_val - ZERO_C_TO_MUK)/1000000,
			    (sensor->last_val - ZERO_C_TO_MUK)%1000000,
			    (sensor->sensor->max_temp - ZERO_C_TO_MUK)/1000000,
			    (sensor->sensor->max_temp - ZERO_C_TO_MUK)%1000000,
			    sensor->critical_count);
			if (sensor->critical_count >= MAX_CRITICAL_COUNT) {
				printf("WARNING: %s temperature exceeded "
				    "critical temperature %d times in a row; "
				    "shutting down!\n",
				    sensor->sensor->name,
				    sensor->critical_count);
				reboot(RB_HALT | RB_POWERDOWN | RB_TIMEBAD);
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
			temp = ulmin(sensor->last_val,
			    sensor->sensor->max_temp);
			frac_excess = (temp -
			    sensor->sensor->target_temp)*100 /
			    (sensor->sensor->max_temp - temp + 1);
			if (frac_excess < 0)
				frac_excess = 0;
			if (sensor->sensor->zone == fan->fan->zone) {
				max_excess_zone = ulmax(max_excess_zone,
				    frac_excess);
				nsens_zone++;
			}
			average_excess += frac_excess;
			nsens++;
		}

		/* No sensors at all? Use default */
		if (nsens == 0) {
			fan->fan->set(fan->fan, fan->fan->default_rpm);
			continue;
		}

		average_excess /= nsens;

		/* If there are no sensors in this zone, use the average */
		if (nsens_zone == 0)
			max_excess_zone = average_excess;

		/*
		 * Scale the fan linearly in the max temperature in its
		 * thermal zone.
		 */
		max_excess_zone = ulmin(max_excess_zone, 100);
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
thermal_fan_register(struct thermal_fan *fan)
{
	struct thermal_fan_le *list_entry;

	thermal_thread_init();	/* first caller inits our thread */

	list_entry = malloc(sizeof(struct thermal_fan_le), M_DEVBUF,
	    M_ZERO | M_WAITOK);
	list_entry->fan = fan;

	SLIST_INSERT_HEAD(&fans, list_entry, entries);
}

void
thermal_sensor_register(struct thermal_temp *sensor)
{
	struct thermal_sens_le *list_entry;

	thermal_thread_init();	/* first caller inits our thread */

	list_entry = malloc(sizeof(struct thermal_sens_le), M_DEVBUF,
	    M_ZERO | M_WAITOK);
	list_entry->sensor = sensor;
	list_entry->last_val = 0;
	list_entry->critical_count = 0;

	SLIST_INSERT_HEAD(&sensors, list_entry, entries);
}
