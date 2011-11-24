/* $Id: driverenv.h,v 1.12 2007/09/24 14:18:29 peek Exp $ */

/*!
  * @file de_event.h
  *
  * Adaptation layer for event handling functions
  *
  */

#ifndef DE_EVENT_H
#define DE_EVENT_H




/******************************************************************************
T Y P E D E F ' S
******************************************************************************/



typedef enum _NRWIF_INTERNAL_MESSAGE
{
    NRWIFI_TARGET_IRQ,
    NRWIFI_SETMULTICASTADDR,
    NRWIFI_CLEARMULTICASTADDR,
    NRWIFI_TIMER_CALLBACK,
    NRWIFI_SENDPACKAGE,
    NRWIFI_INIT,
    NRWIFI_POWER_ON,
    NRWIFI_POWER_OFF,
    NRWIFI_POWER_SAVE_ENTER,
    NRWIFI_POWER_SAVE_EXIT,
    NRWIFI_GET_PHONE_MAC_ADDRESS,
    NRWIFI_BROAD_SCAN,
    NRWIFI_SPECIFIC_SCAN,
    NRWIFI_DEEP_SLEEP_ENTER,
    NRWIFI_DEEP_SLEEP_EXIT,
    NRWIFI_CONNECT,
    NRWIFI_DISCONNECT,
    NRWIFI_GET_RSSI,
    NRWIFI_SETIPADDRESS,
    NRWIFI_CLEARIPADDRESS,
    NRWIFI_TARGET_WAKEUP = 100,
}nrwifi_internal_message;

typedef void *driver_ind_param_t;

/* \brief Modular indication function.
 * 
 * Handle WiFiEngine indications that should trigger external events/actions.
 * This function is executed when WiFiEngine generates a indication event.
 *
 * @param type (we_indication_t) Type of indication.
 * @param data (void *) Indication data.
 * @param len (size_t) Length of data.
*/
/* REMARK: this can be empty initially, eventually it will have to
 * interact with the IP stack, such as indication media status etc. */
void DriverEnvironment_indicate(we_indication_t, void*, size_t);
/****** Event implementation ********/

/* REMARK: this can do nothing initially */
enum sig_state
{
   DE_SIG_CLEAR,
   DE_SIG_SIGNALLED
};

/*! Type used to represent events */
typedef struct {
   enum sig_state state; /**< Event disposition */
   wait_queue_head_t wait_queue;
} de_event_t;

/*!
 * Initialize an event struct.
 * @param ev (de_event_t *)Pointer to an event struct to be initialized.
 * @return (int)
 * - 1 on success.
 * - 0 otherwise.
 */
int DriverEnvironment_InitializeEvent(de_event_t *ev);

/*!
 * Uninitialize an event struct.
 * @param ev (de_event_t *) Pointer to an event struct to be uninitialized.
 * @return 
 * - 1 on success.
 * - 0 otherwise.
 */
void DriverEnvironment_UninitializeEvent(de_event_t *ev);


/*!
 * Signal an event.
 *
 * @param ev (de_event_t *) Event struct to signal.
 */
void DriverEnvironment_SignalEvent(de_event_t *ev);


/*!
 * @brief Wait on an event.
 * 
 * Wait on ev. This will block execution of this thread until someone
 * signals the event or the timeout occurs.
 * @param ev (de_event_t *) The event to wait on.
 * @param ms_to_wait (int) Number of msec to wait until timeout.
 * @return (int)
 * - DRIVERENVIRONMENT_FAILURE_NOT_ALLOWED when the platform doesn't
 *   implement waiting on events, or if the current context/runlevel
 *   doesn't allow waiting on an event.
 * - DRIVERENVIRONMENT_SUCCESS on a successful wait.
 * - DRIVERENVIRONMENT_SUCCESS_TIMEOUT if the wait timed out.
 */
int DriverEnvironment_WaitOnEvent(de_event_t *ev, int ms_to_wait);

/*!
 * Check if waiting on an event is allowed in this context.
 * For drivers not supporting events this should always return 0.
 *
 * @return (int)
 * - 1 if waiting on an event is allowed in this context.
 * - 0 if waiting is not allowed.
 */
int DriverEnvironment_IsEventWaitAllowed(void);

/** @} */ /* End of de_event_api group */


#endif /* DE_EVENT_H */
