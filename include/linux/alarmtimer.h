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

struct alarm {
	struct timerqueue_node	node;
	ktime_t			period;
	void			(*function)(struct alarm *);
	enum alarmtimer_type	type;
	char			enabled;
	void			*data;
};

void alarm_init(struct alarm *alarm, enum alarmtimer_type type,
		void (*function)(struct alarm *));
void alarm_start(struct alarm *alarm, ktime_t start, ktime_t period);
void alarm_cancel(struct alarm *alarm);

#endif
