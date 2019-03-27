/*
 * Copyright (c) 2005-2006 The FreeBSD Project
 * All rights reserved.
 *
 * Author: Victor Cruceru <soc-victor@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * Host Resources MIB for SNMPd.
 *
 * $FreeBSD$
 */

#ifndef HOSTRES_SNMP_H_1132245017
#define	HOSTRES_SNMP_H_1132245017

#include <sys/types.h>
#include <sys/queue.h>

#include <stdio.h>
#include <fcntl.h>
#include <kvm.h>
#include <devinfo.h>

#include <bsnmp/asn1.h>
#include <bsnmp/snmp.h>

#include <bsnmp/snmpmod.h>

/*
 * Default package directory for hrSWInstalledTable. Can be overridden
 * via SNMP or configuration file.
 */
#define	PATH_PKGDIR     "/var/db/pkg"

/*
 * These are the default maximum caching intervals for the various tables
 * in seconds. They can be overridden from the configuration file.
 */
#define	HR_STORAGE_TBL_REFRESH	7
#define	HR_FS_TBL_REFRESH	7
#define	HR_DISK_TBL_REFRESH	7
#define	HR_NETWORK_TBL_REFRESH	7
#define	HR_SWINS_TBL_REFRESH	120
#define	HR_SWRUN_TBL_REFRESH	3

struct tm;
struct statfs;

/* a debug macro */
#ifndef NDEBUG

#define	HRDBG(...) do {							\
	fprintf(stderr, "HRDEBUG: %s: ", __func__);			\
	fprintf(stderr, __VA_ARGS__);					\
	fprintf(stderr, "\n");						\
   } while (0)

#else

#define	HRDBG(...) do { } while (0)

#endif /*NDEBUG*/

/* path to devd(8) output pipe */
#define	PATH_DEVD_PIPE	"/var/run/devd.pipe"

#define	IS_KERNPROC(kp)	(((kp)->ki_flag & P_KPROC) == P_KPROC)

enum snmpTCTruthValue {
	SNMP_TRUE = 1,
	SNMP_FALSE= 2
};

/* The number of CPU load samples per one minute, per each CPU */
#define	MAX_CPU_SAMPLES 4


/*
 * max len (including '\0'), for device_entry::descr field below,
 * according to MIB
 */
#define	DEV_DESCR_MLEN	(64 + 1)

/*
 * max len (including '\0'), for device_entry::name and
 * device_map_entry::name_key fields below, according to MIB
 */
#define	DEV_NAME_MLEN	(32 + 1)

/*
 * max len (including '\0'), for device_entry::location and
 * device_map_entry::location_key fields below, according to MIB
 */
#define	DEV_LOC_MLEN	(128 + 1)

/*
 * This structure is used to hold a SNMP table entry
 * for HOST-RESOURCES-MIB's hrDeviceTable
 */
struct device_entry {
	int32_t		index;
	const struct asn_oid *type;
	u_char		*descr;
	const struct asn_oid *id;	/* only oid_zeroDotZero as (*id) value*/
	int32_t		status;		/* enum DeviceStatus */
	uint32_t	errors;

#define	HR_DEVICE_FOUND		0x001
	/* not dectected by libdevice, so don't try to refresh it*/
#define	HR_DEVICE_IMMUTABLE	0x002

	/* next 3 are not from the SNMP mib table, only to be used internally */
	uint32_t	flags;

	u_char		*name;
	u_char		*location;
	TAILQ_ENTRY(device_entry) link;
};

/*
 * Next structure is used to keep o list of mappings from a specific
 * name (a_name) to an entry in the hrFSTblEntry;
 * We are trying to keep the same index for a specific name at least
 * for the duration of one SNMP agent run.
 */
struct device_map_entry {
	int32_t		hrIndex;	/* used for hrDeviceTblEntry::index */

	/* map key is the pair (name_key, location_key) */
	u_char		*name_key;	/* copy of device name */
	u_char		*location_key;

	/*
	 * Next may be NULL if the respective hrDeviceTblEntry
	 * is (temporally) gone.
	 */
	struct device_entry *entry_p;
	STAILQ_ENTRY(device_map_entry) link;
};
STAILQ_HEAD(device_map, device_map_entry);

/* descriptor to access kernel memory */
extern kvm_t *hr_kd;

/* Table used for consistent device table indexing. */
extern struct device_map device_map;

/* Maximum number of ticks between two updates for hrStorageTable */
extern uint32_t storage_tbl_refresh;

/* Maximum number of ticks between updated of FS table */
extern uint32_t fs_tbl_refresh;

/* maximum number of ticks between updates of SWRun and SWRunPerf table */
extern uint32_t swrun_tbl_refresh;

/* Maximum number of ticks between device table refreshs. */
extern uint32_t device_tbl_refresh;

/* maximum number of ticks between refreshs */
extern uint32_t disk_storage_tbl_refresh;

/* maximum number of ticks between updates of network table */
extern uint32_t swins_tbl_refresh;

/* maximum number of ticks between updates of network table */
extern uint32_t network_tbl_refresh;

/* package directory */
extern u_char *pkg_dir;

/* Initialize and populate storage table */
void init_storage_tbl(void);

/* Finalization routine for hrStorageTable. */
void fini_storage_tbl(void);

/* Refresh routine for hrStorageTable. */
void refresh_storage_tbl(int);

/*
 * Get the type of filesystem referenced in a struct statfs * -
 * used by FSTbl and StorageTbl functions.
 */
const struct asn_oid *fs_get_type(const struct statfs *);

/*
 * Because hrFSTable depends to hrStorageTable we are
 * refreshing hrFSTable by refreshing hrStorageTable.
 * When one entry "of type" fs from hrStorageTable is refreshed
 * then the corresponding entry from hrFSTable is refreshed
 * FS_tbl_pre_refresh_v() is called  before refeshing fs part of hrStorageTable
 */
void fs_tbl_pre_refresh(void);
void fs_tbl_process_statfs_entry(const struct statfs *, int32_t);

/* Called after refreshing fs part of hrStorageTable */
void fs_tbl_post_refresh(void);

/* Refresh the FS table if necessary. */
void refresh_fs_tbl(void);

/* Finalization routine for hrFSTable. */
void fini_fs_tbl(void);

/* Init the things for both of hrSWRunTable and hrSWRunPerfTable */
void init_swrun_tbl(void);

/* Finalization routine for both of hrSWRunTable and hrSWRunPerfTable */
void fini_swrun_tbl(void);

/* Init and populate hrDeviceTable */
void init_device_tbl(void);

/* start devd monitoring */
void start_device_tbl(struct lmodule *);

/* Finalization routine for hrDeviceTable */
void fini_device_tbl(void);

/* Refresh routine for hrDeviceTable. */
void refresh_device_tbl(int);

/* Find an item in hrDeviceTbl by its entry->index. */
struct device_entry *device_find_by_index(int32_t);

/* Find an item in hrDeviceTbl by name. */
struct device_entry *device_find_by_name(const char *);

/* Create a new entry out of thin air. */
struct device_entry *device_entry_create(const char *, const char *,
    const char *);

/* Delete an entry from hrDeviceTbl */
void device_entry_delete(struct device_entry *entry);

/* Init the things for hrProcessorTable. */
void init_processor_tbl(void);

/* Finalization routine for hrProcessorTable. */
void fini_processor_tbl(void);

/* Start the processor table CPU load collector. */
void start_processor_tbl(struct lmodule *);

/* Init the things for hrDiskStorageTable */
int init_disk_storage_tbl(void);

/* Finalization routine for hrDiskStorageTable. */
void fini_disk_storage_tbl(void);

/* Refresh routine for hrDiskStorageTable. */
void refresh_disk_storage_tbl(int);

/* Finalization routine for hrPartitionTable. */
void fini_partition_tbl(void);

/* Finalization routine for hrNetworkTable. */
void fini_network_tbl(void);

/* populate network table */
void start_network_tbl(void);

/* initialize installed software table */
void init_swins_tbl(void);

/* finalize installed software table */
void fini_swins_tbl(void);

/* refresh the hrSWInstalledTable if necessary */
void refresh_swins_tbl(void);

/* Init the things for hrPrinterTable */
void init_printer_tbl(void);

/* Finalization routine for hrPrinterTable. */
void fini_printer_tbl(void);

/* Refresh printer table */
void refresh_printer_tbl(void);

/* get boot command line */
int OS_getSystemInitialLoadParameters(u_char **);

/* Start refreshing the partition table */
void partition_tbl_post_refresh(void);

/* Handle refresh for the given disk */
void partition_tbl_handle_disk(int32_t, const char *);

/* Finish refreshing the partition table. */
void partition_tbl_pre_refresh(void);

/* Set the FS index in a partition table entry */
void handle_partition_fs_index(const char *, int32_t);

/* Make an SNMP DateAndTime from a struct tm. */
int make_date_time(u_char *, const struct tm *, u_int);

/* Free all static data */
void fini_scalars(void);

#endif /* HOSTRES_SNMP_H_1132245017 */
