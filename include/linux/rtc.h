/*
 * Generic RTC interface.
 * This version contains the part of the user interface to the Real Time Clock
 * service. It is used with both the legacy mc146818 and also  EFI
 * Struct rtc_time and first 12 ioctl by Paul Gortmaker, 1996 - separated out
 * from <linux/mc146818rtc.h> to this file for 2.4 kernels.
 *
 * Copyright (C) 1999 Hewlett-Packard Co.
 * Copyright (C) 1999 Stephane Eranian <eranian@hpl.hp.com>
 */
#ifndef _LINUX_RTC_H_
#define _LINUX_RTC_H_


#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/nvmem-provider.h>
#include <uapi/linux/rtc.h>

extern int rtc_month_days(unsigned int month, unsigned int year);
extern int rtc_year_days(unsigned int day, unsigned int month, unsigned int year);
extern int rtc_valid_tm(struct rtc_time *tm);
extern time64_t rtc_tm_to_time64(struct rtc_time *tm);
extern void rtc_time64_to_tm(time64_t time, struct rtc_time *tm);
ktime_t rtc_tm_to_ktime(struct rtc_time tm);
struct rtc_time rtc_ktime_to_tm(ktime_t kt);

/*
 * rtc_tm_sub - Return the difference in seconds.
 */
static inline time64_t rtc_tm_sub(struct rtc_time *lhs, struct rtc_time *rhs)
{
	return rtc_tm_to_time64(lhs) - rtc_tm_to_time64(rhs);
}

/**
 * Deprecated. Use rtc_time64_to_tm().
 */
static inline void rtc_time_to_tm(unsigned long time, struct rtc_time *tm)
{
	rtc_time64_to_tm(time, tm);
}

/**
 * Deprecated. Use rtc_tm_to_time64().
 */
static inline int rtc_tm_to_time(struct rtc_time *tm, unsigned long *time)
{
	*time = rtc_tm_to_time64(tm);

	return 0;
}

#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/timerqueue.h>
#include <linux/workqueue.h>

extern struct class *rtc_class;

/*
 * For these RTC methods the device parameter is the physical device
 * on whatever bus holds the hardware (I2C, Platform, SPI, etc), which
 * was passed to rtc_device_register().  Its driver_data normally holds
 * device state, including the rtc_device pointer for the RTC.
 *
 * Most of these methods are called with rtc_device.ops_lock held,
 * through the rtc_*(struct rtc_device *, ...) calls.
 *
 * The (current) exceptions are mostly filesystem hooks:
 *   - the proc() hook for procfs
 *   - non-ioctl() chardev hooks:  open(), release(), read_callback()
 *
 * REVISIT those periodic irq calls *do* have ops_lock when they're
 * issued through ioctl() ...
 */
struct rtc_class_ops {
	int (*open)(struct device *);
	void (*release)(struct device *);
	int (*ioctl)(struct device *, unsigned int, unsigned long);
	int (*read_time)(struct device *, struct rtc_time *);
	int (*set_time)(struct device *, struct rtc_time *);
	int (*read_alarm)(struct device *, struct rtc_wkalrm *);
	int (*set_alarm)(struct device *, struct rtc_wkalrm *);
	int (*proc)(struct device *, struct seq_file *);
	int (*set_mmss64)(struct device *, time64_t secs);
	int (*set_mmss)(struct device *, unsigned long secs);
	int (*read_callback)(struct device *, int data);
	int (*alarm_irq_enable)(struct device *, unsigned int enabled);
	int (*read_offset)(struct device *, long *offset);
	int (*set_offset)(struct device *, long offset);
};

#define RTC_DEVICE_NAME_SIZE 20
typedef struct rtc_task {
	void (*func)(void *private_data);
	void *private_data;
} rtc_task_t;


struct rtc_timer {
	struct rtc_task	task;
	struct timerqueue_node node;
	ktime_t period;
	int enabled;
};


/* flags */
#define RTC_DEV_BUSY 0

struct rtc_device {
	struct device dev;
	struct module *owner;

	int id;

	const struct rtc_class_ops *ops;
	struct mutex ops_lock;

	struct cdev char_dev;
	unsigned long flags;

	unsigned long irq_data;
	spinlock_t irq_lock;
	wait_queue_head_t irq_queue;
	struct fasync_struct *async_queue;

	struct rtc_task *irq_task;
	spinlock_t irq_task_lock;
	int irq_freq;
	int max_user_freq;

	struct timerqueue_head timerqueue;
	struct rtc_timer aie_timer;
	struct rtc_timer uie_rtctimer;
	struct hrtimer pie_timer; /* sub second exp, so needs hrtimer */
	int pie_enabled;
	struct work_struct irqwork;
	/* Some hardware can't support UIE mode */
	int uie_unsupported;

	bool registered;

	struct nvmem_config *nvmem_config;
	struct nvmem_device *nvmem;
	/* Old ABI support */
	bool nvram_old_abi;
	struct bin_attribute *nvram;

#ifdef CONFIG_RTC_INTF_DEV_UIE_EMUL
	struct work_struct uie_task;
	struct timer_list uie_timer;
	/* Those fields are protected by rtc->irq_lock */
	unsigned int oldsecs;
	unsigned int uie_irq_active:1;
	unsigned int stop_uie_polling:1;
	unsigned int uie_task_active:1;
	unsigned int uie_timer_active:1;
#endif
};
#define to_rtc_device(d) container_of(d, struct rtc_device, dev)

extern struct rtc_device *rtc_device_register(const char *name,
					struct device *dev,
					const struct rtc_class_ops *ops,
					struct module *owner);
extern struct rtc_device *devm_rtc_device_register(struct device *dev,
					const char *name,
					const struct rtc_class_ops *ops,
					struct module *owner);
struct rtc_device *devm_rtc_allocate_device(struct device *dev);
int __rtc_register_device(struct module *owner, struct rtc_device *rtc);
extern void rtc_device_unregister(struct rtc_device *rtc);
extern void devm_rtc_device_unregister(struct device *dev,
					struct rtc_device *rtc);

extern int rtc_read_time(struct rtc_device *rtc, struct rtc_time *tm);
extern int rtc_set_time(struct rtc_device *rtc, struct rtc_time *tm);
extern int rtc_set_ntp_time(struct timespec64 now);
int __rtc_read_alarm(struct rtc_device *rtc, struct rtc_wkalrm *alarm);
extern int rtc_read_alarm(struct rtc_device *rtc,
			struct rtc_wkalrm *alrm);
extern int rtc_set_alarm(struct rtc_device *rtc,
				struct rtc_wkalrm *alrm);
extern int rtc_initialize_alarm(struct rtc_device *rtc,
				struct rtc_wkalrm *alrm);
extern void rtc_update_irq(struct rtc_device *rtc,
			unsigned long num, unsigned long events);

extern struct rtc_device *rtc_class_open(const char *name);
extern void rtc_class_close(struct rtc_device *rtc);

extern int rtc_irq_register(struct rtc_device *rtc,
				struct rtc_task *task);
extern void rtc_irq_unregister(struct rtc_device *rtc,
				struct rtc_task *task);
extern int rtc_irq_set_state(struct rtc_device *rtc,
				struct rtc_task *task, int enabled);
extern int rtc_irq_set_freq(struct rtc_device *rtc,
				struct rtc_task *task, int freq);
extern int rtc_update_irq_enable(struct rtc_device *rtc, unsigned int enabled);
extern int rtc_alarm_irq_enable(struct rtc_device *rtc, unsigned int enabled);
extern int rtc_dev_update_irq_enable_emul(struct rtc_device *rtc,
						unsigned int enabled);

void rtc_handle_legacy_irq(struct rtc_device *rtc, int num, int mode);
void rtc_aie_update_irq(void *private);
void rtc_uie_update_irq(void *private);
enum hrtimer_restart rtc_pie_update_irq(struct hrtimer *timer);

int rtc_register(rtc_task_t *task);
int rtc_unregister(rtc_task_t *task);
int rtc_control(rtc_task_t *t, unsigned int cmd, unsigned long arg);

void rtc_timer_init(struct rtc_timer *timer, void (*f)(void *p), void *data);
int rtc_timer_start(struct rtc_device *rtc, struct rtc_timer *timer,
		    ktime_t expires, ktime_t period);
void rtc_timer_cancel(struct rtc_device *rtc, struct rtc_timer *timer);
int rtc_read_offset(struct rtc_device *rtc, long *offset);
int rtc_set_offset(struct rtc_device *rtc, long offset);
void rtc_timer_do_work(struct work_struct *work);

static inline bool is_leap_year(unsigned int year)
{
	return (!(year % 4) && (year % 100)) || !(year % 400);
}

#define rtc_register_device(device) \
	__rtc_register_device(THIS_MODULE, device)

#ifdef CONFIG_RTC_HCTOSYS_DEVICE
extern int rtc_hctosys_ret;
#else
#define rtc_hctosys_ret -ENODEV
#endif

#endif /* _LINUX_RTC_H_ */
