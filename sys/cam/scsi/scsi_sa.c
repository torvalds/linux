/*-
 * Implementation of SCSI Sequential Access Peripheral driver for CAM.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999, 2000 Matthew Jacob
 * Copyright (c) 2013, 2014, 2015 Spectra Logic Corporation
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
#include <sys/queue.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/kernel.h>
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <sys/bio.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/mtio.h>
#ifdef _KERNEL
#include <sys/conf.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#endif
#include <sys/fcntl.h>
#include <sys/devicestat.h>

#ifndef _KERNEL
#include <stdio.h>
#include <string.h>
#endif

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_debug.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_sa.h>

#ifdef _KERNEL

#include "opt_sa.h"

#ifndef SA_IO_TIMEOUT
#define SA_IO_TIMEOUT		32
#endif
#ifndef SA_SPACE_TIMEOUT
#define SA_SPACE_TIMEOUT	1 * 60
#endif
#ifndef SA_REWIND_TIMEOUT
#define SA_REWIND_TIMEOUT	2 * 60
#endif
#ifndef SA_ERASE_TIMEOUT
#define SA_ERASE_TIMEOUT	4 * 60
#endif
#ifndef SA_REP_DENSITY_TIMEOUT
#define SA_REP_DENSITY_TIMEOUT	90
#endif

#define	SCSIOP_TIMEOUT		(60 * 1000)	/* not an option */

#define	IO_TIMEOUT		(SA_IO_TIMEOUT * 60 * 1000)
#define	REWIND_TIMEOUT		(SA_REWIND_TIMEOUT * 60 * 1000)
#define	ERASE_TIMEOUT		(SA_ERASE_TIMEOUT * 60 * 1000)
#define	SPACE_TIMEOUT		(SA_SPACE_TIMEOUT * 60 * 1000)
#define	REP_DENSITY_TIMEOUT	(SA_REP_DENSITY_TIMEOUT * 60 * 1000)

/*
 * Additional options that can be set for config: SA_1FM_AT_EOT
 */

#ifndef	UNUSED_PARAMETER
#define	UNUSED_PARAMETER(x)	x = x
#endif

#define	QFRLS(ccb)	\
	if (((ccb)->ccb_h.status & CAM_DEV_QFRZN) != 0)	\
		cam_release_devq((ccb)->ccb_h.path, 0, 0, 0, FALSE)

/*
 * Driver states
 */

static MALLOC_DEFINE(M_SCSISA, "SCSI sa", "SCSI sequential access buffers");

typedef enum {
	SA_STATE_NORMAL, SA_STATE_ABNORMAL
} sa_state;

#define ccb_pflags	ppriv_field0
#define ccb_bp	 	ppriv_ptr1

/* bits in ccb_pflags */
#define	SA_POSITION_UPDATED	0x1


typedef enum {
	SA_FLAG_OPEN		= 0x0001,
	SA_FLAG_FIXED		= 0x0002,
	SA_FLAG_TAPE_LOCKED	= 0x0004,
	SA_FLAG_TAPE_MOUNTED	= 0x0008,
	SA_FLAG_TAPE_WP		= 0x0010,
	SA_FLAG_TAPE_WRITTEN	= 0x0020,
	SA_FLAG_EOM_PENDING	= 0x0040,
	SA_FLAG_EIO_PENDING	= 0x0080,
	SA_FLAG_EOF_PENDING	= 0x0100,
	SA_FLAG_ERR_PENDING	= (SA_FLAG_EOM_PENDING|SA_FLAG_EIO_PENDING|
				   SA_FLAG_EOF_PENDING),
	SA_FLAG_INVALID		= 0x0200,
	SA_FLAG_COMP_ENABLED	= 0x0400,
	SA_FLAG_COMP_SUPP	= 0x0800,
	SA_FLAG_COMP_UNSUPP	= 0x1000,
	SA_FLAG_TAPE_FROZEN	= 0x2000,
	SA_FLAG_PROTECT_SUPP	= 0x4000,

	SA_FLAG_COMPRESSION	= (SA_FLAG_COMP_SUPP|SA_FLAG_COMP_ENABLED|
				   SA_FLAG_COMP_UNSUPP),
	SA_FLAG_SCTX_INIT	= 0x8000
} sa_flags;

typedef enum {
	SA_MODE_REWIND		= 0x00,
	SA_MODE_NOREWIND	= 0x01,
	SA_MODE_OFFLINE		= 0x02
} sa_mode;

typedef enum {
	SA_PARAM_NONE		= 0x000,
	SA_PARAM_BLOCKSIZE	= 0x001,
	SA_PARAM_DENSITY	= 0x002,
	SA_PARAM_COMPRESSION	= 0x004,
	SA_PARAM_BUFF_MODE	= 0x008,
	SA_PARAM_NUMBLOCKS	= 0x010,
	SA_PARAM_WP		= 0x020,
	SA_PARAM_SPEED		= 0x040,
	SA_PARAM_DENSITY_EXT	= 0x080,
	SA_PARAM_LBP		= 0x100,
	SA_PARAM_ALL		= 0x1ff
} sa_params;

typedef enum {
	SA_QUIRK_NONE		= 0x000,
	SA_QUIRK_NOCOMP		= 0x001, /* Can't deal with compression at all*/
	SA_QUIRK_FIXED		= 0x002, /* Force fixed mode */
	SA_QUIRK_VARIABLE	= 0x004, /* Force variable mode */
	SA_QUIRK_2FM		= 0x008, /* Needs Two File Marks at EOD */
	SA_QUIRK_1FM		= 0x010, /* No more than 1 File Mark at EOD */
	SA_QUIRK_NODREAD	= 0x020, /* Don't try and dummy read density */
	SA_QUIRK_NO_MODESEL	= 0x040, /* Don't do mode select at all */
	SA_QUIRK_NO_CPAGE	= 0x080, /* Don't use DEVICE COMPRESSION page */
	SA_QUIRK_NO_LONG_POS	= 0x100  /* No long position information */
} sa_quirks;

#define SA_QUIRK_BIT_STRING	\
	"\020"			\
	"\001NOCOMP"		\
	"\002FIXED"		\
	"\003VARIABLE"		\
	"\0042FM"		\
	"\0051FM"		\
	"\006NODREAD"		\
	"\007NO_MODESEL"	\
	"\010NO_CPAGE"		\
	"\011NO_LONG_POS"

#define	SAMODE(z)	(dev2unit(z) & 0x3)
#define	SA_IS_CTRL(z)	(dev2unit(z) & (1 << 4))

#define SA_NOT_CTLDEV	0
#define SA_CTLDEV	1

#define SA_ATYPE_R	0
#define SA_ATYPE_NR	1
#define SA_ATYPE_ER	2
#define SA_NUM_ATYPES	3

#define	SAMINOR(ctl, access) \
	((ctl << 4) | (access & 0x3))

struct sa_devs {
	struct cdev *ctl_dev;
	struct cdev *r_dev;
	struct cdev *nr_dev;
	struct cdev *er_dev;
};

#define	SASBADDBASE(sb, indent, data, xfmt, name, type, xsize, desc)	\
	sbuf_printf(sb, "%*s<%s type=\"%s\" size=\"%zd\" "		\
	    "fmt=\"%s\" desc=\"%s\">" #xfmt "</%s>\n", indent, "", 	\
	    #name, #type, xsize, #xfmt, desc ? desc : "", data, #name);

#define	SASBADDINT(sb, indent, data, fmt, name)				\
	SASBADDBASE(sb, indent, data, fmt, name, int, sizeof(data),	\
		    NULL)

#define	SASBADDINTDESC(sb, indent, data, fmt, name, desc)		\
	SASBADDBASE(sb, indent, data, fmt, name, int, sizeof(data),	\
		    desc)

#define	SASBADDUINT(sb, indent, data, fmt, name)			\
	SASBADDBASE(sb, indent, data, fmt, name, uint, sizeof(data), 	\
		    NULL)

#define	SASBADDUINTDESC(sb, indent, data, fmt, name, desc)		\
	SASBADDBASE(sb, indent, data, fmt, name, uint, sizeof(data), 	\
		    desc)

#define	SASBADDFIXEDSTR(sb, indent, data, fmt, name)			\
	SASBADDBASE(sb, indent, data, fmt, name, str, sizeof(data),	\
		    NULL)

#define	SASBADDFIXEDSTRDESC(sb, indent, data, fmt, name, desc)		\
	SASBADDBASE(sb, indent, data, fmt, name, str, sizeof(data),	\
		    desc)

#define	SASBADDVARSTR(sb, indent, data, fmt, name, maxlen)		\
	SASBADDBASE(sb, indent, data, fmt, name, str, maxlen, NULL)

#define	SASBADDVARSTRDESC(sb, indent, data, fmt, name, maxlen, desc)	\
	SASBADDBASE(sb, indent, data, fmt, name, str, maxlen, desc)

#define	SASBADDNODE(sb, indent, name) {					\
	sbuf_printf(sb, "%*s<%s type=\"%s\">\n", indent, "", #name,	\
	    "node");							\
	indent += 2;							\
}

#define	SASBADDNODENUM(sb, indent, name, num) {				\
	sbuf_printf(sb, "%*s<%s type=\"%s\" num=\"%d\">\n", indent, "",	\
	    #name, "node", num);					\
	indent += 2;							\
}

#define	SASBENDNODE(sb, indent, name) {					\
	indent -= 2;							\
	sbuf_printf(sb, "%*s</%s>\n", indent, "", #name);		\
}

#define	SA_DENSITY_TYPES	4

struct sa_prot_state {
	int initialized;
	uint32_t prot_method;
	uint32_t pi_length;
	uint32_t lbp_w;
	uint32_t lbp_r;
	uint32_t rbdp;
};

struct sa_prot_info {
	struct sa_prot_state cur_prot_state;
	struct sa_prot_state pending_prot_state;
};

/*
 * A table mapping protection parameters to their types and values.
 */
struct sa_prot_map {
	char *name;
	mt_param_set_type param_type;
	off_t offset;
	uint32_t min_val;
	uint32_t max_val;
	uint32_t *value;
} sa_prot_table[] = {
	{ "prot_method", MT_PARAM_SET_UNSIGNED,
	  __offsetof(struct sa_prot_state, prot_method), 
	  /*min_val*/ 0, /*max_val*/ 255, NULL },
	{ "pi_length", MT_PARAM_SET_UNSIGNED, 
	  __offsetof(struct sa_prot_state, pi_length),
	  /*min_val*/ 0, /*max_val*/ SA_CTRL_DP_PI_LENGTH_MASK, NULL },
	{ "lbp_w", MT_PARAM_SET_UNSIGNED,
	  __offsetof(struct sa_prot_state, lbp_w),
	  /*min_val*/ 0, /*max_val*/ 1, NULL },
	{ "lbp_r", MT_PARAM_SET_UNSIGNED,
	  __offsetof(struct sa_prot_state, lbp_r),
	  /*min_val*/ 0, /*max_val*/ 1, NULL },
	{ "rbdp", MT_PARAM_SET_UNSIGNED,
	  __offsetof(struct sa_prot_state, rbdp),
	  /*min_val*/ 0, /*max_val*/ 1, NULL }
};

#define	SA_NUM_PROT_ENTS nitems(sa_prot_table)

#define	SA_PROT_ENABLED(softc) ((softc->flags & SA_FLAG_PROTECT_SUPP)	\
	&& (softc->prot_info.cur_prot_state.initialized != 0)		\
	&& (softc->prot_info.cur_prot_state.prot_method != 0))

#define	SA_PROT_LEN(softc)	softc->prot_info.cur_prot_state.pi_length

struct sa_softc {
	sa_state	state;
	sa_flags	flags;
	sa_quirks	quirks;
	u_int		si_flags;
	struct cam_periph *periph;
	struct		bio_queue_head bio_queue;
	int		queue_count;
	struct		devstat *device_stats;
	struct sa_devs	devs;
	int		open_count;
	int		num_devs_to_destroy;
	int		blk_gran;
	int		blk_mask;
	int		blk_shift;
	u_int32_t	max_blk;
	u_int32_t	min_blk;
	u_int32_t	maxio;
	u_int32_t	cpi_maxio;
	int		allow_io_split;
	int		inject_eom;
	int		set_pews_status;
	u_int32_t	comp_algorithm;
	u_int32_t	saved_comp_algorithm;
	u_int32_t	media_blksize;
	u_int32_t	last_media_blksize;
	u_int32_t	media_numblks;
	u_int8_t	media_density;
	u_int8_t	speed;
	u_int8_t	scsi_rev;
	u_int8_t	dsreg;		/* mtio mt_dsreg, redux */
	int		buffer_mode;
	int		filemarks;
	union		ccb saved_ccb;
	int		last_resid_was_io;
	uint8_t		density_type_bits[SA_DENSITY_TYPES];
	int		density_info_valid[SA_DENSITY_TYPES];
	uint8_t		density_info[SA_DENSITY_TYPES][SRDS_MAX_LENGTH];

	struct sa_prot_info	prot_info;

	int		sili;
	int		eot_warn;

	/*
	 * Current position information.  -1 means that the given value is
	 * unknown.  fileno and blkno are always calculated.  blkno is
	 * relative to the previous file mark.  rep_fileno and rep_blkno
	 * are as reported by the drive, if it supports the long form
	 * report for the READ POSITION command.  rep_blkno is relative to
	 * the beginning of the partition.
	 *
	 * bop means that the drive is at the beginning of the partition.
	 * eop means that the drive is between early warning and end of
	 * partition, inside the current partition.
	 * bpew means that the position is in a PEWZ (Programmable Early
	 * Warning Zone)
	 */
	daddr_t		partition;	/* Absolute from BOT */
	daddr_t		fileno;		/* Relative to beginning of partition */
	daddr_t		blkno;		/* Relative to last file mark */
	daddr_t		rep_blkno;	/* Relative to beginning of partition */
	daddr_t		rep_fileno;	/* Relative to beginning of partition */
	int		bop;		/* Beginning of Partition */
	int		eop;		/* End of Partition */
	int		bpew;		/* Beyond Programmable Early Warning */

	/*
	 * Latched Error Info
	 */
	struct {
		struct scsi_sense_data _last_io_sense;
		u_int64_t _last_io_resid;
		u_int8_t _last_io_cdb[CAM_MAX_CDBLEN];
		struct scsi_sense_data _last_ctl_sense;
		u_int64_t _last_ctl_resid;
		u_int8_t _last_ctl_cdb[CAM_MAX_CDBLEN];
#define	last_io_sense	errinfo._last_io_sense
#define	last_io_resid	errinfo._last_io_resid
#define	last_io_cdb	errinfo._last_io_cdb
#define	last_ctl_sense	errinfo._last_ctl_sense
#define	last_ctl_resid	errinfo._last_ctl_resid
#define	last_ctl_cdb	errinfo._last_ctl_cdb
	} errinfo;
	/*
	 * Misc other flags/state
	 */
	u_int32_t
					: 29,
		open_rdonly		: 1,	/* open read-only */
		open_pending_mount	: 1,	/* open pending mount */
		ctrl_mode		: 1;	/* control device open */

	struct task		sysctl_task;
	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
};

struct sa_quirk_entry {
	struct scsi_inquiry_pattern inq_pat;	/* matching pattern */
	sa_quirks quirks;	/* specific quirk type */
	u_int32_t prefblk;	/* preferred blocksize when in fixed mode */
};

static struct sa_quirk_entry sa_quirk_table[] =
{
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "OnStream",
		  "ADR*", "*"}, SA_QUIRK_FIXED|SA_QUIRK_NODREAD |
		   SA_QUIRK_1FM|SA_QUIRK_NO_MODESEL, 32768
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "ARCHIVE",
		  "Python 06408*", "*"}, SA_QUIRK_NODREAD, 0
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "ARCHIVE",
		  "Python 25601*", "*"}, SA_QUIRK_NOCOMP|SA_QUIRK_NODREAD, 0
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "ARCHIVE",
		  "Python*", "*"}, SA_QUIRK_NODREAD, 0
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "ARCHIVE",
		  "VIPER 150*", "*"}, SA_QUIRK_FIXED|SA_QUIRK_1FM, 512
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "ARCHIVE",
		  "VIPER 2525 25462", "-011"},
		  SA_QUIRK_NOCOMP|SA_QUIRK_1FM|SA_QUIRK_NODREAD, 0
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "ARCHIVE",
		  "VIPER 2525*", "*"}, SA_QUIRK_FIXED|SA_QUIRK_1FM, 1024
	},
#if	0
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "HP",
		  "C15*", "*"}, SA_QUIRK_VARIABLE|SA_QUIRK_NO_CPAGE, 0,
	},
#endif
 	{
 		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "HP",
		  "C56*", "*"}, SA_QUIRK_VARIABLE|SA_QUIRK_2FM, 0
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "HP",
		  "T20*", "*"}, SA_QUIRK_FIXED|SA_QUIRK_1FM, 512
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "HP",
		  "T4000*", "*"}, SA_QUIRK_FIXED|SA_QUIRK_1FM, 512
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "HP",
		  "HP-88780*", "*"}, SA_QUIRK_VARIABLE|SA_QUIRK_2FM, 0
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "KENNEDY",
		  "*", "*"}, SA_QUIRK_VARIABLE|SA_QUIRK_2FM, 0
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "M4 DATA",
		  "123107 SCSI*", "*"}, SA_QUIRK_VARIABLE|SA_QUIRK_2FM, 0
	},
	{	/* jreynold@primenet.com */
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "Seagate",
		"STT8000N*", "*"}, SA_QUIRK_1FM, 0
	},
	{	/* mike@sentex.net */
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "Seagate",
		"STT20000*", "*"}, SA_QUIRK_1FM, 0
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "SEAGATE",
		"DAT    06241-XXX", "*"}, SA_QUIRK_VARIABLE|SA_QUIRK_2FM, 0
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "TANDBERG",
		  " TDC 3600", "U07:"}, SA_QUIRK_NOCOMP|SA_QUIRK_1FM, 512
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "TANDBERG",
		  " TDC 3800", "*"}, SA_QUIRK_NOCOMP|SA_QUIRK_1FM, 512
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "TANDBERG",
		  " TDC 4100", "*"}, SA_QUIRK_NOCOMP|SA_QUIRK_1FM, 512
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "TANDBERG",
		  " TDC 4200", "*"}, SA_QUIRK_NOCOMP|SA_QUIRK_1FM, 512
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "TANDBERG",
		  " SLR*", "*"}, SA_QUIRK_1FM, 0
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "WANGTEK",
		  "5525ES*", "*"}, SA_QUIRK_FIXED|SA_QUIRK_1FM, 512
	},
	{
		{ T_SEQUENTIAL, SIP_MEDIA_REMOVABLE, "WANGTEK",
		  "51000*", "*"}, SA_QUIRK_FIXED|SA_QUIRK_1FM, 1024
	}
};

static	d_open_t	saopen;
static	d_close_t	saclose;
static	d_strategy_t	sastrategy;
static	d_ioctl_t	saioctl;
static	periph_init_t	sainit;
static	periph_ctor_t	saregister;
static	periph_oninv_t	saoninvalidate;
static	periph_dtor_t	sacleanup;
static	periph_start_t	sastart;
static	void		saasync(void *callback_arg, u_int32_t code,
				struct cam_path *path, void *arg);
static	void		sadone(struct cam_periph *periph,
			       union ccb *start_ccb);
static  int		saerror(union ccb *ccb, u_int32_t cam_flags,
				u_int32_t sense_flags);
static int		samarkswanted(struct cam_periph *);
static int		sacheckeod(struct cam_periph *periph);
static int		sagetparams(struct cam_periph *periph,
				    sa_params params_to_get,
				    u_int32_t *blocksize, u_int8_t *density,
				    u_int32_t *numblocks, int *buff_mode,
				    u_int8_t *write_protect, u_int8_t *speed,
				    int *comp_supported, int *comp_enabled,
				    u_int32_t *comp_algorithm,
				    sa_comp_t *comp_page,
				    struct scsi_control_data_prot_subpage
				    *prot_page, int dp_size,
				    int prot_changeable);
static int		sasetprot(struct cam_periph *periph,
				  struct sa_prot_state *new_prot);
static int		sasetparams(struct cam_periph *periph,
				    sa_params params_to_set,
				    u_int32_t blocksize, u_int8_t density,
				    u_int32_t comp_algorithm,
				    u_int32_t sense_flags);
static int		sasetsili(struct cam_periph *periph,
				  struct mtparamset *ps, int num_params);
static int		saseteotwarn(struct cam_periph *periph,
				     struct mtparamset *ps, int num_params);
static void		safillprot(struct sa_softc *softc, int *indent,
				   struct sbuf *sb);
static void		sapopulateprots(struct sa_prot_state *cur_state,
					struct sa_prot_map *new_table,
					int table_ents);
static struct sa_prot_map *safindprotent(char *name, struct sa_prot_map *table,
					 int table_ents);
static int		sasetprotents(struct cam_periph *periph,
				      struct mtparamset *ps, int num_params);
static struct sa_param_ent *safindparament(struct mtparamset *ps);
static int		saparamsetlist(struct cam_periph *periph,
				       struct mtsetlist *list, int need_copy);
static	int		saextget(struct cdev *dev, struct cam_periph *periph,
				 struct sbuf *sb, struct mtextget *g);
static	int		saparamget(struct sa_softc *softc, struct sbuf *sb);
static void		saprevent(struct cam_periph *periph, int action);
static int		sarewind(struct cam_periph *periph);
static int		saspace(struct cam_periph *periph, int count,
				scsi_space_code code);
static void		sadevgonecb(void *arg);
static void		sasetupdev(struct sa_softc *softc, struct cdev *dev);
static int		samount(struct cam_periph *, int, struct cdev *);
static int		saretension(struct cam_periph *periph);
static int		sareservereleaseunit(struct cam_periph *periph,
					     int reserve);
static int		saloadunload(struct cam_periph *periph, int load);
static int		saerase(struct cam_periph *periph, int longerase);
static int		sawritefilemarks(struct cam_periph *periph,
					 int nmarks, int setmarks, int immed);
static int		sagetpos(struct cam_periph *periph);
static int		sardpos(struct cam_periph *periph, int, u_int32_t *);
static int		sasetpos(struct cam_periph *periph, int, 
				 struct mtlocate *);
static void		safilldenstypesb(struct sbuf *sb, int *indent,
					 uint8_t *buf, int buf_len,
					 int is_density);
static void		safilldensitysb(struct sa_softc *softc, int *indent,
					struct sbuf *sb);


#ifndef	SA_DEFAULT_IO_SPLIT
#define	SA_DEFAULT_IO_SPLIT	0
#endif

static int sa_allow_io_split = SA_DEFAULT_IO_SPLIT;

/*
 * Tunable to allow the user to set a global allow_io_split value.  Note
 * that this WILL GO AWAY in FreeBSD 11.0.  Silently splitting the I/O up
 * is bad behavior, because it hides the true tape block size from the
 * application.
 */
static SYSCTL_NODE(_kern_cam, OID_AUTO, sa, CTLFLAG_RD, 0,
		  "CAM Sequential Access Tape Driver");
SYSCTL_INT(_kern_cam_sa, OID_AUTO, allow_io_split, CTLFLAG_RDTUN,
    &sa_allow_io_split, 0, "Default I/O split value");

static struct periph_driver sadriver =
{
	sainit, "sa",
	TAILQ_HEAD_INITIALIZER(sadriver.units), /* generation */ 0
};

PERIPHDRIVER_DECLARE(sa, sadriver);

/* For 2.2-stable support */
#ifndef D_TAPE
#define D_TAPE 0
#endif


static struct cdevsw sa_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	saopen,
	.d_close =	saclose,
	.d_read =	physread,
	.d_write =	physwrite,
	.d_ioctl =	saioctl,
	.d_strategy =	sastrategy,
	.d_name =	"sa",
	.d_flags =	D_TAPE | D_TRACKCLOSE,
};

static int
saopen(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct cam_periph *periph;
	struct sa_softc *softc;
	int error;

	periph = (struct cam_periph *)dev->si_drv1;
	if (cam_periph_acquire(periph) != 0) {
		return (ENXIO);
	}

	cam_periph_lock(periph);

	softc = (struct sa_softc *)periph->softc;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE|CAM_DEBUG_INFO,
	    ("saopen(%s): softc=0x%x\n", devtoname(dev), softc->flags));

	if (SA_IS_CTRL(dev)) {
		softc->ctrl_mode = 1;
		softc->open_count++;
		cam_periph_unlock(periph);
		return (0);
	}

	if ((error = cam_periph_hold(periph, PRIBIO|PCATCH)) != 0) {
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return (error);
	}

	if (softc->flags & SA_FLAG_OPEN) {
		error = EBUSY;
	} else if (softc->flags & SA_FLAG_INVALID) {
		error = ENXIO;
	} else {
		/*
		 * Preserve whether this is a read_only open.
		 */
		softc->open_rdonly = (flags & O_RDWR) == O_RDONLY;

		/*
		 * The function samount ensures media is loaded and ready.
		 * It also does a device RESERVE if the tape isn't yet mounted.
		 *
		 * If the mount fails and this was a non-blocking open,
		 * make this a 'open_pending_mount' action.
		 */
		error = samount(periph, flags, dev);
		if (error && (flags & O_NONBLOCK)) {
			softc->flags |= SA_FLAG_OPEN;
			softc->open_pending_mount = 1;
			softc->open_count++;
			cam_periph_unhold(periph);
			cam_periph_unlock(periph);
			return (0);
		}
	}

	if (error) {
		cam_periph_unhold(periph);
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return (error);
	}

	saprevent(periph, PR_PREVENT);
	softc->flags |= SA_FLAG_OPEN;
	softc->open_count++;

	cam_periph_unhold(periph);
	cam_periph_unlock(periph);
	return (error);
}

static int
saclose(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct	cam_periph *periph;
	struct	sa_softc *softc;
	int	mode, error, writing, tmp, i;
	int	closedbits = SA_FLAG_OPEN;

	mode = SAMODE(dev);
	periph = (struct cam_periph *)dev->si_drv1;
	cam_periph_lock(periph);

	softc = (struct sa_softc *)periph->softc;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE|CAM_DEBUG_INFO,
	    ("saclose(%s): softc=0x%x\n", devtoname(dev), softc->flags));


	softc->open_rdonly = 0; 
	if (SA_IS_CTRL(dev)) {
		softc->ctrl_mode = 0;
		softc->open_count--;
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return (0);
	}

	if (softc->open_pending_mount) {
		softc->flags &= ~SA_FLAG_OPEN;
		softc->open_pending_mount = 0; 
		softc->open_count--;
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return (0);
	}

	if ((error = cam_periph_hold(periph, PRIBIO)) != 0) {
		cam_periph_unlock(periph);
		return (error);
	}

	/*
	 * Were we writing the tape?
	 */
	writing = (softc->flags & SA_FLAG_TAPE_WRITTEN) != 0;

	/*
	 * See whether or not we need to write filemarks. If this
	 * fails, we probably have to assume we've lost tape
	 * position.
	 */
	error = sacheckeod(periph);
	if (error) {
		xpt_print(periph->path,
		    "failed to write terminating filemark(s)\n");
		softc->flags |= SA_FLAG_TAPE_FROZEN;
	}

	/*
	 * Whatever we end up doing, allow users to eject tapes from here on.
	 */
	saprevent(periph, PR_ALLOW);

	/*
	 * Decide how to end...
	 */
	if ((softc->flags & SA_FLAG_TAPE_MOUNTED) == 0) {
		closedbits |= SA_FLAG_TAPE_FROZEN;
	} else switch (mode) {
	case SA_MODE_OFFLINE:
		/*
		 * An 'offline' close is an unconditional release of
		 * frozen && mount conditions, irrespective of whether
		 * these operations succeeded. The reason for this is
		 * to allow at least some kind of programmatic way
		 * around our state getting all fouled up. If somebody
		 * issues an 'offline' command, that will be allowed
		 * to clear state.
		 */
		(void) sarewind(periph);
		(void) saloadunload(periph, FALSE);
		closedbits |= SA_FLAG_TAPE_MOUNTED|SA_FLAG_TAPE_FROZEN;
		break;
	case SA_MODE_REWIND:
		/*
		 * If the rewind fails, return an error- if anyone cares,
		 * but not overwriting any previous error.
		 *
		 * We don't clear the notion of mounted here, but we do
		 * clear the notion of frozen if we successfully rewound.
		 */
		tmp = sarewind(periph);
		if (tmp) {
			if (error != 0)
				error = tmp;
		} else {
			closedbits |= SA_FLAG_TAPE_FROZEN;
		}
		break;
	case SA_MODE_NOREWIND:
		/*
		 * If we're not rewinding/unloading the tape, find out
		 * whether we need to back up over one of two filemarks
		 * we wrote (if we wrote two filemarks) so that appends
		 * from this point on will be sane.
		 */
		if (error == 0 && writing && (softc->quirks & SA_QUIRK_2FM)) {
			tmp = saspace(periph, -1, SS_FILEMARKS);
			if (tmp) {
				xpt_print(periph->path, "unable to backspace "
				    "over one of double filemarks at end of "
				    "tape\n");
				xpt_print(periph->path, "it is possible that "
				    "this device needs a SA_QUIRK_1FM quirk set"
				    "for it\n");
				softc->flags |= SA_FLAG_TAPE_FROZEN;
			}
		}
		break;
	default:
		xpt_print(periph->path, "unknown mode 0x%x in saclose\n", mode);
		/* NOTREACHED */
		break;
	}

	/*
	 * We wish to note here that there are no more filemarks to be written.
	 */
	softc->filemarks = 0;
	softc->flags &= ~SA_FLAG_TAPE_WRITTEN;

	/*
	 * And we are no longer open for business.
	 */
	softc->flags &= ~closedbits;
	softc->open_count--;

	/*
	 * Invalidate any density information that depends on having tape
	 * media in the drive.
	 */
	for (i = 0; i < SA_DENSITY_TYPES; i++) {
		if (softc->density_type_bits[i] & SRDS_MEDIA)
			softc->density_info_valid[i] = 0;
	}

	/*
	 * Inform users if tape state if frozen....
	 */
	if (softc->flags & SA_FLAG_TAPE_FROZEN) {
		xpt_print(periph->path, "tape is now frozen- use an OFFLINE, "
		    "REWIND or MTEOM command to clear this state.\n");
	}
	
	/* release the device if it is no longer mounted */
	if ((softc->flags & SA_FLAG_TAPE_MOUNTED) == 0)
		sareservereleaseunit(periph, FALSE);

	cam_periph_unhold(periph);
	cam_periph_unlock(periph);
	cam_periph_release(periph);

	return (error);	
}

/*
 * Actually translate the requested transfer into one the physical driver
 * can understand.  The transfer is described by a buf and will include
 * only one physical transfer.
 */
static void
sastrategy(struct bio *bp)
{
	struct cam_periph *periph;
	struct sa_softc *softc;
	
	bp->bio_resid = bp->bio_bcount;
	if (SA_IS_CTRL(bp->bio_dev)) {
		biofinish(bp, NULL, EINVAL);
		return;
	}
	periph = (struct cam_periph *)bp->bio_dev->si_drv1;
	cam_periph_lock(periph);

	softc = (struct sa_softc *)periph->softc;

	if (softc->flags & SA_FLAG_INVALID) {
		cam_periph_unlock(periph);
		biofinish(bp, NULL, ENXIO);
		return;
	}

	if (softc->flags & SA_FLAG_TAPE_FROZEN) {
		cam_periph_unlock(periph);
		biofinish(bp, NULL, EPERM);
		return;
	}

	/*
	 * This should actually never occur as the write(2)
	 * system call traps attempts to write to a read-only
	 * file descriptor.
	 */
	if (bp->bio_cmd == BIO_WRITE && softc->open_rdonly) {
		cam_periph_unlock(periph);
		biofinish(bp, NULL, EBADF);
		return;
	}

	if (softc->open_pending_mount) {
		int error = samount(periph, 0, bp->bio_dev);
		if (error) {
			cam_periph_unlock(periph);
			biofinish(bp, NULL, ENXIO);
			return;
		}
		saprevent(periph, PR_PREVENT);
		softc->open_pending_mount = 0;
	}


	/*
	 * If it's a null transfer, return immediately
	 */
	if (bp->bio_bcount == 0) {
		cam_periph_unlock(periph);
		biodone(bp);
		return;
	}

	/* valid request?  */
	if (softc->flags & SA_FLAG_FIXED) {
		/*
		 * Fixed block device.  The byte count must
		 * be a multiple of our block size.
		 */
		if (((softc->blk_mask != ~0) &&
		    ((bp->bio_bcount & softc->blk_mask) != 0)) ||
		    ((softc->blk_mask == ~0) &&
		    ((bp->bio_bcount % softc->min_blk) != 0))) {
			xpt_print(periph->path, "Invalid request.  Fixed block "
			    "device requests must be a multiple of %d bytes\n",
			    softc->min_blk);
			cam_periph_unlock(periph);
			biofinish(bp, NULL, EINVAL);
			return;
		}
	} else if ((bp->bio_bcount > softc->max_blk) ||
		   (bp->bio_bcount < softc->min_blk) ||
		   (bp->bio_bcount & softc->blk_mask) != 0) {

		xpt_print_path(periph->path);
		printf("Invalid request.  Variable block "
		    "device requests must be ");
		if (softc->blk_mask != 0) {
			printf("a multiple of %d ", (0x1 << softc->blk_gran));
		}
		printf("between %d and %d bytes\n", softc->min_blk,
		    softc->max_blk);
		cam_periph_unlock(periph);
		biofinish(bp, NULL, EINVAL);
		return;
        }
	
	/*
	 * Place it at the end of the queue.
	 */
	bioq_insert_tail(&softc->bio_queue, bp);
	softc->queue_count++;
#if	0
	CAM_DEBUG(periph->path, CAM_DEBUG_INFO,
	    ("sastrategy: queuing a %ld %s byte %s\n", bp->bio_bcount,
 	    (softc->flags & SA_FLAG_FIXED)?  "fixed" : "variable",
	    (bp->bio_cmd == BIO_READ)? "read" : "write"));
#endif
	if (softc->queue_count > 1) {
		CAM_DEBUG(periph->path, CAM_DEBUG_INFO,
		    ("sastrategy: queue count now %d\n", softc->queue_count));
	}
	
	/*
	 * Schedule ourselves for performing the work.
	 */
	xpt_schedule(periph, CAM_PRIORITY_NORMAL);
	cam_periph_unlock(periph);

	return;
}

static int
sasetsili(struct cam_periph *periph, struct mtparamset *ps, int num_params)
{
	uint32_t sili_blocksize;
	struct sa_softc *softc;
	int error;

	error = 0;
	softc = (struct sa_softc *)periph->softc;

	if (ps->value_type != MT_PARAM_SET_SIGNED) {
		snprintf(ps->error_str, sizeof(ps->error_str),
		    "sili is a signed parameter");
		goto bailout;
	}
	if ((ps->value.value_signed < 0)
	 || (ps->value.value_signed > 1)) {
		snprintf(ps->error_str, sizeof(ps->error_str),
		    "invalid sili value %jd", (intmax_t)ps->value.value_signed);
		goto bailout_error;
	}
	/*
	 * We only set the SILI flag in variable block
	 * mode.  You'll get a check condition in fixed
	 * block mode if things don't line up in any case.
	 */
	if (softc->flags & SA_FLAG_FIXED) {
		snprintf(ps->error_str, sizeof(ps->error_str),
		    "can't set sili bit in fixed block mode");
		goto bailout_error;
	}
	if (softc->sili == ps->value.value_signed)
		goto bailout;

	if (ps->value.value_signed == 1)
		sili_blocksize = 4;
	else
		sili_blocksize = 0;

	error = sasetparams(periph, SA_PARAM_BLOCKSIZE,
			    sili_blocksize, 0, 0, SF_QUIET_IR);
	if (error != 0) {
		snprintf(ps->error_str, sizeof(ps->error_str),
		    "sasetparams() returned error %d", error);
		goto bailout_error;
	}

	softc->sili = ps->value.value_signed;

bailout:
	ps->status = MT_PARAM_STATUS_OK;
	return (error);

bailout_error:
	ps->status = MT_PARAM_STATUS_ERROR;
	if (error == 0)
		error = EINVAL;

	return (error);
}

static int
saseteotwarn(struct cam_periph *periph, struct mtparamset *ps, int num_params)
{
	struct sa_softc *softc;
	int error;

	error = 0;
	softc = (struct sa_softc *)periph->softc;

	if (ps->value_type != MT_PARAM_SET_SIGNED) {
		snprintf(ps->error_str, sizeof(ps->error_str),
		    "eot_warn is a signed parameter");
		ps->status = MT_PARAM_STATUS_ERROR;
		goto bailout;
	}
	if ((ps->value.value_signed < 0)
	 || (ps->value.value_signed > 1)) {
		snprintf(ps->error_str, sizeof(ps->error_str),
		    "invalid eot_warn value %jd\n",
		    (intmax_t)ps->value.value_signed);
		ps->status = MT_PARAM_STATUS_ERROR;
		goto bailout;
	}
	softc->eot_warn = ps->value.value_signed;
	ps->status = MT_PARAM_STATUS_OK;
bailout:
	if (ps->status != MT_PARAM_STATUS_OK)
		error = EINVAL;

	return (error);
}


static void
safillprot(struct sa_softc *softc, int *indent, struct sbuf *sb)
{
	int tmpint;

	SASBADDNODE(sb, *indent, protection);
	if (softc->flags & SA_FLAG_PROTECT_SUPP)
		tmpint = 1;
	else
		tmpint = 0;
	SASBADDINTDESC(sb, *indent, tmpint, %d, protection_supported,
	    "Set to 1 if protection information is supported");

	if ((tmpint != 0)
	 && (softc->prot_info.cur_prot_state.initialized != 0)) {
		struct sa_prot_state *prot;

		prot = &softc->prot_info.cur_prot_state;

		SASBADDUINTDESC(sb, *indent, prot->prot_method, %u,
		    prot_method, "Current Protection Method");
		SASBADDUINTDESC(sb, *indent, prot->pi_length, %u,
		    pi_length, "Length of Protection Information");
		SASBADDUINTDESC(sb, *indent, prot->lbp_w, %u,
		    lbp_w, "Check Protection on Writes");
		SASBADDUINTDESC(sb, *indent, prot->lbp_r, %u,
		    lbp_r, "Check and Include Protection on Reads");
		SASBADDUINTDESC(sb, *indent, prot->rbdp, %u,
		    rbdp, "Transfer Protection Information for RECOVER "
		    "BUFFERED DATA command");
	}
	SASBENDNODE(sb, *indent, protection);
}

static void
sapopulateprots(struct sa_prot_state *cur_state, struct sa_prot_map *new_table,
    int table_ents)
{
	int i;

	bcopy(sa_prot_table, new_table, min(table_ents * sizeof(*new_table),
	    sizeof(sa_prot_table)));

	table_ents = min(table_ents, SA_NUM_PROT_ENTS);

	for (i = 0; i < table_ents; i++)
		new_table[i].value = (uint32_t *)((uint8_t *)cur_state +
		    new_table[i].offset);

	return;
}

static struct sa_prot_map *
safindprotent(char *name, struct sa_prot_map *table, int table_ents)
{
	char *prot_name = "protection.";
	int i, prot_len;

	prot_len = strlen(prot_name);

	/*
	 * This shouldn't happen, but we check just in case.
	 */
	if (strncmp(name, prot_name, prot_len) != 0)
		goto bailout;

	for (i = 0; i < table_ents; i++) {
		if (strcmp(&name[prot_len], table[i].name) != 0)
			continue;
		return (&table[i]);
	}
bailout:
	return (NULL);
}

static int
sasetprotents(struct cam_periph *periph, struct mtparamset *ps, int num_params)
{
	struct sa_softc *softc;
	struct sa_prot_map prot_ents[SA_NUM_PROT_ENTS];
	struct sa_prot_state new_state;
	int error;
	int i;

	softc = (struct sa_softc *)periph->softc;
	error = 0;

	/*
	 * Make sure that this tape drive supports protection information.
	 * Otherwise we can't set anything.
	 */
	if ((softc->flags & SA_FLAG_PROTECT_SUPP) == 0) {
		snprintf(ps[0].error_str, sizeof(ps[0].error_str),
		    "Protection information is not supported for this device");
		ps[0].status = MT_PARAM_STATUS_ERROR;
		goto bailout;
	}

	/*
	 * We can't operate with physio(9) splitting enabled, because there
	 * is no way to insure (especially in variable block mode) that
	 * what the user writes (with a checksum block at the end) will 
	 * make it into the sa(4) driver intact.
	 */
	if ((softc->si_flags & SI_NOSPLIT) == 0) {
		snprintf(ps[0].error_str, sizeof(ps[0].error_str),
		    "Protection information cannot be enabled with I/O "
		    "splitting");
		ps[0].status = MT_PARAM_STATUS_ERROR;
		goto bailout;
	}

	/*
	 * Take the current cached protection state and use that as the
	 * basis for our new entries.
	 */
	bcopy(&softc->prot_info.cur_prot_state, &new_state, sizeof(new_state));

	/*
	 * Populate the table mapping property names to pointers into the
	 * state structure.
	 */
	sapopulateprots(&new_state, prot_ents, SA_NUM_PROT_ENTS);

	/*
	 * For each parameter the user passed in, make sure the name, type
	 * and value are valid.
	 */
	for (i = 0; i < num_params; i++) {
		struct sa_prot_map *ent;

		ent = safindprotent(ps[i].value_name, prot_ents,
		    SA_NUM_PROT_ENTS);
		if (ent == NULL) {
			ps[i].status = MT_PARAM_STATUS_ERROR;
			snprintf(ps[i].error_str, sizeof(ps[i].error_str),
			    "Invalid protection entry name %s",
			    ps[i].value_name);
			error = EINVAL;
			goto bailout;
		}
		if (ent->param_type != ps[i].value_type) {
			ps[i].status = MT_PARAM_STATUS_ERROR;
			snprintf(ps[i].error_str, sizeof(ps[i].error_str),
			    "Supplied type %d does not match actual type %d",
			    ps[i].value_type, ent->param_type);
			error = EINVAL;
			goto bailout;
		}
		if ((ps[i].value.value_unsigned < ent->min_val)
		 || (ps[i].value.value_unsigned > ent->max_val)) {
			ps[i].status = MT_PARAM_STATUS_ERROR;
			snprintf(ps[i].error_str, sizeof(ps[i].error_str),
			    "Value %ju is outside valid range %u - %u",
			    (uintmax_t)ps[i].value.value_unsigned, ent->min_val,
			    ent->max_val);
			error = EINVAL;
			goto bailout;
		}
		*(ent->value) = ps[i].value.value_unsigned;
	}

	/*
	 * Actually send the protection settings to the drive.
	 */
	error = sasetprot(periph, &new_state);
	if (error != 0) {
		for (i = 0; i < num_params; i++) {
			ps[i].status = MT_PARAM_STATUS_ERROR;
			snprintf(ps[i].error_str, sizeof(ps[i].error_str),
			    "Unable to set parameter, see dmesg(8)");
		}
		goto bailout;
	}

	/*
	 * Let the user know that his settings were stored successfully.
	 */
	for (i = 0; i < num_params; i++)
		ps[i].status = MT_PARAM_STATUS_OK;

bailout:
	return (error);
}
/*
 * Entry handlers generally only handle a single entry.  Node handlers will
 * handle a contiguous range of parameters to set in a single call.
 */
typedef enum {
	SA_PARAM_TYPE_ENTRY,
	SA_PARAM_TYPE_NODE
} sa_param_type;

struct sa_param_ent {
	char *name;
	sa_param_type param_type;
	int (*set_func)(struct cam_periph *periph, struct mtparamset *ps,
			int num_params);
} sa_param_table[] = {
	{"sili", SA_PARAM_TYPE_ENTRY, sasetsili },
	{"eot_warn", SA_PARAM_TYPE_ENTRY, saseteotwarn },
	{"protection.", SA_PARAM_TYPE_NODE, sasetprotents }
};

static struct sa_param_ent *
safindparament(struct mtparamset *ps)
{
	unsigned int i;

	for (i = 0; i < nitems(sa_param_table); i++){
		/*
		 * For entries, we compare all of the characters.  For
		 * nodes, we only compare the first N characters.  The node
		 * handler will decode the rest.
		 */
		if (sa_param_table[i].param_type == SA_PARAM_TYPE_ENTRY) {
			if (strcmp(ps->value_name, sa_param_table[i].name) != 0)
				continue;
		} else {
			if (strncmp(ps->value_name, sa_param_table[i].name,
			    strlen(sa_param_table[i].name)) != 0)
				continue;
		}
		return (&sa_param_table[i]);
	}

	return (NULL);
}

/*
 * Go through a list of parameters, coalescing contiguous parameters with
 * the same parent node into a single call to a set_func.
 */
static int
saparamsetlist(struct cam_periph *periph, struct mtsetlist *list,
    int need_copy)
{
	int i, contig_ents;
	int error;
	struct mtparamset *params, *first;
	struct sa_param_ent *first_ent;

	error = 0;
	params = NULL;

	if (list->num_params == 0)
		/* Nothing to do */
		goto bailout;

	/*
	 * Verify that the user has the correct structure size.
	 */
	if ((list->num_params * sizeof(struct mtparamset)) !=
	     list->param_len) {
		xpt_print(periph->path, "%s: length of params %d != "
		    "sizeof(struct mtparamset) %zd * num_params %d\n",
		    __func__, list->param_len, sizeof(struct mtparamset),
		    list->num_params);
		error = EINVAL;
		goto bailout;
	}

	if (need_copy != 0) {
		/*
		 * XXX KDM will dropping the lock cause an issue here?
		 */
		cam_periph_unlock(periph);
		params = malloc(list->param_len, M_SCSISA, M_WAITOK | M_ZERO);
		error = copyin(list->params, params, list->param_len);
		cam_periph_lock(periph);

		if (error != 0)
			goto bailout;
	} else {
		params = list->params;
	}

	contig_ents = 0;
	first = NULL;
	first_ent = NULL;
	for (i = 0; i < list->num_params; i++) {
		struct sa_param_ent *ent;

		ent = safindparament(&params[i]);
		if (ent == NULL) {
			snprintf(params[i].error_str,
			    sizeof(params[i].error_str),
			    "%s: cannot find parameter %s", __func__,
			    params[i].value_name);
			params[i].status = MT_PARAM_STATUS_ERROR;
			break;
		}

		if (first != NULL) {
			if (first_ent == ent) {
				/*
				 * We're still in a contiguous list of
				 * parameters that can be handled by one
				 * node handler.
				 */
				contig_ents++;
				continue;
			} else {
				error = first_ent->set_func(periph, first,
				    contig_ents);
				first = NULL;
				first_ent = NULL;
				contig_ents = 0;
				if (error != 0) {
					error = 0;
					break;
				}
			}
		}
		if (ent->param_type == SA_PARAM_TYPE_NODE) {
			first = &params[i];
			first_ent = ent;
			contig_ents = 1;
		} else {
			error = ent->set_func(periph, &params[i], 1);
			if (error != 0) {
				error = 0;
				break;
			}
		}
	}
	if (first != NULL)
		first_ent->set_func(periph, first, contig_ents);

bailout:
	if (need_copy != 0) {
		if (error != EFAULT) {
			cam_periph_unlock(periph);
			copyout(params, list->params, list->param_len);
			cam_periph_lock(periph);
		}
		free(params, M_SCSISA);
	}
	return (error);
}

static int
sagetparams_common(struct cdev *dev, struct cam_periph *periph)
{
	struct sa_softc *softc;
	u_int8_t write_protect;
	int comp_enabled, comp_supported, error;

	softc = (struct sa_softc *)periph->softc;

	if (softc->open_pending_mount)
		return (0);

	/* The control device may issue getparams() if there are no opens. */
	if (SA_IS_CTRL(dev) && (softc->flags & SA_FLAG_OPEN) != 0)
		return (0);

	error = sagetparams(periph, SA_PARAM_ALL, &softc->media_blksize,
	    &softc->media_density, &softc->media_numblks, &softc->buffer_mode,
	    &write_protect, &softc->speed, &comp_supported, &comp_enabled,
	    &softc->comp_algorithm, NULL, NULL, 0, 0);
	if (error)
		return (error);
	if (write_protect)
		softc->flags |= SA_FLAG_TAPE_WP;
	else
		softc->flags &= ~SA_FLAG_TAPE_WP;
	softc->flags &= ~SA_FLAG_COMPRESSION;
	if (comp_supported) {
		if (softc->saved_comp_algorithm == 0)
			softc->saved_comp_algorithm =
			    softc->comp_algorithm;
		softc->flags |= SA_FLAG_COMP_SUPP;
		if (comp_enabled)
			softc->flags |= SA_FLAG_COMP_ENABLED;
	} else  
		softc->flags |= SA_FLAG_COMP_UNSUPP;

	return (0);
}

#define	PENDING_MOUNT_CHECK(softc, periph, dev)		\
	if (softc->open_pending_mount) {		\
		error = samount(periph, 0, dev);	\
		if (error) {				\
			break;				\
		}					\
		saprevent(periph, PR_PREVENT);		\
		softc->open_pending_mount = 0;		\
	}

static int
saioctl(struct cdev *dev, u_long cmd, caddr_t arg, int flag, struct thread *td)
{
	struct cam_periph *periph;
	struct sa_softc *softc;
	scsi_space_code spaceop;
	int didlockperiph = 0;
	int mode;
	int error = 0;

	mode = SAMODE(dev);
	error = 0;		/* shut up gcc */
	spaceop = 0;		/* shut up gcc */

	periph = (struct cam_periph *)dev->si_drv1;
	cam_periph_lock(periph);
	softc = (struct sa_softc *)periph->softc;

	/*
	 * Check for control mode accesses. We allow MTIOCGET and
	 * MTIOCERRSTAT (but need to be the only one open in order
	 * to clear latched status), and MTSETBSIZE, MTSETDNSTY
	 * and MTCOMP (but need to be the only one accessing this
	 * device to run those).
	 */

	if (SA_IS_CTRL(dev)) {
		switch (cmd) {
		case MTIOCGETEOTMODEL:
		case MTIOCGET:
		case MTIOCEXTGET:
		case MTIOCPARAMGET:
		case MTIOCRBLIM:
			break;
		case MTIOCERRSTAT:
			/*
			 * If the periph isn't already locked, lock it
			 * so our MTIOCERRSTAT can reset latched error stats.
			 *
			 * If the periph is already locked, skip it because
			 * we're just getting status and it'll be up to the
			 * other thread that has this device open to do
			 * an MTIOCERRSTAT that would clear latched status.
			 */
			if ((periph->flags & CAM_PERIPH_LOCKED) == 0) {
				error = cam_periph_hold(periph, PRIBIO|PCATCH);
				if (error != 0) {
					cam_periph_unlock(periph);
					return (error);
				}
				didlockperiph = 1;
			}
			break;

		case MTIOCTOP:
		{
			struct mtop *mt = (struct mtop *) arg;

			/*
			 * Check to make sure it's an OP we can perform
			 * with no media inserted.
			 */
			switch (mt->mt_op) {
			case MTSETBSIZ:
			case MTSETDNSTY:
			case MTCOMP:
				mt = NULL;
				/* FALLTHROUGH */
			default:
				break;
			}
			if (mt != NULL) {
				break;
			}
			/* FALLTHROUGH */
		}
		case MTIOCSETEOTMODEL:
			/*
			 * We need to acquire the peripheral here rather
			 * than at open time because we are sharing writable
			 * access to data structures.
			 */
			error = cam_periph_hold(periph, PRIBIO|PCATCH);
			if (error != 0) {
				cam_periph_unlock(periph);
				return (error);
			}
			didlockperiph = 1;
			break;

		default:
			cam_periph_unlock(periph);
			return (EINVAL);
		}
	}

	/*
	 * Find the device that the user is talking about
	 */
	switch (cmd) {
	case MTIOCGET:
	{
		struct mtget *g = (struct mtget *)arg;

		error = sagetparams_common(dev, periph);
		if (error)
			break;
		bzero(g, sizeof(struct mtget));
		g->mt_type = MT_ISAR;
		if (softc->flags & SA_FLAG_COMP_UNSUPP) {
			g->mt_comp = MT_COMP_UNSUPP;
			g->mt_comp0 = MT_COMP_UNSUPP;
			g->mt_comp1 = MT_COMP_UNSUPP;
			g->mt_comp2 = MT_COMP_UNSUPP;
			g->mt_comp3 = MT_COMP_UNSUPP;
		} else {
			if ((softc->flags & SA_FLAG_COMP_ENABLED) == 0) {
				g->mt_comp = MT_COMP_DISABLED;
			} else {
				g->mt_comp = softc->comp_algorithm;
			}
			g->mt_comp0 = softc->comp_algorithm;
			g->mt_comp1 = softc->comp_algorithm;
			g->mt_comp2 = softc->comp_algorithm;
			g->mt_comp3 = softc->comp_algorithm;
		}
		g->mt_density = softc->media_density;
		g->mt_density0 = softc->media_density;
		g->mt_density1 = softc->media_density;
		g->mt_density2 = softc->media_density;
		g->mt_density3 = softc->media_density;
		g->mt_blksiz = softc->media_blksize;
		g->mt_blksiz0 = softc->media_blksize;
		g->mt_blksiz1 = softc->media_blksize;
		g->mt_blksiz2 = softc->media_blksize;
		g->mt_blksiz3 = softc->media_blksize;
		g->mt_fileno = softc->fileno;
		g->mt_blkno = softc->blkno;
		g->mt_dsreg = (short) softc->dsreg;
		/*
		 * Yes, we know that this is likely to overflow
		 */
		if (softc->last_resid_was_io) {
			if ((g->mt_resid = (short) softc->last_io_resid) != 0) {
				if (SA_IS_CTRL(dev) == 0 || didlockperiph) {
					softc->last_io_resid = 0;
				}
			}
		} else {
			if ((g->mt_resid = (short)softc->last_ctl_resid) != 0) {
				if (SA_IS_CTRL(dev) == 0 || didlockperiph) {
					softc->last_ctl_resid = 0;
				}
			}
		}
		error = 0;
		break;
	}
	case MTIOCEXTGET:
	case MTIOCPARAMGET:
	{
		struct mtextget *g = (struct mtextget *)arg;
		char *tmpstr2;
		struct sbuf *sb;

		/*
		 * Report drive status using an XML format.
		 */

		/*
		 * XXX KDM will dropping the lock cause any problems here?
		 */
		cam_periph_unlock(periph);
		sb = sbuf_new(NULL, NULL, g->alloc_len, SBUF_FIXEDLEN);
		if (sb == NULL) {
			g->status = MT_EXT_GET_ERROR;
			snprintf(g->error_str, sizeof(g->error_str),
				 "Unable to allocate %d bytes for status info",
				 g->alloc_len);
			cam_periph_lock(periph);
			goto extget_bailout;
		}
		cam_periph_lock(periph);

		if (cmd == MTIOCEXTGET)
			error = saextget(dev, periph, sb, g);
		else
			error = saparamget(softc, sb);

		if (error != 0)
			goto extget_bailout;

		error = sbuf_finish(sb);
		if (error == ENOMEM) {
			g->status = MT_EXT_GET_NEED_MORE_SPACE;
			error = 0;
		} else if (error != 0) {
			g->status = MT_EXT_GET_ERROR;
			snprintf(g->error_str, sizeof(g->error_str),
			    "Error %d returned from sbuf_finish()", error);
		} else
			g->status = MT_EXT_GET_OK;

		error = 0;
		tmpstr2 = sbuf_data(sb);
		g->fill_len = strlen(tmpstr2) + 1;
		cam_periph_unlock(periph);

		error = copyout(tmpstr2, g->status_xml, g->fill_len);

		cam_periph_lock(periph);

extget_bailout:
		sbuf_delete(sb);
		break;
	}
	case MTIOCPARAMSET:
	{
		struct mtsetlist list;
		struct mtparamset *ps = (struct mtparamset *)arg;
		
		bzero(&list, sizeof(list));
		list.num_params = 1;
		list.param_len = sizeof(*ps);
		list.params = ps;

		error = saparamsetlist(periph, &list, /*need_copy*/ 0);
		break;
	}
	case MTIOCSETLIST:
	{
		struct mtsetlist *list = (struct mtsetlist *)arg;

		error = saparamsetlist(periph, list, /*need_copy*/ 1);
		break;
	}
	case MTIOCERRSTAT:
	{
		struct scsi_tape_errors *sep =
		    &((union mterrstat *)arg)->scsi_errstat;

		CAM_DEBUG(periph->path, CAM_DEBUG_TRACE,
		    ("saioctl: MTIOCERRSTAT\n"));

		bzero(sep, sizeof(*sep));
		sep->io_resid = softc->last_io_resid;
		bcopy((caddr_t) &softc->last_io_sense, sep->io_sense,
		    sizeof (sep->io_sense));
		bcopy((caddr_t) &softc->last_io_cdb, sep->io_cdb,
		    sizeof (sep->io_cdb));
		sep->ctl_resid = softc->last_ctl_resid;
		bcopy((caddr_t) &softc->last_ctl_sense, sep->ctl_sense,
		    sizeof (sep->ctl_sense));
		bcopy((caddr_t) &softc->last_ctl_cdb, sep->ctl_cdb,
		    sizeof (sep->ctl_cdb));

		if ((SA_IS_CTRL(dev) == 0 && !softc->open_pending_mount) ||
		    didlockperiph)
			bzero((caddr_t) &softc->errinfo,
			    sizeof (softc->errinfo));
		error = 0;
		break;
	}
	case MTIOCTOP:
	{
		struct mtop *mt;
		int    count;

		PENDING_MOUNT_CHECK(softc, periph, dev);

		mt = (struct mtop *)arg;


		CAM_DEBUG(periph->path, CAM_DEBUG_TRACE,
			 ("saioctl: op=0x%x count=0x%x\n",
			  mt->mt_op, mt->mt_count));

		count = mt->mt_count;
		switch (mt->mt_op) {
		case MTWEOF:	/* write an end-of-file marker */
			/*
			 * We don't need to clear the SA_FLAG_TAPE_WRITTEN
			 * flag because by keeping track of filemarks
			 * we have last written we know whether or not
			 * we need to write more when we close the device.
			 */
			error = sawritefilemarks(periph, count, FALSE, FALSE);
			break;
		case MTWEOFI:
			/* write an end-of-file marker without waiting */
			error = sawritefilemarks(periph, count, FALSE, TRUE);
			break;
		case MTWSS:	/* write a setmark */
			error = sawritefilemarks(periph, count, TRUE, FALSE);
			break;
		case MTBSR:	/* backward space record */
		case MTFSR:	/* forward space record */
		case MTBSF:	/* backward space file */
		case MTFSF:	/* forward space file */
		case MTBSS:	/* backward space setmark */
		case MTFSS:	/* forward space setmark */
		case MTEOD:	/* space to end of recorded medium */
		{
			int nmarks;

			spaceop = SS_FILEMARKS;
			nmarks = softc->filemarks;
			error = sacheckeod(periph);
			if (error) {
				xpt_print(periph->path,
				    "EOD check prior to spacing failed\n");
				softc->flags |= SA_FLAG_EIO_PENDING;
				break;
			}
			nmarks -= softc->filemarks;
			switch(mt->mt_op) {
			case MTBSR:
				count = -count;
				/* FALLTHROUGH */
			case MTFSR:
				spaceop = SS_BLOCKS;
				break;
			case MTBSF:
				count = -count;
				/* FALLTHROUGH */
			case MTFSF:
				break;
			case MTBSS:
				count = -count;
				/* FALLTHROUGH */
			case MTFSS:
				spaceop = SS_SETMARKS;
				break;
			case MTEOD:
				spaceop = SS_EOD;
				count = 0;
				nmarks = 0;
				break;
			default:
				error = EINVAL;
				break;
			}
			if (error)
				break;

			nmarks = softc->filemarks;
			/*
			 * XXX: Why are we checking again?
			 */
			error = sacheckeod(periph);
			if (error)
				break;
			nmarks -= softc->filemarks;
			error = saspace(periph, count - nmarks, spaceop);
			/*
			 * At this point, clear that we've written the tape
			 * and that we've written any filemarks. We really
			 * don't know what the applications wishes to do next-
			 * the sacheckeod's will make sure we terminated the
			 * tape correctly if we'd been writing, but the next
			 * action the user application takes will set again
			 * whether we need to write filemarks.
			 */
			softc->flags &=
			    ~(SA_FLAG_TAPE_WRITTEN|SA_FLAG_TAPE_FROZEN);
			softc->filemarks = 0;
			break;
		}
		case MTREW:	/* rewind */
			PENDING_MOUNT_CHECK(softc, periph, dev);
			(void) sacheckeod(periph);
			error = sarewind(periph);
			/* see above */
			softc->flags &=
			    ~(SA_FLAG_TAPE_WRITTEN|SA_FLAG_TAPE_FROZEN);
			softc->flags &= ~SA_FLAG_ERR_PENDING;
			softc->filemarks = 0;
			break;
		case MTERASE:	/* erase */
			PENDING_MOUNT_CHECK(softc, periph, dev);
			error = saerase(periph, count);
			softc->flags &=
			    ~(SA_FLAG_TAPE_WRITTEN|SA_FLAG_TAPE_FROZEN);
			softc->flags &= ~SA_FLAG_ERR_PENDING;
			break;
		case MTRETENS:	/* re-tension tape */
			PENDING_MOUNT_CHECK(softc, periph, dev);
			error = saretension(periph);		
			softc->flags &=
			    ~(SA_FLAG_TAPE_WRITTEN|SA_FLAG_TAPE_FROZEN);
			softc->flags &= ~SA_FLAG_ERR_PENDING;
			break;
		case MTOFFL:	/* rewind and put the drive offline */

			PENDING_MOUNT_CHECK(softc, periph, dev);

			(void) sacheckeod(periph);
			/* see above */
			softc->flags &= ~SA_FLAG_TAPE_WRITTEN;
			softc->filemarks = 0;

			error = sarewind(periph);
			/* clear the frozen flag anyway */
			softc->flags &= ~SA_FLAG_TAPE_FROZEN;

			/*
			 * Be sure to allow media removal before ejecting.
			 */

			saprevent(periph, PR_ALLOW);
			if (error == 0) {
				error = saloadunload(periph, FALSE);
				if (error == 0) {
					softc->flags &= ~SA_FLAG_TAPE_MOUNTED;
				}
			}
			break;

		case MTLOAD:
			error = saloadunload(periph, TRUE);
			break;
		case MTNOP:	/* no operation, sets status only */
		case MTCACHE:	/* enable controller cache */
		case MTNOCACHE:	/* disable controller cache */
			error = 0;
			break;

		case MTSETBSIZ:	/* Set block size for device */

			PENDING_MOUNT_CHECK(softc, periph, dev);

			if ((softc->sili != 0)
			 && (count != 0)) {
				xpt_print(periph->path, "Can't enter fixed "
				    "block mode with SILI enabled\n");
				error = EINVAL;
				break;
			}
			error = sasetparams(periph, SA_PARAM_BLOCKSIZE, count,
					    0, 0, 0);
			if (error == 0) {
				softc->last_media_blksize =
				    softc->media_blksize;
				softc->media_blksize = count;
				if (count) {
					softc->flags |= SA_FLAG_FIXED;
					if (powerof2(count)) {
						softc->blk_shift =
						    ffs(count) - 1;
						softc->blk_mask = count - 1;
					} else {
						softc->blk_mask = ~0;
						softc->blk_shift = 0;
					}
					/*
					 * Make the user's desire 'persistent'.
					 */
					softc->quirks &= ~SA_QUIRK_VARIABLE;
					softc->quirks |= SA_QUIRK_FIXED;
				} else {
					softc->flags &= ~SA_FLAG_FIXED;
					if (softc->max_blk == 0) {
						softc->max_blk = ~0;
					}
					softc->blk_shift = 0;
					if (softc->blk_gran != 0) {
						softc->blk_mask =
						    softc->blk_gran - 1;
					} else {
						softc->blk_mask = 0;
					}
					/*
					 * Make the user's desire 'persistent'.
					 */
					softc->quirks |= SA_QUIRK_VARIABLE;
					softc->quirks &= ~SA_QUIRK_FIXED;
				}
			}
			break;
		case MTSETDNSTY:	/* Set density for device and mode */
			PENDING_MOUNT_CHECK(softc, periph, dev);

			if (count > UCHAR_MAX) {
				error = EINVAL;	
				break;
			} else {
				error = sasetparams(periph, SA_PARAM_DENSITY,
						    0, count, 0, 0);
			}
			break;
		case MTCOMP:	/* enable compression */
			PENDING_MOUNT_CHECK(softc, periph, dev);
			/*
			 * Some devices don't support compression, and
			 * don't like it if you ask them for the
			 * compression page.
			 */
			if ((softc->quirks & SA_QUIRK_NOCOMP) ||
			    (softc->flags & SA_FLAG_COMP_UNSUPP)) {
				error = ENODEV;
				break;
			}
			error = sasetparams(periph, SA_PARAM_COMPRESSION,
			    0, 0, count, SF_NO_PRINT);
			break;
		default:
			error = EINVAL;
		}
		break;
	}
	case MTIOCIEOT:
	case MTIOCEEOT:
		error = 0;
		break;
	case MTIOCRDSPOS:
		PENDING_MOUNT_CHECK(softc, periph, dev);
		error = sardpos(periph, 0, (u_int32_t *) arg);
		break;
	case MTIOCRDHPOS:
		PENDING_MOUNT_CHECK(softc, periph, dev);
		error = sardpos(periph, 1, (u_int32_t *) arg);
		break;
	case MTIOCSLOCATE:
	case MTIOCHLOCATE: {
		struct mtlocate locate_info;
		int hard;

		bzero(&locate_info, sizeof(locate_info));
		locate_info.logical_id = *((uint32_t *)arg);
		if (cmd == MTIOCSLOCATE)
			hard = 0;
		else
			hard = 1;

		PENDING_MOUNT_CHECK(softc, periph, dev);

		error = sasetpos(periph, hard, &locate_info);
		break;
	}
	case MTIOCEXTLOCATE:
		PENDING_MOUNT_CHECK(softc, periph, dev);
		error = sasetpos(periph, /*hard*/ 0, (struct mtlocate *)arg);
		softc->flags &=
		    ~(SA_FLAG_TAPE_WRITTEN|SA_FLAG_TAPE_FROZEN);
		softc->flags &= ~SA_FLAG_ERR_PENDING;
		softc->filemarks = 0;
		break;
	case MTIOCGETEOTMODEL:
		error = 0;
		if (softc->quirks & SA_QUIRK_1FM)
			mode = 1;
		else
			mode = 2;
		*((u_int32_t *) arg) = mode;
		break;
	case MTIOCSETEOTMODEL:
		error = 0;
		switch (*((u_int32_t *) arg)) {
		case 1:
			softc->quirks &= ~SA_QUIRK_2FM;
			softc->quirks |= SA_QUIRK_1FM;
			break;
		case 2:
			softc->quirks &= ~SA_QUIRK_1FM;
			softc->quirks |= SA_QUIRK_2FM;
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	case MTIOCRBLIM: {
		struct mtrblim *rblim;

		rblim = (struct mtrblim *)arg;

		rblim->granularity = softc->blk_gran;
		rblim->min_block_length = softc->min_blk;
		rblim->max_block_length = softc->max_blk;
		break;
	}
	default:
		error = cam_periph_ioctl(periph, cmd, arg, saerror);
		break;
	}

	/*
	 * Check to see if we cleared a frozen state
	 */
	if (error == 0 && (softc->flags & SA_FLAG_TAPE_FROZEN)) {
		switch(cmd) {
		case MTIOCRDSPOS:
		case MTIOCRDHPOS:
		case MTIOCSLOCATE:
		case MTIOCHLOCATE:
			/*
			 * XXX KDM look at this.
			 */
			softc->fileno = (daddr_t) -1;
			softc->blkno = (daddr_t) -1;
			softc->rep_blkno = (daddr_t) -1;
			softc->rep_fileno = (daddr_t) -1;
			softc->partition = (daddr_t) -1;
			softc->flags &= ~SA_FLAG_TAPE_FROZEN;
			xpt_print(periph->path,
			    "tape state now unfrozen.\n");
			break;
		default:
			break;
		}
	}
	if (didlockperiph) {
		cam_periph_unhold(periph);
	}
	cam_periph_unlock(periph);
	return (error);
}

static void
sainit(void)
{
	cam_status status;

	/*
	 * Install a global async callback.
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, saasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("sa: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	}
}

static void
sadevgonecb(void *arg)
{
	struct cam_periph *periph;
	struct mtx *mtx;
	struct sa_softc *softc;

	periph = (struct cam_periph *)arg;
	softc = (struct sa_softc *)periph->softc;

	mtx = cam_periph_mtx(periph);
	mtx_lock(mtx);

	softc->num_devs_to_destroy--;
	if (softc->num_devs_to_destroy == 0) {
		int i;

		/*
		 * When we have gotten all of our callbacks, we will get
		 * no more close calls from devfs.  So if we have any
		 * dangling opens, we need to release the reference held
		 * for that particular context.
		 */
		for (i = 0; i < softc->open_count; i++)
			cam_periph_release_locked(periph);

		softc->open_count = 0;

		/*
		 * Release the reference held for devfs, all of our
		 * instances are gone now.
		 */
		cam_periph_release_locked(periph);
	}

	/*
	 * We reference the lock directly here, instead of using
	 * cam_periph_unlock().  The reason is that the final call to
	 * cam_periph_release_locked() above could result in the periph
	 * getting freed.  If that is the case, dereferencing the periph
	 * with a cam_periph_unlock() call would cause a page fault.
	 */
	mtx_unlock(mtx);
}

static void
saoninvalidate(struct cam_periph *periph)
{
	struct sa_softc *softc;

	softc = (struct sa_softc *)periph->softc;

	/*
	 * De-register any async callbacks.
	 */
	xpt_register_async(0, saasync, periph, periph->path);

	softc->flags |= SA_FLAG_INVALID;

	/*
	 * Return all queued I/O with ENXIO.
	 * XXX Handle any transactions queued to the card
	 *     with XPT_ABORT_CCB.
	 */
	bioq_flush(&softc->bio_queue, NULL, ENXIO);
	softc->queue_count = 0;

	/*
	 * Tell devfs that all of our devices have gone away, and ask for a
	 * callback when it has cleaned up its state.
	 */
	destroy_dev_sched_cb(softc->devs.ctl_dev, sadevgonecb, periph);
	destroy_dev_sched_cb(softc->devs.r_dev, sadevgonecb, periph);
	destroy_dev_sched_cb(softc->devs.nr_dev, sadevgonecb, periph);
	destroy_dev_sched_cb(softc->devs.er_dev, sadevgonecb, periph);
}

static void
sacleanup(struct cam_periph *periph)
{
	struct sa_softc *softc;

	softc = (struct sa_softc *)periph->softc;

	cam_periph_unlock(periph);

	if ((softc->flags & SA_FLAG_SCTX_INIT) != 0
	 && sysctl_ctx_free(&softc->sysctl_ctx) != 0)
		xpt_print(periph->path, "can't remove sysctl context\n");

	cam_periph_lock(periph);

	devstat_remove_entry(softc->device_stats);

	free(softc, M_SCSISA);
}

static void
saasync(void *callback_arg, u_int32_t code,
	struct cam_path *path, void *arg)
{
	struct cam_periph *periph;

	periph = (struct cam_periph *)callback_arg;
	switch (code) {
	case AC_FOUND_DEVICE:
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
		if (SID_TYPE(&cgd->inq_data) != T_SEQUENTIAL)
			break;

		/*
		 * Allocate a peripheral instance for
		 * this device and start the probe
		 * process.
		 */
		status = cam_periph_alloc(saregister, saoninvalidate,
					  sacleanup, sastart,
					  "sa", CAM_PERIPH_BIO, path,
					  saasync, AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP
		 && status != CAM_REQ_INPROG)
			printf("saasync: Unable to probe new device "
				"due to status 0x%x\n", status);
		break;
	}
	default:
		cam_periph_async(periph, code, path, arg);
		break;
	}
}

static void
sasetupdev(struct sa_softc *softc, struct cdev *dev)
{

	dev->si_iosize_max = softc->maxio;
	dev->si_flags |= softc->si_flags;
	/*
	 * Keep a count of how many non-alias devices we have created,
	 * so we can make sure we clean them all up on shutdown.  Aliases
	 * are cleaned up when we destroy the device they're an alias for.
	 */
	if ((dev->si_flags & SI_ALIAS) == 0)
		softc->num_devs_to_destroy++;
}

static void
sasysctlinit(void *context, int pending)
{
	struct cam_periph *periph;
	struct sa_softc *softc;
	char tmpstr[32], tmpstr2[16];

	periph = (struct cam_periph *)context;
	/*
	 * If the periph is invalid, no need to setup the sysctls.
	 */
	if (periph->flags & CAM_PERIPH_INVALID)
		goto bailout;

	softc = (struct sa_softc *)periph->softc;

	snprintf(tmpstr, sizeof(tmpstr), "CAM SA unit %d", periph->unit_number);
	snprintf(tmpstr2, sizeof(tmpstr2), "%u", periph->unit_number);

	sysctl_ctx_init(&softc->sysctl_ctx);
	softc->flags |= SA_FLAG_SCTX_INIT;
	softc->sysctl_tree = SYSCTL_ADD_NODE_WITH_LABEL(&softc->sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_kern_cam_sa), OID_AUTO, tmpstr2,
	    CTLFLAG_RD, 0, tmpstr, "device_index");
	if (softc->sysctl_tree == NULL)
		goto bailout;

	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "allow_io_split", CTLFLAG_RDTUN | CTLFLAG_NOFETCH, 
	    &softc->allow_io_split, 0, "Allow Splitting I/O");
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "maxio", CTLFLAG_RD, 
	    &softc->maxio, 0, "Maximum I/O size");
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "cpi_maxio", CTLFLAG_RD, 
	    &softc->cpi_maxio, 0, "Maximum Controller I/O size");
	SYSCTL_ADD_INT(&softc->sysctl_ctx, SYSCTL_CHILDREN(softc->sysctl_tree),
	    OID_AUTO, "inject_eom", CTLFLAG_RW, 
	    &softc->inject_eom, 0, "Queue EOM for the next write/read");

bailout:
	/*
	 * Release the reference that was held when this task was enqueued.
	 */
	cam_periph_release(periph);
}

static cam_status
saregister(struct cam_periph *periph, void *arg)
{
	struct sa_softc *softc;
	struct ccb_getdev *cgd;
	struct ccb_pathinq cpi;
	struct make_dev_args args;
	caddr_t match;
	char tmpstr[80];
	int error;
	
	cgd = (struct ccb_getdev *)arg;
	if (cgd == NULL) {
		printf("saregister: no getdev CCB, can't register device\n");
		return (CAM_REQ_CMP_ERR);
	}

	softc = (struct sa_softc *)
	    malloc(sizeof (*softc), M_SCSISA, M_NOWAIT | M_ZERO);
	if (softc == NULL) {
		printf("saregister: Unable to probe new device. "
		       "Unable to allocate softc\n");				
		return (CAM_REQ_CMP_ERR);
	}
	softc->scsi_rev = SID_ANSI_REV(&cgd->inq_data);
	softc->state = SA_STATE_NORMAL;
	softc->fileno = (daddr_t) -1;
	softc->blkno = (daddr_t) -1;
	softc->rep_fileno = (daddr_t) -1;
	softc->rep_blkno = (daddr_t) -1;
	softc->partition = (daddr_t) -1;
	softc->bop = -1;
	softc->eop = -1;
	softc->bpew = -1;

	bioq_init(&softc->bio_queue);
	softc->periph = periph;
	periph->softc = softc;

	/*
	 * See if this device has any quirks.
	 */
	match = cam_quirkmatch((caddr_t)&cgd->inq_data,
			       (caddr_t)sa_quirk_table,
			       nitems(sa_quirk_table),
			       sizeof(*sa_quirk_table), scsi_inquiry_match);

	if (match != NULL) {
		softc->quirks = ((struct sa_quirk_entry *)match)->quirks;
		softc->last_media_blksize =
		    ((struct sa_quirk_entry *)match)->prefblk;
	} else
		softc->quirks = SA_QUIRK_NONE;

	/*
	 * Long format data for READ POSITION was introduced in SSC, which
	 * was after SCSI-2.  (Roughly equivalent to SCSI-3.)  If the drive
	 * reports that it is SCSI-2 or older, it is unlikely to support
	 * long position data, but it might.  Some drives from that era
	 * claim to be SCSI-2, but do support long position information.
	 * So, instead of immediately disabling long position information
	 * for SCSI-2 devices, we'll try one pass through sagetpos(), and 
	 * then disable long position information if we get an error.   
	 */
	if (cgd->inq_data.version <= SCSI_REV_CCS)
		softc->quirks |= SA_QUIRK_NO_LONG_POS;

	if (cgd->inq_data.spc3_flags & SPC3_SID_PROTECT) {
		struct ccb_dev_advinfo cdai;
		struct scsi_vpd_extended_inquiry_data ext_inq;

		bzero(&ext_inq, sizeof(ext_inq));

		xpt_setup_ccb(&cdai.ccb_h, periph->path, CAM_PRIORITY_NORMAL);

		cdai.ccb_h.func_code = XPT_DEV_ADVINFO;
		cdai.flags = CDAI_FLAG_NONE;
		cdai.buftype = CDAI_TYPE_EXT_INQ;
		cdai.bufsiz = sizeof(ext_inq);
		cdai.buf = (uint8_t *)&ext_inq;
		xpt_action((union ccb *)&cdai);

		if ((cdai.ccb_h.status & CAM_DEV_QFRZN) != 0)
			cam_release_devq(cdai.ccb_h.path, 0, 0, 0, FALSE);
		if ((cdai.ccb_h.status == CAM_REQ_CMP)
		 && (ext_inq.flags1 & SVPD_EID_SA_SPT_LBP))
			softc->flags |= SA_FLAG_PROTECT_SUPP;
	}

	xpt_path_inq(&cpi, periph->path);

	/*
	 * The SA driver supports a blocksize, but we don't know the
	 * blocksize until we media is inserted.  So, set a flag to
	 * indicate that the blocksize is unavailable right now.
	 */
	cam_periph_unlock(periph);
	softc->device_stats = devstat_new_entry("sa", periph->unit_number, 0,
	    DEVSTAT_BS_UNAVAILABLE, SID_TYPE(&cgd->inq_data) |
	    XPORT_DEVSTAT_TYPE(cpi.transport), DEVSTAT_PRIORITY_TAPE);

	/*
	 * Load the default value that is either compiled in, or loaded 
	 * in the global kern.cam.sa.allow_io_split tunable.
	 */
	softc->allow_io_split = sa_allow_io_split;

	/*
	 * Load a per-instance tunable, if it exists.  NOTE that this
	 * tunable WILL GO AWAY in FreeBSD 11.0.
	 */ 
	snprintf(tmpstr, sizeof(tmpstr), "kern.cam.sa.%u.allow_io_split",
		 periph->unit_number);
	TUNABLE_INT_FETCH(tmpstr, &softc->allow_io_split);

	/*
	 * If maxio isn't set, we fall back to DFLTPHYS.  Otherwise we take
	 * the smaller of cpi.maxio or MAXPHYS.
	 */
	if (cpi.maxio == 0)
		softc->maxio = DFLTPHYS;
	else if (cpi.maxio > MAXPHYS)
		softc->maxio = MAXPHYS;
	else
		softc->maxio = cpi.maxio;

	/*
	 * Record the controller's maximum I/O size so we can report it to
	 * the user later.
	 */
	softc->cpi_maxio = cpi.maxio;

	/*
	 * By default we tell physio that we do not want our I/O split.
	 * The user needs to have a 1:1 mapping between the size of his
	 * write to a tape character device and the size of the write
	 * that actually goes down to the drive.
	 */
	if (softc->allow_io_split == 0)
		softc->si_flags = SI_NOSPLIT;
	else
		softc->si_flags = 0;

	TASK_INIT(&softc->sysctl_task, 0, sasysctlinit, periph);

	/*
	 * If the SIM supports unmapped I/O, let physio know that we can
	 * handle unmapped buffers.
	 */
	if (cpi.hba_misc & PIM_UNMAPPED)
		softc->si_flags |= SI_UNMAPPED;

	/*
	 * Acquire a reference to the periph before we create the devfs
	 * instances for it.  We'll release this reference once the devfs
	 * instances have been freed.
	 */
	if (cam_periph_acquire(periph) != 0) {
		xpt_print(periph->path, "%s: lost periph during "
			  "registration!\n", __func__);
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}

	make_dev_args_init(&args);
	args.mda_devsw = &sa_cdevsw;
	args.mda_si_drv1 = softc->periph;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0660;

	args.mda_unit = SAMINOR(SA_CTLDEV, SA_ATYPE_R);
	error = make_dev_s(&args, &softc->devs.ctl_dev, "%s%d.ctl",
	    periph->periph_name, periph->unit_number);
	if (error != 0) {
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}
	sasetupdev(softc, softc->devs.ctl_dev);

	args.mda_unit = SAMINOR(SA_NOT_CTLDEV, SA_ATYPE_R);
	error = make_dev_s(&args, &softc->devs.r_dev, "%s%d",
	    periph->periph_name, periph->unit_number);
	if (error != 0) {
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}
	sasetupdev(softc, softc->devs.r_dev);

	args.mda_unit = SAMINOR(SA_NOT_CTLDEV, SA_ATYPE_NR);
	error = make_dev_s(&args, &softc->devs.nr_dev, "n%s%d",
	    periph->periph_name, periph->unit_number);
	if (error != 0) {
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}
	sasetupdev(softc, softc->devs.nr_dev);

	args.mda_unit = SAMINOR(SA_NOT_CTLDEV, SA_ATYPE_ER);
	error = make_dev_s(&args, &softc->devs.er_dev, "e%s%d",
	    periph->periph_name, periph->unit_number);
	if (error != 0) {
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}
	sasetupdev(softc, softc->devs.er_dev);

	cam_periph_lock(periph);

	softc->density_type_bits[0] = 0;
	softc->density_type_bits[1] = SRDS_MEDIA;
	softc->density_type_bits[2] = SRDS_MEDIUM_TYPE;
	softc->density_type_bits[3] = SRDS_MEDIUM_TYPE | SRDS_MEDIA;
	/*
	 * Bump the peripheral refcount for the sysctl thread, in case we
	 * get invalidated before the thread has a chance to run.
	 */
	cam_periph_acquire(periph);
	taskqueue_enqueue(taskqueue_thread, &softc->sysctl_task);

	/*
	 * Add an async callback so that we get
	 * notified if this device goes away.
	 */
	xpt_register_async(AC_LOST_DEVICE, saasync, periph, periph->path);

	xpt_announce_periph(periph, NULL);
	xpt_announce_quirks(periph, softc->quirks, SA_QUIRK_BIT_STRING);

	return (CAM_REQ_CMP);
}

static void
sastart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct sa_softc *softc;

	softc = (struct sa_softc *)periph->softc;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sastart\n"));

	
	switch (softc->state) {
	case SA_STATE_NORMAL:
	{
		/* Pull a buffer from the queue and get going on it */		
		struct bio *bp;

		/*
		 * See if there is a buf with work for us to do..
		 */
		bp = bioq_first(&softc->bio_queue);
		if (bp == NULL) {
			xpt_release_ccb(start_ccb);
		} else if (((softc->flags & SA_FLAG_ERR_PENDING) != 0)
			|| (softc->inject_eom != 0)) {
			struct bio *done_bp;

			if (softc->inject_eom != 0) {
				softc->flags |= SA_FLAG_EOM_PENDING;
				softc->inject_eom = 0;
				/*
				 * If we're injecting EOM for writes, we
				 * need to keep PEWS set for 3 queries
				 * to cover 2 position requests from the
				 * kernel via sagetpos(), and then allow
				 * for one for the user to see the BPEW
				 * flag (e.g. via mt status).  After that,
				 * it will be cleared.
				 */
				if (bp->bio_cmd == BIO_WRITE)
					softc->set_pews_status = 3;
				else
					softc->set_pews_status = 1;
			}
again:
			softc->queue_count--;
			bioq_remove(&softc->bio_queue, bp);
			bp->bio_resid = bp->bio_bcount;
			done_bp = bp;
			if ((softc->flags & SA_FLAG_EOM_PENDING) != 0) {
				/*
				 * We have two different behaviors for
				 * writes when we hit either Early Warning
				 * or the PEWZ (Programmable Early Warning
				 * Zone).  The default behavior is that
				 * for all writes that are currently
				 * queued after the write where we saw the
				 * early warning, we will return the write
				 * with the residual equal to the count.
				 * i.e. tell the application that 0 bytes
				 * were written.
				 * 
				 * The alternate behavior, which is enabled
				 * when eot_warn is set, is that in
				 * addition to setting the residual equal
				 * to the count, we will set the error
				 * to ENOSPC.
				 *
				 * In either case, once queued writes are
				 * cleared out, we clear the error flag
				 * (see below) and the application is free to
				 * attempt to write more.
				 */
				if (softc->eot_warn != 0) {
					bp->bio_flags |= BIO_ERROR;
					bp->bio_error = ENOSPC;
				} else
					bp->bio_error = 0;
			} else if ((softc->flags & SA_FLAG_EOF_PENDING) != 0) {
				/*
				 * This can only happen if we're reading
				 * in fixed length mode. In this case,
				 * we dump the rest of the list the
				 * same way.
				 */
				bp->bio_error = 0;
				if (bioq_first(&softc->bio_queue) != NULL) {
					biodone(done_bp);
					goto again;
				}
			} else if ((softc->flags & SA_FLAG_EIO_PENDING) != 0) {
				bp->bio_error = EIO;
				bp->bio_flags |= BIO_ERROR;
			}
			bp = bioq_first(&softc->bio_queue);
			/*
			 * Only if we have no other buffers queued up
			 * do we clear the pending error flag.
			 */
			if (bp == NULL)
				softc->flags &= ~SA_FLAG_ERR_PENDING;
			CAM_DEBUG(periph->path, CAM_DEBUG_INFO,
			    ("sastart- ERR_PENDING now 0x%x, bp is %sNULL, "
			    "%d more buffers queued up\n",
			    (softc->flags & SA_FLAG_ERR_PENDING),
			    (bp != NULL)? "not " : " ", softc->queue_count));
			xpt_release_ccb(start_ccb);
			biodone(done_bp);
		} else {
			u_int32_t length;

			bioq_remove(&softc->bio_queue, bp);
			softc->queue_count--;

			length = bp->bio_bcount;

			if ((softc->flags & SA_FLAG_FIXED) != 0) {
				if (softc->blk_shift != 0) {
					length = length >> softc->blk_shift;
				} else if (softc->media_blksize != 0) {
					length = length / softc->media_blksize;
				} else {
					bp->bio_error = EIO;
					xpt_print(periph->path, "zero blocksize"
					    " for FIXED length writes?\n");
					biodone(bp);
					break;
				}
#if	0
				CAM_DEBUG(start_ccb->ccb_h.path, CAM_DEBUG_INFO,
				    ("issuing a %d fixed record %s\n",
				    length,  (bp->bio_cmd == BIO_READ)? "read" :
				    "write"));
#endif
			} else {
#if	0
				CAM_DEBUG(start_ccb->ccb_h.path, CAM_DEBUG_INFO,
				    ("issuing a %d variable byte %s\n",
				    length,  (bp->bio_cmd == BIO_READ)? "read" :
				    "write"));
#endif
			}
			devstat_start_transaction_bio(softc->device_stats, bp);
			/*
			 * Some people have theorized that we should
			 * suppress illegal length indication if we are
			 * running in variable block mode so that we don't
			 * have to request sense every time our requested
			 * block size is larger than the written block.
			 * The residual information from the ccb allows
			 * us to identify this situation anyway.  The only
			 * problem with this is that we will not get
			 * information about blocks that are larger than
			 * our read buffer unless we set the block size
			 * in the mode page to something other than 0.
			 *
			 * I believe that this is a non-issue. If user apps
			 * don't adjust their read size to match our record
			 * size, that's just life. Anyway, the typical usage
			 * would be to issue, e.g., 64KB reads and occasionally
			 * have to do deal with 512 byte or 1KB intermediate
			 * records.
			 *
			 * That said, though, we now support setting the
			 * SILI bit on reads, and we set the blocksize to 4
			 * bytes when we do that.  This gives us
			 * compatibility with software that wants this,
			 * although the only real difference between that
			 * and not setting the SILI bit on reads is that we
			 * won't get a check condition on reads where our
			 * request size is larger than the block on tape.
			 * That probably only makes a real difference in
			 * non-packetized SCSI, where you have to go back
			 * to the drive to request sense and thus incur
			 * more latency.
			 */
			softc->dsreg = (bp->bio_cmd == BIO_READ)?
			    MTIO_DSREG_RD : MTIO_DSREG_WR;
			scsi_sa_read_write(&start_ccb->csio, 0, sadone,
			    MSG_SIMPLE_Q_TAG, (bp->bio_cmd == BIO_READ ? 
			    SCSI_RW_READ : SCSI_RW_WRITE) |
			    ((bp->bio_flags & BIO_UNMAPPED) != 0 ?
			    SCSI_RW_BIO : 0), softc->sili,
			    (softc->flags & SA_FLAG_FIXED) != 0, length,
			    (bp->bio_flags & BIO_UNMAPPED) != 0 ? (void *)bp :
			    bp->bio_data, bp->bio_bcount, SSD_FULL_SIZE,
			    IO_TIMEOUT);
			start_ccb->ccb_h.ccb_pflags &= ~SA_POSITION_UPDATED;
			start_ccb->ccb_h.ccb_bp = bp;
			bp = bioq_first(&softc->bio_queue);
			xpt_action(start_ccb);
		}
		
		if (bp != NULL) {
			/* Have more work to do, so ensure we stay scheduled */
			xpt_schedule(periph, CAM_PRIORITY_NORMAL);
		}
		break;
	}
	case SA_STATE_ABNORMAL:
	default:
		panic("state 0x%x in sastart", softc->state);
		break;
	}
}


static void
sadone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct sa_softc *softc;
	struct ccb_scsiio *csio;
	struct bio *bp;
	int error;

	softc = (struct sa_softc *)periph->softc;
	csio = &done_ccb->csio;

	softc->dsreg = MTIO_DSREG_REST;
	bp = (struct bio *)done_ccb->ccb_h.ccb_bp;
	error = 0;
	if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if ((error = saerror(done_ccb, 0, 0)) == ERESTART) {
			/*
			 * A retry was scheduled, so just return.
			 */
			return;
		}
	}

	if (error == EIO) {

		/*
		 * Catastrophic error. Mark the tape as frozen
		 * (we no longer know tape position).
		 *
		 * Return all queued I/O with EIO, and unfreeze
		 * our queue so that future transactions that
		 * attempt to fix this problem can get to the
		 * device.
		 *
		 */

		softc->flags |= SA_FLAG_TAPE_FROZEN;
		bioq_flush(&softc->bio_queue, NULL, EIO);
	}
	if (error != 0) {
		bp->bio_resid = bp->bio_bcount;
		bp->bio_error = error;
		bp->bio_flags |= BIO_ERROR;
		/*
		 * In the error case, position is updated in saerror.
		 */
	} else {
		bp->bio_resid = csio->resid;
		bp->bio_error = 0;
		if (csio->resid != 0) {
			bp->bio_flags |= BIO_ERROR;
		}
		if (bp->bio_cmd == BIO_WRITE) {
			softc->flags |= SA_FLAG_TAPE_WRITTEN;
			softc->filemarks = 0;
		}
		if (!(csio->ccb_h.ccb_pflags & SA_POSITION_UPDATED) &&
		    (softc->blkno != (daddr_t) -1)) {
			if ((softc->flags & SA_FLAG_FIXED) != 0) {
				u_int32_t l;
				if (softc->blk_shift != 0) {
					l = bp->bio_bcount >>
						softc->blk_shift;
				} else {
					l = bp->bio_bcount /
						softc->media_blksize;
				}
				softc->blkno += (daddr_t) l;
			} else {
				softc->blkno++;
			}
		}
	}
	/*
	 * If we had an error (immediate or pending),
	 * release the device queue now.
	 */
	if (error || (softc->flags & SA_FLAG_ERR_PENDING))
		cam_release_devq(done_ccb->ccb_h.path, 0, 0, 0, 0);
	if (error || bp->bio_resid) {
		CAM_DEBUG(periph->path, CAM_DEBUG_INFO,
		    	  ("error %d resid %ld count %ld\n", error,
			  bp->bio_resid, bp->bio_bcount));
	}
	biofinish(bp, softc->device_stats, 0);
	xpt_release_ccb(done_ccb);
}

/*
 * Mount the tape (make sure it's ready for I/O).
 */
static int
samount(struct cam_periph *periph, int oflags, struct cdev *dev)
{
	struct	sa_softc *softc;
	union	ccb *ccb;
	int	error;

	/*
	 * oflags can be checked for 'kind' of open (read-only check) - later
	 * dev can be checked for a control-mode or compression open - later
	 */
	UNUSED_PARAMETER(oflags);
	UNUSED_PARAMETER(dev);


	softc = (struct sa_softc *)periph->softc;

	/*
	 * This should determine if something has happened since the last
	 * open/mount that would invalidate the mount. We do *not* want
	 * to retry this command- we just want the status. But we only
	 * do this if we're mounted already- if we're not mounted,
	 * we don't care about the unit read state and can instead use
	 * this opportunity to attempt to reserve the tape unit.
	 */
	
	if (softc->flags & SA_FLAG_TAPE_MOUNTED) {
		ccb = cam_periph_getccb(periph, 1);
		scsi_test_unit_ready(&ccb->csio, 0, NULL,
		    MSG_SIMPLE_Q_TAG, SSD_FULL_SIZE, IO_TIMEOUT);
		error = cam_periph_runccb(ccb, saerror, 0, SF_NO_PRINT,
		    softc->device_stats);
		if (error == ENXIO) {
			softc->flags &= ~SA_FLAG_TAPE_MOUNTED;
			scsi_test_unit_ready(&ccb->csio, 0, NULL,
			    MSG_SIMPLE_Q_TAG, SSD_FULL_SIZE, IO_TIMEOUT);
			error = cam_periph_runccb(ccb, saerror, 0, SF_NO_PRINT,
			    softc->device_stats);
		} else if (error) {
			/*
			 * We don't need to freeze the tape because we
			 * will now attempt to rewind/load it.
			 */
			softc->flags &= ~SA_FLAG_TAPE_MOUNTED;
			if (CAM_DEBUGGED(periph->path, CAM_DEBUG_INFO)) {
				xpt_print(periph->path,
				    "error %d on TUR in samount\n", error);
			}
		}
	} else {
		error = sareservereleaseunit(periph, TRUE);
		if (error) {
			return (error);
		}
		ccb = cam_periph_getccb(periph, 1);
		scsi_test_unit_ready(&ccb->csio, 0, NULL,
		    MSG_SIMPLE_Q_TAG, SSD_FULL_SIZE, IO_TIMEOUT);
		error = cam_periph_runccb(ccb, saerror, 0, SF_NO_PRINT,
		    softc->device_stats);
	}

	if ((softc->flags & SA_FLAG_TAPE_MOUNTED) == 0) {
		struct scsi_read_block_limits_data *rblim = NULL;
		int comp_enabled, comp_supported;
		u_int8_t write_protect, guessing = 0;

		/*
		 * Clear out old state.
		 */
		softc->flags &= ~(SA_FLAG_TAPE_WP|SA_FLAG_TAPE_WRITTEN|
				  SA_FLAG_ERR_PENDING|SA_FLAG_COMPRESSION);
		softc->filemarks = 0;

		/*
		 * *Very* first off, make sure we're loaded to BOT.
		 */
		scsi_load_unload(&ccb->csio, 2, NULL, MSG_SIMPLE_Q_TAG, FALSE,
		    FALSE, FALSE, 1, SSD_FULL_SIZE, REWIND_TIMEOUT);
		error = cam_periph_runccb(ccb, saerror, 0, SF_NO_PRINT,
		    softc->device_stats);

		/*
		 * In case this doesn't work, do a REWIND instead
		 */
		if (error) {
			scsi_rewind(&ccb->csio, 2, NULL, MSG_SIMPLE_Q_TAG,
			    FALSE, SSD_FULL_SIZE, REWIND_TIMEOUT);
			error = cam_periph_runccb(ccb, saerror, 0, SF_NO_PRINT,
				softc->device_stats);
		}
		if (error) {
			xpt_release_ccb(ccb);
			goto exit;
		}

		/*
		 * Do a dummy test read to force access to the
		 * media so that the drive will really know what's
		 * there. We actually don't really care what the
		 * blocksize on tape is and don't expect to really
		 * read a full record.
		 */
		rblim = (struct  scsi_read_block_limits_data *)
		    malloc(8192, M_SCSISA, M_NOWAIT);
		if (rblim == NULL) {
			xpt_print(periph->path, "no memory for test read\n");
			xpt_release_ccb(ccb);
			error = ENOMEM;
			goto exit;
		}

		if ((softc->quirks & SA_QUIRK_NODREAD) == 0) {
			scsi_sa_read_write(&ccb->csio, 0, NULL,
			    MSG_SIMPLE_Q_TAG, 1, FALSE, 0, 8192,
			    (void *) rblim, 8192, SSD_FULL_SIZE,
			    IO_TIMEOUT);
			(void) cam_periph_runccb(ccb, saerror, 0, SF_NO_PRINT,
			    softc->device_stats);
			scsi_rewind(&ccb->csio, 1, NULL, MSG_SIMPLE_Q_TAG,
			    FALSE, SSD_FULL_SIZE, REWIND_TIMEOUT);
			error = cam_periph_runccb(ccb, saerror, CAM_RETRY_SELTO,
			    SF_NO_PRINT | SF_RETRY_UA,
			    softc->device_stats);
			if (error) {
				xpt_print(periph->path,
				    "unable to rewind after test read\n");
				xpt_release_ccb(ccb);
				goto exit;
			}
		}

		/*
		 * Next off, determine block limits.
		 */
		scsi_read_block_limits(&ccb->csio, 5, NULL, MSG_SIMPLE_Q_TAG,
		    rblim, SSD_FULL_SIZE, SCSIOP_TIMEOUT);

		error = cam_periph_runccb(ccb, saerror, CAM_RETRY_SELTO,
		    SF_NO_PRINT | SF_RETRY_UA, softc->device_stats);

		xpt_release_ccb(ccb);

		if (error != 0) {
			/*
			 * If it's less than SCSI-2, READ BLOCK LIMITS is not
			 * a MANDATORY command. Anyway- it doesn't matter-
			 * we can proceed anyway.
			 */
			softc->blk_gran = 0;
			softc->max_blk = ~0;
			softc->min_blk = 0;
		} else {
			if (softc->scsi_rev >= SCSI_REV_SPC) {
				softc->blk_gran = RBL_GRAN(rblim);
			} else {
				softc->blk_gran = 0;
			}
			/*
			 * We take max_blk == min_blk to mean a default to
			 * fixed mode- but note that whatever we get out of
			 * sagetparams below will actually determine whether
			 * we are actually *in* fixed mode.
			 */
			softc->max_blk = scsi_3btoul(rblim->maximum);
			softc->min_blk = scsi_2btoul(rblim->minimum);


		}
		/*
		 * Next, perform a mode sense to determine
		 * current density, blocksize, compression etc.
		 */
		error = sagetparams(periph, SA_PARAM_ALL,
				    &softc->media_blksize,
				    &softc->media_density,
				    &softc->media_numblks,
				    &softc->buffer_mode, &write_protect,
				    &softc->speed, &comp_supported,
				    &comp_enabled, &softc->comp_algorithm,
				    NULL, NULL, 0, 0);

		if (error != 0) {
			/*
			 * We could work a little harder here. We could
			 * adjust our attempts to get information. It
			 * might be an ancient tape drive. If someone
			 * nudges us, we'll do that.
			 */
			goto exit;
		}

		/*
		 * If no quirk has determined that this is a device that is
		 * preferred to be in fixed or variable mode, now is the time
		 * to find out.
	 	 */
		if ((softc->quirks & (SA_QUIRK_FIXED|SA_QUIRK_VARIABLE)) == 0) {
			guessing = 1;
			/*
			 * This could be expensive to find out. Luckily we
			 * only need to do this once. If we start out in
			 * 'default' mode, try and set ourselves to one
			 * of the densities that would determine a wad
			 * of other stuff. Go from highest to lowest.
			 */
			if (softc->media_density == SCSI_DEFAULT_DENSITY) {
				int i;
				static u_int8_t ctry[] = {
					SCSI_DENSITY_HALFINCH_PE,
					SCSI_DENSITY_HALFINCH_6250C,
					SCSI_DENSITY_HALFINCH_6250,
					SCSI_DENSITY_HALFINCH_1600,
					SCSI_DENSITY_HALFINCH_800,
					SCSI_DENSITY_QIC_4GB,
					SCSI_DENSITY_QIC_2GB,
					SCSI_DENSITY_QIC_525_320,
					SCSI_DENSITY_QIC_150,
					SCSI_DENSITY_QIC_120,
					SCSI_DENSITY_QIC_24,
					SCSI_DENSITY_QIC_11_9TRK,
					SCSI_DENSITY_QIC_11_4TRK,
					SCSI_DENSITY_QIC_1320,
					SCSI_DENSITY_QIC_3080,
					0
				};
				for (i = 0; ctry[i]; i++) {
					error = sasetparams(periph,
					    SA_PARAM_DENSITY, 0, ctry[i],
					    0, SF_NO_PRINT);
					if (error == 0) {
						softc->media_density = ctry[i];
						break;
					}
				}
			}
			switch (softc->media_density) {
			case SCSI_DENSITY_QIC_11_4TRK:
			case SCSI_DENSITY_QIC_11_9TRK:
			case SCSI_DENSITY_QIC_24:
			case SCSI_DENSITY_QIC_120:
			case SCSI_DENSITY_QIC_150:
			case SCSI_DENSITY_QIC_525_320:
			case SCSI_DENSITY_QIC_1320:
			case SCSI_DENSITY_QIC_3080:
				softc->quirks &= ~SA_QUIRK_2FM;
				softc->quirks |= SA_QUIRK_FIXED|SA_QUIRK_1FM;
				softc->last_media_blksize = 512;
				break;
			case SCSI_DENSITY_QIC_4GB:
			case SCSI_DENSITY_QIC_2GB:
				softc->quirks &= ~SA_QUIRK_2FM;
				softc->quirks |= SA_QUIRK_FIXED|SA_QUIRK_1FM;
				softc->last_media_blksize = 1024;
				break;
			default:
				softc->last_media_blksize =
				    softc->media_blksize;
				softc->quirks |= SA_QUIRK_VARIABLE;
				break;
			}
		}

		/*
		 * If no quirk has determined that this is a device that needs
		 * to have 2 Filemarks at EOD, now is the time to find out.
		 */

		if ((softc->quirks & SA_QUIRK_2FM) == 0) {
			switch (softc->media_density) {
			case SCSI_DENSITY_HALFINCH_800:
			case SCSI_DENSITY_HALFINCH_1600:
			case SCSI_DENSITY_HALFINCH_6250:
			case SCSI_DENSITY_HALFINCH_6250C:
			case SCSI_DENSITY_HALFINCH_PE:
				softc->quirks &= ~SA_QUIRK_1FM;
				softc->quirks |= SA_QUIRK_2FM;
				break;
			default:
				break;
			}
		}

		/*
		 * Now validate that some info we got makes sense.
		 */
		if ((softc->max_blk < softc->media_blksize) ||
		    (softc->min_blk > softc->media_blksize &&
		    softc->media_blksize)) {
			xpt_print(periph->path,
			    "BLOCK LIMITS (%d..%d) could not match current "
			    "block settings (%d)- adjusting\n", softc->min_blk,
			    softc->max_blk, softc->media_blksize);
			softc->max_blk = softc->min_blk =
			    softc->media_blksize;
		}

		/*
		 * Now put ourselves into the right frame of mind based
		 * upon quirks...
		 */
tryagain:
		/*
		 * If we want to be in FIXED mode and our current blocksize
		 * is not equal to our last blocksize (if nonzero), try and
		 * set ourselves to this last blocksize (as the 'preferred'
		 * block size).  The initial quirkmatch at registry sets the
		 * initial 'last' blocksize. If, for whatever reason, this
		 * 'last' blocksize is zero, set the blocksize to 512,
		 * or min_blk if that's larger.
		 */
		if ((softc->quirks & SA_QUIRK_FIXED) &&
		    (softc->quirks & SA_QUIRK_NO_MODESEL) == 0 &&
		    (softc->media_blksize != softc->last_media_blksize)) {
			softc->media_blksize = softc->last_media_blksize;
			if (softc->media_blksize == 0) {
				softc->media_blksize = 512;
				if (softc->media_blksize < softc->min_blk) {
					softc->media_blksize = softc->min_blk;
				}
			}
			error = sasetparams(periph, SA_PARAM_BLOCKSIZE,
			    softc->media_blksize, 0, 0, SF_NO_PRINT);
			if (error) {
				xpt_print(periph->path,
				    "unable to set fixed blocksize to %d\n",
				    softc->media_blksize);
				goto exit;
			}
		}

		if ((softc->quirks & SA_QUIRK_VARIABLE) && 
		    (softc->media_blksize != 0)) {
			softc->last_media_blksize = softc->media_blksize;
			softc->media_blksize = 0;
			error = sasetparams(periph, SA_PARAM_BLOCKSIZE,
			    0, 0, 0, SF_NO_PRINT);
			if (error) {
				/*
				 * If this fails and we were guessing, just
				 * assume that we got it wrong and go try
				 * fixed block mode. Don't even check against
				 * density code at this point.
				 */
				if (guessing) {
					softc->quirks &= ~SA_QUIRK_VARIABLE;
					softc->quirks |= SA_QUIRK_FIXED;
					if (softc->last_media_blksize == 0)
						softc->last_media_blksize = 512;
					goto tryagain;
				}
				xpt_print(periph->path,
				    "unable to set variable blocksize\n");
				goto exit;
			}
		}

		/*
		 * Now that we have the current block size,
		 * set up some parameters for sastart's usage.
		 */
		if (softc->media_blksize) {
			softc->flags |= SA_FLAG_FIXED;
			if (powerof2(softc->media_blksize)) {
				softc->blk_shift =
				    ffs(softc->media_blksize) - 1;
				softc->blk_mask = softc->media_blksize - 1;
			} else {
				softc->blk_mask = ~0;
				softc->blk_shift = 0;
			}
		} else {
			/*
			 * The SCSI-3 spec allows 0 to mean "unspecified".
			 * The SCSI-1 spec allows 0 to mean 'infinite'.
			 *
			 * Either works here.
			 */
			if (softc->max_blk == 0) {
				softc->max_blk = ~0;
			}
			softc->blk_shift = 0;
			if (softc->blk_gran != 0) {
				softc->blk_mask = softc->blk_gran - 1;
			} else {
				softc->blk_mask = 0;
			}
		}

		if (write_protect) 
			softc->flags |= SA_FLAG_TAPE_WP;

		if (comp_supported) {
			if (softc->saved_comp_algorithm == 0)
				softc->saved_comp_algorithm =
				    softc->comp_algorithm;
			softc->flags |= SA_FLAG_COMP_SUPP;
			if (comp_enabled)
				softc->flags |= SA_FLAG_COMP_ENABLED;
		} else
			softc->flags |= SA_FLAG_COMP_UNSUPP;

		if ((softc->buffer_mode == SMH_SA_BUF_MODE_NOBUF) &&
		    (softc->quirks & SA_QUIRK_NO_MODESEL) == 0) {
			error = sasetparams(periph, SA_PARAM_BUFF_MODE, 0,
			    0, 0, SF_NO_PRINT);
			if (error == 0) {
				softc->buffer_mode = SMH_SA_BUF_MODE_SIBUF;
			} else {
				xpt_print(periph->path,
				    "unable to set buffered mode\n");
			}
			error = 0;	/* not an error */
		}


		if (error == 0) {
			softc->flags |= SA_FLAG_TAPE_MOUNTED;
		}
exit:
		if (rblim != NULL)
			free(rblim, M_SCSISA);

		if (error != 0) {
			softc->dsreg = MTIO_DSREG_NIL;
		} else {
			softc->fileno = softc->blkno = 0;
			softc->rep_fileno = softc->rep_blkno = -1;
			softc->partition = 0;
			softc->dsreg = MTIO_DSREG_REST;
		}
#ifdef	SA_1FM_AT_EOD
		if ((softc->quirks & SA_QUIRK_2FM) == 0)
			softc->quirks |= SA_QUIRK_1FM;
#else
		if ((softc->quirks & SA_QUIRK_1FM) == 0)
			softc->quirks |= SA_QUIRK_2FM;
#endif
	} else
		xpt_release_ccb(ccb);

	/*
	 * If we return an error, we're not mounted any more,
	 * so release any device reservation.
	 */
	if (error != 0) {
		(void) sareservereleaseunit(periph, FALSE);
	} else {
		/*
		 * Clear I/O residual.
		 */
		softc->last_io_resid = 0;
		softc->last_ctl_resid = 0;
	}
	return (error);
}

/*
 * How many filemarks do we need to write if we were to terminate the
 * tape session right now? Note that this can be a negative number
 */

static int
samarkswanted(struct cam_periph *periph)
{
	int	markswanted;
	struct	sa_softc *softc;

	softc = (struct sa_softc *)periph->softc;
	markswanted = 0;
	if ((softc->flags & SA_FLAG_TAPE_WRITTEN) != 0) {
		markswanted++;
		if (softc->quirks & SA_QUIRK_2FM)
			markswanted++;
	}
	markswanted -= softc->filemarks;
	return (markswanted);
}

static int
sacheckeod(struct cam_periph *periph)
{
	int	error;
	int	markswanted;

	markswanted = samarkswanted(periph);

	if (markswanted > 0) {
		error = sawritefilemarks(periph, markswanted, FALSE, FALSE);
	} else {
		error = 0;
	}
	return (error);
}

static int
saerror(union ccb *ccb, u_int32_t cflgs, u_int32_t sflgs)
{
	static const char *toobig =
	    "%d-byte tape record bigger than supplied buffer\n";
	struct	cam_periph *periph;
	struct	sa_softc *softc;
	struct	ccb_scsiio *csio;
	struct	scsi_sense_data *sense;
	uint64_t resid = 0;
	int64_t	info = 0;
	cam_status status;
	int error_code, sense_key, asc, ascq, error, aqvalid, stream_valid;
	int sense_len;
	uint8_t stream_bits;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct sa_softc *)periph->softc;
	csio = &ccb->csio;
	sense = &csio->sense_data;
	sense_len = csio->sense_len - csio->sense_resid;
	scsi_extract_sense_len(sense, sense_len, &error_code, &sense_key,
	    &asc, &ascq, /*show_errors*/ 1);
	if (asc != -1 && ascq != -1)
		aqvalid = 1;
	else
		aqvalid = 0;
	if (scsi_get_stream_info(sense, sense_len, NULL, &stream_bits) == 0)
		stream_valid = 1;
	else
		stream_valid = 0;
	error = 0;

	status = csio->ccb_h.status & CAM_STATUS_MASK;

	/*
	 * Calculate/latch up, any residuals... We do this in a funny 2-step
	 * so we can print stuff here if we have CAM_DEBUG enabled for this
	 * unit.
	 */
	if (status == CAM_SCSI_STATUS_ERROR) {
		if (scsi_get_sense_info(sense, sense_len, SSD_DESC_INFO, &resid,
					&info) == 0) {
			if ((softc->flags & SA_FLAG_FIXED) != 0)
				resid *= softc->media_blksize;
		} else {
			resid = csio->dxfer_len;
			info = resid;
			if ((softc->flags & SA_FLAG_FIXED) != 0) {
				if (softc->media_blksize)
					info /= softc->media_blksize;
			}
		}
		if (csio->cdb_io.cdb_bytes[0] == SA_READ ||
		    csio->cdb_io.cdb_bytes[0] == SA_WRITE) {
			bcopy((caddr_t) sense, (caddr_t) &softc->last_io_sense,
			    sizeof (struct scsi_sense_data));
			bcopy(csio->cdb_io.cdb_bytes, softc->last_io_cdb,
			    (int) csio->cdb_len);
			softc->last_io_resid = resid;
			softc->last_resid_was_io = 1;
		} else {
			bcopy((caddr_t) sense, (caddr_t) &softc->last_ctl_sense,
			    sizeof (struct scsi_sense_data));
			bcopy(csio->cdb_io.cdb_bytes, softc->last_ctl_cdb,
			    (int) csio->cdb_len);
			softc->last_ctl_resid = resid;
			softc->last_resid_was_io = 0;
		}
		CAM_DEBUG(periph->path, CAM_DEBUG_INFO, ("CDB[0]=0x%x Key 0x%x "
		    "ASC/ASCQ 0x%x/0x%x CAM STATUS 0x%x flags 0x%x resid %jd "
		    "dxfer_len %d\n", csio->cdb_io.cdb_bytes[0] & 0xff,
		    sense_key, asc, ascq, status,
		    (stream_valid) ? stream_bits : 0, (intmax_t)resid,
		    csio->dxfer_len));
	} else {
		CAM_DEBUG(periph->path, CAM_DEBUG_INFO,
		    ("Cam Status 0x%x\n", status));
	}

	switch (status) {
	case CAM_REQ_CMP:
		return (0);
	case CAM_SCSI_STATUS_ERROR:
		/*
		 * If a read/write command, we handle it here.
		 */
		if (csio->cdb_io.cdb_bytes[0] == SA_READ ||
		    csio->cdb_io.cdb_bytes[0] == SA_WRITE) {
			break;
		}
		/*
		 * If this was just EOM/EOP, Filemark, Setmark, ILI or
		 * PEW detected on a non read/write command, we assume
		 * it's not an error and propagate the residual and return.
		 */
		if ((aqvalid && asc == 0 && ((ascq > 0 && ascq <= 5)
		  || (ascq == 0x07)))
		 || (aqvalid == 0 && sense_key == SSD_KEY_NO_SENSE)) {
			csio->resid = resid;
			QFRLS(ccb);
			return (0);
		}
		/*
		 * Otherwise, we let the common code handle this.
		 */
		return (cam_periph_error(ccb, cflgs, sflgs));

	/*
	 * XXX: To Be Fixed
	 * We cannot depend upon CAM honoring retry counts for these.
	 */
	case CAM_SCSI_BUS_RESET:
	case CAM_BDR_SENT:
		if (ccb->ccb_h.retry_count <= 0) {
			return (EIO);
		}
		/* FALLTHROUGH */
	default:
		return (cam_periph_error(ccb, cflgs, sflgs));
	}

	/*
	 * Handle filemark, end of tape, mismatched record sizes....
	 * From this point out, we're only handling read/write cases.
	 * Handle writes && reads differently.
	 */

	if (csio->cdb_io.cdb_bytes[0] == SA_WRITE) {
		if (sense_key == SSD_KEY_VOLUME_OVERFLOW) {
			csio->resid = resid;
			error = ENOSPC;
		} else if ((stream_valid != 0) && (stream_bits & SSD_EOM)) {
			softc->flags |= SA_FLAG_EOM_PENDING;
			/*
			 * Grotesque as it seems, the few times
			 * I've actually seen a non-zero resid,
			 * the tape drive actually lied and had
			 * written all the data!.
			 */
			csio->resid = 0;
		}
	} else {
		csio->resid = resid;
		if (sense_key == SSD_KEY_BLANK_CHECK) {
			if (softc->quirks & SA_QUIRK_1FM) {
				error = 0;
				softc->flags |= SA_FLAG_EOM_PENDING;
			} else {
				error = EIO;
			}
		} else if ((stream_valid != 0) && (stream_bits & SSD_FILEMARK)){
			if (softc->flags & SA_FLAG_FIXED) {
				error = -1;
				softc->flags |= SA_FLAG_EOF_PENDING;
			}
			/*
			 * Unconditionally, if we detected a filemark on a read,
			 * mark that we've run moved a file ahead.
			 */
			if (softc->fileno != (daddr_t) -1) {
				softc->fileno++;
				softc->blkno = 0;
				csio->ccb_h.ccb_pflags |= SA_POSITION_UPDATED;
			}
		}
	}

	/*
	 * Incorrect Length usually applies to read, but can apply to writes.
	 */
	if (error == 0 && (stream_valid != 0) && (stream_bits & SSD_ILI)) {
		if (info < 0) {
			xpt_print(csio->ccb_h.path, toobig,
			    csio->dxfer_len - info);
			csio->resid = csio->dxfer_len;
			error = EIO;
		} else {
			csio->resid = resid;
			if (softc->flags & SA_FLAG_FIXED) {
				softc->flags |= SA_FLAG_EIO_PENDING;
			}
			/*
			 * Bump the block number if we hadn't seen a filemark.
			 * Do this independent of errors (we've moved anyway).
			 */
			if ((stream_valid == 0) ||
			    (stream_bits & SSD_FILEMARK) == 0) {
				if (softc->blkno != (daddr_t) -1) {
					softc->blkno++;
					csio->ccb_h.ccb_pflags |=
					   SA_POSITION_UPDATED;
				}
			}
		}
	}

	if (error <= 0) {
		/*
		 * Unfreeze the queue if frozen as we're not returning anything
		 * to our waiters that would indicate an I/O error has occurred
		 * (yet).
		 */
		QFRLS(ccb);
		error = 0;
	}
	return (error);
}

static int
sagetparams(struct cam_periph *periph, sa_params params_to_get,
	    u_int32_t *blocksize, u_int8_t *density, u_int32_t *numblocks,
	    int *buff_mode, u_int8_t *write_protect, u_int8_t *speed,
	    int *comp_supported, int *comp_enabled, u_int32_t *comp_algorithm,
	    sa_comp_t *tcs, struct scsi_control_data_prot_subpage *prot_page,
	    int dp_size, int prot_changeable)
{
	union ccb *ccb;
	void *mode_buffer;
	struct scsi_mode_header_6 *mode_hdr;
	struct scsi_mode_blk_desc *mode_blk;
	int mode_buffer_len;
	struct sa_softc *softc;
	u_int8_t cpage;
	int error;
	cam_status status;

	softc = (struct sa_softc *)periph->softc;
	ccb = cam_periph_getccb(periph, 1);
	if (softc->quirks & SA_QUIRK_NO_CPAGE)
		cpage = SA_DEVICE_CONFIGURATION_PAGE;
	else
		cpage = SA_DATA_COMPRESSION_PAGE;

retry:
	mode_buffer_len = sizeof(*mode_hdr) + sizeof(*mode_blk);

	if (params_to_get & SA_PARAM_COMPRESSION) {
		if (softc->quirks & SA_QUIRK_NOCOMP) {
			*comp_supported = FALSE;
			params_to_get &= ~SA_PARAM_COMPRESSION;
		} else
			mode_buffer_len += sizeof (sa_comp_t);
	}

	/* XXX Fix M_NOWAIT */
	mode_buffer = malloc(mode_buffer_len, M_SCSISA, M_NOWAIT | M_ZERO);
	if (mode_buffer == NULL) {
		xpt_release_ccb(ccb);
		return (ENOMEM);
	}
	mode_hdr = (struct scsi_mode_header_6 *)mode_buffer;
	mode_blk = (struct scsi_mode_blk_desc *)&mode_hdr[1];

	/* it is safe to retry this */
	scsi_mode_sense(&ccb->csio, 5, NULL, MSG_SIMPLE_Q_TAG, FALSE,
	    SMS_PAGE_CTRL_CURRENT, (params_to_get & SA_PARAM_COMPRESSION) ?
	    cpage : SMS_VENDOR_SPECIFIC_PAGE, mode_buffer, mode_buffer_len,
	    SSD_FULL_SIZE, SCSIOP_TIMEOUT);

	error = cam_periph_runccb(ccb, saerror, 0, SF_NO_PRINT,
	    softc->device_stats);

	status = ccb->ccb_h.status & CAM_STATUS_MASK;

	if (error == EINVAL && (params_to_get & SA_PARAM_COMPRESSION) != 0) {
		/*
		 * Hmm. Let's see if we can try another page...
		 * If we've already done that, give up on compression
		 * for this device and remember this for the future
		 * and attempt the request without asking for compression
		 * info.
		 */
		if (cpage == SA_DATA_COMPRESSION_PAGE) {
			cpage = SA_DEVICE_CONFIGURATION_PAGE;
			goto retry;
		}
		softc->quirks |= SA_QUIRK_NOCOMP;
		free(mode_buffer, M_SCSISA);
		goto retry;
	} else if (status == CAM_SCSI_STATUS_ERROR) {
		/* Tell the user about the fatal error. */
		scsi_sense_print(&ccb->csio);
		goto sagetparamsexit;
	}

	/*
	 * If the user only wants the compression information, and
	 * the device doesn't send back the block descriptor, it's
	 * no big deal.  If the user wants more than just
	 * compression, though, and the device doesn't pass back the
	 * block descriptor, we need to send another mode sense to
	 * get the block descriptor.
	 */
	if ((mode_hdr->blk_desc_len == 0) &&
	    (params_to_get & SA_PARAM_COMPRESSION) &&
	    (params_to_get & ~(SA_PARAM_COMPRESSION))) {

		/*
		 * Decrease the mode buffer length by the size of
		 * the compression page, to make sure the data
		 * there doesn't get overwritten.
		 */
		mode_buffer_len -= sizeof (sa_comp_t);

		/*
		 * Now move the compression page that we presumably
		 * got back down the memory chunk a little bit so
		 * it doesn't get spammed.
		 */
		bcopy(&mode_hdr[0], &mode_hdr[1], sizeof (sa_comp_t));
		bzero(&mode_hdr[0], sizeof (mode_hdr[0]));

		/*
		 * Now, we issue another mode sense and just ask
		 * for the block descriptor, etc.
		 */

		scsi_mode_sense(&ccb->csio, 2, NULL, MSG_SIMPLE_Q_TAG, FALSE,
		    SMS_PAGE_CTRL_CURRENT, SMS_VENDOR_SPECIFIC_PAGE,
		    mode_buffer, mode_buffer_len, SSD_FULL_SIZE,
		    SCSIOP_TIMEOUT);

		error = cam_periph_runccb(ccb, saerror, 0, SF_NO_PRINT,
		    softc->device_stats);

		if (error != 0)
			goto sagetparamsexit;
	}

	if (params_to_get & SA_PARAM_BLOCKSIZE)
		*blocksize = scsi_3btoul(mode_blk->blklen);

	if (params_to_get & SA_PARAM_NUMBLOCKS)
		*numblocks = scsi_3btoul(mode_blk->nblocks);

	if (params_to_get & SA_PARAM_BUFF_MODE)
		*buff_mode = mode_hdr->dev_spec & SMH_SA_BUF_MODE_MASK;

	if (params_to_get & SA_PARAM_DENSITY)
		*density = mode_blk->density;

	if (params_to_get & SA_PARAM_WP)
		*write_protect = (mode_hdr->dev_spec & SMH_SA_WP)? TRUE : FALSE;

	if (params_to_get & SA_PARAM_SPEED)
		*speed = mode_hdr->dev_spec & SMH_SA_SPEED_MASK;

	if (params_to_get & SA_PARAM_COMPRESSION) {
		sa_comp_t *ntcs = (sa_comp_t *) &mode_blk[1];
		if (cpage == SA_DATA_COMPRESSION_PAGE) {
			struct scsi_data_compression_page *cp = &ntcs->dcomp;
			*comp_supported =
			    (cp->dce_and_dcc & SA_DCP_DCC)? TRUE : FALSE;
			*comp_enabled =
			    (cp->dce_and_dcc & SA_DCP_DCE)? TRUE : FALSE;
			*comp_algorithm = scsi_4btoul(cp->comp_algorithm);
		} else {
			struct scsi_dev_conf_page *cp = &ntcs->dconf;
			/*
			 * We don't really know whether this device supports
			 * Data Compression if the algorithm field is
			 * zero. Just say we do.
			 */
			*comp_supported = TRUE;
			*comp_enabled =
			    (cp->sel_comp_alg != SA_COMP_NONE)? TRUE : FALSE;
			*comp_algorithm = cp->sel_comp_alg;
		}
		if (tcs != NULL)
			bcopy(ntcs, tcs, sizeof (sa_comp_t));
	}

	if ((params_to_get & SA_PARAM_DENSITY_EXT)
	 && (softc->scsi_rev >= SCSI_REV_SPC)) {
		int i;

		for (i = 0; i < SA_DENSITY_TYPES; i++) {
			scsi_report_density_support(&ccb->csio,
			    /*retries*/ 1,
			    /*cbfcnp*/ NULL,
			    /*tag_action*/ MSG_SIMPLE_Q_TAG,
			    /*media*/ softc->density_type_bits[i] & SRDS_MEDIA,
			    /*medium_type*/ softc->density_type_bits[i] &
					    SRDS_MEDIUM_TYPE,
			    /*data_ptr*/ softc->density_info[i],
			    /*length*/ sizeof(softc->density_info[i]),
			    /*sense_len*/ SSD_FULL_SIZE,
			    /*timeout*/ REP_DENSITY_TIMEOUT);
			error = cam_periph_runccb(ccb, saerror, 0, SF_NO_PRINT,
			    softc->device_stats);
			status = ccb->ccb_h.status & CAM_STATUS_MASK;

			/*
			 * Some tape drives won't support this command at
			 * all, but hopefully we'll minimize that with the
			 * check for SPC or greater support above.  If they
			 * don't support the default report (neither the
			 * MEDIA or MEDIUM_TYPE bits set), then there is
			 * really no point in continuing on to look for
			 * other reports.
			 */
			if ((error != 0)
			 || (status != CAM_REQ_CMP)) {
				error = 0;
				softc->density_info_valid[i] = 0;
				if (softc->density_type_bits[i] == 0)
					break;
				else
					continue;
			}
			softc->density_info_valid[i] = ccb->csio.dxfer_len -
			    ccb->csio.resid;
		}
	}

	/*
	 * Get logical block protection parameters if the drive supports it.
	 */
	if ((params_to_get & SA_PARAM_LBP)
	 && (softc->flags & SA_FLAG_PROTECT_SUPP)) {
		struct scsi_mode_header_10 *mode10_hdr;
		struct scsi_control_data_prot_subpage *dp_page;
		struct scsi_mode_sense_10 *cdb;
		struct sa_prot_state *prot;
		int dp_len, returned_len;

		if (dp_size == 0)
			dp_size = sizeof(*dp_page);

		dp_len = sizeof(*mode10_hdr) + dp_size;
		mode10_hdr = malloc(dp_len, M_SCSISA, M_NOWAIT | M_ZERO);
		if (mode10_hdr == NULL) {
			error = ENOMEM;
			goto sagetparamsexit;
		}

		scsi_mode_sense_len(&ccb->csio,
				    /*retries*/ 5,
				    /*cbfcnp*/ NULL,
				    /*tag_action*/ MSG_SIMPLE_Q_TAG,
				    /*dbd*/ TRUE,
				    /*page_code*/ (prot_changeable == 0) ?
						  SMS_PAGE_CTRL_CURRENT :
						  SMS_PAGE_CTRL_CHANGEABLE,
				    /*page*/ SMS_CONTROL_MODE_PAGE,
				    /*param_buf*/ (uint8_t *)mode10_hdr,
				    /*param_len*/ dp_len,
				    /*minimum_cmd_size*/ 10,
				    /*sense_len*/ SSD_FULL_SIZE,
				    /*timeout*/ SCSIOP_TIMEOUT);
		/*
		 * XXX KDM we need to be able to set the subpage in the
		 * fill function.
		 */
		cdb = (struct scsi_mode_sense_10 *)ccb->csio.cdb_io.cdb_bytes;
		cdb->subpage = SA_CTRL_DP_SUBPAGE_CODE;

		error = cam_periph_runccb(ccb, saerror, 0, SF_NO_PRINT,
		    softc->device_stats);
		if (error != 0) {
			free(mode10_hdr, M_SCSISA);
			goto sagetparamsexit;
		}

		status = ccb->ccb_h.status & CAM_STATUS_MASK;
		if (status != CAM_REQ_CMP) {
			error = EINVAL;
			free(mode10_hdr, M_SCSISA);
			goto sagetparamsexit;
		}

		/*
		 * The returned data length at least has to be long enough
		 * for us to look at length in the mode page header.
		 */
		returned_len = ccb->csio.dxfer_len - ccb->csio.resid;
		if (returned_len < sizeof(mode10_hdr->data_length)) {
			error = EINVAL;
			free(mode10_hdr, M_SCSISA);
			goto sagetparamsexit;
		}

		returned_len = min(returned_len, 
		    sizeof(mode10_hdr->data_length) +
		    scsi_2btoul(mode10_hdr->data_length));

		dp_page = (struct scsi_control_data_prot_subpage *)
		    &mode10_hdr[1];

		/*
		 * We also have to have enough data to include the prot_bits
		 * in the subpage.
		 */
		if (returned_len < (sizeof(*mode10_hdr) +
		    __offsetof(struct scsi_control_data_prot_subpage, prot_bits)
		    + sizeof(dp_page->prot_bits))) {
			error = EINVAL;
			free(mode10_hdr, M_SCSISA);
			goto sagetparamsexit;
		}

		prot = &softc->prot_info.cur_prot_state;
		prot->prot_method = dp_page->prot_method;
		prot->pi_length = dp_page->pi_length &
		    SA_CTRL_DP_PI_LENGTH_MASK;
		prot->lbp_w = (dp_page->prot_bits & SA_CTRL_DP_LBP_W) ? 1 :0;
		prot->lbp_r = (dp_page->prot_bits & SA_CTRL_DP_LBP_R) ? 1 :0;
		prot->rbdp = (dp_page->prot_bits & SA_CTRL_DP_RBDP) ? 1 :0;
		prot->initialized = 1;

		if (prot_page != NULL)
			bcopy(dp_page, prot_page, min(sizeof(*prot_page),
			    sizeof(*dp_page)));

		free(mode10_hdr, M_SCSISA);
	}

	if (CAM_DEBUGGED(periph->path, CAM_DEBUG_INFO)) {
		int idx;
		char *xyz = mode_buffer;
		xpt_print_path(periph->path);
		printf("Mode Sense Data=");
		for (idx = 0; idx < mode_buffer_len; idx++)
			printf(" 0x%02x", xyz[idx] & 0xff);
		printf("\n");
	}

sagetparamsexit:

	xpt_release_ccb(ccb);
	free(mode_buffer, M_SCSISA);
	return (error);
}

/*
 * Set protection information to the pending protection information stored
 * in the softc.
 */
static int
sasetprot(struct cam_periph *periph, struct sa_prot_state *new_prot)
{
	struct sa_softc *softc;
	struct scsi_control_data_prot_subpage *dp_page, *dp_changeable;
	struct scsi_mode_header_10 *mode10_hdr, *mode10_changeable;
	union ccb *ccb;
	uint8_t current_speed;
	size_t dp_size, dp_page_length;
	int dp_len, buff_mode;
	int error;

	softc = (struct sa_softc *)periph->softc;
	mode10_hdr = NULL;
	mode10_changeable = NULL;
	ccb = NULL;

	/*
	 * Start off with the size set to the actual length of the page
	 * that we have defined.
	 */
	dp_size = sizeof(*dp_changeable);
	dp_page_length = dp_size -
	    __offsetof(struct scsi_control_data_prot_subpage, prot_method);

retry_length:

	dp_len = sizeof(*mode10_changeable) + dp_size;
	mode10_changeable = malloc(dp_len, M_SCSISA, M_NOWAIT | M_ZERO);
	if (mode10_changeable == NULL) {
		error = ENOMEM;
		goto bailout;
	}

	dp_changeable =
	    (struct scsi_control_data_prot_subpage *)&mode10_changeable[1];

	/*
	 * First get the data protection page changeable parameters mask.
	 * We need to know which parameters the drive supports changing.
	 * We also need to know what the drive claims that its page length
	 * is.  The reason is that IBM drives in particular are very picky
	 * about the page length.  They want it (the length set in the
	 * page structure itself) to be 28 bytes, and they want the
	 * parameter list length specified in the mode select header to be
	 * 40 bytes.  So, to work with IBM drives as well as any other tape
	 * drive, find out what the drive claims the page length is, and
	 * make sure that we match that.
	 */
	error = sagetparams(periph, SA_PARAM_SPEED | SA_PARAM_LBP,  
	    NULL, NULL, NULL, &buff_mode, NULL, &current_speed, NULL, NULL,
	    NULL, NULL, dp_changeable, dp_size, /*prot_changeable*/ 1);
	if (error != 0)
		goto bailout;

	if (scsi_2btoul(dp_changeable->length) > dp_page_length) {
		dp_page_length = scsi_2btoul(dp_changeable->length);
		dp_size = dp_page_length +
		    __offsetof(struct scsi_control_data_prot_subpage,
		    prot_method);
		free(mode10_changeable, M_SCSISA);
		mode10_changeable = NULL;
		goto retry_length;
	}

	mode10_hdr = malloc(dp_len, M_SCSISA, M_NOWAIT | M_ZERO);
	if (mode10_hdr == NULL) {
		error = ENOMEM;
		goto bailout;
	}

	dp_page = (struct scsi_control_data_prot_subpage *)&mode10_hdr[1];

	/*
	 * Now grab the actual current settings in the page.
	 */
	error = sagetparams(periph, SA_PARAM_SPEED | SA_PARAM_LBP,  
	    NULL, NULL, NULL, &buff_mode, NULL, &current_speed, NULL, NULL,
	    NULL, NULL, dp_page, dp_size, /*prot_changeable*/ 0);
	if (error != 0)
		goto bailout;

	/* These two fields need to be 0 for MODE SELECT */
	scsi_ulto2b(0, mode10_hdr->data_length);
	mode10_hdr->medium_type = 0;
	/* We are not including a block descriptor */
	scsi_ulto2b(0, mode10_hdr->blk_desc_len);

	mode10_hdr->dev_spec = current_speed;
	/* if set, set single-initiator buffering mode */
	if (softc->buffer_mode == SMH_SA_BUF_MODE_SIBUF) {
		mode10_hdr->dev_spec |= SMH_SA_BUF_MODE_SIBUF;
	}

	/*
	 * For each field, make sure that the drive allows changing it
	 * before bringing in the user's setting.
	 */
	if (dp_changeable->prot_method != 0)
		dp_page->prot_method = new_prot->prot_method;

	if (dp_changeable->pi_length & SA_CTRL_DP_PI_LENGTH_MASK) {
		dp_page->pi_length &= ~SA_CTRL_DP_PI_LENGTH_MASK;
		dp_page->pi_length |= (new_prot->pi_length &
		    SA_CTRL_DP_PI_LENGTH_MASK);
	}
	if (dp_changeable->prot_bits & SA_CTRL_DP_LBP_W) {
		if (new_prot->lbp_w)
			dp_page->prot_bits |= SA_CTRL_DP_LBP_W;
		else
			dp_page->prot_bits &= ~SA_CTRL_DP_LBP_W;
	}

	if (dp_changeable->prot_bits & SA_CTRL_DP_LBP_R) {
		if (new_prot->lbp_r)
			dp_page->prot_bits |= SA_CTRL_DP_LBP_R;
		else
			dp_page->prot_bits &= ~SA_CTRL_DP_LBP_R;
	}

	if (dp_changeable->prot_bits & SA_CTRL_DP_RBDP) {
		if (new_prot->rbdp)
			dp_page->prot_bits |= SA_CTRL_DP_RBDP;
		else
			dp_page->prot_bits &= ~SA_CTRL_DP_RBDP;
	}

	ccb = cam_periph_getccb(periph, 1);

	scsi_mode_select_len(&ccb->csio,
			     /*retries*/ 5,
			     /*cbfcnp*/ NULL,
			     /*tag_action*/ MSG_SIMPLE_Q_TAG,
			     /*scsi_page_fmt*/ TRUE,
			     /*save_pages*/ FALSE,
			     /*param_buf*/ (uint8_t *)mode10_hdr,
			     /*param_len*/ dp_len,
			     /*minimum_cmd_size*/ 10,
			     /*sense_len*/ SSD_FULL_SIZE,
			     /*timeout*/ SCSIOP_TIMEOUT);

	error = cam_periph_runccb(ccb, saerror, 0, 0, softc->device_stats);
	if (error != 0)
		goto bailout;

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		error = EINVAL;
		goto bailout;
	}

	/*
	 * The operation was successful.  We could just copy the settings
	 * the user requested, but just in case the drive ignored some of
	 * our settings, let's ask for status again.
	 */
	error = sagetparams(periph, SA_PARAM_SPEED | SA_PARAM_LBP,  
	    NULL, NULL, NULL, &buff_mode, NULL, &current_speed, NULL, NULL,
	    NULL, NULL, dp_page, dp_size, 0);

bailout:
	if (ccb != NULL)
		xpt_release_ccb(ccb);
	free(mode10_hdr, M_SCSISA);
	free(mode10_changeable, M_SCSISA);
	return (error);
}

/*
 * The purpose of this function is to set one of four different parameters
 * for a tape drive:
 *	- blocksize
 *	- density
 *	- compression / compression algorithm
 *	- buffering mode
 *
 * The assumption is that this will be called from saioctl(), and therefore
 * from a process context.  Thus the waiting malloc calls below.  If that
 * assumption ever changes, the malloc calls should be changed to be
 * NOWAIT mallocs.
 *
 * Any or all of the four parameters may be set when this function is
 * called.  It should handle setting more than one parameter at once.
 */
static int
sasetparams(struct cam_periph *periph, sa_params params_to_set,
	    u_int32_t blocksize, u_int8_t density, u_int32_t calg,
	    u_int32_t sense_flags)
{
	struct sa_softc *softc;
	u_int32_t current_blocksize;
	u_int32_t current_calg;
	u_int8_t current_density;
	u_int8_t current_speed;
	int comp_enabled, comp_supported;
	void *mode_buffer;
	int mode_buffer_len;
	struct scsi_mode_header_6 *mode_hdr;
	struct scsi_mode_blk_desc *mode_blk;
	sa_comp_t *ccomp, *cpage;
	int buff_mode;
	union ccb *ccb = NULL;
	int error;

	softc = (struct sa_softc *)periph->softc;

	ccomp = malloc(sizeof (sa_comp_t), M_SCSISA, M_NOWAIT);
	if (ccomp == NULL)
		return (ENOMEM);

	/*
	 * Since it doesn't make sense to set the number of blocks, or
	 * write protection, we won't try to get the current value.  We
	 * always want to get the blocksize, so we can set it back to the
	 * proper value.
	 */
	error = sagetparams(periph,
	    params_to_set | SA_PARAM_BLOCKSIZE | SA_PARAM_SPEED,
	    &current_blocksize, &current_density, NULL, &buff_mode, NULL,
	    &current_speed, &comp_supported, &comp_enabled,
	    &current_calg, ccomp, NULL, 0, 0);

	if (error != 0) {
		free(ccomp, M_SCSISA);
		return (error);
	}

	mode_buffer_len = sizeof(*mode_hdr) + sizeof(*mode_blk);
	if (params_to_set & SA_PARAM_COMPRESSION)
		mode_buffer_len += sizeof (sa_comp_t);

	mode_buffer = malloc(mode_buffer_len, M_SCSISA, M_NOWAIT | M_ZERO);
	if (mode_buffer == NULL) {
		free(ccomp, M_SCSISA);
		return (ENOMEM);
	}

	mode_hdr = (struct scsi_mode_header_6 *)mode_buffer;
	mode_blk = (struct scsi_mode_blk_desc *)&mode_hdr[1];

	ccb = cam_periph_getccb(periph, 1);

retry:

	if (params_to_set & SA_PARAM_COMPRESSION) {
		if (mode_blk) {
			cpage = (sa_comp_t *)&mode_blk[1];
		} else {
			cpage = (sa_comp_t *)&mode_hdr[1];
		}
		bcopy(ccomp, cpage, sizeof (sa_comp_t));
		cpage->hdr.pagecode &= ~0x80;
	} else
		cpage = NULL;

	/*
	 * If the caller wants us to set the blocksize, use the one they
	 * pass in.  Otherwise, use the blocksize we got back from the
	 * mode select above.
	 */
	if (mode_blk) {
		if (params_to_set & SA_PARAM_BLOCKSIZE)
			scsi_ulto3b(blocksize, mode_blk->blklen);
		else
			scsi_ulto3b(current_blocksize, mode_blk->blklen);

		/*
		 * Set density if requested, else preserve old density.
		 * SCSI_SAME_DENSITY only applies to SCSI-2 or better
		 * devices, else density we've latched up in our softc.
		 */
		if (params_to_set & SA_PARAM_DENSITY) {
			mode_blk->density = density;
		} else if (softc->scsi_rev > SCSI_REV_CCS) {
			mode_blk->density = SCSI_SAME_DENSITY;
		} else {
			mode_blk->density = softc->media_density;
		}
	}

	/*
	 * For mode selects, these two fields must be zero.
	 */
	mode_hdr->data_length = 0;
	mode_hdr->medium_type = 0;

	/* set the speed to the current value */
	mode_hdr->dev_spec = current_speed;

	/* if set, set single-initiator buffering mode */
	if (softc->buffer_mode == SMH_SA_BUF_MODE_SIBUF) {
		mode_hdr->dev_spec |= SMH_SA_BUF_MODE_SIBUF;
	}

	if (mode_blk)
		mode_hdr->blk_desc_len = sizeof(struct scsi_mode_blk_desc);
	else
		mode_hdr->blk_desc_len = 0;

	/*
	 * First, if the user wants us to set the compression algorithm or
	 * just turn compression on, check to make sure that this drive
	 * supports compression.
	 */
	if (params_to_set & SA_PARAM_COMPRESSION) {
		/*
		 * If the compression algorithm is 0, disable compression.
		 * If the compression algorithm is non-zero, enable
		 * compression and set the compression type to the
		 * specified compression algorithm, unless the algorithm is
		 * MT_COMP_ENABLE.  In that case, we look at the
		 * compression algorithm that is currently set and if it is
		 * non-zero, we leave it as-is.  If it is zero, and we have
		 * saved a compression algorithm from a time when
		 * compression was enabled before, set the compression to
		 * the saved value.
		 */
		switch (ccomp->hdr.pagecode & ~0x80) {
		case SA_DEVICE_CONFIGURATION_PAGE:
		{
			struct scsi_dev_conf_page *dcp = &cpage->dconf;
			if (calg == 0) {
				dcp->sel_comp_alg = SA_COMP_NONE;
				break;
			}
			if (calg != MT_COMP_ENABLE) {
				dcp->sel_comp_alg = calg;
			} else if (dcp->sel_comp_alg == SA_COMP_NONE &&
			    softc->saved_comp_algorithm != 0) {
				dcp->sel_comp_alg = softc->saved_comp_algorithm;
			}
			break;
		}
		case SA_DATA_COMPRESSION_PAGE:
		if (ccomp->dcomp.dce_and_dcc & SA_DCP_DCC) {
			struct scsi_data_compression_page *dcp = &cpage->dcomp;
			if (calg == 0) {
				/*
				 * Disable compression, but leave the
				 * decompression and the capability bit
				 * alone.
				 */
				dcp->dce_and_dcc = SA_DCP_DCC;
				dcp->dde_and_red |= SA_DCP_DDE;
				break;
			}
			/* enable compression && decompression */
			dcp->dce_and_dcc = SA_DCP_DCE | SA_DCP_DCC;
			dcp->dde_and_red |= SA_DCP_DDE;
			/*
			 * If there, use compression algorithm from caller.
			 * Otherwise, if there's a saved compression algorithm
			 * and there is no current algorithm, use the saved
			 * algorithm. Else parrot back what we got and hope
			 * for the best.
			 */
			if (calg != MT_COMP_ENABLE) {
				scsi_ulto4b(calg, dcp->comp_algorithm);
				scsi_ulto4b(calg, dcp->decomp_algorithm);
			} else if (scsi_4btoul(dcp->comp_algorithm) == 0 &&
			    softc->saved_comp_algorithm != 0) {
				scsi_ulto4b(softc->saved_comp_algorithm,
				    dcp->comp_algorithm);
				scsi_ulto4b(softc->saved_comp_algorithm,
				    dcp->decomp_algorithm);
			}
			break;
		}
		/*
		 * Compression does not appear to be supported-
		 * at least via the DATA COMPRESSION page. It
		 * would be too much to ask us to believe that
		 * the page itself is supported, but incorrectly
		 * reports an ability to manipulate data compression,
		 * so we'll assume that this device doesn't support
		 * compression. We can just fall through for that.
		 */
		/* FALLTHROUGH */
		default:
			/*
			 * The drive doesn't seem to support compression,
			 * so turn off the set compression bit.
			 */
			params_to_set &= ~SA_PARAM_COMPRESSION;
			xpt_print(periph->path,
			    "device does not seem to support compression\n");

			/*
			 * If that was the only thing the user wanted us to set,
			 * clean up allocated resources and return with
			 * 'operation not supported'.
			 */
			if (params_to_set == SA_PARAM_NONE) {
				free(mode_buffer, M_SCSISA);
				xpt_release_ccb(ccb);
				return (ENODEV);
			}
		
			/*
			 * That wasn't the only thing the user wanted us to set.
			 * So, decrease the stated mode buffer length by the
			 * size of the compression mode page.
			 */
			mode_buffer_len -= sizeof(sa_comp_t);
		}
	}

	/* It is safe to retry this operation */
	scsi_mode_select(&ccb->csio, 5, NULL, MSG_SIMPLE_Q_TAG,
	    (params_to_set & SA_PARAM_COMPRESSION)? TRUE : FALSE,
	    FALSE, mode_buffer, mode_buffer_len, SSD_FULL_SIZE, SCSIOP_TIMEOUT);

	error = cam_periph_runccb(ccb, saerror, 0,
	    sense_flags, softc->device_stats);

	if (CAM_DEBUGGED(periph->path, CAM_DEBUG_INFO)) {
		int idx;
		char *xyz = mode_buffer;
		xpt_print_path(periph->path);
		printf("Err%d, Mode Select Data=", error);
		for (idx = 0; idx < mode_buffer_len; idx++)
			printf(" 0x%02x", xyz[idx] & 0xff);
		printf("\n");
	}


	if (error) {
		/*
		 * If we can, try without setting density/blocksize.
		 */
		if (mode_blk) {
			if ((params_to_set &
			    (SA_PARAM_DENSITY|SA_PARAM_BLOCKSIZE)) == 0) {
				mode_blk = NULL;
				goto retry;
			}
		} else {
			mode_blk = (struct scsi_mode_blk_desc *)&mode_hdr[1];
			cpage = (sa_comp_t *)&mode_blk[1];
		}

		/*
		 * If we were setting the blocksize, and that failed, we
		 * want to set it to its original value.  If we weren't
		 * setting the blocksize, we don't want to change it.
		 */
		scsi_ulto3b(current_blocksize, mode_blk->blklen);

		/*
		 * Set density if requested, else preserve old density.
		 * SCSI_SAME_DENSITY only applies to SCSI-2 or better
		 * devices, else density we've latched up in our softc.
		 */
		if (params_to_set & SA_PARAM_DENSITY) {
			mode_blk->density = current_density;
		} else if (softc->scsi_rev > SCSI_REV_CCS) {
			mode_blk->density = SCSI_SAME_DENSITY;
		} else {
			mode_blk->density = softc->media_density;
		}

		if (params_to_set & SA_PARAM_COMPRESSION)
			bcopy(ccomp, cpage, sizeof (sa_comp_t));

		/*
		 * The retry count is the only CCB field that might have been
		 * changed that we care about, so reset it back to 1.
		 */
		ccb->ccb_h.retry_count = 1;
		cam_periph_runccb(ccb, saerror, 0, sense_flags,
		    softc->device_stats);
	}

	xpt_release_ccb(ccb);

	if (ccomp != NULL)
		free(ccomp, M_SCSISA);

	if (params_to_set & SA_PARAM_COMPRESSION) {
		if (error) {
			softc->flags &= ~SA_FLAG_COMP_ENABLED;
			/*
			 * Even if we get an error setting compression,
			 * do not say that we don't support it. We could
			 * have been wrong, or it may be media specific.
			 *	softc->flags &= ~SA_FLAG_COMP_SUPP;
			 */
			softc->saved_comp_algorithm = softc->comp_algorithm;
			softc->comp_algorithm = 0;
		} else {
			softc->flags |= SA_FLAG_COMP_ENABLED;
			softc->comp_algorithm = calg;
		}
	}

	free(mode_buffer, M_SCSISA);
	return (error);
}

static int
saextget(struct cdev *dev, struct cam_periph *periph, struct sbuf *sb,
    struct mtextget *g)
{
	int indent, error;
	char tmpstr[80];
	struct sa_softc *softc;
	int tmpint;
	uint32_t maxio_tmp;
	struct ccb_getdev cgd;

	softc = (struct sa_softc *)periph->softc;

	error = 0;

	error = sagetparams_common(dev, periph);
	if (error)
		goto extget_bailout;
	if (!SA_IS_CTRL(dev) && !softc->open_pending_mount)
		sagetpos(periph);

	indent = 0;
	SASBADDNODE(sb, indent, mtextget);
	/*
	 * Basic CAM peripheral information.
	 */
	SASBADDVARSTR(sb, indent, periph->periph_name, %s, periph_name,
	    strlen(periph->periph_name) + 1);
	SASBADDUINT(sb, indent, periph->unit_number, %u, unit_number);
	xpt_setup_ccb(&cgd.ccb_h,
		      periph->path,
		      CAM_PRIORITY_NORMAL);
	cgd.ccb_h.func_code = XPT_GDEV_TYPE;
	xpt_action((union ccb *)&cgd);
	if ((cgd.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		g->status = MT_EXT_GET_ERROR;
		snprintf(g->error_str, sizeof(g->error_str),
		    "Error %#x returned for XPT_GDEV_TYPE CCB",
		    cgd.ccb_h.status);
		goto extget_bailout;
	}

	cam_strvis(tmpstr, cgd.inq_data.vendor,
	    sizeof(cgd.inq_data.vendor), sizeof(tmpstr));
	SASBADDVARSTRDESC(sb, indent, tmpstr, %s, vendor,
	    sizeof(cgd.inq_data.vendor) + 1, "SCSI Vendor ID");

	cam_strvis(tmpstr, cgd.inq_data.product,
	    sizeof(cgd.inq_data.product), sizeof(tmpstr));
	SASBADDVARSTRDESC(sb, indent, tmpstr, %s, product,
	    sizeof(cgd.inq_data.product) + 1, "SCSI Product ID");

	cam_strvis(tmpstr, cgd.inq_data.revision,
	    sizeof(cgd.inq_data.revision), sizeof(tmpstr));
	SASBADDVARSTRDESC(sb, indent, tmpstr, %s, revision,
	    sizeof(cgd.inq_data.revision) + 1, "SCSI Revision");

	if (cgd.serial_num_len > 0) {
		char *tmpstr2;
		size_t ts2_len;
		int ts2_malloc;

		ts2_len = 0;

		if (cgd.serial_num_len > sizeof(tmpstr)) {
			ts2_len = cgd.serial_num_len + 1;
			ts2_malloc = 1;
			tmpstr2 = malloc(ts2_len, M_SCSISA, M_NOWAIT | M_ZERO);
			/*
			 * The 80 characters allocated on the stack above
			 * will handle the vast majority of serial numbers.
			 * If we run into one that is larger than that, and
			 * we can't malloc the length without blocking,
			 * bail out with an out of memory error.
			 */
			if (tmpstr2 == NULL) {
				error = ENOMEM;
				goto extget_bailout;
			}
		} else {
			ts2_len = sizeof(tmpstr);
			ts2_malloc = 0;
			tmpstr2 = tmpstr;
		}

		cam_strvis(tmpstr2, cgd.serial_num, cgd.serial_num_len,
		    ts2_len);

		SASBADDVARSTRDESC(sb, indent, tmpstr2, %s, serial_num,
		    (ssize_t)cgd.serial_num_len + 1, "Serial Number");
		if (ts2_malloc != 0)
			free(tmpstr2, M_SCSISA);
	} else {
		/*
		 * We return a serial_num element in any case, but it will
		 * be empty if the device has no serial number.
		 */
		tmpstr[0] = '\0';
		SASBADDVARSTRDESC(sb, indent, tmpstr, %s, serial_num,
		    (ssize_t)0, "Serial Number");
	}

	SASBADDUINTDESC(sb, indent, softc->maxio, %u, maxio, 
	    "Maximum I/O size allowed by driver and controller");

	SASBADDUINTDESC(sb, indent, softc->cpi_maxio, %u, cpi_maxio, 
	    "Maximum I/O size reported by controller");

	SASBADDUINTDESC(sb, indent, softc->max_blk, %u, max_blk, 
	    "Maximum block size supported by tape drive and media");

	SASBADDUINTDESC(sb, indent, softc->min_blk, %u, min_blk, 
	    "Minimum block size supported by tape drive and media");

	SASBADDUINTDESC(sb, indent, softc->blk_gran, %u, blk_gran, 
	    "Block granularity supported by tape drive and media");
	
	maxio_tmp = min(softc->max_blk, softc->maxio);

	SASBADDUINTDESC(sb, indent, maxio_tmp, %u, max_effective_iosize, 
	    "Maximum possible I/O size");

	SASBADDINTDESC(sb, indent, softc->flags & SA_FLAG_FIXED ? 1 : 0, %d, 
	    fixed_mode, "Set to 1 for fixed block mode, 0 for variable block");

	/*
	 * XXX KDM include SIM, bus, target, LUN?
	 */
	if (softc->flags & SA_FLAG_COMP_UNSUPP)
		tmpint = 0;
	else
		tmpint = 1;
	SASBADDINTDESC(sb, indent, tmpint, %d, compression_supported,
	    "Set to 1 if compression is supported, 0 if not");
	if (softc->flags & SA_FLAG_COMP_ENABLED)
		tmpint = 1;
	else
		tmpint = 0;
	SASBADDINTDESC(sb, indent, tmpint, %d, compression_enabled,
	    "Set to 1 if compression is enabled, 0 if not");
	SASBADDUINTDESC(sb, indent, softc->comp_algorithm, %u,
	    compression_algorithm, "Numeric compression algorithm");

	safillprot(softc, &indent, sb);

	SASBADDUINTDESC(sb, indent, softc->media_blksize, %u,
	    media_blocksize, "Block size reported by drive or set by user");
	SASBADDINTDESC(sb, indent, (intmax_t)softc->fileno, %jd,
	    calculated_fileno, "Calculated file number, -1 if unknown");
	SASBADDINTDESC(sb, indent, (intmax_t)softc->blkno, %jd,
	    calculated_rel_blkno, "Calculated block number relative to file, "
	    "set to -1 if unknown");
	SASBADDINTDESC(sb, indent, (intmax_t)softc->rep_fileno, %jd,
	    reported_fileno, "File number reported by drive, -1 if unknown");
	SASBADDINTDESC(sb, indent, (intmax_t)softc->rep_blkno, %jd,
	    reported_blkno, "Block number relative to BOP/BOT reported by "
	    "drive, -1 if unknown");
	SASBADDINTDESC(sb, indent, (intmax_t)softc->partition, %jd,
	    partition, "Current partition number, 0 is the default");
	SASBADDINTDESC(sb, indent, softc->bop, %d, bop,
	    "Set to 1 if drive is at the beginning of partition/tape, 0 if "
	    "not, -1 if unknown");
	SASBADDINTDESC(sb, indent, softc->eop, %d, eop,
	    "Set to 1 if drive is past early warning, 0 if not, -1 if unknown");
	SASBADDINTDESC(sb, indent, softc->bpew, %d, bpew,
	    "Set to 1 if drive is past programmable early warning, 0 if not, "
	    "-1 if unknown");
	SASBADDINTDESC(sb, indent, (intmax_t)softc->last_io_resid, %jd,
	    residual, "Residual for the last I/O");
	/*
	 * XXX KDM should we send a string with the current driver
	 * status already decoded instead of a numeric value?
	 */
	SASBADDINTDESC(sb, indent, softc->dsreg, %d, dsreg, 
	    "Current state of the driver");

	safilldensitysb(softc, &indent, sb);

	SASBENDNODE(sb, indent, mtextget);

extget_bailout:

	return (error);
}

static int
saparamget(struct sa_softc *softc, struct sbuf *sb)
{
	int indent;

	indent = 0;
	SASBADDNODE(sb, indent, mtparamget);
	SASBADDINTDESC(sb, indent, softc->sili, %d, sili, 
	    "Suppress an error on underlength variable reads");
	SASBADDINTDESC(sb, indent, softc->eot_warn, %d, eot_warn, 
	    "Return an error to warn that end of tape is approaching");
	safillprot(softc, &indent, sb);
	SASBENDNODE(sb, indent, mtparamget);

	return (0);
}

static void
saprevent(struct cam_periph *periph, int action)
{
	struct	sa_softc *softc;
	union	ccb *ccb;		
	int	error, sf;
		
	softc = (struct sa_softc *)periph->softc;

	if ((action == PR_ALLOW) && (softc->flags & SA_FLAG_TAPE_LOCKED) == 0)
		return;
	if ((action == PR_PREVENT) && (softc->flags & SA_FLAG_TAPE_LOCKED) != 0)
		return;

	/*
	 * We can be quiet about illegal requests.
	 */
	if (CAM_DEBUGGED(periph->path, CAM_DEBUG_INFO)) {
		sf = 0;
	} else
		sf = SF_QUIET_IR;

	ccb = cam_periph_getccb(periph, 1);

	/* It is safe to retry this operation */
	scsi_prevent(&ccb->csio, 5, NULL, MSG_SIMPLE_Q_TAG, action,
	    SSD_FULL_SIZE, SCSIOP_TIMEOUT);

	error = cam_periph_runccb(ccb, saerror, 0, sf, softc->device_stats);
	if (error == 0) {
		if (action == PR_ALLOW)
			softc->flags &= ~SA_FLAG_TAPE_LOCKED;
		else
			softc->flags |= SA_FLAG_TAPE_LOCKED;
	}

	xpt_release_ccb(ccb);
}

static int
sarewind(struct cam_periph *periph)
{
	union	ccb *ccb;
	struct	sa_softc *softc;
	int	error;
		
	softc = (struct sa_softc *)periph->softc;

	ccb = cam_periph_getccb(periph, 1);

	/* It is safe to retry this operation */
	scsi_rewind(&ccb->csio, 2, NULL, MSG_SIMPLE_Q_TAG, FALSE,
	    SSD_FULL_SIZE, REWIND_TIMEOUT);

	softc->dsreg = MTIO_DSREG_REW;
	error = cam_periph_runccb(ccb, saerror, 0, 0, softc->device_stats);
	softc->dsreg = MTIO_DSREG_REST;

	xpt_release_ccb(ccb);
	if (error == 0) {
		softc->partition = softc->fileno = softc->blkno = (daddr_t) 0;
		softc->rep_fileno = softc->rep_blkno = (daddr_t) 0;
	} else {
		softc->fileno = softc->blkno = (daddr_t) -1;
		softc->partition = (daddr_t) -1; 
		softc->rep_fileno = softc->rep_blkno = (daddr_t) -1;
	}
	return (error);
}

static int
saspace(struct cam_periph *periph, int count, scsi_space_code code)
{
	union	ccb *ccb;
	struct	sa_softc *softc;
	int	error;
		
	softc = (struct sa_softc *)periph->softc;

	ccb = cam_periph_getccb(periph, 1);

	/* This cannot be retried */

	scsi_space(&ccb->csio, 0, NULL, MSG_SIMPLE_Q_TAG, code, count,
	    SSD_FULL_SIZE, SPACE_TIMEOUT);

	/*
	 * Clear residual because we will be using it.
	 */
	softc->last_ctl_resid = 0;

	softc->dsreg = (count < 0)? MTIO_DSREG_REV : MTIO_DSREG_FWD;
	error = cam_periph_runccb(ccb, saerror, 0, 0, softc->device_stats);
	softc->dsreg = MTIO_DSREG_REST;

	xpt_release_ccb(ccb);

	/*
	 * If a spacing operation has failed, we need to invalidate
	 * this mount.
	 *
	 * If the spacing operation was setmarks or to end of recorded data,
	 * we no longer know our relative position.
	 *
	 * If the spacing operations was spacing files in reverse, we
	 * take account of the residual, but still check against less
	 * than zero- if we've gone negative, we must have hit BOT.
	 *
	 * If the spacing operations was spacing records in reverse and
	 * we have a residual, we've either hit BOT or hit a filemark.
	 * In the former case, we know our new record number (0). In
	 * the latter case, we have absolutely no idea what the real
	 * record number is- we've stopped between the end of the last
	 * record in the previous file and the filemark that stopped
	 * our spacing backwards.
	 */
	if (error) {
		softc->fileno = softc->blkno = (daddr_t) -1;
		softc->rep_blkno = softc->partition = (daddr_t) -1;
		softc->rep_fileno = (daddr_t) -1;
	} else if (code == SS_SETMARKS || code == SS_EOD) {
		softc->fileno = softc->blkno = (daddr_t) -1;
	} else if (code == SS_FILEMARKS && softc->fileno != (daddr_t) -1) {
		softc->fileno += (count - softc->last_ctl_resid);
		if (softc->fileno < 0)	/* we must of hit BOT */
			softc->fileno = 0;
		softc->blkno = 0;
	} else if (code == SS_BLOCKS && softc->blkno != (daddr_t) -1) {
		softc->blkno += (count - softc->last_ctl_resid);
		if (count < 0) {
			if (softc->last_ctl_resid || softc->blkno < 0) {
				if (softc->fileno == 0) {
					softc->blkno = 0;
				} else {
					softc->blkno = (daddr_t) -1;
				}
			}
		}
	}
	if (error == 0)
		sagetpos(periph);

	return (error);
}

static int
sawritefilemarks(struct cam_periph *periph, int nmarks, int setmarks, int immed)
{
	union	ccb *ccb;
	struct	sa_softc *softc;
	int	error, nwm = 0;

	softc = (struct sa_softc *)periph->softc;
	if (softc->open_rdonly)
		return (EBADF);

	ccb = cam_periph_getccb(periph, 1);
	/*
	 * Clear residual because we will be using it.
	 */
	softc->last_ctl_resid = 0;

	softc->dsreg = MTIO_DSREG_FMK;
	/* this *must* not be retried */
	scsi_write_filemarks(&ccb->csio, 0, NULL, MSG_SIMPLE_Q_TAG,
	    immed, setmarks, nmarks, SSD_FULL_SIZE, IO_TIMEOUT);
	softc->dsreg = MTIO_DSREG_REST;


	error = cam_periph_runccb(ccb, saerror, 0, 0, softc->device_stats);

	if (error == 0 && nmarks) {
		struct sa_softc *softc = (struct sa_softc *)periph->softc;
		nwm = nmarks - softc->last_ctl_resid;
		softc->filemarks += nwm;
	}

	xpt_release_ccb(ccb);

	/*
	 * Update relative positions (if we're doing that).
	 */
	if (error) {
		softc->fileno = softc->blkno = softc->partition = (daddr_t) -1;
	} else if (softc->fileno != (daddr_t) -1) {
		softc->fileno += nwm;
		softc->blkno = 0;
	}

	/*
	 * Ask the tape drive for position information.
	 */
	sagetpos(periph);

	/*
	 * If we got valid position information, since we just wrote a file
	 * mark, we know we're at the file mark and block 0 after that
	 * filemark.
	 */
	if (softc->rep_fileno != (daddr_t) -1) {
		softc->fileno = softc->rep_fileno;
		softc->blkno = 0;
	}

	return (error);
}

static int
sagetpos(struct cam_periph *periph)
{
	union ccb *ccb;
	struct scsi_tape_position_long_data long_pos;
	struct sa_softc *softc = (struct sa_softc *)periph->softc;
	int error;

	if (softc->quirks & SA_QUIRK_NO_LONG_POS) {
		softc->rep_fileno = (daddr_t) -1;
		softc->rep_blkno = (daddr_t) -1;
		softc->bop = softc->eop = softc->bpew = -1;
		return (EOPNOTSUPP);
	}

	bzero(&long_pos, sizeof(long_pos));

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
	scsi_read_position_10(&ccb->csio,
			      /*retries*/ 1,
			      /*cbfcnp*/ NULL,
			      /*tag_action*/ MSG_SIMPLE_Q_TAG,
			      /*service_action*/ SA_RPOS_LONG_FORM,
			      /*data_ptr*/ (uint8_t *)&long_pos,
			      /*length*/ sizeof(long_pos),
			      /*sense_len*/ SSD_FULL_SIZE,
			      /*timeout*/ SCSIOP_TIMEOUT);

	softc->dsreg = MTIO_DSREG_RBSY;
	error = cam_periph_runccb(ccb, saerror, 0, SF_QUIET_IR,
				  softc->device_stats);
	softc->dsreg = MTIO_DSREG_REST;

	if (error == 0) {
		if (long_pos.flags & SA_RPOS_LONG_MPU) {
			/*
			 * If the drive doesn't know what file mark it is
			 * on, our calculated filemark isn't going to be
			 * accurate either.
			 */
			softc->fileno = (daddr_t) -1;
			softc->rep_fileno = (daddr_t) -1;
		} else {
			softc->fileno = softc->rep_fileno =
			    scsi_8btou64(long_pos.logical_file_num);
		}

		if (long_pos.flags & SA_RPOS_LONG_LONU) {
			softc->partition = (daddr_t) -1;
			softc->rep_blkno = (daddr_t) -1;
			/*
			 * If the tape drive doesn't know its block
			 * position, we can't claim to know it either.
			 */
			softc->blkno = (daddr_t) -1;
		} else {
			softc->partition = scsi_4btoul(long_pos.partition);
			softc->rep_blkno =
			    scsi_8btou64(long_pos.logical_object_num);
		}
		if (long_pos.flags & SA_RPOS_LONG_BOP)
			softc->bop = 1;
		else
			softc->bop = 0;

		if (long_pos.flags & SA_RPOS_LONG_EOP)
			softc->eop = 1;
		else
			softc->eop = 0;

		if ((long_pos.flags & SA_RPOS_LONG_BPEW)
		 || (softc->set_pews_status != 0)) {
			softc->bpew = 1;
			if (softc->set_pews_status > 0)
				softc->set_pews_status--;
		} else
			softc->bpew = 0;
	} else if (error == EINVAL) {
		/*
		 * If this drive returned an invalid-request type error,
		 * then it likely doesn't support the long form report.
		 */
		softc->quirks |= SA_QUIRK_NO_LONG_POS;
	}

	if (error != 0) {
		softc->rep_fileno = softc->rep_blkno = (daddr_t) -1;
		softc->partition = (daddr_t) -1;
		softc->bop = softc->eop = softc->bpew = -1;
	}

	xpt_release_ccb(ccb);

	return (error);
}

static int
sardpos(struct cam_periph *periph, int hard, u_int32_t *blkptr)
{
	struct scsi_tape_position_data loc;
	union ccb *ccb;
	struct sa_softc *softc = (struct sa_softc *)periph->softc;
	int error;

	/*
	 * We try and flush any buffered writes here if we were writing
	 * and we're trying to get hardware block position. It eats
	 * up performance substantially, but I'm wary of drive firmware.
	 *
	 * I think that *logical* block position is probably okay-
	 * but hardware block position might have to wait for data
	 * to hit media to be valid. Caveat Emptor.
	 */

	if (hard && (softc->flags & SA_FLAG_TAPE_WRITTEN)) {
		error = sawritefilemarks(periph, 0, 0, 0);
		if (error && error != EACCES)
			return (error);
	}

	ccb = cam_periph_getccb(periph, 1);
	scsi_read_position(&ccb->csio, 1, NULL, MSG_SIMPLE_Q_TAG,
	    hard, &loc, SSD_FULL_SIZE, SCSIOP_TIMEOUT);
	softc->dsreg = MTIO_DSREG_RBSY;
	error = cam_periph_runccb(ccb, saerror, 0, 0, softc->device_stats);
	softc->dsreg = MTIO_DSREG_REST;

	if (error == 0) {
		if (loc.flags & SA_RPOS_UNCERTAIN) {
			error = EINVAL;		/* nothing is certain */
		} else {
			*blkptr = scsi_4btoul(loc.firstblk);
		}
	}

	xpt_release_ccb(ccb);
	return (error);
}

static int
sasetpos(struct cam_periph *periph, int hard, struct mtlocate *locate_info)
{
	union ccb *ccb;
	struct sa_softc *softc;
	int locate16;
	int immed, cp;
	int error;

	/*
	 * We used to try and flush any buffered writes here.
	 * Now we push this onto user applications to either
	 * flush the pending writes themselves (via a zero count
	 * WRITE FILEMARKS command) or they can trust their tape
	 * drive to do this correctly for them.
 	 */

	softc = (struct sa_softc *)periph->softc;
	ccb = cam_periph_getccb(periph, 1);

	cp = locate_info->flags & MT_LOCATE_FLAG_CHANGE_PART ? 1 : 0;
	immed = locate_info->flags & MT_LOCATE_FLAG_IMMED ? 1 : 0;

	/*
	 * Determine whether we have to use LOCATE or LOCATE16.  The hard
	 * bit is only possible with LOCATE, but the new ioctls do not
	 * allow setting that bit.  So we can't get into the situation of
	 * having the hard bit set with a block address that is larger than
	 * 32-bits.
	 */
	if (hard != 0)
		locate16 = 0;
	else if ((locate_info->dest_type != MT_LOCATE_DEST_OBJECT)
	      || (locate_info->block_address_mode != MT_LOCATE_BAM_IMPLICIT)
	      || (locate_info->logical_id > SA_SPOS_MAX_BLK))
		locate16 = 1;
	else
		locate16 = 0;

	if (locate16 != 0) {
		scsi_locate_16(&ccb->csio,
			       /*retries*/ 1,
			       /*cbfcnp*/ NULL,
			       /*tag_action*/ MSG_SIMPLE_Q_TAG,
			       /*immed*/ immed,
			       /*cp*/ cp,
			       /*dest_type*/ locate_info->dest_type,
			       /*bam*/ locate_info->block_address_mode,
			       /*partition*/ locate_info->partition,
			       /*logical_id*/ locate_info->logical_id,
			       /*sense_len*/ SSD_FULL_SIZE,
			       /*timeout*/ SPACE_TIMEOUT);
	} else {
		scsi_locate_10(&ccb->csio,
			       /*retries*/ 1,
			       /*cbfcnp*/ NULL,
			       /*tag_action*/ MSG_SIMPLE_Q_TAG,
			       /*immed*/ immed,
			       /*cp*/ cp,
			       /*hard*/ hard,
			       /*partition*/ locate_info->partition,
			       /*block_address*/ locate_info->logical_id,
			       /*sense_len*/ SSD_FULL_SIZE,
			       /*timeout*/ SPACE_TIMEOUT);
	}

	softc->dsreg = MTIO_DSREG_POS;
	error = cam_periph_runccb(ccb, saerror, 0, 0, softc->device_stats);
	softc->dsreg = MTIO_DSREG_REST;
	xpt_release_ccb(ccb);

	/*
	 * We assume the calculated file and block numbers are unknown
	 * unless we have enough information to populate them.
	 */
	softc->fileno = softc->blkno = (daddr_t) -1;

	/*
	 * If the user requested changing the partition and the request
	 * succeeded, note the partition.
	 */
	if ((error == 0)
	 && (cp != 0))
		softc->partition = locate_info->partition;
	else
		softc->partition = (daddr_t) -1;

	if (error == 0) {
		switch (locate_info->dest_type) {
		case MT_LOCATE_DEST_FILE:
			/*
			 * This is the only case where we can reliably
			 * calculate the file and block numbers.
			 */
			softc->fileno = locate_info->logical_id;
			softc->blkno = 0;
			break;
		case MT_LOCATE_DEST_OBJECT:
		case MT_LOCATE_DEST_SET:
		case MT_LOCATE_DEST_EOD:
		default:
			break;
		}
	}

	/*
	 * Ask the drive for current position information.
	 */
	sagetpos(periph);

	return (error);
}

static int
saretension(struct cam_periph *periph)
{
	union ccb *ccb;
	struct sa_softc *softc;
	int error;

	softc = (struct sa_softc *)periph->softc;

	ccb = cam_periph_getccb(periph, 1);

	/* It is safe to retry this operation */
	scsi_load_unload(&ccb->csio, 5, NULL, MSG_SIMPLE_Q_TAG, FALSE,
	    FALSE, TRUE,  TRUE, SSD_FULL_SIZE, ERASE_TIMEOUT);

	softc->dsreg = MTIO_DSREG_TEN;
	error = cam_periph_runccb(ccb, saerror, 0, 0, softc->device_stats);
	softc->dsreg = MTIO_DSREG_REST;

	xpt_release_ccb(ccb);
	if (error == 0) {
		softc->partition = softc->fileno = softc->blkno = (daddr_t) 0;
		sagetpos(periph);
	} else
		softc->partition = softc->fileno = softc->blkno = (daddr_t) -1;
	return (error);
}

static int
sareservereleaseunit(struct cam_periph *periph, int reserve)
{
	union ccb *ccb;
	struct sa_softc *softc;
	int error;

	softc = (struct sa_softc *)periph->softc;
	ccb = cam_periph_getccb(periph,  1);

	/* It is safe to retry this operation */
	scsi_reserve_release_unit(&ccb->csio, 2, NULL, MSG_SIMPLE_Q_TAG,
	    FALSE,  0, SSD_FULL_SIZE,  SCSIOP_TIMEOUT, reserve);
	softc->dsreg = MTIO_DSREG_RBSY;
	error = cam_periph_runccb(ccb, saerror, 0,
	    SF_RETRY_UA | SF_NO_PRINT, softc->device_stats);
	softc->dsreg = MTIO_DSREG_REST;
	xpt_release_ccb(ccb);

	/*
	 * If the error was Illegal Request, then the device doesn't support
	 * RESERVE/RELEASE. This is not an error.
	 */
	if (error == EINVAL) {
		error = 0;
	}

	return (error);
}

static int
saloadunload(struct cam_periph *periph, int load)
{
	union	ccb *ccb;
	struct	sa_softc *softc;
	int	error;

	softc = (struct sa_softc *)periph->softc;

	ccb = cam_periph_getccb(periph, 1);

	/* It is safe to retry this operation */
	scsi_load_unload(&ccb->csio, 5, NULL, MSG_SIMPLE_Q_TAG, FALSE,
	    FALSE, FALSE, load, SSD_FULL_SIZE, REWIND_TIMEOUT);

	softc->dsreg = (load)? MTIO_DSREG_LD : MTIO_DSREG_UNL;
	error = cam_periph_runccb(ccb, saerror, 0, 0, softc->device_stats);
	softc->dsreg = MTIO_DSREG_REST;
	xpt_release_ccb(ccb);

	if (error || load == 0) {
		softc->partition = softc->fileno = softc->blkno = (daddr_t) -1;
		softc->rep_fileno = softc->rep_blkno = (daddr_t) -1;
	} else if (error == 0) {
		softc->partition = softc->fileno = softc->blkno = (daddr_t) 0;
		sagetpos(periph);
	}
	return (error);
}

static int
saerase(struct cam_periph *periph, int longerase)
{

	union	ccb *ccb;
	struct	sa_softc *softc;
	int error;

	softc = (struct sa_softc *)periph->softc;
	if (softc->open_rdonly)
		return (EBADF);

	ccb = cam_periph_getccb(periph, 1);

	scsi_erase(&ccb->csio, 1, NULL, MSG_SIMPLE_Q_TAG, FALSE, longerase,
	    SSD_FULL_SIZE, ERASE_TIMEOUT);

	softc->dsreg = MTIO_DSREG_ZER;
	error = cam_periph_runccb(ccb, saerror, 0, 0, softc->device_stats);
	softc->dsreg = MTIO_DSREG_REST;

	xpt_release_ccb(ccb);
	return (error);
}

/*
 * Fill an sbuf with density data in XML format.  This particular macro
 * works for multi-byte integer fields.
 *
 * Note that 1 byte fields aren't supported here.  The reason is that the
 * compiler does not evaluate the sizeof(), and assumes that any of the
 * sizes are possible for a given field.  So passing in a multi-byte
 * field will result in a warning that the assignment makes an integer
 * from a pointer without a cast, if there is an assignment in the 1 byte
 * case.
 */
#define	SAFILLDENSSB(dens_data, sb, indent, field, desc_remain, 	\
		     len_to_go, cur_offset, desc){			\
	size_t cur_field_len;						\
									\
	cur_field_len = sizeof(dens_data->field);			\
	if (desc_remain < cur_field_len) {				\
		len_to_go -= desc_remain;				\
		cur_offset += desc_remain;				\
		continue;						\
	}								\
	len_to_go -= cur_field_len;					\
	cur_offset += cur_field_len;					\
	desc_remain -= cur_field_len;					\
									\
	switch (sizeof(dens_data->field)) {				\
	case 1:								\
		KASSERT(1 == 0, ("Programmer error, invalid 1 byte "	\
			"field width for SAFILLDENSFIELD"));		\
		break;							\
	case 2:								\
		SASBADDUINTDESC(sb, indent,				\
		    scsi_2btoul(dens_data->field), %u, field, desc);	\
		break;							\
	case 3:								\
		SASBADDUINTDESC(sb, indent,				\
		    scsi_3btoul(dens_data->field), %u, field, desc);	\
		break;							\
	case 4:								\
		SASBADDUINTDESC(sb, indent,				\
		    scsi_4btoul(dens_data->field), %u, field, desc);	\
		break;							\
	case 8:								\
		SASBADDUINTDESC(sb, indent, 				\
		    (uintmax_t)scsi_8btou64(dens_data->field),	%ju, 	\
		    field, desc);					\
		break;							\
	default:							\
		break;							\
	}								\
};
/*
 * Fill an sbuf with density data in XML format.  This particular macro
 * works for strings.
 */
#define	SAFILLDENSSBSTR(dens_data, sb, indent, field, desc_remain, 	\
			len_to_go, cur_offset, desc){			\
	size_t cur_field_len;						\
	char tmpstr[32];						\
									\
	cur_field_len = sizeof(dens_data->field);			\
	if (desc_remain < cur_field_len) {				\
		len_to_go -= desc_remain;				\
		cur_offset += desc_remain;				\
		continue;						\
	}								\
	len_to_go -= cur_field_len;					\
	cur_offset += cur_field_len;					\
	desc_remain -= cur_field_len;					\
									\
	cam_strvis(tmpstr, dens_data->field,				\
	    sizeof(dens_data->field), sizeof(tmpstr));			\
	SASBADDVARSTRDESC(sb, indent, tmpstr, %s, field,		\
	    strlen(tmpstr) + 1, desc);					\
};

/*
 * Fill an sbuf with density data descriptors.
 */
static void
safilldenstypesb(struct sbuf *sb, int *indent, uint8_t *buf, int buf_len,
    int is_density)
{
	struct scsi_density_hdr *hdr;
	uint32_t hdr_len;
	int len_to_go, cur_offset;
	int length_offset;
	int num_reports, need_close;

	/*
	 * We need at least the header length.  Note that this isn't an
	 * error, not all tape drives will have every data type.
	 */
	if (buf_len < sizeof(*hdr))
		goto bailout;


	hdr = (struct scsi_density_hdr *)buf;
	hdr_len = scsi_2btoul(hdr->length);
	len_to_go = min(buf_len - sizeof(*hdr), hdr_len);
	if (is_density) {
		length_offset = __offsetof(struct scsi_density_data,
		    bits_per_mm);
	} else {
		length_offset = __offsetof(struct scsi_medium_type_data,
		    num_density_codes);
	}
	cur_offset = sizeof(*hdr);

	num_reports = 0;
	need_close = 0;

	while (len_to_go > length_offset) {
		struct scsi_density_data *dens_data;
		struct scsi_medium_type_data *type_data;
		int desc_remain;
		size_t cur_field_len;

		dens_data = NULL;
		type_data = NULL;

		if (is_density) {
			dens_data =(struct scsi_density_data *)&buf[cur_offset];
			if (dens_data->byte2 & SDD_DLV)
				desc_remain = scsi_2btoul(dens_data->length);
			else
				desc_remain = SDD_DEFAULT_LENGTH -
				    length_offset;
		} else {
			type_data = (struct scsi_medium_type_data *)
			    &buf[cur_offset];
			desc_remain = scsi_2btoul(type_data->length);
		}

		len_to_go -= length_offset;
		desc_remain = min(desc_remain, len_to_go);
		cur_offset += length_offset;

		if (need_close != 0) {
			SASBENDNODE(sb, *indent, density_entry);
		}

		SASBADDNODENUM(sb, *indent, density_entry, num_reports);
		num_reports++;
		need_close = 1;

		if (is_density) {
			SASBADDUINTDESC(sb, *indent,
			    dens_data->primary_density_code, %u,
			    primary_density_code, "Primary Density Code");
			SASBADDUINTDESC(sb, *indent,
			    dens_data->secondary_density_code, %u,
			    secondary_density_code, "Secondary Density Code");
			SASBADDUINTDESC(sb, *indent,
			    dens_data->byte2 & ~SDD_DLV, %#x, density_flags,
			    "Density Flags");

			SAFILLDENSSB(dens_data, sb, *indent, bits_per_mm,
			    desc_remain, len_to_go, cur_offset, "Bits per mm");
			SAFILLDENSSB(dens_data, sb, *indent, media_width,
			    desc_remain, len_to_go, cur_offset, "Media width");
			SAFILLDENSSB(dens_data, sb, *indent, tracks,
			    desc_remain, len_to_go, cur_offset,
			    "Number of Tracks");
			SAFILLDENSSB(dens_data, sb, *indent, capacity,
			    desc_remain, len_to_go, cur_offset, "Capacity");

			SAFILLDENSSBSTR(dens_data, sb, *indent, assigning_org,
			    desc_remain, len_to_go, cur_offset,
			    "Assigning Organization");

			SAFILLDENSSBSTR(dens_data, sb, *indent, density_name,
			    desc_remain, len_to_go, cur_offset, "Density Name");

			SAFILLDENSSBSTR(dens_data, sb, *indent, description,
			    desc_remain, len_to_go, cur_offset, "Description");
		} else {
			int i;

			SASBADDUINTDESC(sb, *indent, type_data->medium_type,
			    %u, medium_type, "Medium Type");

			cur_field_len =
			    __offsetof(struct scsi_medium_type_data,
				       media_width) -
			    __offsetof(struct scsi_medium_type_data,
				       num_density_codes);

			if (desc_remain < cur_field_len) {
				len_to_go -= desc_remain;
				cur_offset += desc_remain;
				continue;
			}
			len_to_go -= cur_field_len;
			cur_offset += cur_field_len;
			desc_remain -= cur_field_len;

			SASBADDINTDESC(sb, *indent,
			    type_data->num_density_codes, %d,
			    num_density_codes, "Number of Density Codes");
			SASBADDNODE(sb, *indent, density_code_list);
			for (i = 0; i < type_data->num_density_codes;
			     i++) {
				SASBADDUINTDESC(sb, *indent,
				    type_data->primary_density_codes[i], %u,
				    density_code, "Density Code");
			}
			SASBENDNODE(sb, *indent, density_code_list);

			SAFILLDENSSB(type_data, sb, *indent, media_width,
			    desc_remain, len_to_go, cur_offset,
			    "Media width");
			SAFILLDENSSB(type_data, sb, *indent, medium_length,
			    desc_remain, len_to_go, cur_offset,
			    "Medium length");

			/*
			 * Account for the two reserved bytes.
			 */
			cur_field_len = sizeof(type_data->reserved2);
			if (desc_remain < cur_field_len) {
				len_to_go -= desc_remain;
				cur_offset += desc_remain;
				continue;
			}
			len_to_go -= cur_field_len;
			cur_offset += cur_field_len;
			desc_remain -= cur_field_len;
			
			SAFILLDENSSBSTR(type_data, sb, *indent, assigning_org,
			    desc_remain, len_to_go, cur_offset,
			    "Assigning Organization");
			SAFILLDENSSBSTR(type_data, sb, *indent,
			    medium_type_name, desc_remain, len_to_go,
			    cur_offset, "Medium type name");
			SAFILLDENSSBSTR(type_data, sb, *indent, description,
			    desc_remain, len_to_go, cur_offset, "Description");

		}
	}
	if (need_close != 0) {
		SASBENDNODE(sb, *indent, density_entry);
	}

bailout:
	return;
}

/*
 * Fill an sbuf with density data information
 */
static void
safilldensitysb(struct sa_softc *softc, int *indent, struct sbuf *sb)
{
	int i, is_density;
	
	SASBADDNODE(sb, *indent, mtdensity);
	SASBADDUINTDESC(sb, *indent, softc->media_density, %u, media_density,
	    "Current Medium Density");
	is_density = 0;
	for (i = 0; i < SA_DENSITY_TYPES; i++) {
		int tmpint;

		if (softc->density_info_valid[i] == 0)
			continue;

		SASBADDNODE(sb, *indent, density_report);
		if (softc->density_type_bits[i] & SRDS_MEDIUM_TYPE) {
			tmpint = 1;
			is_density = 0;
		} else {
			tmpint = 0;
			is_density = 1;
		}
		SASBADDINTDESC(sb, *indent, tmpint, %d, medium_type_report,
		    "Medium type report");

		if (softc->density_type_bits[i] & SRDS_MEDIA)
			tmpint = 1;
		else
			tmpint = 0;
		SASBADDINTDESC(sb, *indent, tmpint, %d, media_report, 
		    "Media report");

		safilldenstypesb(sb, indent, softc->density_info[i],
		    softc->density_info_valid[i], is_density);
		SASBENDNODE(sb, *indent, density_report);
	}
	SASBENDNODE(sb, *indent, mtdensity);
}

#endif /* _KERNEL */

/*
 * Read tape block limits command.
 */
void
scsi_read_block_limits(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action,
		   struct scsi_read_block_limits_data *rlimit_buf,
		   u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_read_block_limits *scsi_cmd;

	cam_fill_csio(csio, retries, cbfcnp, CAM_DIR_IN, tag_action,
	     (u_int8_t *)rlimit_buf, sizeof(*rlimit_buf), sense_len,
	     sizeof(*scsi_cmd), timeout);

	scsi_cmd = (struct scsi_read_block_limits *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = READ_BLOCK_LIMITS;
}

void
scsi_sa_read_write(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action, int readop, int sli,
		   int fixed, u_int32_t length, u_int8_t *data_ptr,
		   u_int32_t dxfer_len, u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_sa_rw *scsi_cmd;
	int read;

	read = (readop & SCSI_RW_DIRMASK) == SCSI_RW_READ;

	scsi_cmd = (struct scsi_sa_rw *)&csio->cdb_io.cdb_bytes;
	scsi_cmd->opcode = read ? SA_READ : SA_WRITE;
	scsi_cmd->sli_fixed = 0;
	if (sli && read)
		scsi_cmd->sli_fixed |= SAR_SLI;
	if (fixed)
		scsi_cmd->sli_fixed |= SARW_FIXED;
	scsi_ulto3b(length, scsi_cmd->length);
	scsi_cmd->control = 0;

	cam_fill_csio(csio, retries, cbfcnp, (read ? CAM_DIR_IN : CAM_DIR_OUT) |
	    ((readop & SCSI_RW_BIO) != 0 ? CAM_DATA_BIO : 0),
	    tag_action, data_ptr, dxfer_len, sense_len,
	    sizeof(*scsi_cmd), timeout);
}

void
scsi_load_unload(struct ccb_scsiio *csio, u_int32_t retries,         
		 void (*cbfcnp)(struct cam_periph *, union ccb *),   
		 u_int8_t tag_action, int immediate, int eot,
		 int reten, int load, u_int8_t sense_len,
		 u_int32_t timeout)
{
	struct scsi_load_unload *scsi_cmd;

	scsi_cmd = (struct scsi_load_unload *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = LOAD_UNLOAD;
	if (immediate)
		scsi_cmd->immediate = SLU_IMMED;
	if (eot)
		scsi_cmd->eot_reten_load |= SLU_EOT;
	if (reten)
		scsi_cmd->eot_reten_load |= SLU_RETEN;
	if (load)
		scsi_cmd->eot_reten_load |= SLU_LOAD;

	cam_fill_csio(csio, retries, cbfcnp, CAM_DIR_NONE, tag_action,
	    NULL, 0, sense_len, sizeof(*scsi_cmd), timeout);	
}

void
scsi_rewind(struct ccb_scsiio *csio, u_int32_t retries,         
	    void (*cbfcnp)(struct cam_periph *, union ccb *),   
	    u_int8_t tag_action, int immediate, u_int8_t sense_len,     
	    u_int32_t timeout)
{
	struct scsi_rewind *scsi_cmd;

	scsi_cmd = (struct scsi_rewind *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = REWIND;
	if (immediate)
		scsi_cmd->immediate = SREW_IMMED;
	
	cam_fill_csio(csio, retries, cbfcnp, CAM_DIR_NONE, tag_action, NULL,
	    0, sense_len, sizeof(*scsi_cmd), timeout);
}

void
scsi_space(struct ccb_scsiio *csio, u_int32_t retries,
	   void (*cbfcnp)(struct cam_periph *, union ccb *),
	   u_int8_t tag_action, scsi_space_code code,
	   u_int32_t count, u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_space *scsi_cmd;

	scsi_cmd = (struct scsi_space *)&csio->cdb_io.cdb_bytes;
	scsi_cmd->opcode = SPACE;
	scsi_cmd->code = code;
	scsi_ulto3b(count, scsi_cmd->count);
	scsi_cmd->control = 0;

	cam_fill_csio(csio, retries, cbfcnp, CAM_DIR_NONE, tag_action, NULL,
	    0, sense_len, sizeof(*scsi_cmd), timeout);
}

void
scsi_write_filemarks(struct ccb_scsiio *csio, u_int32_t retries,
		     void (*cbfcnp)(struct cam_periph *, union ccb *),
		     u_int8_t tag_action, int immediate, int setmark,
		     u_int32_t num_marks, u_int8_t sense_len,
		     u_int32_t timeout)
{
	struct scsi_write_filemarks *scsi_cmd;

	scsi_cmd = (struct scsi_write_filemarks *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = WRITE_FILEMARKS;
	if (immediate)
		scsi_cmd->byte2 |= SWFMRK_IMMED;
	if (setmark)
		scsi_cmd->byte2 |= SWFMRK_WSMK;
	
	scsi_ulto3b(num_marks, scsi_cmd->num_marks);

	cam_fill_csio(csio, retries, cbfcnp, CAM_DIR_NONE, tag_action, NULL,
	    0, sense_len, sizeof(*scsi_cmd), timeout);
}

/*
 * The reserve and release unit commands differ only by their opcodes.
 */
void
scsi_reserve_release_unit(struct ccb_scsiio *csio, u_int32_t retries,
			  void (*cbfcnp)(struct cam_periph *, union ccb *),
			  u_int8_t tag_action, int third_party,
			  int third_party_id, u_int8_t sense_len,
			  u_int32_t timeout, int reserve)
{
	struct scsi_reserve_release_unit *scsi_cmd;

	scsi_cmd = (struct scsi_reserve_release_unit *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	if (reserve)
		scsi_cmd->opcode = RESERVE_UNIT;
	else
		scsi_cmd->opcode = RELEASE_UNIT;

	if (third_party) {
		scsi_cmd->lun_thirdparty |= SRRU_3RD_PARTY;
		scsi_cmd->lun_thirdparty |=
			((third_party_id << SRRU_3RD_SHAMT) & SRRU_3RD_MASK);
	}

	cam_fill_csio(csio, retries, cbfcnp, CAM_DIR_NONE, tag_action, NULL,
	    0, sense_len, sizeof(*scsi_cmd), timeout);
}

void
scsi_erase(struct ccb_scsiio *csio, u_int32_t retries,
	   void (*cbfcnp)(struct cam_periph *, union ccb *),
	   u_int8_t tag_action, int immediate, int long_erase,
	   u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_erase *scsi_cmd;

	scsi_cmd = (struct scsi_erase *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = ERASE;

	if (immediate)
		scsi_cmd->lun_imm_long |= SE_IMMED;

	if (long_erase)
		scsi_cmd->lun_imm_long |= SE_LONG;

	cam_fill_csio(csio, retries, cbfcnp, CAM_DIR_NONE, tag_action, NULL,
	    0, sense_len, sizeof(*scsi_cmd), timeout);
}

/*
 * Read Tape Position command.
 */
void
scsi_read_position(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action, int hardsoft,
		   struct scsi_tape_position_data *sbp,
		   u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_tape_read_position *scmd;

	cam_fill_csio(csio, retries, cbfcnp, CAM_DIR_IN, tag_action,
	    (u_int8_t *)sbp, sizeof (*sbp), sense_len, sizeof(*scmd), timeout);
	scmd = (struct scsi_tape_read_position *)&csio->cdb_io.cdb_bytes;
	bzero(scmd, sizeof(*scmd));
	scmd->opcode = READ_POSITION;
	scmd->byte1 = hardsoft;
}

/*
 * Read Tape Position command.
 */
void
scsi_read_position_10(struct ccb_scsiio *csio, u_int32_t retries,
		      void (*cbfcnp)(struct cam_periph *, union ccb *),
		      u_int8_t tag_action, int service_action,
		      u_int8_t *data_ptr, u_int32_t length,
		      u_int32_t sense_len, u_int32_t timeout)
{
	struct scsi_tape_read_position *scmd;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_IN,
		      tag_action,
		      /*data_ptr*/data_ptr,
		      /*dxfer_len*/length,
		      sense_len,
		      sizeof(*scmd),
		      timeout);


	scmd = (struct scsi_tape_read_position *)&csio->cdb_io.cdb_bytes;
	bzero(scmd, sizeof(*scmd));
	scmd->opcode = READ_POSITION;
	scmd->byte1 = service_action;
	/*
	 * The length is only currently set (as of SSC4r03) if the extended
	 * form is specified.  The other forms have fixed lengths.
	 */
	if (service_action == SA_RPOS_EXTENDED_FORM)
		scsi_ulto2b(length, scmd->length);
}

/*
 * Set Tape Position command.
 */
void
scsi_set_position(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action, int hardsoft, u_int32_t blkno,
		   u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_tape_locate *scmd;

	cam_fill_csio(csio, retries, cbfcnp, CAM_DIR_NONE, tag_action,
	    (u_int8_t *)NULL, 0, sense_len, sizeof(*scmd), timeout);
	scmd = (struct scsi_tape_locate *)&csio->cdb_io.cdb_bytes;
	bzero(scmd, sizeof(*scmd));
	scmd->opcode = LOCATE;
	if (hardsoft)
		scmd->byte1 |= SA_SPOS_BT;
	scsi_ulto4b(blkno, scmd->blkaddr);
}

/*
 * XXX KDM figure out how to make a compatibility function.
 */
void
scsi_locate_10(struct ccb_scsiio *csio, u_int32_t retries,
	       void (*cbfcnp)(struct cam_periph *, union ccb *),
	       u_int8_t tag_action, int immed, int cp, int hard,
	       int64_t partition, u_int32_t block_address,
	       int sense_len, u_int32_t timeout)
{
	struct scsi_tape_locate *scmd;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/ NULL,
		      /*dxfer_len*/ 0,
		      sense_len,
		      sizeof(*scmd),
		      timeout);
	scmd = (struct scsi_tape_locate *)&csio->cdb_io.cdb_bytes;
	bzero(scmd, sizeof(*scmd));
	scmd->opcode = LOCATE;
	if (immed)
		scmd->byte1 |= SA_SPOS_IMMED;
	if (cp)
		scmd->byte1 |= SA_SPOS_CP;
	if (hard)
		scmd->byte1 |= SA_SPOS_BT;
	scsi_ulto4b(block_address, scmd->blkaddr);
	scmd->partition = partition;
}

void
scsi_locate_16(struct ccb_scsiio *csio, u_int32_t retries,
	       void (*cbfcnp)(struct cam_periph *, union ccb *),
	       u_int8_t tag_action, int immed, int cp, u_int8_t dest_type,
	       int bam, int64_t partition, u_int64_t logical_id,
	       int sense_len, u_int32_t timeout)
{

	struct scsi_locate_16 *scsi_cmd;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);

	scsi_cmd = (struct scsi_locate_16 *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = LOCATE_16;
	if (immed)
		scsi_cmd->byte1 |= SA_LC_IMMEDIATE;
	if (cp)
		scsi_cmd->byte1 |= SA_LC_CP;
	scsi_cmd->byte1 |= (dest_type << SA_LC_DEST_TYPE_SHIFT);

	scsi_cmd->byte2 |= bam;
	scsi_cmd->partition = partition;
	scsi_u64to8b(logical_id, scsi_cmd->logical_id);
}

void
scsi_report_density_support(struct ccb_scsiio *csio, u_int32_t retries,
			    void (*cbfcnp)(struct cam_periph *, union ccb *),
			    u_int8_t tag_action, int media, int medium_type,
			    u_int8_t *data_ptr, u_int32_t length,
			    u_int32_t sense_len, u_int32_t timeout)
{
	struct scsi_report_density_support *scsi_cmd;

	scsi_cmd =(struct scsi_report_density_support *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = REPORT_DENSITY_SUPPORT;
	if (media != 0)
		scsi_cmd->byte1 |= SRDS_MEDIA;
	if (medium_type != 0)
		scsi_cmd->byte1 |= SRDS_MEDIUM_TYPE;

	scsi_ulto2b(length, scsi_cmd->length);

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_IN,
		      tag_action,
		      /*data_ptr*/data_ptr,
		      /*dxfer_len*/length,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_set_capacity(struct ccb_scsiio *csio, u_int32_t retries,
		  void (*cbfcnp)(struct cam_periph *, union ccb *),
		  u_int8_t tag_action, int byte1, u_int32_t proportion,
		  u_int32_t sense_len, u_int32_t timeout)
{
	struct scsi_set_capacity *scsi_cmd;

	scsi_cmd = (struct scsi_set_capacity *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = SET_CAPACITY;

	scsi_cmd->byte1 = byte1;
	scsi_ulto2b(proportion, scsi_cmd->cap_proportion);

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_format_medium(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action, int byte1, int byte2, 
		   u_int8_t *data_ptr, u_int32_t dxfer_len,
		   u_int32_t sense_len, u_int32_t timeout)
{
	struct scsi_format_medium *scsi_cmd;

	scsi_cmd = (struct scsi_format_medium*)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = FORMAT_MEDIUM;

	scsi_cmd->byte1 = byte1;
	scsi_cmd->byte2 = byte2;

	scsi_ulto2b(dxfer_len, scsi_cmd->length);

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/(dxfer_len > 0) ? CAM_DIR_OUT : CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/ data_ptr,
		      /*dxfer_len*/ dxfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_allow_overwrite(struct ccb_scsiio *csio, u_int32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   u_int8_t tag_action, int allow_overwrite, int partition, 
		   u_int64_t logical_id, u_int32_t sense_len, u_int32_t timeout)
{
	struct scsi_allow_overwrite *scsi_cmd;

	scsi_cmd = (struct scsi_allow_overwrite *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = ALLOW_OVERWRITE;

	scsi_cmd->allow_overwrite = allow_overwrite;
	scsi_cmd->partition = partition;
	scsi_u64to8b(logical_id, scsi_cmd->logical_id);

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/ NULL,
		      /*dxfer_len*/ 0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}
