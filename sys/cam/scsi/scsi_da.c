/*-
 * Implementation of SCSI Direct Access Peripheral driver for CAM.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#ifdef _KERNEL
#include "opt_da.h"
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
#include <sys/cons.h>
#include <sys/endian.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <geom/geom.h>
#include <geom/geom_disk.h>
#include <machine/atomic.h>
#endif /* _KERNEL */

#ifndef _KERNEL
#include <stdio.h>
#include <string.h>
#endif /* _KERNEL */

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_iosched.h>

#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_da.h>

#ifdef _KERNEL
/*
 * Note that there are probe ordering dependencies here.  The order isn't
 * controlled by this enumeration, but by explicit state transitions in
 * dastart() and dadone().  Here are some of the dependencies:
 *
 * 1. RC should come first, before RC16, unless there is evidence that RC16
 *    is supported.
 * 2. BDC needs to come before any of the ATA probes, or the ZONE probe.
 * 3. The ATA probes should go in this order:
 *    ATA -> LOGDIR -> IDDIR -> SUP -> ATA_ZONE
 */
typedef enum {
	DA_STATE_PROBE_WP,
	DA_STATE_PROBE_RC,
	DA_STATE_PROBE_RC16,
	DA_STATE_PROBE_LBP,
	DA_STATE_PROBE_BLK_LIMITS,
	DA_STATE_PROBE_BDC,
	DA_STATE_PROBE_ATA,
	DA_STATE_PROBE_ATA_LOGDIR,
	DA_STATE_PROBE_ATA_IDDIR,
	DA_STATE_PROBE_ATA_SUP,
	DA_STATE_PROBE_ATA_ZONE,
	DA_STATE_PROBE_ZONE,
	DA_STATE_NORMAL
} da_state;

typedef enum {
	DA_FLAG_PACK_INVALID	= 0x000001,
	DA_FLAG_NEW_PACK	= 0x000002,
	DA_FLAG_PACK_LOCKED	= 0x000004,
	DA_FLAG_PACK_REMOVABLE	= 0x000008,
	DA_FLAG_NEED_OTAG	= 0x000020,
	DA_FLAG_WAS_OTAG	= 0x000040,
	DA_FLAG_RETRY_UA	= 0x000080,
	DA_FLAG_OPEN		= 0x000100,
	DA_FLAG_SCTX_INIT	= 0x000200,
	DA_FLAG_CAN_RC16	= 0x000400,
	DA_FLAG_PROBED		= 0x000800,
	DA_FLAG_DIRTY		= 0x001000,
	DA_FLAG_ANNOUNCED	= 0x002000,
	DA_FLAG_CAN_ATA_DMA	= 0x004000,
	DA_FLAG_CAN_ATA_LOG	= 0x008000,
	DA_FLAG_CAN_ATA_IDLOG	= 0x010000,
	DA_FLAG_CAN_ATA_SUPCAP	= 0x020000,
	DA_FLAG_CAN_ATA_ZONE	= 0x040000,
	DA_FLAG_TUR_PENDING	= 0x080000
} da_flags;

typedef enum {
	DA_Q_NONE		= 0x00,
	DA_Q_NO_SYNC_CACHE	= 0x01,
	DA_Q_NO_6_BYTE		= 0x02,
	DA_Q_NO_PREVENT		= 0x04,
	DA_Q_4K			= 0x08,
	DA_Q_NO_RC16		= 0x10,
	DA_Q_NO_UNMAP		= 0x20,
	DA_Q_RETRY_BUSY		= 0x40,
	DA_Q_SMR_DM		= 0x80,
	DA_Q_STRICT_UNMAP	= 0x100,
	DA_Q_128KB		= 0x200
} da_quirks;

#define DA_Q_BIT_STRING		\
	"\020"			\
	"\001NO_SYNC_CACHE"	\
	"\002NO_6_BYTE"		\
	"\003NO_PREVENT"	\
	"\0044K"		\
	"\005NO_RC16"		\
	"\006NO_UNMAP"		\
	"\007RETRY_BUSY"	\
	"\010SMR_DM"		\
	"\011STRICT_UNMAP"	\
	"\012128KB"

typedef enum {
	DA_CCB_PROBE_RC		= 0x01,
	DA_CCB_PROBE_RC16	= 0x02,
	DA_CCB_PROBE_LBP	= 0x03,
	DA_CCB_PROBE_BLK_LIMITS	= 0x04,
	DA_CCB_PROBE_BDC	= 0x05,
	DA_CCB_PROBE_ATA	= 0x06,
	DA_CCB_BUFFER_IO	= 0x07,
	DA_CCB_DUMP		= 0x0A,
	DA_CCB_DELETE		= 0x0B,
	DA_CCB_TUR		= 0x0C,
	DA_CCB_PROBE_ZONE	= 0x0D,
	DA_CCB_PROBE_ATA_LOGDIR	= 0x0E,
	DA_CCB_PROBE_ATA_IDDIR	= 0x0F,
	DA_CCB_PROBE_ATA_SUP	= 0x10,
	DA_CCB_PROBE_ATA_ZONE	= 0x11,
	DA_CCB_PROBE_WP		= 0x12,
	DA_CCB_TYPE_MASK	= 0x1F,
	DA_CCB_RETRY_UA		= 0x20
} da_ccb_state;

/*
 * Order here is important for method choice
 *
 * We prefer ATA_TRIM as tests run against a Sandforce 2281 SSD attached to
 * LSI 2008 (mps) controller (FW: v12, Drv: v14) resulted 20% quicker deletes
 * using ATA_TRIM than the corresponding UNMAP results for a real world mysql
 * import taking 5mins.
 *
 */
typedef enum {
	DA_DELETE_NONE,
	DA_DELETE_DISABLE,
	DA_DELETE_ATA_TRIM,
	DA_DELETE_UNMAP,
	DA_DELETE_WS16,
	DA_DELETE_WS10,
	DA_DELETE_ZERO,
	DA_DELETE_MIN = DA_DELETE_ATA_TRIM,
	DA_DELETE_MAX = DA_DELETE_ZERO
} da_delete_methods;

/*
 * For SCSI, host managed drives show up as a separate device type.  For
 * ATA, host managed drives also have a different device signature.
 * XXX KDM figure out the ATA host managed signature.
 */
typedef enum {
	DA_ZONE_NONE		= 0x00,
	DA_ZONE_DRIVE_MANAGED	= 0x01,
	DA_ZONE_HOST_AWARE	= 0x02,
	DA_ZONE_HOST_MANAGED	= 0x03
} da_zone_mode;

/*
 * We distinguish between these interface cases in addition to the drive type:
 * o ATA drive behind a SCSI translation layer that knows about ZBC/ZAC
 * o ATA drive behind a SCSI translation layer that does not know about
 *   ZBC/ZAC, and so needs to be managed via ATA passthrough.  In this
 *   case, we would need to share the ATA code with the ada(4) driver.
 * o SCSI drive.
 */
typedef enum {
	DA_ZONE_IF_SCSI,
	DA_ZONE_IF_ATA_PASS,
	DA_ZONE_IF_ATA_SAT,
} da_zone_interface;

typedef enum {
	DA_ZONE_FLAG_RZ_SUP		= 0x0001,
	DA_ZONE_FLAG_OPEN_SUP		= 0x0002,
	DA_ZONE_FLAG_CLOSE_SUP		= 0x0004,
	DA_ZONE_FLAG_FINISH_SUP		= 0x0008,
	DA_ZONE_FLAG_RWP_SUP		= 0x0010,
	DA_ZONE_FLAG_SUP_MASK		= (DA_ZONE_FLAG_RZ_SUP |
					   DA_ZONE_FLAG_OPEN_SUP |
					   DA_ZONE_FLAG_CLOSE_SUP |
					   DA_ZONE_FLAG_FINISH_SUP |
					   DA_ZONE_FLAG_RWP_SUP),
	DA_ZONE_FLAG_URSWRZ		= 0x0020,
	DA_ZONE_FLAG_OPT_SEQ_SET	= 0x0040,
	DA_ZONE_FLAG_OPT_NONSEQ_SET	= 0x0080,
	DA_ZONE_FLAG_MAX_SEQ_SET	= 0x0100,
	DA_ZONE_FLAG_SET_MASK		= (DA_ZONE_FLAG_OPT_SEQ_SET |
					   DA_ZONE_FLAG_OPT_NONSEQ_SET |
					   DA_ZONE_FLAG_MAX_SEQ_SET)
} da_zone_flags;

static struct da_zone_desc {
	da_zone_flags value;
	const char *desc;
} da_zone_desc_table[] = {
	{DA_ZONE_FLAG_RZ_SUP, "Report Zones" },
	{DA_ZONE_FLAG_OPEN_SUP, "Open" },
	{DA_ZONE_FLAG_CLOSE_SUP, "Close" },
	{DA_ZONE_FLAG_FINISH_SUP, "Finish" },
	{DA_ZONE_FLAG_RWP_SUP, "Reset Write Pointer" },
};

typedef void da_delete_func_t (struct cam_periph *periph, union ccb *ccb,
			      struct bio *bp);
static da_delete_func_t da_delete_trim;
static da_delete_func_t da_delete_unmap;
static da_delete_func_t da_delete_ws;

static const void * da_delete_functions[] = {
	NULL,
	NULL,
	da_delete_trim,
	da_delete_unmap,
	da_delete_ws,
	da_delete_ws,
	da_delete_ws
};

static const char *da_delete_method_names[] =
    { "NONE", "DISABLE", "ATA_TRIM", "UNMAP", "WS16", "WS10", "ZERO" };
static const char *da_delete_method_desc[] =
    { "NONE", "DISABLED", "ATA TRIM", "UNMAP", "WRITE SAME(16) with UNMAP",
      "WRITE SAME(10) with UNMAP", "ZERO" };

/* Offsets into our private area for storing information */
#define ccb_state	ppriv_field0
#define ccb_bp		ppriv_ptr1

struct disk_params {
	u_int8_t  heads;
	u_int32_t cylinders;
	u_int8_t  secs_per_track;
	u_int32_t secsize;	/* Number of bytes/sector */
	u_int64_t sectors;	/* total number sectors */
	u_int     stripesize;
	u_int     stripeoffset;
};

#define UNMAP_RANGE_MAX		0xffffffff
#define UNMAP_HEAD_SIZE		8
#define UNMAP_RANGE_SIZE	16
#define UNMAP_MAX_RANGES	2048 /* Protocol Max is 4095 */
#define UNMAP_BUF_SIZE		((UNMAP_MAX_RANGES * UNMAP_RANGE_SIZE) + \
				UNMAP_HEAD_SIZE)

#define WS10_MAX_BLKS		0xffff
#define WS16_MAX_BLKS		0xffffffff
#define ATA_TRIM_MAX_RANGES	((UNMAP_BUF_SIZE / \
	(ATA_DSM_RANGE_SIZE * ATA_DSM_BLK_SIZE)) * ATA_DSM_BLK_SIZE)

#define DA_WORK_TUR		(1 << 16)

typedef enum {
	DA_REF_OPEN = 1,
	DA_REF_OPEN_HOLD,
	DA_REF_CLOSE_HOLD,
	DA_REF_PROBE_HOLD,
	DA_REF_TUR,
	DA_REF_GEOM,
	DA_REF_SYSCTL,
	DA_REF_REPROBE,
	DA_REF_MAX		/* KEEP LAST */
} da_ref_token;

struct da_softc {
	struct   cam_iosched_softc *cam_iosched;
	struct	 bio_queue_head delete_run_queue;
	LIST_HEAD(, ccb_hdr) pending_ccbs;
	int	 refcount;		/* Active xpt_action() calls */
	da_state state;
	da_flags flags;
	da_quirks quirks;
	int	 minimum_cmd_size;
	int	 error_inject;
	int	 trim_max_ranges;
	int	 delete_available;	/* Delete methods possibly available */
	da_zone_mode			zone_mode;
	da_zone_interface		zone_interface;
	da_zone_flags			zone_flags;
	struct ata_gp_log_dir		ata_logdir;
	int				valid_logdir_len;
	struct ata_identify_log_pages	ata_iddir;
	int				valid_iddir_len;
	uint64_t			optimal_seq_zones;
	uint64_t			optimal_nonseq_zones;
	uint64_t			max_seq_zones;
	u_int			maxio;
	uint32_t		unmap_max_ranges;
	uint32_t		unmap_max_lba; /* Max LBAs in UNMAP req */
	uint32_t		unmap_gran;
	uint32_t		unmap_gran_align;
	uint64_t		ws_max_blks;
	uint64_t		trim_count;
	uint64_t		trim_ranges;
	uint64_t		trim_lbas;
	da_delete_methods	delete_method_pref;
	da_delete_methods	delete_method;
	da_delete_func_t	*delete_func;
	int			unmappedio;
	int			rotating;
	struct	 disk_params params;
	struct	 disk *disk;
	union	 ccb saved_ccb;
	struct task		sysctl_task;
	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
	struct callout		sendordered_c;
	uint64_t wwpn;
	uint8_t	 unmap_buf[UNMAP_BUF_SIZE];
	struct scsi_read_capacity_data_long rcaplong;
	struct callout		mediapoll_c;
	int			ref_flags[DA_REF_MAX];
#ifdef CAM_IO_STATS
	struct sysctl_ctx_list	sysctl_stats_ctx;
	struct sysctl_oid	*sysctl_stats_tree;
	u_int	errors;
	u_int	timeouts;
	u_int	invalidations;
#endif
#define DA_ANNOUNCETMP_SZ 160
	char			announce_temp[DA_ANNOUNCETMP_SZ];
#define DA_ANNOUNCE_SZ 400
	char			announcebuf[DA_ANNOUNCE_SZ];
};

#define dadeleteflag(softc, delete_method, enable)			\
	if (enable) {							\
		softc->delete_available |= (1 << delete_method);	\
	} else {							\
		softc->delete_available &= ~(1 << delete_method);	\
	}

struct da_quirk_entry {
	struct scsi_inquiry_pattern inq_pat;
	da_quirks quirks;
};

static const char quantum[] = "QUANTUM";
static const char microp[] = "MICROP";

static struct da_quirk_entry da_quirk_table[] =
{
	/* SPI, FC devices */
	{
		/*
		 * Fujitsu M2513A MO drives.
		 * Tested devices: M2513A2 firmware versions 1200 & 1300.
		 * (dip switch selects whether T_DIRECT or T_OPTICAL device)
		 * Reported by: W.Scholten <whs@xs4all.nl>
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "FUJITSU", "M2513A", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/* See above. */
		{T_OPTICAL, SIP_MEDIA_REMOVABLE, "FUJITSU", "M2513A", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * This particular Fujitsu drive doesn't like the
		 * synchronize cache command.
		 * Reported by: Tom Jackson <toj@gorilla.net>
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "FUJITSU", "M2954*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * This drive doesn't like the synchronize cache command
		 * either.  Reported by: Matthew Jacob <mjacob@feral.com>
		 * in NetBSD PR kern/6027, August 24, 1998.
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, microp, "2217*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * This drive doesn't like the synchronize cache command
		 * either.  Reported by: Hellmuth Michaelis (hm@kts.org)
		 * (PR 8882).
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, microp, "2112*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Doesn't like the synchronize cache command.
		 * Reported by: Blaz Zupan <blaz@gold.amis.net>
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "NEC", "D3847*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Doesn't like the synchronize cache command.
		 * Reported by: Blaz Zupan <blaz@gold.amis.net>
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, quantum, "MAVERICK 540S", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Doesn't like the synchronize cache command.
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, quantum, "LPS525S", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Doesn't like the synchronize cache command.
		 * Reported by: walter@pelissero.de
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, quantum, "LPS540S", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Doesn't work correctly with 6 byte reads/writes.
		 * Returns illegal request, and points to byte 9 of the
		 * 6-byte CDB.
		 * Reported by:  Adam McDougall <bsdx@spawnet.com>
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, quantum, "VIKING 4*", "*"},
		/*quirks*/ DA_Q_NO_6_BYTE
	},
	{
		/* See above. */
		{T_DIRECT, SIP_MEDIA_FIXED, quantum, "VIKING 2*", "*"},
		/*quirks*/ DA_Q_NO_6_BYTE
	},
	{
		/*
		 * Doesn't like the synchronize cache command.
		 * Reported by: walter@pelissero.de
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "CONNER", "CP3500*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * The CISS RAID controllers do not support SYNC_CACHE
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "COMPAQ", "RAID*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * The STEC SSDs sometimes hang on UNMAP.
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "STEC", "*", "*"},
		/*quirks*/ DA_Q_NO_UNMAP
	},
	{
		/*
		 * VMware returns BUSY status when storage has transient
		 * connectivity problems, so better wait.
		 * Also VMware returns odd errors on misaligned UNMAPs.
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "VMware*", "*", "*"},
		/*quirks*/ DA_Q_RETRY_BUSY | DA_Q_STRICT_UNMAP
	},
	/* USB mass storage devices supported by umass(4) */
	{
		/*
		 * EXATELECOM (Sigmatel) i-Bead 100/105 USB Flash MP3 Player
		 * PR: kern/51675
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "EXATEL", "i-BEAD10*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Power Quotient Int. (PQI) USB flash key
		 * PR: kern/53067
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Generic*", "USB Flash Disk*",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Creative Nomad MUVO mp3 player (USB)
		 * PR: kern/53094
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "CREATIVE", "NOMAD_MUVO", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE|DA_Q_NO_PREVENT
	},
	{
		/*
		 * Jungsoft NEXDISK USB flash key
		 * PR: kern/54737
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "JUNGSOFT", "NEXDISK*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * FreeDik USB Mini Data Drive
		 * PR: kern/54786
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "FreeDik*", "Mini Data Drive",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Sigmatel USB Flash MP3 Player
		 * PR: kern/57046
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "SigmaTel", "MSCN", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE|DA_Q_NO_PREVENT
	},
	{
		/*
		 * Neuros USB Digital Audio Computer
		 * PR: kern/63645
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "NEUROS", "dig. audio comp.",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * SEAGRAND NP-900 MP3 Player
		 * PR: kern/64563
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "SEAGRAND", "NP-900*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE|DA_Q_NO_PREVENT
	},
	{
		/*
		 * iRiver iFP MP3 player (with UMS Firmware)
		 * PR: kern/54881, i386/63941, kern/66124
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "iRiver", "iFP*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Frontier Labs NEX IA+ Digital Audio Player, rev 1.10/0.01
		 * PR: kern/70158
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "FL" , "Nex*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * ZICPlay USB MP3 Player with FM
		 * PR: kern/75057
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "ACTIONS*" , "USB DISK*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * TEAC USB floppy mechanisms
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "TEAC" , "FD-05*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Kingston DataTraveler II+ USB Pen-Drive.
		 * Reported by: Pawel Jakub Dawidek <pjd@FreeBSD.org>
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Kingston" , "DataTraveler II+",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * USB DISK Pro PMAP
		 * Reported by: jhs
		 * PR: usb/96381
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, " ", "USB DISK Pro", "PMAP"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Motorola E398 Mobile Phone (TransFlash memory card).
		 * Reported by: Wojciech A. Koszek <dunstan@FreeBSD.czest.pl>
		 * PR: usb/89889
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Motorola" , "Motorola Phone",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Qware BeatZkey! Pro
		 * PR: usb/79164
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "GENERIC", "USB DISK DEVICE",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Time DPA20B 1GB MP3 Player
		 * PR: usb/81846
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "USB2.0*", "(FS) FLASH DISK*",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Samsung USB key 128Mb
		 * PR: usb/90081
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "USB-DISK", "FreeDik-FlashUsb",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Kingston DataTraveler 2.0 USB Flash memory.
		 * PR: usb/89196
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Kingston", "DataTraveler 2.0",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Creative MUVO Slim mp3 player (USB)
		 * PR: usb/86131
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "CREATIVE", "MuVo Slim",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE|DA_Q_NO_PREVENT
		},
	{
		/*
		 * United MP5512 Portable MP3 Player (2-in-1 USB DISK/MP3)
		 * PR: usb/80487
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Generic*", "MUSIC DISK",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * SanDisk Micro Cruzer 128MB
		 * PR: usb/75970
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "SanDisk" , "Micro Cruzer",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * TOSHIBA TransMemory USB sticks
		 * PR: kern/94660
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "TOSHIBA", "TransMemory",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * PNY USB 3.0 Flash Drives
		*/
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "PNY", "USB 3.0 FD*",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE | DA_Q_NO_RC16
	},
	{
		/*
		 * PNY USB Flash keys
		 * PR: usb/75578, usb/72344, usb/65436
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "*" , "USB DISK*",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Genesys GL3224
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Generic*", "STORAGE DEVICE*",
		"120?"}, /*quirks*/ DA_Q_NO_SYNC_CACHE | DA_Q_4K | DA_Q_NO_RC16
	},
	{
		/*
		 * Genesys 6-in-1 Card Reader
		 * PR: usb/94647
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Generic*", "STORAGE DEVICE*",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Rekam Digital CAMERA
		 * PR: usb/98713
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "CAMERA*", "4MP-9J6*",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * iRiver H10 MP3 player
		 * PR: usb/102547
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "iriver", "H10*",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * iRiver U10 MP3 player
		 * PR: usb/92306
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "iriver", "U10*",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * X-Micro Flash Disk
		 * PR: usb/96901
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "X-Micro", "Flash Disk",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * EasyMP3 EM732X USB 2.0 Flash MP3 Player
		 * PR: usb/96546
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "EM732X", "MP3 Player*",
		"1.00"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Denver MP3 player
		 * PR: usb/107101
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "DENVER", "MP3 PLAYER",
		 "*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Philips USB Key Audio KEY013
		 * PR: usb/68412
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "PHILIPS", "Key*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE | DA_Q_NO_PREVENT
	},
	{
		/*
		 * JNC MP3 Player
		 * PR: usb/94439
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "JNC*" , "MP3 Player*",
		 "*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * SAMSUNG MP0402H
		 * PR: usb/108427
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "SAMSUNG", "MP0402H", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * I/O Magic USB flash - Giga Bank
		 * PR: usb/108810
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "GS-Magic", "stor*", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * JoyFly 128mb USB Flash Drive
		 * PR: 96133
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "USB 2.0", "Flash Disk*",
		 "*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * ChipsBnk usb stick
		 * PR: 103702
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "ChipsBnk", "USB*",
		 "*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Storcase (Kingston) InfoStation IFS FC2/SATA-R 201A
		 * PR: 129858
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "IFS", "FC2/SATA-R*",
		 "*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Samsung YP-U3 mp3-player
		 * PR: 125398
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Samsung", "YP-U3",
		 "*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Netac", "OnlyDisk*",
		 "2000"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Sony Cyber-Shot DSC cameras
		 * PR: usb/137035
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Sony", "Sony DSC", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE | DA_Q_NO_PREVENT
	},
	{
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "Kingston", "DataTraveler G3",
		 "1.00"}, /*quirks*/ DA_Q_NO_PREVENT
	},
	{
		/* At least several Transcent USB sticks lie on RC16. */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "JetFlash", "Transcend*",
		 "*"}, /*quirks*/ DA_Q_NO_RC16
	},
	{
		/*
		 * I-O Data USB Flash Disk
		 * PR: usb/211716
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "I-O DATA", "USB Flash Disk*",
		 "*"}, /*quirks*/ DA_Q_NO_RC16
	},
	{
		/*
		 * SLC CHIPFANCIER USB drives
		 * PR: usb/234503 (RC10 right, RC16 wrong)
		 * 16GB, 32GB and 128GB confirmed to have same issue
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "*SLC", "CHIPFANCIER",
		 "*"}, /*quirks*/ DA_Q_NO_RC16
       },
	/* ATA/SATA devices over SAS/USB/... */
	{
		/* Sandisk X400 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "SanDisk SD8SB8U1*", "*" },
		/*quirks*/DA_Q_128KB
	},
	{
		/* Hitachi Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "Hitachi", "H??????????E3*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Micron Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "Micron 5100 MTFDDAK*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Samsung Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "SAMSUNG HD155UI*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Samsung Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "SAMSUNG", "HD155UI*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Samsung Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "SAMSUNG HD204UI*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Samsung Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "SAMSUNG", "HD204UI*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Barracuda Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST????DL*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Barracuda Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST????DL", "*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Barracuda Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST???DM*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Barracuda Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST???DM*", "*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Barracuda Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST????DM*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Barracuda Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST????DM", "*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST9500423AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST950042", "3AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST9500424AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST950042", "4AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST9640423AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST964042", "3AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST9640424AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST964042", "4AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST9750420AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST975042", "0AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST9750422AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST975042", "2AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST9750423AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST975042", "3AS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Thin Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST???LT*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* Seagate Momentus Thin Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ST???LT*", "*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "WDC WD????RS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "WDC WD??", "??RS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "WDC WD????RX*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "WDC WD??", "??RX*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "WDC WD??????RS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "WDC WD??", "????RS*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "WDC WD??????RX*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Caviar Green Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "WDC WD??", "????RX*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Scorpio Black Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "WDC WD???PKT*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Scorpio Black Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "WDC WD??", "?PKT*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Scorpio Black Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "WDC WD?????PKT*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Scorpio Black Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "WDC WD??", "???PKT*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Scorpio Blue Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "WDC WD???PVT*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Scorpio Blue Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "WDC WD??", "?PVT*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Scorpio Blue Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "WDC WD?????PVT*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/* WDC Scorpio Blue Advanced Format (4k) drives */
		{ T_DIRECT, SIP_MEDIA_FIXED, "WDC WD??", "???PVT*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Olympus digital cameras (C-3040ZOOM, C-2040ZOOM, C-1)
		 * PR: usb/97472
		 */
		{ T_DIRECT, SIP_MEDIA_REMOVABLE, "OLYMPUS", "C*", "*"},
		/*quirks*/ DA_Q_NO_6_BYTE | DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Olympus digital cameras (D-370)
		 * PR: usb/97472
		 */
		{ T_DIRECT, SIP_MEDIA_REMOVABLE, "OLYMPUS", "D*", "*"},
		/*quirks*/ DA_Q_NO_6_BYTE
	},
	{
		/*
		 * Olympus digital cameras (E-100RS, E-10).
		 * PR: usb/97472
		 */
		{ T_DIRECT, SIP_MEDIA_REMOVABLE, "OLYMPUS", "E*", "*"},
		/*quirks*/ DA_Q_NO_6_BYTE | DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Olympus FE-210 camera
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "OLYMPUS", "FE210*",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		* Pentax Digital Camera
		* PR: usb/93389
		*/
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "PENTAX", "DIGITAL CAMERA",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * LG UP3S MP3 player
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "LG", "UP3S",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * Laser MP3-2GA13 MP3 player
		 */
		{T_DIRECT, SIP_MEDIA_REMOVABLE, "USB 2.0", "(HS) Flash Disk",
		"*"}, /*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	{
		/*
		 * LaCie external 250GB Hard drive des by Porsche
		 * Submitted by: Ben Stuyts <ben@altesco.nl>
		 * PR: 121474
		 */
		{T_DIRECT, SIP_MEDIA_FIXED, "SAMSUNG", "HM250JI", "*"},
		/*quirks*/ DA_Q_NO_SYNC_CACHE
	},
	/* SATA SSDs */
	{
		/*
		 * Corsair Force 2 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "Corsair CSSD-F*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Corsair Force 3 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "Corsair Force 3*", "*" },
		/*quirks*/DA_Q_4K
	},
        {
		/*
		 * Corsair Neutron GTX SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "Corsair Neutron GTX*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Corsair Force GT & GS SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "Corsair Force G*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Crucial M4 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "M4-CT???M4SSD2*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Crucial RealSSD C300 SSDs
		 * 4k optimised
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "C300-CTFDDAC???MAG*",
		"*" }, /*quirks*/DA_Q_4K
	},
	{
		/*
		 * Intel 320 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "INTEL SSDSA2CW*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Intel 330 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "INTEL SSDSC2CT*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Intel 510 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "INTEL SSDSC2MH*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Intel 520 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "INTEL SSDSC2BW*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Intel S3610 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "INTEL SSDSC2BX*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Intel X25-M Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "INTEL SSDSA2M*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Kingston E100 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "KINGSTON SE100S3*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Kingston HyperX 3k SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "KINGSTON SH103S3*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Marvell SSDs (entry taken from OpenSolaris)
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "MARVELL SD88SA02*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * OCZ Agility 2 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "*", "OCZ-AGILITY2*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * OCZ Agility 3 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "OCZ-AGILITY3*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * OCZ Deneva R Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "DENRSTE251M45*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * OCZ Vertex 2 SSDs (inc pro series)
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "OCZ?VERTEX2*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * OCZ Vertex 3 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "OCZ-VERTEX3*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * OCZ Vertex 4 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "OCZ-VERTEX4*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Samsung 750 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "Samsung SSD 750*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Samsung 830 Series SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "SAMSUNG SSD 830 Series*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Samsung 840 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "Samsung SSD 840*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Samsung 845 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "Samsung SSD 845*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Samsung 850 SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "Samsung SSD 850*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Samsung 843T Series SSDs (MZ7WD*)
		 * Samsung PM851 Series SSDs (MZ7TE*)
		 * Samsung PM853T Series SSDs (MZ7GE*)
		 * Samsung SM863 Series SSDs (MZ7KM*)
		 * 4k optimised
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "SAMSUNG MZ7*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Same as for SAMSUNG MZ7* but enable the quirks for SSD
		 * starting with MZ7* too
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "MZ7*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * SuperTalent TeraDrive CT SSDs
		 * 4k optimised & trim only works in 4k requests + 4k aligned
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "FTM??CT25H*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * XceedIOPS SATA SSDs
		 * 4k optimised
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "SG9XCS2D*", "*" },
		/*quirks*/DA_Q_4K
	},
	{
		/*
		 * Hama Innostor USB-Stick
		 */
		{ T_DIRECT, SIP_MEDIA_REMOVABLE, "Innostor", "Innostor*", "*" },
		/*quirks*/DA_Q_NO_RC16
	},
	{
		/*
		 * Seagate Lamarr 8TB Shingled Magnetic Recording (SMR)
		 * Drive Managed SATA hard drive.  This drive doesn't report
		 * in firmware that it is a drive managed SMR drive.
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "ATA", "ST8000AS000[23]*", "*" },
		/*quirks*/DA_Q_SMR_DM
	},
	{
		/*
		 * MX-ES USB Drive by Mach Xtreme
		 */
		{ T_DIRECT, SIP_MEDIA_REMOVABLE, "MX", "MXUB3*", "*"},
		/*quirks*/DA_Q_NO_RC16
	},
};

static	disk_strategy_t	dastrategy;
static	dumper_t	dadump;
static	periph_init_t	dainit;
static	void		daasync(void *callback_arg, u_int32_t code,
				struct cam_path *path, void *arg);
static	void		dasysctlinit(void *context, int pending);
static	int		dasysctlsofttimeout(SYSCTL_HANDLER_ARGS);
static	int		dacmdsizesysctl(SYSCTL_HANDLER_ARGS);
static	int		dadeletemethodsysctl(SYSCTL_HANDLER_ARGS);
static	int		dazonemodesysctl(SYSCTL_HANDLER_ARGS);
static	int		dazonesupsysctl(SYSCTL_HANDLER_ARGS);
static	int		dadeletemaxsysctl(SYSCTL_HANDLER_ARGS);
static	void		dadeletemethodset(struct da_softc *softc,
					  da_delete_methods delete_method);
static	off_t		dadeletemaxsize(struct da_softc *softc,
					da_delete_methods delete_method);
static	void		dadeletemethodchoose(struct da_softc *softc,
					     da_delete_methods default_method);
static	void		daprobedone(struct cam_periph *periph, union ccb *ccb);

static	periph_ctor_t	daregister;
static	periph_dtor_t	dacleanup;
static	periph_start_t	dastart;
static	periph_oninv_t	daoninvalidate;
static	void		dazonedone(struct cam_periph *periph, union ccb *ccb);
static	void		dadone(struct cam_periph *periph,
			       union ccb *done_ccb);
static void		dadone_probewp(struct cam_periph *periph,
				       union ccb *done_ccb);
static void		dadone_proberc(struct cam_periph *periph,
				       union ccb *done_ccb);
static void		dadone_probelbp(struct cam_periph *periph,
					union ccb *done_ccb);
static void		dadone_probeblklimits(struct cam_periph *periph,
					      union ccb *done_ccb);
static void		dadone_probebdc(struct cam_periph *periph,
					union ccb *done_ccb);
static void		dadone_probeata(struct cam_periph *periph,
					union ccb *done_ccb);
static void		dadone_probeatalogdir(struct cam_periph *periph,
					      union ccb *done_ccb);
static void		dadone_probeataiddir(struct cam_periph *periph,
					     union ccb *done_ccb);
static void		dadone_probeatasup(struct cam_periph *periph,
					   union ccb *done_ccb);
static void		dadone_probeatazone(struct cam_periph *periph,
					    union ccb *done_ccb);
static void		dadone_probezone(struct cam_periph *periph,
					 union ccb *done_ccb);
static void		dadone_tur(struct cam_periph *periph,
				   union ccb *done_ccb);
static  int		daerror(union ccb *ccb, u_int32_t cam_flags,
				u_int32_t sense_flags);
static void		daprevent(struct cam_periph *periph, int action);
static void		dareprobe(struct cam_periph *periph);
static void		dasetgeom(struct cam_periph *periph, uint32_t block_len,
				  uint64_t maxsector,
				  struct scsi_read_capacity_data_long *rcaplong,
				  size_t rcap_size);
static timeout_t	dasendorderedtag;
static void		dashutdown(void *arg, int howto);
static timeout_t	damediapoll;

#ifndef	DA_DEFAULT_POLL_PERIOD
#define	DA_DEFAULT_POLL_PERIOD	3
#endif

#ifndef DA_DEFAULT_TIMEOUT
#define DA_DEFAULT_TIMEOUT 60	/* Timeout in seconds */
#endif

#ifndef DA_DEFAULT_SOFTTIMEOUT
#define DA_DEFAULT_SOFTTIMEOUT	0
#endif

#ifndef	DA_DEFAULT_RETRY
#define	DA_DEFAULT_RETRY	4
#endif

#ifndef	DA_DEFAULT_SEND_ORDERED
#define	DA_DEFAULT_SEND_ORDERED	1
#endif

static int da_poll_period = DA_DEFAULT_POLL_PERIOD;
static int da_retry_count = DA_DEFAULT_RETRY;
static int da_default_timeout = DA_DEFAULT_TIMEOUT;
static sbintime_t da_default_softtimeout = DA_DEFAULT_SOFTTIMEOUT;
static int da_send_ordered = DA_DEFAULT_SEND_ORDERED;
static int da_disable_wp_detection = 0;

static SYSCTL_NODE(_kern_cam, OID_AUTO, da, CTLFLAG_RD, 0,
            "CAM Direct Access Disk driver");
SYSCTL_INT(_kern_cam_da, OID_AUTO, poll_period, CTLFLAG_RWTUN,
           &da_poll_period, 0, "Media polling period in seconds");
SYSCTL_INT(_kern_cam_da, OID_AUTO, retry_count, CTLFLAG_RWTUN,
           &da_retry_count, 0, "Normal I/O retry count");
SYSCTL_INT(_kern_cam_da, OID_AUTO, default_timeout, CTLFLAG_RWTUN,
           &da_default_timeout, 0, "Normal I/O timeout (in seconds)");
SYSCTL_INT(_kern_cam_da, OID_AUTO, send_ordered, CTLFLAG_RWTUN,
           &da_send_ordered, 0, "Send Ordered Tags");
SYSCTL_INT(_kern_cam_da, OID_AUTO, disable_wp_detection, CTLFLAG_RWTUN,
           &da_disable_wp_detection, 0,
	   "Disable detection of write-protected disks");

SYSCTL_PROC(_kern_cam_da, OID_AUTO, default_softtimeout,
    CTLTYPE_UINT | CTLFLAG_RW, NULL, 0, dasysctlsofttimeout, "I",
    "Soft I/O timeout (ms)");
TUNABLE_INT64("kern.cam.da.default_softtimeout", &da_default_softtimeout);

/*
 * DA_ORDEREDTAG_INTERVAL determines how often, relative
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
#ifndef DA_ORDEREDTAG_INTERVAL
#define DA_ORDEREDTAG_INTERVAL 4
#endif

static struct periph_driver dadriver =
{
	dainit, "da",
	TAILQ_HEAD_INITIALIZER(dadriver.units), /* generation */ 0
};

PERIPHDRIVER_DECLARE(da, dadriver);

static MALLOC_DEFINE(M_SCSIDA, "scsi_da", "scsi_da buffers");

/*
 * This driver takes out references / holds in well defined pairs, never
 * recursively. These macros / inline functions enforce those rules. They
 * are only enabled with DA_TRACK_REFS or INVARIANTS. If DA_TRACK_REFS is
 * defined to be 2 or larger, the tracking also includes debug printfs.
 */
#if defined(DA_TRACK_REFS) || defined(INVARIANTS)

#ifndef DA_TRACK_REFS
#define DA_TRACK_REFS 1
#endif

#if DA_TRACK_REFS > 1
static const char *da_ref_text[] = {
	"bogus",
	"open",
	"open hold",
	"close hold",
	"reprobe hold",
	"Test Unit Ready",
	"Geom",
	"sysctl",
	"reprobe",
	"max -- also bogus"
};

#define DA_PERIPH_PRINT(periph, msg, args...)		\
	CAM_PERIPH_PRINT(periph, msg, ##args)
#else
#define DA_PERIPH_PRINT(periph, msg, args...)
#endif

static inline void
token_sanity(da_ref_token token)
{
	if ((unsigned)token >= DA_REF_MAX)
		panic("Bad token value passed in %d\n", token);
}

static inline int
da_periph_hold(struct cam_periph *periph, int priority, da_ref_token token)
{
	int err = cam_periph_hold(periph, priority);

	token_sanity(token);
	DA_PERIPH_PRINT(periph, "Holding device %s (%d): %d\n",
	    da_ref_text[token], token, err);
	if (err == 0) {
		int cnt;
		struct da_softc *softc = periph->softc;

		cnt = atomic_fetchadd_int(&softc->ref_flags[token], 1);
		if (cnt != 0)
			panic("Re-holding for reason %d, cnt = %d", token, cnt);
	}
	return (err);
}

static inline void
da_periph_unhold(struct cam_periph *periph, da_ref_token token)
{
	int cnt;
	struct da_softc *softc = periph->softc;

	token_sanity(token);
	DA_PERIPH_PRINT(periph, "Unholding device %s (%d)\n",
	    da_ref_text[token], token);
	cnt = atomic_fetchadd_int(&softc->ref_flags[token], -1);
	if (cnt != 1)
		panic("Unholding %d with cnt = %d", token, cnt);
	cam_periph_unhold(periph);
}

static inline int
da_periph_acquire(struct cam_periph *periph, da_ref_token token)
{
	int err = cam_periph_acquire(periph);

	token_sanity(token);
	DA_PERIPH_PRINT(periph, "acquiring device %s (%d): %d\n",
	    da_ref_text[token], token, err);
	if (err == 0) {
		int cnt;
		struct da_softc *softc = periph->softc;

		cnt = atomic_fetchadd_int(&softc->ref_flags[token], 1);
		if (cnt != 0)
			panic("Re-refing for reason %d, cnt = %d", token, cnt);
	}
	return (err);
}

static inline void
da_periph_release(struct cam_periph *periph, da_ref_token token)
{
	int cnt;
	struct da_softc *softc = periph->softc;

	token_sanity(token);
	DA_PERIPH_PRINT(periph, "releasing device %s (%d)\n",
	    da_ref_text[token], token);
	cnt = atomic_fetchadd_int(&softc->ref_flags[token], -1);
	if (cnt != 1)
		panic("Releasing %d with cnt = %d", token, cnt);
	cam_periph_release(periph);
}

static inline void
da_periph_release_locked(struct cam_periph *periph, da_ref_token token)
{
	int cnt;
	struct da_softc *softc = periph->softc;

	token_sanity(token);
	DA_PERIPH_PRINT(periph, "releasing device (locked) %s (%d)\n",
	    da_ref_text[token], token);
	cnt = atomic_fetchadd_int(&softc->ref_flags[token], -1);
	if (cnt != 1)
		panic("Unholding %d with cnt = %d", token, cnt);
	cam_periph_release_locked(periph);
}

#define cam_periph_hold POISON
#define cam_periph_unhold POISON
#define cam_periph_acquire POISON
#define cam_periph_release POISON
#define cam_periph_release_locked POISON

#else
#define	da_periph_hold(periph, prio, token)	cam_periph_hold((periph), (prio))
#define da_periph_unhold(periph, token)		cam_periph_unhold((periph))
#define da_periph_acquire(periph, token)	cam_periph_acquire((periph))
#define da_periph_release(periph, token)	cam_periph_release((periph))
#define da_periph_release_locked(periph, token)	cam_periph_release_locked((periph))
#endif

static int
daopen(struct disk *dp)
{
	struct cam_periph *periph;
	struct da_softc *softc;
	int error;

	periph = (struct cam_periph *)dp->d_drv1;
	if (da_periph_acquire(periph, DA_REF_OPEN) != 0) {
		return (ENXIO);
	}

	cam_periph_lock(periph);
	if ((error = da_periph_hold(periph, PRIBIO|PCATCH, DA_REF_OPEN_HOLD)) != 0) {
		cam_periph_unlock(periph);
		da_periph_release(periph, DA_REF_OPEN);
		return (error);
	}

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE | CAM_DEBUG_PERIPH,
	    ("daopen\n"));

	softc = (struct da_softc *)periph->softc;
	dareprobe(periph);

	/* Wait for the disk size update.  */
	error = cam_periph_sleep(periph, &softc->disk->d_mediasize, PRIBIO,
	    "dareprobe", 0);
	if (error != 0)
		xpt_print(periph->path, "unable to retrieve capacity data\n");

	if (periph->flags & CAM_PERIPH_INVALID)
		error = ENXIO;

	if (error == 0 && (softc->flags & DA_FLAG_PACK_REMOVABLE) != 0 &&
	    (softc->quirks & DA_Q_NO_PREVENT) == 0)
		daprevent(periph, PR_PREVENT);

	if (error == 0) {
		softc->flags &= ~DA_FLAG_PACK_INVALID;
		softc->flags |= DA_FLAG_OPEN;
	}

	da_periph_unhold(periph, DA_REF_OPEN_HOLD);
	cam_periph_unlock(periph);

	if (error != 0)
		da_periph_release(periph, DA_REF_OPEN);

	return (error);
}

static int
daclose(struct disk *dp)
{
	struct	cam_periph *periph;
	struct	da_softc *softc;
	union	ccb *ccb;

	periph = (struct cam_periph *)dp->d_drv1;
	softc = (struct da_softc *)periph->softc;
	cam_periph_lock(periph);
	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE | CAM_DEBUG_PERIPH,
	    ("daclose\n"));

	if (da_periph_hold(periph, PRIBIO, DA_REF_CLOSE_HOLD) == 0) {

		/* Flush disk cache. */
		if ((softc->flags & DA_FLAG_DIRTY) != 0 &&
		    (softc->quirks & DA_Q_NO_SYNC_CACHE) == 0 &&
		    (softc->flags & DA_FLAG_PACK_INVALID) == 0) {
			ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
			scsi_synchronize_cache(&ccb->csio, /*retries*/1,
			    /*cbfcnp*/NULL, MSG_SIMPLE_Q_TAG,
			    /*begin_lba*/0, /*lb_count*/0, SSD_FULL_SIZE,
			    5 * 60 * 1000);
			cam_periph_runccb(ccb, daerror, /*cam_flags*/0,
			    /*sense_flags*/SF_RETRY_UA | SF_QUIET_IR,
			    softc->disk->d_devstat);
			softc->flags &= ~DA_FLAG_DIRTY;
			xpt_release_ccb(ccb);
		}

		/* Allow medium removal. */
		if ((softc->flags & DA_FLAG_PACK_REMOVABLE) != 0 &&
		    (softc->quirks & DA_Q_NO_PREVENT) == 0)
			daprevent(periph, PR_ALLOW);

		da_periph_unhold(periph, DA_REF_CLOSE_HOLD);
	}

	/*
	 * If we've got removeable media, mark the blocksize as
	 * unavailable, since it could change when new media is
	 * inserted.
	 */
	if ((softc->flags & DA_FLAG_PACK_REMOVABLE) != 0)
		softc->disk->d_devstat->flags |= DEVSTAT_BS_UNAVAILABLE;

	softc->flags &= ~DA_FLAG_OPEN;
	while (softc->refcount != 0)
		cam_periph_sleep(periph, &softc->refcount, PRIBIO, "daclose", 1);
	cam_periph_unlock(periph);
	da_periph_release(periph, DA_REF_OPEN);
	return (0);
}

static void
daschedule(struct cam_periph *periph)
{
	struct da_softc *softc = (struct da_softc *)periph->softc;

	if (softc->state != DA_STATE_NORMAL)
		return;

	cam_iosched_schedule(softc->cam_iosched, periph);
}

/*
 * Actually translate the requested transfer into one the physical driver
 * can understand.  The transfer is described by a buf and will include
 * only one physical transfer.
 */
static void
dastrategy(struct bio *bp)
{
	struct cam_periph *periph;
	struct da_softc *softc;

	periph = (struct cam_periph *)bp->bio_disk->d_drv1;
	softc = (struct da_softc *)periph->softc;

	cam_periph_lock(periph);

	/*
	 * If the device has been made invalid, error out
	 */
	if ((softc->flags & DA_FLAG_PACK_INVALID)) {
		cam_periph_unlock(periph);
		biofinish(bp, NULL, ENXIO);
		return;
	}

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dastrategy(%p)\n", bp));

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
	daschedule(periph);
	cam_periph_unlock(periph);

	return;
}

static int
dadump(void *arg, void *virtual, vm_offset_t physical, off_t offset, size_t length)
{
	struct	    cam_periph *periph;
	struct	    da_softc *softc;
	u_int	    secsize;
	struct	    ccb_scsiio csio;
	struct	    disk *dp;
	int	    error = 0;

	dp = arg;
	periph = dp->d_drv1;
	softc = (struct da_softc *)periph->softc;
	secsize = softc->params.secsize;

	if ((softc->flags & DA_FLAG_PACK_INVALID) != 0)
		return (ENXIO);

	memset(&csio, 0, sizeof(csio));
	if (length > 0) {
		xpt_setup_ccb(&csio.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
		csio.ccb_h.ccb_state = DA_CCB_DUMP;
		scsi_read_write(&csio,
				/*retries*/0,
				/*cbfcnp*/NULL,
				MSG_ORDERED_Q_TAG,
				/*read*/SCSI_RW_WRITE,
				/*byte2*/0,
				/*minimum_cmd_size*/ softc->minimum_cmd_size,
				offset / secsize,
				length / secsize,
				/*data_ptr*/(u_int8_t *) virtual,
				/*dxfer_len*/length,
				/*sense_len*/SSD_FULL_SIZE,
				da_default_timeout * 1000);
		error = cam_periph_runccb((union ccb *)&csio, cam_periph_error,
		    0, SF_NO_RECOVERY | SF_NO_RETRY, NULL);
		if (error != 0)
			printf("Aborting dump due to I/O error.\n");
		return (error);
	}

	/*
	 * Sync the disk cache contents to the physical media.
	 */
	if ((softc->quirks & DA_Q_NO_SYNC_CACHE) == 0) {

		xpt_setup_ccb(&csio.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
		csio.ccb_h.ccb_state = DA_CCB_DUMP;
		scsi_synchronize_cache(&csio,
				       /*retries*/0,
				       /*cbfcnp*/NULL,
				       MSG_SIMPLE_Q_TAG,
				       /*begin_lba*/0,/* Cover the whole disk */
				       /*lb_count*/0,
				       SSD_FULL_SIZE,
				       5 * 1000);
		error = cam_periph_runccb((union ccb *)&csio, cam_periph_error,
		    0, SF_NO_RECOVERY | SF_NO_RETRY, NULL);
		if (error != 0)
			xpt_print(periph->path, "Synchronize cache failed\n");
	}
	return (error);
}

static int
dagetattr(struct bio *bp)
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

static void
dainit(void)
{
	cam_status status;

	/*
	 * Install a global async callback.  This callback will
	 * receive async callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, daasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("da: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	} else if (da_send_ordered) {

		/* Register our shutdown event handler */
		if ((EVENTHANDLER_REGISTER(shutdown_post_sync, dashutdown,
					   NULL, SHUTDOWN_PRI_DEFAULT)) == NULL)
		    printf("dainit: shutdown event registration failed!\n");
	}
}

/*
 * Callback from GEOM, called when it has finished cleaning up its
 * resources.
 */
static void
dadiskgonecb(struct disk *dp)
{
	struct cam_periph *periph;

	periph = (struct cam_periph *)dp->d_drv1;
	da_periph_release(periph, DA_REF_GEOM);
}

static void
daoninvalidate(struct cam_periph *periph)
{
	struct da_softc *softc;

	cam_periph_assert(periph, MA_OWNED);
	softc = (struct da_softc *)periph->softc;

	/*
	 * De-register any async callbacks.
	 */
	xpt_register_async(0, daasync, periph, periph->path);

	softc->flags |= DA_FLAG_PACK_INVALID;
#ifdef CAM_IO_STATS
	softc->invalidations++;
#endif

	/*
	 * Return all queued I/O with ENXIO.
	 * XXX Handle any transactions queued to the card
	 *     with XPT_ABORT_CCB.
	 */
	cam_iosched_flush(softc->cam_iosched, NULL, ENXIO);

	/*
	 * Tell GEOM that we've gone away, we'll get a callback when it is
	 * done cleaning up its resources.
	 */
	disk_gone(softc->disk);
}

static void
dacleanup(struct cam_periph *periph)
{
	struct da_softc *softc;

	softc = (struct da_softc *)periph->softc;

	cam_periph_unlock(periph);

	cam_iosched_fini(softc->cam_iosched);

	/*
	 * If we can't free the sysctl tree, oh well...
	 */
	if ((softc->flags & DA_FLAG_SCTX_INIT) != 0) {
#ifdef CAM_IO_STATS
		if (sysctl_ctx_free(&softc->sysctl_stats_ctx) != 0)
			xpt_print(periph->path,
			    "can't remove sysctl stats context\n");
#endif
		if (sysctl_ctx_free(&softc->sysctl_ctx) != 0)
			xpt_print(periph->path,
			    "can't remove sysctl context\n");
	}

	callout_drain(&softc->mediapoll_c);
	disk_destroy(softc->disk);
	callout_drain(&softc->sendordered_c);
	free(softc, M_DEVBUF);
	cam_periph_lock(periph);
}

static void
daasync(void *callback_arg, u_int32_t code,
	struct cam_path *path, void *arg)
{
	struct cam_periph *periph;
	struct da_softc *softc;

	periph = (struct cam_periph *)callback_arg;
	switch (code) {
	case AC_FOUND_DEVICE:	/* callback to create periph, no locking yet */
	{
		struct ccb_getdev *cgd;
		cam_status status;

		cgd = (struct ccb_getdev *)arg;
		if (cgd == NULL)
			break;

		if (cgd->protocol != PROTO_SCSI)
			break;
		if (SID_QUAL(&cgd->inq_data) != SID_QUAL_LU_CONNECTED)
			break;
		if (SID_TYPE(&cgd->inq_data) != T_DIRECT
		    && SID_TYPE(&cgd->inq_data) != T_RBC
		    && SID_TYPE(&cgd->inq_data) != T_OPTICAL
		    && SID_TYPE(&cgd->inq_data) != T_ZBC_HM)
			break;

		/*
		 * Allocate a peripheral instance for
		 * this device and start the probe
		 * process.
		 */
		status = cam_periph_alloc(daregister, daoninvalidate,
					  dacleanup, dastart,
					  "da", CAM_PERIPH_BIO,
					  path, daasync,
					  AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP
		 && status != CAM_REQ_INPROG)
			printf("daasync: Unable to attach to new device "
				"due to status 0x%x\n", status);
		return;
	}
	case AC_ADVINFO_CHANGED:	/* Doesn't touch periph */
	{
		uintptr_t buftype;

		buftype = (uintptr_t)arg;
		if (buftype == CDAI_TYPE_PHYS_PATH) {
			struct da_softc *softc;

			softc = periph->softc;
			disk_attr_changed(softc->disk, "GEOM::physpath",
					  M_NOWAIT);
		}
		break;
	}
	case AC_UNIT_ATTENTION:
	{
		union ccb *ccb;
		int error_code, sense_key, asc, ascq;

		softc = (struct da_softc *)periph->softc;
		ccb = (union ccb *)arg;

		/*
		 * Handle all UNIT ATTENTIONs except our own, as they will be
		 * handled by daerror(). Since this comes from a different periph,
		 * that periph's lock is held, not ours, so we have to take it ours
		 * out to touch softc flags.
		 */
		if (xpt_path_periph(ccb->ccb_h.path) != periph &&
		    scsi_extract_sense_ccb(ccb,
		     &error_code, &sense_key, &asc, &ascq)) {
			if (asc == 0x2A && ascq == 0x09) {
				xpt_print(ccb->ccb_h.path,
				    "Capacity data has changed\n");
				cam_periph_lock(periph);
				softc->flags &= ~DA_FLAG_PROBED;
				cam_periph_unlock(periph);
				dareprobe(periph);
			} else if (asc == 0x28 && ascq == 0x00) {
				cam_periph_lock(periph);
				softc->flags &= ~DA_FLAG_PROBED;
				cam_periph_unlock(periph);
				disk_media_changed(softc->disk, M_NOWAIT);
			} else if (asc == 0x3F && ascq == 0x03) {
				xpt_print(ccb->ccb_h.path,
				    "INQUIRY data has changed\n");
				cam_periph_lock(periph);
				softc->flags &= ~DA_FLAG_PROBED;
				cam_periph_unlock(periph);
				dareprobe(periph);
			}
		}
		break;
	}
	case AC_SCSI_AEN:		/* Called for this path: periph locked */
		/*
		 * Appears to be currently unused for SCSI devices, only ata SIMs
		 * generate this.
		 */
		cam_periph_assert(periph, MA_OWNED);
		softc = (struct da_softc *)periph->softc;
		if (!cam_iosched_has_work_flags(softc->cam_iosched, DA_WORK_TUR) &&
		    (softc->flags & DA_FLAG_TUR_PENDING) == 0) {
			if (da_periph_acquire(periph, DA_REF_TUR) == 0) {
				cam_iosched_set_work_flags(softc->cam_iosched, DA_WORK_TUR);
				daschedule(periph);
			}
		}
		/* FALLTHROUGH */
	case AC_SENT_BDR:		/* Called for this path: periph locked */
	case AC_BUS_RESET:		/* Called for this path: periph locked */
	{
		struct ccb_hdr *ccbh;

		cam_periph_assert(periph, MA_OWNED);
		softc = (struct da_softc *)periph->softc;
		/*
		 * Don't fail on the expected unit attention
		 * that will occur.
		 */
		softc->flags |= DA_FLAG_RETRY_UA;
		LIST_FOREACH(ccbh, &softc->pending_ccbs, periph_links.le)
			ccbh->ccb_state |= DA_CCB_RETRY_UA;
		break;
	}
	case AC_INQ_CHANGED:		/* Called for this path: periph locked */
		cam_periph_assert(periph, MA_OWNED);
		softc = (struct da_softc *)periph->softc;
		softc->flags &= ~DA_FLAG_PROBED;
		dareprobe(periph);
		break;
	default:
		break;
	}
	cam_periph_async(periph, code, path, arg);
}

static void
dasysctlinit(void *context, int pending)
{
	struct cam_periph *periph;
	struct da_softc *softc;
	char tmpstr[32], tmpstr2[16];
	struct ccb_trans_settings cts;

	periph = (struct cam_periph *)context;
	/*
	 * periph was held for us when this task was enqueued
	 */
	if (periph->flags & CAM_PERIPH_INVALID) {
		da_periph_release(periph, DA_REF_SYSCTL);
		return;
	}

	softc = (struct da_softc *)periph->softc;
	snprintf(tmpstr, sizeof(tmpstr), "CAM DA unit %d", periph->unit_number);
	snprintf(tmpstr2, sizeof(tmpstr2), "%d", periph->unit_number);

	sysctl_ctx_init(&softc->sysctl_ctx);
	cam_periph_lock(periph);
	softc->flags |= DA_FLAG_SCTX_INIT;
	cam_periph_unlock(periph);
	softc->sysctl_tree = SYSCTL_ADD_NODE_WITH_LABEL(&softc->sysctl_ctx,
		SYSCTL_STATIC_CHILDREN(_kern_cam_da), OID_AUTO, tmpstr2,
		CTLFLAG_RD, 0, tmpstr, "device_index");
	if (softc->sysctl_tree == NULL) {
		printf("dasysctlinit: unable to allocate sysctl tree\n");
		da_periph_release(periph, DA_REF_SYSCTL);
		return;
	}

	/*
	 * Now register the sysctl handler, so the user can change the value on
	 * the fly.
	 */
	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "delete_method", CTLTYPE_STRING | CTLFLAG_RWTUN,
		softc, 0, dadeletemethodsysctl, "A",
		"BIO_DELETE execution method");
	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "delete_max", CTLTYPE_U64 | CTLFLAG_RW,
		softc, 0, dadeletemaxsysctl, "Q",
		"Maximum BIO_DELETE size");
	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "minimum_cmd_size", CTLTYPE_INT | CTLFLAG_RW,
		&softc->minimum_cmd_size, 0, dacmdsizesysctl, "I",
		"Minimum CDB size");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"trim_count", CTLFLAG_RD, &softc->trim_count,
		"Total number of unmap/dsm commands sent");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"trim_ranges", CTLFLAG_RD, &softc->trim_ranges,
		"Total number of ranges in unmap/dsm commands");
	SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
		SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO,
		"trim_lbas", CTLFLAG_RD, &softc->trim_lbas,
		"Total lbas in the unmap/dsm commands sent");

	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "zone_mode", CTLTYPE_STRING | CTLFLAG_RD,
		softc, 0, dazonemodesysctl, "A",
		"Zone Mode");
	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "zone_support", CTLTYPE_STRING | CTLFLAG_RD,
		softc, 0, dazonesupsysctl, "A",
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

	SYSCTL_ADD_INT(&softc->sysctl_ctx,
		       SYSCTL_CHILDREN(softc->sysctl_tree),
		       OID_AUTO,
		       "error_inject",
		       CTLFLAG_RW,
		       &softc->error_inject,
		       0,
		       "error_inject leaf");

	SYSCTL_ADD_INT(&softc->sysctl_ctx,
		       SYSCTL_CHILDREN(softc->sysctl_tree),
		       OID_AUTO,
		       "unmapped_io",
		       CTLFLAG_RD,
		       &softc->unmappedio,
		       0,
		       "Unmapped I/O leaf");

	SYSCTL_ADD_INT(&softc->sysctl_ctx,
		       SYSCTL_CHILDREN(softc->sysctl_tree),
		       OID_AUTO,
		       "rotating",
		       CTLFLAG_RD,
		       &softc->rotating,
		       0,
		       "Rotating media");

#ifdef CAM_TEST_FAILURE
	SYSCTL_ADD_PROC(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "invalidate", CTLTYPE_U64 | CTLFLAG_RW | CTLFLAG_MPSAFE,
		periph, 0, cam_periph_invalidate_sysctl, "I",
		"Write 1 to invalidate the drive immediately");
#endif

	/*
	 * Add some addressing info.
	 */
	memset(&cts, 0, sizeof (cts));
	xpt_setup_ccb(&cts.ccb_h, periph->path, CAM_PRIORITY_NONE);
	cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
	cts.type = CTS_TYPE_CURRENT_SETTINGS;
	cam_periph_lock(periph);
	xpt_action((union ccb *)&cts);
	cam_periph_unlock(periph);
	if (cts.ccb_h.status != CAM_REQ_CMP) {
		da_periph_release(periph, DA_REF_SYSCTL);
		return;
	}
	if (cts.protocol == PROTO_SCSI && cts.transport == XPORT_FC) {
		struct ccb_trans_settings_fc *fc = &cts.xport_specific.fc;
		if (fc->valid & CTS_FC_VALID_WWPN) {
			softc->wwpn = fc->wwpn;
			SYSCTL_ADD_UQUAD(&softc->sysctl_ctx,
			    SYSCTL_CHILDREN(softc->sysctl_tree),
			    OID_AUTO, "wwpn", CTLFLAG_RD,
			    &softc->wwpn, "World Wide Port Name");
		}
	}

#ifdef CAM_IO_STATS
	/*
	 * Now add some useful stats.
	 * XXX These should live in cam_periph and be common to all periphs
	 */
	softc->sysctl_stats_tree = SYSCTL_ADD_NODE(&softc->sysctl_stats_ctx,
	    SYSCTL_CHILDREN(softc->sysctl_tree), OID_AUTO, "stats",
	    CTLFLAG_RD, 0, "Statistics");
	SYSCTL_ADD_INT(&softc->sysctl_stats_ctx,
		       SYSCTL_CHILDREN(softc->sysctl_stats_tree),
		       OID_AUTO,
		       "errors",
		       CTLFLAG_RD,
		       &softc->errors,
		       0,
		       "Transport errors reported by the SIM");
	SYSCTL_ADD_INT(&softc->sysctl_stats_ctx,
		       SYSCTL_CHILDREN(softc->sysctl_stats_tree),
		       OID_AUTO,
		       "timeouts",
		       CTLFLAG_RD,
		       &softc->timeouts,
		       0,
		       "Device timeouts reported by the SIM");
	SYSCTL_ADD_INT(&softc->sysctl_stats_ctx,
		       SYSCTL_CHILDREN(softc->sysctl_stats_tree),
		       OID_AUTO,
		       "pack_invalidations",
		       CTLFLAG_RD,
		       &softc->invalidations,
		       0,
		       "Device pack invalidations");
#endif

	cam_iosched_sysctl_init(softc->cam_iosched, &softc->sysctl_ctx,
	    softc->sysctl_tree);

	da_periph_release(periph, DA_REF_SYSCTL);
}

static int
dadeletemaxsysctl(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint64_t value;
	struct da_softc *softc;

	softc = (struct da_softc *)arg1;

	value = softc->disk->d_delmaxsize;
	error = sysctl_handle_64(oidp, &value, 0, req);
	if ((error != 0) || (req->newptr == NULL))
		return (error);

	/* only accept values smaller than the calculated value */
	if (value > dadeletemaxsize(softc, softc->delete_method)) {
		return (EINVAL);
	}
	softc->disk->d_delmaxsize = value;

	return (0);
}

static int
dacmdsizesysctl(SYSCTL_HANDLER_ARGS)
{
	int error, value;

	value = *(int *)arg1;

	error = sysctl_handle_int(oidp, &value, 0, req);

	if ((error != 0)
	 || (req->newptr == NULL))
		return (error);

	/*
	 * Acceptable values here are 6, 10, 12 or 16.
	 */
	if (value < 6)
		value = 6;
	else if ((value > 6)
	      && (value <= 10))
		value = 10;
	else if ((value > 10)
	      && (value <= 12))
		value = 12;
	else if (value > 12)
		value = 16;

	*(int *)arg1 = value;

	return (0);
}

static int
dasysctlsofttimeout(SYSCTL_HANDLER_ARGS)
{
	sbintime_t value;
	int error;

	value = da_default_softtimeout / SBT_1MS;

	error = sysctl_handle_int(oidp, (int *)&value, 0, req);
	if ((error != 0) || (req->newptr == NULL))
		return (error);

	/* XXX Should clip this to a reasonable level */
	if (value > da_default_timeout * 1000)
		return (EINVAL);

	da_default_softtimeout = value * SBT_1MS;
	return (0);
}

static void
dadeletemethodset(struct da_softc *softc, da_delete_methods delete_method)
{

	softc->delete_method = delete_method;
	softc->disk->d_delmaxsize = dadeletemaxsize(softc, delete_method);
	softc->delete_func = da_delete_functions[delete_method];

	if (softc->delete_method > DA_DELETE_DISABLE)
		softc->disk->d_flags |= DISKFLAG_CANDELETE;
	else
		softc->disk->d_flags &= ~DISKFLAG_CANDELETE;
}

static off_t
dadeletemaxsize(struct da_softc *softc, da_delete_methods delete_method)
{
	off_t sectors;

	switch(delete_method) {
	case DA_DELETE_UNMAP:
		sectors = (off_t)softc->unmap_max_lba;
		break;
	case DA_DELETE_ATA_TRIM:
		sectors = (off_t)ATA_DSM_RANGE_MAX * softc->trim_max_ranges;
		break;
	case DA_DELETE_WS16:
		sectors = omin(softc->ws_max_blks, WS16_MAX_BLKS);
		break;
	case DA_DELETE_ZERO:
	case DA_DELETE_WS10:
		sectors = omin(softc->ws_max_blks, WS10_MAX_BLKS);
		break;
	default:
		return 0;
	}

	return (off_t)softc->params.secsize *
	    omin(sectors, softc->params.sectors);
}

static void
daprobedone(struct cam_periph *periph, union ccb *ccb)
{
	struct da_softc *softc;

	softc = (struct da_softc *)periph->softc;

	cam_periph_assert(periph, MA_OWNED);

	dadeletemethodchoose(softc, DA_DELETE_NONE);

	if (bootverbose && (softc->flags & DA_FLAG_ANNOUNCED) == 0) {
		char buf[80];
		int i, sep;

		snprintf(buf, sizeof(buf), "Delete methods: <");
		sep = 0;
		for (i = 0; i <= DA_DELETE_MAX; i++) {
			if ((softc->delete_available & (1 << i)) == 0 &&
			    i != softc->delete_method)
				continue;
			if (sep)
				strlcat(buf, ",", sizeof(buf));
			strlcat(buf, da_delete_method_names[i],
			    sizeof(buf));
			if (i == softc->delete_method)
				strlcat(buf, "(*)", sizeof(buf));
			sep = 1;
		}
		strlcat(buf, ">", sizeof(buf));
		printf("%s%d: %s\n", periph->periph_name,
		    periph->unit_number, buf);
	}
	if ((softc->disk->d_flags & DISKFLAG_WRITE_PROTECT) != 0 &&
	    (softc->flags & DA_FLAG_ANNOUNCED) == 0) {
		printf("%s%d: Write Protected\n", periph->periph_name,
		    periph->unit_number);
	}

	/*
	 * Since our peripheral may be invalidated by an error
	 * above or an external event, we must release our CCB
	 * before releasing the probe lock on the peripheral.
	 * The peripheral will only go away once the last lock
	 * is removed, and we need it around for the CCB release
	 * operation.
	 */
	xpt_release_ccb(ccb);
	softc->state = DA_STATE_NORMAL;
	softc->flags |= DA_FLAG_PROBED;
	daschedule(periph);
	wakeup(&softc->disk->d_mediasize);
	if ((softc->flags & DA_FLAG_ANNOUNCED) == 0) {
		softc->flags |= DA_FLAG_ANNOUNCED;
		da_periph_unhold(periph, DA_REF_PROBE_HOLD);
	} else
		da_periph_release_locked(periph, DA_REF_REPROBE);
}

static void
dadeletemethodchoose(struct da_softc *softc, da_delete_methods default_method)
{
	int i, methods;

	/* If available, prefer the method requested by user. */
	i = softc->delete_method_pref;
	methods = softc->delete_available | (1 << DA_DELETE_DISABLE);
	if (methods & (1 << i)) {
		dadeletemethodset(softc, i);
		return;
	}

	/* Use the pre-defined order to choose the best performing delete. */
	for (i = DA_DELETE_MIN; i <= DA_DELETE_MAX; i++) {
		if (i == DA_DELETE_ZERO)
			continue;
		if (softc->delete_available & (1 << i)) {
			dadeletemethodset(softc, i);
			return;
		}
	}

	/* Fallback to default. */
	dadeletemethodset(softc, default_method);
}

static int
dadeletemethodsysctl(SYSCTL_HANDLER_ARGS)
{
	char buf[16];
	const char *p;
	struct da_softc *softc;
	int i, error, value;

	softc = (struct da_softc *)arg1;

	value = softc->delete_method;
	if (value < 0 || value > DA_DELETE_MAX)
		p = "UNKNOWN";
	else
		p = da_delete_method_names[value];
	strncpy(buf, p, sizeof(buf));
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	for (i = 0; i <= DA_DELETE_MAX; i++) {
		if (strcmp(buf, da_delete_method_names[i]) == 0)
			break;
	}
	if (i > DA_DELETE_MAX)
		return (EINVAL);
	softc->delete_method_pref = i;
	dadeletemethodchoose(softc, DA_DELETE_NONE);
	return (0);
}

static int
dazonemodesysctl(SYSCTL_HANDLER_ARGS)
{
	char tmpbuf[40];
	struct da_softc *softc;
	int error;

	softc = (struct da_softc *)arg1;

	switch (softc->zone_mode) {
	case DA_ZONE_DRIVE_MANAGED:
		snprintf(tmpbuf, sizeof(tmpbuf), "Drive Managed");
		break;
	case DA_ZONE_HOST_AWARE:
		snprintf(tmpbuf, sizeof(tmpbuf), "Host Aware");
		break;
	case DA_ZONE_HOST_MANAGED:
		snprintf(tmpbuf, sizeof(tmpbuf), "Host Managed");
		break;
	case DA_ZONE_NONE:
	default:
		snprintf(tmpbuf, sizeof(tmpbuf), "Not Zoned");
		break;
	}

	error = sysctl_handle_string(oidp, tmpbuf, sizeof(tmpbuf), req);

	return (error);
}

static int
dazonesupsysctl(SYSCTL_HANDLER_ARGS)
{
	char tmpbuf[180];
	struct da_softc *softc;
	struct sbuf sb;
	int error, first;
	unsigned int i;

	softc = (struct da_softc *)arg1;

	error = 0;
	first = 1;
	sbuf_new(&sb, tmpbuf, sizeof(tmpbuf), 0);

	for (i = 0; i < sizeof(da_zone_desc_table) /
	     sizeof(da_zone_desc_table[0]); i++) {
		if (softc->zone_flags & da_zone_desc_table[i].value) {
			if (first == 0)
				sbuf_printf(&sb, ", ");
			else
				first = 0;
			sbuf_cat(&sb, da_zone_desc_table[i].desc);
		}
	}

	if (first == 1)
		sbuf_printf(&sb, "None");

	sbuf_finish(&sb);

	error = sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);

	return (error);
}

static cam_status
daregister(struct cam_periph *periph, void *arg)
{
	struct da_softc *softc;
	struct ccb_pathinq cpi;
	struct ccb_getdev *cgd;
	char tmpstr[80];
	caddr_t match;

	cgd = (struct ccb_getdev *)arg;
	if (cgd == NULL) {
		printf("daregister: no getdev CCB, can't register device\n");
		return(CAM_REQ_CMP_ERR);
	}

	softc = (struct da_softc *)malloc(sizeof(*softc), M_DEVBUF,
	    M_NOWAIT|M_ZERO);

	if (softc == NULL) {
		printf("daregister: Unable to probe new device. "
		       "Unable to allocate softc\n");
		return(CAM_REQ_CMP_ERR);
	}

	if (cam_iosched_init(&softc->cam_iosched, periph) != 0) {
		printf("daregister: Unable to probe new device. "
		       "Unable to allocate iosched memory\n");
		free(softc, M_DEVBUF);
		return(CAM_REQ_CMP_ERR);
	}

	LIST_INIT(&softc->pending_ccbs);
	softc->state = DA_STATE_PROBE_WP;
	bioq_init(&softc->delete_run_queue);
	if (SID_IS_REMOVABLE(&cgd->inq_data))
		softc->flags |= DA_FLAG_PACK_REMOVABLE;
	softc->unmap_max_ranges = UNMAP_MAX_RANGES;
	softc->unmap_max_lba = UNMAP_RANGE_MAX;
	softc->unmap_gran = 0;
	softc->unmap_gran_align = 0;
	softc->ws_max_blks = WS16_MAX_BLKS;
	softc->trim_max_ranges = ATA_TRIM_MAX_RANGES;
	softc->rotating = 1;

	periph->softc = softc;

	/*
	 * See if this device has any quirks.
	 */
	match = cam_quirkmatch((caddr_t)&cgd->inq_data,
			       (caddr_t)da_quirk_table,
			       nitems(da_quirk_table),
			       sizeof(*da_quirk_table), scsi_inquiry_match);

	if (match != NULL)
		softc->quirks = ((struct da_quirk_entry *)match)->quirks;
	else
		softc->quirks = DA_Q_NONE;

	/* Check if the SIM does not want 6 byte commands */
	xpt_path_inq(&cpi, periph->path);
	if (cpi.ccb_h.status == CAM_REQ_CMP && (cpi.hba_misc & PIM_NO_6_BYTE))
		softc->quirks |= DA_Q_NO_6_BYTE;

	if (SID_TYPE(&cgd->inq_data) == T_ZBC_HM)
		softc->zone_mode = DA_ZONE_HOST_MANAGED;
	else if (softc->quirks & DA_Q_SMR_DM)
		softc->zone_mode = DA_ZONE_DRIVE_MANAGED;
	else
		softc->zone_mode = DA_ZONE_NONE;

	if (softc->zone_mode != DA_ZONE_NONE) {
		if (scsi_vpd_supported_page(periph, SVPD_ATA_INFORMATION)) {
			if (scsi_vpd_supported_page(periph, SVPD_ZONED_BDC))
				softc->zone_interface = DA_ZONE_IF_ATA_SAT;
			else
				softc->zone_interface = DA_ZONE_IF_ATA_PASS;
		} else
			softc->zone_interface = DA_ZONE_IF_SCSI;
	}

	TASK_INIT(&softc->sysctl_task, 0, dasysctlinit, periph);

	/*
	 * Take an exclusive section lock qon the periph while dastart is called
	 * to finish the probe.  The lock will be dropped in dadone at the end
	 * of probe. This locks out daopen and daclose from racing with the
	 * probe.
	 *
	 * XXX if cam_periph_hold returns an error, we don't hold a refcount.
	 */
	(void)da_periph_hold(periph, PRIBIO, DA_REF_PROBE_HOLD);

	/*
	 * Schedule a periodic event to occasionally send an
	 * ordered tag to a device.
	 */
	callout_init_mtx(&softc->sendordered_c, cam_periph_mtx(periph), 0);
	callout_reset(&softc->sendordered_c,
	    (da_default_timeout * hz) / DA_ORDEREDTAG_INTERVAL,
	    dasendorderedtag, periph);

	cam_periph_unlock(periph);
	/*
	 * RBC devices don't have to support READ(6), only READ(10).
	 */
	if (softc->quirks & DA_Q_NO_6_BYTE || SID_TYPE(&cgd->inq_data) == T_RBC)
		softc->minimum_cmd_size = 10;
	else
		softc->minimum_cmd_size = 6;

	/*
	 * Load the user's default, if any.
	 */
	snprintf(tmpstr, sizeof(tmpstr), "kern.cam.da.%d.minimum_cmd_size",
		 periph->unit_number);
	TUNABLE_INT_FETCH(tmpstr, &softc->minimum_cmd_size);

	/*
	 * 6, 10, 12 and 16 are the currently permissible values.
	 */
	if (softc->minimum_cmd_size > 12)
		softc->minimum_cmd_size = 16;
	else if (softc->minimum_cmd_size > 10)
		softc->minimum_cmd_size = 12;
	else if (softc->minimum_cmd_size > 6)
		softc->minimum_cmd_size = 10;
	else
		softc->minimum_cmd_size = 6;

	/* Predict whether device may support READ CAPACITY(16). */
	if (SID_ANSI_REV(&cgd->inq_data) >= SCSI_REV_SPC3 &&
	    (softc->quirks & DA_Q_NO_RC16) == 0) {
		softc->flags |= DA_FLAG_CAN_RC16;
	}

	/*
	 * Register this media as a disk.
	 */
	softc->disk = disk_alloc();
	softc->disk->d_devstat = devstat_new_entry(periph->periph_name,
			  periph->unit_number, 0,
			  DEVSTAT_BS_UNAVAILABLE,
			  SID_TYPE(&cgd->inq_data) |
			  XPORT_DEVSTAT_TYPE(cpi.transport),
			  DEVSTAT_PRIORITY_DISK);
	softc->disk->d_open = daopen;
	softc->disk->d_close = daclose;
	softc->disk->d_strategy = dastrategy;
	softc->disk->d_dump = dadump;
	softc->disk->d_getattr = dagetattr;
	softc->disk->d_gone = dadiskgonecb;
	softc->disk->d_name = "da";
	softc->disk->d_drv1 = periph;
	if (cpi.maxio == 0)
		softc->maxio = DFLTPHYS;	/* traditional default */
	else if (cpi.maxio > MAXPHYS)
		softc->maxio = MAXPHYS;		/* for safety */
	else
		softc->maxio = cpi.maxio;
	if (softc->quirks & DA_Q_128KB)
		softc->maxio = min(softc->maxio, 128 * 1024);
	softc->disk->d_maxsize = softc->maxio;
	softc->disk->d_unit = periph->unit_number;
	softc->disk->d_flags = DISKFLAG_DIRECT_COMPLETION | DISKFLAG_CANZONE;
	if ((softc->quirks & DA_Q_NO_SYNC_CACHE) == 0)
		softc->disk->d_flags |= DISKFLAG_CANFLUSHCACHE;
	if ((cpi.hba_misc & PIM_UNMAPPED) != 0) {
		softc->unmappedio = 1;
		softc->disk->d_flags |= DISKFLAG_UNMAPPED_BIO;
	}
	cam_strvis(softc->disk->d_descr, cgd->inq_data.vendor,
	    sizeof(cgd->inq_data.vendor), sizeof(softc->disk->d_descr));
	strlcat(softc->disk->d_descr, " ", sizeof(softc->disk->d_descr));
	cam_strvis(&softc->disk->d_descr[strlen(softc->disk->d_descr)],
	    cgd->inq_data.product, sizeof(cgd->inq_data.product),
	    sizeof(softc->disk->d_descr) - strlen(softc->disk->d_descr));
	softc->disk->d_hba_vendor = cpi.hba_vendor;
	softc->disk->d_hba_device = cpi.hba_device;
	softc->disk->d_hba_subvendor = cpi.hba_subvendor;
	softc->disk->d_hba_subdevice = cpi.hba_subdevice;

	/*
	 * Acquire a reference to the periph before we register with GEOM.
	 * We'll release this reference once GEOM calls us back (via
	 * dadiskgonecb()) telling us that our provider has been freed.
	 */
	if (da_periph_acquire(periph, DA_REF_GEOM) != 0) {
		xpt_print(periph->path, "%s: lost periph during "
			  "registration!\n", __func__);
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}

	disk_create(softc->disk, DISK_VERSION);
	cam_periph_lock(periph);

	/*
	 * Add async callbacks for events of interest.
	 * I don't bother checking if this fails as,
	 * in most cases, the system will function just
	 * fine without them and the only alternative
	 * would be to not attach the device on failure.
	 */
	xpt_register_async(AC_SENT_BDR | AC_BUS_RESET | AC_LOST_DEVICE |
	    AC_ADVINFO_CHANGED | AC_SCSI_AEN | AC_UNIT_ATTENTION |
	    AC_INQ_CHANGED, daasync, periph, periph->path);

	/*
	 * Emit an attribute changed notification just in case
	 * physical path information arrived before our async
	 * event handler was registered, but after anyone attaching
	 * to our disk device polled it.
	 */
	disk_attr_changed(softc->disk, "GEOM::physpath", M_NOWAIT);

	/*
	 * Schedule a periodic media polling events.
	 */
	callout_init_mtx(&softc->mediapoll_c, cam_periph_mtx(periph), 0);
	if ((softc->flags & DA_FLAG_PACK_REMOVABLE) &&
	    (cgd->inq_flags & SID_AEN) == 0 &&
	    da_poll_period != 0)
		callout_reset(&softc->mediapoll_c, da_poll_period * hz,
		    damediapoll, periph);

	xpt_schedule(periph, CAM_PRIORITY_DEV);

	return(CAM_REQ_CMP);
}

static int
da_zone_bio_to_scsi(int disk_zone_cmd)
{
	switch (disk_zone_cmd) {
	case DISK_ZONE_OPEN:
		return ZBC_OUT_SA_OPEN;
	case DISK_ZONE_CLOSE:
		return ZBC_OUT_SA_CLOSE;
	case DISK_ZONE_FINISH:
		return ZBC_OUT_SA_FINISH;
	case DISK_ZONE_RWP:
		return ZBC_OUT_SA_RWP;
	}

	return -1;
}

static int
da_zone_cmd(struct cam_periph *periph, union ccb *ccb, struct bio *bp,
	    int *queue_ccb)
{
	struct da_softc *softc;
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

		zone_sa = da_zone_bio_to_scsi(bp->bio_zone.zone_cmd);
		if (zone_sa == -1) {
			xpt_print(periph->path, "Cannot translate zone "
			    "cmd %#x to SCSI\n", bp->bio_zone.zone_cmd);
			error = EINVAL;
			goto bailout;
		}

		zone_flags = 0;
		lba = bp->bio_zone.zone_params.rwp.id;

		if (bp->bio_zone.zone_params.rwp.flags &
		    DISK_ZONE_RWP_FLAG_ALL)
			zone_flags |= ZBC_OUT_ALL;

		if (softc->zone_interface != DA_ZONE_IF_ATA_PASS) {
			scsi_zbc_out(&ccb->csio,
				     /*retries*/ da_retry_count,
				     /*cbfcnp*/ dadone,
				     /*tag_action*/ MSG_SIMPLE_Q_TAG,
				     /*service_action*/ zone_sa,
				     /*zone_id*/ lba,
				     /*zone_flags*/ zone_flags,
				     /*data_ptr*/ NULL,
				     /*dxfer_len*/ 0,
				     /*sense_len*/ SSD_FULL_SIZE,
				     /*timeout*/ da_default_timeout * 1000);
		} else {
			/*
			 * Note that in this case, even though we can
			 * technically use NCQ, we don't bother for several
			 * reasons:
			 * 1. It hasn't been tested on a SAT layer that
			 *    supports it.  This is new as of SAT-4.
			 * 2. Even when there is a SAT layer that supports
			 *    it, that SAT layer will also probably support
			 *    ZBC -> ZAC translation, since they are both
			 *    in the SAT-4 spec.
			 * 3. Translation will likely be preferable to ATA
			 *    passthrough.  LSI / Avago at least single
			 *    steps ATA passthrough commands in the HBA,
			 *    regardless of protocol, so unless that
			 *    changes, there is a performance penalty for
			 *    doing ATA passthrough no matter whether
			 *    you're using NCQ/FPDMA, DMA or PIO.
			 * 4. It requires a 32-byte CDB, which at least at
			 *    this point in CAM requires a CDB pointer, which
			 *    would require us to allocate an additional bit
			 *    of storage separate from the CCB.
			 */
			error = scsi_ata_zac_mgmt_out(&ccb->csio,
			    /*retries*/ da_retry_count,
			    /*cbfcnp*/ dadone,
			    /*tag_action*/ MSG_SIMPLE_Q_TAG,
			    /*use_ncq*/ 0,
			    /*zm_action*/ zone_sa,
			    /*zone_id*/ lba,
			    /*zone_flags*/ zone_flags,
			    /*data_ptr*/ NULL,
			    /*dxfer_len*/ 0,
			    /*cdb_storage*/ NULL,
			    /*cdb_storage_len*/ 0,
			    /*sense_len*/ SSD_FULL_SIZE,
			    /*timeout*/ da_default_timeout * 1000);
			if (error != 0) {
				error = EINVAL;
				xpt_print(periph->path,
				    "scsi_ata_zac_mgmt_out() returned an "
				    "error!");
				goto bailout;
			}
		}
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
		rz_ptr = malloc(alloc_size, M_SCSIDA, M_NOWAIT | M_ZERO);
		if (rz_ptr == NULL) {
			xpt_print(periph->path, "Unable to allocate memory "
			   "for Report Zones request\n");
			error = ENOMEM;
			goto bailout;
		}

		if (softc->zone_interface != DA_ZONE_IF_ATA_PASS) {
			scsi_zbc_in(&ccb->csio,
				    /*retries*/ da_retry_count,
				    /*cbcfnp*/ dadone,
				    /*tag_action*/ MSG_SIMPLE_Q_TAG,
				    /*service_action*/ ZBC_IN_SA_REPORT_ZONES,
				    /*zone_start_lba*/ rep->starting_id,
				    /*zone_options*/ rep->rep_options,
				    /*data_ptr*/ rz_ptr,
				    /*dxfer_len*/ alloc_size,
				    /*sense_len*/ SSD_FULL_SIZE,
				    /*timeout*/ da_default_timeout * 1000);
		} else {
			/*
			 * Note that in this case, even though we can
			 * technically use NCQ, we don't bother for several
			 * reasons:
			 * 1. It hasn't been tested on a SAT layer that
			 *    supports it.  This is new as of SAT-4.
			 * 2. Even when there is a SAT layer that supports
			 *    it, that SAT layer will also probably support
			 *    ZBC -> ZAC translation, since they are both
			 *    in the SAT-4 spec.
			 * 3. Translation will likely be preferable to ATA
			 *    passthrough.  LSI / Avago at least single
			 *    steps ATA passthrough commands in the HBA,
			 *    regardless of protocol, so unless that
			 *    changes, there is a performance penalty for
			 *    doing ATA passthrough no matter whether
			 *    you're using NCQ/FPDMA, DMA or PIO.
			 * 4. It requires a 32-byte CDB, which at least at
			 *    this point in CAM requires a CDB pointer, which
			 *    would require us to allocate an additional bit
			 *    of storage separate from the CCB.
			 */
			error = scsi_ata_zac_mgmt_in(&ccb->csio,
			    /*retries*/ da_retry_count,
			    /*cbcfnp*/ dadone,
			    /*tag_action*/ MSG_SIMPLE_Q_TAG,
			    /*use_ncq*/ 0,
			    /*zm_action*/ ATA_ZM_REPORT_ZONES,
			    /*zone_id*/ rep->starting_id,
			    /*zone_flags*/ rep->rep_options,
			    /*data_ptr*/ rz_ptr,
			    /*dxfer_len*/ alloc_size,
			    /*cdb_storage*/ NULL,
			    /*cdb_storage_len*/ 0,
			    /*sense_len*/ SSD_FULL_SIZE,
			    /*timeout*/ da_default_timeout * 1000);
			if (error != 0) {
				error = EINVAL;
				xpt_print(periph->path,
				    "scsi_ata_zac_mgmt_in() returned an "
				    "error!");
				goto bailout;
			}
		}

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
		case DA_ZONE_DRIVE_MANAGED:
			params->zone_mode = DISK_ZONE_MODE_DRIVE_MANAGED;
			break;
		case DA_ZONE_HOST_AWARE:
			params->zone_mode = DISK_ZONE_MODE_HOST_AWARE;
			break;
		case DA_ZONE_HOST_MANAGED:
			params->zone_mode = DISK_ZONE_MODE_HOST_MANAGED;
			break;
		default:
		case DA_ZONE_NONE:
			params->zone_mode = DISK_ZONE_MODE_NONE;
			break;
		}

		if (softc->zone_flags & DA_ZONE_FLAG_URSWRZ)
			params->flags |= DISK_ZONE_DISK_URSWRZ;

		if (softc->zone_flags & DA_ZONE_FLAG_OPT_SEQ_SET) {
			params->optimal_seq_zones = softc->optimal_seq_zones;
			params->flags |= DISK_ZONE_OPT_SEQ_SET;
		}

		if (softc->zone_flags & DA_ZONE_FLAG_OPT_NONSEQ_SET) {
			params->optimal_nonseq_zones =
			    softc->optimal_nonseq_zones;
			params->flags |= DISK_ZONE_OPT_NONSEQ_SET;
		}

		if (softc->zone_flags & DA_ZONE_FLAG_MAX_SEQ_SET) {
			params->max_seq_zones = softc->max_seq_zones;
			params->flags |= DISK_ZONE_MAX_SEQ_SET;
		}
		if (softc->zone_flags & DA_ZONE_FLAG_RZ_SUP)
			params->flags |= DISK_ZONE_RZ_SUP;

		if (softc->zone_flags & DA_ZONE_FLAG_OPEN_SUP)
			params->flags |= DISK_ZONE_OPEN_SUP;

		if (softc->zone_flags & DA_ZONE_FLAG_CLOSE_SUP)
			params->flags |= DISK_ZONE_CLOSE_SUP;

		if (softc->zone_flags & DA_ZONE_FLAG_FINISH_SUP)
			params->flags |= DISK_ZONE_FINISH_SUP;

		if (softc->zone_flags & DA_ZONE_FLAG_RWP_SUP)
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
dastart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct da_softc *softc;

	cam_periph_assert(periph, MA_OWNED);
	softc = (struct da_softc *)periph->softc;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dastart\n"));

skipstate:
	switch (softc->state) {
	case DA_STATE_NORMAL:
	{
		struct bio *bp;
		uint8_t tag_code;

more:
		bp = cam_iosched_next_bio(softc->cam_iosched);
		if (bp == NULL) {
			if (cam_iosched_has_work_flags(softc->cam_iosched,
			    DA_WORK_TUR)) {
				softc->flags |= DA_FLAG_TUR_PENDING;
				cam_iosched_clr_work_flags(softc->cam_iosched,
				    DA_WORK_TUR);
				scsi_test_unit_ready(&start_ccb->csio,
				     /*retries*/ da_retry_count,
				     dadone_tur,
				     MSG_SIMPLE_Q_TAG,
				     SSD_FULL_SIZE,
				     da_default_timeout * 1000);
				start_ccb->ccb_h.ccb_bp = NULL;
				start_ccb->ccb_h.ccb_state = DA_CCB_TUR;
				xpt_action(start_ccb);
			} else
				xpt_release_ccb(start_ccb);
			break;
		}

		if (bp->bio_cmd == BIO_DELETE) {
			if (softc->delete_func != NULL) {
				softc->delete_func(periph, start_ccb, bp);
				goto out;
			} else {
				/*
				 * Not sure this is possible, but failsafe by
				 * lying and saying "sure, done."
				 */
				biofinish(bp, NULL, 0);
				goto more;
			}
		}

		if (cam_iosched_has_work_flags(softc->cam_iosched,
		    DA_WORK_TUR)) {
			cam_iosched_clr_work_flags(softc->cam_iosched,
			    DA_WORK_TUR);
			da_periph_release_locked(periph, DA_REF_TUR);
		}

		if ((bp->bio_flags & BIO_ORDERED) != 0 ||
		    (softc->flags & DA_FLAG_NEED_OTAG) != 0) {
			softc->flags &= ~DA_FLAG_NEED_OTAG;
			softc->flags |= DA_FLAG_WAS_OTAG;
			tag_code = MSG_ORDERED_Q_TAG;
		} else {
			tag_code = MSG_SIMPLE_Q_TAG;
		}

		switch (bp->bio_cmd) {
		case BIO_WRITE:
		case BIO_READ:
		{
			void *data_ptr;
			int rw_op;

			biotrack(bp, __func__);

			if (bp->bio_cmd == BIO_WRITE) {
				softc->flags |= DA_FLAG_DIRTY;
				rw_op = SCSI_RW_WRITE;
			} else {
				rw_op = SCSI_RW_READ;
			}

			data_ptr = bp->bio_data;
			if ((bp->bio_flags & (BIO_UNMAPPED|BIO_VLIST)) != 0) {
				rw_op |= SCSI_RW_BIO;
				data_ptr = bp;
			}

			scsi_read_write(&start_ccb->csio,
					/*retries*/da_retry_count,
					/*cbfcnp*/dadone,
					/*tag_action*/tag_code,
					rw_op,
					/*byte2*/0,
					softc->minimum_cmd_size,
					/*lba*/bp->bio_pblkno,
					/*block_count*/bp->bio_bcount /
					softc->params.secsize,
					data_ptr,
					/*dxfer_len*/ bp->bio_bcount,
					/*sense_len*/SSD_FULL_SIZE,
					da_default_timeout * 1000);
#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
			start_ccb->csio.bio = bp;
#endif
			break;
		}
		case BIO_FLUSH:
			/*
			 * If we don't support sync cache, or the disk
			 * isn't dirty, FLUSH is a no-op.  Use the
			 * allocated CCB for the next bio if one is
			 * available.
			 */
			if ((softc->quirks & DA_Q_NO_SYNC_CACHE) != 0 ||
			    (softc->flags & DA_FLAG_DIRTY) == 0) {
				biodone(bp);
				goto skipstate;
			}

			/*
			 * BIO_FLUSH doesn't currently communicate
			 * range data, so we synchronize the cache
			 * over the whole disk.
			 */
			scsi_synchronize_cache(&start_ccb->csio,
					       /*retries*/1,
					       /*cbfcnp*/dadone,
					       /*tag_action*/tag_code,
					       /*begin_lba*/0,
					       /*lb_count*/0,
					       SSD_FULL_SIZE,
					       da_default_timeout*1000);
			/*
			 * Clear the dirty flag before sending the command.
			 * Either this sync cache will be successful, or it
			 * will fail after a retry.  If it fails, it is
			 * unlikely to be successful if retried later, so
			 * we'll save ourselves time by just marking the
			 * device clean.
			 */
			softc->flags &= ~DA_FLAG_DIRTY;
			break;
		case BIO_ZONE: {
			int error, queue_ccb;

			queue_ccb = 0;

			error = da_zone_cmd(periph, start_ccb, bp,&queue_ccb);
			if ((error != 0)
			 || (queue_ccb == 0)) {
				biofinish(bp, NULL, error);
				xpt_release_ccb(start_ccb);
				return;
			}
			break;
		}
		}
		start_ccb->ccb_h.ccb_state = DA_CCB_BUFFER_IO;
		start_ccb->ccb_h.flags |= CAM_UNLOCKED;
		start_ccb->ccb_h.softtimeout = sbttotv(da_default_softtimeout);

out:
		LIST_INSERT_HEAD(&softc->pending_ccbs,
				 &start_ccb->ccb_h, periph_links.le);

		/* We expect a unit attention from this device */
		if ((softc->flags & DA_FLAG_RETRY_UA) != 0) {
			start_ccb->ccb_h.ccb_state |= DA_CCB_RETRY_UA;
			softc->flags &= ~DA_FLAG_RETRY_UA;
		}

		start_ccb->ccb_h.ccb_bp = bp;
		softc->refcount++;
		cam_periph_unlock(periph);
		xpt_action(start_ccb);
		cam_periph_lock(periph);

		/* May have more work to do, so ensure we stay scheduled */
		daschedule(periph);
		break;
	}
	case DA_STATE_PROBE_WP:
	{
		void  *mode_buf;
		int    mode_buf_len;

		if (da_disable_wp_detection) {
			if ((softc->flags & DA_FLAG_CAN_RC16) != 0)
				softc->state = DA_STATE_PROBE_RC16;
			else
				softc->state = DA_STATE_PROBE_RC;
			goto skipstate;
		}
		mode_buf_len = 192;
		mode_buf = malloc(mode_buf_len, M_SCSIDA, M_NOWAIT);
		if (mode_buf == NULL) {
			xpt_print(periph->path, "Unable to send mode sense - "
			    "malloc failure\n");
			if ((softc->flags & DA_FLAG_CAN_RC16) != 0)
				softc->state = DA_STATE_PROBE_RC16;
			else
				softc->state = DA_STATE_PROBE_RC;
			goto skipstate;
		}
		scsi_mode_sense_len(&start_ccb->csio,
				    /*retries*/ da_retry_count,
				    /*cbfcnp*/ dadone_probewp,
				    /*tag_action*/ MSG_SIMPLE_Q_TAG,
				    /*dbd*/ FALSE,
				    /*pc*/ SMS_PAGE_CTRL_CURRENT,
				    /*page*/ SMS_ALL_PAGES_PAGE,
				    /*param_buf*/ mode_buf,
				    /*param_len*/ mode_buf_len,
				    /*minimum_cmd_size*/ softc->minimum_cmd_size,
				    /*sense_len*/ SSD_FULL_SIZE,
				    /*timeout*/ da_default_timeout * 1000);
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_WP;
		xpt_action(start_ccb);
		break;
	}
	case DA_STATE_PROBE_RC:
	{
		struct scsi_read_capacity_data *rcap;

		rcap = (struct scsi_read_capacity_data *)
		    malloc(sizeof(*rcap), M_SCSIDA, M_NOWAIT|M_ZERO);
		if (rcap == NULL) {
			printf("dastart: Couldn't malloc read_capacity data\n");
			/* da_free_periph??? */
			break;
		}
		scsi_read_capacity(&start_ccb->csio,
				   /*retries*/da_retry_count,
				   dadone_proberc,
				   MSG_SIMPLE_Q_TAG,
				   rcap,
				   SSD_FULL_SIZE,
				   /*timeout*/5000);
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_RC;
		xpt_action(start_ccb);
		break;
	}
	case DA_STATE_PROBE_RC16:
	{
		struct scsi_read_capacity_data_long *rcaplong;

		rcaplong = (struct scsi_read_capacity_data_long *)
			malloc(sizeof(*rcaplong), M_SCSIDA, M_NOWAIT|M_ZERO);
		if (rcaplong == NULL) {
			printf("dastart: Couldn't malloc read_capacity data\n");
			/* da_free_periph??? */
			break;
		}
		scsi_read_capacity_16(&start_ccb->csio,
				      /*retries*/ da_retry_count,
				      /*cbfcnp*/ dadone_proberc,
				      /*tag_action*/ MSG_SIMPLE_Q_TAG,
				      /*lba*/ 0,
				      /*reladr*/ 0,
				      /*pmi*/ 0,
				      /*rcap_buf*/ (uint8_t *)rcaplong,
				      /*rcap_buf_len*/ sizeof(*rcaplong),
				      /*sense_len*/ SSD_FULL_SIZE,
				      /*timeout*/ da_default_timeout * 1000);
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_RC16;
		xpt_action(start_ccb);
		break;
	}
	case DA_STATE_PROBE_LBP:
	{
		struct scsi_vpd_logical_block_prov *lbp;

		if (!scsi_vpd_supported_page(periph, SVPD_LBP)) {
			/*
			 * If we get here we don't support any SBC-3 delete
			 * methods with UNMAP as the Logical Block Provisioning
			 * VPD page support is required for devices which
			 * support it according to T10/1799-D Revision 31
			 * however older revisions of the spec don't mandate
			 * this so we currently don't remove these methods
			 * from the available set.
			 */
			softc->state = DA_STATE_PROBE_BLK_LIMITS;
			goto skipstate;
		}

		lbp = (struct scsi_vpd_logical_block_prov *)
			malloc(sizeof(*lbp), M_SCSIDA, M_NOWAIT|M_ZERO);

		if (lbp == NULL) {
			printf("dastart: Couldn't malloc lbp data\n");
			/* da_free_periph??? */
			break;
		}

		scsi_inquiry(&start_ccb->csio,
			     /*retries*/da_retry_count,
			     /*cbfcnp*/dadone_probelbp,
			     /*tag_action*/MSG_SIMPLE_Q_TAG,
			     /*inq_buf*/(u_int8_t *)lbp,
			     /*inq_len*/sizeof(*lbp),
			     /*evpd*/TRUE,
			     /*page_code*/SVPD_LBP,
			     /*sense_len*/SSD_MIN_SIZE,
			     /*timeout*/da_default_timeout * 1000);
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_LBP;
		xpt_action(start_ccb);
		break;
	}
	case DA_STATE_PROBE_BLK_LIMITS:
	{
		struct scsi_vpd_block_limits *block_limits;

		if (!scsi_vpd_supported_page(periph, SVPD_BLOCK_LIMITS)) {
			/* Not supported skip to next probe */
			softc->state = DA_STATE_PROBE_BDC;
			goto skipstate;
		}

		block_limits = (struct scsi_vpd_block_limits *)
			malloc(sizeof(*block_limits), M_SCSIDA, M_NOWAIT|M_ZERO);

		if (block_limits == NULL) {
			printf("dastart: Couldn't malloc block_limits data\n");
			/* da_free_periph??? */
			break;
		}

		scsi_inquiry(&start_ccb->csio,
			     /*retries*/da_retry_count,
			     /*cbfcnp*/dadone_probeblklimits,
			     /*tag_action*/MSG_SIMPLE_Q_TAG,
			     /*inq_buf*/(u_int8_t *)block_limits,
			     /*inq_len*/sizeof(*block_limits),
			     /*evpd*/TRUE,
			     /*page_code*/SVPD_BLOCK_LIMITS,
			     /*sense_len*/SSD_MIN_SIZE,
			     /*timeout*/da_default_timeout * 1000);
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_BLK_LIMITS;
		xpt_action(start_ccb);
		break;
	}
	case DA_STATE_PROBE_BDC:
	{
		struct scsi_vpd_block_characteristics *bdc;

		if (!scsi_vpd_supported_page(periph, SVPD_BDC)) {
			softc->state = DA_STATE_PROBE_ATA;
			goto skipstate;
		}

		bdc = (struct scsi_vpd_block_characteristics *)
			malloc(sizeof(*bdc), M_SCSIDA, M_NOWAIT|M_ZERO);

		if (bdc == NULL) {
			printf("dastart: Couldn't malloc bdc data\n");
			/* da_free_periph??? */
			break;
		}

		scsi_inquiry(&start_ccb->csio,
			     /*retries*/da_retry_count,
			     /*cbfcnp*/dadone_probebdc,
			     /*tag_action*/MSG_SIMPLE_Q_TAG,
			     /*inq_buf*/(u_int8_t *)bdc,
			     /*inq_len*/sizeof(*bdc),
			     /*evpd*/TRUE,
			     /*page_code*/SVPD_BDC,
			     /*sense_len*/SSD_MIN_SIZE,
			     /*timeout*/da_default_timeout * 1000);
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_BDC;
		xpt_action(start_ccb);
		break;
	}
	case DA_STATE_PROBE_ATA:
	{
		struct ata_params *ata_params;

		if (!scsi_vpd_supported_page(periph, SVPD_ATA_INFORMATION)) {
			if ((softc->zone_mode == DA_ZONE_HOST_AWARE)
			 || (softc->zone_mode == DA_ZONE_HOST_MANAGED)) {
				/*
				 * Note that if the ATA VPD page isn't
				 * supported, we aren't talking to an ATA
				 * device anyway.  Support for that VPD
				 * page is mandatory for SCSI to ATA (SAT)
				 * translation layers.
				 */
				softc->state = DA_STATE_PROBE_ZONE;
				goto skipstate;
			}
			daprobedone(periph, start_ccb);
			break;
		}

		ata_params = (struct ata_params*)
			malloc(sizeof(*ata_params), M_SCSIDA,M_NOWAIT|M_ZERO);

		if (ata_params == NULL) {
			xpt_print(periph->path, "Couldn't malloc ata_params "
			    "data\n");
			/* da_free_periph??? */
			break;
		}

		scsi_ata_identify(&start_ccb->csio,
				  /*retries*/da_retry_count,
				  /*cbfcnp*/dadone_probeata,
                                  /*tag_action*/MSG_SIMPLE_Q_TAG,
				  /*data_ptr*/(u_int8_t *)ata_params,
				  /*dxfer_len*/sizeof(*ata_params),
				  /*sense_len*/SSD_FULL_SIZE,
				  /*timeout*/da_default_timeout * 1000);
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_ATA;
		xpt_action(start_ccb);
		break;
	}
	case DA_STATE_PROBE_ATA_LOGDIR:
	{
		struct ata_gp_log_dir *log_dir;
		int retval;

		retval = 0;

		if ((softc->flags & DA_FLAG_CAN_ATA_LOG) == 0) {
			/*
			 * If we don't have log support, not much point in
			 * trying to probe zone support.
			 */
			daprobedone(periph, start_ccb);
			break;
		}

		/*
		 * If we have an ATA device (the SCSI ATA Information VPD
		 * page should be present and the ATA identify should have
		 * succeeded) and it supports logs, ask for the log directory.
		 */

		log_dir = malloc(sizeof(*log_dir), M_SCSIDA, M_NOWAIT|M_ZERO);
		if (log_dir == NULL) {
			xpt_print(periph->path, "Couldn't malloc log_dir "
			    "data\n");
			daprobedone(periph, start_ccb);
			break;
		}

		retval = scsi_ata_read_log(&start_ccb->csio,
		    /*retries*/ da_retry_count,
		    /*cbfcnp*/ dadone_probeatalogdir,
		    /*tag_action*/ MSG_SIMPLE_Q_TAG,
		    /*log_address*/ ATA_LOG_DIRECTORY,
		    /*page_number*/ 0,
		    /*block_count*/ 1,
		    /*protocol*/ softc->flags & DA_FLAG_CAN_ATA_DMA ?
				 AP_PROTO_DMA : AP_PROTO_PIO_IN,
		    /*data_ptr*/ (uint8_t *)log_dir,
		    /*dxfer_len*/ sizeof(*log_dir),
		    /*sense_len*/ SSD_FULL_SIZE,
		    /*timeout*/ da_default_timeout * 1000);

		if (retval != 0) {
			xpt_print(periph->path, "scsi_ata_read_log() failed!");
			free(log_dir, M_SCSIDA);
			daprobedone(periph, start_ccb);
			break;
		}
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_ATA_LOGDIR;
		xpt_action(start_ccb);
		break;
	}
	case DA_STATE_PROBE_ATA_IDDIR:
	{
		struct ata_identify_log_pages *id_dir;
		int retval;

		retval = 0;

		/*
		 * Check here to see whether the Identify Device log is
		 * supported in the directory of logs.  If so, continue
		 * with requesting the log of identify device pages.
		 */
		if ((softc->flags & DA_FLAG_CAN_ATA_IDLOG) == 0) {
			daprobedone(periph, start_ccb);
			break;
		}

		id_dir = malloc(sizeof(*id_dir), M_SCSIDA, M_NOWAIT | M_ZERO);
		if (id_dir == NULL) {
			xpt_print(periph->path, "Couldn't malloc id_dir "
			    "data\n");
			daprobedone(periph, start_ccb);
			break;
		}

		retval = scsi_ata_read_log(&start_ccb->csio,
		    /*retries*/ da_retry_count,
		    /*cbfcnp*/ dadone_probeataiddir,
		    /*tag_action*/ MSG_SIMPLE_Q_TAG,
		    /*log_address*/ ATA_IDENTIFY_DATA_LOG,
		    /*page_number*/ ATA_IDL_PAGE_LIST,
		    /*block_count*/ 1,
		    /*protocol*/ softc->flags & DA_FLAG_CAN_ATA_DMA ?
				 AP_PROTO_DMA : AP_PROTO_PIO_IN,
		    /*data_ptr*/ (uint8_t *)id_dir,
		    /*dxfer_len*/ sizeof(*id_dir),
		    /*sense_len*/ SSD_FULL_SIZE,
		    /*timeout*/ da_default_timeout * 1000);

		if (retval != 0) {
			xpt_print(periph->path, "scsi_ata_read_log() failed!");
			free(id_dir, M_SCSIDA);
			daprobedone(periph, start_ccb);
			break;
		}
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_ATA_IDDIR;
		xpt_action(start_ccb);
		break;
	}
	case DA_STATE_PROBE_ATA_SUP:
	{
		struct ata_identify_log_sup_cap *sup_cap;
		int retval;

		retval = 0;

		/*
		 * Check here to see whether the Supported Capabilities log
		 * is in the list of Identify Device logs.
		 */
		if ((softc->flags & DA_FLAG_CAN_ATA_SUPCAP) == 0) {
			daprobedone(periph, start_ccb);
			break;
		}

		sup_cap = malloc(sizeof(*sup_cap), M_SCSIDA, M_NOWAIT|M_ZERO);
		if (sup_cap == NULL) {
			xpt_print(periph->path, "Couldn't malloc sup_cap "
			    "data\n");
			daprobedone(periph, start_ccb);
			break;
		}

		retval = scsi_ata_read_log(&start_ccb->csio,
		    /*retries*/ da_retry_count,
		    /*cbfcnp*/ dadone_probeatasup,
		    /*tag_action*/ MSG_SIMPLE_Q_TAG,
		    /*log_address*/ ATA_IDENTIFY_DATA_LOG,
		    /*page_number*/ ATA_IDL_SUP_CAP,
		    /*block_count*/ 1,
		    /*protocol*/ softc->flags & DA_FLAG_CAN_ATA_DMA ?
				 AP_PROTO_DMA : AP_PROTO_PIO_IN,
		    /*data_ptr*/ (uint8_t *)sup_cap,
		    /*dxfer_len*/ sizeof(*sup_cap),
		    /*sense_len*/ SSD_FULL_SIZE,
		    /*timeout*/ da_default_timeout * 1000);

		if (retval != 0) {
			xpt_print(periph->path, "scsi_ata_read_log() failed!");
			free(sup_cap, M_SCSIDA);
			daprobedone(periph, start_ccb);
			break;

		}

		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_ATA_SUP;
		xpt_action(start_ccb);
		break;
	}
	case DA_STATE_PROBE_ATA_ZONE:
	{
		struct ata_zoned_info_log *ata_zone;
		int retval;

		retval = 0;

		/*
		 * Check here to see whether the zoned device information
		 * page is supported.  If so, continue on to request it.
		 * If not, skip to DA_STATE_PROBE_LOG or done.
		 */
		if ((softc->flags & DA_FLAG_CAN_ATA_ZONE) == 0) {
			daprobedone(periph, start_ccb);
			break;
		}
		ata_zone = malloc(sizeof(*ata_zone), M_SCSIDA,
				  M_NOWAIT|M_ZERO);
		if (ata_zone == NULL) {
			xpt_print(periph->path, "Couldn't malloc ata_zone "
			    "data\n");
			daprobedone(periph, start_ccb);
			break;
		}

		retval = scsi_ata_read_log(&start_ccb->csio,
		    /*retries*/ da_retry_count,
		    /*cbfcnp*/ dadone_probeatazone,
		    /*tag_action*/ MSG_SIMPLE_Q_TAG,
		    /*log_address*/ ATA_IDENTIFY_DATA_LOG,
		    /*page_number*/ ATA_IDL_ZDI,
		    /*block_count*/ 1,
		    /*protocol*/ softc->flags & DA_FLAG_CAN_ATA_DMA ?
				 AP_PROTO_DMA : AP_PROTO_PIO_IN,
		    /*data_ptr*/ (uint8_t *)ata_zone,
		    /*dxfer_len*/ sizeof(*ata_zone),
		    /*sense_len*/ SSD_FULL_SIZE,
		    /*timeout*/ da_default_timeout * 1000);

		if (retval != 0) {
			xpt_print(periph->path, "scsi_ata_read_log() failed!");
			free(ata_zone, M_SCSIDA);
			daprobedone(periph, start_ccb);
			break;
		}
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_ATA_ZONE;
		xpt_action(start_ccb);

		break;
	}
	case DA_STATE_PROBE_ZONE:
	{
		struct scsi_vpd_zoned_bdc *bdc;

		/*
		 * Note that this page will be supported for SCSI protocol
		 * devices that support ZBC (SMR devices), as well as ATA
		 * protocol devices that are behind a SAT (SCSI to ATA
		 * Translation) layer that supports converting ZBC commands
		 * to their ZAC equivalents.
		 */
		if (!scsi_vpd_supported_page(periph, SVPD_ZONED_BDC)) {
			daprobedone(periph, start_ccb);
			break;
		}
		bdc = (struct scsi_vpd_zoned_bdc *)
			malloc(sizeof(*bdc), M_SCSIDA, M_NOWAIT|M_ZERO);

		if (bdc == NULL) {
			xpt_release_ccb(start_ccb);
			xpt_print(periph->path, "Couldn't malloc zone VPD "
			    "data\n");
			break;
		}
		scsi_inquiry(&start_ccb->csio,
			     /*retries*/da_retry_count,
			     /*cbfcnp*/dadone_probezone,
			     /*tag_action*/MSG_SIMPLE_Q_TAG,
			     /*inq_buf*/(u_int8_t *)bdc,
			     /*inq_len*/sizeof(*bdc),
			     /*evpd*/TRUE,
			     /*page_code*/SVPD_ZONED_BDC,
			     /*sense_len*/SSD_FULL_SIZE,
			     /*timeout*/da_default_timeout * 1000);
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = DA_CCB_PROBE_ZONE;
		xpt_action(start_ccb);
		break;
	}
	}
}

/*
 * In each of the methods below, while its the caller's
 * responsibility to ensure the request will fit into a
 * single device request, we might have changed the delete
 * method due to the device incorrectly advertising either
 * its supported methods or limits.
 *
 * To prevent this causing further issues we validate the
 * against the methods limits, and warn which would
 * otherwise be unnecessary.
 */
static void
da_delete_unmap(struct cam_periph *periph, union ccb *ccb, struct bio *bp)
{
	struct da_softc *softc = (struct da_softc *)periph->softc;;
	struct bio *bp1;
	uint8_t *buf = softc->unmap_buf;
	struct scsi_unmap_desc *d = (void *)&buf[UNMAP_HEAD_SIZE];
	uint64_t lba, lastlba = (uint64_t)-1;
	uint64_t totalcount = 0;
	uint64_t count;
	uint32_t c, lastcount = 0, ranges = 0;

	/*
	 * Currently this doesn't take the UNMAP
	 * Granularity and Granularity Alignment
	 * fields into account.
	 *
	 * This could result in both unoptimal unmap
	 * requests as as well as UNMAP calls unmapping
	 * fewer LBA's than requested.
	 */

	bzero(softc->unmap_buf, sizeof(softc->unmap_buf));
	bp1 = bp;
	do {
		/*
		 * Note: ada and da are different in how they store the
		 * pending bp's in a trim. ada stores all of them in the
		 * trim_req.bps. da stores all but the first one in the
		 * delete_run_queue. ada then completes all the bps in
		 * its adadone() loop. da completes all the bps in the
		 * delete_run_queue in dadone, and relies on the biodone
		 * after to complete. This should be reconciled since there's
		 * no real reason to do it differently. XXX
		 */
		if (bp1 != bp)
			bioq_insert_tail(&softc->delete_run_queue, bp1);
		lba = bp1->bio_pblkno;
		count = bp1->bio_bcount / softc->params.secsize;

		/* Try to extend the previous range. */
		if (lba == lastlba) {
			c = omin(count, UNMAP_RANGE_MAX - lastcount);
			lastlba += c;
			lastcount += c;
			scsi_ulto4b(lastcount, d[ranges - 1].length);
			count -= c;
			lba += c;
			totalcount += c;
		} else if ((softc->quirks & DA_Q_STRICT_UNMAP) &&
		    softc->unmap_gran != 0) {
			/* Align length of the previous range. */
			if ((c = lastcount % softc->unmap_gran) != 0) {
				if (lastcount <= c) {
					totalcount -= lastcount;
					lastlba = (uint64_t)-1;
					lastcount = 0;
					ranges--;
				} else {
					totalcount -= c;
					lastlba -= c;
					lastcount -= c;
					scsi_ulto4b(lastcount,
					    d[ranges - 1].length);
				}
			}
			/* Align beginning of the new range. */
			c = (lba - softc->unmap_gran_align) % softc->unmap_gran;
			if (c != 0) {
				c = softc->unmap_gran - c;
				if (count <= c) {
					count = 0;
				} else {
					lba += c;
					count -= c;
				}
			}
		}

		while (count > 0) {
			c = omin(count, UNMAP_RANGE_MAX);
			if (totalcount + c > softc->unmap_max_lba ||
			    ranges >= softc->unmap_max_ranges) {
				xpt_print(periph->path,
				    "%s issuing short delete %ld > %ld"
				    "|| %d >= %d",
				    da_delete_method_desc[softc->delete_method],
				    totalcount + c, softc->unmap_max_lba,
				    ranges, softc->unmap_max_ranges);
				break;
			}
			scsi_u64to8b(lba, d[ranges].lba);
			scsi_ulto4b(c, d[ranges].length);
			lba += c;
			totalcount += c;
			ranges++;
			count -= c;
			lastlba = lba;
			lastcount = c;
		}
		bp1 = cam_iosched_next_trim(softc->cam_iosched);
		if (bp1 == NULL)
			break;
		if (ranges >= softc->unmap_max_ranges ||
		    totalcount + bp1->bio_bcount /
		    softc->params.secsize > softc->unmap_max_lba) {
			cam_iosched_put_back_trim(softc->cam_iosched, bp1);
			break;
		}
	} while (1);

	/* Align length of the last range. */
	if ((softc->quirks & DA_Q_STRICT_UNMAP) && softc->unmap_gran != 0 &&
	    (c = lastcount % softc->unmap_gran) != 0) {
		if (lastcount <= c)
			ranges--;
		else
			scsi_ulto4b(lastcount - c, d[ranges - 1].length);
	}

	scsi_ulto2b(ranges * 16 + 6, &buf[0]);
	scsi_ulto2b(ranges * 16, &buf[2]);

	scsi_unmap(&ccb->csio,
		   /*retries*/da_retry_count,
		   /*cbfcnp*/dadone,
		   /*tag_action*/MSG_SIMPLE_Q_TAG,
		   /*byte2*/0,
		   /*data_ptr*/ buf,
		   /*dxfer_len*/ ranges * 16 + 8,
		   /*sense_len*/SSD_FULL_SIZE,
		   da_default_timeout * 1000);
	ccb->ccb_h.ccb_state = DA_CCB_DELETE;
	ccb->ccb_h.flags |= CAM_UNLOCKED;
	softc->trim_count++;
	softc->trim_ranges += ranges;
	softc->trim_lbas += totalcount;
	cam_iosched_submit_trim(softc->cam_iosched);
}

static void
da_delete_trim(struct cam_periph *periph, union ccb *ccb, struct bio *bp)
{
	struct da_softc *softc = (struct da_softc *)periph->softc;
	struct bio *bp1;
	uint8_t *buf = softc->unmap_buf;
	uint64_t lastlba = (uint64_t)-1;
	uint64_t count;
	uint64_t lba;
	uint32_t lastcount = 0, c, requestcount;
	int ranges = 0, off, block_count;

	bzero(softc->unmap_buf, sizeof(softc->unmap_buf));
	bp1 = bp;
	do {
		if (bp1 != bp)//XXX imp XXX
			bioq_insert_tail(&softc->delete_run_queue, bp1);
		lba = bp1->bio_pblkno;
		count = bp1->bio_bcount / softc->params.secsize;
		requestcount = count;

		/* Try to extend the previous range. */
		if (lba == lastlba) {
			c = omin(count, ATA_DSM_RANGE_MAX - lastcount);
			lastcount += c;
			off = (ranges - 1) * 8;
			buf[off + 6] = lastcount & 0xff;
			buf[off + 7] = (lastcount >> 8) & 0xff;
			count -= c;
			lba += c;
		}

		while (count > 0) {
			c = omin(count, ATA_DSM_RANGE_MAX);
			off = ranges * 8;

			buf[off + 0] = lba & 0xff;
			buf[off + 1] = (lba >> 8) & 0xff;
			buf[off + 2] = (lba >> 16) & 0xff;
			buf[off + 3] = (lba >> 24) & 0xff;
			buf[off + 4] = (lba >> 32) & 0xff;
			buf[off + 5] = (lba >> 40) & 0xff;
			buf[off + 6] = c & 0xff;
			buf[off + 7] = (c >> 8) & 0xff;
			lba += c;
			ranges++;
			count -= c;
			lastcount = c;
			if (count != 0 && ranges == softc->trim_max_ranges) {
				xpt_print(periph->path,
				    "%s issuing short delete %ld > %ld\n",
				    da_delete_method_desc[softc->delete_method],
				    requestcount,
				    (softc->trim_max_ranges - ranges) *
				    ATA_DSM_RANGE_MAX);
				break;
			}
		}
		lastlba = lba;
		bp1 = cam_iosched_next_trim(softc->cam_iosched);
		if (bp1 == NULL)
			break;
		if (bp1->bio_bcount / softc->params.secsize >
		    (softc->trim_max_ranges - ranges) * ATA_DSM_RANGE_MAX) {
			cam_iosched_put_back_trim(softc->cam_iosched, bp1);
			break;
		}
	} while (1);

	block_count = howmany(ranges, ATA_DSM_BLK_RANGES);
	scsi_ata_trim(&ccb->csio,
		      /*retries*/da_retry_count,
		      /*cbfcnp*/dadone,
		      /*tag_action*/MSG_SIMPLE_Q_TAG,
		      block_count,
		      /*data_ptr*/buf,
		      /*dxfer_len*/block_count * ATA_DSM_BLK_SIZE,
		      /*sense_len*/SSD_FULL_SIZE,
		      da_default_timeout * 1000);
	ccb->ccb_h.ccb_state = DA_CCB_DELETE;
	ccb->ccb_h.flags |= CAM_UNLOCKED;
	cam_iosched_submit_trim(softc->cam_iosched);
}

/*
 * We calculate ws_max_blks here based off d_delmaxsize instead
 * of using softc->ws_max_blks as it is absolute max for the
 * device not the protocol max which may well be lower.
 */
static void
da_delete_ws(struct cam_periph *periph, union ccb *ccb, struct bio *bp)
{
	struct da_softc *softc;
	struct bio *bp1;
	uint64_t ws_max_blks;
	uint64_t lba;
	uint64_t count; /* forward compat with WS32 */

	softc = (struct da_softc *)periph->softc;
	ws_max_blks = softc->disk->d_delmaxsize / softc->params.secsize;
	lba = bp->bio_pblkno;
	count = 0;
	bp1 = bp;
	do {
		if (bp1 != bp)//XXX imp XXX
			bioq_insert_tail(&softc->delete_run_queue, bp1);
		count += bp1->bio_bcount / softc->params.secsize;
		if (count > ws_max_blks) {
			xpt_print(periph->path,
			    "%s issuing short delete %ld > %ld\n",
			    da_delete_method_desc[softc->delete_method],
			    count, ws_max_blks);
			count = omin(count, ws_max_blks);
			break;
		}
		bp1 = cam_iosched_next_trim(softc->cam_iosched);
		if (bp1 == NULL)
			break;
		if (lba + count != bp1->bio_pblkno ||
		    count + bp1->bio_bcount /
		    softc->params.secsize > ws_max_blks) {
			cam_iosched_put_back_trim(softc->cam_iosched, bp1);
			break;
		}
	} while (1);

	scsi_write_same(&ccb->csio,
			/*retries*/da_retry_count,
			/*cbfcnp*/dadone,
			/*tag_action*/MSG_SIMPLE_Q_TAG,
			/*byte2*/softc->delete_method ==
			    DA_DELETE_ZERO ? 0 : SWS_UNMAP,
			softc->delete_method == DA_DELETE_WS16 ? 16 : 10,
			/*lba*/lba,
			/*block_count*/count,
			/*data_ptr*/ __DECONST(void *, zero_region),
			/*dxfer_len*/ softc->params.secsize,
			/*sense_len*/SSD_FULL_SIZE,
			da_default_timeout * 1000);
	ccb->ccb_h.ccb_state = DA_CCB_DELETE;
	ccb->ccb_h.flags |= CAM_UNLOCKED;
	cam_iosched_submit_trim(softc->cam_iosched);
}

static int
cmd6workaround(union ccb *ccb)
{
	struct scsi_rw_6 cmd6;
	struct scsi_rw_10 *cmd10;
	struct da_softc *softc;
	u_int8_t *cdb;
	struct bio *bp;
	int frozen;

	cdb = ccb->csio.cdb_io.cdb_bytes;
	softc = (struct da_softc *)xpt_path_periph(ccb->ccb_h.path)->softc;

	if (ccb->ccb_h.ccb_state == DA_CCB_DELETE) {
		da_delete_methods old_method = softc->delete_method;

		/*
		 * Typically there are two reasons for failure here
		 * 1. Delete method was detected as supported but isn't
		 * 2. Delete failed due to invalid params e.g. too big
		 *
		 * While we will attempt to choose an alternative delete method
		 * this may result in short deletes if the existing delete
		 * requests from geom are big for the new method chosen.
		 *
		 * This method assumes that the error which triggered this
		 * will not retry the io otherwise a panic will occur
		 */
		dadeleteflag(softc, old_method, 0);
		dadeletemethodchoose(softc, DA_DELETE_DISABLE);
		if (softc->delete_method == DA_DELETE_DISABLE)
			xpt_print(ccb->ccb_h.path,
				  "%s failed, disabling BIO_DELETE\n",
				  da_delete_method_desc[old_method]);
		else
			xpt_print(ccb->ccb_h.path,
				  "%s failed, switching to %s BIO_DELETE\n",
				  da_delete_method_desc[old_method],
				  da_delete_method_desc[softc->delete_method]);

		while ((bp = bioq_takefirst(&softc->delete_run_queue)) != NULL)
			cam_iosched_queue_work(softc->cam_iosched, bp);
		cam_iosched_queue_work(softc->cam_iosched,
		    (struct bio *)ccb->ccb_h.ccb_bp);
		ccb->ccb_h.ccb_bp = NULL;
		return (0);
	}

	/* Detect unsupported PREVENT ALLOW MEDIUM REMOVAL. */
	if ((ccb->ccb_h.flags & CAM_CDB_POINTER) == 0 &&
	    (*cdb == PREVENT_ALLOW) &&
	    (softc->quirks & DA_Q_NO_PREVENT) == 0) {
		if (bootverbose)
			xpt_print(ccb->ccb_h.path,
			    "PREVENT ALLOW MEDIUM REMOVAL not supported.\n");
		softc->quirks |= DA_Q_NO_PREVENT;
		return (0);
	}

	/* Detect unsupported SYNCHRONIZE CACHE(10). */
	if ((ccb->ccb_h.flags & CAM_CDB_POINTER) == 0 &&
	    (*cdb == SYNCHRONIZE_CACHE) &&
	    (softc->quirks & DA_Q_NO_SYNC_CACHE) == 0) {
		if (bootverbose)
			xpt_print(ccb->ccb_h.path,
			    "SYNCHRONIZE CACHE(10) not supported.\n");
		softc->quirks |= DA_Q_NO_SYNC_CACHE;
		softc->disk->d_flags &= ~DISKFLAG_CANFLUSHCACHE;
		return (0);
	}

	/* Translation only possible if CDB is an array and cmd is R/W6 */
	if ((ccb->ccb_h.flags & CAM_CDB_POINTER) != 0 ||
	    (*cdb != READ_6 && *cdb != WRITE_6))
		return 0;

	xpt_print(ccb->ccb_h.path, "READ(6)/WRITE(6) not supported, "
	    "increasing minimum_cmd_size to 10.\n");
	softc->minimum_cmd_size = 10;

	bcopy(cdb, &cmd6, sizeof(struct scsi_rw_6));
	cmd10 = (struct scsi_rw_10 *)cdb;
	cmd10->opcode = (cmd6.opcode == READ_6) ? READ_10 : WRITE_10;
	cmd10->byte2 = 0;
	scsi_ulto4b(scsi_3btoul(cmd6.addr), cmd10->addr);
	cmd10->reserved = 0;
	scsi_ulto2b(cmd6.length, cmd10->length);
	cmd10->control = cmd6.control;
	ccb->csio.cdb_len = sizeof(*cmd10);

	/* Requeue request, unfreezing queue if necessary */
	frozen = (ccb->ccb_h.status & CAM_DEV_QFRZN) != 0;
	ccb->ccb_h.status = CAM_REQUEUE_REQ;
	xpt_action(ccb);
	if (frozen) {
		cam_release_devq(ccb->ccb_h.path,
				 /*relsim_flags*/0,
				 /*reduction*/0,
				 /*timeout*/0,
				 /*getcount_only*/0);
	}
	return (ERESTART);
}

static void
dazonedone(struct cam_periph *periph, union ccb *ccb)
{
	struct da_softc *softc;
	struct bio *bp;

	softc = periph->softc;
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
		int ata;

		rep = &bp->bio_zone.zone_params.report;
		avail_len = ccb->csio.dxfer_len - ccb->csio.resid;
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
		bp->bio_resid = ccb->csio.resid;
		hdr = (struct scsi_report_zones_hdr *)ccb->csio.data_ptr;
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

		if (softc->zone_interface == DA_ZONE_IF_ATA_PASS)
			ata = 1;
		else
			ata = 0;

		hdr_len = ata ? le32dec(hdr->length) :
				scsi_4btoul(hdr->length);
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
		rep->header.maximum_lba = ata ?  le64dec(hdr->maximum_lba) :
					  scsi_8btou64(hdr->maximum_lba);
		/*
		 * If the drive reports no entries that match the query,
		 * we're done.
		 */
		if (hdr_len == 0) {
			rep->entries_filled = 0;
			break;
		}

		num_avail = min((avail_len - sizeof(*hdr)) / sizeof(*desc),
				hdr_len / sizeof(*desc));
		/*
		 * If the drive didn't return any data, then we're done.
		 */
		if (num_avail == 0) {
			rep->entries_filled = 0;
			break;
		}

		num_to_fill = min(num_avail, rep->entries_allocated);
		/*
		 * If the user didn't allocate any entries for us to fill,
		 * we're done.
		 */
		if (num_to_fill == 0) {
			rep->entries_filled = 0;
			break;
		}

		for (i = 0, desc = &hdr->desc_list[0], entry=&rep->entries[0];
		     i < num_to_fill; i++, desc++, entry++) {
			/*
			 * NOTE: we're mapping the values here directly
			 * from the SCSI/ATA bit definitions to the bio.h
			 * definitons.  There is also a warning in
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
			entry->zone_length =
			    ata ? le64dec(desc->zone_length) :
				  scsi_8btou64(desc->zone_length);
			entry->zone_start_lba =
			    ata ? le64dec(desc->zone_start_lba) :
				  scsi_8btou64(desc->zone_start_lba);
			entry->write_pointer_lba =
			    ata ? le64dec(desc->write_pointer_lba) :
				  scsi_8btou64(desc->write_pointer_lba);
		}
		rep->entries_filled = num_to_fill;
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
		free(ccb->csio.data_ptr, M_SCSIDA);
}

static void
dadone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct bio *bp, *bp1;
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	u_int32_t  priority;
	da_ccb_state state;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone\n"));

	softc = (struct da_softc *)periph->softc;
	priority = done_ccb->ccb_h.pinfo.priority;
	csio = &done_ccb->csio;

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	if (csio->bio != NULL)
		biotrack(csio->bio, __func__);
#endif
	state = csio->ccb_h.ccb_state & DA_CCB_TYPE_MASK;

	cam_periph_lock(periph);
	bp = (struct bio *)done_ccb->ccb_h.ccb_bp;
	if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		int error;
		int sf;

		if ((csio->ccb_h.ccb_state & DA_CCB_RETRY_UA) != 0)
			sf = SF_RETRY_UA;
		else
			sf = 0;

		error = daerror(done_ccb, CAM_RETRY_SELTO, sf);
		if (error == ERESTART) {
			/* A retry was scheduled, so just return. */
			cam_periph_unlock(periph);
			return;
		}
		bp = (struct bio *)done_ccb->ccb_h.ccb_bp;
		if (error != 0) {
			int queued_error;

			/*
			 * return all queued I/O with EIO, so that
			 * the client can retry these I/Os in the
			 * proper order should it attempt to recover.
			 */
			queued_error = EIO;

			if (error == ENXIO
			 && (softc->flags & DA_FLAG_PACK_INVALID)== 0) {
				/*
				 * Catastrophic error.  Mark our pack as
				 * invalid.
				 *
				 * XXX See if this is really a media
				 * XXX change first?
				 */
				xpt_print(periph->path, "Invalidating pack\n");
				softc->flags |= DA_FLAG_PACK_INVALID;
#ifdef CAM_IO_STATS
				softc->invalidations++;
#endif
				queued_error = ENXIO;
			}
			cam_iosched_flush(softc->cam_iosched, NULL,
			   queued_error);
			if (bp != NULL) {
				bp->bio_error = error;
				bp->bio_resid = bp->bio_bcount;
				bp->bio_flags |= BIO_ERROR;
			}
		} else if (bp != NULL) {
			if (state == DA_CCB_DELETE)
				bp->bio_resid = 0;
			else
				bp->bio_resid = csio->resid;
			bp->bio_error = 0;
			if (bp->bio_resid != 0)
				bp->bio_flags |= BIO_ERROR;
		}
		if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
			cam_release_devq(done_ccb->ccb_h.path,
					 /*relsim_flags*/0,
					 /*reduction*/0,
					 /*timeout*/0,
					 /*getcount_only*/0);
	} else if (bp != NULL) {
		if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
			panic("REQ_CMP with QFRZN");
		if (bp->bio_cmd == BIO_ZONE)
			dazonedone(periph, done_ccb);
		else if (state == DA_CCB_DELETE)
			bp->bio_resid = 0;
		else
			bp->bio_resid = csio->resid;
		if ((csio->resid > 0) && (bp->bio_cmd != BIO_ZONE))
			bp->bio_flags |= BIO_ERROR;
		if (softc->error_inject != 0) {
			bp->bio_error = softc->error_inject;
			bp->bio_resid = bp->bio_bcount;
			bp->bio_flags |= BIO_ERROR;
			softc->error_inject = 0;
		}
	}

	if (bp != NULL)
		biotrack(bp, __func__);
	LIST_REMOVE(&done_ccb->ccb_h, periph_links.le);
	if (LIST_EMPTY(&softc->pending_ccbs))
		softc->flags |= DA_FLAG_WAS_OTAG;

	/*
	 * We need to call cam_iosched before we call biodone so that we don't
	 * measure any activity that happens in the completion routine, which in
	 * the case of sendfile can be quite extensive. Release the periph
	 * refcount taken in dastart() for each CCB.
	 */
	cam_iosched_bio_complete(softc->cam_iosched, bp, done_ccb);
	xpt_release_ccb(done_ccb);
	KASSERT(softc->refcount >= 1, ("dadone softc %p refcount %d", softc, softc->refcount));
	softc->refcount--;
	if (state == DA_CCB_DELETE) {
		TAILQ_HEAD(, bio) queue;

		TAILQ_INIT(&queue);
		TAILQ_CONCAT(&queue, &softc->delete_run_queue.queue, bio_queue);
		softc->delete_run_queue.insert_point = NULL;
		/*
		 * Normally, the xpt_release_ccb() above would make sure
		 * that when we have more work to do, that work would
		 * get kicked off. However, we specifically keep
		 * delete_running set to 0 before the call above to
		 * allow other I/O to progress when many BIO_DELETE
		 * requests are pushed down. We set delete_running to 0
		 * and call daschedule again so that we don't stall if
		 * there are no other I/Os pending apart from BIO_DELETEs.
		 */
		cam_iosched_trim_done(softc->cam_iosched);
		daschedule(periph);
		cam_periph_unlock(periph);
		while ((bp1 = TAILQ_FIRST(&queue)) != NULL) {
			TAILQ_REMOVE(&queue, bp1, bio_queue);
			bp1->bio_error = bp->bio_error;
			if (bp->bio_flags & BIO_ERROR) {
				bp1->bio_flags |= BIO_ERROR;
				bp1->bio_resid = bp1->bio_bcount;
			} else
				bp1->bio_resid = 0;
			biodone(bp1);
		}
	} else {
		daschedule(periph);
		cam_periph_unlock(periph);
	}
	if (bp != NULL)
		biodone(bp);
	return;
}

static void
dadone_probewp(struct cam_periph *periph, union ccb *done_ccb)
{
	struct scsi_mode_header_6 *mode_hdr6;
	struct scsi_mode_header_10 *mode_hdr10;
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	u_int32_t  priority;
	uint8_t dev_spec;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_probewp\n"));

	softc = (struct da_softc *)periph->softc;
	priority = done_ccb->ccb_h.pinfo.priority;
	csio = &done_ccb->csio;

	cam_periph_assert(periph, MA_OWNED);

	if (softc->minimum_cmd_size > 6) {
		mode_hdr10 = (struct scsi_mode_header_10 *)csio->data_ptr;
		dev_spec = mode_hdr10->dev_spec;
	} else {
		mode_hdr6 = (struct scsi_mode_header_6 *)csio->data_ptr;
		dev_spec = mode_hdr6->dev_spec;
	}
	if (cam_ccb_status(done_ccb) == CAM_REQ_CMP) {
		if ((dev_spec & 0x80) != 0)
			softc->disk->d_flags |= DISKFLAG_WRITE_PROTECT;
		else
			softc->disk->d_flags &= ~DISKFLAG_WRITE_PROTECT;
	} else {
		int error;

		error = daerror(done_ccb, CAM_RETRY_SELTO,
				SF_RETRY_UA|SF_NO_PRINT);
		if (error == ERESTART)
			return;
		else if (error != 0) {
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				/* Don't wedge this device's queue */
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
			}
		}
	}

	free(csio->data_ptr, M_SCSIDA);
	xpt_release_ccb(done_ccb);
	if ((softc->flags & DA_FLAG_CAN_RC16) != 0)
		softc->state = DA_STATE_PROBE_RC16;
	else
		softc->state = DA_STATE_PROBE_RC;
	xpt_schedule(periph, priority);
	return;
}

static void
dadone_proberc(struct cam_periph *periph, union ccb *done_ccb)
{
	struct scsi_read_capacity_data *rdcap;
	struct scsi_read_capacity_data_long *rcaplong;
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	da_ccb_state state;
	char *announce_buf;
	u_int32_t  priority;
	int lbp;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_proberc\n"));

	softc = (struct da_softc *)periph->softc;
	priority = done_ccb->ccb_h.pinfo.priority;
	csio = &done_ccb->csio;
	state = csio->ccb_h.ccb_state & DA_CCB_TYPE_MASK;

	lbp = 0;
	rdcap = NULL;
	rcaplong = NULL;
	/* XXX TODO: can this be a malloc? */
	announce_buf = softc->announce_temp;
	bzero(announce_buf, DA_ANNOUNCETMP_SZ);

	if (state == DA_CCB_PROBE_RC)
		rdcap =(struct scsi_read_capacity_data *)csio->data_ptr;
	else
		rcaplong = (struct scsi_read_capacity_data_long *)
			csio->data_ptr;

	cam_periph_assert(periph, MA_OWNED);

	if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		struct disk_params *dp;
		uint32_t block_size;
		uint64_t maxsector;
		u_int lalba;	/* Lowest aligned LBA. */

		if (state == DA_CCB_PROBE_RC) {
			block_size = scsi_4btoul(rdcap->length);
			maxsector = scsi_4btoul(rdcap->addr);
			lalba = 0;

			/*
			 * According to SBC-2, if the standard 10
			 * byte READ CAPACITY command returns 2^32,
			 * we should issue the 16 byte version of
			 * the command, since the device in question
			 * has more sectors than can be represented
			 * with the short version of the command.
			 */
			if (maxsector == 0xffffffff) {
				free(rdcap, M_SCSIDA);
				xpt_release_ccb(done_ccb);
				softc->state = DA_STATE_PROBE_RC16;
				xpt_schedule(periph, priority);
				return;
			}
		} else {
			block_size = scsi_4btoul(rcaplong->length);
			maxsector = scsi_8btou64(rcaplong->addr);
			lalba = scsi_2btoul(rcaplong->lalba_lbp);
		}

		/*
		 * Because GEOM code just will panic us if we
		 * give them an 'illegal' value we'll avoid that
		 * here.
		 */
		if (block_size == 0) {
			block_size = 512;
			if (maxsector == 0)
				maxsector = -1;
		}
		if (block_size >= MAXPHYS) {
			xpt_print(periph->path,
			    "unsupportable block size %ju\n",
			    (uintmax_t) block_size);
			announce_buf = NULL;
			cam_periph_invalidate(periph);
		} else {
			/*
			 * We pass rcaplong into dasetgeom(),
			 * because it will only use it if it is
			 * non-NULL.
			 */
			dasetgeom(periph, block_size, maxsector,
				  rcaplong, sizeof(*rcaplong));
			lbp = (lalba & SRC16_LBPME_A);
			dp = &softc->params;
			snprintf(announce_buf, DA_ANNOUNCETMP_SZ,
			    "%juMB (%ju %u byte sectors)",
			    ((uintmax_t)dp->secsize * dp->sectors) /
			     (1024 * 1024),
			    (uintmax_t)dp->sectors, dp->secsize);
		}
	} else {
		int error;

		/*
		 * Retry any UNIT ATTENTION type errors.  They
		 * are expected at boot.
		 */
		error = daerror(done_ccb, CAM_RETRY_SELTO,
				SF_RETRY_UA|SF_NO_PRINT);
		if (error == ERESTART) {
			/*
			 * A retry was scheuled, so
			 * just return.
			 */
			return;
		} else if (error != 0) {
			int asc, ascq;
			int sense_key, error_code;
			int have_sense;
			cam_status status;
			struct ccb_getdev cgd;

			/* Don't wedge this device's queue */
			status = done_ccb->ccb_h.status;
			if ((status & CAM_DEV_QFRZN) != 0)
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);


			xpt_setup_ccb(&cgd.ccb_h, done_ccb->ccb_h.path,
				      CAM_PRIORITY_NORMAL);
			cgd.ccb_h.func_code = XPT_GDEV_TYPE;
			xpt_action((union ccb *)&cgd);

			if (scsi_extract_sense_ccb(done_ccb,
			    &error_code, &sense_key, &asc, &ascq))
				have_sense = TRUE;
			else
				have_sense = FALSE;

			/*
			 * If we tried READ CAPACITY(16) and failed,
			 * fallback to READ CAPACITY(10).
			 */
			if ((state == DA_CCB_PROBE_RC16) &&
			    (softc->flags & DA_FLAG_CAN_RC16) &&
			    (((csio->ccb_h.status & CAM_STATUS_MASK) ==
				CAM_REQ_INVALID) ||
			     ((have_sense) &&
			      (error_code == SSD_CURRENT_ERROR ||
			       error_code == SSD_DESC_CURRENT_ERROR) &&
			      (sense_key == SSD_KEY_ILLEGAL_REQUEST)))) {
				cam_periph_assert(periph, MA_OWNED);
				softc->flags &= ~DA_FLAG_CAN_RC16;
				free(rdcap, M_SCSIDA);
				xpt_release_ccb(done_ccb);
				softc->state = DA_STATE_PROBE_RC;
				xpt_schedule(periph, priority);
				return;
			}

			/*
			 * Attach to anything that claims to be a
			 * direct access or optical disk device,
			 * as long as it doesn't return a "Logical
			 * unit not supported" (0x25) error.
			 * "Internal Target Failure" (0x44) is also
			 * special and typically means that the
			 * device is a SATA drive behind a SATL
			 * translation that's fallen into a
			 * terminally fatal state.
			 */
			if ((have_sense)
			 && (asc != 0x25) && (asc != 0x44)
			 && (error_code == SSD_CURRENT_ERROR
			  || error_code == SSD_DESC_CURRENT_ERROR)) {
				const char *sense_key_desc;
				const char *asc_desc;

				dasetgeom(periph, 512, -1, NULL, 0);
				scsi_sense_desc(sense_key, asc, ascq,
						&cgd.inq_data, &sense_key_desc,
						&asc_desc);
				snprintf(announce_buf, DA_ANNOUNCETMP_SZ,
				    "Attempt to query device "
				    "size failed: %s, %s",
				    sense_key_desc, asc_desc);
			} else {
				if (have_sense)
					scsi_sense_print(&done_ccb->csio);
				else {
					xpt_print(periph->path,
					    "got CAM status %#x\n",
					    done_ccb->ccb_h.status);
				}

				xpt_print(periph->path, "fatal error, "
				    "failed to attach to device\n");

				announce_buf = NULL;

				/*
				 * Free up resources.
				 */
				cam_periph_invalidate(periph);
			}
		}
	}
	free(csio->data_ptr, M_SCSIDA);
	if (announce_buf != NULL &&
	    ((softc->flags & DA_FLAG_ANNOUNCED) == 0)) {
		struct sbuf sb;

		sbuf_new(&sb, softc->announcebuf, DA_ANNOUNCE_SZ,
		    SBUF_FIXEDLEN);
		xpt_announce_periph_sbuf(periph, &sb, announce_buf);
		xpt_announce_quirks_sbuf(periph, &sb, softc->quirks,
		    DA_Q_BIT_STRING);
		sbuf_finish(&sb);
		sbuf_putbuf(&sb);

		/*
		 * Create our sysctl variables, now that we know
		 * we have successfully attached.
		 */
		/* increase the refcount */
		if (da_periph_acquire(periph, DA_REF_SYSCTL) == 0) {
			taskqueue_enqueue(taskqueue_thread,
					  &softc->sysctl_task);
		} else {
			/* XXX This message is useless! */
			xpt_print(periph->path, "fatal error, "
			    "could not acquire reference count\n");
		}
	}

	/* We already probed the device. */
	if (softc->flags & DA_FLAG_PROBED) {
		daprobedone(periph, done_ccb);
		return;
	}

	/* Ensure re-probe doesn't see old delete. */
	softc->delete_available = 0;
	dadeleteflag(softc, DA_DELETE_ZERO, 1);
	if (lbp && (softc->quirks & DA_Q_NO_UNMAP) == 0) {
		/*
		 * Based on older SBC-3 spec revisions
		 * any of the UNMAP methods "may" be
		 * available via LBP given this flag so
		 * we flag all of them as available and
		 * then remove those which further
		 * probes confirm aren't available
		 * later.
		 *
		 * We could also check readcap(16) p_type
		 * flag to exclude one or more invalid
		 * write same (X) types here
		 */
		dadeleteflag(softc, DA_DELETE_WS16, 1);
		dadeleteflag(softc, DA_DELETE_WS10, 1);
		dadeleteflag(softc, DA_DELETE_UNMAP, 1);

		xpt_release_ccb(done_ccb);
		softc->state = DA_STATE_PROBE_LBP;
		xpt_schedule(periph, priority);
		return;
	}

	xpt_release_ccb(done_ccb);
	softc->state = DA_STATE_PROBE_BDC;
	xpt_schedule(periph, priority);
	return;
}

static void
dadone_probelbp(struct cam_periph *periph, union ccb *done_ccb)
{
	struct scsi_vpd_logical_block_prov *lbp;
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	u_int32_t  priority;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_probelbp\n"));

	softc = (struct da_softc *)periph->softc;
	priority = done_ccb->ccb_h.pinfo.priority;
	csio = &done_ccb->csio;
	lbp = (struct scsi_vpd_logical_block_prov *)csio->data_ptr;

	cam_periph_assert(periph, MA_OWNED);

	if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		/*
		 * T10/1799-D Revision 31 states at least one of these
		 * must be supported but we don't currently enforce this.
		 */
		dadeleteflag(softc, DA_DELETE_WS16,
		     (lbp->flags & SVPD_LBP_WS16));
		dadeleteflag(softc, DA_DELETE_WS10,
			     (lbp->flags & SVPD_LBP_WS10));
		dadeleteflag(softc, DA_DELETE_UNMAP,
			     (lbp->flags & SVPD_LBP_UNMAP));
	} else {
		int error;
		error = daerror(done_ccb, CAM_RETRY_SELTO,
				SF_RETRY_UA|SF_NO_PRINT);
		if (error == ERESTART)
			return;
		else if (error != 0) {
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				/* Don't wedge this device's queue */
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
			}

			/*
			 * Failure indicates we don't support any SBC-3
			 * delete methods with UNMAP
			 */
		}
	}

	free(lbp, M_SCSIDA);
	xpt_release_ccb(done_ccb);
	softc->state = DA_STATE_PROBE_BLK_LIMITS;
	xpt_schedule(periph, priority);
	return;
}

static void
dadone_probeblklimits(struct cam_periph *periph, union ccb *done_ccb)
{
	struct scsi_vpd_block_limits *block_limits;
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	u_int32_t  priority;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_probeblklimits\n"));

	softc = (struct da_softc *)periph->softc;
	priority = done_ccb->ccb_h.pinfo.priority;
	csio = &done_ccb->csio;
	block_limits = (struct scsi_vpd_block_limits *)csio->data_ptr;

	cam_periph_assert(periph, MA_OWNED);

	if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		uint32_t max_txfer_len = scsi_4btoul(
			block_limits->max_txfer_len);
		uint32_t max_unmap_lba_cnt = scsi_4btoul(
			block_limits->max_unmap_lba_cnt);
		uint32_t max_unmap_blk_cnt = scsi_4btoul(
			block_limits->max_unmap_blk_cnt);
		uint32_t unmap_gran = scsi_4btoul(
			block_limits->opt_unmap_grain);
		uint32_t unmap_gran_align = scsi_4btoul(
			block_limits->unmap_grain_align);
		uint64_t ws_max_blks = scsi_8btou64(
			block_limits->max_write_same_length);

		if (max_txfer_len != 0) {
			softc->disk->d_maxsize = MIN(softc->maxio,
			    (off_t)max_txfer_len * softc->params.secsize);
		}

		/*
		 * We should already support UNMAP but we check lba
		 * and block count to be sure
		 */
		if (max_unmap_lba_cnt != 0x00L &&
		    max_unmap_blk_cnt != 0x00L) {
			softc->unmap_max_lba = max_unmap_lba_cnt;
			softc->unmap_max_ranges = min(max_unmap_blk_cnt,
				UNMAP_MAX_RANGES);
			if (unmap_gran > 1) {
				softc->unmap_gran = unmap_gran;
				if (unmap_gran_align & 0x80000000) {
					softc->unmap_gran_align =
					    unmap_gran_align & 0x7fffffff;
				}
			}
		} else {
			/*
			 * Unexpected UNMAP limits which means the
			 * device doesn't actually support UNMAP
			 */
			dadeleteflag(softc, DA_DELETE_UNMAP, 0);
		}

		if (ws_max_blks != 0x00L)
			softc->ws_max_blks = ws_max_blks;
	} else {
		int error;
		error = daerror(done_ccb, CAM_RETRY_SELTO,
				SF_RETRY_UA|SF_NO_PRINT);
		if (error == ERESTART)
			return;
		else if (error != 0) {
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				/* Don't wedge this device's queue */
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
			}

			/*
			 * Failure here doesn't mean UNMAP is not
			 * supported as this is an optional page.
			 */
			softc->unmap_max_lba = 1;
			softc->unmap_max_ranges = 1;
		}
	}

	free(block_limits, M_SCSIDA);
	xpt_release_ccb(done_ccb);
	softc->state = DA_STATE_PROBE_BDC;
	xpt_schedule(periph, priority);
	return;
}

static void
dadone_probebdc(struct cam_periph *periph, union ccb *done_ccb)
{
	struct scsi_vpd_block_device_characteristics *bdc;
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	u_int32_t  priority;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_probebdc\n"));

	softc = (struct da_softc *)periph->softc;
	priority = done_ccb->ccb_h.pinfo.priority;
	csio = &done_ccb->csio;
	bdc = (struct scsi_vpd_block_device_characteristics *)csio->data_ptr;

	cam_periph_assert(periph, MA_OWNED);

	if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		uint32_t valid_len;

		/*
		 * Disable queue sorting for non-rotational media
		 * by default.
		 */
		u_int16_t old_rate = softc->disk->d_rotation_rate;

		valid_len = csio->dxfer_len - csio->resid;
		if (SBDC_IS_PRESENT(bdc, valid_len,
		    medium_rotation_rate)) {
			softc->disk->d_rotation_rate =
				scsi_2btoul(bdc->medium_rotation_rate);
			if (softc->disk->d_rotation_rate ==
			    SVPD_BDC_RATE_NON_ROTATING) {
				cam_iosched_set_sort_queue(
				    softc->cam_iosched, 0);
				softc->rotating = 0;
			}
			if (softc->disk->d_rotation_rate != old_rate) {
				disk_attr_changed(softc->disk,
				    "GEOM::rotation_rate", M_NOWAIT);
			}
		}
		if ((SBDC_IS_PRESENT(bdc, valid_len, flags))
		 && (softc->zone_mode == DA_ZONE_NONE)) {
			int ata_proto;

			if (scsi_vpd_supported_page(periph,
			    SVPD_ATA_INFORMATION))
				ata_proto = 1;
			else
				ata_proto = 0;

			/*
			 * The Zoned field will only be set for
			 * Drive Managed and Host Aware drives.  If
			 * they are Host Managed, the device type
			 * in the standard INQUIRY data should be
			 * set to T_ZBC_HM (0x14).
			 */
			if ((bdc->flags & SVPD_ZBC_MASK) ==
			     SVPD_HAW_ZBC) {
				softc->zone_mode = DA_ZONE_HOST_AWARE;
				softc->zone_interface = (ata_proto) ?
				   DA_ZONE_IF_ATA_SAT : DA_ZONE_IF_SCSI;
			} else if ((bdc->flags & SVPD_ZBC_MASK) ==
			     SVPD_DM_ZBC) {
				softc->zone_mode =DA_ZONE_DRIVE_MANAGED;
				softc->zone_interface = (ata_proto) ?
				   DA_ZONE_IF_ATA_SAT : DA_ZONE_IF_SCSI;
			} else if ((bdc->flags & SVPD_ZBC_MASK) !=
				  SVPD_ZBC_NR) {
				xpt_print(periph->path, "Unknown zoned "
				    "type %#x",
				    bdc->flags & SVPD_ZBC_MASK);
			}
		}
	} else {
		int error;
		error = daerror(done_ccb, CAM_RETRY_SELTO,
				SF_RETRY_UA|SF_NO_PRINT);
		if (error == ERESTART)
			return;
		else if (error != 0) {
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				/* Don't wedge this device's queue */
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
			}
		}
	}

	free(bdc, M_SCSIDA);
	xpt_release_ccb(done_ccb);
	softc->state = DA_STATE_PROBE_ATA;
	xpt_schedule(periph, priority);
	return;
}

static void
dadone_probeata(struct cam_periph *periph, union ccb *done_ccb)
{
	struct ata_params *ata_params;
	struct ccb_scsiio *csio;
	struct da_softc *softc;
	u_int32_t  priority;
	int continue_probe;
	int error, i;
	int16_t *ptr;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_probeata\n"));

	softc = (struct da_softc *)periph->softc;
	priority = done_ccb->ccb_h.pinfo.priority;
	csio = &done_ccb->csio;
	ata_params = (struct ata_params *)csio->data_ptr;
	ptr = (uint16_t *)ata_params;
	continue_probe = 0;
	error = 0;

	cam_periph_assert(periph, MA_OWNED);

	if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		uint16_t old_rate;

		for (i = 0; i < sizeof(*ata_params) / 2; i++)
			ptr[i] = le16toh(ptr[i]);
		if (ata_params->support_dsm & ATA_SUPPORT_DSM_TRIM &&
		    (softc->quirks & DA_Q_NO_UNMAP) == 0) {
			dadeleteflag(softc, DA_DELETE_ATA_TRIM, 1);
			if (ata_params->max_dsm_blocks != 0)
				softc->trim_max_ranges = min(
				  softc->trim_max_ranges,
				  ata_params->max_dsm_blocks *
				  ATA_DSM_BLK_RANGES);
		}
		/*
		 * Disable queue sorting for non-rotational media
		 * by default.
		 */
		old_rate = softc->disk->d_rotation_rate;
		softc->disk->d_rotation_rate = ata_params->media_rotation_rate;
		if (softc->disk->d_rotation_rate == ATA_RATE_NON_ROTATING) {
			cam_iosched_set_sort_queue(softc->cam_iosched, 0);
			softc->rotating = 0;
		}
		if (softc->disk->d_rotation_rate != old_rate) {
			disk_attr_changed(softc->disk,
			    "GEOM::rotation_rate", M_NOWAIT);
		}

		cam_periph_assert(periph, MA_OWNED);
		if (ata_params->capabilities1 & ATA_SUPPORT_DMA)
			softc->flags |= DA_FLAG_CAN_ATA_DMA;

		if (ata_params->support.extension & ATA_SUPPORT_GENLOG)
			softc->flags |= DA_FLAG_CAN_ATA_LOG;

		/*
		 * At this point, if we have a SATA host aware drive,
		 * we communicate via ATA passthrough unless the
		 * SAT layer supports ZBC -> ZAC translation.  In
		 * that case,
		 *
		 * XXX KDM figure out how to detect a host managed
		 * SATA drive.
		 */
		if (softc->zone_mode == DA_ZONE_NONE) {
			/*
			 * Note that we don't override the zone
			 * mode or interface if it has already been
			 * set.  This is because it has either been
			 * set as a quirk, or when we probed the
			 * SCSI Block Device Characteristics page,
			 * the zoned field was set.  The latter
			 * means that the SAT layer supports ZBC to
			 * ZAC translation, and we would prefer to
			 * use that if it is available.
			 */
			if ((ata_params->support3 &
			    ATA_SUPPORT_ZONE_MASK) ==
			    ATA_SUPPORT_ZONE_HOST_AWARE) {
				softc->zone_mode = DA_ZONE_HOST_AWARE;
				softc->zone_interface =
				    DA_ZONE_IF_ATA_PASS;
			} else if ((ata_params->support3 &
				    ATA_SUPPORT_ZONE_MASK) ==
				    ATA_SUPPORT_ZONE_DEV_MANAGED) {
				softc->zone_mode =DA_ZONE_DRIVE_MANAGED;
				softc->zone_interface = DA_ZONE_IF_ATA_PASS;
			}
		}

	} else {
		error = daerror(done_ccb, CAM_RETRY_SELTO,
				SF_RETRY_UA|SF_NO_PRINT);
		if (error == ERESTART)
			return;
		else if (error != 0) {
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				/* Don't wedge this device's queue */
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
			}
		}
	}

	free(ata_params, M_SCSIDA);
	if ((softc->zone_mode == DA_ZONE_HOST_AWARE)
	 || (softc->zone_mode == DA_ZONE_HOST_MANAGED)) {
		/*
		 * If the ATA IDENTIFY failed, we could be talking
		 * to a SCSI drive, although that seems unlikely,
		 * since the drive did report that it supported the
		 * ATA Information VPD page.  If the ATA IDENTIFY
		 * succeeded, and the SAT layer doesn't support
		 * ZBC -> ZAC translation, continue on to get the
		 * directory of ATA logs, and complete the rest of
		 * the ZAC probe.  If the SAT layer does support
		 * ZBC -> ZAC translation, we want to use that,
		 * and we'll probe the SCSI Zoned Block Device
		 * Characteristics VPD page next.
		 */
		if ((error == 0)
		 && (softc->flags & DA_FLAG_CAN_ATA_LOG)
		 && (softc->zone_interface == DA_ZONE_IF_ATA_PASS))
			softc->state = DA_STATE_PROBE_ATA_LOGDIR;
		else
			softc->state = DA_STATE_PROBE_ZONE;
		continue_probe = 1;
	}
	if (continue_probe != 0) {
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;
	} else
		daprobedone(periph, done_ccb);
	return;
}

static void
dadone_probeatalogdir(struct cam_periph *periph, union ccb *done_ccb)
{
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	u_int32_t  priority;
	int error;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_probeatalogdir\n"));

	softc = (struct da_softc *)periph->softc;
	priority = done_ccb->ccb_h.pinfo.priority;
	csio = &done_ccb->csio;

	cam_periph_assert(periph, MA_OWNED);
	if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		error = 0;
		softc->valid_logdir_len = 0;
		bzero(&softc->ata_logdir, sizeof(softc->ata_logdir));
		softc->valid_logdir_len = csio->dxfer_len - csio->resid;
		if (softc->valid_logdir_len > 0)
			bcopy(csio->data_ptr, &softc->ata_logdir,
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
			softc->flags |= DA_FLAG_CAN_ATA_IDLOG;
		} else {
			softc->flags &= ~DA_FLAG_CAN_ATA_IDLOG;
		}
	} else {
		error = daerror(done_ccb, CAM_RETRY_SELTO,
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
			softc->flags &= ~(DA_FLAG_CAN_ATA_LOG |
					  DA_FLAG_CAN_ATA_IDLOG);
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

	free(csio->data_ptr, M_SCSIDA);

	if ((error == 0)
	 && (softc->flags & DA_FLAG_CAN_ATA_IDLOG)) {
		softc->state = DA_STATE_PROBE_ATA_IDDIR;
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;
	}
	daprobedone(periph, done_ccb);
	return;
}

static void
dadone_probeataiddir(struct cam_periph *periph, union ccb *done_ccb)
{
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	u_int32_t  priority;
	int error;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_probeataiddir\n"));

	softc = (struct da_softc *)periph->softc;
	priority = done_ccb->ccb_h.pinfo.priority;
	csio = &done_ccb->csio;

	cam_periph_assert(periph, MA_OWNED);

	if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		off_t entries_offset, max_entries;
		error = 0;

		softc->valid_iddir_len = 0;
		bzero(&softc->ata_iddir, sizeof(softc->ata_iddir));
		softc->flags &= ~(DA_FLAG_CAN_ATA_SUPCAP |
				  DA_FLAG_CAN_ATA_ZONE);
		softc->valid_iddir_len = csio->dxfer_len - csio->resid;
		if (softc->valid_iddir_len > 0)
			bcopy(csio->data_ptr, &softc->ata_iddir,
			    min(softc->valid_iddir_len,
				sizeof(softc->ata_iddir)));

		entries_offset =
		    __offsetof(struct ata_identify_log_pages,entries);
		max_entries = softc->valid_iddir_len - entries_offset;
		if ((softc->valid_iddir_len > (entries_offset + 1))
		 && (le64dec(softc->ata_iddir.header) == ATA_IDLOG_REVISION)
		 && (softc->ata_iddir.entry_count > 0)) {
			int num_entries, i;

			num_entries = softc->ata_iddir.entry_count;
			num_entries = min(num_entries,
			   softc->valid_iddir_len - entries_offset);
			for (i = 0; i < num_entries && i < max_entries; i++) {
				if (softc->ata_iddir.entries[i] ==
				    ATA_IDL_SUP_CAP)
					softc->flags |= DA_FLAG_CAN_ATA_SUPCAP;
				else if (softc->ata_iddir.entries[i] ==
					 ATA_IDL_ZDI)
					softc->flags |= DA_FLAG_CAN_ATA_ZONE;

				if ((softc->flags & DA_FLAG_CAN_ATA_SUPCAP)
				 && (softc->flags & DA_FLAG_CAN_ATA_ZONE))
					break;
			}
		}
	} else {
		error = daerror(done_ccb, CAM_RETRY_SELTO,
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
			softc->flags &= ~DA_FLAG_CAN_ATA_IDLOG;
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				/* Don't wedge this device's queue */
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
			}
		}
	}

	free(csio->data_ptr, M_SCSIDA);

	if ((error == 0) && (softc->flags & DA_FLAG_CAN_ATA_SUPCAP)) {
		softc->state = DA_STATE_PROBE_ATA_SUP;
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;
	}
	daprobedone(periph, done_ccb);
	return;
}

static void
dadone_probeatasup(struct cam_periph *periph, union ccb *done_ccb)
{
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	u_int32_t  priority;
	int error;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_probeatasup\n"));

	softc = (struct da_softc *)periph->softc;
	priority = done_ccb->ccb_h.pinfo.priority;
	csio = &done_ccb->csio;

	cam_periph_assert(periph, MA_OWNED);

	if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		uint32_t valid_len;
		size_t needed_size;
		struct ata_identify_log_sup_cap *sup_cap;
		error = 0;

		sup_cap = (struct ata_identify_log_sup_cap *)csio->data_ptr;
		valid_len = csio->dxfer_len - csio->resid;
		needed_size = __offsetof(struct ata_identify_log_sup_cap,
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
					softc->zone_mode = DA_ZONE_HOST_AWARE;
				else if ((zoned & ATA_ZONED_MASK) ==
				    ATA_SUPPORT_ZONE_DEV_MANAGED)
					softc->zone_mode =
					    DA_ZONE_DRIVE_MANAGED;
			}

			zac_cap = le64dec(sup_cap->sup_zac_cap);
			if (zac_cap & ATA_SUP_ZAC_CAP_VALID) {
				if (zac_cap & ATA_REPORT_ZONES_SUP)
					softc->zone_flags |=
					    DA_ZONE_FLAG_RZ_SUP;
				if (zac_cap & ATA_ND_OPEN_ZONE_SUP)
					softc->zone_flags |=
					    DA_ZONE_FLAG_OPEN_SUP;
				if (zac_cap & ATA_ND_CLOSE_ZONE_SUP)
					softc->zone_flags |=
					    DA_ZONE_FLAG_CLOSE_SUP;
				if (zac_cap & ATA_ND_FINISH_ZONE_SUP)
					softc->zone_flags |=
					    DA_ZONE_FLAG_FINISH_SUP;
				if (zac_cap & ATA_ND_RWP_SUP)
					softc->zone_flags |=
					    DA_ZONE_FLAG_RWP_SUP;
			} else {
				/*
				 * This field was introduced in
				 * ACS-4, r08 on April 28th, 2015.
				 * If the drive firmware was written
				 * to an earlier spec, it won't have
				 * the field.  So, assume all
				 * commands are supported.
				 */
				softc->zone_flags |= DA_ZONE_FLAG_SUP_MASK;
			}
		}
	} else {
		error = daerror(done_ccb, CAM_RETRY_SELTO,
				SF_RETRY_UA|SF_NO_PRINT);
		if (error == ERESTART)
			return;
		else if (error != 0) {
			/*
			 * If we can't get the ATA Identify Data
			 * Supported Capabilities page, clear the
			 * flag...
			 */
			softc->flags &= ~DA_FLAG_CAN_ATA_SUPCAP;
			/*
			 * And clear zone capabilities.
			 */
			softc->zone_flags &= ~DA_ZONE_FLAG_SUP_MASK;
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				/* Don't wedge this device's queue */
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
			}
		}
	}

	free(csio->data_ptr, M_SCSIDA);

	if ((error == 0) && (softc->flags & DA_FLAG_CAN_ATA_ZONE)) {
		softc->state = DA_STATE_PROBE_ATA_ZONE;
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;
	}
	daprobedone(periph, done_ccb);
	return;
}

static void
dadone_probeatazone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	int error;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_probeatazone\n"));

	softc = (struct da_softc *)periph->softc;
	csio = &done_ccb->csio;

	cam_periph_assert(periph, MA_OWNED);

	if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		struct ata_zoned_info_log *zi_log;
		uint32_t valid_len;
		size_t needed_size;

		zi_log = (struct ata_zoned_info_log *)csio->data_ptr;

		valid_len = csio->dxfer_len - csio->resid;
		needed_size = __offsetof(struct ata_zoned_info_log,
		    version_info) + 1 + sizeof(zi_log->version_info);
		if (valid_len >= needed_size) {
			uint64_t tmpvar;

			tmpvar = le64dec(zi_log->zoned_cap);
			if (tmpvar & ATA_ZDI_CAP_VALID) {
				if (tmpvar & ATA_ZDI_CAP_URSWRZ)
					softc->zone_flags |=
					    DA_ZONE_FLAG_URSWRZ;
				else
					softc->zone_flags &=
					    ~DA_ZONE_FLAG_URSWRZ;
			}
			tmpvar = le64dec(zi_log->optimal_seq_zones);
			if (tmpvar & ATA_ZDI_OPT_SEQ_VALID) {
				softc->zone_flags |= DA_ZONE_FLAG_OPT_SEQ_SET;
				softc->optimal_seq_zones = (tmpvar &
				    ATA_ZDI_OPT_SEQ_MASK);
			} else {
				softc->zone_flags &= ~DA_ZONE_FLAG_OPT_SEQ_SET;
				softc->optimal_seq_zones = 0;
			}

			tmpvar =le64dec(zi_log->optimal_nonseq_zones);
			if (tmpvar & ATA_ZDI_OPT_NS_VALID) {
				softc->zone_flags |=
				    DA_ZONE_FLAG_OPT_NONSEQ_SET;
				softc->optimal_nonseq_zones =
				    (tmpvar & ATA_ZDI_OPT_NS_MASK);
			} else {
				softc->zone_flags &=
				    ~DA_ZONE_FLAG_OPT_NONSEQ_SET;
				softc->optimal_nonseq_zones = 0;
			}

			tmpvar = le64dec(zi_log->max_seq_req_zones);
			if (tmpvar & ATA_ZDI_MAX_SEQ_VALID) {
				softc->zone_flags |= DA_ZONE_FLAG_MAX_SEQ_SET;
				softc->max_seq_zones =
				    (tmpvar & ATA_ZDI_MAX_SEQ_MASK);
			} else {
				softc->zone_flags &= ~DA_ZONE_FLAG_MAX_SEQ_SET;
				softc->max_seq_zones = 0;
			}
		}
	} else {
		error = daerror(done_ccb, CAM_RETRY_SELTO,
				SF_RETRY_UA|SF_NO_PRINT);
		if (error == ERESTART)
			return;
		else if (error != 0) {
			softc->flags &= ~DA_FLAG_CAN_ATA_ZONE;
			softc->flags &= ~DA_ZONE_FLAG_SET_MASK;

			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				/* Don't wedge this device's queue */
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
			}
		}

	}

	free(csio->data_ptr, M_SCSIDA);

	daprobedone(periph, done_ccb);
	return;
}

static void
dadone_probezone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct da_softc *softc;
	struct ccb_scsiio *csio;
	int error;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_probezone\n"));

	softc = (struct da_softc *)periph->softc;
	csio = &done_ccb->csio;

	cam_periph_assert(periph, MA_OWNED);

	if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		uint32_t valid_len;
		size_t needed_len;
		struct scsi_vpd_zoned_bdc *zoned_bdc;

		error = 0;
		zoned_bdc = (struct scsi_vpd_zoned_bdc *)csio->data_ptr;
		valid_len = csio->dxfer_len - csio->resid;
		needed_len = __offsetof(struct scsi_vpd_zoned_bdc,
		    max_seq_req_zones) + 1 +
		    sizeof(zoned_bdc->max_seq_req_zones);
		if ((valid_len >= needed_len)
		 && (scsi_2btoul(zoned_bdc->page_length) >= SVPD_ZBDC_PL)) {
			if (zoned_bdc->flags & SVPD_ZBDC_URSWRZ)
				softc->zone_flags |= DA_ZONE_FLAG_URSWRZ;
			else
				softc->zone_flags &= ~DA_ZONE_FLAG_URSWRZ;
			softc->optimal_seq_zones =
			    scsi_4btoul(zoned_bdc->optimal_seq_zones);
			softc->zone_flags |= DA_ZONE_FLAG_OPT_SEQ_SET;
			softc->optimal_nonseq_zones = scsi_4btoul(
			    zoned_bdc->optimal_nonseq_zones);
			softc->zone_flags |= DA_ZONE_FLAG_OPT_NONSEQ_SET;
			softc->max_seq_zones =
			    scsi_4btoul(zoned_bdc->max_seq_req_zones);
			softc->zone_flags |= DA_ZONE_FLAG_MAX_SEQ_SET;
		}
		/*
		 * All of the zone commands are mandatory for SCSI
		 * devices.
		 *
		 * XXX KDM this is valid as of September 2015.
		 * Re-check this assumption once the SAT spec is
		 * updated to support SCSI ZBC to ATA ZAC mapping.
		 * Since ATA allows zone commands to be reported
		 * as supported or not, this may not necessarily
		 * be true for an ATA device behind a SAT (SCSI to
		 * ATA Translation) layer.
		 */
		softc->zone_flags |= DA_ZONE_FLAG_SUP_MASK;
	} else {
		error = daerror(done_ccb, CAM_RETRY_SELTO,
				SF_RETRY_UA|SF_NO_PRINT);
		if (error == ERESTART)
			return;
		else if (error != 0) {
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				/* Don't wedge this device's queue */
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
			}
		}
	}

	free(csio->data_ptr, M_SCSIDA);

	daprobedone(periph, done_ccb);
	return;
}

static void
dadone_tur(struct cam_periph *periph, union ccb *done_ccb)
{
	struct da_softc *softc;
	struct ccb_scsiio *csio;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("dadone_tur\n"));

	softc = (struct da_softc *)periph->softc;
	csio = &done_ccb->csio;

	cam_periph_assert(periph, MA_OWNED);

	if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {

		if (daerror(done_ccb, CAM_RETRY_SELTO,
		    SF_RETRY_UA | SF_NO_RECOVERY | SF_NO_PRINT) == ERESTART)
			return;	/* Will complete again, keep reference */
		if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
			cam_release_devq(done_ccb->ccb_h.path,
					 /*relsim_flags*/0,
					 /*reduction*/0,
					 /*timeout*/0,
					 /*getcount_only*/0);
	}
	xpt_release_ccb(done_ccb);
	softc->flags &= ~DA_FLAG_TUR_PENDING;
	da_periph_release_locked(periph, DA_REF_TUR);
	return;
}

static void
dareprobe(struct cam_periph *periph)
{
	struct da_softc	  *softc;
	int status;

	softc = (struct da_softc *)periph->softc;

	/* Probe in progress; don't interfere. */
	if (softc->state != DA_STATE_NORMAL)
		return;

	status = da_periph_acquire(periph, DA_REF_REPROBE);
	KASSERT(status == 0, ("dareprobe: cam_periph_acquire failed"));

	softc->state = DA_STATE_PROBE_WP;
	xpt_schedule(periph, CAM_PRIORITY_DEV);
}

static int
daerror(union ccb *ccb, u_int32_t cam_flags, u_int32_t sense_flags)
{
	struct da_softc	  *softc;
	struct cam_periph *periph;
	int error, error_code, sense_key, asc, ascq;

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	if (ccb->csio.bio != NULL)
		biotrack(ccb->csio.bio, __func__);
#endif

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct da_softc *)periph->softc;

	cam_periph_assert(periph, MA_OWNED);

	/*
	 * Automatically detect devices that do not support
	 * READ(6)/WRITE(6) and upgrade to using 10 byte cdbs.
	 */
	error = 0;
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_INVALID) {
		error = cmd6workaround(ccb);
	} else if (scsi_extract_sense_ccb(ccb,
	    &error_code, &sense_key, &asc, &ascq)) {
		if (sense_key == SSD_KEY_ILLEGAL_REQUEST)
			error = cmd6workaround(ccb);
		/*
		 * If the target replied with CAPACITY DATA HAS CHANGED UA,
		 * query the capacity and notify upper layers.
		 */
		else if (sense_key == SSD_KEY_UNIT_ATTENTION &&
		    asc == 0x2A && ascq == 0x09) {
			xpt_print(periph->path, "Capacity data has changed\n");
			softc->flags &= ~DA_FLAG_PROBED;
			dareprobe(periph);
			sense_flags |= SF_NO_PRINT;
		} else if (sense_key == SSD_KEY_UNIT_ATTENTION &&
		    asc == 0x28 && ascq == 0x00) {
			softc->flags &= ~DA_FLAG_PROBED;
			disk_media_changed(softc->disk, M_NOWAIT);
		} else if (sense_key == SSD_KEY_UNIT_ATTENTION &&
		    asc == 0x3F && ascq == 0x03) {
			xpt_print(periph->path, "INQUIRY data has changed\n");
			softc->flags &= ~DA_FLAG_PROBED;
			dareprobe(periph);
			sense_flags |= SF_NO_PRINT;
		} else if (sense_key == SSD_KEY_NOT_READY &&
		    asc == 0x3a && (softc->flags & DA_FLAG_PACK_INVALID) == 0) {
			softc->flags |= DA_FLAG_PACK_INVALID;
			disk_media_gone(softc->disk, M_NOWAIT);
		}
	}
	if (error == ERESTART)
		return (ERESTART);

#ifdef CAM_IO_STATS
	switch (ccb->ccb_h.status & CAM_STATUS_MASK) {
	case CAM_CMD_TIMEOUT:
		softc->timeouts++;
		break;
	case CAM_REQ_ABORTED:
	case CAM_REQ_CMP_ERR:
	case CAM_REQ_TERMIO:
	case CAM_UNREC_HBA_ERROR:
	case CAM_DATA_RUN_ERR:
		softc->errors++;
		break;
	default:
		break;
	}
#endif

	/*
	 * XXX
	 * Until we have a better way of doing pack validation,
	 * don't treat UAs as errors.
	 */
	sense_flags |= SF_RETRY_UA;

	if (softc->quirks & DA_Q_RETRY_BUSY)
		sense_flags |= SF_RETRY_BUSY;
	return(cam_periph_error(ccb, cam_flags, sense_flags));
}

static void
damediapoll(void *arg)
{
	struct cam_periph *periph = arg;
	struct da_softc *softc = periph->softc;

	if (!cam_iosched_has_work_flags(softc->cam_iosched, DA_WORK_TUR) &&
	    (softc->flags & DA_FLAG_TUR_PENDING) == 0 &&
	    LIST_EMPTY(&softc->pending_ccbs)) {
		if (da_periph_acquire(periph, DA_REF_TUR) == 0) {
			cam_iosched_set_work_flags(softc->cam_iosched, DA_WORK_TUR);
			daschedule(periph);
		}
	}
	/* Queue us up again */
	if (da_poll_period != 0)
		callout_schedule(&softc->mediapoll_c, da_poll_period * hz);
}

static void
daprevent(struct cam_periph *periph, int action)
{
	struct	da_softc *softc;
	union	ccb *ccb;
	int	error;

	cam_periph_assert(periph, MA_OWNED);
	softc = (struct da_softc *)periph->softc;

	if (((action == PR_ALLOW)
	  && (softc->flags & DA_FLAG_PACK_LOCKED) == 0)
	 || ((action == PR_PREVENT)
	  && (softc->flags & DA_FLAG_PACK_LOCKED) != 0)) {
		return;
	}

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_prevent(&ccb->csio,
		     /*retries*/1,
		     /*cbcfp*/NULL,
		     MSG_SIMPLE_Q_TAG,
		     action,
		     SSD_FULL_SIZE,
		     5000);

	error = cam_periph_runccb(ccb, daerror, CAM_RETRY_SELTO,
	    SF_RETRY_UA | SF_NO_PRINT, softc->disk->d_devstat);

	if (error == 0) {
		if (action == PR_ALLOW)
			softc->flags &= ~DA_FLAG_PACK_LOCKED;
		else
			softc->flags |= DA_FLAG_PACK_LOCKED;
	}

	xpt_release_ccb(ccb);
}

static void
dasetgeom(struct cam_periph *periph, uint32_t block_len, uint64_t maxsector,
	  struct scsi_read_capacity_data_long *rcaplong, size_t rcap_len)
{
	struct ccb_calc_geometry ccg;
	struct da_softc *softc;
	struct disk_params *dp;
	u_int lbppbe, lalba;
	int error;

	softc = (struct da_softc *)periph->softc;

	dp = &softc->params;
	dp->secsize = block_len;
	dp->sectors = maxsector + 1;
	if (rcaplong != NULL) {
		lbppbe = rcaplong->prot_lbppbe & SRC16_LBPPBE;
		lalba = scsi_2btoul(rcaplong->lalba_lbp);
		lalba &= SRC16_LALBA_A;
	} else {
		lbppbe = 0;
		lalba = 0;
	}

	if (lbppbe > 0) {
		dp->stripesize = block_len << lbppbe;
		dp->stripeoffset = (dp->stripesize - block_len * lalba) %
		    dp->stripesize;
	} else if (softc->quirks & DA_Q_4K) {
		dp->stripesize = 4096;
		dp->stripeoffset = 0;
	} else if (softc->unmap_gran != 0) {
		dp->stripesize = block_len * softc->unmap_gran;
		dp->stripeoffset = (dp->stripesize - block_len *
		    softc->unmap_gran_align) % dp->stripesize;
	} else {
		dp->stripesize = 0;
		dp->stripeoffset = 0;
	}
	/*
	 * Have the controller provide us with a geometry
	 * for this disk.  The only time the geometry
	 * matters is when we boot and the controller
	 * is the only one knowledgeable enough to come
	 * up with something that will make this a bootable
	 * device.
	 */
	xpt_setup_ccb(&ccg.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
	ccg.ccb_h.func_code = XPT_CALC_GEOMETRY;
	ccg.block_size = dp->secsize;
	ccg.volume_size = dp->sectors;
	ccg.heads = 0;
	ccg.secs_per_track = 0;
	ccg.cylinders = 0;
	xpt_action((union ccb*)&ccg);
	if ((ccg.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		/*
		 * We don't know what went wrong here- but just pick
		 * a geometry so we don't have nasty things like divide
		 * by zero.
		 */
		dp->heads = 255;
		dp->secs_per_track = 255;
		dp->cylinders = dp->sectors / (255 * 255);
		if (dp->cylinders == 0) {
			dp->cylinders = 1;
		}
	} else {
		dp->heads = ccg.heads;
		dp->secs_per_track = ccg.secs_per_track;
		dp->cylinders = ccg.cylinders;
	}

	/*
	 * If the user supplied a read capacity buffer, and if it is
	 * different than the previous buffer, update the data in the EDT.
	 * If it's the same, we don't bother.  This avoids sending an
	 * update every time someone opens this device.
	 */
	if ((rcaplong != NULL)
	 && (bcmp(rcaplong, &softc->rcaplong,
		  min(sizeof(softc->rcaplong), rcap_len)) != 0)) {
		struct ccb_dev_advinfo cdai;

		xpt_setup_ccb(&cdai.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
		cdai.ccb_h.func_code = XPT_DEV_ADVINFO;
		cdai.buftype = CDAI_TYPE_RCAPLONG;
		cdai.flags = CDAI_FLAG_STORE;
		cdai.bufsiz = rcap_len;
		cdai.buf = (uint8_t *)rcaplong;
		xpt_action((union ccb *)&cdai);
		if ((cdai.ccb_h.status & CAM_DEV_QFRZN) != 0)
			cam_release_devq(cdai.ccb_h.path, 0, 0, 0, FALSE);
		if (cdai.ccb_h.status != CAM_REQ_CMP) {
			xpt_print(periph->path, "%s: failed to set read "
				  "capacity advinfo\n", __func__);
			/* Use cam_error_print() to decode the status */
			cam_error_print((union ccb *)&cdai, CAM_ESF_CAM_STATUS,
					CAM_EPF_ALL);
		} else {
			bcopy(rcaplong, &softc->rcaplong,
			      min(sizeof(softc->rcaplong), rcap_len));
		}
	}

	softc->disk->d_sectorsize = softc->params.secsize;
	softc->disk->d_mediasize = softc->params.secsize * (off_t)softc->params.sectors;
	softc->disk->d_stripesize = softc->params.stripesize;
	softc->disk->d_stripeoffset = softc->params.stripeoffset;
	/* XXX: these are not actually "firmware" values, so they may be wrong */
	softc->disk->d_fwsectors = softc->params.secs_per_track;
	softc->disk->d_fwheads = softc->params.heads;
	softc->disk->d_devstat->block_size = softc->params.secsize;
	softc->disk->d_devstat->flags &= ~DEVSTAT_BS_UNAVAILABLE;

	error = disk_resize(softc->disk, M_NOWAIT);
	if (error != 0)
		xpt_print(periph->path, "disk_resize(9) failed, error = %d\n", error);
}

static void
dasendorderedtag(void *arg)
{
	struct cam_periph *periph = arg;
	struct da_softc *softc = periph->softc;

	cam_periph_assert(periph, MA_OWNED);
	if (da_send_ordered) {
		if (!LIST_EMPTY(&softc->pending_ccbs)) {
			if ((softc->flags & DA_FLAG_WAS_OTAG) == 0)
				softc->flags |= DA_FLAG_NEED_OTAG;
			softc->flags &= ~DA_FLAG_WAS_OTAG;
		}
	}

	/* Queue us up again */
	callout_reset(&softc->sendordered_c,
	    (da_default_timeout * hz) / DA_ORDEREDTAG_INTERVAL,
	    dasendorderedtag, periph);
}

/*
 * Step through all DA peripheral drivers, and if the device is still open,
 * sync the disk cache to physical media.
 */
static void
dashutdown(void * arg, int howto)
{
	struct cam_periph *periph;
	struct da_softc *softc;
	union ccb *ccb;
	int error;

	CAM_PERIPH_FOREACH(periph, &dadriver) {
		softc = (struct da_softc *)periph->softc;
		if (SCHEDULER_STOPPED()) {
			/* If we paniced with the lock held, do not recurse. */
			if (!cam_periph_owned(periph) &&
			    (softc->flags & DA_FLAG_OPEN)) {
				dadump(softc->disk, NULL, 0, 0, 0);
			}
			continue;
		}
		cam_periph_lock(periph);

		/*
		 * We only sync the cache if the drive is still open, and
		 * if the drive is capable of it..
		 */
		if (((softc->flags & DA_FLAG_OPEN) == 0)
		 || (softc->quirks & DA_Q_NO_SYNC_CACHE)) {
			cam_periph_unlock(periph);
			continue;
		}

		ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
		scsi_synchronize_cache(&ccb->csio,
				       /*retries*/0,
				       /*cbfcnp*/NULL,
				       MSG_SIMPLE_Q_TAG,
				       /*begin_lba*/0, /* whole disk */
				       /*lb_count*/0,
				       SSD_FULL_SIZE,
				       60 * 60 * 1000);

		error = cam_periph_runccb(ccb, daerror, /*cam_flags*/0,
		    /*sense_flags*/ SF_NO_RECOVERY | SF_NO_RETRY | SF_QUIET_IR,
		    softc->disk->d_devstat);
		if (error != 0)
			xpt_print(periph->path, "Synchronize cache failed\n");
		xpt_release_ccb(ccb);
		cam_periph_unlock(periph);
	}
}

#else /* !_KERNEL */

/*
 * XXX These are only left out of the kernel build to silence warnings.  If,
 * for some reason these functions are used in the kernel, the ifdefs should
 * be moved so they are included both in the kernel and userland.
 */
void
scsi_format_unit(struct ccb_scsiio *csio, u_int32_t retries,
		 void (*cbfcnp)(struct cam_periph *, union ccb *),
		 u_int8_t tag_action, u_int8_t byte2, u_int16_t ileave,
		 u_int8_t *data_ptr, u_int32_t dxfer_len, u_int8_t sense_len,
		 u_int32_t timeout)
{
	struct scsi_format_unit *scsi_cmd;

	scsi_cmd = (struct scsi_format_unit *)&csio->cdb_io.cdb_bytes;
	scsi_cmd->opcode = FORMAT_UNIT;
	scsi_cmd->byte2 = byte2;
	scsi_ulto2b(ileave, scsi_cmd->interleave);

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ (dxfer_len > 0) ? CAM_DIR_OUT : CAM_DIR_NONE,
		      tag_action,
		      data_ptr,
		      dxfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_read_defects(struct ccb_scsiio *csio, uint32_t retries,
		  void (*cbfcnp)(struct cam_periph *, union ccb *),
		  uint8_t tag_action, uint8_t list_format,
		  uint32_t addr_desc_index, uint8_t *data_ptr,
		  uint32_t dxfer_len, int minimum_cmd_size,
		  uint8_t sense_len, uint32_t timeout)
{
	uint8_t cdb_len;

	/*
	 * These conditions allow using the 10 byte command.  Otherwise we
	 * need to use the 12 byte command.
	 */
	if ((minimum_cmd_size <= 10)
	 && (addr_desc_index == 0)
	 && (dxfer_len <= SRDD10_MAX_LENGTH)) {
		struct scsi_read_defect_data_10 *cdb10;

		cdb10 = (struct scsi_read_defect_data_10 *)
			&csio->cdb_io.cdb_bytes;

		cdb_len = sizeof(*cdb10);
		bzero(cdb10, cdb_len);
                cdb10->opcode = READ_DEFECT_DATA_10;
                cdb10->format = list_format;
                scsi_ulto2b(dxfer_len, cdb10->alloc_length);
	} else {
		struct scsi_read_defect_data_12 *cdb12;

		cdb12 = (struct scsi_read_defect_data_12 *)
			&csio->cdb_io.cdb_bytes;

		cdb_len = sizeof(*cdb12);
		bzero(cdb12, cdb_len);
                cdb12->opcode = READ_DEFECT_DATA_12;
                cdb12->format = list_format;
                scsi_ulto4b(dxfer_len, cdb12->alloc_length);
		scsi_ulto4b(addr_desc_index, cdb12->address_descriptor_index);
	}

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ CAM_DIR_IN,
		      tag_action,
		      data_ptr,
		      dxfer_len,
		      sense_len,
		      cdb_len,
		      timeout);
}

void
scsi_sanitize(struct ccb_scsiio *csio, u_int32_t retries,
	      void (*cbfcnp)(struct cam_periph *, union ccb *),
	      u_int8_t tag_action, u_int8_t byte2, u_int16_t control,
	      u_int8_t *data_ptr, u_int32_t dxfer_len, u_int8_t sense_len,
	      u_int32_t timeout)
{
	struct scsi_sanitize *scsi_cmd;

	scsi_cmd = (struct scsi_sanitize *)&csio->cdb_io.cdb_bytes;
	scsi_cmd->opcode = SANITIZE;
	scsi_cmd->byte2 = byte2;
	scsi_cmd->control = control;
	scsi_ulto2b(dxfer_len, scsi_cmd->length);

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ (dxfer_len > 0) ? CAM_DIR_OUT : CAM_DIR_NONE,
		      tag_action,
		      data_ptr,
		      dxfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

#endif /* _KERNEL */

void
scsi_zbc_out(struct ccb_scsiio *csio, uint32_t retries,
	     void (*cbfcnp)(struct cam_periph *, union ccb *),
	     uint8_t tag_action, uint8_t service_action, uint64_t zone_id,
	     uint8_t zone_flags, uint8_t *data_ptr, uint32_t dxfer_len,
	     uint8_t sense_len, uint32_t timeout)
{
	struct scsi_zbc_out *scsi_cmd;

	scsi_cmd = (struct scsi_zbc_out *)&csio->cdb_io.cdb_bytes;
	scsi_cmd->opcode = ZBC_OUT;
	scsi_cmd->service_action = service_action;
	scsi_u64to8b(zone_id, scsi_cmd->zone_id);
	scsi_cmd->zone_flags = zone_flags;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ (dxfer_len > 0) ? CAM_DIR_OUT : CAM_DIR_NONE,
		      tag_action,
		      data_ptr,
		      dxfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_zbc_in(struct ccb_scsiio *csio, uint32_t retries,
	    void (*cbfcnp)(struct cam_periph *, union ccb *),
	    uint8_t tag_action, uint8_t service_action, uint64_t zone_start_lba,
	    uint8_t zone_options, uint8_t *data_ptr, uint32_t dxfer_len,
	    uint8_t sense_len, uint32_t timeout)
{
	struct scsi_zbc_in *scsi_cmd;

	scsi_cmd = (struct scsi_zbc_in *)&csio->cdb_io.cdb_bytes;
	scsi_cmd->opcode = ZBC_IN;
	scsi_cmd->service_action = service_action;
	scsi_ulto4b(dxfer_len, scsi_cmd->length);
	scsi_u64to8b(zone_start_lba, scsi_cmd->zone_start_lba);
	scsi_cmd->zone_options = zone_options;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ (dxfer_len > 0) ? CAM_DIR_IN : CAM_DIR_NONE,
		      tag_action,
		      data_ptr,
		      dxfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);

}

int
scsi_ata_zac_mgmt_out(struct ccb_scsiio *csio, uint32_t retries,
		      void (*cbfcnp)(struct cam_periph *, union ccb *),
		      uint8_t tag_action, int use_ncq,
		      uint8_t zm_action, uint64_t zone_id, uint8_t zone_flags,
		      uint8_t *data_ptr, uint32_t dxfer_len,
		      uint8_t *cdb_storage, size_t cdb_storage_len,
		      uint8_t sense_len, uint32_t timeout)
{
	uint8_t command_out, protocol, ata_flags;
	uint16_t features_out;
	uint32_t sectors_out, auxiliary;
	int retval;

	retval = 0;

	if (use_ncq == 0) {
		command_out = ATA_ZAC_MANAGEMENT_OUT;
		features_out = (zm_action & 0xf) | (zone_flags << 8);
		ata_flags = AP_FLAG_BYT_BLOK_BLOCKS;
		if (dxfer_len == 0) {
			protocol = AP_PROTO_NON_DATA;
			ata_flags |= AP_FLAG_TLEN_NO_DATA;
			sectors_out = 0;
		} else {
			protocol = AP_PROTO_DMA;
			ata_flags |= AP_FLAG_TLEN_SECT_CNT |
				     AP_FLAG_TDIR_TO_DEV;
			sectors_out = ((dxfer_len >> 9) & 0xffff);
		}
		auxiliary = 0;
	} else {
		ata_flags = AP_FLAG_BYT_BLOK_BLOCKS;
		if (dxfer_len == 0) {
			command_out = ATA_NCQ_NON_DATA;
			features_out = ATA_NCQ_ZAC_MGMT_OUT;
			/*
			 * We're assuming the SCSI to ATA translation layer
			 * will set the NCQ tag number in the tag field.
			 * That isn't clear from the SAT-4 spec (as of rev 05).
			 */
			sectors_out = 0;
			ata_flags |= AP_FLAG_TLEN_NO_DATA;
		} else {
			command_out = ATA_SEND_FPDMA_QUEUED;
			/*
			 * Note that we're defaulting to normal priority,
			 * and assuming that the SCSI to ATA translation
			 * layer will insert the NCQ tag number in the tag
			 * field.  That isn't clear in the SAT-4 spec (as
			 * of rev 05).
			 */
			sectors_out = ATA_SFPDMA_ZAC_MGMT_OUT << 8;

			ata_flags |= AP_FLAG_TLEN_FEAT |
				     AP_FLAG_TDIR_TO_DEV;

			/*
			 * For SEND FPDMA QUEUED, the transfer length is
			 * encoded in the FEATURE register, and 0 means
			 * that 65536 512 byte blocks are to be tranferred.
			 * In practice, it seems unlikely that we'll see
			 * a transfer that large, and it may confuse the
			 * the SAT layer, because generally that means that
			 * 0 bytes should be transferred.
			 */
			if (dxfer_len == (65536 * 512)) {
				features_out = 0;
			} else if (dxfer_len <= (65535 * 512)) {
				features_out = ((dxfer_len >> 9) & 0xffff);
			} else {
				/* The transfer is too big. */
				retval = 1;
				goto bailout;
			}

		}

		auxiliary = (zm_action & 0xf) | (zone_flags << 8);
		protocol = AP_PROTO_FPDMA;
	}

	protocol |= AP_EXTEND;

	retval = scsi_ata_pass(csio,
	    retries,
	    cbfcnp,
	    /*flags*/ (dxfer_len > 0) ? CAM_DIR_OUT : CAM_DIR_NONE,
	    tag_action,
	    /*protocol*/ protocol,
	    /*ata_flags*/ ata_flags,
	    /*features*/ features_out,
	    /*sector_count*/ sectors_out,
	    /*lba*/ zone_id,
	    /*command*/ command_out,
	    /*device*/ 0,
	    /*icc*/ 0,
	    /*auxiliary*/ auxiliary,
	    /*control*/ 0,
	    /*data_ptr*/ data_ptr,
	    /*dxfer_len*/ dxfer_len,
	    /*cdb_storage*/ cdb_storage,
	    /*cdb_storage_len*/ cdb_storage_len,
	    /*minimum_cmd_size*/ 0,
	    /*sense_len*/ SSD_FULL_SIZE,
	    /*timeout*/ timeout);

bailout:

	return (retval);
}

int
scsi_ata_zac_mgmt_in(struct ccb_scsiio *csio, uint32_t retries,
		     void (*cbfcnp)(struct cam_periph *, union ccb *),
		     uint8_t tag_action, int use_ncq,
		     uint8_t zm_action, uint64_t zone_id, uint8_t zone_flags,
		     uint8_t *data_ptr, uint32_t dxfer_len,
		     uint8_t *cdb_storage, size_t cdb_storage_len,
		     uint8_t sense_len, uint32_t timeout)
{
	uint8_t command_out, protocol;
	uint16_t features_out, sectors_out;
	uint32_t auxiliary;
	int ata_flags;
	int retval;

	retval = 0;
	ata_flags = AP_FLAG_TDIR_FROM_DEV | AP_FLAG_BYT_BLOK_BLOCKS;

	if (use_ncq == 0) {
		command_out = ATA_ZAC_MANAGEMENT_IN;
		/* XXX KDM put a macro here */
		features_out = (zm_action & 0xf) | (zone_flags << 8);
		sectors_out = dxfer_len >> 9; /* XXX KDM macro */
		protocol = AP_PROTO_DMA;
		ata_flags |= AP_FLAG_TLEN_SECT_CNT;
		auxiliary = 0;
	} else {
		ata_flags |= AP_FLAG_TLEN_FEAT;

		command_out = ATA_RECV_FPDMA_QUEUED;
		sectors_out = ATA_RFPDMA_ZAC_MGMT_IN << 8;

		/*
		 * For RECEIVE FPDMA QUEUED, the transfer length is
		 * encoded in the FEATURE register, and 0 means
		 * that 65536 512 byte blocks are to be tranferred.
		 * In practice, it seems unlikely that we'll see
		 * a transfer that large, and it may confuse the
		 * the SAT layer, because generally that means that
		 * 0 bytes should be transferred.
		 */
		if (dxfer_len == (65536 * 512)) {
			features_out = 0;
		} else if (dxfer_len <= (65535 * 512)) {
			features_out = ((dxfer_len >> 9) & 0xffff);
		} else {
			/* The transfer is too big. */
			retval = 1;
			goto bailout;
		}
		auxiliary = (zm_action & 0xf) | (zone_flags << 8),
		protocol = AP_PROTO_FPDMA;
	}

	protocol |= AP_EXTEND;

	retval = scsi_ata_pass(csio,
	    retries,
	    cbfcnp,
	    /*flags*/ CAM_DIR_IN,
	    tag_action,
	    /*protocol*/ protocol,
	    /*ata_flags*/ ata_flags,
	    /*features*/ features_out,
	    /*sector_count*/ sectors_out,
	    /*lba*/ zone_id,
	    /*command*/ command_out,
	    /*device*/ 0,
	    /*icc*/ 0,
	    /*auxiliary*/ auxiliary,
	    /*control*/ 0,
	    /*data_ptr*/ data_ptr,
	    /*dxfer_len*/ (dxfer_len >> 9) * 512, /* XXX KDM */
	    /*cdb_storage*/ cdb_storage,
	    /*cdb_storage_len*/ cdb_storage_len,
	    /*minimum_cmd_size*/ 0,
	    /*sense_len*/ SSD_FULL_SIZE,
	    /*timeout*/ timeout);

bailout:
	return (retval);
}
