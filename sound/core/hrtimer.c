/*
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/hrtimer.h>
#include <sound/core.h>
#include <sound/timer.h>

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("ALSA hrtimer backend");
MODULE_LICENSE("GPL");

MODULE_ALIAS("snd-timer-" __stringify(SNDRV_TIMER_GLOBAL_HRTIMER));

#define NANO_SEC	1000000000UL	/* 10^9 in sec */
static unsigned int resolution;

struct snd_hrtimer {
	struct snd_timer *timer;
	struct hrtimer hrt;
};

static enum hrtimer_restart snd_hrtimer_callback(struct hrtimer *hrt)
{
	struct snd_hrtimer *stime = container_of(hrt, struct snd_hrtimer, hrt);
	struct snd_timer *t = stime->timer;
	hrtimer_forward_now(hrt, ktime_set(0, t->sticks * resolution));
	snd_timer_interrupt(stime->timer, t->sticks);
	return HRTIMER_RESTART;
}

static int snd_hrtimer_open(struct snd_timer *t)
{
	struct snd_hrtimer *stime;

	stime = kmalloc(sizeof(*stime), GFP_KERNEL);
	if (!stime)
		return -ENOMEM;
	hrtimer_init(&stime->hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	stime->timer = t;
	stime->hrt.cb_mode = HRTIMER_CB_SOFTIRQ;
	stime->hrt.function = snd_hrtimer_callback;
	t->private_data = stime;
	return 0;
}

static int snd_hrtimer_close(struct snd_timer *t)
{
	struct snd_hrtimer *stime = t->private_data;

	if (stime) {
		hrtimer_cancel(&stime->hrt);
		kfree(stime);
		t->private_data = NULL;
	}
	return 0;
}

static int snd_hrtimer_start(struct snd_timer *t)
{
	struct snd_hrtimer *stime = t->private_data;

	hrtimer_start(&stime->hrt, ktime_set(0, t->sticks * resolution),
		      HRTIMER_MODE_REL);
	return 0;
}

static int snd_hrtimer_stop(struct snd_timer *t)
{
	struct snd_hrtimer *stime = t->private_data;

	hrtimer_cancel(&stime->hrt);
	return 0;
}

static struct snd_timer_hardware hrtimer_hw = {
	.flags =	(SNDRV_TIMER_HW_AUTO |
			 /*SNDRV_TIMER_HW_FIRST |*/
			 SNDRV_TIMER_HW_TASKLET),
	.open =		snd_hrtimer_open,
	.close =	snd_hrtimer_close,
	.start =	snd_hrtimer_start,
	.stop =		snd_hrtimer_stop,
};

/*
 * entry functions
 */

static struct snd_timer *mytimer;

static int __init snd_hrtimer_init(void)
{
	struct snd_timer *timer;
	struct timespec tp;
	int err;

	hrtimer_get_res(CLOCK_MONOTONIC, &tp);
	if (tp.tv_sec > 0 || !tp.tv_nsec) {
		snd_printk(KERN_ERR
			   "snd-hrtimer: Invalid resolution %u.%09u",
			   (unsigned)tp.tv_sec, (unsigned)tp.tv_nsec);
		return -EINVAL;
	}
	resolution = tp.tv_nsec;

	/* Create a new timer and set up the fields */
	err = snd_timer_global_new("hrtimer", SNDRV_TIMER_GLOBAL_HRTIMER,
				   &timer);
	if (err < 0)
		return err;

	timer->module = THIS_MODULE;
	strcpy(timer->name, "HR timer");
	timer->hw = hrtimer_hw;
	timer->hw.resolution = resolution;
	timer->hw.ticks = NANO_SEC / resolution;

	err = snd_timer_global_register(timer);
	if (err < 0) {
		snd_timer_global_free(timer);
		return err;
	}
	mytimer = timer; /* remember this */

	return 0;
}

static void __exit snd_hrtimer_exit(void)
{
	if (mytimer) {
		snd_timer_global_free(mytimer);
		mytimer = NULL;
	}
}

module_init(snd_hrtimer_init);
module_exit(snd_hrtimer_exit);
