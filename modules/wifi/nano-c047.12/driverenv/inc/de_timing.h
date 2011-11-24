/* $Id: $ */

/*!
  * @file de_timing.h
  *
  * Adaptation layer for time services
  *
  */

#ifndef DE_TIMING_H
#define DE_TIMING_H

/******************************************************************************
T Y P E D E F ' S
******************************************************************************/

extern struct timers_head active_timers;

/*! Type used to represent clock ticks */
#define driver_tick_t unsigned long

/*! Type used to represent milliseconds for timers */
#define driver_msec_t uint32_t

/*! Type used to represent timer identifiers */
#define driver_timer_id_t uintptr_t


/** @defgroup de_timing_api Driver Environment API timing functions
 *  @{
 */

/*!
 * @brief Get a high resolution tick count.
 *
 * The time is used as a timestamp and does not have to correspond
 * to any particular epoch as long as it is monotonically increasing
 * between driver start and stop.
 *
 * @return (driver_tick_t )
 */

#define DriverEnvironment_GetHighResTick()      (driver_tick_t )0

/*!
 * @brief Get the current time in platform dependant ticks.
 *
 * The time is used as a timestamp and does not have to correspond
 * to any particular epoch as long as it is monotonically increasing
 * between driver start and stop.
 *
 * @return (driver_tick_t )
 */
#undef DriverEnvironment_GetTicks
#define DriverEnvironment_GetTicks()            ((driver_tick_t )jiffies)



/*!
 * @brief Get the current length in platform dependant ticks of a milli second.
 *
 * @return (driver_msec_t)
 */
#undef DriverEnvironment_msec_to_ticks
#define DriverEnvironment_msec_to_ticks(_msec)  msecs_to_jiffies(_msec)


/*!
 * @brief Get the current length in milli seconds of a platform dependant tick.
 *
 * @return (driver_msec_t)
 */

#undef DriverEnvironment_tick_to_msec
#define DriverEnvironment_tick_to_msec(_ticks)  jiffies_to_msecs(_ticks)


/*!
 * @brief Get the current time in milliseconds.
 *
 * The time is used as a timestamp and does not have to correspond
 * to any particular epoch as long as it is monotonically increasing
 * between driver start and stop.
 *
 * @return (driver_msec_t)
 */
driver_msec_t  DriverEnvironment_GetTimestamp_msec(void);



/*!
 * @brief Get the current wall time.
 *
 * The function returns the current wall time corresponding to the
 * UNIX epoch (seconds since January 1, 1970 00:00 UTC).
 *
 * @return nothing
 */
void DriverEnvironment_GetTimestamp_wall(long *sec, long *usec);

/*!
 * Allocate a new timer from the timer array.
 * @param id (driver_timer_id_t *) OUTPUT: Id of the allocated timer.
 * @param restartFromCb (int) 1 if the timer should be able to be restarted by 
 *   the timer callback.
 * @return (int) 1 on success. 0 otherwise.
 */
int DriverEnvironment_GetNewTimer(driver_timer_id_t *id, int restartFromCb);

/*!
 * @brief Register a timer-based callback.
 *
 * The timer must have been allocated with DriverEnvironment_GetNewTimer().
 *
 * @param time (long) Time in ms after which the callback should be executed.
 * @param timer_id (driver_timer_id_t) Timer id. Use 0 for the default WiFiEngine system timer.
 * @param cb (de_callback_t) Callback to be invoked at timer expiration.
 * @param repeating (int) If 1 then the timer is repeating and the callback will be called
 *                  each time the timer is triggered. If 0 then the callback will
 *                  be triggered only once.
 * @return (int) 0 if the timer is busy, 1 on success, -1 on invalid timer_id.
 */
int DriverEnvironment_RegisterTimerCallback(long time, driver_timer_id_t timer_id, de_callback_t cb, int repeating);

/*!
 * @brief Free a timer
 *
 * Cancels and frees a timer. The timer id is invalid after this call.
 *
 * @param timer_id (driver_timer_id_t) Timer id.
 * @return (void)
 */
void DriverEnvironment__FreeTimer(driver_timer_id_t id);
#define DriverEnvironment_FreeTimer(ID)         \
   ({ DriverEnvironment__FreeTimer((ID)); (ID) = 0;})



/*!
 * @brief Cancel a timer
 *
 * Inactivates a timer. The timer is stopped and can be restarter with
 * DriverEnvironment_RegisterTimerCallback().
 *
 * @param timer_id (driver_timer_id_t) Timer id.
 * @return (void)
 */
void DriverEnvironment_CancelTimer(driver_timer_id_t id);



#ifdef WITH_TIMESTAMPS
#define DE_TIMESTAMP(str) DriverEnvironment_DeltaTimestamp(str)
#else
#define DE_TIMESTAMP(str)
#endif


/** @} */ /* End of de_timing_api group */
#endif /* DE_TIMING_H */
