/*-
 * SPDX-License-Identifier: BSD-2-Clause
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/isci/isci.h>

#include <dev/isci/scil/scif_user_callback.h>
#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/sci_logger.h>

#include <machine/stdarg.h>
#include <sys/time.h>

#define ERROR_LEVEL	0
#define WARNING_LEVEL	1
#define TRACE_LEVEL	2
#define INFO_LEVEL	3

void
isci_log_message(uint32_t verbosity, char *log_message_prefix,
    char *log_message, ...)
{
	va_list argp;
	char buffer[512];
	struct timeval tv;

	if (verbosity > g_isci_debug_level)
		return;

	va_start (argp, log_message);
	vsnprintf(buffer, sizeof(buffer)-1, log_message, argp);
	va_end(argp);
	microtime(&tv);

	printf("isci: %d:%06d %s %s", (int)tv.tv_sec, (int)tv.tv_usec,
	    log_message_prefix, buffer);
}


#ifdef SCI_LOGGING
#define SCI_ENABLE_LOGGING_ERROR	1
#define SCI_ENABLE_LOGGING_WARNING	1
#define SCI_ENABLE_LOGGING_INFO		1
#define SCI_ENABLE_LOGGING_TRACE	1
#define SCI_ENABLE_LOGGING_STATES	1

#define ISCI_LOG_MESSAGE(			\
	logger_object,				\
	log_object_mask,			\
	log_message,				\
	verbosity,				\
	log_message_prefix			\
)						\
{						\
	va_list argp;				\
	char buffer[512];			\
						\
	if (!sci_logger_is_enabled(logger_object, log_object_mask, verbosity)) \
		return;				\
						\
	va_start (argp, log_message);		\
	vsnprintf(buffer, sizeof(buffer)-1, log_message, argp); \
	va_end(argp);				\
						\
	/* prepend the "object:verbosity_level:" */ \
	isci_log_message(verbosity, log_message_prefix, buffer); \
}
#endif /* SCI_LOGGING */


#ifdef SCI_ENABLE_LOGGING_ERROR
/**
 * @brief In this method the user is expected to log the supplied
 *        error information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is an error from the framework.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void scif_cb_logger_log_error(SCI_LOGGER_HANDLE_T logger_object,
    uint32_t log_object_mask, char *log_message, ...)
{

	ISCI_LOG_MESSAGE(logger_object, log_object_mask, log_message,
	    SCI_LOG_VERBOSITY_ERROR, "FRAMEWORK: ERROR: ");
}
#endif

#ifdef SCI_ENABLE_LOGGING_WARNING
/**
 * @brief In this method the user is expected to log the supplied warning
 *        information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a warning from the framework.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void
scif_cb_logger_log_warning(SCI_LOGGER_HANDLE_T logger_object,
    uint32_t log_object_mask, char *log_message, ...)
{

	ISCI_LOG_MESSAGE(logger_object, log_object_mask, log_message,
	    SCI_LOG_VERBOSITY_WARNING, "FRAMEWORK: WARNING: ");
}
#endif

#ifdef SCI_ENABLE_LOGGING_INFO
/**
 * @brief In this method the user is expected to log the supplied debug
 *        information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a debug message from the framework.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void
scif_cb_logger_log_info(SCI_LOGGER_HANDLE_T logger_object,
    uint32_t log_object_mask, char *log_message, ...)
{

	ISCI_LOG_MESSAGE(logger_object, log_object_mask, log_message,
	    SCI_LOG_VERBOSITY_INFO, "FRAMEWORK: INFO: ");
}
#endif

#ifdef SCI_ENABLE_LOGGING_TRACE
/**
 * @brief In this method the user is expected to log the supplied function
 *        trace information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a function trace (i.e. entry/exit) message from the
 *        framework.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void
scif_cb_logger_log_trace(SCI_LOGGER_HANDLE_T logger_object,
    uint32_t log_object_mask, char *log_message, ...)
{

	ISCI_LOG_MESSAGE(logger_object, log_object_mask, log_message,
	    SCI_LOG_VERBOSITY_TRACE, "FRAMEWORK: TRACE: ");
}
#endif

#ifdef SCI_ENABLE_LOGGING_STATES
/**
 * @brief In this method the user is expected to log the supplied function
 *        state transition information.  The user must be capable of handling
 *        variable length argument lists and should consider prepending the
 *        fact that this is a function trace (i.e. entry/exit) message from
 *        the framework.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void
scif_cb_logger_log_states(SCI_LOGGER_HANDLE_T logger_object,
    uint32_t log_object_mask, char *log_message, ...)
{

	ISCI_LOG_MESSAGE(logger_object, log_object_mask, log_message,
	    SCI_LOG_VERBOSITY_STATES, "FRAMEWORK: STATE TRANSITION: ");
}
#endif

#ifdef SCI_ENABLE_LOGGING_ERROR
/**
 * @brief In this method the user is expected to log the supplied
 *        error information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is an error from the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void
scic_cb_logger_log_error(SCI_LOGGER_HANDLE_T logger_object,
    uint32_t log_object_mask, char *log_message, ...)
{

	ISCI_LOG_MESSAGE(logger_object, log_object_mask, log_message,
	    SCI_LOG_VERBOSITY_ERROR, "CORE: ERROR: ");
}
#endif

#ifdef SCI_ENABLE_LOGGING_WARNING
/**
 * @brief In this method the user is expected to log the supplied warning
 *        information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a warning from the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void
scic_cb_logger_log_warning(SCI_LOGGER_HANDLE_T logger_object,
    uint32_t log_object_mask, char *log_message, ...)
{

	ISCI_LOG_MESSAGE(logger_object, log_object_mask, log_message,
	    SCI_LOG_VERBOSITY_WARNING, "CORE: WARNING: ");
}
#endif

#ifdef SCI_ENABLE_LOGGING_INFO
/**
 * @brief In this method the user is expected to log the supplied debug
 *        information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a debug message from the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void
scic_cb_logger_log_info(SCI_LOGGER_HANDLE_T logger_object,
    uint32_t log_object_mask, char *log_message, ...)
{

	ISCI_LOG_MESSAGE(logger_object, log_object_mask, log_message,
	    SCI_LOG_VERBOSITY_INFO, "CORE: INFO: ");
}
#endif

#ifdef SCI_ENABLE_LOGGING_TRACE
/**
 * @brief In this method the user is expected to log the supplied function
 *        trace information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a function trace (i.e. entry/exit) message from the
 *        core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void
scic_cb_logger_log_trace(SCI_LOGGER_HANDLE_T logger_object,
    uint32_t log_object_mask, char *log_message, ...)
{

	ISCI_LOG_MESSAGE(logger_object, log_object_mask, log_message,
	    SCI_LOG_VERBOSITY_TRACE, "CORE: TRACE: ");
}
#endif

#ifdef SCI_ENABLE_LOGGING_STATES
/**
 * @brief In this method the user is expected to log the supplied function
 *        state transition information.  The user must be capable of handling
 *        variable length argument lists and should consider prepending the
 *        fact that this is a function trace (i.e. entry/exit) message from
 *        the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void
scic_cb_logger_log_states(SCI_LOGGER_HANDLE_T logger_object,
    uint32_t log_object_mask, char *log_message, ...)
{

	ISCI_LOG_MESSAGE(logger_object, log_object_mask, log_message,
	    SCI_LOG_VERBOSITY_STATES, "CORE: STATE TRANSITION: ");
}
#endif
