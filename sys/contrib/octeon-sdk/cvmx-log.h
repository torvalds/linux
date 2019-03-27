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






#ifndef __CVMX_LOG_H__
#define __CVMX_LOG_H__

/**
 * @file
 *
 * cvmx-log supplies a fast log buffer implementation. Each core writes
 * log data to a differnet buffer to avoid synchronization overhead. Function
 * call logging can be turned on with the GCC option "-pg".
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx-core.h>
#else
#include "cvmx-core.h"
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Add CVMX_LOG_DISABLE_PC_LOGGING as an attribute to and function prototype
 * that you don't want logged when the gcc option "-pg" is supplied. We
 * use it on the cvmx-log functions since it is pointless to log the
 * calling of a function than in itself writes to the log.
 */
#define CVMX_LOG_DISABLE_PC_LOGGING __attribute__((no_instrument_function))

/**
 * Log a constant printf style format string with 0 to 4
 * arguments. The string must persist until the log is read,
 * but the parameters are copied into the log.
 *
 * @param format  Constant printf style format string.
 * @param numberx 64bit argument to the printf format string
 */
void cvmx_log_printf0(const char *format) CVMX_LOG_DISABLE_PC_LOGGING;
void cvmx_log_printf1(const char *format, uint64_t number1) CVMX_LOG_DISABLE_PC_LOGGING;
void cvmx_log_printf2(const char *format, uint64_t number1, uint64_t number2) CVMX_LOG_DISABLE_PC_LOGGING;
void cvmx_log_printf3(const char *format, uint64_t number1, uint64_t number2, uint64_t number3) CVMX_LOG_DISABLE_PC_LOGGING;
void cvmx_log_printf4(const char *format, uint64_t number1, uint64_t number2, uint64_t number3, uint64_t number4) CVMX_LOG_DISABLE_PC_LOGGING;

/**
 * Log an arbitrary block of 64bit words. At most 255 64bit
 * words can be logged. The words are copied into the log.
 *
 * @param size_in_dwords
 *               Number of 64bit dwords to copy into the log.
 * @param data   Array of 64bit dwords to copy
 */
void cvmx_log_data(uint64_t size_in_dwords, const uint64_t *data) CVMX_LOG_DISABLE_PC_LOGGING;

/**
 * Log a structured data object. Post processing will use the
 * debugging information in the ELF file to determine how to
 * display the structure. Max of 2032 bytes.
 *
 * Example:
 * cvmx_log_structure("cvmx_wqe_t", work, sizeof(*work));
 *
 * @param type   C typedef expressed as a string. This will be used to
 *               lookup the structure in the debugging infirmation.
 * @param data   Data to be written to the log.
 * @param size_in_bytes
 *               Size if the data in bytes. Normally you'll use the
 *               sizeof() operator here.
 */
void cvmx_log_structure(const char *type, void *data, int size_in_bytes) CVMX_LOG_DISABLE_PC_LOGGING;

/**
 * Setup the mips performance counters
 *
 * @param counter1 Event type for counter 1
 * @param counter2 Event type for counter 2
 */
void cvmx_log_perf_setup(cvmx_core_perf_t counter1, cvmx_core_perf_t counter2);

/**
 * Log the performance counters
 */
void cvmx_log_perf(void) CVMX_LOG_DISABLE_PC_LOGGING;

/**
 * Display the current log in a human readable format.
 */
void cvmx_log_display(void);

#ifdef	__cplusplus
}
#endif

#endif
