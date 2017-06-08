/*
 *  (C) 2010,2011       Thomas Renninger <trenn@suse.de>, Novell Inc.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 * ToDo: Needs to be done more properly for AMD/Intel specifics
 */

/* Helper struct for qsort, must be in sync with cpupower_topology.cpu_info */
/* Be careful: Need to pass unsigned to the sort, so that offlined cores are
   in the end, but double check for -1 for offlined cpus at other places */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <cpuidle.h>

/* CPU topology/hierarchy parsing ******************/

