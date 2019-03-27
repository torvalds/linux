/* Copyright (c) 2008-2012 Freescale Semiconductor, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/**************************************************************************//**
 @File          error_ext.h

 @Description   Error definitions.
*//***************************************************************************/

#ifndef __ERROR_EXT_H
#define __ERROR_EXT_H

#if defined(NCSW_FREEBSD)
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/pcpu.h>
#endif

#include "std_ext.h"
#include "xx_ext.h"
#include "core_ext.h"




/**************************************************************************//**
 @Group         gen_id  General Drivers Utilities

 @Description   External routines.

 @{
*//***************************************************************************/

/**************************************************************************//**
 @Group         gen_error_id  Errors, Events and Debug

 @Description   External routines.

 @{
*//***************************************************************************/

/******************************************************************************
The scheme below provides the bits description for error codes:

 0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
|       Reserved (should be zero)      |              Module ID               |

 16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31
|                               Error Type                                    |
******************************************************************************/

#define ERROR_CODE(_err)            ((((uint32_t)_err) & 0x0000FFFF) | __ERR_MODULE__)

#define GET_ERROR_TYPE(_errcode)    ((_errcode) & 0x0000FFFF)
                                /**< Extract module code from error code (#t_Error) */

#define GET_ERROR_MODULE(_errcode)  ((_errcode) & 0x00FF0000)
                                /**< Extract error type (#e_ErrorType) from
                                     error code (#t_Error) */


/**************************************************************************//**
 @Description    Error Type Enumeration
*//***************************************************************************/
typedef enum e_ErrorType    /*   Comments / Associated Message Strings                      */
{                           /* ------------------------------------------------------------ */
    E_OK = 0                /*   Never use "RETURN_ERROR" with E_OK; Use "return E_OK;"     */
    ,E_WRITE_FAILED = EIO   /**< Write access failed on memory/device.                      */
                            /*   String: none, or device name.                              */
    ,E_NO_DEVICE = ENXIO    /**< The associated device is not initialized.                  */
                            /*   String: none.                                              */
    ,E_NOT_AVAILABLE = EAGAIN
                            /**< Resource is unavailable.                                   */
                            /*   String: none, unless the operation is not the main goal
                                 of the function (in this case add resource description).   */
    ,E_NO_MEMORY = ENOMEM   /**< External memory allocation failed.                         */
                            /*   String: description of item for which allocation failed.   */
    ,E_INVALID_ADDRESS = EFAULT
                            /**< Invalid address.                                           */
                            /*   String: description of the specific violation.             */
    ,E_BUSY = EBUSY         /**< Resource or module is busy.                                */
                            /*   String: none, unless the operation is not the main goal
                                 of the function (in this case add resource description).   */
    ,E_ALREADY_EXISTS = EEXIST
                            /**< Requested resource or item already exists.                 */
                            /*   Use when resource duplication or sharing are not allowed.
                                 String: none, unless the operation is not the main goal
                                 of the function (in this case add item description).       */
    ,E_INVALID_OPERATION = ENODEV
                            /**< The operation/command is invalid (unrecognized).           */
                            /*   String: none.                                              */
    ,E_INVALID_VALUE = EDOM /**< Invalid value.                                             */
                            /*   Use for non-enumeration parameters, and
                                 only when other error types are not suitable.
                                 String: parameter description + "(should be <attribute>)",
                                 e.g: "Maximum Rx buffer length (should be divisible by 8)",
                                      "Channel number (should be even)".                    */
    ,E_NOT_IN_RANGE = ERANGE/**< Parameter value is out of range.                           */
                            /*   Don't use this error for enumeration parameters.
                                 String: parameter description + "(should be %d-%d)",
                                 e.g: "Number of pad characters (should be 0-15)".          */
    ,E_NOT_SUPPORTED = ENOSYS
                            /**< The function is not supported or not implemented.          */
                            /*   String: none.                                              */
    ,E_INVALID_STATE        /**< The operation is not allowed in current module state.      */
                            /*   String: none.                                              */
    ,E_INVALID_HANDLE       /**< Invalid handle of module or object.                        */
                            /*   String: none, unless the function takes in more than one
                                 handle (in this case add the handle description)           */
    ,E_INVALID_ID           /**< Invalid module ID (usually enumeration or index).          */
                            /*   String: none, unless the function takes in more than one
                                 ID (in this case add the ID description)                   */
    ,E_NULL_POINTER         /**< Unexpected NULL pointer.                                   */
                            /*   String: pointer description.                               */
    ,E_INVALID_SELECTION    /**< Invalid selection or mode.                                 */
                            /*   Use for enumeration values, only when other error types
                                 are not suitable.
                                 String: parameter description.                             */
    ,E_INVALID_COMM_MODE    /**< Invalid communication mode.                                */
                            /*   String: none, unless the function takes in more than one
                                 communication mode indications (in this case add
                                 parameter description).                                    */
    ,E_INVALID_MEMORY_TYPE  /**< Invalid memory type.                                       */
                            /*   String: none, unless the function takes in more than one
                                 memory types (in this case add memory description,
                                 e.g: "Data memory", "Buffer descriptors memory").          */
    ,E_INVALID_CLOCK        /**< Invalid clock.                                             */
                            /*   String: none, unless the function takes in more than one
                                 clocks (in this case add clock description,
                                 e.g: "Rx clock", "Tx clock").                              */
    ,E_CONFLICT             /**< Some setting conflicts with another setting.               */
                            /*   String: description of the conflicting settings.           */
    ,E_NOT_ALIGNED          /**< Non-aligned address.                                       */
                            /*   String: parameter description + "(should be %d-bytes aligned)",
                                 e.g: "Rx data buffer (should be 32-bytes aligned)".        */
    ,E_NOT_FOUND            /**< Requested resource or item was not found.                  */
                            /*   Use only when the resource/item is uniquely identified.
                                 String: none, unless the operation is not the main goal
                                 of the function (in this case add item description).       */
    ,E_FULL                 /**< Resource is full.                                          */
                            /*   String: none, unless the operation is not the main goal
                                 of the function (in this case add resource description).   */
    ,E_EMPTY                /**< Resource is empty.                                         */
                            /*   String: none, unless the operation is not the main goal
                                 of the function (in this case add resource description).   */
    ,E_ALREADY_FREE         /**< Specified resource or item is already free or deleted.     */
                            /*   String: none, unless the operation is not the main goal
                                 of the function (in this case add item description).       */
    ,E_READ_FAILED          /**< Read access failed on memory/device.                       */
                            /*   String: none, or device name.                              */
    ,E_INVALID_FRAME        /**< Invalid frame object (NULL handle or missing buffers).     */
                            /*   String: none.                                              */
    ,E_SEND_FAILED          /**< Send operation failed on device.                           */
                            /*   String: none, or device name.                              */
    ,E_RECEIVE_FAILED       /**< Receive operation failed on device.                        */
                            /*   String: none, or device name.                              */
    ,E_TIMEOUT/* = ETIMEDOUT*/  /**< The operation timed out.                                   */
                            /*   String: none.                                              */

    ,E_DUMMY_LAST           /* NEVER USED */

} e_ErrorType;

/**************************************************************************//**
 @Description    Event Type Enumeration
*//***************************************************************************/
typedef enum e_Event        /*   Comments / Associated Flags and Message Strings            */
{                           /* ------------------------------------------------------------ */
    EV_NO_EVENT = 0         /**< No event; Never used.                                      */

    ,EV_RX_DISCARD          /**< Received packet discarded (by the driver, and only for
                                 complete packets);
                                 Flags: error flags in case of error, zero otherwise.       */
                            /*   String: reason for discard, e.g: "Error in frame",
                                 "Disordered frame", "Incomplete frame", "No frame object". */
    ,EV_RX_ERROR            /**< Receive error (by hardware/firmware);
                                 Flags: usually status flags from the buffer descriptor.    */
                            /*   String: none.                                              */
    ,EV_TX_ERROR            /**< Transmit error (by hardware/firmware);
                                 Flags: usually status flags from the buffer descriptor.    */
                            /*   String: none.                                              */
    ,EV_NO_BUFFERS          /**< System ran out of buffer objects;
                                 Flags: zero.                                               */
                            /*   String: none.                                              */
    ,EV_NO_MB_FRAMES        /**< System ran out of multi-buffer frame objects;
                                 Flags: zero.                                               */
                            /*   String: none.                                              */
    ,EV_NO_SB_FRAMES        /**< System ran out of single-buffer frame objects;
                                 Flags: zero.                                               */
                            /*   String: none.                                              */
    ,EV_TX_QUEUE_FULL       /**< Transmit queue is full;
                                 Flags: zero.                                               */
                            /*   String: none.                                              */
    ,EV_RX_QUEUE_FULL       /**< Receive queue is full;
                                 Flags: zero.                                               */
                            /*   String: none.                                              */
    ,EV_INTR_QUEUE_FULL     /**< Interrupt queue overflow;
                                 Flags: zero.                                               */
                            /*   String: none.                                              */
    ,EV_NO_DATA_BUFFER      /**< Data buffer allocation (from higher layer) failed;
                                 Flags: zero.                                               */
                            /*   String: none.                                              */
    ,EV_OBJ_POOL_EMPTY      /**< Objects pool is empty;
                                 Flags: zero.                                               */
                            /*   String: object description (name).                         */
    ,EV_BUS_ERROR           /**< Illegal access on bus;
                                 Flags: the address (if available) or bus identifier        */
                            /*   String: bus/address/module description.                    */
    ,EV_PTP_TXTS_QUEUE_FULL /**< PTP Tx timestamps queue is full;
                                 Flags: zero.                                               */
                            /*   String: none.                                              */
    ,EV_PTP_RXTS_QUEUE_FULL /**< PTP Rx timestamps queue is full;
                                 Flags: zero.                                               */
                            /*   String: none.                                              */
    ,EV_DUMMY_LAST

} e_Event;


/**************************************************************************//**
 @Collection    Debug Levels for Errors and Events

                The level description refers to errors only.
                For events, classification is done by the user.

                The TRACE, INFO and WARNING levels are allowed only when using
                the DBG macro, and are not allowed when using the error macros
                (RETURN_ERROR or REPORT_ERROR).
 @{
*//***************************************************************************/
#define REPORT_LEVEL_CRITICAL   1       /**< Crasher: Incorrect flow, NULL pointers/handles. */
#define REPORT_LEVEL_MAJOR      2       /**< Cannot proceed: Invalid operation, parameters or
                                             configuration. */
#define REPORT_LEVEL_MINOR      3       /**< Recoverable problem: a repeating call with the same
                                             parameters may be successful. */
#define REPORT_LEVEL_WARNING    4       /**< Something is not exactly right, yet it is not an error. */
#define REPORT_LEVEL_INFO       5       /**< Messages which may be of interest to user/programmer. */
#define REPORT_LEVEL_TRACE      6       /**< Program flow messages. */

#define EVENT_DISABLED          0xFF    /**< Disabled event (not reported at all) */

/* @} */



#define NO_MSG      ("")

#ifndef DEBUG_GLOBAL_LEVEL
#define DEBUG_GLOBAL_LEVEL  REPORT_LEVEL_WARNING
#endif /* DEBUG_GLOBAL_LEVEL */

#ifndef ERROR_GLOBAL_LEVEL
#define ERROR_GLOBAL_LEVEL  DEBUG_GLOBAL_LEVEL
#endif /* ERROR_GLOBAL_LEVEL */

#ifndef EVENT_GLOBAL_LEVEL
#define EVENT_GLOBAL_LEVEL  REPORT_LEVEL_MINOR
#endif /* EVENT_GLOBAL_LEVEL */

#ifdef EVENT_LOCAL_LEVEL
#define EVENT_DYNAMIC_LEVEL EVENT_LOCAL_LEVEL
#else
#define EVENT_DYNAMIC_LEVEL EVENT_GLOBAL_LEVEL
#endif /* EVENT_LOCAL_LEVEL */


#ifndef DEBUG_DYNAMIC_LEVEL
#define DEBUG_USING_STATIC_LEVEL

#ifdef DEBUG_STATIC_LEVEL
#define DEBUG_DYNAMIC_LEVEL DEBUG_STATIC_LEVEL
#else
#define DEBUG_DYNAMIC_LEVEL DEBUG_GLOBAL_LEVEL
#endif /* DEBUG_STATIC_LEVEL */

#else /* DEBUG_DYNAMIC_LEVEL */
#ifdef DEBUG_STATIC_LEVEL
#error "Please use either DEBUG_STATIC_LEVEL or DEBUG_DYNAMIC_LEVEL (not both)"
#else
int DEBUG_DYNAMIC_LEVEL = DEBUG_GLOBAL_LEVEL;
#endif /* DEBUG_STATIC_LEVEL */
#endif /* !DEBUG_DYNAMIC_LEVEL */


#ifndef ERROR_DYNAMIC_LEVEL

#ifdef ERROR_STATIC_LEVEL
#define ERROR_DYNAMIC_LEVEL ERROR_STATIC_LEVEL
#else
#define ERROR_DYNAMIC_LEVEL ERROR_GLOBAL_LEVEL
#endif /* ERROR_STATIC_LEVEL */

#else /* ERROR_DYNAMIC_LEVEL */
#ifdef ERROR_STATIC_LEVEL
#error "Please use either ERROR_STATIC_LEVEL or ERROR_DYNAMIC_LEVEL (not both)"
#else
int ERROR_DYNAMIC_LEVEL = ERROR_GLOBAL_LEVEL;
#endif /* ERROR_STATIC_LEVEL */
#endif /* !ERROR_DYNAMIC_LEVEL */

#define PRINT_FORMAT        "[CPU%02d, %s:%d %s]"
#define PRINT_FMT_PARAMS    PCPU_GET(cpuid), __FILE__, __LINE__, __FUNCTION__

#if (!(defined(DEBUG_ERRORS)) || (DEBUG_ERRORS == 0))
/* No debug/error/event messages at all */
#define DBG(_level, _vmsg)

#define REPORT_ERROR(_level, _err, _vmsg)

#define RETURN_ERROR(_level, _err, _vmsg) \
        return ERROR_CODE(_err)

#if (REPORT_EVENTS > 0)

#define REPORT_EVENT(_ev, _appId, _flg, _vmsg) \
    do { \
        if (_ev##_LEVEL <= EVENT_DYNAMIC_LEVEL) { \
            XX_EventById((uint32_t)(_ev), (t_Handle)(_appId), (uint16_t)(_flg), NO_MSG); \
        } \
    } while (0)

#else

#define REPORT_EVENT(_ev, _appId, _flg, _vmsg)

#endif /* (REPORT_EVENTS > 0) */


#else /* DEBUG_ERRORS > 0 */

extern const char *dbgLevelStrings[];
#if (REPORT_EVENTS > 0)
extern const char *eventStrings[];
#endif /* (REPORT_EVENTS > 0) */

char * ErrTypeStrings (e_ErrorType err);


#if ((defined(DEBUG_USING_STATIC_LEVEL)) && (DEBUG_DYNAMIC_LEVEL < REPORT_LEVEL_WARNING))
/* No need for DBG macro - debug level is higher anyway */
#define DBG(_level, _vmsg)
#else
#define DBG(_level, _vmsg) \
    do { \
        if (REPORT_LEVEL_##_level <= DEBUG_DYNAMIC_LEVEL) { \
            XX_Print("> %s (%s) " PRINT_FORMAT ": ", \
                     dbgLevelStrings[REPORT_LEVEL_##_level - 1], \
                     __STRING(__ERR_MODULE__), \
                     PRINT_FMT_PARAMS); \
            XX_Print _vmsg; \
            XX_Print("\r\n"); \
        } \
    } while (0)
#endif /* (defined(DEBUG_USING_STATIC_LEVEL) && (DEBUG_DYNAMIC_LEVEL < WARNING)) */


#define REPORT_ERROR(_level, _err, _vmsg) \
    do { \
        if (REPORT_LEVEL_##_level <= ERROR_DYNAMIC_LEVEL) { \
            XX_Print("! %s %s Error " PRINT_FORMAT ": %s; ", \
                     dbgLevelStrings[REPORT_LEVEL_##_level - 1], \
                     __STRING(__ERR_MODULE__), \
                     PRINT_FMT_PARAMS, \
                     ErrTypeStrings((e_ErrorType)GET_ERROR_TYPE(_err))); \
            XX_Print _vmsg; \
            XX_Print("\r\n"); \
        } \
    } while (0)


#define RETURN_ERROR(_level, _err, _vmsg) \
    do { \
        REPORT_ERROR(_level, (_err), _vmsg); \
        return ERROR_CODE(_err); \
    } while (0)


#if (REPORT_EVENTS > 0)

#define REPORT_EVENT(_ev, _appId, _flg, _vmsg) \
    do { \
        if (_ev##_LEVEL <= EVENT_DYNAMIC_LEVEL) { \
            XX_Print("~ %s %s Event " PRINT_FORMAT ": %s (flags: 0x%04x); ", \
                     dbgLevelStrings[_ev##_LEVEL - 1], \
                     __STRING(__ERR_MODULE__), \
                     PRINT_FMT_PARAMS, \
                     eventStrings[((_ev) - EV_NO_EVENT - 1)], \
                     (uint16_t)(_flg)); \
            XX_Print _vmsg; \
            XX_Print("\r\n"); \
            XX_EventById((uint32_t)(_ev), (t_Handle)(_appId), (uint16_t)(_flg), NO_MSG); \
        } \
    } while (0)

#else /* not REPORT_EVENTS */

#define REPORT_EVENT(_ev, _appId, _flg, _vmsg)

#endif /* (REPORT_EVENTS > 0) */

#endif /* (DEBUG_ERRORS > 0) */


/**************************************************************************//**
 @Function      ASSERT_COND

 @Description   Assertion macro.

 @Param[in]     _cond - The condition being checked, in positive form;
                        Failure of the condition triggers the assert.
*//***************************************************************************/
#ifdef DISABLE_ASSERTIONS
#define ASSERT_COND(_cond)
#else
#define ASSERT_COND(_cond) \
    do { \
        if (!(_cond)) { \
            XX_Print("*** ASSERT_COND failed " PRINT_FORMAT "\r\n", \
                    PRINT_FMT_PARAMS); \
            XX_Exit(1); \
        } \
    } while (0)
#endif /* DISABLE_ASSERTIONS */


#ifdef DISABLE_INIT_PARAMETERS_CHECK

#define CHECK_INIT_PARAMETERS(handle, f_check)
#define CHECK_INIT_PARAMETERS_RETURN_VALUE(handle, f_check, retval)

#else

#define CHECK_INIT_PARAMETERS(handle, f_check) \
    do { \
        t_Error err = f_check(handle); \
        if (err != E_OK) { \
            RETURN_ERROR(MAJOR, err, NO_MSG); \
        } \
    } while (0)

#define CHECK_INIT_PARAMETERS_RETURN_VALUE(handle, f_check, retval) \
    do { \
        t_Error err = f_check(handle); \
        if (err != E_OK) { \
            REPORT_ERROR(MAJOR, err, NO_MSG); \
            return (retval); \
        } \
    } while (0)

#endif /* DISABLE_INIT_PARAMETERS_CHECK */

#ifdef DISABLE_SANITY_CHECKS

#define SANITY_CHECK_RETURN_ERROR(_cond, _err)
#define SANITY_CHECK_RETURN_VALUE(_cond, _err, retval)
#define SANITY_CHECK_RETURN(_cond, _err)
#define SANITY_CHECK_EXIT(_cond, _err)

#else /* DISABLE_SANITY_CHECKS */

#define SANITY_CHECK_RETURN_ERROR(_cond, _err) \
    do { \
        if (!(_cond)) { \
            RETURN_ERROR(CRITICAL, (_err), NO_MSG); \
        } \
    } while (0)

#define SANITY_CHECK_RETURN_VALUE(_cond, _err, retval) \
    do { \
        if (!(_cond)) { \
            REPORT_ERROR(CRITICAL, (_err), NO_MSG); \
            return (retval); \
        } \
    } while (0)

#define SANITY_CHECK_RETURN(_cond, _err) \
    do { \
        if (!(_cond)) { \
            REPORT_ERROR(CRITICAL, (_err), NO_MSG); \
            return; \
        } \
    } while (0)

#define SANITY_CHECK_EXIT(_cond, _err) \
    do { \
        if (!(_cond)) { \
            REPORT_ERROR(CRITICAL, (_err), NO_MSG); \
            XX_Exit(1); \
        } \
    } while (0)

#endif /* DISABLE_SANITY_CHECKS */

/** @} */ /* end of Debug/error Utils group */

/** @} */ /* end of General Utils group */

#endif /* __ERROR_EXT_H */


