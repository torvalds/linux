/*!
  * @file de_task.h
  *
  * Adaptation layer for task related functions
  *
  */

#ifndef DE_TASK_H
#define DE_TASK_H

/** @defgroup de_task_api Driver Environment API task functions
 *  @{
 */

/******************************************************************************
D E F I N E S
******************************************************************************/

#define SYSIO_USING_MONITOR
#define SYSIO_ENTER_MONITOR   DriverEnvironment_MonitorEnter()
#define SYSIO_EXIT_MONITOR    DriverEnvironment_MonitorExit()

/* M O D U L E   V A R I A B L E S *******************************************/

/******************************************************************************
T Y P E D E F ' S
******************************************************************************/

struct lock_s {
      spinlock_t lock;
      unsigned long irq_flags;
#ifdef WIFI_DEBUG_ON
      int acquire_lock_ps;
      int acquire_lock_line;
      const char * acquire_lock_file;
#endif
};

/* Mutex */
typedef struct semaphore driver_mutex_t;

/*! Type used to represent locks */
typedef struct lock_s  driver_lock_t;
/*! Type used to represent trylocks */
typedef int            driver_trylock_t;

/*!
 * Set read thread priority higher. This function may do nothing.
 */
void DriverEnvironment_SetPriorityThreadHigh(void);

/*!
 * Set read thread priority to default value. This function may do nothing.
 */
void DriverEnvironment_SetPriorityThreadLow(void);

/* REMARK: on platforms where we run all of WiFiEngine in a single
 * task, the locking primitives can be empty, same with the lock types
 * above */
/* Read and write lock wrappers */
/*!
 * Initialize a spinlock-type lock. This lock must prohibit concurrent execution
 * in sections where the lock is held. No blocking operations will be attempted
 * while this lock is held.
 * @param lock (driver_lock_t *)
 */
#define DriverEnvironment_initialize_lock(__lk) ({      \
         memset((__lk), 0, sizeof(*(__lk)));            \
         spin_lock_init(&(__lk)->lock);                 \
      })

/*!
 * \brief Free a lock that was initialized with DriverEnvironment_initialize_lock().
 * @param lock (driver_lock_t *)
 */
#define DriverEnvironment_free_lock(__lk)

/*!
 * \brief Acquire a lock
 *
 * Acquire a spinlock-type lock. This lock must prohibit concurrent execution
 * in sections where the lock is held. No blocking operations will be attempted
 * while this lock is held.
 * @param lock (driver_lock_t *)
 */
#ifdef WIFI_DEBUG_ON
#define DriverEnvironment_acquire_lock(lk) do {                                                                         \
   if (current->pid == 0 && (lk)->acquire_lock_file != NULL) { /* Interrupt */                                          \
      printk(KERN_EMERG "Possible double lock in interrupt ctx: First in %s line %d. Second in %s line %d",                     \
            (lk)->acquire_lock_file, (lk)->acquire_lock_line, __FILE__, __LINE__);                                      \
   } else if ((lk)->acquire_lock_ps == current->pid && (lk)->acquire_lock_file != NULL) { /* Same process */            \
      printk(KERN_EMERG "Possible double lock in process ctx: First %s line %d. Second %s line %d",                               \
            (lk)->acquire_lock_file, (lk)->acquire_lock_line, __FILE__, __LINE__);                                      \
   }                                                                                                                    \
   spin_lock_irqsave(&((lk)->lock), ((lk)->irq_flags));                                                                 \
   (lk)->acquire_lock_ps = current->pid;                                                                                \
   (lk)->acquire_lock_line = __LINE__;                                                                                  \
   (lk)->acquire_lock_file = __FILE__;                                                                                  \
} while(0)
#else
#define DriverEnvironment_acquire_lock(lk) spin_lock_irqsave(&((lk)->lock), ((lk)->irq_flags))
#endif

/*!
 * \brief Release a lock
 *
 * Release a spinlock-type lock. 
 * @param lock (driver_lock_t *)
 */
#ifdef WIFI_DEBUG_ON
#define DriverEnvironment_release_lock(lk) do {                                                                         \
   (lk)->acquire_lock_ps = 0;                                                                                           \
   (lk)->acquire_lock_line = 0;                                                                                         \
   (lk)->acquire_lock_file = NULL;                                                                                      \
   spin_unlock_irqrestore(&((lk)->lock), ((lk)->irq_flags));                                                            \
} while(0)
#else
#define DriverEnvironment_release_lock(lk) spin_unlock_irqrestore(&((lk)->lock), ((lk)->irq_flags))
#endif

/*!
 * \brief Initialize a trylock. 
 * This lock must allow blocking operations (such as I/O)
 * while it is being held. A atomic test-and-set-type lock is sufficient.
 * @param lock (driver_trylock_t *)
 */
void DriverEnvironment_init_trylock(driver_trylock_t *lock);

/*!
 * \brief Attempt to Acquire a trylock. 
 * This lock must allow blocking operations (such as I/O)
 * while it is being held. 
 * @param lock (driver_trylock_t *)
 * @return (driver_trylock_t) If the lock is held when this function is called it should 
 * immediatly return LOCK_LOCKED without having acquired the lock. If the lock
 * is free then the call should Acquire the lock and return LOCK_UNLOCKED
 * (that is, it returns the previous state of the lock).
 */
driver_trylock_t DriverEnvironment_acquire_trylock(driver_trylock_t *lock);

/*!
 * \brief Release a trylock that was Acquired with DriverEnvironment_acquire_trylock().
 * @param lock (driver_trylock_t *)
 */
void           DriverEnvironment_release_trylock(driver_trylock_t *lock);


/*!
 * \brief Acquire a read lock. 
 * This must block a write lock acquire on the same
 * lock while the read lock is held. Several consumers may acquire the same
 * read lock at the same time.
 * This function can be implemented using DriverEnvironment_acquire_lock()
 * if the platform does not support or need read locks.
 * @param lock (driver_lock_t *)
 */
void           DriverEnvironment_acquire_read_lock(driver_lock_t *);


/*!
 * \brief Release a read lock
 * This function can be implemented using DriverEnvironment_release_lock()
 * if the platform does not support or need read locks.
 * @param lock (driver_lock_t *)
 */
void           DriverEnvironment_release_read_lock(driver_lock_t *);

/*!
 * \brief Acquire a write lock
 * This must block if any consumer is holding a read lock on the same lock.
 * This function can be implemented using DriverEnvironment_acquire_lock()
 * if the platform does not support or need write locks.
 * @param lock (driver_lock_t *)
 */
void           DriverEnvironment_acquire_write_lock(driver_lock_t *);

/*!
 * \brief Release a write lock
 * This function can be implemented using DriverEnvironment_release_lock()
 * if the platform does not support or need write locks.
 * @param lock (driver_lock_t *)
 */
void           DriverEnvironment_release_write_lock(driver_lock_t *);

/*!
 * \brief Init a mutex.
 *
 * Porting: In a non-preemptive kernel, all mutexes can be discarded.
 */
static inline void DriverEnvironment_mutex_init(driver_mutex_t *sem) { sema_init(sem, 1); }

/*!
 * \brief Mutex down
 *
 * Used to get hold of the mutex.
 *
 * \return 0 on success, non-zero otherwise.
 *
 * Porting: Should it not get the mutex the function must schedule an
 * other task. It should return DRIVERENVIRONMENT_SUCCESS when the mutex is retrieved.
 * failures can occur (as a user interrupt), such a failure should
 * result in returning DRIVERENVIRONMENT_FAILURE.
 */
static inline int DriverEnvironment_mutex_down(driver_mutex_t *sem) { \
   return down_interruptible(sem) == 0 ? DRIVERENVIRONMENT_SUCCESS : DRIVERENVIRONMENT_FAILURE;
}

/*!
 * \brief Mutex up
 *
 * Used when finished with the mutex.
 */
static inline void DriverEnvironment_mutex_up(driver_mutex_t *sem) { up(sem); }

/*!
 * \brief Monitor enter
 *
 * Used to get hold of a global lock.
 *
 */
void DriverEnvironment_MonitorEnter(void);

/*!
 * \brief Monitor exit
 *
 * Used to release global lock.
 */
void DriverEnvironment_MonitorExit(void);

/*!
 * Yield the processor to other tasks for a while.
 *
 * @param ms Number of milli secounds to suspend the current task
 *
 * @return (void)
 */
#define DriverEnvironment_Yield(_ms) TOS_SleepMs(_ms)

/** @} */ /* End of de_task_api group */
#endif /* DE_TASK_H */
