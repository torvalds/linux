/*****************************************************************************

    AudioScience HPI driver
    Copyright (C) 1997-2011  AudioScience Inc. <support@audioscience.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of version 2 of the GNU General Public License as
    published by the Free Software Foundation;

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

De macros.

*****************************************************************************/

#ifndef _HPIDE_H
#define _HPIDE_H

#include "hpi_internal.h"

/* Define deging levels.  */
enum { HPI_DE_LEVEL_ERROR = 0,	/* always log errors */
	HPI_DE_LEVEL_WARNING = 1,
	HPI_DE_LEVEL_NOTICE = 2,
	HPI_DE_LEVEL_INFO = 3,
	HPI_DE_LEVEL_DE = 4,
	HPI_DE_LEVEL_VERBOSE = 5	/* same printk level as DE */
};

#define HPI_DE_LEVEL_DEFAULT HPI_DE_LEVEL_NOTICE

/* an OS can define an extra flag string that is appended to
   the start of each message, eg see linux kernel hpios.h */

#ifdef SOURCEFILE_NAME
#define FILE_LINE  SOURCEFILE_NAME ":" __stringify(__LINE__) " "
#else
#define FILE_LINE  __FILE__ ":" __stringify(__LINE__) " "
#endif

#define HPI_DE_ASSERT(expression) \
	do { \
		if (!(expression)) { \
			printk(KERN_ERR  FILE_LINE \
				"ASSERT " __stringify(expression)); \
		} \
	} while (0)

#define HPI_DE_LOG(level, ...) \
	do { \
		if (hpi_de_level >= HPI_DE_LEVEL_##level) { \
			printk(HPI_DE_FLAG_##level \
			FILE_LINE  __VA_ARGS__); \
		} \
	} while (0)

void hpi_de_init(void);
int hpi_de_level_set(int level);
int hpi_de_level_get(void);
/* needed by Linux driver for dynamic de level changes */
extern int hpi_de_level;

void hpi_de_message(struct hpi_message *phm, char *sz_fileline);

void hpi_de_data(u16 *pdata, u32 len);

#define HPI_DE_DATA(pdata, len) \
	do { \
		if (hpi_de_level >= HPI_DE_LEVEL_VERBOSE) \
			hpi_de_data(pdata, len); \
	} while (0)

#define HPI_DE_MESSAGE(level, phm) \
	do { \
		if (hpi_de_level >= HPI_DE_LEVEL_##level) { \
			hpi_de_message(phm, HPI_DE_FLAG_##level \
				FILE_LINE __stringify(level)); \
		} \
	} while (0)

#define HPI_DE_RESPONSE(phr) \
	do { \
		if (((hpi_de_level >= HPI_DE_LEVEL_DE) && \
			(phr->error)) ||\
		(hpi_de_level >= HPI_DE_LEVEL_VERBOSE)) \
			printk(KERN_DE "HPI_RES%d,%d,%d\n", \
				phr->version, phr->error, phr->specific_error); \
	} while (0)

#ifndef compile_time_assert
#define compile_time_assert(cond, msg) \
    typedef char msg[(cond) ? 1 : -1]
#endif

#endif				/* _HPIDE_H_  */
