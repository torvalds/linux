#ifndef _LINUX_ALARMTIMER_H
#define _LINUX_ALARMTIMER_H

#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/timerqueue.h>
#include <linux/rtc.h>

enum alarmtimer_type {
	ALARM_REALTIME,
	ALARM_BOOTTIME,

	ALARM_NUMTYPE,
};

enum alarmtimer_restart {
	ALARMTIMER_NORESTART,
	ALARMTIMER_RESTART,
};

/**
 * struct alarm - Alarm timer structure
 * @node:	timerqueue node for adding to the event list this value
 *		also includes the expiration time.
 * @period:	Period for recuring alarms
 * @function:	Function pointer to be executed when the timer fires.
 * @type:	Alarm type (BOOTTIME/REALTIME)
 * @enabled:	Flag that represents if the alarm is set to fire or not
 * @data:	Internal data value.
 */
struct alarm {
	struct timerqueue_node	node;
	enum alarmtimer_restart	(*function)(struct alarm *, ktime_t now);
	enum alarmtimer_type	type;
	bool			enabled;
	void			*data;
};

void alarm_init(struct alarm *alarm, enum alarmtimer_type type,
		enum alarmtimer_restart (*function)(struct alarm *, ktime_t));
void alarm_start(struct alarm *alarm, ktime_t start);
void alarm_cancel(struct alarm *alarm);

u64 alarm_forward(struct alarm *alarm, ktime_t now, ktime_t interval);

#endif
