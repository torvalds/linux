/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997, 1998 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEVSTAT_H
#define _DEVSTAT_H
#include <sys/cdefs.h>
#include <sys/devicestat.h>
#include <sys/resource.h>

#include <kvm.h>

/*
 * Bumped every time we change the userland API.  Hopefully this doesn't
 * happen very often!  This should be bumped every time we have to
 * increment SHLIB_MAJOR in the libdevstat Makefile (for non-backwards
 * compatible API changes) and should also be bumped every time we make
 * backwards-compatible API changes, so application writers have a way to 
 * determine when a particular feature is available.
 */
#define	DEVSTAT_USER_API_VER	6

#define DEVSTAT_ERRBUF_SIZE  2048 /* size of the devstat library error string */

extern char devstat_errbuf[];

typedef enum {
	DEVSTAT_MATCH_NONE	= 0x00,
	DEVSTAT_MATCH_TYPE	= 0x01,
	DEVSTAT_MATCH_IF	= 0x02,
	DEVSTAT_MATCH_PASS	= 0x04
} devstat_match_flags;

typedef enum {
	DSM_NONE,
	DSM_TOTAL_BYTES,
	DSM_TOTAL_BYTES_READ,
	DSM_TOTAL_BYTES_WRITE,
	DSM_TOTAL_TRANSFERS,
	DSM_TOTAL_TRANSFERS_READ,
	DSM_TOTAL_TRANSFERS_WRITE,
	DSM_TOTAL_TRANSFERS_OTHER,
	DSM_TOTAL_BLOCKS,
	DSM_TOTAL_BLOCKS_READ,
	DSM_TOTAL_BLOCKS_WRITE,
	DSM_KB_PER_TRANSFER,
	DSM_KB_PER_TRANSFER_READ,
	DSM_KB_PER_TRANSFER_WRITE,
	DSM_TRANSFERS_PER_SECOND,
	DSM_TRANSFERS_PER_SECOND_READ,
	DSM_TRANSFERS_PER_SECOND_WRITE,
	DSM_TRANSFERS_PER_SECOND_OTHER,
	DSM_MB_PER_SECOND,
	DSM_MB_PER_SECOND_READ,
	DSM_MB_PER_SECOND_WRITE,
	DSM_BLOCKS_PER_SECOND,
	DSM_BLOCKS_PER_SECOND_READ,
	DSM_BLOCKS_PER_SECOND_WRITE,
	DSM_MS_PER_TRANSACTION,
	DSM_MS_PER_TRANSACTION_READ,
	DSM_MS_PER_TRANSACTION_WRITE,
	DSM_SKIP,
	DSM_TOTAL_BYTES_FREE,
	DSM_TOTAL_TRANSFERS_FREE,
	DSM_TOTAL_BLOCKS_FREE,
	DSM_KB_PER_TRANSFER_FREE,
	DSM_MB_PER_SECOND_FREE,
	DSM_TRANSFERS_PER_SECOND_FREE,
	DSM_BLOCKS_PER_SECOND_FREE,
	DSM_MS_PER_TRANSACTION_OTHER,
	DSM_MS_PER_TRANSACTION_FREE,
	DSM_BUSY_PCT,
	DSM_QUEUE_LENGTH,
	DSM_TOTAL_DURATION,
	DSM_TOTAL_DURATION_READ,
	DSM_TOTAL_DURATION_WRITE,
	DSM_TOTAL_DURATION_FREE,
	DSM_TOTAL_DURATION_OTHER,
	DSM_TOTAL_BUSY_TIME,
	DSM_MAX
} devstat_metric;

struct devstat_match {
	devstat_match_flags	match_fields;
	devstat_type_flags	device_type;
	int			num_match_categories;
};

struct devstat_match_table {
	const char *		match_str;
	devstat_type_flags	type;
	devstat_match_flags	match_field;
};

struct device_selection {
	u_int32_t	device_number;
	char		device_name[DEVSTAT_NAME_LEN];
	int		unit_number;
	int		selected;
	u_int64_t	bytes;
	int		position;
};

struct devinfo {
	struct devstat	*devices;
	u_int8_t	*mem_ptr;
	long		generation;
	int		numdevs;
};

struct statinfo {
	long		cp_time[CPUSTATES];
	long		tk_nin;
	long		tk_nout;
	struct devinfo	*dinfo;
	long double 	snap_time;
};

typedef enum {
	DS_SELECT_ADD,
	DS_SELECT_ONLY,
	DS_SELECT_REMOVE,
	DS_SELECT_ADDONLY
} devstat_select_mode;

__BEGIN_DECLS

int devstat_getnumdevs(kvm_t *kd);
long devstat_getgeneration(kvm_t *kd);
int devstat_getversion(kvm_t *kd);
int devstat_checkversion(kvm_t *kd);
int devstat_getdevs(kvm_t *kd, struct statinfo *stats);
int devstat_selectdevs(struct device_selection **dev_select, int *num_selected,
		       int *num_selections, long *select_generation, 
		       long current_generation, struct devstat *devices,
		       int numdevs, struct devstat_match *matches,
		       int num_matches, char **dev_selections,
		       int num_dev_selections, devstat_select_mode select_mode,
		       int maxshowdevs, int perf_select);
int devstat_buildmatch(char *match_str, struct devstat_match **matches,
		       int *num_matches);
int devstat_compute_statistics(struct devstat *current,
			       struct devstat *previous,
			       long double etime, ...);
long double devstat_compute_etime(struct bintime *cur_time,
				  struct bintime *prev_time);
__END_DECLS

#endif /* _DEVSTAT_H  */
