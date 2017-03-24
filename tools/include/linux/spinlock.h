#define spinlock_t		pthread_mutex_t
#define DEFINE_SPINLOCK(x)	pthread_mutex_t x = PTHREAD_MUTEX_INITIALIZER;

#define spin_lock_irqsave(x, f)		(void)f, pthread_mutex_lock(x)
#define spin_unlock_irqrestore(x, f)	(void)f, pthread_mutex_unlock(x)
