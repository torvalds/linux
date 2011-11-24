/* $Id: driverenv.h,v 1.120 2008-03-14 16:54:32 miwi Exp $ */

/*!
* @file driverenv.h
*
*/


#ifndef DRIVERENV_H
#define DRIVERENV_H

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <linux/spinlock.h>

#include <asm/pgtable.h> /* XXX is this the correct way to defined BUG()? */

#include "de_common.h"

/* Abstraction of standard C libraries. */
#include "ucos.h"
#include "mac_api.h"
#include "de_clib.h"

#include "nanonet.h"
#include "nanoparam.h"
#include "nanoutil.h"
/** @defgroup c_lib_defines Abstraction of Standard C-Library functions
 *  @{
 */
#define GET_MIB_TIMEOUT  3000


#include <registry.h>
#ifndef driver_packet_ref
 #define driver_packet_ref void*
#endif 


/*******/

typedef int (*send_callback_fn_t)(char* buffer, int size);
typedef int (*receive_callback_fn_t)(char* buffer, int size);

/*! Mirror we_callback_t but avoid header file mess */
typedef int (*de_callback_t)(void *data, size_t data_len);

#include "de_timing.h"

struct de_timer {
   struct timer_list timer;
   de_callback_t callback;
   long time;
   int repeating;
   unsigned long flags;
   WEI_TQ_ENTRY(de_timer) tq;
#define DE_TIMER_LOCK		0
#define DE_TIMER_DESTROY	1
#define DE_TIMER_RUNNING        2
};

#include <linux/kref.h>
#define de_kref_init kref_init
#define de_kref_get kref_get
#define de_kref_put kref_put
#define de_kref kref

/** @defgroup driverenv_api Driver Environment API functions
 *  @{
 */

/*!
 * \brief Initialize the Driver Environment 
 * 
 * Calls platform specific initialization routines.
 * Called by WiFiEnding_Initialize.
 * 
 * @return (unsigned int) A platform specific Id for the initialized driver used
 * later by DriverEnvironment_Terminate
 */
unsigned int   DriverEnvironment_Startup(void);

/*!
 * \brief Terminate the Driver Environment
 *
 * Gracefully releasing all allocated resources during DriverEnvironment_Startup()
 *
 * @param driver_id (unsigned int) The Id returned by DriverEnvironment_Startup()
 * @return (int) DRIVERENVIRONMENT_SUCCESS on success or 
 * DRIVERENVIRONMENT_FAILURE in case of error
 */
int            DriverEnvironment_Terminate(unsigned int driver_id);


/*!
 * Set read thread priority higher. 
 */
void DriverEnvironment_SetPriorityThreadHigh(void);

/*!
 * Set read thread priority to default value. 
 */
void DriverEnvironment_SetPriorityThreadLow(void);


void           DriverEnvironment_Enable_Boot(void);
void           DriverEnvironment_Disable_Boot(void);

/*!
 * @brief Indicate that a coredump has started.
 *
 * The function is called on the first message that a coredump is in progress.
 * The driverenvironment coredump functions is controled by the excistens if a 
 * non-NULL *ctx variable that will not be modified only by the platform.
 *
 * if on return
 *
 * *ctx == NULL
 *
 * No coredump will be read from target, do not collect 100 dollars. Go directly
 * to jail. DriverEnvironment_Core_Dump_Complete will be called later on.
 *
 * *ctx != NULL
 *
 * DriverEnvironment_Core_Dump_Write will be called repeatedly with new data.
 * When no more data is available DriverEnvironment_Core_Dump_Complete will be called.
 *
 * @return nothing
 */
void DriverEnvironment_Core_Dump_Started(
      int coredump,
      int restart, 
      uint8_t objId, 
      uint8_t errCode, 
      size_t expected_size, /* or more */
      size_t max_size, /* used to help malloc */
      void **ctx);


/*!
 * @brief Write coredump data to media.
 *
 * The function is called after DriverEnvironment_Core_Dump_Started if *ctx != NULL
 *
 * @return nothing
 */
void DriverEnvironment_Core_Dump_Write(
   void *ctx,
   void *data, 
   size_t len);


/*!
 * @brief Will be called if an ongiong coredump was interupted.
 *
 * The function may be called after DriverEnvironment_Core_Dump_Started if *ctx != NULL
 *
 * @return nothing
 */
void DriverEnvironment_Core_Dump_Abort(
      int coredump,
      int restart, 
      uint8_t objid, 
      uint8_t err_code, 
      void **ctx);

/*!
 * @brief Will be called when a coredump is complete.
 *
 * 'coredump' will be 0 if *ctx == NULL
 *
 * @return nothing
 */
void DriverEnvironment_Core_Dump_Complete(
      int coredump,
      int restart, 
      uint8_t objid, 
      uint8_t err_code, 
      void **ctx);



#ifdef MEM_TRACE
void memtrace_alloc(const void *ptr, size_t, int type, const char *func, unsigned int line);
void memtrace_free(const void *ptr, int type, int poison, const char *func, unsigned int line);
#define MTALLOC(S, B, L) memtrace_alloc((B), (S), (L), __func__, __LINE__)
#define MTFREE(B, L, P) memtrace_free((B), (L), (P), __func__, __LINE__)
#else
#define MTALLOC(S, B, L) 
#define MTFREE(B, L, P) 
#endif


/*!
 * \brief Send a message to the device
 *
 * Transfer a message buffer to the device. The message buffer
 * will be allocated by WiFiEngine using the DriverEnvironment_TX_Alloc()
 * call and will always be freed by this function.
 * While the message buffer can be queued externally to WiFiEngine it
 * must be sent to the device as soon as possible. Specifically, it
 * must not be queued due to power save states since this function is
 * used to send the wakup messages. This call _must_ succeed,
 * transmission failure at this level is a fatal error.
 * @param message (char *)Input message buffer (allocated with DriverEnvironment_TX_Alloc()
 * @param size (int) Length of the message buffer in bytes
 * @return (int) DRIVERENVIRONMENT_SUCCESS
 */
int            DriverEnvironment__HIC_Send(char* message, size_t size);

#define DriverEnvironment_HIC_Send(BUF, SIZE)		\
	({						\
           void *__p = (BUF);				\
           MTFREE(__p, 'T', 0); /* no poison here */	\
           DriverEnvironment__HIC_Send(__p, (SIZE));	\
	})



/* Hooks to enable/disable target device sleep (such as with a IOCTL in a driver) */

/*!
 * \brief Allow the device to go into deep sleep mode. 
 *
 * This will be called in 802.11 sleep mode before requesting
 * that the device enter deep sleep mode and should allow the
 * device to turn of all clocks.
 */
/* REMARK: this can be empty initially */
void           DriverEnvironment_enable_target_sleep(void);

/*!
 * \brief Disallow the device to go into deep sleep mode. 
 *
 * This will be called in deep sleep mode when waking up,
 * either because the device indicates that it wants to be
 * woken up or because the host wants to wake up the
 * device. The call should disallow the device to turn of
 * all clocks.
 */
/* REMARK: this can be empty initially */
void           DriverEnvironment_disable_target_sleep(void);

/*!
 * \brief Turn off the host-device interface.
 *
 * This will be called upon confirmation that the device has
 * entered deep sleep mode and should turn of the host-device
 * interface. For example, in the SDIO case it should turn off
 * the SDIO clock.
 */
/* REMARK: this can be empty initially */
void           DriverEnvironment_disable_target_interface(void);

/*!
 * \brief Perform power up housekeeping tasks
 * 
 * This will be called when the device has been woken up from power
 * save mode.  It can perform driver-specific tasks that need to be
 * taken care of in this case (such as starting to send queued packets).
 */
/* REMARK: this can be empty initially */
void           DriverEnvironment_handle_driver_wakeup(void);

/*! Generate (pseudo-)random data. Level of randomness is not
 *  specified, so it should not be used for cryptographic purposes.
 * @param data (void *) Output buffer.
 * @param len (size_t) Output buffer length.
 */
/* REMARK: currently only used for IBSS, so implementation can wait */
void DriverEnvironment_RandomData(void *data, size_t len);

/*!
 * \brief Register an opaque handle with WiFiEngine
 *
 * This function registers an opaque data object in WiFiEngine.
 * This is purely a utility function for the user of WiFiEngine
 * and is never called from within WiFiEngine (and can thus
 * be left unimplemented). 
 * @param hndl (void *) A pointer to the handle.
 */
void DriverEnvironment_Register_Handle(void *hndl);

/*!
 * \brief Retrieve an opaque handle from WiFiEngine
 *
 * This function retrieves an opaquedata object that was
 * previously registered with DriverEnvironment_Register_Handle().
 * @returns (void *) A pointer to the handle object.
 */
void *DriverEnvironment_Get_Handle(void);

/*!
 * Translate a byte-array from little endian byte order to the
 * native endianness for the current platform.
 * @param dst (char *) Source and destination buffer.
 * @param len (char *) Length of the input buffer.
 * return (int)
 */
/* REMARK: for a little endian machine this should be empty */
int DriverEnvironment_LittleEndian2Native(char *dst, size_t len);

#ifdef WITH_TIMESTAMPS
void DriverEnvironment_DeltaTimestamp(const char* str);
#endif


#ifdef C_LOGGING_WITH_TIMESTAMPS

/* Platform specific */
#define de_time_t struct timeval

/*!
 * Write current platform specific timestamp to *ts
 * @param tv (de_time_t *) Pointer to a data structure that will be written to.
 */
void DriverEnvironment_get_current_time(de_time_t *ts);
#endif

#include "de_timing.h"
#include "de_task.h"
#include "de_event.h"
#include "de_memory.h"
#include "de_trace.h"

/** @} */ /* End of driverenv_api group */

#include "log.h"

#endif /* DRIVERENV_H */
