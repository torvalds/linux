/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _SCI_LOGGER_H_
#define _SCI_LOGGER_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods that can be called
 *        by an SCI user on the logger object.  These methods should be
 *        utilized to control the amount of information being logged by the
 *        SCI implementation.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>


/* The following is a list of verbosity levels that can be used to enable */
/* logging for specific log objects.                                      */

/** Enable/disable error level logging for the associated logger object(s). */
#define SCI_LOG_VERBOSITY_ERROR      0x00

/** Enable/disable warning level logging for the associated logger object(s). */
#define SCI_LOG_VERBOSITY_WARNING    0x01

/**
 * Enable/disable informative level logging for the associated logger object(s).
 */
#define SCI_LOG_VERBOSITY_INFO       0x02

/**
 * This constant is used to enable function trace (enter/exit) level
 * logging for the associated log object(s).
 */
#define SCI_LOG_VERBOSITY_TRACE      0x03

/**
 * This constant is used to enable state tracing information it will emit a
 * log message each time a state is entered for the associated log object(s).
 */
#define SCI_LOG_VERBOSITY_STATES     0x04

#ifdef SCI_LOGGING

/**
 * @brief This method will return the verbosity levels enabled for the object
 *        listed in the log_object parameter.
 * @note  Logging must be enabled at compile time in the driver, otherwise
 *        calling this method has no affect.
 *
 * @param[in]  logger This parameter specifies the logger for which to
 *             disable the supplied objects/verbosities.  For example,
 *             the framework and core components have different loggers.
 * @param[in]  log_object This parameter specifies the log object for which
 *             to retrieve the associated verbosity levels.
 *             @note This parameter is not a mask, but rather a discrete
 *             value.
 *
 * @return This method will return the verbosity levels enabled for the
 *         supplied log object.
 * @retval SCI_LOGGER_VERBOSITY_ERROR This value indicates that the error
 *         verbosity level was set for the supplied log_object.
 * @retval SCI_LOGGER_VERBOSITY_WARNING This value indicates that the warning
 *         verbosity level was set for the supplied log_object.
 * @retval SCI_LOGGER_VERBOSITY_INFO This value indicates that the
 *         informational verbosity level was set for the supplied log_object.
 * @retval SCI_LOGGER_VERBOSITY_TRACE This value indicates that the trace
 *         verbosity level was set for the supplied log_object.
 * @retval SCI_LOGGER_VERBOSITY_STATES This value indicates that the states
 *         transition verbosity level was set for the supplied log_object
 */
U8 sci_logger_get_verbosity_mask(
   SCI_LOGGER_HANDLE_T  logger,
   U32                  log_object
);

/**
 * @brief This method simply returns the log object mask.  This mask
 *        is essentially a list of log objects for which the specified
 *        level (verbosity) is enabled.
 * @note  Logging must be enabled at compile time in the driver, otherwise
 *        calling this method has no affect.
 * @note  Reserved bits in both the supplied masks shall be ignored.
 *
 * @param[in]  logger This parameter specifies the logger for which to
 *             disable the supplied objects/verbosities.  For example,
 *             the framework and core components have different loggers.
 * @param[in]  verbosity This parameter specifies the verbosity for which
 *             to retrieve the set of enabled log objects.  Valid values for
 *             this parameter are:
 *                -# SCI_LOGGER_VERBOSITY_ERROR
 *                -# SCI_LOGGER_VERBOSITY_WARNING
 *                -# SCI_LOGGER_VERBOSITY_INFO
 *                -# SCI_LOGGER_VERBOSITY_TRACE
 *                -# SCI_LOGGER_VERBOSITY_STATES
 *             @note This parameter is not a mask, but rather a discrete
 *             value.
 *
 * @return This method will return the log object mask indicating each of
 *         the log objects for which logging is enabled at the supplied level.
 */
U32 sci_logger_get_object_mask(
   SCI_LOGGER_HANDLE_T  logger,
   U8                   verbosity
);

/**
 * @brief This method will enable each of the supplied log objects in
 *        log_object_mask for each of the supplied verbosities in
 *        verbosity_mask.  To enable all logging, simply set all bits in
 *        both the log_object_mask and verbosity_mask.
 * @note  Logging must be enabled at compile time in the driver, otherwise
 *        calling this method has no affect.
 * @note  Reserved bits in both the supplied masks shall be ignored.
 *
 * @param[in]  logger This parameter specifies the logger for which to
 *             disable the supplied objects/verbosities.  For example,
 *             the framework and core components have different loggers.
 * @param[in]  log_object_mask This parameter specifies the log objects for
 *             which to enable logging.
 * @param[in]  verbosity_mask This parameter specifies the verbosity levels
 *             at which to enable each log_object.
 *
 * @return none
 */
void sci_logger_enable(
   SCI_LOGGER_HANDLE_T  logger,
   U32                  log_object_mask,
   U8                   verbosity_mask
);

/**
 * @brief This method will disable each of the supplied log objects in
 *        log_object_mask for each of the supplied verbosities in
 *        verbosity_mask.  To disable all logging, simply set all bits in
 *        both the log_object_mask and verbosity_mask.
 * @note  Logging must be enabled at compile time in the driver, otherwise
 *        calling this method has no affect.
 * @note  Reserved bits in both the supplied masks shall be ignored.
 *
 * @param[in]  logger This parameter specifies the logger for which to
 *             disable the supplied objects/verbosities.  For example,
 *             the framework and core components have different loggers.
 * @param[in]  log_object_mask This parameter specifies the log objects for
 *             which to disable logging.
 * @param[in]  verbosity_mask This parameter specifies the verbosity levels
 *             at which to disable each log_object.
 *
 * @return none
 */
void sci_logger_disable(
   SCI_LOGGER_HANDLE_T  logger,
   U32                  log_object_mask,
   U8                   verbosity_mask
);


/**
 * @brief this macro checks whether it is ok to log.
 *
 * @param[in]  logger This parameter specifies the logger for
 *             which to disable the supplied
 *             objects/verbosities.  For example, the framework
 *             and core components have different loggers.
 * @param[in]  log_object_mask This parameter specifies the log objects for
 *             which to disable logging.
 * @param[in]  verbosity_mask This parameter specifies the verbosity levels
 *             at which to disable each log_object.
 *
 * @return TRUE or FALSE
 */
BOOL sci_logger_is_enabled(
   SCI_LOGGER_HANDLE_T  logger,
   U32                  log_object_mask,
   U8                   verbosity_mask
);


#else // SCI_LOGGING

#define sci_logger_get_verbosity_mask(logger, log_object)
#define sci_logger_get_object_mask(logger, verbosity)
#define sci_logger_enable(logger, log_object_mask, verbosity_mask)
#define sci_logger_disable(logger, log_object_mask, verbosity_mask)
#define sci_logger_is_enabled(logger, log_object_mask, verbosity_level)

#endif // SCI_LOGGING

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_LOGGER_H_

