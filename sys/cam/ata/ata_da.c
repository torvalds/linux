/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ada.h"

#include <sys/param.h>

#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>
#include <sys/devicestat.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/endian.h>
#include <sys/cons.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/sbuf.h>
#include <geom/geom_disk.h>
#endif /* _KERNEL */

#ifndef _KERNEL
#include <stdio.h>
#include <string.h>
#endif /* _KERNEL */

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/cam_sim.h>
#include <cam/cam_iosched.h>

#include <cam/ata/ata_all.h>

#include <machine/md_var.h>	/* geometry translation */

#ifdef _KERNEL

#define ATA_MAX_28BIT_LBA               268435455UL

extern int iosched_debug;

typedef enum {
	ADA_STATE_RAHEAD,
	ADA_STATE_WCACHE,
	ADA_STATE_LOGDIR,
	ADA_STATE_IDDIR,
	ADA_STATE_SUP_CAP,
	ADA_STATE_ZONE,
	ADA_STATE_NORMAL
} ada_state;

typedef enum {
	ADA_FLAG_CAN_48BIT	= 0x00000002,
	ADA_FLAG_CAN_FLUSHCACHE	= 0x00000004,
	ADA_FLAG_CAN_NCQ	= 0x00000008,
	ADA_FLAG_CAN_DMA	= 0x00000010,
	ADA_FLAG_NEED_OTAG	= 0x00000020,
	ADA_FLAG_WAS_OTAG	= 0x00000040,
	ADA_FLAG_CAN_TRIM	= 0x00000080,
	ADA_FLAG_OPEN		= 0x00000100,
	ADA_FLAG_SCTX_INIT	= 0x00000200,
	ADA_FLAG_CAN_CFA        = 0x00000400,
	ADA_FLAG_CAN_POWERMGT   = 0x00000800,
	ADA_FLAG_CAN_DMA48	= 0x00001000,
	ADA_FLAG_CAN_LOG	= 0x00002000,
	ADA_FLAG_CAN_IDLOG	= 0x00004000,
	ADA_FLAG_CAN_SUPCAP	= 0x00008000,
	ADA_FLAG_CAN_ZONE	= 0x00010000,
	ADA_FLAG_CAN_WCACHE	= 0x00020000,
	ADA_FLAG_CAN_RAHEAD	= 0x00040000,
	ADA_FLAG_PROBED		= 0x00080000,
	ADA_FLAG_ANNOUNCED	= 0x00100000,
	ADA_FLAG_DIRTY		= 0x00200000,
	ADA_FLAG_CAN_NCQ_TRIM	= 0x00400000,	/* CAN_TRIM also set */
	ADA_FLAG_PIM_ATA_EXT	= 0x00800000
} ada_flags;

typedef enum {
	ADA_Q_NONE		= 0x00,
	ADA_Q_4K		= 0x01,
	ADA_Q_NCQ_TRIM_BROKEN	= 0x02,
	ADA_Q_LOG_BROKEN	= 0x04,
	ADA_Q_SMR_DM		= 0x08,
	ADA_Q_NO_TRIM		= 0x10,
	ADA_Q_128KB		= 0x20
} ada_quirks;

#define ADA_Q_BIT_STRING	\
	"\020"			\
	"\0014K"		\
	"\002NCQ_TRIM_BROKEN"	\
	"\003LOG_BROKEN"	\
	"\004SMR_DM"		\
	"\005NO_TRIM"		\
	"\006128KB"

typedef enum {
	ADA_CCB_RAHEAD		= 0x01,
	ADA_CCB_WCACHE		= 0x02,
	ADA_CCB_BUFFER_IO	= 0x03,
	ADA_CCB_DUMP		= 0x05,
	ADA_CCB_TRIM		= 0x06,
	ADA_CCB_LOGDIR		= 0x07,
	ADA_CCB_IDDIR		= 0x08,
	ADA_CCB_SUP_CAP		= 0x09,
	ADA_CCB_ZONE		= 0x0a,
	ADA_CCB_TYPE_MASK	= 0x0F,
} ada_ccb_state;

typedef enum {
	ADA_ZONE_NONE		= 0x00,
	ADA_ZONE_DRIVE_MANAGED	= 0x01,
	ADA_ZONE_HOST_AWARE	= 0x02,
	ADA_ZONE_HOST_MANAGED	= 0x03
} ada_zone_mode;

typedef enum {
	ADA_ZONE_FLAG_RZ_SUP		= 0x0001,
	ADA_ZONE_FLAG_OPEN_SUP		= 0x0002,
	ADA_ZONE_FLAG_CLOSE_SUP		= 0x0004,
	ADA_ZONE_FLAG_FINISH_SUP	= 0x0008,
	ADA_ZONE_FLAG_RWP_SUP		= 0x0010,
	ADA_ZONE_FLAG_SUP_MASK		= (ADA_ZONE_FLAG_RZ_SUP |
					   ADA_ZONE_FLAG_OPEN_SUP |
					   ADA_ZONE_FLAG_CLOSE_SUP |
					   ADA_ZONE_FLAG_FINISH_SUP |
					   ADA_ZONE_FLAG_RWP_SUP),
	ADA_ZONE_FLAG_URSWRZ		= 0x0020,
	ADA_ZONE_FLAG_OPT_SEQ_SET	= 0x0040,
	ADA_ZONE_FLAG_OPT_NONSEQ_SET	= 0x0080,
	ADA_ZONE_FLAG_MAX_SEQ_SET	= 0x0100,
	ADA_ZONE_FLAG_SET_MASK		= (ADA_ZONE_FLAG_OPT_SEQ_SET |
					   ADA_ZONE_FLAG_OPT_NONSEQ_SET |
					   ADA_ZONE_FLAG_MAX_SEQ_SET)
} ada_zone_flags;

static struct ada_zone_desc {
	ada_zone_flags value;
	const char *desc;
} ada_zone_desc_table[] = {
	{ADA_ZONE_FLAG_RZ_SUP, "Report Zones" },
	{ADA_ZONE_FLAG_OPEN_SUP, "Open" },
	{ADA_ZONE_FLAG_CLOSE_SUP, "Close" },
	{ADA_ZONE_FLAG_FINISH_SUP, "Finish" },
	{ADA_ZONE_FLAG_RWP_SUP, "Reset Write Pointer" },
};


/* Offsets into our private area for storing information */
#define ccb_state	ppriv_field0
#define ccb_bp		ppriv_ptr1

typedef enum {
	ADA_DELETE_NONE,
	ADA_DELETE_DISABLE,
	ADA_DELETE_CFA_ERASE,
	ADA_DELETE_DSM_TRIM,
	ADA_DELETE_NCQ_DSM_TRIM,
	ADA_DELETE_MIN = ADA_DELETE_CFA_ERASE,
	ADA_DELETE_MAX = ADA_DELETE_NCQ_DSM_TRIM,
} ada_delete_methods;

static const char *ada_delete_method_names[] =
    { "NONE", "DISABLE", "CFA_ERASE", "DSM_TRIM", "NCQ_DSM_TRIM" };
#if 0
static const char *ada_delete_method_desc[] =
    { "NONE", "DISABLED", "CFA Erase", "DSM Trim", "DSM Trim via NCQ" };
#endif

struct disk_params {
	u_int8_t  heads;
	u_int8_t  secs_per_track;
	u_int32_t cylinders;
	u_int32_t secsize;	/* Number of bytes/logical sector */
	u_int64_t sectors;	/* Total number sectors */
};

#define TRIM_MAX_BLOCKS	8
#define TRIM_MAX_RANGES	(TRIM_MAX_BLOCKS * ATA_DSM_BLK_RANGES)
struct trim_request {
	uint8_t		data[TRIM_MAX_RANGES * ATA_DSM_RANGE_SIZE];
	TAILQ_HEAD(, bio) bps;
};

struct ada_softc {
	struct   cam_iosched_softc *cam_iosched;
	int	 outstanding_cmds;	/* Number of active commands */
	int	 refcount;		/* Active xpt_action() calls */
	ada_state state;
	ada_flags flags;
	ada_zone_mode zone_mode;
	ada_zone_flags zone_flags;
	struct ata_gp_log_dir ata_logdir;
	int valid_logdir_len;
	struct ata_identify_log_pages ata_iddir;
	int valid_iddir_len;
	uint64_t optimal_seq_zones;
	uint64_t optimal_nonseq_zones;
	uint64_t max_seq_zones;
	ada_quirks quirks;
	ada_delete_methods delete_method;
	int	 trim_max_ranges;
	int	 read_ahead;
	int	 write_cache;
	int	 unmappedio;
	int	 rotating;
#ifdef CAM_TEST_FAILURE
	int      force_read_error;
	int      force_write_error;
	int      periodic_read_error;
	int      periodic_read_count;
#endif
	struct	 disk_params params;
	struct	 disk *disk;
	struct task		sysctl_task;
	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
	struct callout		sendordered_c;
	struct trim_request	trim_req;
	uint64_t		trim_count;
	uint64_t		trim_ranges;
	uint64_t		trim_lbas;
#ifdef CAM_IO_STATS
	struct sysctl_ctx_list	sysctl_stats_ctx;
	struct sysctl_oid	*sysctl_stats_tree;
	u_int	timeouts;
	u_int	errors;
	u_int	invalidations;
#endif
#define ADA_ANNOUNCETMP_SZ 80
	char	announce_temp[ADA_ANNOUNCETMP_SZ];
#define ADA_ANNOUNCE_SZ 400
	char	announce_buffer[ADA_ANNOUNCE_SZ];
};

struct ada_quirk_entry {
	struct scsi_inquiry_pattern inq_pat;
	ada_quirks quirks;
};

static struct ada_quirk_entry ada_quirk_table[] =
{
	{
		/* Sandisk X400 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "SanDisk?SD8SB8U1T00*", "X4162000*" },
		/*quirks*/ADA_Q_128KB
	},
	{
		/* Hitachi Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Hitachi H??????????E3*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Samsung Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "SAMSUNG HD155UI*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Samsung Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "SAMSUNG HD204UI*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Barracuda Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST????DL*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Barracuda Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST???DM*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Barracuda Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST????DM*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST9500423AS*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST9500424AS*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST9640423AS*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST9640424AS*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST9750420AS*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST9750422AS*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST9750423AS*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* Seagate Momentus Thin Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST???LT*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Caviar Red Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD????CX*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD????RS*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Caviar Green/Red Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD????RX*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Caviar Red Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD??????CX*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Caviar Black Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD????AZEX*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Caviar Black Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD????FZEX*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD??????RS*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD??????RX*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Scorpio Black Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD???PKT*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Scorpio Black Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD?????PKT*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Scorpio Blue Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD???PVT*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/* WDC Scorpio Blue Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "WDC WD?????PVT*", "*" },
		/*quirks*/ADA_Q_4K
	},
	/* SSDs */
	{
		/*
		 * Corsair Force 2 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Corsair CSSD-F*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Corsair Force 3 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Corsair Force 3*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Corsair Neutron GTX SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Corsair Neutron GTX*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Corsair Force GT & GS SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Corsair Force G*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Crucial M4 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "M4-CT???M4SSD2*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Crucial M500 SSDs MU07 firmware
		 * NCQ Trim works
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Crucial CT*M500*", "MU07" },
		/*quirks*/0
	},
	{
		/*
		 * Crucial M500 SSDs all other firmware
		 * NCQ Trim doesn't work
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Crucial CT*M500*", "*" },
		/*quirks*/ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Crucial M550 SSDs
		 * NCQ Trim doesn't work, but only on MU01 firmware
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Crucial CT*M550*", "MU01" },
		/*quirks*/ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Crucial MX100 SSDs
		 * NCQ Trim doesn't work, but only on MU01 firmware
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Crucial CT*MX100*", "MU01" },
		/*quirks*/ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Crucial RealSSD C300 SSDs
		 * 4k optimised
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "C300-CTFDDAC???MAG*",
		"*" }, /*quirks*/ADA_Q_4K
	},
	{
		/*
		 * FCCT M500 SSDs
		 * NCQ Trim doesn't work
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "FCCT*M500*", "*" },
		/*quirks*/ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Intel 320 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "INTEL SSDSA2CW*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Intel 330 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "INTEL SSDSC2CT*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Intel 510 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "INTEL SSDSC2MH*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Intel 520 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "INTEL SSDSC2BW*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Intel S3610 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "INTEL SSDSC2BX*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Intel X25-M Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "INTEL SSDSA2M*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * KingDian S200 60GB P0921B
		 * Trimming crash the SSD
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "KingDian S200 *", "*" },
		/*quirks*/ADA_Q_NO_TRIM
	},
	{
		/*
		 * Kingston E100 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "KINGSTON SE100S3*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Kingston HyperX 3k SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "KINGSTON SH103S3*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Marvell SSDs (entry taken from OpenSolaris)
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "MARVELL SD88SA02*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Micron M500 SSDs firmware MU07
		 * NCQ Trim works?
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Micron M500*", "MU07" },
		/*quirks*/0
	},
	{
		/*
		 * Micron M500 SSDs all other firmware
		 * NCQ Trim doesn't work
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Micron M500*", "*" },
		/*quirks*/ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Micron M5[15]0 SSDs
		 * NCQ Trim doesn't work, but only MU01 firmware
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Micron M5[15]0*", "MU01" },
		/*quirks*/ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Micron 5100 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Micron 5100 MTFDDAK*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * OCZ Agility 2 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "OCZ-AGILITY2*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * OCZ Agility 3 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "OCZ-AGILITY3*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * OCZ Deneva R Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "DENRSTE251M45*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * OCZ Vertex 2 SSDs (inc pro series)
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "OCZ?VERTEX2*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * OCZ Vertex 3 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "OCZ-VERTEX3*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * OCZ Vertex 4 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "OCZ-VERTEX4*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Samsung 750 SSDs
		 * 4k optimised, NCQ TRIM seems to work
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Samsung SSD 750*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Samsung 830 Series SSDs
		 * 4k optimised, NCQ TRIM Broken (normal TRIM is fine)
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "SAMSUNG SSD 830 Series*", "*" },
		/*quirks*/ADA_Q_4K | ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Samsung 840 SSDs
		 * 4k optimised, NCQ TRIM Broken (normal TRIM is fine)
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Samsung SSD 840*", "*" },
		/*quirks*/ADA_Q_4K | ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Samsung 845 SSDs
		 * 4k optimised, NCQ TRIM Broken (normal TRIM is fine)
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Samsung SSD 845*", "*" },
		/*quirks*/ADA_Q_4K | ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Samsung 850 SSDs
		 * 4k optimised, NCQ TRIM broken (normal TRIM fine)
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Samsung SSD 850*", "*" },
		/*quirks*/ADA_Q_4K | ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Samsung SM863 Series SSDs (MZ7KM*)
		 * 4k optimised, NCQ believed to be working
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "SAMSUNG MZ7KM*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Samsung 843T Series SSDs (MZ7WD*)
		 * Samsung PM851 Series SSDs (MZ7TE*)
		 * Samsung PM853T Series SSDs (MZ7GE*)
		 * 4k optimised, NCQ believed to be broken since these are
		 * appear to be built with the same controllers as the 840/850.
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "SAMSUNG MZ7*", "*" },
		/*quirks*/ADA_Q_4K | ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Same as for SAMSUNG MZ7* but enable the quirks for SSD
		 * starting with MZ7* too
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "MZ7*", "*" },
		/*quirks*/ADA_Q_4K | ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * Samsung PM851 Series SSDs Dell OEM
		 * device model          "SAMSUNG SSD PM851 mSATA 256GB"
		 * 4k optimised, NCQ broken
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "SAMSUNG SSD PM851*", "*" },
		/*quirks*/ADA_Q_4K | ADA_Q_NCQ_TRIM_BROKEN
	},
	{
		/*
		 * SuperTalent TeraDrive CT SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "FTM??CT25H*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * XceedIOPS SATA SSDs
		 * 4k optimised
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "SG9XCS2D*", "*" },
		/*quirks*/ADA_Q_4K
	},
	{
		/*
		 * Samsung drive that doesn't support READ LOG EXT or
		 * READ LOG DMA EXT, despite reporting that it does in
		 * ATA identify data:
		 * SAMSUNG HD200HJ KF100-06
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "SAMSUNG HD200*", "*" },
		/*quirks*/ADA_Q_LOG_BROKEN
	},
	{
		/*
		 * Samsung drive that doesn't support READ LOG EXT or
		 * READ LOG DMA EXT, despite reporting that it does in
		 * ATA identify data:
		 * SAMSUNG HD501LJ CR100-10
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "SAMSUNG HD501*", "*" },
		/*quirks*/ADA_Q_LOG_BROKEN
	},
	{
		/*
		 * Seagate Lamarr 8TB Shingled Magnetic Recording (SMR)
		 * Drive Managed SATA hard drive.  This drive doesn't report
		 * in firmware that it is a drive managed SMR drive.
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "ST8000AS000[23]*", "*" },
		/*quirks*/ADA_Q_SMR_DM
	},
	{
		/* Default */
		{
		  T_ANY, SIP_MEDIA_REMOVABLE|SIP_MEDIA_FIXED,
		  /*vendor*/"*", /*product*/"*", /*revision*/"*"
		},
		/*quirks*/0
	},
};

static	disk_strategy_t	adastrategy;
static	dumper_t	adadump;
static	periph_init_t	adainit;
static	void		adadiskgonecb(struct disk *dp);
static	periph_oninv_t	adaoninvalidate;
static	periph_dtor_t	adacleanup;
static	void		adaasync(void *callback_arg, u_int32_t code,
				struct cam_path *path, void *arg);
static	int		adazonemodesysctl(SYSCTL_HANDLER_ARGS);
static	int		adazonesupsysctl(SYSCTL_HANDLER_ARGS);
static	void		adasysctlinit(void *context, int pending);
static	int		adagetattr(struct bio *bp);
static	void		adasetflags(struct ada_softc *softc,
				    struct ccb_getdev *cgd);
static	periph_ctor_t	adaregister;
static	void		ada_dsmtrim(struct ada_softc *softc, struct bio *bp,
				    struct ccb_ataio *ataio);
static	void 		ada_cfaerase(struct ada_softc *softc, struct bio *bp,
				     struct ccb_ataio *ataio);
static	int		ada_zone_bio_to_ata(int disk_zone_cmd);
static	int		ada_zone_cmd(struct cam_periph *periph, union ccb *ccb,
				     struct bio *bp, int *queue_ccb);
static	periph_start_t	adastart;
static	void		adaprobedone(struct cam_periph *periph, union ccb *ccb);
static	void		adazonedone(struct cam_periph *periph, union ccb *ccb);
static	void		adadone(struct cam_periph *periph,
			       union ccb *done_ccb);
static  int		adaerror(union ccb *ccb, u_int32_t cam_flags,
				u_int32_t sense_flags);
static void		adagetparams(struct cam_periph *periph,
				struct ccb_getdev *cgd);
static timeout_t	adasendorderedtag;
static void		adashutdown(void *arg, int howto);
static void		adasuspend(void *arg);
static void		adaresume(void *arg);

#ifndef ADA_DEFAULT_TIMEOUT
#define ADA_DEFAULT_TIMEOUT 30	/* Timeout in seconds */
#endif

#ifndef	ADA_DEFAULT_RETRY
#define	ADA_DEFAULT_RETRY	4
#endif

#ifndef	ADA_DEFAULT_SEND_ORDERED
#define	ADA_DEFAULT_SEND_ORDERED	1
#endif

#ifndef	ADA_DEFAULT_SPINDOWN_SHUTDOWN
#define	ADA_DEFAULT_SPINDOWN_SHUTDOWN	1
#endif

#ifndef	ADA_DEFAULT_SPINDOWN_SUSPEND
#define	ADA_DEFAULT_SPINDOWN_SUSPEND	1
#endif

#ifndef	ADA_DEFAULT_READ_AHEAD
#define	ADA_DEFAULT_READ_AHEAD	1
#endif

#ifndef	ADA_DEFAULT_WRITE_CACHE
#define	ADA_DEFAULT_WRITE_CACHE	1
#endif

#define	ADA_RA	(softc->read_ahead >= 0 ? \
		 softc->read_ahead : ada_read_ahead)
#define	ADA_WC	(softc->write_cache >= 0 ? \
		 softc->write_cache : ada_write_cache)

/*
 * Most platforms map firmware geometry to actual, but some don't.  If
 * not overridden, default to nothing.
 */
#ifndef ata_disk_firmware_geom_adjust
#define	ata_disk_firmware_geom_adjust(disk)
#endif

static int ada_retry_count = ADA_DEFAULT_RETRY;
static int ada_default_timeout = ADA_DEFAULT_TIMEOUT;
static int ada_send_ordered = ADA_DEFAULT_SEND_ORDERED;
static int ada_spindown_shutdown = ADA_DEFAULT_SPINDOWN_SHUTDOWN;
static int ada_spindown_suspend = ADA_DEFAULT_SPINDOWN_SUSPEND;
static int ada_read_ahead = ADA_DEFAULT_READ_AHEAD;
static int ada_write_cache = ADA_DEFAULT_WRITE_CACHE;

static SYSCTL_NODE(_kern_cam, OID_AUTO, ada, CTLFLAG_RD, 0,
            "CAM Direct Access Disk driver");
SYSCTL_INT(_kern_cam_ada, OID_AUTO, retry_count, CTLFLAG_RWTUN,
           &ada_retry_count, 0, "Normal I/O retry count");
SYSCTL_INT(_kern_cam_ada, OID_AUTO, default_timeout, CTLFLAG_RWTUN,
           &ada_default_timeout, 0, "Normal I/O timeout (in seconds)");
SYSCTL_INT(_kern_cam_ada, OID_AUTO, send_ordered, CTLFLAG_RWTUN,
           &ada_send_ordered, 0, "Send Ordered Tags");
SYSCTL_INT(_kern_cam_ada, OID_AUTO, spindown_shutdown, CTLFLAG_RWTUN,
           &ada_spindown_shutdown, 0, "Spin down upon shutdown");
SYSCTL_INT(_kern_cam_ada, OID_AUTO, spindown_suspend, CTLFLAG_RWTUN,
           &ada_spindown_suspend, 0, "Spin down upon suspend");
SYSCTL_INT(_kern_cam_ada, OID_AUTO, read_ahead, CTLFLAG_RWTUN,
           &ada_read_ahead, 0, "Enable disk read-ahead");
SYSCTL_INT(_kern_cam_ada, OID_AUTO, write_cache, CTLFLAG_RWTUN,
           &ada_write_cache, 0, "Enable disk write cache");

/*
 * ADA_ORDEREDTAG_INTERVAL determines how often, relative
 * to the default timeout, we check to see whether an ordered
 * tagged transaction is appropriate to prevent simple tag
 * starvation.  Since we'd like to ensure that there is at least
 * 1/2 of the timeout length left for a starved transaction to
 * complete after we've sent an ordered tag, we must poll at least
 * four times in every timeout period.  This takes care of the worst
 * case where a starved transaction starts during an interval that
 * meets the requirement "don't send an ordered tag" test so it takes
 * us two intervals to determine that a tag must be sent.
 */
#ifndef ADA_ORDEREDTAG_INTERVAL
#define ADA_ORDEREDTAG_INTERVAL 4
#endif

static struct periph_driver adadriver =
{
	adainit, "ada",
	TAILQ_HEAD_INITIALIZER(adadriver.units), /* generation */ 0
};

static int adadeletemethodsysctl(SYSCTL_HANDLER_ARGS);

PERIPHDRIVER_DECLARE(ada, adadriver);

static MALLOC_DEFINE(M_ATADA, "ata_da", "ata_da buffers");

static int
adaopen(struct disk *dp)
{
	struct cam_periph *periph;
	struct ada_softc *softc;
	int error;

	periph = (struct cam_periph *)dp->d_drv1;
	if (cam_periph_acquire(periph) != 0) {
		return(ENXIO);
	}

	cam_periph_lock(periph);
	if ((error = cam_periph_hold(periph, PRIBIO|PCATCH)) != 0) {
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return (error);
	}

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE | CAM_DEBUG_PERIPH,
	    ("adaopen\n"));

	softc = (struct ada_softc *)periph->softc;
	softc->flags |= ADA_FLAG_OPEN;

	cam_periph_unhold(periph);
	cam_periph_unlock(periph);
	return (0);
}

static int
adaclose(struct disk *dp)
{
	struct	cam_periph *periph;
	struct	ada_softc *softc;
	union ccb *ccb;
	int error;

	periph = (struct cam_periph *)dp->d_drv1;
	softc = (struct ada_softc *)periph->softc;
	cam_periph_lock(periph);

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE | CAM_DEBUG_PERIPH,
	    ("adaclose\n"));

	/* We only sync the cache if the drive is capable of it. */
	if ((softc->flags & ADA_FLAG_DIRTY) != 0 &&
	    (softc->flags & ADA_FLAG_CAN_FLUSHCACHE) != 0 &&
	    (periph->flags & CAM_PERIPH_INVALID) == 0 &&
	    cam_periph_hold(periph, PRIBIO) == 0) {

		ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
		cam_fill_ataio(&ccb->ataio,
				    1,
				    NULL,
				    CAM_DIR_NONE,
				    0,
				    NULL,
				    0,
				    ada_default_timeout*1000);

		if (softc->flags & ADA_FLAG_CAN_48BIT)
			ata_48bit_cmd(&ccb->ataio, ATA_FLUSHCACHE48, 0, 0, 0);
		else
			ata_28bit_cmd(&ccb->ataio, ATA_FLUSHCACHE, 0, 0, 0);
		error = cam_periph_runccb(ccb, adaerror, /*cam_flags*/0,
		    /*sense_flags*/0, softc->disk->d_devstat);

		if (error != 0)
			xpt_print(periph->path, "Synchronize cache failed\n");
		softc->flags &= ~ADA_FLAG_DIRTY;
		xpt_release_ccb(ccb);
		cam_periph_unhold(periph);
	}

	softc->flags &= ~ADA_FLAG_OPEN;

	while (softc->refcount != 0)
		cam_periph_sleep(periph, &softc->refcount, PRIBIO, "adaclose", 1);
	cam_periph_unlock(periph);
	cam_periph_release(periph);
	return (0);
}

static void
adaschedule(struct cam_periph *periph)
{
	struct ada_softc *softc = (struct ada_softc *)periph->softc;

	if (softc->state != ADA_STATE_NORMAL)
		return;

	cam_iosched_schedule(softc->cam_iosched, periph);
}

/*
 * Actually translate the requested transfer into one the physical driver
 * can understand.  The transfer is described by a buf and will include
 * only one physical transfer.
 */
static void
adastrategy(struct bio *bp)
{
	struct cam_periph *periph;
	struct ada_softc *softc;

	periph = (struct cam_periph *)bp->bio_disk->d_drv1;
	softc = (struct ada_softc *)periph->softc;

	cam_periph_lock(periph);

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("adastrategy(%p)\n", bp));

	/*
	 * If the device has been made invalid, error out
	 */
	if ((periph->flags & CAM_PERIPH_INVALID) != 0) {
		cam_periph_unlock(periph);
		biofinish(bp, NULL, ENXIO);
		return;
	}

	/*
	 * Zone commands must be ordered, because they can depend on the
	 * effects of previously issued commands, and they may affect
	 * commands after them.
	 */
	if (bp->bio_cmd == BIO_ZONE)
		bp->bio_flags |= BIO_ORDERED;

	/*
	 * Place it in the queue of disk activities for this disk
	 */
	cam_iosched_queue_work(softc->cam_iosched, bp);

	/*
	 * Schedule ourselves for performing the work.
	 */
	adaschedule(periph);
	cam_periph_unlock(periph);

	return;
}

static int
adadump(void *arg, void *virtual, vm_offset_t physical, off_t offset, size_t length)
{
	struct	    cam_periph *periph;
	struct	    ada_softc *softc;
	u_int	    secsize;
	struct	    ccb_ataio ataio;
	struct	    disk *dp;
	uint64_t    lba;
	uint16_t    count;
	int	    error = 0;

	dp = arg;
	periph = dp->d_drv1;
	softc = (struct ada_softc *)periph->softc;
	secsize = softc->params.secsize;
	lba = offset / secsize;
	count = length / secsize;
	if ((periph->flags & CAM_PERIPH_INVALID) != 0)
		return (ENXIO);

	memset(&ataio, 0, sizeof(ataio));
	if (length > 0) {
		xpt_setup_ccb(&ataio.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
		ataio.ccb_h.ccb_state = ADA_CCB_DUMP;
		cam_fill_ataio(&ataio,
		    0,
		    NULL,
		    CAM_DIR_OUT,
		    0,
		    (u_int8_t *) virtual,
		    length,
		    ada_default_timeout*1000);
		if ((softc->flags & ADA_FLAG_CAN_48BIT) &&
		    (lba + count >= ATA_MAX_28BIT_LBA ||
		    count >= 256)) {
			ata_48bit_cmd(&ataio, ATA_WRITE_DMA48,
			    0, lba, count);
		} else {
			ata_28bit_cmd(&ataio, ATA_WRITE_DMA,
			    0, lba, count);
		}
		error = cam_periph_runccb((union ccb *)&ataio, adaerror,
		    0, SF_NO_RECOVERY | SF_NO_RETRY, NULL);
		if (error != 0)
			printf("Aborting dump due to I/O error.\n");

		return (error);
	}

	if (softc->flags & ADA_FLAG_CAN_FLUSHCACHE) {
		xpt_setup_ccb(&ataio.ccb_h, periph->path, CAM_PRIORITY_NORMAL);

		/*
		 * Tell the drive to flush its internal cache. if we
		 * can't flush in 5s we have big problems. No need to
		 * wait the default 60s to detect problems.
		 */
		ataio.ccb_h.ccb_state = ADA_CCB_DUMP;
		cam_fill_ataio(&ataio,
				    0,
				    NULL,
				    CAM_DIR_NONE,
				    0,
				    NULL,
				    0,
				    5*1000);

		if (softc->flags & ADA_FLAG_CAN_48BIT)
			ata_48bit_cmd(&ataio, ATA_FLUSHCACHE48, 0, 0, 0);
		else
			ata_28bit_cmd(&ataio, ATA_FLUSHCACHE, 0, 0, 0);
		error = cam_periph_runccb((union ccb *)&ataio, adaerror,
		    0, SF_NO_RECOVERY | SF_NO_RETRY, NULL);
		if (error != 0)
			xpt_print(periph->path, "Synchronize cache failed\n");
	}
	return (error);
}

static void
adainit(void)
{
	cam_status status;

	/*
	 * Install a global async callback.  This callback will
	 * receive async callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, adaasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("ada: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	} else if (ada_send_ordered) {

		/* Register our event handlers */
		if ((EVENTHANDLER_REGISTER(power_suspend, adasuspend,
					   NULL, EVENTHANDLER_PRI_LAST)) == NULL)
		    printf("adainit: power event registration failed!\n");
		if ((EVENTHANDLER_REGISTER(power_resume, adaresume,
					   NULL, EVENTHANDLER_PRI_LAST)) == NULL)
		    printf("adainit: power event registration failed!\n");
		if ((EVENTHANDLER_REGISTER(shutdown_post_sync, adashutdown,
					   NULL, SHUTDOWN_PRI_DEFAULT)) == NULL)
		    printf("adainit: shutdown event registration failed!\n");
	}
}

/*
 * Callback from GEOM, called when it has finished cleaning up its
 * resources.
 */
static void
adadiskgonecb(struct disk *dp)
{
	struct cam_periph *periph;

	periph = (struct cam_periph *)dp->d_drv1;

	cam_periph_release(periph);
}

static void
adaoninvalidate(struct cam_periph *periph)
{
	struct ada_softc *softc;

	softc = (struct ada_softc *)periph->softc;

	/*
	 * De-register any async callbacks.
	 */
	xpt_register_async(0, adaasync, periph, periph->path);
#ifdef CAM_IO_STATS
	softc->invalidations++;
#endif

	/*
	 * Return all queued I/O with ENXIO.
	 * XXX Handle any transactions queued to the card
	 *     with XPT_ABORT_CCB.
	 */
	cam_iosched_flush(softc->cam_iosched, NULL, ENXIO);

	disk_gone(softc->disk);
}

static void
adacleanup(struct cam_periph *periph)
{
	struct ada_softc *softc;

	softc = (struct ada_softc *)periph->softc;

	cam_periph_unlock(periph);

	cam_iosched_fini(softc->cam_iosched);

	/*
	 * If we can't free the sysctl tree, oh well...
	 */
	if ((softc->flags & ADA_FLAG_SCTX_INIT) != 0) {
#ifdef CAM_IO_STATS
		if (sysctl_ctx_free(&softc->sysctl_stats_ctx) != 0)
			xpt_print(periph->path,
			    "can't remove sysctl stats context\n");
#endif
		if (sysctl_ctx_free(&softc->sysctl_ctx) != 0)
			xpt_print(periph->path,
			    "can't remove sysctl context\n");
	}

	disk_destroy(softc->disk);
	callout_drain(&softc->sendordered_c);
	free(softc, M_DEVBUF);
	cam_periph_lock(periph);
}

static void
adasetdeletemethod(struct ada_softc *softc)
{

	if (softc->flags & ADA_FLAG_CAN_NCQ_TRIM)
		softc->delete_method = ADA_DELETE_NCQ_DSM_TRIM;
	else if (softc->flags & ADA_FLAG_CAN_TRIM)
		softc->delete_method = ADA_DELETE_DSM_TRIM;
	else if ((softc->flags & ADA_FLAG_CAN_CFA) && !(softc->flags & ADA_FLAG_CAN_48BIT))
		softc->delete_method = ADA_DELETE_CFA_ERASE;
	else
		softc->delete_method = ADA_DELETE_NONE;
}

static void
adaasync(void *callback_arg, u_int32_t code,
	struct cam_path *path, void *arg)
{
	struct ccb_getdev cgd;
	struct cam_periph *periph;
	struct ada_softc *softc;

	periph = (struct cam_periph *)callback_arg;
	switch (code) {
	case AC_FOUND_DEVICE:
	{
		struct ccb_getdev *cgd;
		cam_status status;

		cgd = (struct ccb_getdev *)arg;
		if (cgd == NULL)
			break;

		if (cgd->protocol != PROTO_ATA)
			break;

		/*
		 * Allocate a peripheral instance for
		 * this device and start the probe
		 * process.
		 */
		status = cam_periph_alloc(adaregister, adaoninvalidate,
					  adacleanup, adastart,
					  "ada", CAM_PERIPH_BIO,
					  path, adaasync,
					  AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP
		 && status != CAM_REQ_INPROG)
			printf("adaasync: Unable to attach to new device "
				"due to status 0x%x\n", status);
		break;
	}
	case AC_GETDEV_CHANGED:
	{
		softc = (struct ada_softc *)periph->softc;
		xpt_setup_ccb(&cgd.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
		cgd.ccb_h.func_code = XPT_GDEV_TYPE;
		xpt_action((union ccb *)&cgd);

		/*
		 * Set/clear support flags based on the new Identify data.
		 */
		adasetflags(softc, &cgd);

		cam_periph_async(periph, code, path, arg);
		break;
	}
	case AC_ADVINFO_CHANGED:
	{
		uintptr_t buftype;

		buftype = (uintptr_t)arg;
		if (buftype == CDAI_TYPE_PHYS_PATH) {
			struct ada_softc *softc;

			softc = periph->softc;
			disk_attr_changed(softc->disk, "GEOM::physpath",
					  M_NOWAIT);
		}
		break;
	}
	case AC_SENT_BDR:
	case AC_BUS_RESET:
	{
		softc = (struct ada_softc *)periph->softc;
		cam_periph_async(periph, code, path, arg);
		if (softc->state != ADA_STATE_NORMAL)
			break;
		xpt_setup_ccb(&cgd.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
		cgd.ccb_h.func_code = XPT_GDEV_TYPE;
		xpt_action((union ccb *)&cgd);
		if (ADA_RA >= 0 && softc->flags & ADA_FLAG_CAN_RAHEAD)
			softc->state = ADA_STATE_RAHEAD;
		else if (ADA_WC >= 0 && softc->flags & ADA_FLAG_CAN_WCACHE)
			softc->state = ADA_STATE_WCACHE;
		else if ((softc->flags & ADA_FLAG_CAN_LOG)
		      && (softc->zone_mode != ADA_ZONE_NONE))
			softc->state = ADA_STATE_LOGDIR;
		else
		    break;
		if (cam_periph_acquire(periph) != 0)
			softc->state = ADA_STATE_NORMAL;
		else
			xpt_schedule(periph, CAM_PRIORITY_DEV);
	}
	default:
		cam_periph_async(periph, code, path, arg);
		break;
	}
}

static int
adazonemodesysctl(SYSCTL_HANDLER_ARGS)
{
	char tmpbuf[40];
	struct ada_softc *softc;
	int error;

	softc = (struct ada_softc *)arg1;

	switch (softc->zone_mode) {
	case ADA_ZONE_DRIVE_MANAGED:
		snprintf(tmpbuf, sizeof(tmpbuf), "Drive Managed");
		break;
	case ADA_ZONE_HOST_AWARE:
		snprintf(tmpbuf, sizeof(tmpbuf), "Host Aware");
		break;
	case ADA_ZONE_HOST_MANAGED:
		snprintf(tmpbuf, sizeof(tmpbuf), "Host Managed");
		break;
	case ADA_ZONE_NONE:
	default:
		snprintf(tmpbuf, sizeof(tmpbuf), "Not Zoned");
		break;
	}

	error = sysctl_handle_string(oidp, tmpbuf, sizeof(tmpbuf), req);

	return (error);
}

static int
adazonesupsysctl(SYSCTL_HANDLER_ARGS)
{
	char tmpbuf[180];
	struct ada_softc *softc;
	struct sbuf sb;
	int error, first;
	unsigned int i;

	softc = (struct ada_softc *)arg1;

	error = 0;
	first = 1;
	sbuf_new(&sb, tmpbuf, sizeof(tmpbuf), 0);

	for (i = 0; i < sizeof(ada_zone_desc_table) /
	     sizeof(ada_zone_desc_table[0]); i++) {
		if (softc->zone_flags & ada_zone_desc_table[i].value) {
			if (first == 0)
				sbuf_printf(&sb, ", ");
			else
				first = 0;
			sbuf_cat(&sb, ada_zone_desc_table[i].desc);
		}
	}

	if (first == 1)
		sbuf_printf(&sb, "None");

	sbuf_finish(&sb);

	error = sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);

	return (error);
}


static void
adasysctlinit(void *context, int pending)
{
	struct cam_periph *periph;
	struct ada_softc *softc;
	char tmpstr[32], tmpstr2[16];

	periph = (struct cam_periph *)context;

	/* periph was held for us when this task was enqueued */
	if ((periph->flags & CAM_PERIPH_INVALID) != 0) {
		cam_periph_release(periph);
		return;
	}

	softc = (struct ada_softc *)periph->softc;
	snprintf(tmpstr, sizeof(tmpstr), "CAM ADA unit %d",periph->unit_number);
	snprintf(tmpstr2, sizeof(tmpstr2), "%d", periph->unit_number);

	sysctl_ctx_init(&softc->sysctl_ctx);
	softc->flags |= ADA_FLAG_SCTX_INIT;
	softc->sysctl_tree = SYSCTL_ADD_NODE_WITH_LABEL(&softc->sysctl_ctx,
		SYSCTL_STATIC_CHILDREN(_kern_cam_ada), OID_AUTO, tmpstr2,
		CTLFLAG_RD, 0, tmpstr, "device_index");
	if (softc->sysctl_tree == NULL) {
		printf("adasysctlinit: unable to allocate sysctl tree\n");
		cam_periph_release(periph);
		return;
	}

	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "delete_method", CTLTYPE_STRING | CTLFLAG_RW,
		softc, 0, adadeletemethodsysctl, "A",
		"BIO_DELETE execution method");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"trim_count", CTLFLAG_RD, &softc->trim_count,
		"Total number of dsm commands sent");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"trim_ranges", CTLFLAG_RD, &softc->trim_ranges,
		"Total number of ranges in dsm commands");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"trim_lbas", CTLFLAG_RD, &softc->trim_lbas,
		"Total lbas in the dsm commands sent");
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "read_ahead", CTLFLAG_RW | CTLFLAG_MPSAFE,
		&softc->read_ahead, 0, "Enable disk read ahead.");
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "write_cache", CTLFLAG_RW | CTLFLAG_MPSAFE,
		&softc->write_cache, 0, "Enable disk write cache.");
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "unmapped_io", CTLFLAG_RD | CTLFLAG_MPSAFE,
		&softc->unmappedio, 0, "Unmapped I/O leaf");
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "rotating", CTLFLAG_RD | CTLFLAG_MPSAFE,
		&softc->rotating, 0, "Rotating media");
	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "zone_mode", CTLTYPE_STRING | CTLFLAG_RD,
		softc, 0, adazonemodesysctl, "A",
		"Zone Mode");
	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "zone_support", CTLTYPE_STRING | CTLFLAG_RD,
		softc, 0, adazonesupsysctl, "A",
		"Zone Support");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"optimal_seq_zones", CTLFLAG_RD, &softc->optimal_seq_zones,
		"Optimal Number of Open Sequential Write Preferred Zones");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"optimal_nonseq_zones", CTLFLAG_RD,
		&softc->optimal_nonseq_zones,
		"Optimal Number of Non-Sequentially Written Sequential Write "
		"Preferred Zones");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"max_seq_zones", CTLFLAG_RD, &softc->max_seq_zones,
		"Maximum Number of Open Sequential Write Required Zones");

#ifdef CAM_TEST_FAILURE
	/*
	 * Add a 'door bell' sysctl which allows one to set it from userland
	 * and cause something bad to happen.  For the moment, we only allow
	 * whacking the next read or write.
	 */
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "force_read_error", CTLFLAG_RW | CTLFLAG_MPSAFE,
		&softc->force_read_error, 0,
		"Force a read error for the next N reads.");
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "force_write_error", CTLFLAG_RW | CTLFLAG_MPSAFE,
		&softc->force_write_error, 0,
		"Force a write error for the next N writes.");
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "periodic_read_error", CTLFLAG_RW | CTLFLAG_MPSAFE,
		&softc->periodic_read_error, 0,
		"Force a read error every N reads (don't set too low).");
	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "invalidate", CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE,
		periph, 0, cam_periph_invalidate_sysctl, "I",
		"Write 1 to invalidate the drive immediately");
#endif

#ifdef CAM_IO_STATS
	softc->sysctl_stats_tree = SYSCTL_ADD_NODE(&softc->sysctl_stats_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO, "stats",
		CTLFLAG_RD, 0, "Statistics");
	SYSCTL_ADD_INT(&softc->sysctl_stats_ctx,
		SYSCTL_CHILDREN(softc->sysctl_stats_tree),
		OID_AUTO, "timeouts", CTLFLAG_RD | CTLFLAG_MPSAFE,
		&softc->timeouts, 0,
		"Device timeouts reported by the SIM");
	SYSCTL_ADD_INT(&softc->sysctl_stats_ctx,
		SYSCTL_CHILDREN(softc->sysctl_stats_tree),
		OID_AUTO, "errors", CTLFLAG_RD | CTLFLAG_MPSAFE,
		&softc->errors, 0,
		"Transport errors reported by the SIM.");
	SYSCTL_ADD_INT(&softc->sysctl_stats_ctx,
		SYSCTL_CHILDREN(softc->sysctl_stats_tree),
		OID_AUTO, "pack_invalidations", CTLFLAG_RD | CTLFLAG_MPSAFE,
		&softc->invalidations, 0,
		"Device pack invalidations.");
#endif

	cam_iosched_sysctl_init(softc->cam_iosched, &softc->sysctl_ctx,
	    softc->sysctl_tree);

	cam_periph_release(periph);
}

static int
adagetattr(struct bio *bp)
{
	int ret;
	struct cam_periph *periph;

	periph = (struct cam_periph *)bp->bio_disk->d_drv1;
	cam_periph_lock(periph);
	ret = xpt_getattr(bp->bio_data, bp->bio_length, bp->bio_attribute,
	    periph->path);
	cam_periph_unlock(periph);
	if (ret == 0)
		bp->bio_completed = bp->bio_length;
	return ret;
}

static int
adadeletemethodsysctl(SYSCTL_HANDLER_ARGS)
{
	char buf[16];
	const char *p;
	struct ada_softc *softc;
	int i, error, value, methods;

	softc = (struct ada_softc *)arg1;

	value = softc->delete_method;
	if (value < 0 || value > ADA_DELETE_MAX)
		p = "UNKNOWN";
	else
		p = ada_delete_method_names[value];
	strncpy(buf, p, sizeof(buf));
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	methods = 1 << ADA_DELETE_DISABLE;
	if ((softc->flags & ADA_FLAG_CAN_CFA) &&
	    !(softc->flags & ADA_FLAG_CAN_48BIT))
		methods |= 1 << ADA_DELETE_CFA_ERASE;
	if (softc->flags & ADA_FLAG_CAN_TRIM)
		methods |= 1 << ADA_DELETE_DSM_TRIM;
	if (softc->flags & ADA_FLAG_CAN_NCQ_TRIM)
		methods |= 1 << ADA_DELETE_NCQ_DSM_TRIM;
	for (i = 0; i <= ADA_DELETE_MAX; i++) {
		if (!(methods & (1 << i)) ||
		    strcmp(buf, ada_delete_method_names[i]) != 0)
			continue;
		softc->delete_method = i;
		return (0);
	}
	return (EINVAL);
}

static void
adasetflags(struct ada_softc *softc, struct ccb_getdev *cgd)
{
	if ((cgd->ident_data.capabilities1 & ATA_SUPPORT_DMA) &&
	    (cgd->inq_flags & SID_DMA))
		softc->flags |= ADA_FLAG_CAN_DMA;
	else
		softc->flags &= ~ADA_FLAG_CAN_DMA;

	if (cgd->ident_data.support.command2 & ATA_SUPPORT_ADDRESS48) {
		softc->flags |= ADA_FLAG_CAN_48BIT;
		if (cgd->inq_flags & SID_DMA48)
			softc->flags |= ADA_FLAG_CAN_DMA48;
		else
			softc->flags &= ~ADA_FLAG_CAN_DMA48;
	} else
		softc->flags &= ~(ADA_FLAG_CAN_48BIT | ADA_FLAG_CAN_DMA48);

	if (cgd->ident_data.support.command2 & ATA_SUPPORT_FLUSHCACHE)
		softc->flags |= ADA_FLAG_CAN_FLUSHCACHE;
	else
		softc->flags &= ~ADA_FLAG_CAN_FLUSHCACHE;

	if (cgd->ident_data.support.command1 & ATA_SUPPORT_POWERMGT)
		softc->flags |= ADA_FLAG_CAN_POWERMGT;
	else
		softc->flags &= ~ADA_FLAG_CAN_POWERMGT;

	if ((cgd->ident_data.satacapabilities & ATA_SUPPORT_NCQ) &&
	    (cgd->inq_flags & SID_DMA) && (cgd->inq_flags & SID_CmdQue))
		softc->flags |= ADA_FLAG_CAN_NCQ;
	else
		softc->flags &= ~ADA_FLAG_CAN_NCQ;

	if ((cgd->ident_data.support_dsm & ATA_SUPPORT_DSM_TRIM) &&
	    (cgd->inq_flags & SID_DMA)) {
		softc->flags |= ADA_FLAG_CAN_TRIM;
		softc->trim_max_ranges = TRIM_MAX_RANGES;
		if (cgd->ident_data.max_dsm_blocks != 0) {
			softc->trim_max_ranges =
			    min(cgd->ident_data.max_dsm_blocks *
				ATA_DSM_BLK_RANGES, softc->trim_max_ranges);
		}
		/*
		 * If we can do RCVSND_FPDMA_QUEUED commands, we may be able
		 * to do NCQ trims, if we support trims at all. We also need
		 * support from the SIM to do things properly. Perhaps we
		 * should look at log 13 dword 0 bit 0 and dword 1 bit 0 are
		 * set too...
		 */
		if ((softc->quirks & ADA_Q_NCQ_TRIM_BROKEN) == 0 &&
		    (softc->flags & ADA_FLAG_PIM_ATA_EXT) != 0 &&
		    (cgd->ident_data.satacapabilities2 &
		     ATA_SUPPORT_RCVSND_FPDMA_QUEUED) != 0 &&
		    (softc->flags & ADA_FLAG_CAN_TRIM) != 0)
			softc->flags |= ADA_FLAG_CAN_NCQ_TRIM;
		else
			softc->flags &= ~ADA_FLAG_CAN_NCQ_TRIM;
	} else
		softc->flags &= ~(ADA_FLAG_CAN_TRIM | ADA_FLAG_CAN_NCQ_TRIM);

	if (cgd->ident_data.support.command2 & ATA_SUPPORT_CFA)
		softc->flags |= ADA_FLAG_CAN_CFA;
	else
		softc->flags &= ~ADA_FLAG_CAN_CFA;

	/*
	 * Now that we've set the appropriate flags, setup the delete
	 * method.
	 */
	adasetdeletemethod(softc);

	if ((cgd->ident_data.support.extension & ATA_SUPPORT_GENLOG)
	 && ((softc->quirks & ADA_Q_LOG_BROKEN) == 0))
		softc->flags |= ADA_FLAG_CAN_LOG;
	else
		softc->flags &= ~ADA_FLAG_CAN_LOG;

	if ((cgd->ident_data.support3 & ATA_SUPPORT_ZONE_MASK) ==
	     ATA_SUPPORT_ZONE_HOST_AWARE)
		softc->zone_mode = ADA_ZONE_HOST_AWARE;
	else if (((cgd->ident_data.support3 & ATA_SUPPORT_ZONE_MASK) ==
		   ATA_SUPPORT_ZONE_DEV_MANAGED)
	      || (softc->quirks & ADA_Q_SMR_DM))
		softc->zone_mode = ADA_ZONE_DRIVE_MANAGED;
	else
		softc->zone_mode = ADA_ZONE_NONE;

	if (cgd->ident_data.support.command1 & ATA_SUPPORT_LOOKAHEAD)
		softc->flags |= ADA_FLAG_CAN_RAHEAD;
	else
		softc->flags &= ~ADA_FLAG_CAN_RAHEAD;

	if (cgd->ident_data.support.command1 & ATA_SUPPORT_WRITECACHE)
		softc->flags |= ADA_FLAG_CAN_WCACHE;
	else
		softc->flags &= ~ADA_FLAG_CAN_WCACHE;
}

static cam_status
adaregister(struct cam_periph *periph, void *arg)
{
	struct ada_softc *softc;
	struct ccb_pathinq cpi;
	struct ccb_getdev *cgd;
	struct disk_params *dp;
	struct sbuf sb;
	char   *announce_buf;
	caddr_t match;
	u_int maxio;
	int quirks;

	cgd = (struct ccb_getdev *)arg;
	if (cgd == NULL) {
		printf("adaregister: no getdev CCB, can't register device\n");
		return(CAM_REQ_CMP_ERR);
	}

	softc = (struct ada_softc *)malloc(sizeof(*softc), M_DEVBUF,
	    M_NOWAIT|M_ZERO);

	if (softc == NULL) {
		printf("adaregister: Unable to probe new device. "
		    "Unable to allocate softc\n");
		return(CAM_REQ_CMP_ERR);
	}

	announce_buf = softc->announce_temp;
	bzero(announce_buf, ADA_ANNOUNCETMP_SZ);

	if (cam_iosched_init(&softc->cam_iosched, periph) != 0) {
		printf("adaregister: Unable to probe new device. "
		       "Unable to allocate iosched memory\n");
		free(softc, M_DEVBUF);
		return(CAM_REQ_CMP_ERR);
	}

	periph->softc = softc;

	/*
	 * See if this device has any quirks.
	 */
	match = cam_quirkmatch((caddr_t)&cgd->ident_data,
			       (caddr_t)ada_quirk_table,
			       nitems(ada_quirk_table),
			       sizeof(*ada_quirk_table), ata_identify_match);
	if (match != NULL)
		softc->quirks = ((struct ada_quirk_entry *)match)->quirks;
	else
		softc->quirks = ADA_Q_NONE;

	xpt_path_inq(&cpi, periph->path);

	TASK_INIT(&softc->sysctl_task, 0, adasysctlinit, periph);

	/*
	 * Register this media as a disk
	 */
	(void)cam_periph_hold(periph, PRIBIO);
	cam_periph_unlock(periph);
	snprintf(announce_buf, ADA_ANNOUNCETMP_SZ,
	    "kern.cam.ada.%d.quirks", periph->unit_number);
	quirks = softc->quirks;
	TUNABLE_INT_FETCH(announce_buf, &quirks);
	softc->quirks = quirks;
	softc->read_ahead = -1;
	snprintf(announce_buf, ADA_ANNOUNCETMP_SZ,
	    "kern.cam.ada.%d.read_ahead", periph->unit_number);
	TUNABLE_INT_FETCH(announce_buf, &softc->read_ahead);
	softc->write_cache = -1;
	snprintf(announce_buf, ADA_ANNOUNCETMP_SZ,
	    "kern.cam.ada.%d.write_cache", periph->unit_number);
	TUNABLE_INT_FETCH(announce_buf, &softc->write_cache);

	/*
	 * Set support flags based on the Identify data and quirks.
	 */
	adasetflags(softc, cgd);

	/* Disable queue sorting for non-rotational media by default. */
	if (cgd->ident_data.media_rotation_rate == ATA_RATE_NON_ROTATING) {
		softc->rotating = 0;
	} else {
		softc->rotating = 1;
	}
	cam_iosched_set_sort_queue(softc->cam_iosched,  softc->rotating ? -1 : 0);
	adagetparams(periph, cgd);
	softc->disk = disk_alloc();
	softc->disk->d_rotation_rate = cgd->ident_data.media_rotation_rate;
	softc->disk->d_devstat = devstat_new_entry(periph->periph_name,
			  periph->unit_number, softc->params.secsize,
			  DEVSTAT_ALL_SUPPORTED,
			  DEVSTAT_TYPE_DIRECT |
			  XPORT_DEVSTAT_TYPE(cpi.transport),
			  DEVSTAT_PRIORITY_DISK);
	softc->disk->d_open = adaopen;
	softc->disk->d_close = adaclose;
	softc->disk->d_strategy = adastrategy;
	softc->disk->d_getattr = adagetattr;
	softc->disk->d_dump = adadump;
	softc->disk->d_gone = adadiskgonecb;
	softc->disk->d_name = "ada";
	softc->disk->d_drv1 = periph;
	maxio = cpi.maxio;		/* Honor max I/O size of SIM */
	if (maxio == 0)
		maxio = DFLTPHYS;	/* traditional default */
	else if (maxio > MAXPHYS)
		maxio = MAXPHYS;	/* for safety */
	if (softc->flags & ADA_FLAG_CAN_48BIT)
		maxio = min(maxio, 65536 * softc->params.secsize);
	else					/* 28bit ATA command limit */
		maxio = min(maxio, 256 * softc->params.secsize);
	if (softc->quirks & ADA_Q_128KB)
		maxio = min(maxio, 128 * 1024);
	softc->disk->d_maxsize = maxio;
	softc->disk->d_unit = periph->unit_number;
	softc->disk->d_flags = DISKFLAG_DIRECT_COMPLETION | DISKFLAG_CANZONE;
	if (softc->flags & ADA_FLAG_CAN_FLUSHCACHE)
		softc->disk->d_flags |= DISKFLAG_CANFLUSHCACHE;
	/* Device lies about TRIM capability. */
	if ((softc->quirks & ADA_Q_NO_TRIM) &&
	    (softc->flags & ADA_FLAG_CAN_TRIM))
		softc->flags &= ~ADA_FLAG_CAN_TRIM;
	if (softc->flags & ADA_FLAG_CAN_TRIM) {
		softc->disk->d_flags |= DISKFLAG_CANDELETE;
		softc->disk->d_delmaxsize = softc->params.secsize *
					    ATA_DSM_RANGE_MAX *
					    softc->trim_max_ranges;
	} else if ((softc->flags & ADA_FLAG_CAN_CFA) &&
	    !(softc->flags & ADA_FLAG_CAN_48BIT)) {
		softc->disk->d_flags |= DISKFLAG_CANDELETE;
		softc->disk->d_delmaxsize = 256 * softc->params.secsize;
	} else
		softc->disk->d_delmaxsize = maxio;
	if ((cpi.hba_misc & PIM_UNMAPPED) != 0) {
		softc->disk->d_flags |= DISKFLAG_UNMAPPED_BIO;
		softc->unmappedio = 1;
	}
	if (cpi.hba_misc & PIM_ATA_EXT)
		softc->flags |= ADA_FLAG_PIM_ATA_EXT;
	strlcpy(softc->disk->d_descr, cgd->ident_data.model,
	    MIN(sizeof(softc->disk->d_descr), sizeof(cgd->ident_data.model)));
	strlcpy(softc->disk->d_ident, cgd->ident_data.serial,
	    MIN(sizeof(softc->disk->d_ident), sizeof(cgd->ident_data.serial)));
	softc->disk->d_hba_vendor = cpi.hba_vendor;
	softc->disk->d_hba_device = cpi.hba_device;
	softc->disk->d_hba_subvendor = cpi.hba_subvendor;
	softc->disk->d_hba_subdevice = cpi.hba_subdevice;

	softc->disk->d_sectorsize = softc->params.secsize;
	softc->disk->d_mediasize = (off_t)softc->params.sectors *
	    softc->params.secsize;
	if (ata_physical_sector_size(&cgd->ident_data) !=
	    softc->params.secsize) {
		softc->disk->d_stripesize =
		    ata_physical_sector_size(&cgd->ident_data);
		softc->disk->d_stripeoffset = (softc->disk->d_stripesize -
		    ata_logical_sector_offset(&cgd->ident_data)) %
		    softc->disk->d_stripesize;
	} else if (softc->quirks & ADA_Q_4K) {
		softc->disk->d_stripesize = 4096;
		softc->disk->d_stripeoffset = 0;
	}
	softc->disk->d_fwsectors = softc->params.secs_per_track;
	softc->disk->d_fwheads = softc->params.heads;
	ata_disk_firmware_geom_adjust(softc->disk);

	/*
	 * Acquire a reference to the periph before we register with GEOM.
	 * We'll release this reference once GEOM calls us back (via
	 * adadiskgonecb()) telling us that our provider has been freed.
	 */
	if (cam_periph_acquire(periph) != 0) {
		xpt_print(periph->path, "%s: lost periph during "
			  "registration!\n", __func__);
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}
	disk_create(softc->disk, DISK_VERSION);
	cam_periph_lock(periph);

	dp = &softc->params;
	snprintf(announce_buf, ADA_ANNOUNCETMP_SZ,
	    "%juMB (%ju %u byte sectors)",
	    ((uintmax_t)dp->secsize * dp->sectors) / (1024 * 1024),
	    (uintmax_t)dp->sectors, dp->secsize);

	sbuf_new(&sb, softc->announce_buffer, ADA_ANNOUNCE_SZ, SBUF_FIXEDLEN);
	xpt_announce_periph_sbuf(periph, &sb, announce_buf);
	xpt_announce_quirks_sbuf(periph, &sb, softc->quirks, ADA_Q_BIT_STRING);
	sbuf_finish(&sb);
	sbuf_putbuf(&sb);

	/*
	 * Create our sysctl variables, now that we know
	 * we have successfully attached.
	 */
	if (cam_periph_acquire(periph) == 0)
		taskqueue_enqueue(taskqueue_thread, &softc->sysctl_task);

	/*
	 * Add async callbacks for bus reset and
	 * bus device reset calls.  I don't bother
	 * checking if this fails as, in most cases,
	 * the system will function just fine without
	 * them and the only alternative would be to
	 * not attach the device on failure.
	 */
	xpt_register_async(AC_SENT_BDR | AC_BUS_RESET | AC_LOST_DEVICE |
	    AC_GETDEV_CHANGED | AC_ADVINFO_CHANGED,
	    adaasync, periph, periph->path);

	/*
	 * Schedule a periodic event to occasionally send an
	 * ordered tag to a device.
	 */
	callout_init_mtx(&softc->sendordered_c, cam_periph_mtx(periph), 0);
	callout_reset(&softc->sendordered_c,
	    (ada_default_timeout * hz) / ADA_ORDEREDTAG_INTERVAL,
	    adasendorderedtag, softc);

	if (ADA_RA >= 0 && softc->flags & ADA_FLAG_CAN_RAHEAD) {
		softc->state = ADA_STATE_RAHEAD;
	} else if (ADA_WC >= 0 && softc->flags & ADA_FLAG_CAN_WCACHE) {
		softc->state = ADA_STATE_WCACHE;
	} else if ((softc->flags & ADA_FLAG_CAN_LOG)
		&& (softc->zone_mode != ADA_ZONE_NONE)) {
		softc->state = ADA_STATE_LOGDIR;
	} else {
		/*
		 * Nothing to probe, so we can just transition to the
		 * normal state.
		 */
		adaprobedone(periph, NULL);
		return(CAM_REQ_CMP);
	}

	xpt_schedule(periph, CAM_PRIORITY_DEV);

	return(CAM_REQ_CMP);
}

static int
ada_dsmtrim_req_create(struct ada_softc *softc, struct bio *bp, struct trim_request *req)
{
	uint64_t lastlba = (uint64_t)-1, lbas = 0;
	int c, lastcount = 0, off, ranges = 0;

	bzero(req, sizeof(*req));
	TAILQ_INIT(&req->bps);
	do {
		uint64_t lba = bp->bio_pblkno;
		int count = bp->bio_bcount / softc->params.secsize;

		/* Try to extend the previous range. */
		if (lba == lastlba) {
			c = min(count, ATA_DSM_RANGE_MAX - lastcount);
			lastcount += c;
			off = (ranges - 1) * ATA_DSM_RANGE_SIZE;
			req->data[off + 6] = lastcount & 0xff;
			req->data[off + 7] =
				(lastcount >> 8) & 0xff;
			count -= c;
			lba += c;
			lbas += c;
		}

		while (count > 0) {
			c = min(count, ATA_DSM_RANGE_MAX);
			off = ranges * ATA_DSM_RANGE_SIZE;
			req->data[off + 0] = lba & 0xff;
			req->data[off + 1] = (lba >> 8) & 0xff;
			req->data[off + 2] = (lba >> 16) & 0xff;
			req->data[off + 3] = (lba >> 24) & 0xff;
			req->data[off + 4] = (lba >> 32) & 0xff;
			req->data[off + 5] = (lba >> 40) & 0xff;
			req->data[off + 6] = c & 0xff;
			req->data[off + 7] = (c >> 8) & 0xff;
			lba += c;
			lbas += c;
			count -= c;
			lastcount = c;
			ranges++;
			/*
			 * Its the caller's responsibility to ensure the
			 * request will fit so we don't need to check for
			 * overrun here
			 */
		}
		lastlba = lba;
		TAILQ_INSERT_TAIL(&req->bps, bp, bio_queue);

		bp = cam_iosched_next_trim(softc->cam_iosched);
		if (bp == NULL)
			break;
		if (bp->bio_bcount / softc->params.secsize >
		    (softc->trim_max_ranges - ranges) * ATA_DSM_RANGE_MAX) {
			cam_iosched_put_back_trim(softc->cam_iosched, bp);
			break;
		}
	} while (1);
	softc->trim_count++;
	softc->trim_ranges += ranges;
	softc->trim_lbas += lbas;

	return (ranges);
}

static void
ada_dsmtrim(struct ada_softc *softc, struct bio *bp, struct ccb_ataio *ataio)
{
	struct trim_request *req = &softc->trim_req;
	int ranges;

	ranges = ada_dsmtrim_req_create(softc, bp, req);
	cam_fill_ataio(ataio,
	    ada_retry_count,
	    adadone,
	    CAM_DIR_OUT,
	    0,
	    req->data,
	    howmany(ranges, ATA_DSM_BLK_RANGES) * ATA_DSM_BLK_SIZE,
	    ada_default_timeout * 1000);
	ata_48bit_cmd(ataio, ATA_DATA_SET_MANAGEMENT,
	    ATA_DSM_TRIM, 0, howmany(ranges, ATA_DSM_BLK_RANGES));
}

static void
ada_ncq_dsmtrim(struct ada_softc *softc, struct bio *bp, struct ccb_ataio *ataio)
{
	struct trim_request *req = &softc->trim_req;
	int ranges;

	ranges = ada_dsmtrim_req_create(softc, bp, req);
	cam_fill_ataio(ataio,
	    ada_retry_count,
	    adadone,
	    CAM_DIR_OUT,
	    0,
	    req->data,
	    howmany(ranges, ATA_DSM_BLK_RANGES) * ATA_DSM_BLK_SIZE,
	    ada_default_timeout * 1000);
	ata_ncq_cmd(ataio,
	    ATA_SEND_FPDMA_QUEUED,
	    0,
	    howmany(ranges, ATA_DSM_BLK_RANGES));
	ataio->cmd.sector_count_exp = ATA_SFPDMA_DSM;
	ataio->ata_flags |= ATA_FLAG_AUX;
	ataio->aux = 1;
}

static void
ada_cfaerase(struct ada_softc *softc, struct bio *bp, struct ccb_ataio *ataio)
{
	struct trim_request *req = &softc->trim_req;
	uint64_t lba = bp->bio_pblkno;
	uint16_t count = bp->bio_bcount / softc->params.secsize;

	bzero(req, sizeof(*req));
	TAILQ_INIT(&req->bps);
	TAILQ_INSERT_TAIL(&req->bps, bp, bio_queue);

	cam_fill_ataio(ataio,
	    ada_retry_count,
	    adadone,
	    CAM_DIR_NONE,
	    0,
	    NULL,
	    0,
	    ada_default_timeout*1000);

	if (count >= 256)
		count = 0;
	ata_28bit_cmd(ataio, ATA_CFA_ERASE, 0, lba, count);
}

static int
ada_zone_bio_to_ata(int disk_zone_cmd)
{
	switch (disk_zone_cmd) {
	case DISK_ZONE_OPEN:
		return ATA_ZM_OPEN_ZONE;
	case DISK_ZONE_CLOSE:
		return ATA_ZM_CLOSE_ZONE;
	case DISK_ZONE_FINISH:
		return ATA_ZM_FINISH_ZONE;
	case DISK_ZONE_RWP:
		return ATA_ZM_RWP;
	}

	return -1;
}

static int
ada_zone_cmd(struct cam_periph *periph, union ccb *ccb, struct bio *bp,
	     int *queue_ccb)
{
	struct ada_softc *softc;
	int error;

	error = 0;

	if (bp->bio_cmd != BIO_ZONE) {
		error = EINVAL;
		goto bailout;
	}

	softc = periph->softc;

	switch (bp->bio_zone.zone_cmd) {
	case DISK_ZONE_OPEN:
	case DISK_ZONE_CLOSE:
	case DISK_ZONE_FINISH:
	case DISK_ZONE_RWP: {
		int zone_flags;
		int zone_sa;
		uint64_t lba;

		zone_sa = ada_zone_bio_to_ata(bp->bio_zone.zone_cmd);
		if (zone_sa == -1) {
			xpt_print(periph->path, "Cannot translate zone "
			    "cmd %#x to ATA\n", bp->bio_zone.zone_cmd);
			error = EINVAL;
			goto bailout;
		}

		zone_flags = 0;
		lba = bp->bio_zone.zone_params.rwp.id;

		if (bp->bio_zone.zone_params.rwp.flags &
		    DISK_ZONE_RWP_FLAG_ALL)
			zone_flags |= ZBC_OUT_ALL;

		ata_zac_mgmt_out(&ccb->ataio,
				 /*retries*/ ada_retry_count,
				 /*cbfcnp*/ adadone,
				 /*use_ncq*/ (softc->flags &
					      ADA_FLAG_PIM_ATA_EXT) ? 1 : 0,
				 /*zm_action*/ zone_sa,
				 /*zone_id*/ lba,
				 /*zone_flags*/ zone_flags,
				 /*sector_count*/ 0,
				 /*data_ptr*/ NULL,
				 /*dxfer_len*/ 0,
				 /*timeout*/ ada_default_timeout * 1000);
		*queue_ccb = 1;

		break;
	}
	case DISK_ZONE_REPORT_ZONES: {
		uint8_t *rz_ptr;
		uint32_t num_entries, alloc_size;
		struct disk_zone_report *rep;

		rep = &bp->bio_zone.zone_params.report;

		num_entries = rep->entries_allocated;
		if (num_entries == 0) {
			xpt_print(periph->path, "No entries allocated for "
			    "Report Zones request\n");
			error = EINVAL;
			goto bailout;
		}
		alloc_size = sizeof(struct scsi_report_zones_hdr) +
		    (sizeof(struct scsi_report_zones_desc) * num_entries);
		alloc_size = min(alloc_size, softc->disk->d_maxsize);
		rz_ptr = malloc(alloc_size, M_ATADA, M_NOWAIT | M_ZERO);
		if (rz_ptr == NULL) {
			xpt_print(periph->path, "Unable to allocate memory "
			   "for Report Zones request\n");
			error = ENOMEM;
			goto bailout;
		}

		ata_zac_mgmt_in(&ccb->ataio,
				/*retries*/ ada_retry_count,
				/*cbcfnp*/ adadone,
				/*use_ncq*/ (softc->flags &
					     ADA_FLAG_PIM_ATA_EXT) ? 1 : 0,
				/*zm_action*/ ATA_ZM_REPORT_ZONES,
				/*zone_id*/ rep->starting_id,
				/*zone_flags*/ rep->rep_options,
				/*data_ptr*/ rz_ptr,
				/*dxfer_len*/ alloc_size,
				/*timeout*/ ada_default_timeout * 1000);

		/*
		 * For BIO_ZONE, this isn't normally needed.  However, it
		 * is used by devstat_end_transaction_bio() to determine
		 * how much data was transferred.
		 */
		/*
		 * XXX KDM we have a problem.  But I'm not sure how to fix
		 * it.  devstat uses bio_bcount - bio_resid to calculate
		 * the amount of data transferred.   The GEOM disk code
		 * uses bio_length - bio_resid to calculate the amount of
		 * data in bio_completed.  We have different structure
		 * sizes above and below the ada(4) driver.  So, if we
		 * use the sizes above, the amount transferred won't be
		 * quite accurate for devstat.  If we use different sizes
		 * for bio_bcount and bio_length (above and below
		 * respectively), then the residual needs to match one or
		 * the other.  Everything is calculated after the bio
		 * leaves the driver, so changing the values around isn't
		 * really an option.  For now, just set the count to the
		 * passed in length.  This means that the calculations
		 * above (e.g. bio_completed) will be correct, but the
		 * amount of data reported to devstat will be slightly
		 * under or overstated.
		 */
		bp->bio_bcount = bp->bio_length;

		*queue_ccb = 1;

		break;
	}
	case DISK_ZONE_GET_PARAMS: {
		struct disk_zone_disk_params *params;

		params = &bp->bio_zone.zone_params.disk_params;
		bzero(params, sizeof(*params));

		switch (softc->zone_mode) {
		case ADA_ZONE_DRIVE_MANAGED:
			params->zone_mode = DISK_ZONE_MODE_DRIVE_MANAGED;
			break;
		case ADA_ZONE_HOST_AWARE:
			params->zone_mode = DISK_ZONE_MODE_HOST_AWARE;
			break;
		case ADA_ZONE_HOST_MANAGED:
			params->zone_mode = DISK_ZONE_MODE_HOST_MANAGED;
			break;
		default:
		case ADA_ZONE_NONE:
			params->zone_mode = DISK_ZONE_MODE_NONE;
			break;
		}

		if (softc->zone_flags & ADA_ZONE_FLAG_URSWRZ)
			params->flags |= DISK_ZONE_DISK_URSWRZ;

		if (softc->zone_flags & ADA_ZONE_FLAG_OPT_SEQ_SET) {
			params->optimal_seq_zones = softc->optimal_seq_zones;
			params->flags |= DISK_ZONE_OPT_SEQ_SET;
		}

		if (softc->zone_flags & ADA_ZONE_FLAG_OPT_NONSEQ_SET) {
			params->optimal_nonseq_zones =
			    softc->optimal_nonseq_zones;
			params->flags |= DISK_ZONE_OPT_NONSEQ_SET;
		}

		if (softc->zone_flags & ADA_ZONE_FLAG_MAX_SEQ_SET) {
			params->max_seq_zones = softc->max_seq_zones;
			params->flags |= DISK_ZONE_MAX_SEQ_SET;
		}
		if (softc->zone_flags & ADA_ZONE_FLAG_RZ_SUP)
			params->flags |= DISK_ZONE_RZ_SUP;

		if (softc->zone_flags & ADA_ZONE_FLAG_OPEN_SUP)
			params->flags |= DISK_ZONE_OPEN_SUP;

		if (softc->zone_flags & ADA_ZONE_FLAG_CLOSE_SUP)
			params->flags |= DISK_ZONE_CLOSE_SUP;

		if (softc->zone_flags & ADA_ZONE_FLAG_FINISH_SUP)
			params->flags |= DISK_ZONE_FINISH_SUP;

		if (softc->zone_flags & ADA_ZONE_FLAG_RWP_SUP)
			params->flags |= DISK_ZONE_RWP_SUP;
		break;
	}
	default:
		break;
	}
bailout:
	return (error);
}

static void
adastart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct ada_softc *softc = (struct ada_softc *)periph->softc;
	struct ccb_ataio *ataio = &start_ccb->ataio;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("adastart\n"));

	switch (softc->state) {
	case ADA_STATE_NORMAL:
	{
		struct bio *bp;
		u_int8_t tag_code;

		bp = cam_iosched_next_bio(softc->cam_iosched);
		if (bp == NULL) {
			xpt_release_ccb(start_ccb);
			break;
		}

		if ((bp->bio_flags & BIO_ORDERED) != 0 ||
		    (bp->bio_cmd != BIO_DELETE && (softc->flags & ADA_FLAG_NEED_OTAG) != 0)) {
			softc->flags &= ~ADA_FLAG_NEED_OTAG;
			softc->flags |= ADA_FLAG_WAS_OTAG;
			tag_code = 0;
		} else {
			tag_code = 1;
		}
		switch (bp->bio_cmd) {
		case BIO_WRITE:
		case BIO_READ:
		{
			uint64_t lba = bp->bio_pblkno;
			uint16_t count = bp->bio_bcount / softc->params.secsize;
			void *data_ptr;
			int rw_op;

			if (bp->bio_cmd == BIO_WRITE) {
				softc->flags |= ADA_FLAG_DIRTY;
				rw_op = CAM_DIR_OUT;
			} else {
				rw_op = CAM_DIR_IN;
			}

			data_ptr = bp->bio_data;
			if ((bp->bio_flags & (BIO_UNMAPPED|BIO_VLIST)) != 0) {
				rw_op |= CAM_DATA_BIO;
				data_ptr = bp;
			}

#ifdef CAM_TEST_FAILURE
			int fail = 0;

			/*
			 * Support the failure ioctls.  If the command is a
			 * read, and there are pending forced read errors, or
			 * if a write and pending write errors, then fail this
			 * operation with EIO.  This is useful for testing
			 * purposes.  Also, support having every Nth read fail.
			 *
			 * This is a rather blunt tool.
			 */
			if (bp->bio_cmd == BIO_READ) {
				if (softc->force_read_error) {
					softc->force_read_error--;
					fail = 1;
				}
				if (softc->periodic_read_error > 0) {
					if (++softc->periodic_read_count >=
					    softc->periodic_read_error) {
						softc->periodic_read_count = 0;
						fail = 1;
					}
				}
			} else {
				if (softc->force_write_error) {
					softc->force_write_error--;
					fail = 1;
				}
			}
			if (fail) {
				biofinish(bp, NULL, EIO);
				xpt_release_ccb(start_ccb);
				adaschedule(periph);
				return;
			}
#endif
			KASSERT((bp->bio_flags & BIO_UNMAPPED) == 0 ||
			    round_page(bp->bio_bcount + bp->bio_ma_offset) /
			    PAGE_SIZE == bp->bio_ma_n,
			    ("Short bio %p", bp));
			cam_fill_ataio(ataio,
			    ada_retry_count,
			    adadone,
			    rw_op,
			    0,
			    data_ptr,
			    bp->bio_bcount,
			    ada_default_timeout*1000);

			if ((softc->flags & ADA_FLAG_CAN_NCQ) && tag_code) {
				if (bp->bio_cmd == BIO_READ) {
					ata_ncq_cmd(ataio, ATA_READ_FPDMA_QUEUED,
					    lba, count);
				} else {
					ata_ncq_cmd(ataio, ATA_WRITE_FPDMA_QUEUED,
					    lba, count);
				}
			} else if ((softc->flags & ADA_FLAG_CAN_48BIT) &&
			    (lba + count >= ATA_MAX_28BIT_LBA ||
			    count > 256)) {
				if (softc->flags & ADA_FLAG_CAN_DMA48) {
					if (bp->bio_cmd == BIO_READ) {
						ata_48bit_cmd(ataio, ATA_READ_DMA48,
						    0, lba, count);
					} else {
						ata_48bit_cmd(ataio, ATA_WRITE_DMA48,
						    0, lba, count);
					}
				} else {
					if (bp->bio_cmd == BIO_READ) {
						ata_48bit_cmd(ataio, ATA_READ_MUL48,
						    0, lba, count);
					} else {
						ata_48bit_cmd(ataio, ATA_WRITE_MUL48,
						    0, lba, count);
					}
				}
			} else {
				if (count == 256)
					count = 0;
				if (softc->flags & ADA_FLAG_CAN_DMA) {
					if (bp->bio_cmd == BIO_READ) {
						ata_28bit_cmd(ataio, ATA_READ_DMA,
						    0, lba, count);
					} else {
						ata_28bit_cmd(ataio, ATA_WRITE_DMA,
						    0, lba, count);
					}
				} else {
					if (bp->bio_cmd == BIO_READ) {
						ata_28bit_cmd(ataio, ATA_READ_MUL,
						    0, lba, count);
					} else {
						ata_28bit_cmd(ataio, ATA_WRITE_MUL,
						    0, lba, count);
					}
				}
			}
			break;
		}
		case BIO_DELETE:
			switch (softc->delete_method) {
			case ADA_DELETE_NCQ_DSM_TRIM:
				ada_ncq_dsmtrim(softc, bp, ataio);
				break;
			case ADA_DELETE_DSM_TRIM:
				ada_dsmtrim(softc, bp, ataio);
				break;
			case ADA_DELETE_CFA_ERASE:
				ada_cfaerase(softc, bp, ataio);
				break;
			default:
				biofinish(bp, NULL, EOPNOTSUPP);
				xpt_release_ccb(start_ccb);
				adaschedule(periph);
				return;
			}
			start_ccb->ccb_h.ccb_state = ADA_CCB_TRIM;
			start_ccb->ccb_h.flags |= CAM_UNLOCKED;
			cam_iosched_submit_trim(softc->cam_iosched);
			goto out;
		case BIO_FLUSH:
			cam_fill_ataio(ataio,
			    1,
			    adadone,
			    CAM_DIR_NONE,
			    0,
			    NULL,
			    0,
			    ada_default_timeout*1000);

			if (softc->flags & ADA_FLAG_CAN_48BIT)
				ata_48bit_cmd(ataio, ATA_FLUSHCACHE48, 0, 0, 0);
			else
				ata_28bit_cmd(ataio, ATA_FLUSHCACHE, 0, 0, 0);
			break;
		case BIO_ZONE: {
			int error, queue_ccb;

			queue_ccb = 0;

			error = ada_zone_cmd(periph, start_ccb, bp, &queue_ccb);
			if ((error != 0)
			 || (queue_ccb == 0)) {
				biofinish(bp, NULL, error);
				xpt_release_ccb(start_ccb);
				return;
			}
			break;
		}
		}
		start_ccb->ccb_h.ccb_state = ADA_CCB_BUFFER_IO;
		start_ccb->ccb_h.flags |= CAM_UNLOCKED;
out:
		start_ccb->ccb_h.ccb_bp = bp;
		softc->outstanding_cmds++;
		softc->refcount++;
		cam_periph_unlock(periph);
		xpt_action(start_ccb);
		cam_periph_lock(periph);

		/* May have more work to do, so ensure we stay scheduled */
		adaschedule(periph);
		break;
	}
	case ADA_STATE_RAHEAD:
	case ADA_STATE_WCACHE:
	{
		cam_fill_ataio(ataio,
		    1,
		    adadone,
		    CAM_DIR_NONE,
		    0,
		    NULL,
		    0,
		    ada_default_timeout*1000);

		if (softc->state == ADA_STATE_RAHEAD) {
			ata_28bit_cmd(ataio, ATA_SETFEATURES, ADA_RA ?
			    ATA_SF_ENAB_RCACHE : ATA_SF_DIS_RCACHE, 0, 0);
			start_ccb->ccb_h.ccb_state = ADA_CCB_RAHEAD;
		} else {
			ata_28bit_cmd(ataio, ATA_SETFEATURES, ADA_WC ?
			    ATA_SF_ENAB_WCACHE : ATA_SF_DIS_WCACHE, 0, 0);
			start_ccb->ccb_h.ccb_state = ADA_CCB_WCACHE;
		}
		start_ccb->ccb_h.flags |= CAM_DEV_QFREEZE;
		xpt_action(start_ccb);
		break;
	}
	case ADA_STATE_LOGDIR:
	{
		struct ata_gp_log_dir *log_dir;

		if ((softc->flags & ADA_FLAG_CAN_LOG) == 0) {
			adaprobedone(periph, start_ccb);
			break;
		}

		log_dir = malloc(sizeof(*log_dir), M_ATADA, M_NOWAIT|M_ZERO);
		if (log_dir == NULL) {
			xpt_print(periph->path, "Couldn't malloc log_dir "
			    "data\n");
			softc->state = ADA_STATE_NORMAL;
			xpt_release_ccb(start_ccb);
			break;
		}


		ata_read_log(ataio,
		    /*retries*/1,
		    /*cbfcnp*/adadone,
		    /*log_address*/ ATA_LOG_DIRECTORY,
		    /*page_number*/ 0,
		    /*block_count*/ 1,
		    /*protocol*/ softc->flags & ADA_FLAG_CAN_DMA ?
				 CAM_ATAIO_DMA : 0,
		    /*data_ptr*/ (uint8_t *)log_dir,
		    /*dxfer_len*/sizeof(*log_dir),
		    /*timeout*/ada_default_timeout*1000);

		start_ccb->ccb_h.ccb_state = ADA_CCB_LOGDIR;
		xpt_action(start_ccb);
		break;
	}
	case ADA_STATE_IDDIR:
	{
		struct ata_identify_log_pages *id_dir;

		id_dir = malloc(sizeof(*id_dir), M_ATADA, M_NOWAIT | M_ZERO);
		if (id_dir == NULL) {
			xpt_print(periph->path, "Couldn't malloc id_dir "
			    "data\n");
			adaprobedone(periph, start_ccb);
			break;
		}

		ata_read_log(ataio,
		    /*retries*/1,
		    /*cbfcnp*/adadone,
		    /*log_address*/ ATA_IDENTIFY_DATA_LOG,
		    /*page_number*/ ATA_IDL_PAGE_LIST,
		    /*block_count*/ 1,
		    /*protocol*/ softc->flags & ADA_FLAG_CAN_DMA ?
				 CAM_ATAIO_DMA : 0,
		    /*data_ptr*/ (uint8_t *)id_dir,
		    /*dxfer_len*/ sizeof(*id_dir),
		    /*timeout*/ada_default_timeout*1000);

		start_ccb->ccb_h.ccb_state = ADA_CCB_IDDIR;
		xpt_action(start_ccb);
		break;
	}
	case ADA_STATE_SUP_CAP:
	{
		struct ata_identify_log_sup_cap *sup_cap;

		sup_cap = malloc(sizeof(*sup_cap), M_ATADA, M_NOWAIT|M_ZERO);
		if (sup_cap == NULL) {
			xpt_print(periph->path, "Couldn't malloc sup_cap "
			    "data\n");
			adaprobedone(periph, start_ccb);
			break;
		}

		ata_read_log(ataio,
		    /*retries*/1,
		    /*cbfcnp*/adadone,
		    /*log_address*/ ATA_IDENTIFY_DATA_LOG,
		    /*page_number*/ ATA_IDL_SUP_CAP,
		    /*block_count*/ 1,
		    /*protocol*/ softc->flags & ADA_FLAG_CAN_DMA ?
				 CAM_ATAIO_DMA : 0,
		    /*data_ptr*/ (uint8_t *)sup_cap,
		    /*dxfer_len*/ sizeof(*sup_cap),
		    /*timeout*/ada_default_timeout*1000);

		start_ccb->ccb_h.ccb_state = ADA_CCB_SUP_CAP;
		xpt_action(start_ccb);
		break;
	}
	case ADA_STATE_ZONE:
	{
		struct ata_zoned_info_log *ata_zone;

		ata_zone = malloc(sizeof(*ata_zone), M_ATADA, M_NOWAIT|M_ZERO);
		if (ata_zone == NULL) {
			xpt_print(periph->path, "Couldn't malloc ata_zone "
			    "data\n");
			adaprobedone(periph, start_ccb);
			break;
		}

		ata_read_log(ataio,
		    /*retries*/1,
		    /*cbfcnp*/adadone,
		    /*log_address*/ ATA_IDENTIFY_DATA_LOG,
		    /*page_number*/ ATA_IDL_ZDI,
		    /*block_count*/ 1,
		    /*protocol*/ softc->flags & ADA_FLAG_CAN_DMA ?
				 CAM_ATAIO_DMA : 0,
		    /*data_ptr*/ (uint8_t *)ata_zone,
		    /*dxfer_len*/ sizeof(*ata_zone),
		    /*timeout*/ada_default_timeout*1000);

		start_ccb->ccb_h.ccb_state = ADA_CCB_ZONE;
		xpt_action(start_ccb);
		break;
	}
	}
}

static void
adaprobedone(struct cam_periph *periph, union ccb *ccb)
{
	struct ada_softc *softc;

	softc = (struct ada_softc *)periph->softc;

	if (ccb != NULL)
		xpt_release_ccb(ccb);

	softc->state = ADA_STATE_NORMAL;
	softc->flags |= ADA_FLAG_PROBED;
	adaschedule(periph);
	if ((softc->flags & ADA_FLAG_ANNOUNCED) == 0) {
		softc->flags |= ADA_FLAG_ANNOUNCED;
		cam_periph_unhold(periph);
	} else {
		cam_periph_release_locked(periph);
	}
}

static void
adazonedone(struct cam_periph *periph, union ccb *ccb)
{
	struct bio *bp;

	bp = (struct bio *)ccb->ccb_h.ccb_bp;

	switch (bp->bio_zone.zone_cmd) {
	case DISK_ZONE_OPEN:
	case DISK_ZONE_CLOSE:
	case DISK_ZONE_FINISH:
	case DISK_ZONE_RWP:
		break;
	case DISK_ZONE_REPORT_ZONES: {
		uint32_t avail_len;
		struct disk_zone_report *rep;
		struct scsi_report_zones_hdr *hdr;
		struct scsi_report_zones_desc *desc;
		struct disk_zone_rep_entry *entry;
		uint32_t hdr_len, num_avail;
		uint32_t num_to_fill, i;

		rep = &bp->bio_zone.zone_params.report;
		avail_len = ccb->ataio.dxfer_len - ccb->ataio.resid;
		/*
		 * Note that bio_resid isn't normally used for zone
		 * commands, but it is used by devstat_end_transaction_bio()
		 * to determine how much data was transferred.  Because
		 * the size of the SCSI/ATA data structures is different
		 * than the size of the BIO interface structures, the
		 * amount of data actually transferred from the drive will
		 * be different than the amount of data transferred to
		 * the user.
		 */
		hdr = (struct scsi_report_zones_hdr *)ccb->ataio.data_ptr;
		if (avail_len < sizeof(*hdr)) {
			/*
			 * Is there a better error than EIO here?  We asked
			 * for at least the header, and we got less than
			 * that.
			 */
			bp->bio_error = EIO;
			bp->bio_flags |= BIO_ERROR;
			bp->bio_resid = bp->bio_bcount;
			break;
		}

		hdr_len = le32dec(hdr->length);
		if (hdr_len > 0)
			rep->entries_available = hdr_len / sizeof(*desc);
		else
			rep->entries_available = 0;
		/*
		 * NOTE: using the same values for the BIO version of the
		 * same field as the SCSI/ATA values.  This means we could
		 * get some additional values that aren't defined in bio.h
		 * if more values of the same field are defined later.
		 */
		rep->header.same = hdr->byte4 & SRZ_SAME_MASK;
		rep->header.maximum_lba = le64dec(hdr->maximum_lba);
		/*
		 * If the drive reports no entries that match the query,
		 * we're done.
		 */
		if (hdr_len == 0) {
			rep->entries_filled = 0;
			bp->bio_resid = bp->bio_bcount;
			break;
		}

		num_avail = min((avail_len - sizeof(*hdr)) / sizeof(*desc),
				hdr_len / sizeof(*desc));
		/*
		 * If the drive didn't return any data, then we're done.
		 */
		if (num_avail == 0) {
			rep->entries_filled = 0;
			bp->bio_resid = bp->bio_bcount;
			break;
		}

		num_to_fill = min(num_avail, rep->entries_allocated);
		/*
		 * If the user didn't allocate any entries for us to fill,
		 * we're done.
		 */
		if (num_to_fill == 0) {
			rep->entries_filled = 0;
			bp->bio_resid = bp->bio_bcount;
			break;
		}

		for (i = 0, desc = &hdr->desc_list[0], entry=&rep->entries[0];
		     i < num_to_fill; i++, desc++, entry++) {
			/*
			 * NOTE: we're mapping the values here directly
			 * from the SCSI/ATA bit definitions to the bio.h
			 * definitions.  There is also a warning in
			 * disk_zone.h, but the impact is that if
			 * additional values are added in the SCSI/ATA
			 * specs these will be visible to consumers of
			 * this interface.
			 */
			entry->zone_type = desc->zone_type & SRZ_TYPE_MASK;
			entry->zone_condition =
			    (desc->zone_flags & SRZ_ZONE_COND_MASK) >>
			    SRZ_ZONE_COND_SHIFT;
			entry->zone_flags |= desc->zone_flags &
			    (SRZ_ZONE_NON_SEQ|SRZ_ZONE_RESET);
			entry->zone_length = le64dec(desc->zone_length);
			entry->zone_start_lba = le64dec(desc->zone_start_lba);
			entry->write_pointer_lba =
			    le64dec(desc->write_pointer_lba);
		}
		rep->entries_filled = num_to_fill;
		/*
		 * Note that this residual is accurate from the user's
		 * standpoint, but the amount transferred isn't accurate
		 * from the standpoint of what actually came back from the
		 * drive.
		 */
		bp->bio_resid = bp->bio_bcount - (num_to_fill * sizeof(*entry));
		break;
	}
	case DISK_ZONE_GET_PARAMS:
	default:
		/*
		 * In theory we should not get a GET_PARAMS bio, since it
		 * should be handled without queueing the command to the
		 * drive.
		 */
		panic("%s: Invalid zone command %d", __func__,
		    bp->bio_zone.zone_cmd);
		break;
	}

	if (bp->bio_zone.zone_cmd == DISK_ZONE_REPORT_ZONES)
		free(ccb->ataio.data_ptr, M_ATADA);
}


static void
adadone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct ada_softc *softc;
	struct ccb_ataio *ataio;
	struct cam_path *path;
	uint32_t priority;
	int state;

	softc = (struct ada_softc *)periph->softc;
	ataio = &done_ccb->ataio;
	path = done_ccb->ccb_h.path;
	priority = done_ccb->ccb_h.pinfo.priority;

	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("adadone\n"));

	state = ataio->ccb_h.ccb_state & ADA_CCB_TYPE_MASK;
	switch (state) {
	case ADA_CCB_BUFFER_IO:
	case ADA_CCB_TRIM:
	{
		struct bio *bp;
		int error;

		cam_periph_lock(periph);
		bp = (struct bio *)done_ccb->ccb_h.ccb_bp;
		if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			error = adaerror(done_ccb, 0, 0);
			if (error == ERESTART) {
				/* A retry was scheduled, so just return. */
				cam_periph_unlock(periph);
				return;
			}
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
				cam_release_devq(path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
			/*
			 * If we get an error on an NCQ DSM TRIM, fall back
			 * to a non-NCQ DSM TRIM forever. Please note that if
			 * CAN_NCQ_TRIM is set, CAN_TRIM is necessarily set too.
			 * However, for this one trim, we treat it as advisory
			 * and return success up the stack.
			 */
			if (state == ADA_CCB_TRIM &&
			    error != 0 &&
			    (softc->flags & ADA_FLAG_CAN_NCQ_TRIM) != 0) {
				softc->flags &= ~ADA_FLAG_CAN_NCQ_TRIM;
				error = 0;
				adasetdeletemethod(softc);
			}
		} else {
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
				panic("REQ_CMP with QFRZN");

			error = 0;
		}
		bp->bio_error = error;
		if (error != 0) {
			bp->bio_resid = bp->bio_bcount;
			bp->bio_flags |= BIO_ERROR;
		} else {
			if (bp->bio_cmd == BIO_ZONE)
				adazonedone(periph, done_ccb);
			else if (state == ADA_CCB_TRIM)
				bp->bio_resid = 0;
			else
				bp->bio_resid = ataio->resid;

			if ((bp->bio_resid > 0)
			 && (bp->bio_cmd != BIO_ZONE))
				bp->bio_flags |= BIO_ERROR;
		}
		softc->outstanding_cmds--;
		if (softc->outstanding_cmds == 0)
			softc->flags |= ADA_FLAG_WAS_OTAG;

		/*
		 * We need to call cam_iosched before we call biodone so that we
		 * don't measure any activity that happens in the completion
		 * routine, which in the case of sendfile can be quite
		 * extensive.  Release the periph refcount taken in adastart()
		 * for each CCB.
		 */
		cam_iosched_bio_complete(softc->cam_iosched, bp, done_ccb);
		xpt_release_ccb(done_ccb);
		KASSERT(softc->refcount >= 1, ("adadone softc %p refcount %d", softc, softc->refcount));
		softc->refcount--;
		if (state == ADA_CCB_TRIM) {
			TAILQ_HEAD(, bio) queue;
			struct bio *bp1;

			TAILQ_INIT(&queue);
			TAILQ_CONCAT(&queue, &softc->trim_req.bps, bio_queue);
			/*
			 * Normally, the xpt_release_ccb() above would make sure
			 * that when we have more work to do, that work would
			 * get kicked off. However, we specifically keep
			 * trim_running set to 0 before the call above to allow
			 * other I/O to progress when many BIO_DELETE requests
			 * are pushed down. We set trim_running to 0 and call
			 * daschedule again so that we don't stall if there are
			 * no other I/Os pending apart from BIO_DELETEs.
			 */
			cam_iosched_trim_done(softc->cam_iosched);
			adaschedule(periph);
			cam_periph_unlock(periph);
			while ((bp1 = TAILQ_FIRST(&queue)) != NULL) {
				TAILQ_REMOVE(&queue, bp1, bio_queue);
				bp1->bio_error = error;
				if (error != 0) {
					bp1->bio_flags |= BIO_ERROR;
					bp1->bio_resid = bp1->bio_bcount;
				} else
					bp1->bio_resid = 0;
				biodone(bp1);
			}
		} else {
			adaschedule(periph);
			cam_periph_unlock(periph);
			biodone(bp);
		}
		return;
	}
	case ADA_CCB_RAHEAD:
	{
		if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			if (adaerror(done_ccb, 0, 0) == ERESTART) {
				/* Drop freeze taken due to CAM_DEV_QFREEZE */
				cam_release_devq(path, 0, 0, 0, FALSE);
				return;
			} else if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				cam_release_devq(path,
				    /*relsim_flags*/0,
				    /*reduction*/0,
				    /*timeout*/0,
				    /*getcount_only*/0);
			}
		}

		/*
		 * Since our peripheral may be invalidated by an error
		 * above or an external event, we must release our CCB
		 * before releasing the reference on the peripheral.
		 * The peripheral will only go away once the last reference
		 * is removed, and we need it around for the CCB release
		 * operation.
		 */

		xpt_release_ccb(done_ccb);
		softc->state = ADA_STATE_WCACHE;
		xpt_schedule(periph, priority);
		/* Drop freeze taken due to CAM_DEV_QFREEZE */
		cam_release_devq(path, 0, 0, 0, FALSE);
		return;
	}
	case ADA_CCB_WCACHE:
	{
		if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			if (adaerror(done_ccb, 0, 0) == ERESTART) {
				/* Drop freeze taken due to CAM_DEV_QFREEZE */
				cam_release_devq(path, 0, 0, 0, FALSE);
				return;
			} else if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				cam_release_devq(path,
				    /*relsim_flags*/0,
				    /*reduction*/0,
				    /*timeout*/0,
				    /*getcount_only*/0);
			}
		}

		/* Drop freeze taken due to CAM_DEV_QFREEZE */
		cam_release_devq(path, 0, 0, 0, FALSE);

		if ((softc->flags & ADA_FLAG_CAN_LOG)
		 && (softc->zone_mode != ADA_ZONE_NONE)) {
			xpt_release_ccb(done_ccb);
			softc->state = ADA_STATE_LOGDIR;
			xpt_schedule(periph, priority);
		} else {
			adaprobedone(periph, done_ccb);
		}
		return;
	}
	case ADA_CCB_LOGDIR:
	{
		int error;

		if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
			error = 0;
			softc->valid_logdir_len = 0;
			bzero(&softc->ata_logdir, sizeof(softc->ata_logdir));
			softc->valid_logdir_len =
				ataio->dxfer_len - ataio->resid;
			if (softc->valid_logdir_len > 0)
				bcopy(ataio->data_ptr, &softc->ata_logdir,
				    min(softc->valid_logdir_len,
					sizeof(softc->ata_logdir)));
			/*
			 * Figure out whether the Identify Device log is
			 * supported.  The General Purpose log directory
			 * has a header, and lists the number of pages
			 * available for each GP log identified by the
			 * offset into the list.
			 */
			if ((softc->valid_logdir_len >=
			    ((ATA_IDENTIFY_DATA_LOG + 1) * sizeof(uint16_t)))
			 && (le16dec(softc->ata_logdir.header) ==
			     ATA_GP_LOG_DIR_VERSION)
			 && (le16dec(&softc->ata_logdir.num_pages[
			     (ATA_IDENTIFY_DATA_LOG *
			     sizeof(uint16_t)) - sizeof(uint16_t)]) > 0)){
				softc->flags |= ADA_FLAG_CAN_IDLOG;
			} else {
				softc->flags &= ~ADA_FLAG_CAN_IDLOG;
			}
		} else {
			error = adaerror(done_ccb, CAM_RETRY_SELTO,
					 SF_RETRY_UA|SF_NO_PRINT);
			if (error == ERESTART)
				return;
			else if (error != 0) {
				/*
				 * If we can't get the ATA log directory,
				 * then ATA logs are effectively not
				 * supported even if the bit is set in the
				 * identify data.
				 */
				softc->flags &= ~(ADA_FLAG_CAN_LOG |
						  ADA_FLAG_CAN_IDLOG);
				if ((done_ccb->ccb_h.status &
				     CAM_DEV_QFRZN) != 0) {
					/* Don't wedge this device's queue */
					cam_release_devq(done_ccb->ccb_h.path,
							 /*relsim_flags*/0,
							 /*reduction*/0,
							 /*timeout*/0,
							 /*getcount_only*/0);
				}
			}


		}

		free(ataio->data_ptr, M_ATADA);

		if ((error == 0)
		 && (softc->flags & ADA_FLAG_CAN_IDLOG)) {
			softc->state = ADA_STATE_IDDIR;
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
		} else
			adaprobedone(periph, done_ccb);

		return;
	}
	case ADA_CCB_IDDIR: {
		int error;

		if ((ataio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
			off_t entries_offset, max_entries;
			error = 0;

			softc->valid_iddir_len = 0;
			bzero(&softc->ata_iddir, sizeof(softc->ata_iddir));
			softc->flags &= ~(ADA_FLAG_CAN_SUPCAP |
					  ADA_FLAG_CAN_ZONE);
			softc->valid_iddir_len =
				ataio->dxfer_len - ataio->resid;
			if (softc->valid_iddir_len > 0)
				bcopy(ataio->data_ptr, &softc->ata_iddir,
				    min(softc->valid_iddir_len,
					sizeof(softc->ata_iddir)));

			entries_offset =
			    __offsetof(struct ata_identify_log_pages,entries);
			max_entries = softc->valid_iddir_len - entries_offset;
			if ((softc->valid_iddir_len > (entries_offset + 1))
			 && (le64dec(softc->ata_iddir.header) ==
			     ATA_IDLOG_REVISION)
			 && (softc->ata_iddir.entry_count > 0)) {
				int num_entries, i;

				num_entries = softc->ata_iddir.entry_count;
				num_entries = min(num_entries,
				   softc->valid_iddir_len - entries_offset);
				for (i = 0; i < num_entries &&
				     i < max_entries; i++) {
					if (softc->ata_iddir.entries[i] ==
					    ATA_IDL_SUP_CAP)
						softc->flags |=
						    ADA_FLAG_CAN_SUPCAP;
					else if (softc->ata_iddir.entries[i]==
						 ATA_IDL_ZDI)
						softc->flags |=
						    ADA_FLAG_CAN_ZONE;

					if ((softc->flags &
					     ADA_FLAG_CAN_SUPCAP)
					 && (softc->flags &
					     ADA_FLAG_CAN_ZONE))
						break;
				}
			}
		} else {
			error = adaerror(done_ccb, CAM_RETRY_SELTO,
					 SF_RETRY_UA|SF_NO_PRINT);
			if (error == ERESTART)
				return;
			else if (error != 0) {
				/*
				 * If we can't get the ATA Identify Data log
				 * directory, then it effectively isn't
				 * supported even if the ATA Log directory
				 * a non-zero number of pages present for
				 * this log.
				 */
				softc->flags &= ~ADA_FLAG_CAN_IDLOG;
				if ((done_ccb->ccb_h.status &
				     CAM_DEV_QFRZN) != 0) {
					/* Don't wedge this device's queue */
					cam_release_devq(done_ccb->ccb_h.path,
							 /*relsim_flags*/0,
							 /*reduction*/0,
							 /*timeout*/0,
							 /*getcount_only*/0);
				}
			}
		}

		free(ataio->data_ptr, M_ATADA);

		if ((error == 0)
		 && (softc->flags & ADA_FLAG_CAN_SUPCAP)) {
			softc->state = ADA_STATE_SUP_CAP;
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
		} else
			adaprobedone(periph, done_ccb);
		return;
	}
	case ADA_CCB_SUP_CAP: {
		int error;

		if ((ataio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
			uint32_t valid_len;
			size_t needed_size;
			struct ata_identify_log_sup_cap *sup_cap;
			error = 0;

			sup_cap = (struct ata_identify_log_sup_cap *)
			    ataio->data_ptr;
			valid_len = ataio->dxfer_len - ataio->resid;
			needed_size =
			    __offsetof(struct ata_identify_log_sup_cap,
			    sup_zac_cap) + 1 + sizeof(sup_cap->sup_zac_cap);
			if (valid_len >= needed_size) {
				uint64_t zoned, zac_cap;

				zoned = le64dec(sup_cap->zoned_cap);
				if (zoned & ATA_ZONED_VALID) {
					/*
					 * This should have already been
					 * set, because this is also in the
					 * ATA identify data.
					 */
					if ((zoned & ATA_ZONED_MASK) ==
					    ATA_SUPPORT_ZONE_HOST_AWARE)
						softc->zone_mode =
						    ADA_ZONE_HOST_AWARE;
					else if ((zoned & ATA_ZONED_MASK) ==
					    ATA_SUPPORT_ZONE_DEV_MANAGED)
						softc->zone_mode =
						    ADA_ZONE_DRIVE_MANAGED;
				}

				zac_cap = le64dec(sup_cap->sup_zac_cap);
				if (zac_cap & ATA_SUP_ZAC_CAP_VALID) {
					if (zac_cap & ATA_REPORT_ZONES_SUP)
						softc->zone_flags |=
						    ADA_ZONE_FLAG_RZ_SUP;
					if (zac_cap & ATA_ND_OPEN_ZONE_SUP)
						softc->zone_flags |=
						    ADA_ZONE_FLAG_OPEN_SUP;
					if (zac_cap & ATA_ND_CLOSE_ZONE_SUP)
						softc->zone_flags |=
						    ADA_ZONE_FLAG_CLOSE_SUP;
					if (zac_cap & ATA_ND_FINISH_ZONE_SUP)
						softc->zone_flags |=
						    ADA_ZONE_FLAG_FINISH_SUP;
					if (zac_cap & ATA_ND_RWP_SUP)
						softc->zone_flags |=
						    ADA_ZONE_FLAG_RWP_SUP;
				} else {
					/*
					 * This field was introduced in
					 * ACS-4, r08 on April 28th, 2015.
					 * If the drive firmware was written
					 * to an earlier spec, it won't have
					 * the field.  So, assume all
					 * commands are supported.
					 */
					softc->zone_flags |=
					    ADA_ZONE_FLAG_SUP_MASK;
				}
			}
		} else {
			error = adaerror(done_ccb, CAM_RETRY_SELTO,
					 SF_RETRY_UA|SF_NO_PRINT);
			if (error == ERESTART)
				return;
			else if (error != 0) {
				/*
				 * If we can't get the ATA Identify Data
				 * Supported Capabilities page, clear the
				 * flag...
				 */
				softc->flags &= ~ADA_FLAG_CAN_SUPCAP;
				/*
				 * And clear zone capabilities.
				 */
				softc->zone_flags &= ~ADA_ZONE_FLAG_SUP_MASK;
				if ((done_ccb->ccb_h.status &
				     CAM_DEV_QFRZN) != 0) {
					/* Don't wedge this device's queue */
					cam_release_devq(done_ccb->ccb_h.path,
							 /*relsim_flags*/0,
							 /*reduction*/0,
							 /*timeout*/0,
							 /*getcount_only*/0);
				}
			}
		}

		free(ataio->data_ptr, M_ATADA);

		if ((error == 0)
		 && (softc->flags & ADA_FLAG_CAN_ZONE)) {
			softc->state = ADA_STATE_ZONE;
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
		} else
			adaprobedone(periph, done_ccb);
		return;
	}
	case ADA_CCB_ZONE: {
		int error;

		if ((ataio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
			struct ata_zoned_info_log *zi_log;
			uint32_t valid_len;
			size_t needed_size;

			zi_log = (struct ata_zoned_info_log *)ataio->data_ptr;

			valid_len = ataio->dxfer_len - ataio->resid;
			needed_size = __offsetof(struct ata_zoned_info_log,
			    version_info) + 1 + sizeof(zi_log->version_info);
			if (valid_len >= needed_size) {
				uint64_t tmpvar;

				tmpvar = le64dec(zi_log->zoned_cap);
				if (tmpvar & ATA_ZDI_CAP_VALID) {
					if (tmpvar & ATA_ZDI_CAP_URSWRZ)
						softc->zone_flags |=
						    ADA_ZONE_FLAG_URSWRZ;
					else
						softc->zone_flags &=
						    ~ADA_ZONE_FLAG_URSWRZ;
				}
				tmpvar = le64dec(zi_log->optimal_seq_zones);
				if (tmpvar & ATA_ZDI_OPT_SEQ_VALID) {
					softc->zone_flags |=
					    ADA_ZONE_FLAG_OPT_SEQ_SET;
					softc->optimal_seq_zones = (tmpvar &
					    ATA_ZDI_OPT_SEQ_MASK);
				} else {
					softc->zone_flags &=
					    ~ADA_ZONE_FLAG_OPT_SEQ_SET;
					softc->optimal_seq_zones = 0;
				}

				tmpvar =le64dec(zi_log->optimal_nonseq_zones);
				if (tmpvar & ATA_ZDI_OPT_NS_VALID) {
					softc->zone_flags |=
					    ADA_ZONE_FLAG_OPT_NONSEQ_SET;
					softc->optimal_nonseq_zones =
					    (tmpvar & ATA_ZDI_OPT_NS_MASK);
				} else {
					softc->zone_flags &=
					    ~ADA_ZONE_FLAG_OPT_NONSEQ_SET;
					softc->optimal_nonseq_zones = 0;
				}

				tmpvar = le64dec(zi_log->max_seq_req_zones);
				if (tmpvar & ATA_ZDI_MAX_SEQ_VALID) {
					softc->zone_flags |=
					    ADA_ZONE_FLAG_MAX_SEQ_SET;
					softc->max_seq_zones =
					    (tmpvar & ATA_ZDI_MAX_SEQ_MASK);
				} else {
					softc->zone_flags &=
					    ~ADA_ZONE_FLAG_MAX_SEQ_SET;
					softc->max_seq_zones = 0;
				}
			}
		} else {
			error = adaerror(done_ccb, CAM_RETRY_SELTO,
					 SF_RETRY_UA|SF_NO_PRINT);
			if (error == ERESTART)
				return;
			else if (error != 0) {
				softc->flags &= ~ADA_FLAG_CAN_ZONE;
				softc->flags &= ~ADA_ZONE_FLAG_SET_MASK;

				if ((done_ccb->ccb_h.status &
				     CAM_DEV_QFRZN) != 0) {
					/* Don't wedge this device's queue */
					cam_release_devq(done_ccb->ccb_h.path,
							 /*relsim_flags*/0,
							 /*reduction*/0,
							 /*timeout*/0,
							 /*getcount_only*/0);
				}
			}
		}
		free(ataio->data_ptr, M_ATADA);

		adaprobedone(periph, done_ccb);
		return;
	}
	case ADA_CCB_DUMP:
		/* No-op.  We're polling */
		return;
	default:
		break;
	}
	xpt_release_ccb(done_ccb);
}

static int
adaerror(union ccb *ccb, u_int32_t cam_flags, u_int32_t sense_flags)
{
#ifdef CAM_IO_STATS
	struct ada_softc *softc;
	struct cam_periph *periph;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct ada_softc *)periph->softc;

	switch (ccb->ccb_h.status & CAM_STATUS_MASK) {
	case CAM_CMD_TIMEOUT:
		softc->timeouts++;
		break;
	case CAM_REQ_ABORTED:
	case CAM_REQ_CMP_ERR:
	case CAM_REQ_TERMIO:
	case CAM_UNREC_HBA_ERROR:
	case CAM_DATA_RUN_ERR:
	case CAM_ATA_STATUS_ERROR:
		softc->errors++;
		break;
	default:
		break;
	}
#endif

	return(cam_periph_error(ccb, cam_flags, sense_flags));
}

static void
adagetparams(struct cam_periph *periph, struct ccb_getdev *cgd)
{
	struct ada_softc *softc = (struct ada_softc *)periph->softc;
	struct disk_params *dp = &softc->params;
	u_int64_t lbasize48;
	u_int32_t lbasize;

	dp->secsize = ata_logical_sector_size(&cgd->ident_data);
	if ((cgd->ident_data.atavalid & ATA_FLAG_54_58) &&
		cgd->ident_data.current_heads && cgd->ident_data.current_sectors) {
		dp->heads = cgd->ident_data.current_heads;
		dp->secs_per_track = cgd->ident_data.current_sectors;
		dp->cylinders = cgd->ident_data.cylinders;
		dp->sectors = (u_int32_t)cgd->ident_data.current_size_1 |
			  ((u_int32_t)cgd->ident_data.current_size_2 << 16);
	} else {
		dp->heads = cgd->ident_data.heads;
		dp->secs_per_track = cgd->ident_data.sectors;
		dp->cylinders = cgd->ident_data.cylinders;
		dp->sectors = cgd->ident_data.cylinders *
			      (u_int32_t)(dp->heads * dp->secs_per_track);
	}
	lbasize = (u_int32_t)cgd->ident_data.lba_size_1 |
		  ((u_int32_t)cgd->ident_data.lba_size_2 << 16);

	/* use the 28bit LBA size if valid or bigger than the CHS mapping */
	if (cgd->ident_data.cylinders == 16383 || dp->sectors < lbasize)
		dp->sectors = lbasize;

	/* use the 48bit LBA size if valid */
	lbasize48 = ((u_int64_t)cgd->ident_data.lba_size48_1) |
		    ((u_int64_t)cgd->ident_data.lba_size48_2 << 16) |
		    ((u_int64_t)cgd->ident_data.lba_size48_3 << 32) |
		    ((u_int64_t)cgd->ident_data.lba_size48_4 << 48);
	if ((cgd->ident_data.support.command2 & ATA_SUPPORT_ADDRESS48) &&
	    lbasize48 > ATA_MAX_28BIT_LBA)
		dp->sectors = lbasize48;
}

static void
adasendorderedtag(void *arg)
{
	struct ada_softc *softc = arg;

	if (ada_send_ordered) {
		if (softc->outstanding_cmds > 0) {
			if ((softc->flags & ADA_FLAG_WAS_OTAG) == 0)
				softc->flags |= ADA_FLAG_NEED_OTAG;
			softc->flags &= ~ADA_FLAG_WAS_OTAG;
		}
	}
	/* Queue us up again */
	callout_reset(&softc->sendordered_c,
	    (ada_default_timeout * hz) / ADA_ORDEREDTAG_INTERVAL,
	    adasendorderedtag, softc);
}

/*
 * Step through all ADA peripheral drivers, and if the device is still open,
 * sync the disk cache to physical media.
 */
static void
adaflush(void)
{
	struct cam_periph *periph;
	struct ada_softc *softc;
	union ccb *ccb;
	int error;

	CAM_PERIPH_FOREACH(periph, &adadriver) {
		softc = (struct ada_softc *)periph->softc;
		if (SCHEDULER_STOPPED()) {
			/* If we paniced with the lock held, do not recurse. */
			if (!cam_periph_owned(periph) &&
			    (softc->flags & ADA_FLAG_OPEN)) {
				adadump(softc->disk, NULL, 0, 0, 0);
			}
			continue;
		}
		cam_periph_lock(periph);
		/*
		 * We only sync the cache if the drive is still open, and
		 * if the drive is capable of it..
		 */
		if (((softc->flags & ADA_FLAG_OPEN) == 0) ||
		    (softc->flags & ADA_FLAG_CAN_FLUSHCACHE) == 0) {
			cam_periph_unlock(periph);
			continue;
		}

		ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
		cam_fill_ataio(&ccb->ataio,
				    0,
				    NULL,
				    CAM_DIR_NONE,
				    0,
				    NULL,
				    0,
				    ada_default_timeout*1000);
		if (softc->flags & ADA_FLAG_CAN_48BIT)
			ata_48bit_cmd(&ccb->ataio, ATA_FLUSHCACHE48, 0, 0, 0);
		else
			ata_28bit_cmd(&ccb->ataio, ATA_FLUSHCACHE, 0, 0, 0);

		error = cam_periph_runccb(ccb, adaerror, /*cam_flags*/0,
		    /*sense_flags*/ SF_NO_RECOVERY | SF_NO_RETRY,
		    softc->disk->d_devstat);
		if (error != 0)
			xpt_print(periph->path, "Synchronize cache failed\n");
		xpt_release_ccb(ccb);
		cam_periph_unlock(periph);
	}
}

static void
adaspindown(uint8_t cmd, int flags)
{
	struct cam_periph *periph;
	struct ada_softc *softc;
	struct ccb_ataio local_ccb;
	int error;

	CAM_PERIPH_FOREACH(periph, &adadriver) {
		/* If we paniced with lock held - not recurse here. */
		if (cam_periph_owned(periph))
			continue;
		cam_periph_lock(periph);
		softc = (struct ada_softc *)periph->softc;
		/*
		 * We only spin-down the drive if it is capable of it..
		 */
		if ((softc->flags & ADA_FLAG_CAN_POWERMGT) == 0) {
			cam_periph_unlock(periph);
			continue;
		}

		if (bootverbose)
			xpt_print(periph->path, "spin-down\n");

		memset(&local_ccb, 0, sizeof(local_ccb));
		xpt_setup_ccb(&local_ccb.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
		local_ccb.ccb_h.ccb_state = ADA_CCB_DUMP;

		cam_fill_ataio(&local_ccb,
				    0,
				    NULL,
				    CAM_DIR_NONE | flags,
				    0,
				    NULL,
				    0,
				    ada_default_timeout*1000);
		ata_28bit_cmd(&local_ccb, cmd, 0, 0, 0);
		error = cam_periph_runccb((union ccb *)&local_ccb, adaerror,
		    /*cam_flags*/0, /*sense_flags*/ SF_NO_RECOVERY | SF_NO_RETRY,
		    softc->disk->d_devstat);
		if (error != 0)
			xpt_print(periph->path, "Spin-down disk failed\n");
		cam_periph_unlock(periph);
	}
}

static void
adashutdown(void *arg, int howto)
{
	int how;

	adaflush();

	/*
	 * STANDBY IMMEDIATE saves any volatile data to the drive. It also spins
	 * down hard drives. IDLE IMMEDIATE also saves the volatile data without
	 * a spindown. We send the former when we expect to lose power soon. For
	 * a warm boot, we send the latter to avoid a thundering herd of spinups
	 * just after the kernel loads while probing. We have to do something to
	 * flush the data because the BIOS in many systems resets the HBA
	 * causing a COMINIT/COMRESET negotiation, which some drives interpret
	 * as license to toss the volatile data, and others count as unclean
	 * shutdown when in the Active PM state in SMART attributes.
	 *
	 * adaspindown will ensure that we don't send this to a drive that
	 * doesn't support it.
	 */
	if (ada_spindown_shutdown != 0) {
		how = (howto & (RB_HALT | RB_POWEROFF | RB_POWERCYCLE)) ?
		    ATA_STANDBY_IMMEDIATE : ATA_IDLE_IMMEDIATE;
		adaspindown(how, 0);
	}
}

static void
adasuspend(void *arg)
{

	adaflush();
	/*
	 * SLEEP also fushes any volatile data, like STANDBY IMEDIATE,
	 * so we don't need to send it as well.
	 */
	if (ada_spindown_suspend != 0)
		adaspindown(ATA_SLEEP, CAM_DEV_QFREEZE);
}

static void
adaresume(void *arg)
{
	struct cam_periph *periph;
	struct ada_softc *softc;

	if (ada_spindown_suspend == 0)
		return;

	CAM_PERIPH_FOREACH(periph, &adadriver) {
		cam_periph_lock(periph);
		softc = (struct ada_softc *)periph->softc;
		/*
		 * We only spin-down the drive if it is capable of it..
		 */
		if ((softc->flags & ADA_FLAG_CAN_POWERMGT) == 0) {
			cam_periph_unlock(periph);
			continue;
		}

		if (bootverbose)
			xpt_print(periph->path, "resume\n");

		/*
		 * Drop freeze taken due to CAM_DEV_QFREEZE flag set on
		 * sleep request.
		 */
		cam_release_devq(periph->path,
			 /*relsim_flags*/0,
			 /*openings*/0,
			 /*timeout*/0,
			 /*getcount_only*/0);

		cam_periph_unlock(periph);
	}
}

#endif /* _KERNEL */
