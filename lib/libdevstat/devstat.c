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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/resource.h>
#include <sys/queue.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kvm.h>
#include <nlist.h>

#include "devstat.h"

int
compute_stats(struct devstat *current, struct devstat *previous,
	      long double etime, u_int64_t *total_bytes,
	      u_int64_t *total_transfers, u_int64_t *total_blocks,
	      long double *kb_per_transfer, long double *transfers_per_second,
	      long double *mb_per_second, long double *blocks_per_second,
	      long double *ms_per_transaction);

typedef enum {
	DEVSTAT_ARG_NOTYPE,
	DEVSTAT_ARG_UINT64,
	DEVSTAT_ARG_LD,
	DEVSTAT_ARG_SKIP
} devstat_arg_type;

char devstat_errbuf[DEVSTAT_ERRBUF_SIZE];

/*
 * Table to match descriptive strings with device types.  These are in
 * order from most common to least common to speed search time.
 */
struct devstat_match_table match_table[] = {
	{"da",		DEVSTAT_TYPE_DIRECT,	DEVSTAT_MATCH_TYPE},
	{"cd",		DEVSTAT_TYPE_CDROM,	DEVSTAT_MATCH_TYPE},
	{"scsi",	DEVSTAT_TYPE_IF_SCSI,	DEVSTAT_MATCH_IF},
	{"ide",		DEVSTAT_TYPE_IF_IDE,	DEVSTAT_MATCH_IF},
	{"other",	DEVSTAT_TYPE_IF_OTHER,	DEVSTAT_MATCH_IF},
	{"worm",	DEVSTAT_TYPE_WORM,	DEVSTAT_MATCH_TYPE},
	{"sa",		DEVSTAT_TYPE_SEQUENTIAL,DEVSTAT_MATCH_TYPE},
	{"pass",	DEVSTAT_TYPE_PASS,	DEVSTAT_MATCH_PASS},
	{"optical",	DEVSTAT_TYPE_OPTICAL,	DEVSTAT_MATCH_TYPE},
	{"array",	DEVSTAT_TYPE_STORARRAY,	DEVSTAT_MATCH_TYPE},
	{"changer",	DEVSTAT_TYPE_CHANGER,	DEVSTAT_MATCH_TYPE},
	{"scanner",	DEVSTAT_TYPE_SCANNER,	DEVSTAT_MATCH_TYPE},
	{"printer",	DEVSTAT_TYPE_PRINTER,	DEVSTAT_MATCH_TYPE},
	{"floppy",	DEVSTAT_TYPE_FLOPPY,	DEVSTAT_MATCH_TYPE},
	{"proc",	DEVSTAT_TYPE_PROCESSOR,	DEVSTAT_MATCH_TYPE},
	{"comm",	DEVSTAT_TYPE_COMM,	DEVSTAT_MATCH_TYPE},
	{"enclosure",	DEVSTAT_TYPE_ENCLOSURE,	DEVSTAT_MATCH_TYPE},
	{NULL,		0,			0}
};

struct devstat_args {
	devstat_metric 		metric;
	devstat_arg_type	argtype;
} devstat_arg_list[] = {
	{ DSM_NONE, DEVSTAT_ARG_NOTYPE },
	{ DSM_TOTAL_BYTES, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_BYTES_READ, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_BYTES_WRITE, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_TRANSFERS, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_TRANSFERS_READ, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_TRANSFERS_WRITE, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_TRANSFERS_OTHER, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_BLOCKS, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_BLOCKS_READ, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_BLOCKS_WRITE, DEVSTAT_ARG_UINT64 },
	{ DSM_KB_PER_TRANSFER, DEVSTAT_ARG_LD },
	{ DSM_KB_PER_TRANSFER_READ, DEVSTAT_ARG_LD },
	{ DSM_KB_PER_TRANSFER_WRITE, DEVSTAT_ARG_LD },
	{ DSM_TRANSFERS_PER_SECOND, DEVSTAT_ARG_LD },
	{ DSM_TRANSFERS_PER_SECOND_READ, DEVSTAT_ARG_LD },
	{ DSM_TRANSFERS_PER_SECOND_WRITE, DEVSTAT_ARG_LD },
	{ DSM_TRANSFERS_PER_SECOND_OTHER, DEVSTAT_ARG_LD },
	{ DSM_MB_PER_SECOND, DEVSTAT_ARG_LD },
	{ DSM_MB_PER_SECOND_READ, DEVSTAT_ARG_LD },
	{ DSM_MB_PER_SECOND_WRITE, DEVSTAT_ARG_LD },
	{ DSM_BLOCKS_PER_SECOND, DEVSTAT_ARG_LD },
	{ DSM_BLOCKS_PER_SECOND_READ, DEVSTAT_ARG_LD },
	{ DSM_BLOCKS_PER_SECOND_WRITE, DEVSTAT_ARG_LD },
	{ DSM_MS_PER_TRANSACTION, DEVSTAT_ARG_LD },
	{ DSM_MS_PER_TRANSACTION_READ, DEVSTAT_ARG_LD },
	{ DSM_MS_PER_TRANSACTION_WRITE, DEVSTAT_ARG_LD },
	{ DSM_SKIP, DEVSTAT_ARG_SKIP },
	{ DSM_TOTAL_BYTES_FREE, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_TRANSFERS_FREE, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_BLOCKS_FREE, DEVSTAT_ARG_UINT64 },
	{ DSM_KB_PER_TRANSFER_FREE, DEVSTAT_ARG_LD },
	{ DSM_MB_PER_SECOND_FREE, DEVSTAT_ARG_LD },
	{ DSM_TRANSFERS_PER_SECOND_FREE, DEVSTAT_ARG_LD },
	{ DSM_BLOCKS_PER_SECOND_FREE, DEVSTAT_ARG_LD },
	{ DSM_MS_PER_TRANSACTION_OTHER, DEVSTAT_ARG_LD },
	{ DSM_MS_PER_TRANSACTION_FREE, DEVSTAT_ARG_LD },
	{ DSM_BUSY_PCT, DEVSTAT_ARG_LD },
	{ DSM_QUEUE_LENGTH, DEVSTAT_ARG_UINT64 },
	{ DSM_TOTAL_DURATION, DEVSTAT_ARG_LD },
	{ DSM_TOTAL_DURATION_READ, DEVSTAT_ARG_LD },
	{ DSM_TOTAL_DURATION_WRITE, DEVSTAT_ARG_LD },
	{ DSM_TOTAL_DURATION_FREE, DEVSTAT_ARG_LD },
	{ DSM_TOTAL_DURATION_OTHER, DEVSTAT_ARG_LD },
	{ DSM_TOTAL_BUSY_TIME, DEVSTAT_ARG_LD },
};

static const char *namelist[] = {
#define X_NUMDEVS	0
	"_devstat_num_devs",
#define X_GENERATION	1
	"_devstat_generation",
#define X_VERSION	2
	"_devstat_version",
#define X_DEVICE_STATQ	3
	"_device_statq",
#define X_TIME_UPTIME	4
	"_time_uptime",
#define X_END		5
};

/*
 * Local function declarations.
 */
static int compare_select(const void *arg1, const void *arg2);
static int readkmem(kvm_t *kd, unsigned long addr, void *buf, size_t nbytes);
static int readkmem_nl(kvm_t *kd, const char *name, void *buf, size_t nbytes);
static char *get_devstat_kvm(kvm_t *kd);

#define KREADNL(kd, var, val) \
	readkmem_nl(kd, namelist[var], &val, sizeof(val))

int
devstat_getnumdevs(kvm_t *kd)
{
	size_t numdevsize;
	int numdevs;

	numdevsize = sizeof(int);

	/*
	 * Find out how many devices we have in the system.
	 */
	if (kd == NULL) {
		if (sysctlbyname("kern.devstat.numdevs", &numdevs,
				 &numdevsize, NULL, 0) == -1) {
			snprintf(devstat_errbuf, sizeof(devstat_errbuf),
				 "%s: error getting number of devices\n"
				 "%s: %s", __func__, __func__, 
				 strerror(errno));
			return(-1);
		} else
			return(numdevs);
	} else {

		if (KREADNL(kd, X_NUMDEVS, numdevs) == -1)
			return(-1);
		else
			return(numdevs);
	}
}

/*
 * This is an easy way to get the generation number, but the generation is
 * supplied in a more atmoic manner by the kern.devstat.all sysctl.
 * Because this generation sysctl is separate from the statistics sysctl,
 * the device list and the generation could change between the time that
 * this function is called and the device list is retrieved.
 */
long
devstat_getgeneration(kvm_t *kd)
{
	size_t gensize;
	long generation;

	gensize = sizeof(long);

	/*
	 * Get the current generation number.
	 */
	if (kd == NULL) {
		if (sysctlbyname("kern.devstat.generation", &generation, 
				 &gensize, NULL, 0) == -1) {
			snprintf(devstat_errbuf, sizeof(devstat_errbuf),
				 "%s: error getting devstat generation\n%s: %s",
				 __func__, __func__, strerror(errno));
			return(-1);
		} else
			return(generation);
	} else {
		if (KREADNL(kd, X_GENERATION, generation) == -1)
			return(-1);
		else
			return(generation);
	}
}

/*
 * Get the current devstat version.  The return value of this function
 * should be compared with DEVSTAT_VERSION, which is defined in
 * sys/devicestat.h.  This will enable userland programs to determine
 * whether they are out of sync with the kernel.
 */
int
devstat_getversion(kvm_t *kd)
{
	size_t versize;
	int version;

	versize = sizeof(int);

	/*
	 * Get the current devstat version.
	 */
	if (kd == NULL) {
		if (sysctlbyname("kern.devstat.version", &version, &versize,
				 NULL, 0) == -1) {
			snprintf(devstat_errbuf, sizeof(devstat_errbuf),
				 "%s: error getting devstat version\n%s: %s",
				 __func__, __func__, strerror(errno));
			return(-1);
		} else
			return(version);
	} else {
		if (KREADNL(kd, X_VERSION, version) == -1)
			return(-1);
		else
			return(version);
	}
}

/*
 * Check the devstat version we know about against the devstat version the
 * kernel knows about.  If they don't match, print an error into the
 * devstat error buffer, and return -1.  If they match, return 0.
 */
int
devstat_checkversion(kvm_t *kd)
{
	int buflen, res, retval = 0, version;

	version = devstat_getversion(kd);

	if (version != DEVSTAT_VERSION) {
		/*
		 * If getversion() returns an error (i.e. -1), then it
		 * has printed an error message in the buffer.  Therefore,
		 * we need to add a \n to the end of that message before we
		 * print our own message in the buffer.
		 */
		if (version == -1)
			buflen = strlen(devstat_errbuf);
		else
			buflen = 0;

		res = snprintf(devstat_errbuf + buflen,
			       DEVSTAT_ERRBUF_SIZE - buflen,
			       "%s%s: userland devstat version %d is not "
			       "the same as the kernel\n%s: devstat "
			       "version %d\n", version == -1 ? "\n" : "",
			       __func__, DEVSTAT_VERSION, __func__, version);

		if (res < 0)
			devstat_errbuf[buflen] = '\0';

		buflen = strlen(devstat_errbuf);
		if (version < DEVSTAT_VERSION)
			res = snprintf(devstat_errbuf + buflen,
				       DEVSTAT_ERRBUF_SIZE - buflen,
				       "%s: libdevstat newer than kernel\n",
				       __func__);
		else
			res = snprintf(devstat_errbuf + buflen,
				       DEVSTAT_ERRBUF_SIZE - buflen,
				       "%s: kernel newer than libdevstat\n",
				       __func__);

		if (res < 0)
			devstat_errbuf[buflen] = '\0';

		retval = -1;
	}

	return(retval);
}

/*
 * Get the current list of devices and statistics, and the current
 * generation number.
 * 
 * Return values:
 * -1  -- error
 *  0  -- device list is unchanged
 *  1  -- device list has changed
 */
int
devstat_getdevs(kvm_t *kd, struct statinfo *stats)
{
	int error;
	size_t dssize;
	long oldgeneration;
	int retval = 0;
	struct devinfo *dinfo;
	struct timespec ts;

	dinfo = stats->dinfo;

	if (dinfo == NULL) {
		snprintf(devstat_errbuf, sizeof(devstat_errbuf),
			 "%s: stats->dinfo was NULL", __func__);
		return(-1);
	}

	oldgeneration = dinfo->generation;

	if (kd == NULL) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		stats->snap_time = ts.tv_sec + ts.tv_nsec * 1e-9;

		/* If this is our first time through, mem_ptr will be null. */
		if (dinfo->mem_ptr == NULL) {
			/*
			 * Get the number of devices.  If it's negative, it's an
			 * error.  Don't bother setting the error string, since
			 * getnumdevs() has already done that for us.
			 */
			if ((dinfo->numdevs = devstat_getnumdevs(kd)) < 0)
				return(-1);
			
			/*
			 * The kern.devstat.all sysctl returns the current 
			 * generation number, as well as all the devices.  
			 * So we need four bytes more.
			 */
			dssize = (dinfo->numdevs * sizeof(struct devstat)) +
				 sizeof(long);
			dinfo->mem_ptr = (u_int8_t *)malloc(dssize);
			if (dinfo->mem_ptr == NULL) {
				snprintf(devstat_errbuf, sizeof(devstat_errbuf),
					 "%s: Cannot allocate memory for mem_ptr element",
					 __func__);
				return(-1);
			}
		} else
			dssize = (dinfo->numdevs * sizeof(struct devstat)) +
				 sizeof(long);

		/*
		 * Request all of the devices.  We only really allow for one
		 * ENOMEM failure.  It would, of course, be possible to just go
		 * in a loop and keep reallocing the device structure until we
		 * don't get ENOMEM back.  I'm not sure it's worth it, though.
		 * If devices are being added to the system that quickly, maybe
		 * the user can just wait until all devices are added.
		 */
		for (;;) {
			error = sysctlbyname("kern.devstat.all",
					     dinfo->mem_ptr, 
					     &dssize, NULL, 0);
			if (error != -1 || errno != EBUSY)
				break;
		}
		if (error == -1) {
			/*
			 * If we get ENOMEM back, that means that there are 
			 * more devices now, so we need to allocate more 
			 * space for the device array.
			 */
			if (errno == ENOMEM) {
				/*
				 * No need to set the error string here, 
				 * devstat_getnumdevs() will do that if it fails.
				 */
				if ((dinfo->numdevs = devstat_getnumdevs(kd)) < 0)
					return(-1);

				dssize = (dinfo->numdevs * 
					sizeof(struct devstat)) + sizeof(long);
				dinfo->mem_ptr = (u_int8_t *)
					realloc(dinfo->mem_ptr, dssize);
				if ((error = sysctlbyname("kern.devstat.all", 
				    dinfo->mem_ptr, &dssize, NULL, 0)) == -1) {
					snprintf(devstat_errbuf,
						 sizeof(devstat_errbuf),
					    	 "%s: error getting device "
					    	 "stats\n%s: %s", __func__,
					    	 __func__, strerror(errno));
					return(-1);
				}
			} else {
				snprintf(devstat_errbuf, sizeof(devstat_errbuf),
					 "%s: error getting device stats\n"
					 "%s: %s", __func__, __func__,
					 strerror(errno));
				return(-1);
			}
		} 

	} else {
		if (KREADNL(kd, X_TIME_UPTIME, ts.tv_sec) == -1)
			return(-1);
		else
			stats->snap_time = ts.tv_sec;

		/* 
		 * This is of course non-atomic, but since we are working
		 * on a core dump, the generation is unlikely to change
		 */
		if ((dinfo->numdevs = devstat_getnumdevs(kd)) == -1)
			return(-1);
		if ((dinfo->mem_ptr = (u_int8_t *)get_devstat_kvm(kd)) == NULL)
			return(-1);
	}
	/*
	 * The sysctl spits out the generation as the first four bytes,
	 * then all of the device statistics structures.
	 */
	dinfo->generation = *(long *)dinfo->mem_ptr;

	/*
	 * If the generation has changed, and if the current number of
	 * devices is not the same as the number of devices recorded in the
	 * devinfo structure, it is likely that the device list has shrunk.
	 * The reason that it is likely that the device list has shrunk in
	 * this case is that if the device list has grown, the sysctl above
	 * will return an ENOMEM error, and we will reset the number of
	 * devices and reallocate the device array.  If the second sysctl
	 * fails, we will return an error and therefore never get to this
	 * point.  If the device list has shrunk, the sysctl will not
	 * return an error since we have more space allocated than is
	 * necessary.  So, in the shrinkage case, we catch it here and
	 * reallocate the array so that we don't use any more space than is
	 * necessary.
	 */
	if (oldgeneration != dinfo->generation) {
		if (devstat_getnumdevs(kd) != dinfo->numdevs) {
			if ((dinfo->numdevs = devstat_getnumdevs(kd)) < 0)
				return(-1);
			dssize = (dinfo->numdevs * sizeof(struct devstat)) +
				sizeof(long);
			dinfo->mem_ptr = (u_int8_t *)realloc(dinfo->mem_ptr,
							     dssize);
		}
		retval = 1;
	}

	dinfo->devices = (struct devstat *)(dinfo->mem_ptr + sizeof(long));

	return(retval);
}

/*
 * selectdevs():
 *
 * Devices are selected/deselected based upon the following criteria:
 * - devices specified by the user on the command line
 * - devices matching any device type expressions given on the command line
 * - devices with the highest I/O, if 'top' mode is enabled
 * - the first n unselected devices in the device list, if maxshowdevs
 *   devices haven't already been selected and if the user has not
 *   specified any devices on the command line and if we're in "add" mode.
 *
 * Input parameters:
 * - device selection list (dev_select)
 * - current number of devices selected (num_selected)
 * - total number of devices in the selection list (num_selections)
 * - devstat generation as of the last time selectdevs() was called
 *   (select_generation)
 * - current devstat generation (current_generation)
 * - current list of devices and statistics (devices)
 * - number of devices in the current device list (numdevs)
 * - compiled version of the command line device type arguments (matches)
 *   - This is optional.  If the number of devices is 0, this will be ignored.
 *   - The matching code pays attention to the current selection mode.  So
 *     if you pass in a matching expression, it will be evaluated based
 *     upon the selection mode that is passed in.  See below for details.
 * - number of device type matching expressions (num_matches)
 *   - Set to 0 to disable the matching code.
 * - list of devices specified on the command line by the user (dev_selections)
 * - number of devices selected on the command line by the user
 *   (num_dev_selections)
 * - Our selection mode.  There are four different selection modes:
 *      - add mode.  (DS_SELECT_ADD) Any devices matching devices explicitly
 *        selected by the user or devices matching a pattern given by the
 *        user will be selected in addition to devices that are already
 *        selected.  Additional devices will be selected, up to maxshowdevs
 *        number of devices. 
 *      - only mode. (DS_SELECT_ONLY)  Only devices matching devices
 *        explicitly given by the user or devices matching a pattern
 *        given by the user will be selected.  No other devices will be
 *        selected.
 *      - addonly mode.  (DS_SELECT_ADDONLY)  This is similar to add and
 *        only.  Basically, this will not de-select any devices that are
 *        current selected, as only mode would, but it will also not
 *        gratuitously select up to maxshowdevs devices as add mode would.
 *      - remove mode.  (DS_SELECT_REMOVE)  Any devices matching devices
 *        explicitly selected by the user or devices matching a pattern
 *        given by the user will be de-selected.
 * - maximum number of devices we can select (maxshowdevs)
 * - flag indicating whether or not we're in 'top' mode (perf_select)
 *
 * Output data:
 * - the device selection list may be modified and passed back out
 * - the number of devices selected and the total number of items in the
 *   device selection list may be changed
 * - the selection generation may be changed to match the current generation
 * 
 * Return values:
 * -1  -- error
 *  0  -- selected devices are unchanged
 *  1  -- selected devices changed
 */
int
devstat_selectdevs(struct device_selection **dev_select, int *num_selected,
		   int *num_selections, long *select_generation, 
		   long current_generation, struct devstat *devices,
		   int numdevs, struct devstat_match *matches, int num_matches,
		   char **dev_selections, int num_dev_selections,
		   devstat_select_mode select_mode, int maxshowdevs,
		   int perf_select)
{
	int i, j, k;
	int init_selections = 0, init_selected_var = 0;
	struct device_selection *old_dev_select = NULL;
	int old_num_selections = 0, old_num_selected;
	int selection_number = 0;
	int changed = 0, found = 0;

	if ((dev_select == NULL) || (devices == NULL) || (numdevs < 0))
		return(-1);

	/*
	 * We always want to make sure that we have as many dev_select
	 * entries as there are devices. 
	 */
	/*
	 * In this case, we haven't selected devices before.
	 */
	if (*dev_select == NULL) {
		*dev_select = (struct device_selection *)malloc(numdevs *
			sizeof(struct device_selection));
		*select_generation = current_generation;
		init_selections = 1;
		changed = 1;
	/*
	 * In this case, we have selected devices before, but the device
	 * list has changed since we last selected devices, so we need to
	 * either enlarge or reduce the size of the device selection list.
	 */
	} else if (*num_selections != numdevs) {
		*dev_select = (struct device_selection *)reallocf(*dev_select,
			numdevs * sizeof(struct device_selection));
		*select_generation = current_generation;
		init_selections = 1;
	/*
	 * In this case, we've selected devices before, and the selection
	 * list is the same size as it was the last time, but the device
	 * list has changed.
	 */
	} else if (*select_generation < current_generation) {
		*select_generation = current_generation;
		init_selections = 1;
	}

	if (*dev_select == NULL) {
		snprintf(devstat_errbuf, sizeof(devstat_errbuf),
			 "%s: Cannot (re)allocate memory for dev_select argument",
			 __func__);
		return(-1);
	}

	/*
	 * If we're in "only" mode, we want to clear out the selected
	 * variable since we're going to select exactly what the user wants
	 * this time through.
	 */
	if (select_mode == DS_SELECT_ONLY)
		init_selected_var = 1;

	/*
	 * In all cases, we want to back up the number of selected devices.
	 * It is a quick and accurate way to determine whether the selected
	 * devices have changed.
	 */
	old_num_selected = *num_selected;

	/*
	 * We want to make a backup of the current selection list if 
	 * the list of devices has changed, or if we're in performance 
	 * selection mode.  In both cases, we don't want to make a backup
	 * if we already know for sure that the list will be different.
	 * This is certainly the case if this is our first time through the
	 * selection code.
	 */
	if (((init_selected_var != 0) || (init_selections != 0)
	 || (perf_select != 0)) && (changed == 0)){
		old_dev_select = (struct device_selection *)malloc(
		    *num_selections * sizeof(struct device_selection));
		if (old_dev_select == NULL) {
			snprintf(devstat_errbuf, sizeof(devstat_errbuf),
				 "%s: Cannot allocate memory for selection list backup",
				 __func__);
			return(-1);
		}
		old_num_selections = *num_selections;
		bcopy(*dev_select, old_dev_select, 
		    sizeof(struct device_selection) * *num_selections);
	}

	if (init_selections != 0) {
		bzero(*dev_select, sizeof(struct device_selection) * numdevs);

		for (i = 0; i < numdevs; i++) {
			(*dev_select)[i].device_number = 
				devices[i].device_number;
			strncpy((*dev_select)[i].device_name,
				devices[i].device_name,
				DEVSTAT_NAME_LEN);
			(*dev_select)[i].device_name[DEVSTAT_NAME_LEN - 1]='\0';
			(*dev_select)[i].unit_number = devices[i].unit_number;
			(*dev_select)[i].position = i;
		}
		*num_selections = numdevs;
	} else if (init_selected_var != 0) {
		for (i = 0; i < numdevs; i++) 
			(*dev_select)[i].selected = 0;
	}

	/* we haven't gotten around to selecting anything yet.. */
	if ((select_mode == DS_SELECT_ONLY) || (init_selections != 0)
	 || (init_selected_var != 0))
		*num_selected = 0;

	/*
	 * Look through any devices the user specified on the command line
	 * and see if they match known devices.  If so, select them.
	 */
	for (i = 0; (i < *num_selections) && (num_dev_selections > 0); i++) {
		char tmpstr[80];

		snprintf(tmpstr, sizeof(tmpstr), "%s%d",
			 (*dev_select)[i].device_name,
			 (*dev_select)[i].unit_number);
		for (j = 0; j < num_dev_selections; j++) {
			if (strcmp(tmpstr, dev_selections[j]) == 0) {
				/*
				 * Here we do different things based on the
				 * mode we're in.  If we're in add or
				 * addonly mode, we only select this device
				 * if it hasn't already been selected.
				 * Otherwise, we would be unnecessarily
				 * changing the selection order and
				 * incrementing the selection count.  If
				 * we're in only mode, we unconditionally
				 * select this device, since in only mode
				 * any previous selections are erased and
				 * manually specified devices are the first
				 * ones to be selected.  If we're in remove
				 * mode, we de-select the specified device and
				 * decrement the selection count.
				 */
				switch(select_mode) {
				case DS_SELECT_ADD:
				case DS_SELECT_ADDONLY:
					if ((*dev_select)[i].selected)
						break;
					/* FALLTHROUGH */
				case DS_SELECT_ONLY:
					(*dev_select)[i].selected =
						++selection_number;
					(*num_selected)++;
					break;
				case DS_SELECT_REMOVE:
					(*dev_select)[i].selected = 0;
					(*num_selected)--;
					/*
					 * This isn't passed back out, we
					 * just use it to keep track of
					 * how many devices we've removed.
					 */
					num_dev_selections--;
					break;
				}
				break;
			}
		}
	}

	/*
	 * Go through the user's device type expressions and select devices
	 * accordingly.  We only do this if the number of devices already
	 * selected is less than the maximum number we can show.
	 */
	for (i = 0; (i < num_matches) && (*num_selected < maxshowdevs); i++) {
		/* We should probably indicate some error here */
		if ((matches[i].match_fields == DEVSTAT_MATCH_NONE)
		 || (matches[i].num_match_categories <= 0))
			continue;

		for (j = 0; j < numdevs; j++) {
			int num_match_categories;

			num_match_categories = matches[i].num_match_categories;

			/*
			 * Determine whether or not the current device
			 * matches the given matching expression.  This if
			 * statement consists of three components:
			 *   - the device type check
			 *   - the device interface check
			 *   - the passthrough check
			 * If a the matching test is successful, it 
			 * decrements the number of matching categories,
			 * and if we've reached the last element that
			 * needed to be matched, the if statement succeeds.
			 * 
			 */
			if ((((matches[i].match_fields & DEVSTAT_MATCH_TYPE)!=0)
			  && ((devices[j].device_type & DEVSTAT_TYPE_MASK) ==
			        (matches[i].device_type & DEVSTAT_TYPE_MASK))
			  &&(((matches[i].match_fields & DEVSTAT_MATCH_PASS)!=0)
			   || (((matches[i].match_fields & 
				DEVSTAT_MATCH_PASS) == 0)
			    && ((devices[j].device_type &
			        DEVSTAT_TYPE_PASS) == 0)))
			  && (--num_match_categories == 0)) 
			 || (((matches[i].match_fields & DEVSTAT_MATCH_IF) != 0)
			  && ((devices[j].device_type & DEVSTAT_TYPE_IF_MASK) ==
			        (matches[i].device_type & DEVSTAT_TYPE_IF_MASK))
			  &&(((matches[i].match_fields & DEVSTAT_MATCH_PASS)!=0)
			   || (((matches[i].match_fields &
				DEVSTAT_MATCH_PASS) == 0)
			    && ((devices[j].device_type & 
				DEVSTAT_TYPE_PASS) == 0)))
			  && (--num_match_categories == 0))
			 || (((matches[i].match_fields & DEVSTAT_MATCH_PASS)!=0)
			  && ((devices[j].device_type & DEVSTAT_TYPE_PASS) != 0)
			  && (--num_match_categories == 0))) {

				/*
				 * This is probably a non-optimal solution
				 * to the problem that the devices in the
				 * device list will not be in the same
				 * order as the devices in the selection
				 * array.
				 */
				for (k = 0; k < numdevs; k++) {
					if ((*dev_select)[k].position == j) {
						found = 1;
						break;
					}
				}

				/*
				 * There shouldn't be a case where a device
				 * in the device list is not in the
				 * selection list...but it could happen.
				 */
				if (found != 1) {
					fprintf(stderr, "selectdevs: couldn't"
						" find %s%d in selection "
						"list\n",
						devices[j].device_name,
						devices[j].unit_number);
					break;
				}

				/*
				 * We do different things based upon the
				 * mode we're in.  If we're in add or only
				 * mode, we go ahead and select this device
				 * if it hasn't already been selected.  If
				 * it has already been selected, we leave
				 * it alone so we don't mess up the
				 * selection ordering.  Manually specified
				 * devices have already been selected, and
				 * they have higher priority than pattern
				 * matched devices.  If we're in remove
				 * mode, we de-select the given device and
				 * decrement the selected count.
				 */
				switch(select_mode) {
				case DS_SELECT_ADD:
				case DS_SELECT_ADDONLY:
				case DS_SELECT_ONLY:
					if ((*dev_select)[k].selected != 0)
						break;
					(*dev_select)[k].selected =
						++selection_number;
					(*num_selected)++;
					break;
				case DS_SELECT_REMOVE:
					(*dev_select)[k].selected = 0;
					(*num_selected)--;
					break;
				}
			}
		}
	}

	/*
	 * Here we implement "top" mode.  Devices are sorted in the
	 * selection array based on two criteria:  whether or not they are
	 * selected (not selection number, just the fact that they are
	 * selected!) and the number of bytes in the "bytes" field of the
	 * selection structure.  The bytes field generally must be kept up
	 * by the user.  In the future, it may be maintained by library
	 * functions, but for now the user has to do the work.
	 *
	 * At first glance, it may seem wrong that we don't go through and
	 * select every device in the case where the user hasn't specified
	 * any devices or patterns.  In fact, though, it won't make any
	 * difference in the device sorting.  In that particular case (i.e.
	 * when we're in "add" or "only" mode, and the user hasn't
	 * specified anything) the first time through no devices will be
	 * selected, so the only criterion used to sort them will be their
	 * performance.  The second time through, and every time thereafter,
	 * all devices will be selected, so again selection won't matter.
	 */
	if (perf_select != 0) {

		/* Sort the device array by throughput  */
		qsort(*dev_select, *num_selections,
		      sizeof(struct device_selection),
		      compare_select);

		if (*num_selected == 0) {
			/*
			 * Here we select every device in the array, if it
			 * isn't already selected.  Because the 'selected'
			 * variable in the selection array entries contains
			 * the selection order, the devstats routine can show
			 * the devices that were selected first.
			 */
			for (i = 0; i < *num_selections; i++) {
				if ((*dev_select)[i].selected == 0) {
					(*dev_select)[i].selected =
						++selection_number;
					(*num_selected)++;
				}
			}
		} else {
			selection_number = 0;
			for (i = 0; i < *num_selections; i++) {
				if ((*dev_select)[i].selected != 0) {
					(*dev_select)[i].selected =
						++selection_number;
				}
			}
		}
	}

	/*
	 * If we're in the "add" selection mode and if we haven't already
	 * selected maxshowdevs number of devices, go through the array and
	 * select any unselected devices.  If we're in "only" mode, we
	 * obviously don't want to select anything other than what the user
	 * specifies.  If we're in "remove" mode, it probably isn't a good
	 * idea to go through and select any more devices, since we might
	 * end up selecting something that the user wants removed.  Through
	 * more complicated logic, we could actually figure this out, but
	 * that would probably require combining this loop with the various
	 * selections loops above.
	 */
	if ((select_mode == DS_SELECT_ADD) && (*num_selected < maxshowdevs)) {
		for (i = 0; i < *num_selections; i++)
			if ((*dev_select)[i].selected == 0) {
				(*dev_select)[i].selected = ++selection_number;
				(*num_selected)++;
			}
	}

	/*
	 * Look at the number of devices that have been selected.  If it
	 * has changed, set the changed variable.  Otherwise, if we've
	 * made a backup of the selection list, compare it to the current
	 * selection list to see if the selected devices have changed.
	 */
	if ((changed == 0) && (old_num_selected != *num_selected))
		changed = 1;
	else if ((changed == 0) && (old_dev_select != NULL)) {
		/*
		 * Now we go through the selection list and we look at
		 * it three different ways.
		 */
		for (i = 0; (i < *num_selections) && (changed == 0) && 
		     (i < old_num_selections); i++) {
			/*
			 * If the device at index i in both the new and old
			 * selection arrays has the same device number and
			 * selection status, it hasn't changed.  We
			 * continue on to the next index.
			 */
			if (((*dev_select)[i].device_number ==
			     old_dev_select[i].device_number)
			 && ((*dev_select)[i].selected == 
			     old_dev_select[i].selected))
				continue;

			/*
			 * Now, if we're still going through the if
			 * statement, the above test wasn't true.  So we
			 * check here to see if the device at index i in
			 * the current array is the same as the device at
			 * index i in the old array.  If it is, that means
			 * that its selection number has changed.  Set
			 * changed to 1 and exit the loop.
			 */
			else if ((*dev_select)[i].device_number ==
			          old_dev_select[i].device_number) {
				changed = 1;
				break;
			}
			/*
			 * If we get here, then the device at index i in
			 * the current array isn't the same device as the
			 * device at index i in the old array.
			 */
			else {
				found = 0;

				/*
				 * Search through the old selection array
				 * looking for a device with the same
				 * device number as the device at index i
				 * in the current array.  If the selection
				 * status is the same, then we mark it as
				 * found.  If the selection status isn't
				 * the same, we break out of the loop.
				 * Since found isn't set, changed will be
				 * set to 1 below.
				 */
				for (j = 0; j < old_num_selections; j++) {
					if (((*dev_select)[i].device_number ==
					      old_dev_select[j].device_number)
					 && ((*dev_select)[i].selected ==
					      old_dev_select[j].selected)){
						found = 1;
						break;
					}
					else if ((*dev_select)[i].device_number
					    == old_dev_select[j].device_number)
						break;
				}
				if (found == 0)
					changed = 1;
			}
		}
	}
	if (old_dev_select != NULL)
		free(old_dev_select);

	return(changed);
}

/*
 * Comparison routine for qsort() above.  Note that the comparison here is
 * backwards -- generally, it should return a value to indicate whether
 * arg1 is <, =, or > arg2.  Instead, it returns the opposite.  The reason
 * it returns the opposite is so that the selection array will be sorted in
 * order of decreasing performance.  We sort on two parameters.  The first
 * sort key is whether or not one or the other of the devices in question
 * has been selected.  If one of them has, and the other one has not, the
 * selected device is automatically more important than the unselected
 * device.  If neither device is selected, we judge the devices based upon
 * performance.
 */
static int
compare_select(const void *arg1, const void *arg2)
{
	if ((((const struct device_selection *)arg1)->selected)
	 && (((const struct device_selection *)arg2)->selected == 0))
		return(-1);
	else if ((((const struct device_selection *)arg1)->selected == 0)
	      && (((const struct device_selection *)arg2)->selected))
		return(1);
	else if (((const struct device_selection *)arg2)->bytes <
	         ((const struct device_selection *)arg1)->bytes)
		return(-1);
	else if (((const struct device_selection *)arg2)->bytes >
		 ((const struct device_selection *)arg1)->bytes)
		return(1);
	else
		return(0);
}

/*
 * Take a string with the general format "arg1,arg2,arg3", and build a
 * device matching expression from it.
 */
int
devstat_buildmatch(char *match_str, struct devstat_match **matches,
		   int *num_matches)
{
	char *tstr[5];
	char **tempstr;
	int num_args;
	int i, j;

	/* We can't do much without a string to parse */
	if (match_str == NULL) {
		snprintf(devstat_errbuf, sizeof(devstat_errbuf),
			 "%s: no match expression", __func__);
		return(-1);
	}

	/*
	 * Break the (comma delimited) input string out into separate strings.
	 */
	for (tempstr = tstr, num_args  = 0; 
	     (*tempstr = strsep(&match_str, ",")) != NULL && (num_args < 5);)
		if (**tempstr != '\0') {
			num_args++;
			if (++tempstr >= &tstr[5])
				break;
		}

	/* The user gave us too many type arguments */
	if (num_args > 3) {
		snprintf(devstat_errbuf, sizeof(devstat_errbuf),
			 "%s: too many type arguments", __func__);
		return(-1);
	}

	if (*num_matches == 0)
		*matches = NULL;

	*matches = (struct devstat_match *)reallocf(*matches,
		  sizeof(struct devstat_match) * (*num_matches + 1));

	if (*matches == NULL) {
		snprintf(devstat_errbuf, sizeof(devstat_errbuf),
			 "%s: Cannot allocate memory for matches list", __func__);
		return(-1);
	}
			  
	/* Make sure the current entry is clear */
	bzero(&matches[0][*num_matches], sizeof(struct devstat_match));

	/*
	 * Step through the arguments the user gave us and build a device
	 * matching expression from them.
	 */
	for (i = 0; i < num_args; i++) {
		char *tempstr2, *tempstr3;

		/*
		 * Get rid of leading white space.
		 */
		tempstr2 = tstr[i];
		while (isspace(*tempstr2) && (*tempstr2 != '\0'))
			tempstr2++;

		/*
		 * Get rid of trailing white space.
		 */
		tempstr3 = &tempstr2[strlen(tempstr2) - 1];

		while ((*tempstr3 != '\0') && (tempstr3 > tempstr2)
		    && (isspace(*tempstr3))) {
			*tempstr3 = '\0';
			tempstr3--;
		}

		/*
		 * Go through the match table comparing the user's
		 * arguments to known device types, interfaces, etc.  
		 */
		for (j = 0; match_table[j].match_str != NULL; j++) {
			/*
			 * We do case-insensitive matching, in case someone
			 * wants to enter "SCSI" instead of "scsi" or
			 * something like that.  Only compare as many 
			 * characters as are in the string in the match 
			 * table.  This should help if someone tries to use 
			 * a super-long match expression.  
			 */
			if (strncasecmp(tempstr2, match_table[j].match_str,
			    strlen(match_table[j].match_str)) == 0) {
				/*
				 * Make sure the user hasn't specified two
				 * items of the same type, like "da" and
				 * "cd".  One device cannot be both.
				 */
				if (((*matches)[*num_matches].match_fields &
				    match_table[j].match_field) != 0) {
					snprintf(devstat_errbuf,
						 sizeof(devstat_errbuf),
						 "%s: cannot have more than "
						 "one match item in a single "
						 "category", __func__);
					return(-1);
				}
				/*
				 * If we've gotten this far, we have a
				 * winner.  Set the appropriate fields in
				 * the match entry.
				 */
				(*matches)[*num_matches].match_fields |=
					match_table[j].match_field;
				(*matches)[*num_matches].device_type |=
					match_table[j].type;
				(*matches)[*num_matches].num_match_categories++;
				break;
			}
		}
		/*
		 * We should have found a match in the above for loop.  If
		 * not, that means the user entered an invalid device type
		 * or interface.
		 */
		if ((*matches)[*num_matches].num_match_categories != (i + 1)) {
			snprintf(devstat_errbuf, sizeof(devstat_errbuf),
				 "%s: unknown match item \"%s\"", __func__,
				 tstr[i]);
			return(-1);
		}
	}

	(*num_matches)++;

	return(0);
}

/*
 * Compute a number of device statistics.  Only one field is mandatory, and
 * that is "current".  Everything else is optional.  The caller passes in
 * pointers to variables to hold the various statistics he desires.  If he
 * doesn't want a particular staistic, he should pass in a NULL pointer.
 * Return values:
 * 0   -- success
 * -1  -- failure
 */
int
compute_stats(struct devstat *current, struct devstat *previous,
	      long double etime, u_int64_t *total_bytes,
	      u_int64_t *total_transfers, u_int64_t *total_blocks,
	      long double *kb_per_transfer, long double *transfers_per_second,
	      long double *mb_per_second, long double *blocks_per_second,
	      long double *ms_per_transaction)
{
	return(devstat_compute_statistics(current, previous, etime,
	       total_bytes ? DSM_TOTAL_BYTES : DSM_SKIP,
	       total_bytes,
	       total_transfers ? DSM_TOTAL_TRANSFERS : DSM_SKIP,
	       total_transfers,
	       total_blocks ? DSM_TOTAL_BLOCKS : DSM_SKIP,
	       total_blocks,
	       kb_per_transfer ? DSM_KB_PER_TRANSFER : DSM_SKIP,
	       kb_per_transfer,
	       transfers_per_second ? DSM_TRANSFERS_PER_SECOND : DSM_SKIP,
	       transfers_per_second,
	       mb_per_second ? DSM_MB_PER_SECOND : DSM_SKIP,
	       mb_per_second,
	       blocks_per_second ? DSM_BLOCKS_PER_SECOND : DSM_SKIP,
	       blocks_per_second,
	       ms_per_transaction ? DSM_MS_PER_TRANSACTION : DSM_SKIP,
	       ms_per_transaction,
	       DSM_NONE));
}


/* This is 1/2^64 */
#define BINTIME_SCALE 5.42101086242752217003726400434970855712890625e-20

long double
devstat_compute_etime(struct bintime *cur_time, struct bintime *prev_time)
{
	long double etime;

	etime = cur_time->sec;
	etime += cur_time->frac * BINTIME_SCALE;
	if (prev_time != NULL) {
		etime -= prev_time->sec;
		etime -= prev_time->frac * BINTIME_SCALE;
	}
	return(etime);
}

#define DELTA(field, index)				\
	(current->field[(index)] - (previous ? previous->field[(index)] : 0))

#define DELTA_T(field)					\
	devstat_compute_etime(&current->field,  	\
	(previous ? &previous->field : NULL))

int
devstat_compute_statistics(struct devstat *current, struct devstat *previous,
			   long double etime, ...)
{
	u_int64_t totalbytes, totalbytesread, totalbyteswrite, totalbytesfree;
	u_int64_t totaltransfers, totaltransfersread, totaltransferswrite;
	u_int64_t totaltransfersother, totalblocks, totalblocksread;
	u_int64_t totalblockswrite, totaltransfersfree, totalblocksfree;
	long double totalduration, totaldurationread, totaldurationwrite;
	long double totaldurationfree, totaldurationother;
	va_list ap;
	devstat_metric metric;
	u_int64_t *destu64;
	long double *destld;
	int retval;

	retval = 0;

	/*
	 * current is the only mandatory field.
	 */
	if (current == NULL) {
		snprintf(devstat_errbuf, sizeof(devstat_errbuf),
			 "%s: current stats structure was NULL", __func__);
		return(-1);
	}

	totalbytesread = DELTA(bytes, DEVSTAT_READ);
	totalbyteswrite = DELTA(bytes, DEVSTAT_WRITE);
	totalbytesfree = DELTA(bytes, DEVSTAT_FREE);
	totalbytes = totalbytesread + totalbyteswrite + totalbytesfree;

	totaltransfersread = DELTA(operations, DEVSTAT_READ);
	totaltransferswrite = DELTA(operations, DEVSTAT_WRITE);
	totaltransfersother = DELTA(operations, DEVSTAT_NO_DATA);
	totaltransfersfree = DELTA(operations, DEVSTAT_FREE);
	totaltransfers = totaltransfersread + totaltransferswrite +
			 totaltransfersother + totaltransfersfree;

	totalblocks = totalbytes;
	totalblocksread = totalbytesread;
	totalblockswrite = totalbyteswrite;
	totalblocksfree = totalbytesfree;

	if (current->block_size > 0) {
		totalblocks /= current->block_size;
		totalblocksread /= current->block_size;
		totalblockswrite /= current->block_size;
		totalblocksfree /= current->block_size;
	} else {
		totalblocks /= 512;
		totalblocksread /= 512;
		totalblockswrite /= 512;
		totalblocksfree /= 512;
	}

	totaldurationread = DELTA_T(duration[DEVSTAT_READ]);
	totaldurationwrite = DELTA_T(duration[DEVSTAT_WRITE]);
	totaldurationfree = DELTA_T(duration[DEVSTAT_FREE]);
	totaldurationother = DELTA_T(duration[DEVSTAT_NO_DATA]);
	totalduration = totaldurationread + totaldurationwrite +
	    totaldurationfree + totaldurationother;

	va_start(ap, etime);

	while ((metric = (devstat_metric)va_arg(ap, devstat_metric)) != 0) {

		if (metric == DSM_NONE)
			break;

		if (metric >= DSM_MAX) {
			snprintf(devstat_errbuf, sizeof(devstat_errbuf),
				 "%s: metric %d is out of range", __func__,
				 metric);
			retval = -1;
			goto bailout;
		}

		switch (devstat_arg_list[metric].argtype) {
		case DEVSTAT_ARG_UINT64:
			destu64 = (u_int64_t *)va_arg(ap, u_int64_t *);
			break;
		case DEVSTAT_ARG_LD:
			destld = (long double *)va_arg(ap, long double *);
			break;
		case DEVSTAT_ARG_SKIP:
			destld = (long double *)va_arg(ap, long double *);
			break;
		default:
			retval = -1;
			goto bailout;
			break; /* NOTREACHED */
		}

		if (devstat_arg_list[metric].argtype == DEVSTAT_ARG_SKIP)
			continue;

		switch (metric) {
		case DSM_TOTAL_BYTES:
			*destu64 = totalbytes;
			break;
		case DSM_TOTAL_BYTES_READ:
			*destu64 = totalbytesread;
			break;
		case DSM_TOTAL_BYTES_WRITE:
			*destu64 = totalbyteswrite;
			break;
		case DSM_TOTAL_BYTES_FREE:
			*destu64 = totalbytesfree;
			break;
		case DSM_TOTAL_TRANSFERS:
			*destu64 = totaltransfers;
			break;
		case DSM_TOTAL_TRANSFERS_READ:
			*destu64 = totaltransfersread;
			break;
		case DSM_TOTAL_TRANSFERS_WRITE:
			*destu64 = totaltransferswrite;
			break;
		case DSM_TOTAL_TRANSFERS_FREE:
			*destu64 = totaltransfersfree;
			break;
		case DSM_TOTAL_TRANSFERS_OTHER:
			*destu64 = totaltransfersother;
			break;
		case DSM_TOTAL_BLOCKS:
			*destu64 = totalblocks;
			break;
		case DSM_TOTAL_BLOCKS_READ:
			*destu64 = totalblocksread;
			break;
		case DSM_TOTAL_BLOCKS_WRITE:
			*destu64 = totalblockswrite;
			break;
		case DSM_TOTAL_BLOCKS_FREE:
			*destu64 = totalblocksfree;
			break;
		case DSM_KB_PER_TRANSFER:
			*destld = totalbytes;
			*destld /= 1024;
			if (totaltransfers > 0)
				*destld /= totaltransfers;
			else
				*destld = 0.0;
			break;
		case DSM_KB_PER_TRANSFER_READ:
			*destld = totalbytesread;
			*destld /= 1024;
			if (totaltransfersread > 0)
				*destld /= totaltransfersread;
			else
				*destld = 0.0;
			break;
		case DSM_KB_PER_TRANSFER_WRITE:
			*destld = totalbyteswrite;
			*destld /= 1024;
			if (totaltransferswrite > 0)
				*destld /= totaltransferswrite;
			else
				*destld = 0.0;
			break;
		case DSM_KB_PER_TRANSFER_FREE:
			*destld = totalbytesfree;
			*destld /= 1024;
			if (totaltransfersfree > 0)
				*destld /= totaltransfersfree;
			else
				*destld = 0.0;
			break;
		case DSM_TRANSFERS_PER_SECOND:
			if (etime > 0.0) {
				*destld = totaltransfers;
				*destld /= etime;
			} else
				*destld = 0.0;
			break;
		case DSM_TRANSFERS_PER_SECOND_READ:
			if (etime > 0.0) {
				*destld = totaltransfersread;
				*destld /= etime;
			} else
				*destld = 0.0;
			break;
		case DSM_TRANSFERS_PER_SECOND_WRITE:
			if (etime > 0.0) {
				*destld = totaltransferswrite;
				*destld /= etime;
			} else
				*destld = 0.0;
			break;
		case DSM_TRANSFERS_PER_SECOND_FREE:
			if (etime > 0.0) {
				*destld = totaltransfersfree;
				*destld /= etime;
			} else
				*destld = 0.0;
			break;
		case DSM_TRANSFERS_PER_SECOND_OTHER:
			if (etime > 0.0) {
				*destld = totaltransfersother;
				*destld /= etime;
			} else
				*destld = 0.0;
			break;
		case DSM_MB_PER_SECOND:
			*destld = totalbytes;
			*destld /= 1024 * 1024;
			if (etime > 0.0)
				*destld /= etime;
			else
				*destld = 0.0;
			break;
		case DSM_MB_PER_SECOND_READ:
			*destld = totalbytesread;
			*destld /= 1024 * 1024;
			if (etime > 0.0)
				*destld /= etime;
			else
				*destld = 0.0;
			break;
		case DSM_MB_PER_SECOND_WRITE:
			*destld = totalbyteswrite;
			*destld /= 1024 * 1024;
			if (etime > 0.0)
				*destld /= etime;
			else
				*destld = 0.0;
			break;
		case DSM_MB_PER_SECOND_FREE:
			*destld = totalbytesfree;
			*destld /= 1024 * 1024;
			if (etime > 0.0)
				*destld /= etime;
			else
				*destld = 0.0;
			break;
		case DSM_BLOCKS_PER_SECOND:
			*destld = totalblocks;
			if (etime > 0.0)
				*destld /= etime;
			else
				*destld = 0.0;
			break;
		case DSM_BLOCKS_PER_SECOND_READ:
			*destld = totalblocksread;
			if (etime > 0.0)
				*destld /= etime;
			else
				*destld = 0.0;
			break;
		case DSM_BLOCKS_PER_SECOND_WRITE:
			*destld = totalblockswrite;
			if (etime > 0.0)
				*destld /= etime;
			else
				*destld = 0.0;
			break;
		case DSM_BLOCKS_PER_SECOND_FREE:
			*destld = totalblocksfree;
			if (etime > 0.0)
				*destld /= etime;
			else
				*destld = 0.0;
			break;
		/*
		 * Some devstat callers update the duration and some don't.
		 * So this will only be accurate if they provide the
		 * duration. 
		 */
		case DSM_MS_PER_TRANSACTION:
			if (totaltransfers > 0) {
				*destld = totalduration;
				*destld /= totaltransfers;
				*destld *= 1000;
			} else
				*destld = 0.0;
			break;
		case DSM_MS_PER_TRANSACTION_READ:
			if (totaltransfersread > 0) {
				*destld = totaldurationread;
				*destld /= totaltransfersread;
				*destld *= 1000;
			} else
				*destld = 0.0;
			break;
		case DSM_MS_PER_TRANSACTION_WRITE:
			if (totaltransferswrite > 0) {
				*destld = totaldurationwrite;
				*destld /= totaltransferswrite;
				*destld *= 1000;
			} else
				*destld = 0.0;
			break;
		case DSM_MS_PER_TRANSACTION_FREE:
			if (totaltransfersfree > 0) {
				*destld = totaldurationfree;
				*destld /= totaltransfersfree;
				*destld *= 1000;
			} else
				*destld = 0.0;
			break;
		case DSM_MS_PER_TRANSACTION_OTHER:
			if (totaltransfersother > 0) {
				*destld = totaldurationother;
				*destld /= totaltransfersother;
				*destld *= 1000;
			} else
				*destld = 0.0;
			break;
		case DSM_BUSY_PCT:
			*destld = DELTA_T(busy_time);
			if (*destld < 0)
				*destld = 0;
			*destld /= etime;
			*destld *= 100;
			if (*destld < 0)
				*destld = 0;
			break;
		case DSM_QUEUE_LENGTH:
			*destu64 = current->start_count - current->end_count;
			break;
		case DSM_TOTAL_DURATION:
			*destld = totalduration;
			break;
		case DSM_TOTAL_DURATION_READ:
			*destld = totaldurationread;
			break;
		case DSM_TOTAL_DURATION_WRITE:
			*destld = totaldurationwrite;
			break;
		case DSM_TOTAL_DURATION_FREE:
			*destld = totaldurationfree;
			break;
		case DSM_TOTAL_DURATION_OTHER:
			*destld = totaldurationother;
			break;
		case DSM_TOTAL_BUSY_TIME:
			*destld = DELTA_T(busy_time);
			break;
/*
 * XXX: comment out the default block to see if any case's are missing.
 */
#if 1
		default:
			/*
			 * This shouldn't happen, since we should have
			 * caught any out of range metrics at the top of
			 * the loop.
			 */
			snprintf(devstat_errbuf, sizeof(devstat_errbuf),
				 "%s: unknown metric %d", __func__, metric);
			retval = -1;
			goto bailout;
			break; /* NOTREACHED */
#endif
		}
	}

bailout:

	va_end(ap);
	return(retval);
}

static int 
readkmem(kvm_t *kd, unsigned long addr, void *buf, size_t nbytes)
{

	if (kvm_read(kd, addr, buf, nbytes) == -1) {
		snprintf(devstat_errbuf, sizeof(devstat_errbuf),
			 "%s: error reading value (kvm_read): %s", __func__,
			 kvm_geterr(kd));
		return(-1);
	}
	return(0);
}

static int
readkmem_nl(kvm_t *kd, const char *name, void *buf, size_t nbytes)
{
	struct nlist nl[2];

	nl[0].n_name = (char *)name;
	nl[1].n_name = NULL;

	if (kvm_nlist(kd, nl) == -1) {
		snprintf(devstat_errbuf, sizeof(devstat_errbuf),
			 "%s: error getting name list (kvm_nlist): %s",
			 __func__, kvm_geterr(kd));
		return(-1);
	}
	return(readkmem(kd, nl[0].n_value, buf, nbytes));
}

/*
 * This duplicates the functionality of the kernel sysctl handler for poking
 * through crash dumps.
 */
static char *
get_devstat_kvm(kvm_t *kd)
{
	int i, wp;
	long gen;
	struct devstat *nds;
	struct devstat ds;
	struct devstatlist dhead;
	int num_devs;
	char *rv = NULL;

	if ((num_devs = devstat_getnumdevs(kd)) <= 0)
		return(NULL);
	if (KREADNL(kd, X_DEVICE_STATQ, dhead) == -1)
		return(NULL);

	nds = STAILQ_FIRST(&dhead);
	
	if ((rv = malloc(sizeof(gen))) == NULL) {
		snprintf(devstat_errbuf, sizeof(devstat_errbuf), 
			 "%s: out of memory (initial malloc failed)",
			 __func__);
		return(NULL);
	}
	gen = devstat_getgeneration(kd);
	memcpy(rv, &gen, sizeof(gen));
	wp = sizeof(gen);
	/*
	 * Now push out all the devices.
	 */
	for (i = 0; (nds != NULL) && (i < num_devs);  
	     nds = STAILQ_NEXT(nds, dev_links), i++) {
		if (readkmem(kd, (long)nds, &ds, sizeof(ds)) == -1) {
			free(rv);
			return(NULL);
		}
		nds = &ds;
		rv = (char *)reallocf(rv, sizeof(gen) + 
				      sizeof(ds) * (i + 1));
		if (rv == NULL) {
			snprintf(devstat_errbuf, sizeof(devstat_errbuf), 
				 "%s: out of memory (malloc failed)",
				 __func__);
			return(NULL);
		}
		memcpy(rv + wp, &ds, sizeof(ds));
		wp += sizeof(ds);
	}
	return(rv);
}
