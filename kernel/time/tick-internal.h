/*
 * tick internal variable and functions used by low/high res code
 */
DECLARE_PER_CPU(struct tick_device, tick_cpu_device);
extern spinlock_t tick_device_lock;
extern ktime_t tick_next_period;
extern ktime_t tick_period;

extern void tick_setup_periodic(struct clock_event_device *dev, int broadcast);
extern void tick_handle_periodic(struct clock_event_device *dev);

/*
 * Broadcasting support
 */
#ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
extern int tick_do_broadcast(cpumask_t mask);
extern struct tick_device tick_broadcast_device;
extern spinlock_t tick_broadcast_lock;

extern int tick_device_uses_broadcast(struct clock_event_device *dev, int cpu);
extern int tick_check_broadcast_device(struct clock_event_device *dev);
extern int tick_is_broadcast_device(struct clock_event_device *dev);
extern void tick_broadcast_on_off(unsigned long reason, int *oncpu);
extern void tick_shutdown_broadcast(unsigned int *cpup);

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
