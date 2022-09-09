/* SPDX-License-Identifier: GPL-2.0-only */
/*****************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>


Debug macros.

*****************************************************************************/

#ifndef _HPIDEBUG_H
#define _HPIDEBUG_H

#include "hpi_internal.h"

/* Define debugging levels.  */
enum { HPI_DEBUG_LEVEL_ERROR = 0,	/* always log errors */
	HPI_DEBUG_LEVEL_WARNING = 1,
	HPI_DEBUG_LEVEL_NOTICE = 2,
	HPI_DEBUG_LEVEL_INFO = 3,
	HPI_DEBUG_LEVEL_DEBUG = 4,
	HPI_DEBUG_LEVEL_VERBOSE = 5	/* same printk level as DEBUG */
};

#define HPI_DEBUG_LEVEL_DEFAULT HPI_DEBUG_LEVEL_NOTICE

/* an OS can define an extra flag string that is appended to
   the start of each message, eg see linux kernel hpios.h */

#ifdef SOURCEFILE_NAME
#define FILE_LINE  SOURCEFILE_NAME ":" __stringify(__LINE__) " "
#else
#define FILE_LINE  __FILE__ ":" __stringify(__LINE__) " "
#endif

#define HPI_DEBUG_ASSERT(expression) \
	do { \
		if (!(expression)) { \
			printk(KERN_ERR  FILE_LINE \
				"ASSERT " __stringify(expression)); \
		} \
	} while (0)

#define HPI_DEBUG_LOG(level, ...) \
	do { \
		if (hpi_debug_level >= HPI_DEBUG_LEVEL_##level) { \
			printk(HPI_DEBUG_FLAG_##level \
			FILE_LINE  __VA_ARGS__); \
		} \
	} while (0)

void hpi_debug_init(void);
int hpi_debug_level_set(int level);
int hpi_debug_level_get(void);
/* needed by Linux driver for dynamic debug level changes */
extern int hpi_debug_level;

void hpi_debug_message(struct hpi_message *phm, char *sz_fileline);

void hpi_debug_data(u16 *pdata, u32 len);

#define HPI_DEBUG_DATA(pdata, len) \
	do { \
		if (hpi_debug_level >= HPI_DEBUG_LEVEL_VERBOSE) \
			hpi_debug_data(pdata, len); \
	} while (0)

#define HPI_DEBUG_MESSAGE(level, phm) \
	do { \
		if (hpi_debug_level >= HPI_DEBUG_LEVEL_##level) { \
			hpi_debug_message(phm, HPI_DEBUG_FLAG_##level \
				FILE_LINE __stringify(level)); \
		} \
	} while (0)

#define HPI_DEBUG_RESPONSE(phr) \
	do { \
		if (((hpi_debug_level >= HPI_DEBUG_LEVEL_DEBUG) && \
			(phr->error)) ||\
		(hpi_debug_level >= HPI_DEBUG_LEVEL_VERBOSE)) \
			printk(KERN_DEBUG "HPI_RES%d,%d,%d\n", \
				phr->version, phr->error, phr->specific_error); \
	} while (0)

#ifndef compile_time_assert
#define compile_time_assert(cond, msg) \
    typedef char msg[(cond) ? 1 : -1]
#endif

#endif				/* _HPIDEBUG_H_  */
