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
 @File          ncsw_ext.h

 @Description   General NetCommSw Standard Definitions
*//***************************************************************************/

#ifndef __NCSW_EXT_H
#define __NCSW_EXT_H


#include "memcpy_ext.h"

#define WRITE_BLOCK                 IOMemSet32   /* include memcpy_ext.h */
#define COPY_BLOCK                  Mem2IOCpy32  /* include memcpy_ext.h */

#define PTR_TO_UINT(_ptr)           ((uintptr_t)(_ptr))
#define UINT_TO_PTR(_val)           ((void*)(uintptr_t)(_val))

#define PTR_MOVE(_ptr, _offset)     (void*)((uint8_t*)(_ptr) + (_offset))


#define WRITE_UINT8_UINT24(arg, data08, data24) \
    WRITE_UINT32(arg,((uint32_t)(data08)<<24)|((uint32_t)(data24)&0x00FFFFFF))
#define WRITE_UINT24_UINT8(arg, data24, data08) \
    WRITE_UINT32(arg,((uint32_t)(data24)<< 8)|((uint32_t)(data08)&0x000000FF))

/* Little-Endian access macros */

#define WRITE_UINT16_LE(arg, data) \
        WRITE_UINT16((arg), SwapUint16(data))

#define WRITE_UINT32_LE(arg, data) \
        WRITE_UINT32((arg), SwapUint32(data))

#define WRITE_UINT64_LE(arg, data) \
        WRITE_UINT64((arg), SwapUint64(data))

#define GET_UINT16_LE(arg) \
        SwapUint16(GET_UINT16(arg))

#define GET_UINT32_LE(arg) \
        SwapUint32(GET_UINT32(arg))

#define GET_UINT64_LE(arg) \
        SwapUint64(GET_UINT64(arg))

/* Write and Read again macros */
#define WRITE_UINT_SYNC(size, arg, data)    \
    do {                                    \
        WRITE_UINT##size((arg), (data));    \
        CORE_MemoryBarrier();               \
    } while (0)

#define WRITE_UINT8_SYNC(arg, data)     WRITE_UINT_SYNC(8, (arg), (data))

#define WRITE_UINT16_SYNC(arg, data)    WRITE_UINT_SYNC(16, (arg), (data))
#define WRITE_UINT32_SYNC(arg, data)    WRITE_UINT_SYNC(32, (arg), (data))

#define MAKE_UINT64(high32, low32)      (((uint64_t)high32 << 32) | (low32))


/*----------------------*/
/* Miscellaneous macros */
/*----------------------*/

#define UNUSED(_x)		((void)(_x))

#define KILOBYTE            0x400UL                 /* 1024 */
#define MEGABYTE            (KILOBYTE * KILOBYTE)   /* 1024*1024 */
#define GIGABYTE            ((uint64_t)(KILOBYTE * MEGABYTE))   /* 1024*1024*1024 */
#define TERABYTE            ((uint64_t)(KILOBYTE * GIGABYTE))   /* 1024*1024*1024*1024 */

#ifndef NO_IRQ
#define NO_IRQ		(0)
#endif
#define NCSW_MASTER_ID      (0)

/* Macro for checking if a number is a power of 2 */
#define POWER_OF_2(n)   (!((n) & ((n)-1)))

/* Macro for calculating log of base 2 */
#define LOG2(num, log2Num)      \
    do                          \
    {                           \
        uint64_t tmp = (num);   \
        log2Num = 0;            \
        while (tmp > 1)         \
        {                       \
            log2Num++;          \
            tmp >>= 1;          \
        }                       \
    } while (0)

#define NEXT_POWER_OF_2(_num, _nextPow) \
do                                      \
{                                       \
    if (POWER_OF_2(_num))               \
        _nextPow = (_num);              \
    else                                \
    {                                   \
        uint64_t tmp = (_num);          \
        _nextPow = 1;                   \
        while (tmp)                     \
        {                               \
            _nextPow <<= 1;             \
            tmp >>= 1;                  \
        }                               \
    }                                   \
} while (0)

/* Ceiling division - not the fastest way, but safer in terms of overflow */
#define DIV_CEIL(x,y) (((x)/(y)) + (((((x)/(y))*(y)) == (x)) ? 0 : 1))

/* Round up a number to be a multiple of a second number */
#define ROUND_UP(x,y)   ((((x) + (y) - 1) / (y)) * (y))

/* Timing macro for converting usec units to number of ticks.   */
/* (number of usec *  clock_Hz) / 1,000,000) - since            */
/* clk is in MHz units, no division needed.                     */
#define USEC_TO_CLK(usec,clk)       ((usec) * (clk))
#define CYCLES_TO_USEC(cycles,clk)  ((cycles) / (clk))

/* Timing macros for converting between nsec units and number of clocks. */
#define NSEC_TO_CLK(nsec,clk)       DIV_CEIL(((nsec) * (clk)), 1000)
#define CYCLES_TO_NSEC(cycles,clk)  (((cycles) * 1000) / (clk))

/* Timing macros for converting between psec units and number of clocks. */
#define PSEC_TO_CLK(psec,clk)       DIV_CEIL(((psec) * (clk)), 1000000)
#define CYCLES_TO_PSEC(cycles,clk)  (((cycles) * 1000000) / (clk))

/* Min, Max macros */
#define IN_RANGE(min,val,max) ((min)<=(val) && (val)<=(max))

#define ABS(a)  ((a<0)?(a*-1):a)

#if !(defined(ARRAY_SIZE))
#define ARRAY_SIZE(arr)   (sizeof(arr) / sizeof((arr)[0]))
#endif /* !defined(ARRAY_SIZE) */


/* possible alignments */
#define HALF_WORD_ALIGNMENT     2
#define WORD_ALIGNMENT          4
#define DOUBLE_WORD_ALIGNMENT   8
#define BURST_ALIGNMENT         32

#define HALF_WORD_ALIGNED       0x00000001
#define WORD_ALIGNED            0x00000003
#define DOUBLE_WORD_ALIGNED     0x00000007
#define BURST_ALIGNED           0x0000001f
#ifndef IS_ALIGNED
#define IS_ALIGNED(n,align)     (!((uint32_t)(n) & (align - 1)))
#endif /* IS_ALIGNED */


#define LAST_BUF        1
#define FIRST_BUF       2
#define SINGLE_BUF      (LAST_BUF | FIRST_BUF)
#define MIDDLE_BUF      4

#define ARRAY_END       -1

#define ILLEGAL_BASE    (~0)

#define BUF_POSITION(first, last)   state[(!!(last))<<1 | !!(first)]
#define DECLARE_POSITION static uint8_t state[4] = { (uint8_t)MIDDLE_BUF, (uint8_t)FIRST_BUF, (uint8_t)LAST_BUF, (uint8_t)SINGLE_BUF };


/**************************************************************************//**
 @Description   Timers operation mode
*//***************************************************************************/
typedef enum e_TimerMode
{
    e_TIMER_MODE_INVALID = 0,
    e_TIMER_MODE_FREE_RUN,    /**< Free run - counter continues to increase
                                   after reaching the reference value. */
    e_TIMER_MODE_PERIODIC,    /**< Periodic - counter restarts counting from 0
                                   after reaching the reference value. */
    e_TIMER_MODE_SINGLE       /**< Single (one-shot) - counter stops counting
                                   after reaching the reference value. */
} e_TimerMode;


/**************************************************************************//**
 @Description   Enumeration (bit flags) of communication modes (Transmit,
                receive or both).
*//***************************************************************************/
typedef enum e_CommMode
{
    e_COMM_MODE_NONE        = 0,    /**< No transmit/receive communication */
    e_COMM_MODE_RX          = 1,    /**< Only receive communication */
    e_COMM_MODE_TX          = 2,    /**< Only transmit communication */
    e_COMM_MODE_RX_AND_TX   = 3     /**< Both transmit and receive communication */
} e_CommMode;

/**************************************************************************//**
 @Description   General Diagnostic Mode
*//***************************************************************************/
typedef enum e_DiagMode
{
    e_DIAG_MODE_NONE = 0,       /**< Normal operation; no diagnostic mode */
    e_DIAG_MODE_CTRL_LOOPBACK,  /**< Loopback in the controller */
    e_DIAG_MODE_CHIP_LOOPBACK,  /**< Loopback in the chip but not in the
                                     controller; e.g. IO-pins, SerDes, etc. */
    e_DIAG_MODE_PHY_LOOPBACK,   /**< Loopback in the external PHY */
    e_DIAG_MODE_EXT_LOOPBACK,   /**< Loopback in the external line (beyond the PHY) */
    e_DIAG_MODE_CTRL_ECHO,      /**< Echo incoming data by the controller */
    e_DIAG_MODE_PHY_ECHO        /**< Echo incoming data by the PHY */
} e_DiagMode;

/**************************************************************************//**
 @Description   Possible RxStore callback responses.
*//***************************************************************************/
typedef enum e_RxStoreResponse
{
      e_RX_STORE_RESPONSE_PAUSE     /**< Pause invoking callback with received data;
                                         in polling mode, start again invoking callback
                                         only next time user invokes the receive routine;
                                         in interrupt mode, start again invoking callback
                                         only next time a receive event triggers an interrupt;
                                         in all cases, received data that are pending are not
                                         lost, rather, their processing is temporarily deferred;
                                         in all cases, received data are processed in the order
                                         in which they were received. */
    , e_RX_STORE_RESPONSE_CONTINUE  /**< Continue invoking callback with received data. */
} e_RxStoreResponse;


/**************************************************************************//**
 @Description   General Handle
*//***************************************************************************/
typedef void *      t_Handle;   /**< handle, used as object's descriptor */

/**************************************************************************//**
 @Description   MUTEX type
*//***************************************************************************/
typedef uint32_t    t_Mutex;

/**************************************************************************//**
 @Description   Error Code.

                The high word of the error code is the code of the software
                module (driver). The low word is the error type (e_ErrorType).
                To get the values from the error code, use GET_ERROR_TYPE()
                and GET_ERROR_MODULE().
*//***************************************************************************/
typedef uint32_t    t_Error;

/**************************************************************************//**
 @Description   General prototype of interrupt service routine (ISR).

 @Param[in]     handle - Optional handle of the module handling the interrupt.

 @Return        None
 *//***************************************************************************/
typedef void (t_Isr)(t_Handle handle);

/**************************************************************************//**
 @Anchor        mem_attr

 @Collection    Memory Attributes

                Various attributes of memory partitions. These values may be
                or'ed together to create a mask of all memory attributes.
 @{
*//***************************************************************************/
#define MEMORY_ATTR_CACHEABLE           0x00000001
                                        /**< Memory is cacheable */
#define MEMORY_ATTR_QE_2ND_BUS_ACCESS   0x00000002
                                        /**< Memory can be accessed by QUICC Engine
                                             through its secondary bus interface */

/* @} */


/**************************************************************************//**
 @Function      t_GetBufFunction

 @Description   User callback function called by driver to get data buffer.

                User provides this function. Driver invokes it.

 @Param[in]     h_BufferPool        - A handle to buffer pool manager
 @Param[out]    p_BufContextHandle  - Returns the user's private context that
                                      should be associated with the buffer

 @Return        Pointer to data buffer, NULL if error
 *//***************************************************************************/
typedef uint8_t * (t_GetBufFunction)(t_Handle   h_BufferPool,
                                     t_Handle   *p_BufContextHandle);

/**************************************************************************//**
 @Function      t_PutBufFunction

 @Description   User callback function called by driver to return data buffer.

                User provides this function. Driver invokes it.

 @Param[in]     h_BufferPool    - A handle to buffer pool manager
 @Param[in]     p_Buffer        - A pointer to buffer to return
 @Param[in]     h_BufContext    - The user's private context associated with
                                  the returned buffer

 @Return        E_OK on success; Error code otherwise
 *//***************************************************************************/
typedef t_Error (t_PutBufFunction)(t_Handle h_BufferPool,
                                   uint8_t  *p_Buffer,
                                   t_Handle h_BufContext);

/**************************************************************************//**
 @Function      t_PhysToVirt

 @Description   Translates a physical address to the matching virtual address.

 @Param[in]     addr - The physical address to translate.

 @Return        Virtual address.
*//***************************************************************************/
typedef void * t_PhysToVirt(physAddress_t addr);

/**************************************************************************//**
 @Function      t_VirtToPhys

 @Description   Translates a virtual address to the matching physical address.

 @Param[in]     addr - The virtual address to translate.

 @Return        Physical address.
*//***************************************************************************/
typedef physAddress_t t_VirtToPhys(void *addr);

/**************************************************************************//**
 @Description   Buffer Pool Information Structure.
*//***************************************************************************/
typedef struct t_BufferPoolInfo
{
    t_Handle            h_BufferPool;   /**< A handle to the buffer pool manager */
    t_GetBufFunction    *f_GetBuf;      /**< User callback to get a free buffer */
    t_PutBufFunction    *f_PutBuf;      /**< User callback to return a buffer */
    uint16_t            bufferSize;     /**< Buffer size (in bytes) */

    t_PhysToVirt        *f_PhysToVirt;  /**< User callback to translate pool buffers
                                             physical addresses to virtual addresses  */
    t_VirtToPhys        *f_VirtToPhys;  /**< User callback to translate pool buffers
                                             virtual addresses to physical addresses */
} t_BufferPoolInfo;


/**************************************************************************//**
 @Description   User callback function called by driver when transmit completed.

                User provides this function. Driver invokes it.

 @Param[in]     h_App           - Application's handle, as was provided to the
                                  driver by the user
 @Param[in]     queueId         - Transmit queue ID
 @Param[in]     p_Data          - Pointer to the data buffer
 @Param[in]     h_BufContext    - The user's private context associated with
                                  the given data buffer
 @Param[in]     status          - Transmit status and errors
 @Param[in]     flags           - Driver-dependent information
 *//***************************************************************************/
typedef void (t_TxConfFunction)(t_Handle    h_App,
                                uint32_t    queueId,
                                uint8_t     *p_Data,
                                t_Handle    h_BufContext,
                                uint16_t    status,
                                uint32_t    flags);

/**************************************************************************//**
 @Description   User callback function called by driver with receive data.

                User provides this function. Driver invokes it.

 @Param[in]     h_App           - Application's handle, as was provided to the
                                  driver by the user
 @Param[in]     queueId         - Receive queue ID
 @Param[in]     p_Data          - Pointer to the buffer with received data
 @Param[in]     h_BufContext    - The user's private context associated with
                                  the given data buffer
 @Param[in]     length          - Length of received data
 @Param[in]     status          - Receive status and errors
 @Param[in]     position        - Position of buffer in frame
 @Param[in]     flags           - Driver-dependent information

 @Retval        e_RX_STORE_RESPONSE_CONTINUE - order the driver to continue Rx
                                               operation for all ready data.
 @Retval        e_RX_STORE_RESPONSE_PAUSE    - order the driver to stop Rx operation.
 *//***************************************************************************/
typedef e_RxStoreResponse (t_RxStoreFunction)(t_Handle  h_App,
                                              uint32_t  queueId,
                                              uint8_t   *p_Data,
                                              t_Handle  h_BufContext,
                                              uint32_t  length,
                                              uint16_t  status,
                                              uint8_t   position,
                                              uint32_t  flags);


#endif /* __NCSW_EXT_H */
