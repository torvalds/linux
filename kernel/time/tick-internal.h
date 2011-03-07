/*
 * tick internal variable and functions used by low/high res code
 */

#define TICK_DO_TIMER_NONE	-1
#define TICK_DO_TIMER_BOOT	-2

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);
extern ktime_t tick_next_period;
extern ktime_t tick_period;
extern int tick_do_timer_cpu __read_mostly;

extern void tick_setup_periodic(struct clock_event_device *dev, int broadcast);
extern void tick_handle_periodic(struct clock_event_device *dev);

extern void clockevents_shutdown(struct clock_event_device *dev);

/*
 * NO_HZ / high resolution timer shared code
 */
#ifdef CONFIG_TICK_ONESHOT
extern void tick_setup_oneshot(struct clock_event_device *newdev,
			       void (*handler)(struct clock_event_device *),
			       ktime_t nextevt);
extern int tick_dev_program_event(struct clock_event_device *dev,
				  ktime_t expires, int force);
extern int tick_program_event(ktime_t expires, int force);
extern void tick_oneshot_notify(void);
extern int tick_switch_to_oneshot(void (*handler)(struct clock_event_device *));
extern void tick_resume_oneshot(void);
# ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
extern void tick_broadcast_setup_oneshot(struct clock_event_device *bc);
extern void tick_broadcast_oneshot_control(unsigned long reason);
extern void tick_broadcast_switch_to_oneshot(void);
extern void tick_shutdown_broadcast_oneshot(unsigned int *cpup);
extern int tick_resume_broadcast_oneshot(struct clock_event_device *bc);
extern int tick_broadcast_oneshot_active(void);
extern void tick_check_oneshot_broadcast(int cpu);
bool tick_broadcast_oneshot_available(void);
# else /* BROADCAST */
static inline void tick_broadcast_setup_oneshot(struct clock_event_device *bc)
{
	BUG();
}
static inline void tick_broadcast_oneshot_control(unsigned long reason) { }
static inline void tick_broadcast_switch_to_oneshot(void) { }
static inline void tick_shutdown_broadcast_oneshot(unsigned int *cpup) { }
static inline int tick_broadcast_oneshot_active(void) { return 0; }
static inline void tick_check_oneshot_broadcast(int cpu) { }
static inline bool tick_broadcast_oneshot_available(void) { return true; }
# endif /* !BROADCAST */

#else /* !ONESHOT */
static inline
void tick_setup_oneshot(struct clock_event_device *newdev,
			void (*handler)(struct clock_event_device *),
			ktime_t nextevt)
{
	BUG();
}
static inline void tick_resume_oneshot(void)
{
	BUG();
}
static inline int tick_program_event(ktime_t expires, int force)
{
	return 0;
}
static inline void tick_oneshot_notify(void) { }
static inline void tick_broadcast_setup_oneshot(struct clock_event_device *bc)
{
	BUG();
}
static inline void tick_broadcast_oneshot_control(unsigned long reason) { }
static inline void tick_shutdown_broadcast_oneshot(unsigned int *cpup) { }
static inline int tick_resume_broadcast_oneshot(struct clock_event_device *bc)
{
	return 0;
}
static inline int tick_broadcast_oneshot_active(void) { return 0; }
static inline bool tick_broadcast_oneshot_available(void) { return false; }
#endif /* !TICK_ONESHOT */

/*
 * Broadcasting support
 */
#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
extern int tick_device_uses_broadcast(struct clock_event_device *dev, int cpu);
extern int tick_check_broadcast_device(struct clock_event_device *dev);
extern int tick_is_broadcast_device(struct clock_event_device *dev);
extern void tick_broadcast_on_off(unsigned long reason, int *oncpu);
extern void tick_shutdown_broadcast(unsigned int *cpup);
extern void tick_suspend_broadcast(void);
extern int tick_resume_broadcast(void);

extern void
tick_set_periodic_handler(struct clock_event_device *dev, int broadcast);

#else /* !BROADCAST */

static inline int tick_check_broadcast_device(struct clock_event_device *dev)
{
	return 0;
}

static inline int tick_is_broadcast_device(struct clock_event_device *dev)
{
	return 0;
}
static inline int tick_device_uses_broadcast(struct clock_event_device *dev,
					     int cpu)
{
	return 0;
}
static inline void tick_do_periodic_broadcast(struct clock_event_device *d) { }
static inline void tick_broadcast_on_off(unsigned long reason, int *oncpu) { }
static inline void tick_shutdown_broadcast(unsigned int *cpup) { }
static inline void tick_suspend_broadcast(void) { }
static inline int tick_resume_broadcast(void) { return 0; }

/*
 * Set the periodic handler in non broadcast mode
 */
static inline void tick_set_periodic_handler(struct clock_event_device *dev,
					     int broadcast)
{
	dev->event_handler = tick_handle_periodic;
}
#endif /* !BROADCAST */

/*
 * Check, if the device is functional or a dummy for broadcast
 */
static inline int tick_device_is_functional(struct clock_event_device *dev)
{
	return !(dev->features & CLOCK_EVT_FEAT_DUMMY);
}
