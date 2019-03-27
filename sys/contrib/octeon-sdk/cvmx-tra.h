/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/








/**
 * @file
 *
 * Interface to the Trace buffer hardware.
 *
 * WRITING THE TRACE BUFFER
 *
 * When the trace is enabled, commands are traced continuously (wrapping) or until the buffer is filled once
 * (no wrapping).  Additionally and independent of wrapping, tracing can be temporarily enabled and disabled
 * by the tracing triggers.  All XMC commands can be traced except for IDLE and IOBRSP.  The subset of XMC
 * commands that are traced is determined by the filter and the two triggers, each of which is comprised of
 * masks for command, sid, did, and address).  If triggers are disabled, then only those commands matching
 * the filter are traced.  If triggers are enabled, then only those commands matching the filter, the start
 * trigger, or the stop trigger are traced during the time between a start trigger and a stop trigger.
 *
 * For a given command, its XMC data is written immediately to the buffer.  If the command has XMD data,
 * then that data comes in-order at some later time.  The XMD data is accumulated across all valid
 * XMD cycles and written to the buffer or to a shallow fifo.  Data from the fifo is written to the buffer
 * as soon as it gets access to write the buffer (i.e. the buffer is not currently being written with XMC
 * data).  If the fifo overflows, it simply overwrites itself and the previous XMD data is lost.
 *
 *
 * READING THE TRACE BUFFER
 *
 * Each entry of the trace buffer is read by a CSR read command.  The trace buffer services each read in order,
 * as soon as it has access to the (single-ported) trace buffer.
 *
 * On Octeon2, each entry of the trace buffer is read by two CSR memory read operations.  The first read accesses
 * bits 63:0 of the buffer entry, and the second read accesses bits 68:64 of the buffer entry. The trace buffer
 * services each read in order, as soon as it has access to the (single-ported) trace buffer.  Buffer's read pointer
 * increments after two CSR memory read operations.
 *
 *
 * OVERFLOW, UNDERFLOW AND THRESHOLD EVENTS
 *
 * The trace buffer maintains a write pointer and a read pointer and detects both the overflow and underflow
 * conditions.  Each time a new trace is enabled, both pointers are reset to entry 0.  Normally, each write
 * (traced event) increments the write pointer and each read increments the read pointer.  During the overflow
 * condition, writing (tracing) is disabled.  Tracing will continue as soon as the overflow condition is
 * resolved.  The first entry that is written immediately following the overflow condition may be marked to
 * indicate that a tracing discontinuity has occurred before this entry.  During the underflow condition,
 * reading does not increment the read pointer and the read data is marked to indicate that no read data is
 * available.
 *
 * The full threshold events are defined to signal an interrupt a certain levels of "fullness" (1/2, 3/4, 4/4).
 * "fullness" is defined as the relative distance between the write and read pointers (i.e. not defined as the
 * absolute distance between the write pointer and entry 0).  When enabled, the full threshold event occurs
 * every time the desired level of "fullness" is achieved.
 *
 *
 * Trace buffer entry format
 * @verbatim
 *       6                   5                   4                   3                   2                   1                   0
 * 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          |       0       | src id  |   0   | DWB   | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          |       0       | src id  |   0   | PL2   | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          |       0       | src id  |   0   | PSL1  | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          |       0       | src id  |   0   | LDD   | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          |       0       | src id  |   0   | LDI   | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          |       0       | src id  |   0   | LDT   | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          | * or 16B mask | src id  |   0   | STC   | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          | * or 16B mask | src id  |   0   | STF   | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          | * or 16B mask | src id  |   0   | STP   | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          | * or 16B mask | src id  |   0   | STT   | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:0]                                |    0    | src id| dest id |IOBLD8 | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:1]                              |     0     | src id| dest id |IOBLD16| diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:2]                            |      0      | src id| dest id |IOBLD32| diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          |       0       | src id| dest id |IOBLD64| diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[35:3]                          | * or 16B mask | src id| dest id |IOBST  | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                     * or address[35:3]                          | * or length   | src id| dest id |IOBDMA | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *
 * Trace buffer entry format in Octeon2 is different
 *
 *                 6                   5                   4                   3                   2                   1                   0
 * 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[37:0]                                  |       0           |  src id |  Group 1    | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[37:0]                                  | 0 |  xmd mask     |  src id |  Group 2    | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                          address[37:0]                                  | 0 |s-did| dest id |  src id |  Group 3    | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |sta|                         *address[37:3]                            | *Length       | dest id |  src id |  Group 4    | diff timestamp|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * notes:
 * - diff timestamp is the difference in time from the previous trace event to this event - 1.  the granularity of the timestamp is programmable
 * - Fields marked as '*' are first filled with '0' at XMC time and may be filled with real data later at XMD time.  Note that the
 * XMD write may be dropped if the shallow FIFO overflows which leaves the '*' fields as '0'.
 * - 2 bits (sta) are used not to trace, but to return global state information with each read, encoded as follows:
 * 0x0=not valid
 * 0x1=valid, no discontinuity
 * 0x2=not valid, discontinuity
 * 0x3=valid, discontinuity
 * - commands are encoded as follows:
 * 0x0=DWB
 * 0x1=PL2
 * 0x2=PSL1
 * 0x3=LDD
 * 0x4=LDI
 * 0x5=LDT
 * 0x6=STF
 * 0x7=STC
 * 0x8=STP
 * 0x9=STT
 * 0xa=IOBLD8
 * 0xb=IOBLD16
 * 0xc=IOBLD32
 * 0xd=IOBLD64
 * 0xe=IOBST
 * 0xf=IOBDMA
 * - In Octeon2 the commands are grouped as follows:
 * Group1:
 *   XMC_LDT, XMC_LDI, XMC_PL2, XMC_RPL2, XMC_DWB, XMC_WBL2,
 *   XMC_SET8, XMC_SET16, XMC_SET32, XMC_SET64,
 *   XMC_CLR8, XMC_CLR16, XMC_CLR32, XMC_CLR64,
 *   XMC_INCR8, XMC_INCR16, XMC_INCR32, XMC_INCR64,
 *   XMC_DECR8, XMC_DECR16, XMC_DECR32, XMC_DECR64
 * Group2:
 *   XMC_STF, XMC_STT, XMC_STP, XMC_STC,
 *   XMC_LDD, XMC_PSL1
 *   XMC_SAA32, XMC_SAA64,
 *   XMC_FAA32, XMC_FAA64,
 *   XMC_FAS32, XMC_FAS64
 * Group3:
 *   XMC_IOBLD8, XMC_IOBLD16, XMC_IOBLD32, XMC_IOBLD64,
 *   XMC_IOBST8, XMC_IOBST16, XMC_IOBST32, XMC_IOBST64
 * Group4:
 *   XMC_IOBDMA
 * - For non IOB* commands
 * - source id is encoded as follows:
 * 0x00-0x0f=PP[n]
 * 0x10=IOB(Packet)
 * 0x11=IOB(PKO)
 * 0x12=IOB(ReqLoad, ReqStore)
 * 0x13=IOB(DWB)
 * 0x14-0x1e=illegal
 * 0x1f=IOB(generic)
 * - dest id is unused (can only be L2c)
 * - For IOB* commands
 * - source id is encoded as follows:
 * 0x00-0x0f = PP[n]
 * - dest   id is encoded as follows:
 * 0   = CIU/GPIO (for CSRs)
 * 1-2 = illegal
 * 3   = PCIe (access to RSL-type CSRs)
 * 4   = KEY (read/write operations)
 * 5   = FPA (free pool allocate/free operations)
 * 6   = DFA
 * 7   = ZIP (doorbell operations)
 * 8   = RNG (load/IOBDMA operations)
 * 10  = PKO (doorbell operations)
 * 11  = illegal
 * 12  = POW (get work, add work, status/memory/index loads, NULLrd load operations, CSR operations)
 * 13-31 = illegal
 * @endverbatim
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_TRA_H__
#define __CVMX_TRA_H__

#include "cvmx.h"
#include "cvmx-l2c.h"
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include "cvmx-tra-defs.h"
#endif

#ifdef	__cplusplus
extern "C" {
#endif


/* CSR typedefs have been moved to cvmx-tra-defs.h */

/* The 'saa' filter command is renamed as 'saa64' */
#define CVMX_TRA_FILT_SAA       CVMX_TRA_FILT_SAA64
/* The 'iobst' filter command is renamed as 'iobst64' */
#define CVMX_TRA_FILT_IOBST     CVMX_TRA_FILT_IOBST64

/**
 * Enumeration of the bitmask of all the filter commands. The bit positions
 * correspond to Octeon2 model.
 */
typedef enum
{
    CVMX_TRA_FILT_NOP     = 1ull<<0,  /**< none */
    CVMX_TRA_FILT_LDT     = 1ull<<1,  /**< don't allocate L2 or L1 */
    CVMX_TRA_FILT_LDI     = 1ull<<2,  /**< don't allocate L1 */
    CVMX_TRA_FILT_PL2     = 1ull<<3,  /**< pref L2 */
    CVMX_TRA_FILT_RPL2    = 1ull<<4,  /**< mark for replacement in L2 */
    CVMX_TRA_FILT_DWB     = 1ull<<5,  /**< clear L2 dirty bit (no writeback) + RPL2 */
    CVMX_TRA_FILT_LDD     = 1ull<<8,  /**< normal load */
    CVMX_TRA_FILT_PSL1    = 1ull<<9,  /**< pref L1, bypass L2 */
    CVMX_TRA_FILT_IOBDMA  = 1ull<<15,  /**< store reflection by IOB for prior load */
    CVMX_TRA_FILT_STF     = 1ull<<16, /**< full block store to L2, fill 0's */
    CVMX_TRA_FILT_STT     = 1ull<<17, /**< full block store bypass-L2, fill 0's */
    CVMX_TRA_FILT_STP     = 1ull<<18, /**< partial store to L2 */
    CVMX_TRA_FILT_STC     = 1ull<<19, /**< partial store to L2, if duptag valid */
    CVMX_TRA_FILT_STFIL1  = 1ull<<20, /**< full block store to L2, fill 0's, invalidate L1 */
    CVMX_TRA_FILT_STTIL1  = 1ull<<21, /**< full block store bypass-L2, fill 0's, invalidate L1 */
    CVMX_TRA_FILT_FAS32   = 1ull<<22, /**< to load from and write a word of memory atomically */
    CVMX_TRA_FILT_FAS64   = 1ull<<23, /**< to load from and write a doubleword of memory atomically */
    CVMX_TRA_FILT_WBIL2I  = 1ull<<24, /**< writeback if dirty, invalidate, clear use bit, by index/way */
    CVMX_TRA_FILT_LTGL2I  = 1ull<<25, /**< read tag @ index/way into CSR */
    CVMX_TRA_FILT_STGL2I  = 1ull<<26, /**< write tag @ index/way from CSR */
    CVMX_TRA_FILT_INVL2   = 1ull<<28, /**< invalidate, clear use bit, by address (dirty data is LOST) */
    CVMX_TRA_FILT_WBIL2   = 1ull<<29, /**< writeback if dirty, invalidate, clear use bit, by address */
    CVMX_TRA_FILT_WBL2    = 1ull<<30, /**< writeback if dirty, make clean, clear use bit, by address */
    CVMX_TRA_FILT_LCKL2   = 1ull<<31, /**< allocate (if miss), set lock bit, set use bit, by address */
    CVMX_TRA_FILT_IOBLD8  = 1ull<<32, /**< load reflection 8bit */
    CVMX_TRA_FILT_IOBLD16 = 1ull<<33, /**< load reflection 16bit */
    CVMX_TRA_FILT_IOBLD32 = 1ull<<34, /**< load reflection 32bit */
    CVMX_TRA_FILT_IOBLD64 = 1ull<<35, /**< load reflection 64bit */
    CVMX_TRA_FILT_IOBST8  = 1ull<<36, /**< store reflection 8bit */
    CVMX_TRA_FILT_IOBST16 = 1ull<<37, /**< store reflection 16bit */
    CVMX_TRA_FILT_IOBST32 = 1ull<<38, /**< store reflection 32bit */
    CVMX_TRA_FILT_IOBST64 = 1ull<<39, /**< store reflection 64bit */
    CVMX_TRA_FILT_SET8    = 1ull<<40, /**< to load from and write 1's to 8bit of memory atomically */
    CVMX_TRA_FILT_SET16   = 1ull<<41, /**< to load from and write 1's to 16bit of memory atomically */
    CVMX_TRA_FILT_SET32   = 1ull<<42, /**< to load from and write 1's to 32bit of memory atomically */
    CVMX_TRA_FILT_SET64   = 1ull<<43, /**< to load from and write 1's to 64bit of memory atomically */
    CVMX_TRA_FILT_CLR8    = 1ull<<44, /**< to load from and write 0's to 8bit of memory atomically */
    CVMX_TRA_FILT_CLR16   = 1ull<<45, /**< to load from and write 0's to 16bit of memory atomically */
    CVMX_TRA_FILT_CLR32   = 1ull<<46, /**< to load from and write 0's to 32bit of memory atomically */
    CVMX_TRA_FILT_CLR64   = 1ull<<47, /**< to load from and write 0's to 64bit of memory atomically */
    CVMX_TRA_FILT_INCR8   = 1ull<<48, /**< to load and increment 8bit of memory atomically */
    CVMX_TRA_FILT_INCR16  = 1ull<<49, /**< to load and increment 16bit of memory atomically */
    CVMX_TRA_FILT_INCR32  = 1ull<<50, /**< to load and increment 32bit of memory atomically */
    CVMX_TRA_FILT_INCR64  = 1ull<<51, /**< to load and increment 64bit of memory atomically */
    CVMX_TRA_FILT_DECR8   = 1ull<<52, /**< to load and decrement 8bit of memory atomically */
    CVMX_TRA_FILT_DECR16  = 1ull<<53, /**< to load and decrement 16bit of memory atomically */
    CVMX_TRA_FILT_DECR32  = 1ull<<54, /**< to load and decrement 32bit of memory atomically */
    CVMX_TRA_FILT_DECR64  = 1ull<<55, /**< to load and decrement 64bit of memory atomically */
    CVMX_TRA_FILT_FAA32   = 1ull<<58, /**< to load from and add to a word of memory atomically */
    CVMX_TRA_FILT_FAA64   = 1ull<<59, /**< to load from and add to a doubleword of memory atomically  */
    CVMX_TRA_FILT_SAA32   = 1ull<<62, /**< to atomically add a word to a memory location */
    CVMX_TRA_FILT_SAA64   = 1ull<<63, /**< to atomically add a doubleword to a memory location */
    CVMX_TRA_FILT_ALL     = -1ull     /**< all the above filter commands */
} cvmx_tra_filt_t;

/*
 * Enumeration of the bitmask of all source commands.
 */
typedef enum
{
    CVMX_TRA_SID_PP0      = 1ull<<0,  /**< Enable tracing from PP0 with matching sourceID */
    CVMX_TRA_SID_PP1      = 1ull<<1,  /**< Enable tracing from PP1 with matching sourceID */
    CVMX_TRA_SID_PP2      = 1ull<<2,  /**< Enable tracing from PP2 with matching sourceID */
    CVMX_TRA_SID_PP3      = 1ull<<3,  /**< Enable tracing from PP3 with matching sourceID */
    CVMX_TRA_SID_PP4      = 1ull<<4,  /**< Enable tracing from PP4 with matching sourceID */
    CVMX_TRA_SID_PP5      = 1ull<<5,  /**< Enable tracing from PP5 with matching sourceID */
    CVMX_TRA_SID_PP6      = 1ull<<6,  /**< Enable tracing from PP6 with matching sourceID */
    CVMX_TRA_SID_PP7      = 1ull<<7,  /**< Enable tracing from PP7 with matching sourceID */
    CVMX_TRA_SID_PP8      = 1ull<<8,  /**< Enable tracing from PP8 with matching sourceID */
    CVMX_TRA_SID_PP9      = 1ull<<9,  /**< Enable tracing from PP9 with matching sourceID */
    CVMX_TRA_SID_PP10     = 1ull<<10, /**< Enable tracing from PP10 with matching sourceID */
    CVMX_TRA_SID_PP11     = 1ull<<11, /**< Enable tracing from PP11 with matching sourceID */
    CVMX_TRA_SID_PP12     = 1ull<<12, /**< Enable tracing from PP12 with matching sourceID */
    CVMX_TRA_SID_PP13     = 1ull<<13, /**< Enable tracing from PP13 with matching sourceID */
    CVMX_TRA_SID_PP14     = 1ull<<14, /**< Enable tracing from PP14 with matching sourceID */
    CVMX_TRA_SID_PP15     = 1ull<<15, /**< Enable tracing from PP15 with matching sourceID */
    CVMX_TRA_SID_PKI      = 1ull<<16, /**< Enable tracing of write requests from PIP/IPD */
    CVMX_TRA_SID_PKO      = 1ull<<17, /**< Enable tracing of write requests from PKO */
    CVMX_TRA_SID_IOBREQ   = 1ull<<18, /**< Enable tracing of write requests from FPA,TIM,DFA,PCI,ZIP,POW, and PKO (writes) */
    CVMX_TRA_SID_DWB      = 1ull<<19, /**< Enable tracing of write requests from IOB DWB engine */
    CVMX_TRA_SID_ALL      = -1ull     /**< Enable tracing all the above source commands */
} cvmx_tra_sid_t;


#define CVMX_TRA_DID_SLI  CVMX_TRA_DID_PCI /**< Enable tracing of requests to SLI and RSL-type CSRs. */
/*
 * Enumeration of the bitmask of all destination commands.
 */
typedef enum
{
    CVMX_TRA_DID_MIO      = 1ull<<0,  /**< Enable tracing of CIU and GPIO CSR's */
    CVMX_TRA_DID_PCI      = 1ull<<3,  /**< Enable tracing of requests to PCI and RSL type CSR's */
    CVMX_TRA_DID_KEY      = 1ull<<4,  /**< Enable tracing of requests to KEY memory */
    CVMX_TRA_DID_FPA      = 1ull<<5,  /**< Enable tracing of requests to FPA */
    CVMX_TRA_DID_DFA      = 1ull<<6,  /**< Enable tracing of requests to DFA */
    CVMX_TRA_DID_ZIP      = 1ull<<7,  /**< Enable tracing of requests to ZIP */
    CVMX_TRA_DID_RNG      = 1ull<<8,  /**< Enable tracing of requests to RNG */
    CVMX_TRA_DID_IPD      = 1ull<<9,  /**< Enable tracing of IPD CSR accesses */
    CVMX_TRA_DID_PKO      = 1ull<<10, /**< Enable tracing of PKO accesses (doorbells) */
    CVMX_TRA_DID_POW      = 1ull<<12, /**< Enable tracing of requests to RNG */
    CVMX_TRA_DID_USB0     = 1ull<<13, /**< Enable tracing of USB0 accesses (UAHC0 EHCI and OHCI NCB CSRs) */
    CVMX_TRA_DID_RAD      = 1ull<<14, /**< Enable tracing of RAD accesses (doorbells) */
    CVMX_TRA_DID_DPI      = 1ull<<27, /**< Enable tracing of DPI accesses (DPI NCD CSRs) */
    CVMX_TRA_DID_FAU      = 1ull<<30, /**< Enable tracing FAU accesses */
    CVMX_TRA_DID_ALL      = -1ull     /**< Enable tracing all the above destination commands */
} cvmx_tra_did_t;

/**
 * TRA data format definition. Use the type field to
 * determine which union element to use.
 *
 * In Octeon 2, the trace buffer is 69 bits,
 * the first read accesses bits 63:0 of the trace buffer entry, and
 * the second read accesses bits 68:64 of the trace buffer entry.
 */
typedef union
{
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t  datahi;
        uint64_t  data;
#else
        uint64_t  data;
        uint64_t  datahi;
#endif
    } u128;

    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    reserved3   : 64;
        uint64_t    valid       : 1;
        uint64_t    discontinuity:1;
        uint64_t    address     : 36;
        uint64_t    reserved    : 5;
        uint64_t    source      : 5;
        uint64_t    reserved2   : 3;
        uint64_t    type        : 5;
        uint64_t    timestamp   : 8;
#else
        uint64_t    timestamp   : 8;
        uint64_t    type        : 5;
        uint64_t    reserved2   : 3;
        uint64_t    source      : 5;
        uint64_t    reserved    : 5;
        uint64_t    address     : 36;
        uint64_t    discontinuity:1;
        uint64_t    valid       : 1;
        uint64_t    reserved3   : 64;
#endif
    } cmn; /**< for DWB, PL2, PSL1, LDD, LDI, LDT */
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    reserved3   : 64;
        uint64_t    valid       : 1;
        uint64_t    discontinuity:1;
        uint64_t    address     : 33;
        uint64_t    mask        : 8;
        uint64_t    source      : 5;
        uint64_t    reserved2   : 3;
        uint64_t    type        : 5;
        uint64_t    timestamp   : 8;
#else
        uint64_t    timestamp   : 8;
        uint64_t    type        : 5;
        uint64_t    reserved2   : 3;
        uint64_t    source      : 5;
        uint64_t    mask        : 8;
        uint64_t    address     : 33;
        uint64_t    discontinuity:1;
        uint64_t    valid       : 1;
        uint64_t    reserved3   : 64;
#endif
    } store; /**< STC, STF, STP, STT */
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    reserved3   : 64;
        uint64_t    valid       : 1;
        uint64_t    discontinuity:1;
        uint64_t    address     : 36;
        uint64_t    reserved    : 2;
        uint64_t    subid       : 3;
        uint64_t    source      : 4;
        uint64_t    dest        : 5;
        uint64_t    type        : 4;
        uint64_t    timestamp   : 8;
#else
        uint64_t    timestamp   : 8;
        uint64_t    type        : 4;
        uint64_t    dest        : 5;
        uint64_t    source      : 4;
        uint64_t    subid       : 3;
        uint64_t    reserved    : 2;
        uint64_t    address     : 36;
        uint64_t    discontinuity:1;
        uint64_t    valid       : 1;
        uint64_t    reserved3   : 64;
#endif
    } iobld; /**< for IOBLD8, IOBLD16, IOBLD32, IOBLD64, IOBST, SAA */
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    reserved3   : 64;
        uint64_t    valid       : 1;
        uint64_t    discontinuity:1;
        uint64_t    address     : 33;
        uint64_t    mask        : 8;
        uint64_t    source      : 4;
        uint64_t    dest        : 5;
        uint64_t    type        : 4;
        uint64_t    timestamp   : 8;
#else
        uint64_t    timestamp   : 8;
        uint64_t    type        : 4;
        uint64_t    dest        : 5;
        uint64_t    source      : 4;
        uint64_t    mask        : 8;
        uint64_t    address     : 33;
        uint64_t    discontinuity:1;
        uint64_t    valid       : 1;
        uint64_t    reserved3   : 64;
#endif
    } iob; /**< for IOBDMA */

    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    reserved1   : 59;
        uint64_t    discontinuity:1;
        uint64_t    valid       : 1;
        uint64_t    addresshi   : 3;   /* Split the address to fit in upper 64 bits  */
        uint64_t    addresslo   : 35;  /* and lower 64-bits. */
        uint64_t    reserved    : 10;
        uint64_t    source      : 5;
        uint64_t    type        : 6;
        uint64_t    timestamp   : 8;
#else
        uint64_t    timestamp   : 8;
        uint64_t    type        : 6;
        uint64_t    source      : 5;
        uint64_t    reserved    : 10;
        uint64_t    addresslo   : 35;
        uint64_t    addresshi   : 3;
        uint64_t    valid       : 1;
        uint64_t    discontinuity:1;
        uint64_t    reserved1   : 59;
#endif
    } cmn2; /**< for LDT, LDI, PL2, RPL2, DWB, WBL2, WBIL2i, LTGL2i, STGL2i, INVL2, WBIL2, LCKL2, SET*, CLR*, INCR*, DECR* */
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    reserved1   : 59;
        uint64_t    discontinuity:1;
        uint64_t    valid       : 1;
        uint64_t    addresshi   : 3;   /* Split the address to fit in upper 64 bits  */
        uint64_t    addresslo   : 35;  /* and lower 64-bits */
        uint64_t    reserved    : 2;
        uint64_t    mask        : 8;
        uint64_t    source      : 5;
        uint64_t    type        : 6;
        uint64_t    timestamp   : 8;
#else
        uint64_t    timestamp   : 8;
        uint64_t    type        : 6;
        uint64_t    source      : 5;
        uint64_t    mask        : 8;
        uint64_t    reserved    : 2;
        uint64_t    addresslo   : 35;
        uint64_t    addresshi   : 3;
        uint64_t    valid       : 1;
        uint64_t    discontinuity:1;
        uint64_t    reserved1   : 59;
#endif
    } store2; /**< for STC, STF, STP, STT, LDD, PSL1, SAA32, SAA64, FAA32, FAA64, FAS32, FAS64, STTIL1, STFIL1 */
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    reserved1   : 59;
        uint64_t    discontinuity:1;
        uint64_t    valid       : 1;
        uint64_t    addresshi   : 3;   /* Split the address to fit in upper 64 bits  */
        uint64_t    addresslo   : 35;  /* and lower 64-bits */
        uint64_t    reserved    : 2;
        uint64_t    subid       : 3;
        uint64_t    dest        : 5;
        uint64_t    source      : 5;
        uint64_t    type        : 6;
        uint64_t    timestamp   : 8;
#else
        uint64_t    timestamp   : 8;
        uint64_t    type        : 6;
        uint64_t    source      : 5;
        uint64_t    dest        : 5;
        uint64_t    subid       : 3;
        uint64_t    reserved    : 2;
        uint64_t    addresslo   : 35;
        uint64_t    addresshi   : 3;
        uint64_t    valid       : 1;
        uint64_t    discontinuity:1;
        uint64_t    reserved1   : 59;
#endif
    } iobld2; /**< for IOBLD8, IOBLD16, IOBLD32, IOBLD64, IOBST64, IOBST32, IOBST16, IOBST8 */
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t    reserved1   : 59;
        uint64_t    discontinuity:1;
        uint64_t    valid       : 1;
        uint64_t    addresshi   : 3;   /* Split the address to fit in upper 64 bits  */
        uint64_t    addresslo   : 32;  /* and lower 64-bits */
        uint64_t    mask        : 8;
        uint64_t    dest        : 5;
        uint64_t    source      : 5;
        uint64_t    type        : 6;
        uint64_t    timestamp   : 8;
#else
        uint64_t    timestamp   : 8;
        uint64_t    type        : 6;
        uint64_t    source      : 5;
        uint64_t    dest        : 5;
        uint64_t    mask        : 8;
        uint64_t    addresslo   : 32;
	uint64_t    addresshi   : 3;
        uint64_t    valid       : 1;
        uint64_t    discontinuity:1;
        uint64_t    reserved1   : 59;
#endif
    } iob2; /**< for IOBDMA */
} cvmx_tra_data_t;

/* The trace buffer number to use. */
extern int _cvmx_tra_unit;

/**
 * Setup the TRA buffer for use
 *
 * @param control TRA control setup
 * @param filter  Which events to log
 * @param source_filter
 *                Source match
 * @param dest_filter
 *                Destination match
 * @param address Address compare
 * @param address_mask
 *                Address mask
 */
extern void cvmx_tra_setup(cvmx_tra_ctl_t control, cvmx_tra_filt_t filter,
                           cvmx_tra_sid_t source_filter, cvmx_tra_did_t dest_filter,
                           uint64_t address, uint64_t address_mask);

/**
 * Setup each TRA buffer for use
 *
 * @param tra     Which TRA buffer to use (0-3)
 * @param control TRA control setup
 * @param filter  Which events to log
 * @param source_filter
 *                Source match
 * @param dest_filter
 *                Destination match
 * @param address Address compare
 * @param address_mask
 *                Address mask
 */
extern void cvmx_tra_setup_v2(int tra, cvmx_tra_ctl_t control, cvmx_tra_filt_t filter,
                             cvmx_tra_sid_t source_filter, cvmx_tra_did_t dest_filter,
                             uint64_t address, uint64_t address_mask);

/**
 * Setup a TRA trigger. How the triggers are used should be
 * setup using cvmx_tra_setup.
 *
 * @param trigger Trigger to setup (0 or 1)
 * @param filter  Which types of events to trigger on
 * @param source_filter
 *                Source trigger match
 * @param dest_filter
 *                Destination trigger match
 * @param address Trigger address compare
 * @param address_mask
 *                Trigger address mask
 */
extern void cvmx_tra_trig_setup(uint64_t trigger, cvmx_tra_filt_t filter,
                                cvmx_tra_sid_t source_filter, cvmx_tra_did_t dest_filter,
                                uint64_t address, uint64_t address_mask);

/**
 * Setup each TRA trigger. How the triggers are used should be
 * setup using cvmx_tra_setup.
 *
 * @param tra     Which TRA buffer to use (0-3)
 * @param trigger Trigger to setup (0 or 1)
 * @param filter  Which types of events to trigger on
 * @param source_filter
 *                Source trigger match
 * @param dest_filter
 *                Destination trigger match
 * @param address Trigger address compare
 * @param address_mask
 *                Trigger address mask
 */
extern void cvmx_tra_trig_setup_v2(int tra, uint64_t trigger, cvmx_tra_filt_t filter,
                                cvmx_tra_sid_t source_filter, cvmx_tra_did_t dest_filter,
                                uint64_t address, uint64_t address_mask);

/**
 * Read an entry from the TRA buffer. The trace buffer format is
 * different in Octeon2, need to read twice from TRA_READ_DAT.
 *
 * @return Value return. High bit will be zero if there wasn't any data
 */
extern cvmx_tra_data_t cvmx_tra_read(void);

/**
 * Read an entry from the TRA buffer from a given TRA unit.
 *
 * @param tra_unit  Trace buffer unit to read
 *
 * @return Value return. High bit will be zero if there wasn't any data
 */
cvmx_tra_data_t cvmx_tra_read_v2(int tra_unit);

/**
 * Decode a TRA entry into human readable output
 *
 * @param tra_ctl Trace control setup
 * @param data    Data to decode
 */
extern void cvmx_tra_decode_text(cvmx_tra_ctl_t tra_ctl, cvmx_tra_data_t data);

/**
 * Display the entire trace buffer. It is advised that you
 * disable the trace buffer before calling this routine
 * otherwise it could infinitely loop displaying trace data
 * that it created.
 */
extern void cvmx_tra_display(void);

/**
 * Display the entire trace buffer. It is advised that you
 * disable the trace buffer before calling this routine
 * otherwise it could infinitely loop displaying trace data
 * that it created.
 *
 * @param tra_unit   Which TRA buffer to use.
 */
extern void cvmx_tra_display_v2(int tra_unit);

/**
 * Enable or disable the TRA hardware, by default enables all TRAs.
 *
 * @param enable 1=enable, 0=disable
 */
static inline void cvmx_tra_enable(int enable)
{
    cvmx_tra_ctl_t control;
    int tad;

    for (tad = 0; tad < CVMX_L2C_TADS; tad++)
    {    
        control.u64 = cvmx_read_csr(CVMX_TRAX_CTL(tad));
        control.s.ena = enable;
        cvmx_write_csr(CVMX_TRAX_CTL(tad), control.u64);
        cvmx_read_csr(CVMX_TRAX_CTL(tad));
    }
}

/**
 * Enable or disable a particular TRA hardware
 *
 * @param enable  1=enable, 0=disable
 * @param tra     which TRA to enable, CN68XX has 4.
 */
static inline void cvmx_tra_enable_v2(int enable, int tra)
{
    cvmx_tra_ctl_t control;

    if ((tra + 1) > CVMX_L2C_TADS)
    {
        cvmx_dprintf("cvmx_tra_enable: Invalid TRA(%d), max allowed are %d\n", tra, CVMX_L2C_TADS - 1);
        tra = 0;
    }
    control.u64 = cvmx_read_csr(CVMX_TRAX_CTL(tra));
    control.s.ena = enable;
    cvmx_write_csr(CVMX_TRAX_CTL(tra), control.u64);
    cvmx_read_csr(CVMX_TRAX_CTL(tra));
}

#ifdef	__cplusplus
}
#endif

#endif

