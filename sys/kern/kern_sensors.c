/*	$OpenBSD: kern_sensors.c,v 1.40 2022/12/05 23:18:37 deraadt Exp $	*/

/*
 * Copyright (c) 2005 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2006 Constantine A. Murenin <cnst+openbsd@bugmail.mojo.ru>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/hotplug.h>
#include <sys/timeout.h>
#include <sys/task.h>
#include <sys/rwlock.h>
#include <sys/atomic.h>

#include <sys/sensors.h>
#include "hotplug.h"

struct taskq		*sensors_taskq;
int			sensordev_count;
SLIST_HEAD(, ksensordev) sensordev_list =
    SLIST_HEAD_INITIALIZER(sensordev_list);

void
sensordev_install(struct ksensordev *sensdev)
{
	struct ksensordev *v, *nv;
	int s;

	s = splhigh();
	if (sensordev_count == 0) {
		sensdev->num = 0;
		SLIST_INSERT_HEAD(&sensordev_list, sensdev, list);
	} else {
		for (v = SLIST_FIRST(&sensordev_list);
		    (nv = SLIST_NEXT(v, list)) != NULL; v = nv)
			if (nv->num - v->num > 1)
				break;
		sensdev->num = v->num + 1;
		SLIST_INSERT_AFTER(v, sensdev, list);
	}
	sensordev_count++;
	splx(s);

#if NHOTPLUG > 0
	hotplug_device_attach(DV_DULL, "sensordev");
#endif
}

void
sensor_attach(struct ksensordev *sensdev, struct ksensor *sens)
{
	struct ksensor *v, *nv;
	struct ksensors_head *sh;
	int s, i;

	s = splhigh();
	sh = &sensdev->sensors_list;
	if (sensdev->sensors_count == 0) {
		for (i = 0; i < SENSOR_MAX_TYPES; i++)
			sensdev->maxnumt[i] = 0;
		sens->numt = 0;
		SLIST_INSERT_HEAD(sh, sens, list);
	} else {
		for (v = SLIST_FIRST(sh);
		    (nv = SLIST_NEXT(v, list)) != NULL; v = nv)
			if (v->type == sens->type && (v->type != nv->type || 
			    (v->type == nv->type && nv->numt - v->numt > 1)))
				break;
		/* sensors of the same type go after each other */
		if (v->type == sens->type)
			sens->numt = v->numt + 1;
		else
			sens->numt = 0;
		SLIST_INSERT_AFTER(v, sens, list);
	}
	/* we only increment maxnumt[] if the sensor was added
	 * to the last position of sensors of this type
	 */
	if (sensdev->maxnumt[sens->type] == sens->numt)
		sensdev->maxnumt[sens->type]++;
	sensdev->sensors_count++;
	splx(s);
}

void
sensordev_deinstall(struct ksensordev *sensdev)
{
	int s;

	s = splhigh();
	sensordev_count--;
	SLIST_REMOVE(&sensordev_list, sensdev, ksensordev, list);
	splx(s);

#if NHOTPLUG > 0
	hotplug_device_detach(DV_DULL, "sensordev");
#endif
}

void
sensor_detach(struct ksensordev *sensdev, struct ksensor *sens)
{
	struct ksensors_head *sh;
	int s;

	s = splhigh();
	sh = &sensdev->sensors_list;
	sensdev->sensors_count--;
	SLIST_REMOVE(sh, sens, ksensor, list);
	/* we only decrement maxnumt[] if this is the tail 
	 * sensor of this type
	 */
	if (sens->numt == sensdev->maxnumt[sens->type] - 1)
		sensdev->maxnumt[sens->type]--;
	splx(s);
}

int
sensordev_get(int num, struct ksensordev **sensdev)
{
	struct ksensordev *sd;

	SLIST_FOREACH(sd, &sensordev_list, list) {
		if (sd->num == num) {
			*sensdev = sd;
			return (0);
		}
		if (sd->num > num)
			return (ENXIO);
	}
	return (ENOENT);
}

int
sensor_find(int dev, enum sensor_type type, int numt, struct ksensor **ksensorp)
{
	struct ksensor *s;
	struct ksensordev *sensdev;
	struct ksensors_head *sh;
	int ret;

	ret = sensordev_get(dev, &sensdev);
	if (ret)
		return (ret);

	sh = &sensdev->sensors_list;
	SLIST_FOREACH(s, sh, list)
		if (s->type == type && s->numt == numt) {
			*ksensorp = s;
			return (0);
		}

	return (ENOENT);
}

struct sensor_task {
	void				(*func)(void *);
	void				*arg;

	unsigned int			period;
	struct timeout			timeout;
	struct task			task;
	struct rwlock			lock;
};

void	sensor_task_tick(void *);
void	sensor_task_work(void *);

struct sensor_task *
sensor_task_register(void *arg, void (*func)(void *), unsigned int period)
{
	struct sensor_task *st;

#ifdef DIAGNOSTIC
	if (period == 0)
		panic("sensor_task_register: period is 0");
#endif

	if (sensors_taskq == NULL &&
	    (sensors_taskq = taskq_create("sensors", 1, IPL_HIGH, 0)) == NULL)
		sensors_taskq = systq;

	st = malloc(sizeof(*st), M_DEVBUF, M_NOWAIT);
	if (st == NULL)
		return (NULL);

	st->func = func;
	st->arg = arg;
	st->period = period;
	timeout_set(&st->timeout, sensor_task_tick, st);
	task_set(&st->task, sensor_task_work, st);
	rw_init(&st->lock, "sensor");

	sensor_task_tick(st);

	return (st);
}

void
sensor_task_unregister(struct sensor_task *st)
{
	/*
	 * we can't reliably timeout_del or task_del because there's a window
	 * between when they come off the lists and the timeout or task code
	 * actually runs the respective handlers for them. mark the sensor_task
	 * as dying by setting period to 0 and let sensor_task_work mop up.
	 */

	rw_enter_write(&st->lock);
	st->period = 0;
	rw_exit_write(&st->lock);
}

void
sensor_task_tick(void *arg)
{
	struct sensor_task *st = arg;
	task_add(sensors_taskq, &st->task);
}

static int sensors_quiesced;
static int sensors_running;

void
sensor_quiesce(void)
{
	sensors_quiesced = 1;
	while (sensors_running > 0)
		tsleep_nsec(&sensors_running, PZERO, "sensorpause", INFSLP);

}
void
sensor_restart(void)
{
	sensors_quiesced = 0;
}

void
sensor_task_work(void *xst)
{
	struct sensor_task *st = xst;
	unsigned int period = 0;

	atomic_inc_int(&sensors_running);
	rw_enter_write(&st->lock);
	period = st->period;
	if (period > 0 && !sensors_quiesced)
		st->func(st->arg);
	rw_exit_write(&st->lock);
	if (atomic_dec_int_nv(&sensors_running) == 0) {
		if (sensors_quiesced)
			wakeup(&sensors_running);
	}

	if (period == 0)
		free(st, M_DEVBUF, sizeof(*st));
	else 
		timeout_add_sec(&st->timeout, period);
}
