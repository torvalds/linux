/*-
 * Implementation of the SCSI Transport
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998, 1999 Justin T. Gibbs.
 * Copyright (c) 1997, 1998, 1999 Kenneth D. Merry.
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
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/md5.h>
#include <sys/sbuf.h>

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_queue.h>
#include <cam/cam_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_xpt_internal.h>
#include <cam/cam_debug.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_pass.h>
#include <machine/stdarg.h>	/* for xpt_print below */
#include "opt_cam.h"

struct scsi_quirk_entry {
	struct scsi_inquiry_pattern inq_pat;
	u_int8_t quirks;
#define	CAM_QUIRK_NOLUNS	0x01
#define	CAM_QUIRK_NOVPDS	0x02
#define	CAM_QUIRK_HILUNS	0x04
#define	CAM_QUIRK_NOHILUNS	0x08
#define	CAM_QUIRK_NORPTLUNS	0x10
	u_int mintags;
	u_int maxtags;
};
#define SCSI_QUIRK(dev)	((struct scsi_quirk_entry *)((dev)->quirk))

static int cam_srch_hi = 0;
static int sysctl_cam_search_luns(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_kern_cam, OID_AUTO, cam_srch_hi, CTLTYPE_INT | CTLFLAG_RWTUN, 0, 0,
    sysctl_cam_search_luns, "I",
    "allow search above LUN 7 for SCSI3 and greater devices");

#define	CAM_SCSI2_MAXLUN	8
#define	CAM_CAN_GET_SIMPLE_LUN(x, i)				\
	((((x)->luns[i].lundata[0] & RPL_LUNDATA_ATYP_MASK) ==	\
	RPL_LUNDATA_ATYP_PERIPH) ||				\
	(((x)->luns[i].lundata[0] & RPL_LUNDATA_ATYP_MASK) ==	\
	RPL_LUNDATA_ATYP_FLAT))
#define	CAM_GET_SIMPLE_LUN(lp, i, lval)					\
	if (((lp)->luns[(i)].lundata[0] & RPL_LUNDATA_ATYP_MASK) == 	\
	    RPL_LUNDATA_ATYP_PERIPH) {					\
		(lval) = (lp)->luns[(i)].lundata[1];			\
	} else {							\
		(lval) = (lp)->luns[(i)].lundata[0];			\
		(lval) &= RPL_LUNDATA_FLAT_LUN_MASK;			\
		(lval) <<= 8;						\
		(lval) |=  (lp)->luns[(i)].lundata[1];			\
	}
#define	CAM_GET_LUN(lp, i, lval)					\
	(lval) = scsi_8btou64((lp)->luns[(i)].lundata);			\
	(lval) = CAM_EXTLUN_BYTE_SWIZZLE(lval);

/*
 * If we're not quirked to search <= the first 8 luns
 * and we are either quirked to search above lun 8,
 * or we're > SCSI-2 and we've enabled hilun searching,
 * or we're > SCSI-2 and the last lun was a success,
 * we can look for luns above lun 8.
 */
#define	CAN_SRCH_HI_SPARSE(dv)					\
  (((SCSI_QUIRK(dv)->quirks & CAM_QUIRK_NOHILUNS) == 0) 	\
  && ((SCSI_QUIRK(dv)->quirks & CAM_QUIRK_HILUNS)		\
  || (SID_ANSI_REV(&dv->inq_data) > SCSI_REV_2 && cam_srch_hi)))

#define	CAN_SRCH_HI_DENSE(dv)					\
  (((SCSI_QUIRK(dv)->quirks & CAM_QUIRK_NOHILUNS) == 0) 	\
  && ((SCSI_QUIRK(dv)->quirks & CAM_QUIRK_HILUNS)		\
  || (SID_ANSI_REV(&dv->inq_data) > SCSI_REV_2)))

static periph_init_t probe_periph_init;

static struct periph_driver probe_driver =
{
	probe_periph_init, "probe",
	TAILQ_HEAD_INITIALIZER(probe_driver.units), /* generation */ 0,
	CAM_PERIPH_DRV_EARLY
};

PERIPHDRIVER_DECLARE(probe, probe_driver);

typedef enum {
	PROBE_TUR,
	PROBE_INQUIRY,	/* this counts as DV0 for Basic Domain Validation */
	PROBE_FULL_INQUIRY,
	PROBE_REPORT_LUNS,
	PROBE_MODE_SENSE,
	PROBE_SUPPORTED_VPD_LIST,
	PROBE_DEVICE_ID,
	PROBE_EXTENDED_INQUIRY,
	PROBE_SERIAL_NUM,
	PROBE_TUR_FOR_NEGOTIATION,
	PROBE_INQUIRY_BASIC_DV1,
	PROBE_INQUIRY_BASIC_DV2,
	PROBE_DV_EXIT,
	PROBE_DONE,
	PROBE_INVALID
} probe_action;

static char *probe_action_text[] = {
	"PROBE_TUR",
	"PROBE_INQUIRY",
	"PROBE_FULL_INQUIRY",
	"PROBE_REPORT_LUNS",
	"PROBE_MODE_SENSE",
	"PROBE_SUPPORTED_VPD_LIST",
	"PROBE_DEVICE_ID",
	"PROBE_EXTENDED_INQUIRY",
	"PROBE_SERIAL_NUM",
	"PROBE_TUR_FOR_NEGOTIATION",
	"PROBE_INQUIRY_BASIC_DV1",
	"PROBE_INQUIRY_BASIC_DV2",
	"PROBE_DV_EXIT",
	"PROBE_DONE",
	"PROBE_INVALID"
};

#define PROBE_SET_ACTION(softc, newaction)	\
do {									\
	char **text;							\
	text = probe_action_text;					\
	CAM_DEBUG((softc)->periph->path, CAM_DEBUG_PROBE,		\
	    ("Probe %s to %s\n", text[(softc)->action],			\
	    text[(newaction)]));					\
	(softc)->action = (newaction);					\
} while(0)

typedef enum {
	PROBE_INQUIRY_CKSUM	= 0x01,
	PROBE_SERIAL_CKSUM	= 0x02,
	PROBE_NO_ANNOUNCE	= 0x04,
	PROBE_EXTLUN		= 0x08
} probe_flags;

typedef struct {
	TAILQ_HEAD(, ccb_hdr) request_ccbs;
	probe_action	action;
	union ccb	saved_ccb;
	probe_flags	flags;
	MD5_CTX		context;
	u_int8_t	digest[16];
	struct cam_periph *periph;
} probe_softc;

static const char quantum[] = "QUANTUM";
static const char sony[] = "SONY";
static const char west_digital[] = "WDIGTL";
static const char samsung[] = "SAMSUNG";
static const char seagate[] = "SEAGATE";
static const char microp[] = "MICROP";

static struct scsi_quirk_entry scsi_quirk_table[] =
{
	{
		/* Reports QUEUE FULL for temporary resource shortages */
		{ T_DIRECT, SIP_MEDIA_FIXED, quantum, "XP39100*", "*" },
		/*quirks*/0, /*mintags*/24, /*maxtags*/32
	},
	{
		/* Reports QUEUE FULL for temporary resource shortages */
		{ T_DIRECT, SIP_MEDIA_FIXED, quantum, "XP34550*", "*" },
		/*quirks*/0, /*mintags*/24, /*maxtags*/32
	},
	{
		/* Reports QUEUE FULL for temporary resource shortages */
		{ T_DIRECT, SIP_MEDIA_FIXED, quantum, "XP32275*", "*" },
		/*quirks*/0, /*mintags*/24, /*maxtags*/32
	},
	{
		/* Broken tagged queuing drive */
		{ T_DIRECT, SIP_MEDIA_FIXED, microp, "4421-07*", "*" },
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
	},
	{
		/* Broken tagged queuing drive */
		{ T_DIRECT, SIP_MEDIA_FIXED, "HP", "C372*", "*" },
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
	},
	{
		/* Broken tagged queuing drive */
		{ T_DIRECT, SIP_MEDIA_FIXED, microp, "3391*", "x43h" },
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
	},
	{
		/*
		 * Unfortunately, the Quantum Atlas III has the same
		 * problem as the Atlas II drives above.
		 * Reported by: "Johan Granlund" <johan@granlund.nu>
		 *
		 * For future reference, the drive with the problem was:
		 * QUANTUM QM39100TD-SW N1B0
		 *
		 * It's possible that Quantum will fix the problem in later
		 * firmware revisions.  If that happens, the quirk entry
		 * will need to be made specific to the firmware revisions
		 * with the problem.
		 *
		 */
		/* Reports QUEUE FULL for temporary resource shortages */
		{ T_DIRECT, SIP_MEDIA_FIXED, quantum, "QM39100*", "*" },
		/*quirks*/0, /*mintags*/24, /*maxtags*/32
	},
	{
		/*
		 * 18 Gig Atlas III, same problem as the 9G version.
		 * Reported by: Andre Albsmeier
		 *		<andre.albsmeier@mchp.siemens.de>
		 *
		 * For future reference, the drive with the problem was:
		 * QUANTUM QM318000TD-S N491
		 */
		/* Reports QUEUE FULL for temporary resource shortages */
		{ T_DIRECT, SIP_MEDIA_FIXED, quantum, "QM318000*", "*" },
		/*quirks*/0, /*mintags*/24, /*maxtags*/32
	},
	{
		/*
		 * Broken tagged queuing drive
		 * Reported by: Bret Ford <bford@uop.cs.uop.edu>
		 *         and: Martin Renters <martin@tdc.on.ca>
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, seagate, "ST410800*", "71*" },
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
	},
		/*
		 * The Seagate Medalist Pro drives have very poor write
		 * performance with anything more than 2 tags.
		 *
		 * Reported by:  Paul van der Zwan <paulz@trantor.xs4all.nl>
		 * Drive:  <SEAGATE ST36530N 1444>
		 *
		 * Reported by:  Jeremy Lea <reg@shale.csir.co.za>
		 * Drive:  <SEAGATE ST34520W 1281>
		 *
		 * No one has actually reported that the 9G version
		 * (ST39140*) of the Medalist Pro has the same problem, but
		 * we're assuming that it does because the 4G and 6.5G
		 * versions of the drive are broken.
		 */
	{
		{ T_DIRECT, SIP_MEDIA_FIXED, seagate, "ST34520*", "*"},
		/*quirks*/0, /*mintags*/2, /*maxtags*/2
	},
	{
		{ T_DIRECT, SIP_MEDIA_FIXED, seagate, "ST36530*", "*"},
		/*quirks*/0, /*mintags*/2, /*maxtags*/2
	},
	{
		{ T_DIRECT, SIP_MEDIA_FIXED, seagate, "ST39140*", "*"},
		/*quirks*/0, /*mintags*/2, /*maxtags*/2
	},
	{
		/*
		 * Experiences command timeouts under load with a
		 * tag count higher than 55.
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, seagate, "ST3146855LW", "*"},
		/*quirks*/0, /*mintags*/2, /*maxtags*/55
	},
	{
		/*
		 * Slow when tagged queueing is enabled.  Write performance
		 * steadily drops off with more and more concurrent
		 * transactions.  Best sequential write performance with
		 * tagged queueing turned off and write caching turned on.
		 *
		 * PR:  kern/10398
		 * Submitted by:  Hideaki Okada <hokada@isl.melco.co.jp>
		 * Drive:  DCAS-34330 w/ "S65A" firmware.
		 *
		 * The drive with the problem had the "S65A" firmware
		 * revision, and has also been reported (by Stephen J.
		 * Roznowski <sjr@home.net>) for a drive with the "S61A"
		 * firmware revision.
		 *
		 * Although no one has reported problems with the 2 gig
		 * version of the DCAS drive, the assumption is that it
		 * has the same problems as the 4 gig version.  Therefore
		 * this quirk entries disables tagged queueing for all
		 * DCAS drives.
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "IBM", "DCAS*", "*" },
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
	},
	{
		/* Broken tagged queuing drive */
		{ T_DIRECT, SIP_MEDIA_REMOVABLE, "iomega", "jaz*", "*" },
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
	},
	{
		/* Broken tagged queuing drive */
		{ T_DIRECT, SIP_MEDIA_FIXED, "CONNER", "CFP2107*", "*" },
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
	},
	{
		/* This does not support other than LUN 0 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "VMware*", "*", "*" },
		CAM_QUIRK_NOLUNS, /*mintags*/2, /*maxtags*/255
	},
	{
		/*
		 * Broken tagged queuing drive.
		 * Submitted by:
		 * NAKAJI Hiroyuki <nakaji@zeisei.dpri.kyoto-u.ac.jp>
		 * in PR kern/9535
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, samsung, "WN34324U*", "*" },
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
	},
        {
		/*
		 * Slow when tagged queueing is enabled. (1.5MB/sec versus
		 * 8MB/sec.)
		 * Submitted by: Andrew Gallatin <gallatin@cs.duke.edu>
		 * Best performance with these drives is achieved with
		 * tagged queueing turned off, and write caching turned on.
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, west_digital, "WDE*", "*" },
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
        },
        {
		/*
		 * Slow when tagged queueing is enabled. (1.5MB/sec versus
		 * 8MB/sec.)
		 * Submitted by: Andrew Gallatin <gallatin@cs.duke.edu>
		 * Best performance with these drives is achieved with
		 * tagged queueing turned off, and write caching turned on.
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, west_digital, "ENTERPRISE", "*" },
		/*quirks*/0, /*mintags*/0, /*maxtags*/0
        },
	{
		/*
		 * Doesn't handle queue full condition correctly,
		 * so we need to limit maxtags to what the device
		 * can handle instead of determining this automatically.
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, samsung, "WN321010S*", "*" },
		/*quirks*/0, /*mintags*/2, /*maxtags*/32
	},
	{
		/* Really only one LUN */
		{ T_ENCLOSURE, SIP_MEDIA_FIXED, "SUN", "SENA", "*" },
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/* I can't believe we need a quirk for DPT volumes. */
		{ T_ANY, SIP_MEDIA_FIXED|SIP_MEDIA_REMOVABLE, "DPT", "*", "*" },
		CAM_QUIRK_NOLUNS,
		/*mintags*/0, /*maxtags*/255
	},
	{
		/*
		 * Many Sony CDROM drives don't like multi-LUN probing.
		 */
		{ T_CDROM, SIP_MEDIA_REMOVABLE, sony, "CD-ROM CDU*", "*" },
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/*
		 * This drive doesn't like multiple LUN probing.
		 * Submitted by:  Parag Patel <parag@cgt.com>
		 */
		{ T_WORM, SIP_MEDIA_REMOVABLE, sony, "CD-R   CDU9*", "*" },
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		{ T_WORM, SIP_MEDIA_REMOVABLE, "YAMAHA", "CDR100*", "*" },
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/*
		 * The 8200 doesn't like multi-lun probing, and probably
		 * don't like serial number requests either.
		 */
		{
			T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "EXABYTE",
			"EXB-8200*", "*"
		},
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/*
		 * Let's try the same as above, but for a drive that says
		 * it's an IPL-6860 but is actually an EXB 8200.
		 */
		{
			T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "EXABYTE",
			"IPL-6860*", "*"
		},
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/*
		 * These Hitachi drives don't like multi-lun probing.
		 * The PR submitter has a DK319H, but says that the Linux
		 * kernel has a similar work-around for the DK312 and DK314,
		 * so all DK31* drives are quirked here.
		 * PR:            misc/18793
		 * Submitted by:  Paul Haddad <paul@pth.com>
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "HITACHI", "DK31*", "*" },
		CAM_QUIRK_NOLUNS, /*mintags*/2, /*maxtags*/255
	},
	{
		/*
		 * The Hitachi CJ series with J8A8 firmware apparently has
		 * problems with tagged commands.
		 * PR: 23536
		 * Reported by: amagai@nue.org
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "HITACHI", "DK32CJ*", "J8A8" },
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/*
		 * These are the large storage arrays.
		 * Submitted by:  William Carrel <william.carrel@infospace.com>
		 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "HITACHI", "OPEN*", "*" },
		CAM_QUIRK_HILUNS, 2, 1024
	},
	{
		/*
		 * This old revision of the TDC3600 is also SCSI-1, and
		 * hangs upon serial number probing.
		 */
		{
			T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "TANDBERG",
			" TDC 3600", "U07:"
		},
		CAM_QUIRK_NOVPDS, /*mintags*/0, /*maxtags*/0
	},
	{
		/*
		 * Would repond to all LUNs if asked for.
		 */
		{
			T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "CALIPER",
			"CP150", "*"
		},
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/*
		 * Would repond to all LUNs if asked for.
		 */
		{
			T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "KENNEDY",
			"96X2*", "*"
		},
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/* Submitted by: Matthew Dodd <winter@jurai.net> */
		{ T_PROCESSOR, SIP_MEDIA_FIXED, "Cabletrn", "EA41*", "*" },
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/* Submitted by: Matthew Dodd <winter@jurai.net> */
		{ T_PROCESSOR, SIP_MEDIA_FIXED, "CABLETRN", "EA41*", "*" },
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/* TeraSolutions special settings for TRC-22 RAID */
		{ T_DIRECT, SIP_MEDIA_FIXED, "TERASOLU", "TRC-22", "*" },
		  /*quirks*/0, /*mintags*/55, /*maxtags*/255
	},
	{
		/* Veritas Storage Appliance */
		{ T_DIRECT, SIP_MEDIA_FIXED, "VERITAS", "*", "*" },
		  CAM_QUIRK_HILUNS, /*mintags*/2, /*maxtags*/1024
	},
	{
		/*
		 * Would respond to all LUNs.  Device type and removable
		 * flag are jumper-selectable.
		 */
		{ T_ANY, SIP_MEDIA_REMOVABLE|SIP_MEDIA_FIXED, "MaxOptix",
		  "Tahiti 1", "*"
		},
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		/* EasyRAID E5A aka. areca ARC-6010 */
		{ T_DIRECT, SIP_MEDIA_FIXED, "easyRAID", "*", "*" },
		  CAM_QUIRK_NOHILUNS, /*mintags*/2, /*maxtags*/255
	},
	{
		{ T_ENCLOSURE, SIP_MEDIA_FIXED, "DP", "BACKPLANE", "*" },
		CAM_QUIRK_NOLUNS, /*mintags*/0, /*maxtags*/0
	},
	{
		{ T_DIRECT, SIP_MEDIA_REMOVABLE, "Garmin", "*", "*" },
		CAM_QUIRK_NORPTLUNS, /*mintags*/2, /*maxtags*/255
	},
	{
		{ T_DIRECT, SIP_MEDIA_REMOVABLE, "Generic", "STORAGE DEVICE*", "120?" },
		CAM_QUIRK_NORPTLUNS, /*mintags*/2, /*maxtags*/255
	},
	{
		{ T_DIRECT, SIP_MEDIA_REMOVABLE, "Generic", "MassStorageClass", "1533" },
		CAM_QUIRK_NORPTLUNS, /*mintags*/2, /*maxtags*/255
	},
	{
		/* Default tagged queuing parameters for all devices */
		{
		  T_ANY, SIP_MEDIA_REMOVABLE|SIP_MEDIA_FIXED,
		  /*vendor*/"*", /*product*/"*", /*revision*/"*"
		},
		/*quirks*/0, /*mintags*/2, /*maxtags*/255
	},
};

static cam_status	proberegister(struct cam_periph *periph,
				      void *arg);
static void	 probeschedule(struct cam_periph *probe_periph);
static void	 probestart(struct cam_periph *periph, union ccb *start_ccb);
static void	 proberequestdefaultnegotiation(struct cam_periph *periph);
static int       proberequestbackoff(struct cam_periph *periph,
				     struct cam_ed *device);
static void	 probedone(struct cam_periph *periph, union ccb *done_ccb);
static void	 probe_purge_old(struct cam_path *path,
				 struct scsi_report_luns_data *new,
				 probe_flags flags);
static void	 probecleanup(struct cam_periph *periph);
static void	 scsi_find_quirk(struct cam_ed *device);
static void	 scsi_scan_bus(struct cam_periph *periph, union ccb *ccb);
static void	 scsi_scan_lun(struct cam_periph *periph,
			       struct cam_path *path, cam_flags flags,
			       union ccb *ccb);
static void	 xptscandone(struct cam_periph *periph, union ccb *done_ccb);
static struct cam_ed *
		 scsi_alloc_device(struct cam_eb *bus, struct cam_et *target,
				   lun_id_t lun_id);
static void	 scsi_devise_transport(struct cam_path *path);
static void	 scsi_set_transfer_settings(struct ccb_trans_settings *cts,
					    struct cam_path *path,
					    int async_update);
static void	 scsi_toggle_tags(struct cam_path *path);
static void	 scsi_dev_async(u_int32_t async_code,
				struct cam_eb *bus,
				struct cam_et *target,
				struct cam_ed *device,
				void *async_arg);
static void	 scsi_action(union ccb *start_ccb);
static void	 scsi_announce_periph(struct cam_periph *periph);
static void	 scsi_announce_periph_sbuf(struct cam_periph *periph, struct sbuf *sb);
static void	 scsi_proto_announce(struct cam_ed *device);
static void	 scsi_proto_announce_sbuf(struct cam_ed *device,
					  struct sbuf *sb);
static void	 scsi_proto_denounce(struct cam_ed *device);
static void	 scsi_proto_denounce_sbuf(struct cam_ed *device,
					  struct sbuf *sb);
static void	 scsi_proto_debug_out(union ccb *ccb);
static void	 _scsi_announce_periph(struct cam_periph *, u_int *, u_int *, struct ccb_trans_settings *);

static struct xpt_xport_ops scsi_xport_ops = {
	.alloc_device = scsi_alloc_device,
	.action = scsi_action,
	.async = scsi_dev_async,
	.announce = scsi_announce_periph,
	.announce_sbuf = scsi_announce_periph_sbuf,
};
#define SCSI_XPT_XPORT(x, X)			\
static struct xpt_xport scsi_xport_ ## x = {	\
	.xport = XPORT_ ## X,			\
	.name = #x,				\
	.ops = &scsi_xport_ops,			\
};						\
CAM_XPT_XPORT(scsi_xport_ ## x);

SCSI_XPT_XPORT(spi, SPI);
SCSI_XPT_XPORT(sas, SAS);
SCSI_XPT_XPORT(fc, FC);
SCSI_XPT_XPORT(usb, USB);
SCSI_XPT_XPORT(iscsi, ISCSI);
SCSI_XPT_XPORT(srp, SRP);
SCSI_XPT_XPORT(ppb, PPB);

#undef SCSI_XPORT_XPORT

static struct xpt_proto_ops scsi_proto_ops = {
	.announce = scsi_proto_announce,
	.announce_sbuf = scsi_proto_announce_sbuf,
	.denounce = scsi_proto_denounce,
	.denounce_sbuf = scsi_proto_denounce_sbuf,
	.debug_out = scsi_proto_debug_out,
};
static struct xpt_proto scsi_proto = {
	.proto = PROTO_SCSI,
	.name = "scsi",
	.ops = &scsi_proto_ops,
};
CAM_XPT_PROTO(scsi_proto);

static void
probe_periph_init()
{
}

static cam_status
proberegister(struct cam_periph *periph, void *arg)
{
	union ccb *request_ccb;	/* CCB representing the probe request */
	probe_softc *softc;

	request_ccb = (union ccb *)arg;
	if (request_ccb == NULL) {
		printf("proberegister: no probe CCB, "
		       "can't register device\n");
		return(CAM_REQ_CMP_ERR);
	}

	softc = (probe_softc *)malloc(sizeof(*softc), M_CAMXPT, M_NOWAIT);

	if (softc == NULL) {
		printf("proberegister: Unable to probe new device. "
		       "Unable to allocate softc\n");
		return(CAM_REQ_CMP_ERR);
	}
	TAILQ_INIT(&softc->request_ccbs);
	TAILQ_INSERT_TAIL(&softc->request_ccbs, &request_ccb->ccb_h,
			  periph_links.tqe);
	softc->flags = 0;
	periph->softc = softc;
	softc->periph = periph;
	softc->action = PROBE_INVALID;
	if (cam_periph_acquire(periph) != 0)
		return (CAM_REQ_CMP_ERR);

	CAM_DEBUG(periph->path, CAM_DEBUG_PROBE, ("Probe started\n"));
	scsi_devise_transport(periph->path);

	/*
	 * Ensure we've waited at least a bus settle
	 * delay before attempting to probe the device.
	 * For HBAs that don't do bus resets, this won't make a difference.
	 */
	cam_periph_freeze_after_event(periph, &periph->path->bus->last_reset,
				      scsi_delay);
	probeschedule(periph);
	return(CAM_REQ_CMP);
}

static void
probeschedule(struct cam_periph *periph)
{
	struct ccb_pathinq cpi;
	union ccb *ccb;
	probe_softc *softc;

	softc = (probe_softc *)periph->softc;
	ccb = (union ccb *)TAILQ_FIRST(&softc->request_ccbs);

	xpt_path_inq(&cpi, periph->path);

	/*
	 * If a device has gone away and another device, or the same one,
	 * is back in the same place, it should have a unit attention
	 * condition pending.  It will not report the unit attention in
	 * response to an inquiry, which may leave invalid transfer
	 * negotiations in effect.  The TUR will reveal the unit attention
	 * condition.  Only send the TUR for lun 0, since some devices
	 * will get confused by commands other than inquiry to non-existent
	 * luns.  If you think a device has gone away start your scan from
	 * lun 0.  This will insure that any bogus transfer settings are
	 * invalidated.
	 *
	 * If we haven't seen the device before and the controller supports
	 * some kind of transfer negotiation, negotiate with the first
	 * sent command if no bus reset was performed at startup.  This
	 * ensures that the device is not confused by transfer negotiation
	 * settings left over by loader or BIOS action.
	 */
	if (((ccb->ccb_h.path->device->flags & CAM_DEV_UNCONFIGURED) == 0)
	 && (ccb->ccb_h.target_lun == 0)) {
		PROBE_SET_ACTION(softc, PROBE_TUR);
	} else if ((cpi.hba_inquiry & (PI_WIDE_32|PI_WIDE_16|PI_SDTR_ABLE)) != 0
	      && (cpi.hba_misc & PIM_NOBUSRESET) != 0) {
		proberequestdefaultnegotiation(periph);
		PROBE_SET_ACTION(softc, PROBE_INQUIRY);
	} else {
		PROBE_SET_ACTION(softc, PROBE_INQUIRY);
	}

	if (ccb->crcn.flags & CAM_EXPECT_INQ_CHANGE)
		softc->flags |= PROBE_NO_ANNOUNCE;
	else
		softc->flags &= ~PROBE_NO_ANNOUNCE;

	if (cpi.hba_misc & PIM_EXTLUNS)
		softc->flags |= PROBE_EXTLUN;
	else
		softc->flags &= ~PROBE_EXTLUN;

	xpt_schedule(periph, CAM_PRIORITY_XPT);
}

static void
probestart(struct cam_periph *periph, union ccb *start_ccb)
{
	/* Probe the device that our peripheral driver points to */
	struct ccb_scsiio *csio;
	probe_softc *softc;

	CAM_DEBUG(start_ccb->ccb_h.path, CAM_DEBUG_TRACE, ("probestart\n"));

	softc = (probe_softc *)periph->softc;
	csio = &start_ccb->csio;
again:

	switch (softc->action) {
	case PROBE_TUR:
	case PROBE_TUR_FOR_NEGOTIATION:
	case PROBE_DV_EXIT:
	{
		scsi_test_unit_ready(csio,
				     /*retries*/4,
				     probedone,
				     MSG_SIMPLE_Q_TAG,
				     SSD_FULL_SIZE,
				     /*timeout*/60000);
		break;
	}
	case PROBE_INQUIRY:
	case PROBE_FULL_INQUIRY:
	case PROBE_INQUIRY_BASIC_DV1:
	case PROBE_INQUIRY_BASIC_DV2:
	{
		u_int inquiry_len;
		struct scsi_inquiry_data *inq_buf;

		inq_buf = &periph->path->device->inq_data;

		/*
		 * If the device is currently configured, we calculate an
		 * MD5 checksum of the inquiry data, and if the serial number
		 * length is greater than 0, add the serial number data
		 * into the checksum as well.  Once the inquiry and the
		 * serial number check finish, we attempt to figure out
		 * whether we still have the same device.
		 */
		if (((periph->path->device->flags & CAM_DEV_UNCONFIGURED) == 0)
		 && ((softc->flags & PROBE_INQUIRY_CKSUM) == 0)) {

			MD5Init(&softc->context);
			MD5Update(&softc->context, (unsigned char *)inq_buf,
				  sizeof(struct scsi_inquiry_data));
			softc->flags |= PROBE_INQUIRY_CKSUM;
			if (periph->path->device->serial_num_len > 0) {
				MD5Update(&softc->context,
					  periph->path->device->serial_num,
					  periph->path->device->serial_num_len);
				softc->flags |= PROBE_SERIAL_CKSUM;
			}
			MD5Final(softc->digest, &softc->context);
		}

		if (softc->action == PROBE_INQUIRY)
			inquiry_len = SHORT_INQUIRY_LENGTH;
		else
			inquiry_len = SID_ADDITIONAL_LENGTH(inq_buf);

		/*
		 * Some parallel SCSI devices fail to send an
		 * ignore wide residue message when dealing with
		 * odd length inquiry requests.  Round up to be
		 * safe.
		 */
		inquiry_len = roundup2(inquiry_len, 2);

		if (softc->action == PROBE_INQUIRY_BASIC_DV1
		 || softc->action == PROBE_INQUIRY_BASIC_DV2) {
			inq_buf = malloc(inquiry_len, M_CAMXPT, M_NOWAIT);
		}
		if (inq_buf == NULL) {
			xpt_print(periph->path, "malloc failure- skipping Basic"
			    "Domain Validation\n");
			PROBE_SET_ACTION(softc, PROBE_DV_EXIT);
			scsi_test_unit_ready(csio,
					     /*retries*/4,
					     probedone,
					     MSG_SIMPLE_Q_TAG,
					     SSD_FULL_SIZE,
					     /*timeout*/60000);
			break;
		}
		scsi_inquiry(csio,
			     /*retries*/4,
			     probedone,
			     MSG_SIMPLE_Q_TAG,
			     (u_int8_t *)inq_buf,
			     inquiry_len,
			     /*evpd*/FALSE,
			     /*page_code*/0,
			     SSD_MIN_SIZE,
			     /*timeout*/60 * 1000);
		break;
	}
	case PROBE_REPORT_LUNS:
	{
		void *rp;

		rp = malloc(periph->path->target->rpl_size,
		    M_CAMXPT, M_NOWAIT | M_ZERO);
		if (rp == NULL) {
			struct scsi_inquiry_data *inq_buf;
			inq_buf = &periph->path->device->inq_data;
			xpt_print(periph->path,
			    "Unable to alloc report luns storage\n");
			if (INQ_DATA_TQ_ENABLED(inq_buf))
				PROBE_SET_ACTION(softc, PROBE_MODE_SENSE);
			else
				PROBE_SET_ACTION(softc,
				    PROBE_SUPPORTED_VPD_LIST);
			goto again;
		}
		scsi_report_luns(csio, 5, probedone, MSG_SIMPLE_Q_TAG,
		    RPL_REPORT_DEFAULT, rp, periph->path->target->rpl_size,
		    SSD_FULL_SIZE, 60000); break;
		break;
	}
	case PROBE_MODE_SENSE:
	{
		void  *mode_buf;
		int    mode_buf_len;

		mode_buf_len = sizeof(struct scsi_mode_header_6)
			     + sizeof(struct scsi_mode_blk_desc)
			     + sizeof(struct scsi_control_page);
		mode_buf = malloc(mode_buf_len, M_CAMXPT, M_NOWAIT);
		if (mode_buf != NULL) {
	                scsi_mode_sense(csio,
					/*retries*/4,
					probedone,
					MSG_SIMPLE_Q_TAG,
					/*dbd*/FALSE,
					SMS_PAGE_CTRL_CURRENT,
					SMS_CONTROL_MODE_PAGE,
					mode_buf,
					mode_buf_len,
					SSD_FULL_SIZE,
					/*timeout*/60000);
			break;
		}
		xpt_print(periph->path, "Unable to mode sense control page - "
		    "malloc failure\n");
		PROBE_SET_ACTION(softc, PROBE_SUPPORTED_VPD_LIST);
	}
	/* FALLTHROUGH */
	case PROBE_SUPPORTED_VPD_LIST:
	{
		struct scsi_vpd_supported_page_list *vpd_list;
		struct cam_ed *device;

		vpd_list = NULL;
		device = periph->path->device;

		if ((SCSI_QUIRK(device)->quirks & CAM_QUIRK_NOVPDS) == 0)
			vpd_list = malloc(sizeof(*vpd_list), M_CAMXPT,
			    M_NOWAIT | M_ZERO);

		if (vpd_list != NULL) {
			scsi_inquiry(csio,
				     /*retries*/4,
				     probedone,
				     MSG_SIMPLE_Q_TAG,
				     (u_int8_t *)vpd_list,
				     sizeof(*vpd_list),
				     /*evpd*/TRUE,
				     SVPD_SUPPORTED_PAGE_LIST,
				     SSD_MIN_SIZE,
				     /*timeout*/60 * 1000);
			break;
		}
done:
		/*
		 * We'll have to do without, let our probedone
		 * routine finish up for us.
		 */
		start_ccb->csio.data_ptr = NULL;
		cam_freeze_devq(periph->path);
		cam_periph_doacquire(periph);
		probedone(periph, start_ccb);
		return;
	}
	case PROBE_DEVICE_ID:
	{
		struct scsi_vpd_device_id *devid;

		devid = NULL;
		if (scsi_vpd_supported_page(periph, SVPD_DEVICE_ID))
			devid = malloc(SVPD_DEVICE_ID_MAX_SIZE, M_CAMXPT,
			    M_NOWAIT | M_ZERO);

		if (devid != NULL) {
			scsi_inquiry(csio,
				     /*retries*/4,
				     probedone,
				     MSG_SIMPLE_Q_TAG,
				     (uint8_t *)devid,
				     SVPD_DEVICE_ID_MAX_SIZE,
				     /*evpd*/TRUE,
				     SVPD_DEVICE_ID,
				     SSD_MIN_SIZE,
				     /*timeout*/60 * 1000);
			break;
		}
		goto done;
	}
	case PROBE_EXTENDED_INQUIRY:
	{
		struct scsi_vpd_extended_inquiry_data *ext_inq;

		ext_inq = NULL;
		if (scsi_vpd_supported_page(periph, SVPD_EXTENDED_INQUIRY_DATA))
			ext_inq = malloc(sizeof(*ext_inq), M_CAMXPT,
			    M_NOWAIT | M_ZERO);

		if (ext_inq != NULL) {
			scsi_inquiry(csio,
				     /*retries*/4,
				     probedone,
				     MSG_SIMPLE_Q_TAG,
				     (uint8_t *)ext_inq,
				     sizeof(*ext_inq),
				     /*evpd*/TRUE,
				     SVPD_EXTENDED_INQUIRY_DATA,
				     SSD_MIN_SIZE,
				     /*timeout*/60 * 1000);
			break;
		}
		/*
		 * We'll have to do without, let our probedone
		 * routine finish up for us.
		 */
		goto done;
	}
	case PROBE_SERIAL_NUM:
	{
		struct scsi_vpd_unit_serial_number *serial_buf;
		struct cam_ed* device;

		serial_buf = NULL;
		device = periph->path->device;
		if (device->serial_num != NULL) {
			free(device->serial_num, M_CAMXPT);
			device->serial_num = NULL;
			device->serial_num_len = 0;
		}

		if (scsi_vpd_supported_page(periph, SVPD_UNIT_SERIAL_NUMBER))
			serial_buf = (struct scsi_vpd_unit_serial_number *)
				malloc(sizeof(*serial_buf), M_CAMXPT,
				    M_NOWAIT|M_ZERO);

		if (serial_buf != NULL) {
			scsi_inquiry(csio,
				     /*retries*/4,
				     probedone,
				     MSG_SIMPLE_Q_TAG,
				     (u_int8_t *)serial_buf,
				     sizeof(*serial_buf),
				     /*evpd*/TRUE,
				     SVPD_UNIT_SERIAL_NUMBER,
				     SSD_MIN_SIZE,
				     /*timeout*/60 * 1000);
			break;
		}
		goto done;
	}
	default:
		panic("probestart: invalid action state 0x%x\n", softc->action);
	}
	start_ccb->ccb_h.flags |= CAM_DEV_QFREEZE;
	cam_periph_doacquire(periph);
	xpt_action(start_ccb);
}

static void
proberequestdefaultnegotiation(struct cam_periph *periph)
{
	struct ccb_trans_settings cts;

	xpt_setup_ccb(&cts.ccb_h, periph->path, CAM_PRIORITY_NONE);
	cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
	cts.type = CTS_TYPE_USER_SETTINGS;
	xpt_action((union ccb *)&cts);
	if (cam_ccb_status((union ccb *)&cts) != CAM_REQ_CMP) {
		return;
	}
	cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
	cts.type = CTS_TYPE_CURRENT_SETTINGS;
	xpt_action((union ccb *)&cts);
}

/*
 * Backoff Negotiation Code- only pertinent for SPI devices.
 */
static int
proberequestbackoff(struct cam_periph *periph, struct cam_ed *device)
{
	struct ccb_trans_settings cts;
	struct ccb_trans_settings_spi *spi;

	memset(&cts, 0, sizeof (cts));
	xpt_setup_ccb(&cts.ccb_h, periph->path, CAM_PRIORITY_NONE);
	cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
	cts.type = CTS_TYPE_CURRENT_SETTINGS;
	xpt_action((union ccb *)&cts);
	if (cam_ccb_status((union ccb *)&cts) != CAM_REQ_CMP) {
		if (bootverbose) {
			xpt_print(periph->path,
			    "failed to get current device settings\n");
		}
		return (0);
	}
	if (cts.transport != XPORT_SPI) {
		if (bootverbose) {
			xpt_print(periph->path, "not SPI transport\n");
		}
		return (0);
	}
	spi = &cts.xport_specific.spi;

	/*
	 * We cannot renegotiate sync rate if we don't have one.
	 */
	if ((spi->valid & CTS_SPI_VALID_SYNC_RATE) == 0) {
		if (bootverbose) {
			xpt_print(periph->path, "no sync rate known\n");
		}
		return (0);
	}

	/*
	 * We'll assert that we don't have to touch PPR options- the
	 * SIM will see what we do with period and offset and adjust
	 * the PPR options as appropriate.
	 */

	/*
	 * A sync rate with unknown or zero offset is nonsensical.
	 * A sync period of zero means Async.
	 */
	if ((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) == 0
	 || spi->sync_offset == 0 || spi->sync_period == 0) {
		if (bootverbose) {
			xpt_print(periph->path, "no sync rate available\n");
		}
		return (0);
	}

	if (device->flags & CAM_DEV_DV_HIT_BOTTOM) {
		CAM_DEBUG(periph->path, CAM_DEBUG_PROBE,
		    ("hit async: giving up on DV\n"));
		return (0);
	}


	/*
	 * Jump sync_period up by one, but stop at 5MHz and fall back to Async.
	 * We don't try to remember 'last' settings to see if the SIM actually
	 * gets into the speed we want to set. We check on the SIM telling
	 * us that a requested speed is bad, but otherwise don't try and
	 * check the speed due to the asynchronous and handshake nature
	 * of speed setting.
	 */
	spi->valid = CTS_SPI_VALID_SYNC_RATE | CTS_SPI_VALID_SYNC_OFFSET;
	for (;;) {
		spi->sync_period++;
		if (spi->sync_period >= 0xf) {
			spi->sync_period = 0;
			spi->sync_offset = 0;
			CAM_DEBUG(periph->path, CAM_DEBUG_PROBE,
			    ("setting to async for DV\n"));
			/*
			 * Once we hit async, we don't want to try
			 * any more settings.
			 */
			device->flags |= CAM_DEV_DV_HIT_BOTTOM;
		} else if (bootverbose) {
			CAM_DEBUG(periph->path, CAM_DEBUG_PROBE,
			    ("DV: period 0x%x\n", spi->sync_period));
			printf("setting period to 0x%x\n", spi->sync_period);
		}
		cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (cam_ccb_status((union ccb *)&cts) != CAM_REQ_CMP) {
			break;
		}
		CAM_DEBUG(periph->path, CAM_DEBUG_PROBE,
		    ("DV: failed to set period 0x%x\n", spi->sync_period));
		if (spi->sync_period == 0) {
			return (0);
		}
	}
	return (1);
}

#define CCB_COMPLETED_OK(ccb) (((ccb).status & CAM_STATUS_MASK) == CAM_REQ_CMP)

static void
probedone(struct cam_periph *periph, union ccb *done_ccb)
{
	probe_softc *softc;
	struct cam_path *path;
	struct scsi_inquiry_data *inq_buf;
	u_int32_t  priority;

	CAM_DEBUG(done_ccb->ccb_h.path, CAM_DEBUG_TRACE, ("probedone\n"));

	softc = (probe_softc *)periph->softc;
	path = done_ccb->ccb_h.path;
	priority = done_ccb->ccb_h.pinfo.priority;
	cam_periph_assert(periph, MA_OWNED);

	switch (softc->action) {
	case PROBE_TUR:
	{
		if (cam_ccb_status(done_ccb) != CAM_REQ_CMP) {

			if (cam_periph_error(done_ccb, 0, SF_NO_PRINT) ==
			    ERESTART) {
outr:
				/* Drop freeze taken due to CAM_DEV_QFREEZE */
				cam_release_devq(path, 0, 0, 0, FALSE);
				return;
			}
			else if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
				/* Don't wedge the queue */
				xpt_release_devq(done_ccb->ccb_h.path,
						 /*count*/1,
						 /*run_queue*/TRUE);
		}
		PROBE_SET_ACTION(softc, PROBE_INQUIRY);
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
out:
		/* Drop freeze taken due to CAM_DEV_QFREEZE and release. */
		cam_release_devq(path, 0, 0, 0, FALSE);
		cam_periph_release_locked(periph);
		return;
	}
	case PROBE_INQUIRY:
	case PROBE_FULL_INQUIRY:
	{
		if (cam_ccb_status(done_ccb) == CAM_REQ_CMP) {
			u_int8_t periph_qual;

			path->device->flags |= CAM_DEV_INQUIRY_DATA_VALID;
			scsi_find_quirk(path->device);
			inq_buf = &path->device->inq_data;

			periph_qual = SID_QUAL(inq_buf);

			if (periph_qual == SID_QUAL_LU_CONNECTED ||
			    periph_qual == SID_QUAL_LU_OFFLINE) {
				u_int8_t len;

				/*
				 * We conservatively request only
				 * SHORT_INQUIRY_LEN bytes of inquiry
				 * information during our first try
				 * at sending an INQUIRY. If the device
				 * has more information to give,
				 * perform a second request specifying
				 * the amount of information the device
				 * is willing to give.
				 */
				len = inq_buf->additional_length
				    + offsetof(struct scsi_inquiry_data,
                                               additional_length) + 1;
				if (softc->action == PROBE_INQUIRY
				    && len > SHORT_INQUIRY_LENGTH) {
					PROBE_SET_ACTION(softc, PROBE_FULL_INQUIRY);
					xpt_release_ccb(done_ccb);
					xpt_schedule(periph, priority);
					goto out;
				}

				scsi_devise_transport(path);

				if (path->device->lun_id == 0 &&
				    SID_ANSI_REV(inq_buf) > SCSI_REV_SPC2 &&
				    (SCSI_QUIRK(path->device)->quirks &
				     CAM_QUIRK_NORPTLUNS) == 0) {
					PROBE_SET_ACTION(softc,
					    PROBE_REPORT_LUNS);
					/*
					 * Start with room for *one* lun.
					 */
					periph->path->target->rpl_size = 16;
				} else if (INQ_DATA_TQ_ENABLED(inq_buf))
					PROBE_SET_ACTION(softc,
					    PROBE_MODE_SENSE);
				else
					PROBE_SET_ACTION(softc,
					    PROBE_SUPPORTED_VPD_LIST);

				if (path->device->flags & CAM_DEV_UNCONFIGURED) {
					path->device->flags &= ~CAM_DEV_UNCONFIGURED;
					xpt_acquire_device(path->device);
				}
				xpt_release_ccb(done_ccb);
				xpt_schedule(periph, priority);
				goto out;
			} else if (path->device->lun_id == 0 &&
			    SID_ANSI_REV(inq_buf) >= SCSI_REV_SPC2 &&
			    (SCSI_QUIRK(path->device)->quirks &
			     CAM_QUIRK_NORPTLUNS) == 0) {
				PROBE_SET_ACTION(softc, PROBE_REPORT_LUNS);
				periph->path->target->rpl_size = 16;
				xpt_release_ccb(done_ccb);
				xpt_schedule(periph, priority);
				goto out;
			}
		} else if (cam_periph_error(done_ccb, 0,
					    done_ccb->ccb_h.target_lun > 0
					    ? SF_RETRY_UA|SF_QUIET_IR
					    : SF_RETRY_UA) == ERESTART) {
			goto outr;
		} else {
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				/* Don't wedge the queue */
				xpt_release_devq(done_ccb->ccb_h.path,
				    /*count*/1, /*run_queue*/TRUE);
			}
			path->device->flags &= ~CAM_DEV_INQUIRY_DATA_VALID;
		}
		/*
		 * If we get to this point, we got an error status back
		 * from the inquiry and the error status doesn't require
		 * automatically retrying the command.  Therefore, the
		 * inquiry failed.  If we had inquiry information before
		 * for this device, but this latest inquiry command failed,
		 * the device has probably gone away.  If this device isn't
		 * already marked unconfigured, notify the peripheral
		 * drivers that this device is no more.
		 */
		if ((path->device->flags & CAM_DEV_UNCONFIGURED) == 0)
			/* Send the async notification. */
			xpt_async(AC_LOST_DEVICE, path, NULL);
		PROBE_SET_ACTION(softc, PROBE_INVALID);

		xpt_release_ccb(done_ccb);
		break;
	}
	case PROBE_REPORT_LUNS:
	{
		struct ccb_scsiio *csio;
		struct scsi_report_luns_data *lp;
		u_int nlun, maxlun;

		csio = &done_ccb->csio;

		lp = (struct scsi_report_luns_data *)csio->data_ptr;
		nlun = scsi_4btoul(lp->length) / 8;
		maxlun = (csio->dxfer_len / 8) - 1;

		if (cam_ccb_status(done_ccb) != CAM_REQ_CMP) {
			if (cam_periph_error(done_ccb, 0,
				done_ccb->ccb_h.target_lun > 0 ?
				SF_RETRY_UA|SF_QUIET_IR : SF_RETRY_UA) ==
			    ERESTART) {
				goto outr;
			}
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
				xpt_release_devq(done_ccb->ccb_h.path, 1,
				    TRUE);
			}
			free(lp, M_CAMXPT);
			lp = NULL;
		} else if (nlun > maxlun) {
			/*
			 * Reallocate and retry to cover all luns
			 */
			CAM_DEBUG(path, CAM_DEBUG_PROBE,
			    ("Probe: reallocating REPORT_LUNS for %u luns\n",
			     nlun));
			free(lp, M_CAMXPT);
			path->target->rpl_size = (nlun << 3) + 8;
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		} else if (nlun == 0) {
			/*
			 * If there don't appear to be any luns, bail.
			 */
			free(lp, M_CAMXPT);
			lp = NULL;
		} else {
			lun_id_t lun;
			int idx;

			CAM_DEBUG(path, CAM_DEBUG_PROBE,
			   ("Probe: %u lun(s) reported\n", nlun));

			CAM_GET_LUN(lp, 0, lun);
			/*
			 * If the first lun is not lun 0, then either there
			 * is no lun 0 in the list, or the list is unsorted.
			 */
			if (lun != 0) {
				for (idx = 0; idx < nlun; idx++) {
					CAM_GET_LUN(lp, idx, lun);
					if (lun == 0) {
						break;
					}
				}
				if (idx != nlun) {
					uint8_t tlun[8];
					memcpy(tlun,
					    lp->luns[0].lundata, 8);
					memcpy(lp->luns[0].lundata,
					    lp->luns[idx].lundata, 8);
					memcpy(lp->luns[idx].lundata,
					    tlun, 8);
					CAM_DEBUG(path, CAM_DEBUG_PROBE,
					    ("lun 0 in position %u\n", idx));
				}
			}
			/*
			 * If we have an old lun list, We can either
			 * retest luns that appear to have been dropped,
			 * or just nuke them.  We'll opt for the latter.
			 * This function will also install the new list
			 * in the target structure.
			 */
			probe_purge_old(path, lp, softc->flags);
			lp = NULL;
		}
		/* The processing above should either exit via a `goto
		 * out` or leave the `lp` variable `NULL` and (if
		 * applicable) `free()` the storage to which it had
		 * pointed. Assert here that is the case.
		 */
		KASSERT(lp == NULL, ("%s: lp is not NULL", __func__));
		inq_buf = &path->device->inq_data;
		if (path->device->flags & CAM_DEV_INQUIRY_DATA_VALID &&
		    (SID_QUAL(inq_buf) == SID_QUAL_LU_CONNECTED ||
		    SID_QUAL(inq_buf) == SID_QUAL_LU_OFFLINE)) {
			if (INQ_DATA_TQ_ENABLED(inq_buf))
				PROBE_SET_ACTION(softc, PROBE_MODE_SENSE);
			else
				PROBE_SET_ACTION(softc,
				    PROBE_SUPPORTED_VPD_LIST);
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		}
		PROBE_SET_ACTION(softc, PROBE_INVALID);
		xpt_release_ccb(done_ccb);
		break;
	}
	case PROBE_MODE_SENSE:
	{
		struct ccb_scsiio *csio;
		struct scsi_mode_header_6 *mode_hdr;

		csio = &done_ccb->csio;
		mode_hdr = (struct scsi_mode_header_6 *)csio->data_ptr;
		if (cam_ccb_status(done_ccb) == CAM_REQ_CMP) {
			struct scsi_control_page *page;
			u_int8_t *offset;

			offset = ((u_int8_t *)&mode_hdr[1])
			    + mode_hdr->blk_desc_len;
			page = (struct scsi_control_page *)offset;
			path->device->queue_flags = page->queue_flags;
		} else if (cam_periph_error(done_ccb, 0,
			SF_RETRY_UA|SF_NO_PRINT) == ERESTART) {
			goto outr;
		} else if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
			/* Don't wedge the queue */
			xpt_release_devq(done_ccb->ccb_h.path,
					 /*count*/1, /*run_queue*/TRUE);
		}
		xpt_release_ccb(done_ccb);
		free(mode_hdr, M_CAMXPT);
		PROBE_SET_ACTION(softc, PROBE_SUPPORTED_VPD_LIST);
		xpt_schedule(periph, priority);
		goto out;
	}
	case PROBE_SUPPORTED_VPD_LIST:
	{
		struct ccb_scsiio *csio;
		struct scsi_vpd_supported_page_list *page_list;

		csio = &done_ccb->csio;
		page_list =
		    (struct scsi_vpd_supported_page_list *)csio->data_ptr;

		if (path->device->supported_vpds != NULL) {
			free(path->device->supported_vpds, M_CAMXPT);
			path->device->supported_vpds = NULL;
			path->device->supported_vpds_len = 0;
		}

		if (page_list == NULL) {
			/*
			 * Don't process the command as it was never sent
			 */
		} else if (CCB_COMPLETED_OK(csio->ccb_h)) {
			/* Got vpd list */
			path->device->supported_vpds_len = page_list->length +
			    SVPD_SUPPORTED_PAGES_HDR_LEN;
			path->device->supported_vpds = (uint8_t *)page_list;
			xpt_release_ccb(done_ccb);
			PROBE_SET_ACTION(softc, PROBE_DEVICE_ID);
			xpt_schedule(periph, priority);
			goto out;
		} else if (cam_periph_error(done_ccb, 0,
			SF_RETRY_UA|SF_NO_PRINT) == ERESTART) {
			goto outr;
		} else if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
			/* Don't wedge the queue */
			xpt_release_devq(done_ccb->ccb_h.path, /*count*/1,
					 /*run_queue*/TRUE);
		}

		if (page_list)
			free(page_list, M_CAMXPT);
		/* No VPDs available, skip to device check. */
		csio->data_ptr = NULL;
		goto probe_device_check;
	}
	case PROBE_DEVICE_ID:
	{
		struct scsi_vpd_device_id *devid;
		struct ccb_scsiio *csio;
		uint32_t length = 0;

		csio = &done_ccb->csio;
		devid = (struct scsi_vpd_device_id *)csio->data_ptr;

		/* Clean up from previous instance of this device */
		if (path->device->device_id != NULL) {
			path->device->device_id_len = 0;
			free(path->device->device_id, M_CAMXPT);
			path->device->device_id = NULL;
		}

		if (devid == NULL) {
			/* Don't process the command as it was never sent */
		} else if (CCB_COMPLETED_OK(csio->ccb_h)) {
			length = scsi_2btoul(devid->length);
			if (length != 0) {
				/*
				 * NB: device_id_len is actual response
				 * size, not buffer size.
				 */
				path->device->device_id_len = length +
				    SVPD_DEVICE_ID_HDR_LEN;
				path->device->device_id = (uint8_t *)devid;
			}
		} else if (cam_periph_error(done_ccb, 0,
			SF_RETRY_UA) == ERESTART) {
			goto outr;
		} else if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
			/* Don't wedge the queue */
			xpt_release_devq(done_ccb->ccb_h.path, /*count*/1,
					 /*run_queue*/TRUE);
		}

		/* Free the device id space if we don't use it */
		if (devid && length == 0)
			free(devid, M_CAMXPT);
		xpt_release_ccb(done_ccb);
		PROBE_SET_ACTION(softc, PROBE_EXTENDED_INQUIRY);
		xpt_schedule(periph, priority);
		goto out;
	}
	case PROBE_EXTENDED_INQUIRY: {
		struct scsi_vpd_extended_inquiry_data *ext_inq;
		struct ccb_scsiio *csio;
		int32_t length = 0;

		csio = &done_ccb->csio;
		ext_inq = (struct scsi_vpd_extended_inquiry_data *)
		    csio->data_ptr;
		if (path->device->ext_inq != NULL) {
			path->device->ext_inq_len = 0;
			free(path->device->ext_inq, M_CAMXPT);
			path->device->ext_inq = NULL;
		}

		if (ext_inq == NULL) {
			/* Don't process the command as it was never sent */
		} else if (CCB_COMPLETED_OK(csio->ccb_h)) {
			length = scsi_2btoul(ext_inq->page_length) +
			    __offsetof(struct scsi_vpd_extended_inquiry_data,
			    flags1);
			length = min(length, sizeof(*ext_inq));
			length -= csio->resid;
			if (length > 0) {
				path->device->ext_inq_len = length;
				path->device->ext_inq = (uint8_t *)ext_inq;
			}
		} else if (cam_periph_error(done_ccb, 0, SF_RETRY_UA) ==
		    ERESTART) {
			goto outr;
		} else if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
			/* Don't wedge the queue */
			xpt_release_devq(done_ccb->ccb_h.path, /*count*/1,
					 /*run_queue*/TRUE);
		}

		/* Free the device id space if we don't use it */
		if (ext_inq && length <= 0)
			free(ext_inq, M_CAMXPT);
		xpt_release_ccb(done_ccb);
		PROBE_SET_ACTION(softc, PROBE_SERIAL_NUM);
		xpt_schedule(periph, priority);
		goto out;
	}

probe_device_check:
	case PROBE_SERIAL_NUM:
	{
		struct ccb_scsiio *csio;
		struct scsi_vpd_unit_serial_number *serial_buf;
		u_int32_t  priority;
		int changed;
		int have_serialnum;

		changed = 1;
		have_serialnum = 0;
		csio = &done_ccb->csio;
		priority = done_ccb->ccb_h.pinfo.priority;
		serial_buf =
		    (struct scsi_vpd_unit_serial_number *)csio->data_ptr;

		if (serial_buf == NULL) {
			/*
			 * Don't process the command as it was never sent
			 */
		} else if (cam_ccb_status(done_ccb) == CAM_REQ_CMP
			&& (serial_buf->length > 0)) {

			have_serialnum = 1;
			path->device->serial_num =
				(u_int8_t *)malloc((serial_buf->length + 1),
						   M_CAMXPT, M_NOWAIT);
			if (path->device->serial_num != NULL) {
				int start, slen;

				start = strspn(serial_buf->serial_num, " ");
				slen = serial_buf->length - start;
				if (slen <= 0) {
					/* 
					 * SPC5r05 says that an all-space serial
					 * number means no product serial number
					 * is available
					 */
					slen = 0;
				}
				memcpy(path->device->serial_num,
				       &serial_buf->serial_num[start], slen);
				path->device->serial_num_len = slen;
				path->device->serial_num[slen] = '\0';
			}
		} else if (cam_periph_error(done_ccb, 0,
			SF_RETRY_UA|SF_NO_PRINT) == ERESTART) {
			goto outr;
		} else if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
			/* Don't wedge the queue */
			xpt_release_devq(done_ccb->ccb_h.path, /*count*/1,
					 /*run_queue*/TRUE);
		}

		/*
		 * Let's see if we have seen this device before.
		 */
		if ((softc->flags & PROBE_INQUIRY_CKSUM) != 0) {
			MD5_CTX context;
			u_int8_t digest[16];

			MD5Init(&context);

			MD5Update(&context,
				  (unsigned char *)&path->device->inq_data,
				  sizeof(struct scsi_inquiry_data));

			if (have_serialnum)
				MD5Update(&context, path->device->serial_num,
					  path->device->serial_num_len);

			MD5Final(digest, &context);
			if (bcmp(softc->digest, digest, 16) == 0)
				changed = 0;

			/*
			 * XXX Do we need to do a TUR in order to ensure
			 *     that the device really hasn't changed???
			 */
			if ((changed != 0)
			 && ((softc->flags & PROBE_NO_ANNOUNCE) == 0))
				xpt_async(AC_LOST_DEVICE, path, NULL);
		}
		if (serial_buf != NULL)
			free(serial_buf, M_CAMXPT);

		if (changed != 0) {
			/*
			 * Now that we have all the necessary
			 * information to safely perform transfer
			 * negotiations... Controllers don't perform
			 * any negotiation or tagged queuing until
			 * after the first XPT_SET_TRAN_SETTINGS ccb is
			 * received.  So, on a new device, just retrieve
			 * the user settings, and set them as the current
			 * settings to set the device up.
			 */
			proberequestdefaultnegotiation(periph);
			xpt_release_ccb(done_ccb);

			/*
			 * Perform a TUR to allow the controller to
			 * perform any necessary transfer negotiation.
			 */
			PROBE_SET_ACTION(softc, PROBE_TUR_FOR_NEGOTIATION);
			xpt_schedule(periph, priority);
			goto out;
		}
		xpt_release_ccb(done_ccb);
		break;
	}
	case PROBE_TUR_FOR_NEGOTIATION:
	case PROBE_DV_EXIT:
		if (cam_ccb_status(done_ccb) != CAM_REQ_CMP) {
			cam_periph_error(done_ccb, 0,
			    SF_NO_PRINT | SF_NO_RECOVERY | SF_NO_RETRY);
		}
		if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
			/* Don't wedge the queue */
			xpt_release_devq(done_ccb->ccb_h.path, /*count*/1,
					 /*run_queue*/TRUE);
		}
		/*
		 * Do Domain Validation for lun 0 on devices that claim
		 * to support Synchronous Transfer modes.
		 */
	 	if (softc->action == PROBE_TUR_FOR_NEGOTIATION
		 && done_ccb->ccb_h.target_lun == 0
		 && (path->device->inq_data.flags & SID_Sync) != 0
                 && (path->device->flags & CAM_DEV_IN_DV) == 0) {
			CAM_DEBUG(periph->path, CAM_DEBUG_PROBE,
			    ("Begin Domain Validation\n"));
			path->device->flags |= CAM_DEV_IN_DV;
			xpt_release_ccb(done_ccb);
			PROBE_SET_ACTION(softc, PROBE_INQUIRY_BASIC_DV1);
			xpt_schedule(periph, priority);
			goto out;
		}
		if (softc->action == PROBE_DV_EXIT) {
			CAM_DEBUG(periph->path, CAM_DEBUG_PROBE,
			    ("Leave Domain Validation\n"));
		}
		if (path->device->flags & CAM_DEV_UNCONFIGURED) {
			path->device->flags &= ~CAM_DEV_UNCONFIGURED;
			xpt_acquire_device(path->device);
		}
		path->device->flags &=
		    ~(CAM_DEV_IN_DV|CAM_DEV_DV_HIT_BOTTOM);
		if ((softc->flags & PROBE_NO_ANNOUNCE) == 0) {
			/* Inform the XPT that a new device has been found */
			done_ccb->ccb_h.func_code = XPT_GDEV_TYPE;
			xpt_action(done_ccb);
			xpt_async(AC_FOUND_DEVICE, done_ccb->ccb_h.path,
				  done_ccb);
		}
		PROBE_SET_ACTION(softc, PROBE_DONE);
		xpt_release_ccb(done_ccb);
		break;
	case PROBE_INQUIRY_BASIC_DV1:
	case PROBE_INQUIRY_BASIC_DV2:
	{
		struct scsi_inquiry_data *nbuf;
		struct ccb_scsiio *csio;

		if (cam_ccb_status(done_ccb) != CAM_REQ_CMP) {
			cam_periph_error(done_ccb, 0,
			    SF_NO_PRINT | SF_NO_RECOVERY | SF_NO_RETRY);
		}
		if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
			/* Don't wedge the queue */
			xpt_release_devq(done_ccb->ccb_h.path, /*count*/1,
					 /*run_queue*/TRUE);
		}
		csio = &done_ccb->csio;
		nbuf = (struct scsi_inquiry_data *)csio->data_ptr;
		if (bcmp(nbuf, &path->device->inq_data, SHORT_INQUIRY_LENGTH)) {
			xpt_print(path,
			    "inquiry data fails comparison at DV%d step\n",
			    softc->action == PROBE_INQUIRY_BASIC_DV1 ? 1 : 2);
			if (proberequestbackoff(periph, path->device)) {
				path->device->flags &= ~CAM_DEV_IN_DV;
				PROBE_SET_ACTION(softc, PROBE_TUR_FOR_NEGOTIATION);
			} else {
				/* give up */
				PROBE_SET_ACTION(softc, PROBE_DV_EXIT);
			}
			free(nbuf, M_CAMXPT);
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		}
		free(nbuf, M_CAMXPT);
		if (softc->action == PROBE_INQUIRY_BASIC_DV1) {
			PROBE_SET_ACTION(softc, PROBE_INQUIRY_BASIC_DV2);
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, priority);
			goto out;
		}
		if (softc->action == PROBE_INQUIRY_BASIC_DV2) {
			CAM_DEBUG(periph->path, CAM_DEBUG_PROBE,
			    ("Leave Domain Validation Successfully\n"));
		}
		if (path->device->flags & CAM_DEV_UNCONFIGURED) {
			path->device->flags &= ~CAM_DEV_UNCONFIGURED;
			xpt_acquire_device(path->device);
		}
		path->device->flags &=
		    ~(CAM_DEV_IN_DV|CAM_DEV_DV_HIT_BOTTOM);
		if ((softc->flags & PROBE_NO_ANNOUNCE) == 0) {
			/* Inform the XPT that a new device has been found */
			done_ccb->ccb_h.func_code = XPT_GDEV_TYPE;
			xpt_action(done_ccb);
			xpt_async(AC_FOUND_DEVICE, done_ccb->ccb_h.path,
				  done_ccb);
		}
		PROBE_SET_ACTION(softc, PROBE_DONE);
		xpt_release_ccb(done_ccb);
		break;
	}
	default:
		panic("probedone: invalid action state 0x%x\n", softc->action);
	}
	done_ccb = (union ccb *)TAILQ_FIRST(&softc->request_ccbs);
	TAILQ_REMOVE(&softc->request_ccbs, &done_ccb->ccb_h, periph_links.tqe);
	done_ccb->ccb_h.status = CAM_REQ_CMP;
	xpt_done(done_ccb);
	if (TAILQ_FIRST(&softc->request_ccbs) == NULL) {
		CAM_DEBUG(periph->path, CAM_DEBUG_PROBE, ("Probe completed\n"));
		/* Drop freeze taken due to CAM_DEV_QFREEZE flag set. */
		cam_release_devq(path, 0, 0, 0, FALSE);
		cam_periph_release_locked(periph);
		cam_periph_invalidate(periph);
		cam_periph_release_locked(periph);
	} else {
		probeschedule(periph);
		goto out;
	}
}

static void
probe_purge_old(struct cam_path *path, struct scsi_report_luns_data *new,
    probe_flags flags)
{
	struct cam_path *tp;
	struct scsi_report_luns_data *old;
	u_int idx1, idx2, nlun_old, nlun_new;
	lun_id_t this_lun;
	u_int8_t *ol, *nl;

	if (path->target == NULL) {
		return;
	}
	mtx_lock(&path->target->luns_mtx);
	old = path->target->luns;
	path->target->luns = new;
	mtx_unlock(&path->target->luns_mtx);
	if (old == NULL)
		return;
	nlun_old = scsi_4btoul(old->length) / 8;
	nlun_new = scsi_4btoul(new->length) / 8;

	/*
	 * We are not going to assume sorted lists. Deal.
	 */
	for (idx1 = 0; idx1 < nlun_old; idx1++) {
		ol = old->luns[idx1].lundata;
		for (idx2 = 0; idx2 < nlun_new; idx2++) {
			nl = new->luns[idx2].lundata;
			if (memcmp(nl, ol, 8) == 0) {
				break;
			}
		}
		if (idx2 < nlun_new) {
			continue;
		}
		/*
		 * An 'old' item not in the 'new' list.
		 * Nuke it. Except that if it is lun 0,
		 * that would be what the probe state
		 * machine is currently working on,
		 * so we won't do that.
		 */
		CAM_GET_LUN(old, idx1, this_lun);
		if (this_lun == 0) {
			continue;
		}

		/*
		 * We also cannot nuke it if it is
		 * not in a lun format we understand
		 * and replace the LUN with a "simple" LUN
		 * if that is all the HBA supports.
		 */
		if (!(flags & PROBE_EXTLUN)) {
			if (!CAM_CAN_GET_SIMPLE_LUN(old, idx1))
				continue;
			CAM_GET_SIMPLE_LUN(old, idx1, this_lun);
		}

		if (xpt_create_path(&tp, NULL, xpt_path_path_id(path),
		    xpt_path_target_id(path), this_lun) == CAM_REQ_CMP) {
			xpt_async(AC_LOST_DEVICE, tp, NULL);
			xpt_free_path(tp);
		}
	}
	free(old, M_CAMXPT);
}

static void
probecleanup(struct cam_periph *periph)
{
	free(periph->softc, M_CAMXPT);
}

static void
scsi_find_quirk(struct cam_ed *device)
{
	struct scsi_quirk_entry *quirk;
	caddr_t	match;

	match = cam_quirkmatch((caddr_t)&device->inq_data,
			       (caddr_t)scsi_quirk_table,
			       nitems(scsi_quirk_table),
			       sizeof(*scsi_quirk_table), scsi_inquiry_match);

	if (match == NULL)
		panic("xpt_find_quirk: device didn't match wildcard entry!!");

	quirk = (struct scsi_quirk_entry *)match;
	device->quirk = quirk;
	device->mintags = quirk->mintags;
	device->maxtags = quirk->maxtags;
}

static int
sysctl_cam_search_luns(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = cam_srch_hi;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (val == 0 || val == 1) {
		cam_srch_hi = val;
		return (0);
	} else {
		return (EINVAL);
	}
}

typedef struct {
	union	ccb *request_ccb;
	struct 	ccb_pathinq *cpi;
	int	counter;
	int	lunindex[0];
} scsi_scan_bus_info;

/*
 * To start a scan, request_ccb is an XPT_SCAN_BUS ccb.
 * As the scan progresses, scsi_scan_bus is used as the
 * callback on completion function.
 */
static void
scsi_scan_bus(struct cam_periph *periph, union ccb *request_ccb)
{
	struct mtx *mtx;

	CAM_DEBUG(request_ccb->ccb_h.path, CAM_DEBUG_TRACE,
		  ("scsi_scan_bus\n"));
	switch (request_ccb->ccb_h.func_code) {
	case XPT_SCAN_BUS:
	case XPT_SCAN_TGT:
	{
		scsi_scan_bus_info *scan_info;
		union	ccb *work_ccb, *reset_ccb;
		struct	cam_path *path;
		u_int	i;
		u_int	low_target, max_target;
		u_int	initiator_id;

		/* Find out the characteristics of the bus */
		work_ccb = xpt_alloc_ccb_nowait();
		if (work_ccb == NULL) {
			request_ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
			xpt_done(request_ccb);
			return;
		}
		xpt_setup_ccb(&work_ccb->ccb_h, request_ccb->ccb_h.path,
			      request_ccb->ccb_h.pinfo.priority);
		work_ccb->ccb_h.func_code = XPT_PATH_INQ;
		xpt_action(work_ccb);
		if (work_ccb->ccb_h.status != CAM_REQ_CMP) {
			request_ccb->ccb_h.status = work_ccb->ccb_h.status;
			xpt_free_ccb(work_ccb);
			xpt_done(request_ccb);
			return;
		}

		if ((work_ccb->cpi.hba_misc & PIM_NOINITIATOR) != 0) {
			/*
			 * Can't scan the bus on an adapter that
			 * cannot perform the initiator role.
			 */
			request_ccb->ccb_h.status = CAM_REQ_CMP;
			xpt_free_ccb(work_ccb);
			xpt_done(request_ccb);
			return;
		}

		/* We may need to reset bus first, if we haven't done it yet. */
		if ((work_ccb->cpi.hba_inquiry &
		    (PI_WIDE_32|PI_WIDE_16|PI_SDTR_ABLE)) &&
		    !(work_ccb->cpi.hba_misc & PIM_NOBUSRESET) &&
		    !timevalisset(&request_ccb->ccb_h.path->bus->last_reset) &&
		    (reset_ccb = xpt_alloc_ccb_nowait()) != NULL) {
			xpt_setup_ccb(&reset_ccb->ccb_h, request_ccb->ccb_h.path,
			      CAM_PRIORITY_NONE);
			reset_ccb->ccb_h.func_code = XPT_RESET_BUS;
			xpt_action(reset_ccb);
			if (reset_ccb->ccb_h.status != CAM_REQ_CMP) {
				request_ccb->ccb_h.status = reset_ccb->ccb_h.status;
				xpt_free_ccb(reset_ccb);
				xpt_free_ccb(work_ccb);
				xpt_done(request_ccb);
				return;
			}
			xpt_free_ccb(reset_ccb);
		}

		/* Save some state for use while we probe for devices */
		scan_info = (scsi_scan_bus_info *) malloc(sizeof(scsi_scan_bus_info) +
		    (work_ccb->cpi.max_target * sizeof (u_int)), M_CAMXPT, M_ZERO|M_NOWAIT);
		if (scan_info == NULL) {
			request_ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
			xpt_free_ccb(work_ccb);
			xpt_done(request_ccb);
			return;
		}
		CAM_DEBUG(request_ccb->ccb_h.path, CAM_DEBUG_TRACE,
		   ("SCAN start for %p\n", scan_info));
		scan_info->request_ccb = request_ccb;
		scan_info->cpi = &work_ccb->cpi;

		/* Cache on our stack so we can work asynchronously */
		max_target = scan_info->cpi->max_target;
		low_target = 0;
		initiator_id = scan_info->cpi->initiator_id;


		/*
		 * We can scan all targets in parallel, or do it sequentially.
		 */

		if (request_ccb->ccb_h.func_code == XPT_SCAN_TGT) {
			max_target = low_target = request_ccb->ccb_h.target_id;
			scan_info->counter = 0;
		} else if (scan_info->cpi->hba_misc & PIM_SEQSCAN) {
			max_target = 0;
			scan_info->counter = 0;
		} else {
			scan_info->counter = scan_info->cpi->max_target + 1;
			if (scan_info->cpi->initiator_id < scan_info->counter) {
				scan_info->counter--;
			}
		}
		mtx = xpt_path_mtx(scan_info->request_ccb->ccb_h.path);
		mtx_unlock(mtx);

		for (i = low_target; i <= max_target; i++) {
			cam_status status;
			if (i == initiator_id)
				continue;

			status = xpt_create_path(&path, NULL,
						 request_ccb->ccb_h.path_id,
						 i, 0);
			if (status != CAM_REQ_CMP) {
				printf("scsi_scan_bus: xpt_create_path failed"
				       " with status %#x, bus scan halted\n",
				       status);
				free(scan_info, M_CAMXPT);
				request_ccb->ccb_h.status = status;
				xpt_free_ccb(work_ccb);
				xpt_done(request_ccb);
				break;
			}
			work_ccb = xpt_alloc_ccb_nowait();
			if (work_ccb == NULL) {
				xpt_free_ccb((union ccb *)scan_info->cpi);
				free(scan_info, M_CAMXPT);
				xpt_free_path(path);
				request_ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
				xpt_done(request_ccb);
				break;
			}
			xpt_setup_ccb(&work_ccb->ccb_h, path,
				      request_ccb->ccb_h.pinfo.priority);
			work_ccb->ccb_h.func_code = XPT_SCAN_LUN;
			work_ccb->ccb_h.cbfcnp = scsi_scan_bus;
			work_ccb->ccb_h.flags |= CAM_UNLOCKED;
			work_ccb->ccb_h.ppriv_ptr0 = scan_info;
			work_ccb->crcn.flags = request_ccb->crcn.flags;
			xpt_action(work_ccb);
		}

		mtx_lock(mtx);
		break;
	}
	case XPT_SCAN_LUN:
	{
		cam_status status;
		struct cam_path *path, *oldpath;
		scsi_scan_bus_info *scan_info;
		struct cam_et *target;
		struct cam_ed *device, *nextdev;
		int next_target;
		path_id_t path_id;
		target_id_t target_id;
		lun_id_t lun_id;

		oldpath = request_ccb->ccb_h.path;

		status = cam_ccb_status(request_ccb);
		scan_info = (scsi_scan_bus_info *)request_ccb->ccb_h.ppriv_ptr0;
		path_id = request_ccb->ccb_h.path_id;
		target_id = request_ccb->ccb_h.target_id;
		lun_id = request_ccb->ccb_h.target_lun;
		target = request_ccb->ccb_h.path->target;
		next_target = 1;

		mtx = xpt_path_mtx(scan_info->request_ccb->ccb_h.path);
		mtx_lock(mtx);
		mtx_lock(&target->luns_mtx);
		if (target->luns) {
			lun_id_t first;
			u_int nluns = scsi_4btoul(target->luns->length) / 8;

			/*
			 * Make sure we skip over lun 0 if it's the first member
			 * of the list as we've actually just finished probing
			 * it.
			 */
			CAM_GET_LUN(target->luns, 0, first);
			if (first == 0 && scan_info->lunindex[target_id] == 0) {
				scan_info->lunindex[target_id]++;
			} 

			/*
			 * Skip any LUNs that the HBA can't deal with.
			 */
			while (scan_info->lunindex[target_id] < nluns) {
				if (scan_info->cpi->hba_misc & PIM_EXTLUNS) {
					CAM_GET_LUN(target->luns,
					    scan_info->lunindex[target_id],
					    lun_id);
					break;
				}

				if (CAM_CAN_GET_SIMPLE_LUN(target->luns,
				    scan_info->lunindex[target_id])) {
					CAM_GET_SIMPLE_LUN(target->luns,
					    scan_info->lunindex[target_id],
					    lun_id);
					break;
				}
					
				scan_info->lunindex[target_id]++;
			}

			if (scan_info->lunindex[target_id] < nluns) {
				mtx_unlock(&target->luns_mtx);
				next_target = 0;
				CAM_DEBUG(request_ccb->ccb_h.path,
				    CAM_DEBUG_PROBE,
				   ("next lun to try at index %u is %jx\n",
				   scan_info->lunindex[target_id],
				   (uintmax_t)lun_id));
				scan_info->lunindex[target_id]++;
			} else {
				mtx_unlock(&target->luns_mtx);
				/* We're done with scanning all luns. */
			}
		} else {
			mtx_unlock(&target->luns_mtx);
			device = request_ccb->ccb_h.path->device;
			/* Continue sequential LUN scan if: */
			/*  -- we have more LUNs that need recheck */
			mtx_lock(&target->bus->eb_mtx);
			nextdev = device;
			while ((nextdev = TAILQ_NEXT(nextdev, links)) != NULL)
				if ((nextdev->flags & CAM_DEV_UNCONFIGURED) == 0)
					break;
			mtx_unlock(&target->bus->eb_mtx);
			if (nextdev != NULL) {
				next_target = 0;
			/*  -- stop if CAM_QUIRK_NOLUNS is set. */
			} else if (SCSI_QUIRK(device)->quirks & CAM_QUIRK_NOLUNS) {
				next_target = 1;
			/*  -- this LUN is connected and its SCSI version
			 *     allows more LUNs. */
			} else if ((device->flags & CAM_DEV_UNCONFIGURED) == 0) {
				if (lun_id < (CAM_SCSI2_MAXLUN-1) ||
				    CAN_SRCH_HI_DENSE(device))
					next_target = 0;
			/*  -- this LUN is disconnected, its SCSI version
			 *     allows more LUNs and we guess they may be. */
			} else if ((device->flags & CAM_DEV_INQUIRY_DATA_VALID) != 0) {
				if (lun_id < (CAM_SCSI2_MAXLUN-1) ||
				    CAN_SRCH_HI_SPARSE(device))
					next_target = 0;
			}
			if (next_target == 0) {
				lun_id++;
				if (lun_id > scan_info->cpi->max_lun)
					next_target = 1;
			}
		}

		/*
		 * Check to see if we scan any further luns.
		 */
		if (next_target) {
			int done;

			/*
			 * Free the current request path- we're done with it.
			 */
			xpt_free_path(oldpath);
 hop_again:
			done = 0;
			if (scan_info->request_ccb->ccb_h.func_code == XPT_SCAN_TGT) {
				done = 1;
			} else if (scan_info->cpi->hba_misc & PIM_SEQSCAN) {
				scan_info->counter++;
				if (scan_info->counter ==
				    scan_info->cpi->initiator_id) {
					scan_info->counter++;
				}
				if (scan_info->counter >=
				    scan_info->cpi->max_target+1) {
					done = 1;
				}
			} else {
				scan_info->counter--;
				if (scan_info->counter == 0) {
					done = 1;
				}
			}
			if (done) {
				mtx_unlock(mtx);
				xpt_free_ccb(request_ccb);
				xpt_free_ccb((union ccb *)scan_info->cpi);
				request_ccb = scan_info->request_ccb;
				CAM_DEBUG(request_ccb->ccb_h.path,
				    CAM_DEBUG_TRACE,
				   ("SCAN done for %p\n", scan_info));
				free(scan_info, M_CAMXPT);
				request_ccb->ccb_h.status = CAM_REQ_CMP;
				xpt_done(request_ccb);
				break;
			}

			if ((scan_info->cpi->hba_misc & PIM_SEQSCAN) == 0) {
				mtx_unlock(mtx);
				xpt_free_ccb(request_ccb);
				break;
			}
			status = xpt_create_path(&path, NULL,
			    scan_info->request_ccb->ccb_h.path_id,
			    scan_info->counter, 0);
			if (status != CAM_REQ_CMP) {
				mtx_unlock(mtx);
				printf("scsi_scan_bus: xpt_create_path failed"
				    " with status %#x, bus scan halted\n",
			       	    status);
				xpt_free_ccb(request_ccb);
				xpt_free_ccb((union ccb *)scan_info->cpi);
				request_ccb = scan_info->request_ccb;
				free(scan_info, M_CAMXPT);
				request_ccb->ccb_h.status = status;
				xpt_done(request_ccb);
				break;
			}
			xpt_setup_ccb(&request_ccb->ccb_h, path,
			    request_ccb->ccb_h.pinfo.priority);
			request_ccb->ccb_h.func_code = XPT_SCAN_LUN;
			request_ccb->ccb_h.cbfcnp = scsi_scan_bus;
			request_ccb->ccb_h.flags |= CAM_UNLOCKED;
			request_ccb->ccb_h.ppriv_ptr0 = scan_info;
			request_ccb->crcn.flags =
			    scan_info->request_ccb->crcn.flags;
		} else {
			status = xpt_create_path(&path, NULL,
						 path_id, target_id, lun_id);
			/*
			 * Free the old request path- we're done with it. We
			 * do this *after* creating the new path so that
			 * we don't remove a target that has our lun list
			 * in the case that lun 0 is not present.
			 */
			xpt_free_path(oldpath);
			if (status != CAM_REQ_CMP) {
				printf("scsi_scan_bus: xpt_create_path failed "
				       "with status %#x, halting LUN scan\n",
			 	       status);
				goto hop_again;
			}
			xpt_setup_ccb(&request_ccb->ccb_h, path,
				      request_ccb->ccb_h.pinfo.priority);
			request_ccb->ccb_h.func_code = XPT_SCAN_LUN;
			request_ccb->ccb_h.cbfcnp = scsi_scan_bus;
			request_ccb->ccb_h.flags |= CAM_UNLOCKED;
			request_ccb->ccb_h.ppriv_ptr0 = scan_info;
			request_ccb->crcn.flags =
				scan_info->request_ccb->crcn.flags;
		}
		mtx_unlock(mtx);
		xpt_action(request_ccb);
		break;
	}
	default:
		break;
	}
}

static void
scsi_scan_lun(struct cam_periph *periph, struct cam_path *path,
	     cam_flags flags, union ccb *request_ccb)
{
	struct ccb_pathinq cpi;
	cam_status status;
	struct cam_path *new_path;
	struct cam_periph *old_periph;
	int lock;

	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("scsi_scan_lun\n"));

	xpt_setup_ccb(&cpi.ccb_h, path, CAM_PRIORITY_NONE);
	cpi.ccb_h.func_code = XPT_PATH_INQ;
	xpt_action((union ccb *)&cpi);

	if (cpi.ccb_h.status != CAM_REQ_CMP) {
		if (request_ccb != NULL) {
			request_ccb->ccb_h.status = cpi.ccb_h.status;
			xpt_done(request_ccb);
		}
		return;
	}

	if ((cpi.hba_misc & PIM_NOINITIATOR) != 0) {
		/*
		 * Can't scan the bus on an adapter that
		 * cannot perform the initiator role.
		 */
		if (request_ccb != NULL) {
			request_ccb->ccb_h.status = CAM_REQ_CMP;
			xpt_done(request_ccb);
		}
		return;
	}

	if (request_ccb == NULL) {
		request_ccb = xpt_alloc_ccb_nowait();
		if (request_ccb == NULL) {
			xpt_print(path, "scsi_scan_lun: can't allocate CCB, "
			    "can't continue\n");
			return;
		}
		status = xpt_create_path(&new_path, NULL,
					  path->bus->path_id,
					  path->target->target_id,
					  path->device->lun_id);
		if (status != CAM_REQ_CMP) {
			xpt_print(path, "scsi_scan_lun: can't create path, "
			    "can't continue\n");
			xpt_free_ccb(request_ccb);
			return;
		}
		xpt_setup_ccb(&request_ccb->ccb_h, new_path, CAM_PRIORITY_XPT);
		request_ccb->ccb_h.cbfcnp = xptscandone;
		request_ccb->ccb_h.func_code = XPT_SCAN_LUN;
		request_ccb->ccb_h.flags |= CAM_UNLOCKED;
		request_ccb->crcn.flags = flags;
	}

	lock = (xpt_path_owned(path) == 0);
	if (lock)
		xpt_path_lock(path);
	if ((old_periph = cam_periph_find(path, "probe")) != NULL) {
		if ((old_periph->flags & CAM_PERIPH_INVALID) == 0) {
			probe_softc *softc;

			softc = (probe_softc *)old_periph->softc;
			TAILQ_INSERT_TAIL(&softc->request_ccbs,
			    &request_ccb->ccb_h, periph_links.tqe);
		} else {
			request_ccb->ccb_h.status = CAM_REQ_CMP_ERR;
			xpt_done(request_ccb);
		}
	} else {
		status = cam_periph_alloc(proberegister, NULL, probecleanup,
					  probestart, "probe",
					  CAM_PERIPH_BIO,
					  request_ccb->ccb_h.path, NULL, 0,
					  request_ccb);

		if (status != CAM_REQ_CMP) {
			xpt_print(path, "scsi_scan_lun: cam_alloc_periph "
			    "returned an error, can't continue probe\n");
			request_ccb->ccb_h.status = status;
			xpt_done(request_ccb);
		}
	}
	if (lock)
		xpt_path_unlock(path);
}

static void
xptscandone(struct cam_periph *periph, union ccb *done_ccb)
{

	xpt_free_path(done_ccb->ccb_h.path);
	xpt_free_ccb(done_ccb);
}

static struct cam_ed *
scsi_alloc_device(struct cam_eb *bus, struct cam_et *target, lun_id_t lun_id)
{
	struct scsi_quirk_entry *quirk;
	struct cam_ed *device;

	device = xpt_alloc_device(bus, target, lun_id);
	if (device == NULL)
		return (NULL);

	/*
	 * Take the default quirk entry until we have inquiry
	 * data and can determine a better quirk to use.
	 */
	quirk = &scsi_quirk_table[nitems(scsi_quirk_table) - 1];
	device->quirk = (void *)quirk;
	device->mintags = quirk->mintags;
	device->maxtags = quirk->maxtags;
	bzero(&device->inq_data, sizeof(device->inq_data));
	device->inq_flags = 0;
	device->queue_flags = 0;
	device->serial_num = NULL;
	device->serial_num_len = 0;
	device->device_id = NULL;
	device->device_id_len = 0;
	device->supported_vpds = NULL;
	device->supported_vpds_len = 0;
	return (device);
}

static void
scsi_devise_transport(struct cam_path *path)
{
	struct ccb_pathinq cpi;
	struct ccb_trans_settings cts;
	struct scsi_inquiry_data *inq_buf;

	/* Get transport information from the SIM */
	xpt_setup_ccb(&cpi.ccb_h, path, CAM_PRIORITY_NONE);
	cpi.ccb_h.func_code = XPT_PATH_INQ;
	xpt_action((union ccb *)&cpi);

	inq_buf = NULL;
	if ((path->device->flags & CAM_DEV_INQUIRY_DATA_VALID) != 0)
		inq_buf = &path->device->inq_data;
	path->device->protocol = PROTO_SCSI;
	path->device->protocol_version =
	    inq_buf != NULL ? SID_ANSI_REV(inq_buf) : cpi.protocol_version;
	path->device->transport = cpi.transport;
	path->device->transport_version = cpi.transport_version;

	/*
	 * Any device not using SPI3 features should
	 * be considered SPI2 or lower.
	 */
	if (inq_buf != NULL) {
		if (path->device->transport == XPORT_SPI
		 && (inq_buf->spi3data & SID_SPI_MASK) == 0
		 && path->device->transport_version > 2)
			path->device->transport_version = 2;
	} else {
		struct cam_ed* otherdev;

		for (otherdev = TAILQ_FIRST(&path->target->ed_entries);
		     otherdev != NULL;
		     otherdev = TAILQ_NEXT(otherdev, links)) {
			if (otherdev != path->device)
				break;
		}

		if (otherdev != NULL) {
			/*
			 * Initially assume the same versioning as
			 * prior luns for this target.
			 */
			path->device->protocol_version =
			    otherdev->protocol_version;
			path->device->transport_version =
			    otherdev->transport_version;
		} else {
			/* Until we know better, opt for safety */
			path->device->protocol_version = 2;
			if (path->device->transport == XPORT_SPI)
				path->device->transport_version = 2;
			else
				path->device->transport_version = 0;
		}
	}

	/*
	 * XXX
	 * For a device compliant with SPC-2 we should be able
	 * to determine the transport version supported by
	 * scrutinizing the version descriptors in the
	 * inquiry buffer.
	 */

	/* Tell the controller what we think */
	xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
	cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
	cts.type = CTS_TYPE_CURRENT_SETTINGS;
	cts.transport = path->device->transport;
	cts.transport_version = path->device->transport_version;
	cts.protocol = path->device->protocol;
	cts.protocol_version = path->device->protocol_version;
	cts.proto_specific.valid = 0;
	cts.xport_specific.valid = 0;
	xpt_action((union ccb *)&cts);
}

static void
scsi_dev_advinfo(union ccb *start_ccb)
{
	struct cam_ed *device;
	struct ccb_dev_advinfo *cdai;
	off_t amt;

	start_ccb->ccb_h.status = CAM_REQ_INVALID;
	device = start_ccb->ccb_h.path->device;
	cdai = &start_ccb->cdai;
	switch(cdai->buftype) {
	case CDAI_TYPE_SCSI_DEVID:
		if (cdai->flags & CDAI_FLAG_STORE)
			return;
		cdai->provsiz = device->device_id_len;
		if (device->device_id_len == 0)
			break;
		amt = device->device_id_len;
		if (cdai->provsiz > cdai->bufsiz)
			amt = cdai->bufsiz;
		memcpy(cdai->buf, device->device_id, amt);
		break;
	case CDAI_TYPE_SERIAL_NUM:
		if (cdai->flags & CDAI_FLAG_STORE)
			return;
		cdai->provsiz = device->serial_num_len;
		if (device->serial_num_len == 0)
			break;
		amt = device->serial_num_len;
		if (cdai->provsiz > cdai->bufsiz)
			amt = cdai->bufsiz;
		memcpy(cdai->buf, device->serial_num, amt);
		break;
	case CDAI_TYPE_PHYS_PATH:
		if (cdai->flags & CDAI_FLAG_STORE) {
			if (device->physpath != NULL) {
				free(device->physpath, M_CAMXPT);
				device->physpath = NULL;
				device->physpath_len = 0;
			}
			/* Clear existing buffer if zero length */
			if (cdai->bufsiz == 0)
				break;
			device->physpath = malloc(cdai->bufsiz, M_CAMXPT, M_NOWAIT);
			if (device->physpath == NULL) {
				start_ccb->ccb_h.status = CAM_REQ_ABORTED;
				return;
			}
			device->physpath_len = cdai->bufsiz;
			memcpy(device->physpath, cdai->buf, cdai->bufsiz);
		} else {
			cdai->provsiz = device->physpath_len;
			if (device->physpath_len == 0)
				break;
			amt = device->physpath_len;
			if (cdai->provsiz > cdai->bufsiz)
				amt = cdai->bufsiz;
			memcpy(cdai->buf, device->physpath, amt);
		}
		break;
	case CDAI_TYPE_RCAPLONG:
		if (cdai->flags & CDAI_FLAG_STORE) {
			if (device->rcap_buf != NULL) {
				free(device->rcap_buf, M_CAMXPT);
				device->rcap_buf = NULL;
			}

			device->rcap_len = cdai->bufsiz;
			/* Clear existing buffer if zero length */
			if (cdai->bufsiz == 0)
				break;

			device->rcap_buf = malloc(cdai->bufsiz, M_CAMXPT,
						  M_NOWAIT);
			if (device->rcap_buf == NULL) {
				start_ccb->ccb_h.status = CAM_REQ_ABORTED;
				return;
			}

			memcpy(device->rcap_buf, cdai->buf, cdai->bufsiz);
		} else {
			cdai->provsiz = device->rcap_len;
			if (device->rcap_len == 0)
				break;
			amt = device->rcap_len;
			if (cdai->provsiz > cdai->bufsiz)
				amt = cdai->bufsiz;
			memcpy(cdai->buf, device->rcap_buf, amt);
		}
		break;
	case CDAI_TYPE_EXT_INQ:
		/*
		 * We fetch extended inquiry data during probe, if
		 * available.  We don't allow changing it.
		 */
		if (cdai->flags & CDAI_FLAG_STORE) 
			return;
		cdai->provsiz = device->ext_inq_len;
		if (device->ext_inq_len == 0)
			break;
		amt = device->ext_inq_len;
		if (cdai->provsiz > cdai->bufsiz)
			amt = cdai->bufsiz;
		memcpy(cdai->buf, device->ext_inq, amt);
		break;
	default:
		return;
	}
	start_ccb->ccb_h.status = CAM_REQ_CMP;

	if (cdai->flags & CDAI_FLAG_STORE) {
		xpt_async(AC_ADVINFO_CHANGED, start_ccb->ccb_h.path,
			  (void *)(uintptr_t)cdai->buftype);
	}
}

static void
scsi_action(union ccb *start_ccb)
{

	switch (start_ccb->ccb_h.func_code) {
	case XPT_SET_TRAN_SETTINGS:
	{
		scsi_set_transfer_settings(&start_ccb->cts,
					   start_ccb->ccb_h.path,
					   /*async_update*/FALSE);
		break;
	}
	case XPT_SCAN_BUS:
	case XPT_SCAN_TGT:
		scsi_scan_bus(start_ccb->ccb_h.path->periph, start_ccb);
		break;
	case XPT_SCAN_LUN:
		scsi_scan_lun(start_ccb->ccb_h.path->periph,
			      start_ccb->ccb_h.path, start_ccb->crcn.flags,
			      start_ccb);
		break;
	case XPT_DEV_ADVINFO:
	{
		scsi_dev_advinfo(start_ccb);
		break;
	}
	default:
		xpt_action_default(start_ccb);
		break;
	}
}

static void
scsi_set_transfer_settings(struct ccb_trans_settings *cts, struct cam_path *path,
			   int async_update)
{
	struct	ccb_pathinq cpi;
	struct	ccb_trans_settings cur_cts;
	struct	ccb_trans_settings_scsi *scsi;
	struct	ccb_trans_settings_scsi *cur_scsi;
	struct	scsi_inquiry_data *inq_data;
	struct	cam_ed *device;

	if (path == NULL || (device = path->device) == NULL) {
		cts->ccb_h.status = CAM_PATH_INVALID;
		xpt_done((union ccb *)cts);
		return;
	}

	if (cts->protocol == PROTO_UNKNOWN
	 || cts->protocol == PROTO_UNSPECIFIED) {
		cts->protocol = device->protocol;
		cts->protocol_version = device->protocol_version;
	}

	if (cts->protocol_version == PROTO_VERSION_UNKNOWN
	 || cts->protocol_version == PROTO_VERSION_UNSPECIFIED)
		cts->protocol_version = device->protocol_version;

	if (cts->protocol != device->protocol) {
		xpt_print(path, "Uninitialized Protocol %x:%x?\n",
		       cts->protocol, device->protocol);
		cts->protocol = device->protocol;
	}

	if (cts->protocol_version > device->protocol_version) {
		if (bootverbose) {
			xpt_print(path, "Down reving Protocol "
			    "Version from %d to %d?\n", cts->protocol_version,
			    device->protocol_version);
		}
		cts->protocol_version = device->protocol_version;
	}

	if (cts->transport == XPORT_UNKNOWN
	 || cts->transport == XPORT_UNSPECIFIED) {
		cts->transport = device->transport;
		cts->transport_version = device->transport_version;
	}

	if (cts->transport_version == XPORT_VERSION_UNKNOWN
	 || cts->transport_version == XPORT_VERSION_UNSPECIFIED)
		cts->transport_version = device->transport_version;

	if (cts->transport != device->transport) {
		xpt_print(path, "Uninitialized Transport %x:%x?\n",
		    cts->transport, device->transport);
		cts->transport = device->transport;
	}

	if (cts->transport_version > device->transport_version) {
		if (bootverbose) {
			xpt_print(path, "Down reving Transport "
			    "Version from %d to %d?\n", cts->transport_version,
			    device->transport_version);
		}
		cts->transport_version = device->transport_version;
	}

	/*
	 * Nothing more of interest to do unless
	 * this is a device connected via the
	 * SCSI protocol.
	 */
	if (cts->protocol != PROTO_SCSI) {
		if (async_update == FALSE)
			xpt_action_default((union ccb *)cts);
		return;
	}

	inq_data = &device->inq_data;
	scsi = &cts->proto_specific.scsi;
	xpt_setup_ccb(&cpi.ccb_h, path, CAM_PRIORITY_NONE);
	cpi.ccb_h.func_code = XPT_PATH_INQ;
	xpt_action((union ccb *)&cpi);

	/* SCSI specific sanity checking */
	if ((cpi.hba_inquiry & PI_TAG_ABLE) == 0
	 || (INQ_DATA_TQ_ENABLED(inq_data)) == 0
	 || (device->queue_flags & SCP_QUEUE_DQUE) != 0
	 || (device->mintags == 0)) {
		/*
		 * Can't tag on hardware that doesn't support tags,
		 * doesn't have it enabled, or has broken tag support.
		 */
		scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
	}

	if (async_update == FALSE) {
		/*
		 * Perform sanity checking against what the
		 * controller and device can do.
		 */
		xpt_setup_ccb(&cur_cts.ccb_h, path, CAM_PRIORITY_NONE);
		cur_cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cur_cts.type = cts->type;
		xpt_action((union ccb *)&cur_cts);
		if (cam_ccb_status((union ccb *)&cur_cts) != CAM_REQ_CMP) {
			return;
		}
		cur_scsi = &cur_cts.proto_specific.scsi;
		if ((scsi->valid & CTS_SCSI_VALID_TQ) == 0) {
			scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
			scsi->flags |= cur_scsi->flags & CTS_SCSI_FLAGS_TAG_ENB;
		}
		if ((cur_scsi->valid & CTS_SCSI_VALID_TQ) == 0)
			scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
	}

	/* SPI specific sanity checking */
	if (cts->transport == XPORT_SPI && async_update == FALSE) {
		u_int spi3caps;
		struct ccb_trans_settings_spi *spi;
		struct ccb_trans_settings_spi *cur_spi;

		spi = &cts->xport_specific.spi;

		cur_spi = &cur_cts.xport_specific.spi;

		/* Fill in any gaps in what the user gave us */
		if ((spi->valid & CTS_SPI_VALID_SYNC_RATE) == 0)
			spi->sync_period = cur_spi->sync_period;
		if ((cur_spi->valid & CTS_SPI_VALID_SYNC_RATE) == 0)
			spi->sync_period = 0;
		if ((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) == 0)
			spi->sync_offset = cur_spi->sync_offset;
		if ((cur_spi->valid & CTS_SPI_VALID_SYNC_OFFSET) == 0)
			spi->sync_offset = 0;
		if ((spi->valid & CTS_SPI_VALID_PPR_OPTIONS) == 0)
			spi->ppr_options = cur_spi->ppr_options;
		if ((cur_spi->valid & CTS_SPI_VALID_PPR_OPTIONS) == 0)
			spi->ppr_options = 0;
		if ((spi->valid & CTS_SPI_VALID_BUS_WIDTH) == 0)
			spi->bus_width = cur_spi->bus_width;
		if ((cur_spi->valid & CTS_SPI_VALID_BUS_WIDTH) == 0)
			spi->bus_width = 0;
		if ((spi->valid & CTS_SPI_VALID_DISC) == 0) {
			spi->flags &= ~CTS_SPI_FLAGS_DISC_ENB;
			spi->flags |= cur_spi->flags & CTS_SPI_FLAGS_DISC_ENB;
		}
		if ((cur_spi->valid & CTS_SPI_VALID_DISC) == 0)
			spi->flags &= ~CTS_SPI_FLAGS_DISC_ENB;
		if (((device->flags & CAM_DEV_INQUIRY_DATA_VALID) != 0
		  && (inq_data->flags & SID_Sync) == 0
		  && cts->type == CTS_TYPE_CURRENT_SETTINGS)
		 || ((cpi.hba_inquiry & PI_SDTR_ABLE) == 0)) {
			/* Force async */
			spi->sync_period = 0;
			spi->sync_offset = 0;
		}

		switch (spi->bus_width) {
		case MSG_EXT_WDTR_BUS_32_BIT:
			if (((device->flags & CAM_DEV_INQUIRY_DATA_VALID) == 0
			  || (inq_data->flags & SID_WBus32) != 0
			  || cts->type == CTS_TYPE_USER_SETTINGS)
			 && (cpi.hba_inquiry & PI_WIDE_32) != 0)
				break;
			/* Fall Through to 16-bit */
		case MSG_EXT_WDTR_BUS_16_BIT:
			if (((device->flags & CAM_DEV_INQUIRY_DATA_VALID) == 0
			  || (inq_data->flags & SID_WBus16) != 0
			  || cts->type == CTS_TYPE_USER_SETTINGS)
			 && (cpi.hba_inquiry & PI_WIDE_16) != 0) {
				spi->bus_width = MSG_EXT_WDTR_BUS_16_BIT;
				break;
			}
			/* Fall Through to 8-bit */
		default: /* New bus width?? */
		case MSG_EXT_WDTR_BUS_8_BIT:
			/* All targets can do this */
			spi->bus_width = MSG_EXT_WDTR_BUS_8_BIT;
			break;
		}

		spi3caps = cpi.xport_specific.spi.ppr_options;
		if ((device->flags & CAM_DEV_INQUIRY_DATA_VALID) != 0
		 && cts->type == CTS_TYPE_CURRENT_SETTINGS)
			spi3caps &= inq_data->spi3data;

		if ((spi3caps & SID_SPI_CLOCK_DT) == 0)
			spi->ppr_options &= ~MSG_EXT_PPR_DT_REQ;

		if ((spi3caps & SID_SPI_IUS) == 0)
			spi->ppr_options &= ~MSG_EXT_PPR_IU_REQ;

		if ((spi3caps & SID_SPI_QAS) == 0)
			spi->ppr_options &= ~MSG_EXT_PPR_QAS_REQ;

		/* No SPI Transfer settings are allowed unless we are wide */
		if (spi->bus_width == 0)
			spi->ppr_options = 0;

		if ((spi->valid & CTS_SPI_VALID_DISC)
		 && ((spi->flags & CTS_SPI_FLAGS_DISC_ENB) == 0)) {
			/*
			 * Can't tag queue without disconnection.
			 */
			scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
			scsi->valid |= CTS_SCSI_VALID_TQ;
		}

		/*
		 * If we are currently performing tagged transactions to
		 * this device and want to change its negotiation parameters,
		 * go non-tagged for a bit to give the controller a chance to
		 * negotiate unhampered by tag messages.
		 */
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS
		 && (device->inq_flags & SID_CmdQue) != 0
		 && (scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) != 0
		 && (spi->flags & (CTS_SPI_VALID_SYNC_RATE|
				   CTS_SPI_VALID_SYNC_OFFSET|
				   CTS_SPI_VALID_BUS_WIDTH)) != 0)
			scsi_toggle_tags(path);
	}

	if (cts->type == CTS_TYPE_CURRENT_SETTINGS
	 && (scsi->valid & CTS_SCSI_VALID_TQ) != 0) {
		int device_tagenb;

		/*
		 * If we are transitioning from tags to no-tags or
		 * vice-versa, we need to carefully freeze and restart
		 * the queue so that we don't overlap tagged and non-tagged
		 * commands.  We also temporarily stop tags if there is
		 * a change in transfer negotiation settings to allow
		 * "tag-less" negotiation.
		 */
		if ((device->flags & CAM_DEV_TAG_AFTER_COUNT) != 0
		 || (device->inq_flags & SID_CmdQue) != 0)
			device_tagenb = TRUE;
		else
			device_tagenb = FALSE;

		if (((scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) != 0
		  && device_tagenb == FALSE)
		 || ((scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) == 0
		  && device_tagenb == TRUE)) {

			if ((scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) != 0) {
				/*
				 * Delay change to use tags until after a
				 * few commands have gone to this device so
				 * the controller has time to perform transfer
				 * negotiations without tagged messages getting
				 * in the way.
				 */
				device->tag_delay_count = CAM_TAG_DELAY_COUNT;
				device->flags |= CAM_DEV_TAG_AFTER_COUNT;
			} else {
				xpt_stop_tags(path);
			}
		}
	}
	if (async_update == FALSE)
		xpt_action_default((union ccb *)cts);
}

static void
scsi_toggle_tags(struct cam_path *path)
{
	struct cam_ed *dev;

	/*
	 * Give controllers a chance to renegotiate
	 * before starting tag operations.  We
	 * "toggle" tagged queuing off then on
	 * which causes the tag enable command delay
	 * counter to come into effect.
	 */
	dev = path->device;
	if ((dev->flags & CAM_DEV_TAG_AFTER_COUNT) != 0
	 || ((dev->inq_flags & SID_CmdQue) != 0
 	  && (dev->inq_flags & (SID_Sync|SID_WBus16|SID_WBus32)) != 0)) {
		struct ccb_trans_settings cts;

		xpt_setup_ccb(&cts.ccb_h, path, CAM_PRIORITY_NONE);
		cts.protocol = PROTO_SCSI;
		cts.protocol_version = PROTO_VERSION_UNSPECIFIED;
		cts.transport = XPORT_UNSPECIFIED;
		cts.transport_version = XPORT_VERSION_UNSPECIFIED;
		cts.proto_specific.scsi.flags = 0;
		cts.proto_specific.scsi.valid = CTS_SCSI_VALID_TQ;
		scsi_set_transfer_settings(&cts, path,
					  /*async_update*/TRUE);
		cts.proto_specific.scsi.flags = CTS_SCSI_FLAGS_TAG_ENB;
		scsi_set_transfer_settings(&cts, path,
					  /*async_update*/TRUE);
	}
}

/*
 * Handle any per-device event notifications that require action by the XPT.
 */
static void
scsi_dev_async(u_int32_t async_code, struct cam_eb *bus, struct cam_et *target,
	      struct cam_ed *device, void *async_arg)
{
	cam_status status;
	struct cam_path newpath;

	/*
	 * We only need to handle events for real devices.
	 */
	if (target->target_id == CAM_TARGET_WILDCARD
	 || device->lun_id == CAM_LUN_WILDCARD)
		return;

	/*
	 * We need our own path with wildcards expanded to
	 * handle certain types of events.
	 */
	if ((async_code == AC_SENT_BDR)
	 || (async_code == AC_BUS_RESET)
	 || (async_code == AC_INQ_CHANGED))
		status = xpt_compile_path(&newpath, NULL,
					  bus->path_id,
					  target->target_id,
					  device->lun_id);
	else
		status = CAM_REQ_CMP_ERR;

	if (status == CAM_REQ_CMP) {

		/*
		 * Allow transfer negotiation to occur in a
		 * tag free environment and after settle delay.
		 */
		if (async_code == AC_SENT_BDR
		 || async_code == AC_BUS_RESET) {
			cam_freeze_devq(&newpath); 
			cam_release_devq(&newpath,
				RELSIM_RELEASE_AFTER_TIMEOUT,
				/*reduction*/0,
				/*timeout*/scsi_delay,
				/*getcount_only*/0);
			scsi_toggle_tags(&newpath);
		}

		if (async_code == AC_INQ_CHANGED) {
			/*
			 * We've sent a start unit command, or
			 * something similar to a device that
			 * may have caused its inquiry data to
			 * change. So we re-scan the device to
			 * refresh the inquiry data for it.
			 */
			scsi_scan_lun(newpath.periph, &newpath,
				     CAM_EXPECT_INQ_CHANGE, NULL);
		}
		xpt_release_path(&newpath);
	} else if (async_code == AC_LOST_DEVICE &&
	    (device->flags & CAM_DEV_UNCONFIGURED) == 0) {
		device->flags |= CAM_DEV_UNCONFIGURED;
		xpt_release_device(device);
	} else if (async_code == AC_TRANSFER_NEG) {
		struct ccb_trans_settings *settings;
		struct cam_path path;

		settings = (struct ccb_trans_settings *)async_arg;
		xpt_compile_path(&path, NULL, bus->path_id, target->target_id,
				 device->lun_id);
		scsi_set_transfer_settings(settings, &path,
					  /*async_update*/TRUE);
		xpt_release_path(&path);
	}
}

static void
_scsi_announce_periph(struct cam_periph *periph, u_int *speed, u_int *freq, struct ccb_trans_settings *cts)
{
	struct	ccb_pathinq cpi;
	struct	cam_path *path = periph->path;

	cam_periph_assert(periph, MA_OWNED);

	xpt_setup_ccb(&cts->ccb_h, path, CAM_PRIORITY_NORMAL);
	cts->ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
	cts->type = CTS_TYPE_CURRENT_SETTINGS;
	xpt_action((union ccb*)cts);
	if (cam_ccb_status((union ccb *)cts) != CAM_REQ_CMP)
		return;
	
	/* Ask the SIM for its base transfer speed */
	xpt_setup_ccb(&cpi.ccb_h, path, CAM_PRIORITY_NORMAL);
	cpi.ccb_h.func_code = XPT_PATH_INQ;
	xpt_action((union ccb *)&cpi);

	/* Report connection speed */ 
	*speed = cpi.base_transfer_speed;
	*freq = 0;

	if (cts->ccb_h.status == CAM_REQ_CMP && cts->transport == XPORT_SPI) {
		struct	ccb_trans_settings_spi *spi =
		    &cts->xport_specific.spi;

		if ((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) != 0
		  && spi->sync_offset != 0) {
			*freq = scsi_calc_syncsrate(spi->sync_period);
			*speed = *freq;
		}
		if ((spi->valid & CTS_SPI_VALID_BUS_WIDTH) != 0)
			*speed *= (0x01 << spi->bus_width);
	}
	if (cts->ccb_h.status == CAM_REQ_CMP && cts->transport == XPORT_FC) {
		struct	ccb_trans_settings_fc *fc =
		    &cts->xport_specific.fc;

		if (fc->valid & CTS_FC_VALID_SPEED)
			*speed = fc->bitrate;
	}
	if (cts->ccb_h.status == CAM_REQ_CMP && cts->transport == XPORT_SAS) {
		struct	ccb_trans_settings_sas *sas =
		    &cts->xport_specific.sas;

		if (sas->valid & CTS_SAS_VALID_SPEED)
			*speed = sas->bitrate;
	}
}

static void
scsi_announce_periph_sbuf(struct cam_periph *periph, struct sbuf *sb)
{
	struct	ccb_trans_settings cts;
	u_int speed, freq, mb;

	_scsi_announce_periph(periph, &speed, &freq, &cts);
	if (cam_ccb_status((union ccb *)&cts) != CAM_REQ_CMP)
		return;

	mb = speed / 1000;
	if (mb > 0)
		sbuf_printf(sb, "%s%d: %d.%03dMB/s transfers",
		       periph->periph_name, periph->unit_number,
		       mb, speed % 1000);
	else
		sbuf_printf(sb, "%s%d: %dKB/s transfers", periph->periph_name,
		       periph->unit_number, speed);
	/* Report additional information about SPI connections */
	if (cts.ccb_h.status == CAM_REQ_CMP && cts.transport == XPORT_SPI) {
		struct	ccb_trans_settings_spi *spi;

		spi = &cts.xport_specific.spi;
		if (freq != 0) {
			sbuf_printf(sb, " (%d.%03dMHz%s, offset %d", freq / 1000,
			       freq % 1000,
			       (spi->ppr_options & MSG_EXT_PPR_DT_REQ) != 0
			     ? " DT" : "",
			       spi->sync_offset);
		}
		if ((spi->valid & CTS_SPI_VALID_BUS_WIDTH) != 0
		 && spi->bus_width > 0) {
			if (freq != 0) {
				sbuf_printf(sb, ", ");
			} else {
				sbuf_printf(sb, " (");
			}
			sbuf_printf(sb, "%dbit)", 8 * (0x01 << spi->bus_width));
		} else if (freq != 0) {
			sbuf_printf(sb, ")");
		}
	}
	if (cts.ccb_h.status == CAM_REQ_CMP && cts.transport == XPORT_FC) {
		struct	ccb_trans_settings_fc *fc;

		fc = &cts.xport_specific.fc;
		if (fc->valid & CTS_FC_VALID_WWNN)
			sbuf_printf(sb, " WWNN 0x%llx", (long long) fc->wwnn);
		if (fc->valid & CTS_FC_VALID_WWPN)
			sbuf_printf(sb, " WWPN 0x%llx", (long long) fc->wwpn);
		if (fc->valid & CTS_FC_VALID_PORT)
			sbuf_printf(sb, " PortID 0x%x", fc->port);
	}
	sbuf_printf(sb, "\n");
}

static void
scsi_announce_periph(struct cam_periph *periph)
{
	struct	ccb_trans_settings cts;
	u_int speed, freq, mb;

	_scsi_announce_periph(periph, &speed, &freq, &cts);
	if (cam_ccb_status((union ccb *)&cts) != CAM_REQ_CMP)
		return;

	mb = speed / 1000;
	if (mb > 0)
		printf("%s%d: %d.%03dMB/s transfers",
		       periph->periph_name, periph->unit_number,
		       mb, speed % 1000);
	else
		printf("%s%d: %dKB/s transfers", periph->periph_name,
		       periph->unit_number, speed);
	/* Report additional information about SPI connections */
	if (cts.ccb_h.status == CAM_REQ_CMP && cts.transport == XPORT_SPI) {
		struct	ccb_trans_settings_spi *spi;

		spi = &cts.xport_specific.spi;
		if (freq != 0) {
			printf(" (%d.%03dMHz%s, offset %d", freq / 1000,
			       freq % 1000,
			       (spi->ppr_options & MSG_EXT_PPR_DT_REQ) != 0
			     ? " DT" : "",
			       spi->sync_offset);
		}
		if ((spi->valid & CTS_SPI_VALID_BUS_WIDTH) != 0
		 && spi->bus_width > 0) {
			if (freq != 0) {
				printf(", ");
			} else {
				printf(" (");
			}
			printf("%dbit)", 8 * (0x01 << spi->bus_width));
		} else if (freq != 0) {
			printf(")");
		}
	}
	if (cts.ccb_h.status == CAM_REQ_CMP && cts.transport == XPORT_FC) {
		struct	ccb_trans_settings_fc *fc;

		fc = &cts.xport_specific.fc;
		if (fc->valid & CTS_FC_VALID_WWNN)
			printf(" WWNN 0x%llx", (long long) fc->wwnn);
		if (fc->valid & CTS_FC_VALID_WWPN)
			printf(" WWPN 0x%llx", (long long) fc->wwpn);
		if (fc->valid & CTS_FC_VALID_PORT)
			printf(" PortID 0x%x", fc->port);
	}
	printf("\n");
}

static void
scsi_proto_announce_sbuf(struct cam_ed *device, struct sbuf *sb)
{
	scsi_print_inquiry_sbuf(sb, &device->inq_data);
}

static void
scsi_proto_announce(struct cam_ed *device)
{
	scsi_print_inquiry(&device->inq_data);
}

static void
scsi_proto_denounce_sbuf(struct cam_ed *device, struct sbuf *sb)
{
	scsi_print_inquiry_short_sbuf(sb, &device->inq_data);
}

static void
scsi_proto_denounce(struct cam_ed *device)
{
	scsi_print_inquiry_short(&device->inq_data);
}

static void
scsi_proto_debug_out(union ccb *ccb)
{
	char cdb_str[(SCSI_MAX_CDBLEN * 3) + 1];
	struct cam_ed *device;

	if (ccb->ccb_h.func_code != XPT_SCSI_IO)
		return;

	device = ccb->ccb_h.path->device;
	CAM_DEBUG(ccb->ccb_h.path,
	    CAM_DEBUG_CDB,("%s. CDB: %s\n",
		scsi_op_desc(scsiio_cdb_ptr(&ccb->csio)[0], &device->inq_data),
		scsi_cdb_string(scsiio_cdb_ptr(&ccb->csio), cdb_str, sizeof(cdb_str))));
}
