/*	$OpenBSD: biovar.h,v 1.46 2020/06/07 16:51:43 kn Exp $	*/

/*
 * Copyright (c) 2002 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2005 Marco Peereboom.  All rights reserved.
 * Copyright (c) 2012 Joel Sing <jsing@openbsd.org>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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

#ifndef _SYS_DEV_BIOVAR_H_
#define _SYS_DEV_BIOVAR_H_

/*
 * Devices getting ioctls through this interface should use ioctl class 'B'
 * and command numbers starting from 32, lower ones are reserved for generic
 * ioctls. All ioctl data must be structures which start with a struct bio.
 */

#include <sys/types.h>

#define	BIO_MSG_COUNT	5
#define	BIO_MSG_LEN	128

struct bio_msg {
	int		bm_type;
#define	BIO_MSG_INFO	1
#define	BIO_MSG_WARN	2
#define	BIO_MSG_ERROR	3
	char		bm_msg[BIO_MSG_LEN];
};

struct bio_status {
	char		bs_controller[16];
	int		bs_status;
#define	BIO_STATUS_UNKNOWN	0
#define	BIO_STATUS_SUCCESS	1
#define	BIO_STATUS_ERROR	2
	int		bs_msg_count;
	struct bio_msg	bs_msgs[BIO_MSG_COUNT];
};

struct bio {
	void			*bio_cookie;
	struct bio_status	bio_status;
};

/* convert name to a cookie */
#define BIOCLOCATE _IOWR('B', 0, struct bio_locate)
struct bio_locate {
	struct bio	bl_bio;
	char		*bl_name;
};

#define BIOCINQ _IOWR('B', 32, struct bioc_inq)
struct bioc_inq {
	struct bio	bi_bio;

	char		bi_dev[16];	/* controller device */
	int		bi_novol;	/* nr of volumes */
	int		bi_nodisk;	/* nr of total disks */
};

#define BIOCDISK _IOWR('B', 33, struct bioc_disk)
/* structure that represents a disk in a RAID volume */
struct bioc_disk {
	struct bio	bd_bio;

	u_int16_t	bd_channel;
	u_int16_t	bd_target;
	u_int16_t	bd_lun;
	u_int16_t	bd_other_id;	/* unused for now  */

	int		bd_volid;	/* associate with volume */
	int		bd_diskid;	/* virtual disk */
	int		bd_status;	/* current status */
#define BIOC_SDONLINE		0x00
#define BIOC_SDONLINE_S		"Online"
#define BIOC_SDOFFLINE		0x01
#define BIOC_SDOFFLINE_S	"Offline"
#define BIOC_SDFAILED		0x02
#define BIOC_SDFAILED_S		"Failed"
#define BIOC_SDREBUILD		0x03
#define BIOC_SDREBUILD_S	"Rebuild"
#define BIOC_SDHOTSPARE		0x04
#define BIOC_SDHOTSPARE_S	"Hot spare"
#define BIOC_SDUNUSED		0x05
#define BIOC_SDUNUSED_S		"Unused"
#define BIOC_SDSCRUB		0x06
#define BIOC_SDSCRUB_S		"Scrubbing"
#define BIOC_SDINVALID		0xff
#define BIOC_SDINVALID_S	"Invalid"
	uint64_t	bd_size;	/* size of the disk */

	char		bd_vendor[32];	/* scsi string */
	char		bd_serial[32];	/* serial number */
	char		bd_procdev[16];	/* processor device */

	struct {
		int		bdp_percent;
		int		bdp_seconds;
	}		bd_patrol;
};

#define BIOCVOL _IOWR('B', 34, struct bioc_vol)
/* structure that represents a RAID volume */
struct bioc_vol {
	struct bio	bv_bio;
	int		bv_volid;	/* volume id */

	int16_t		bv_percent;	/* percent done operation */
	u_int16_t	bv_seconds;	/* seconds of progress so far */

	int		bv_status;	/* current status */
#define BIOC_SVONLINE		0x00
#define BIOC_SVONLINE_S		"Online"
#define BIOC_SVOFFLINE		0x01
#define BIOC_SVOFFLINE_S	"Offline"
#define BIOC_SVDEGRADED		0x02
#define BIOC_SVDEGRADED_S	"Degraded"
#define BIOC_SVBUILDING		0x03
#define BIOC_SVBUILDING_S	"Building"
#define BIOC_SVSCRUB		0x04
#define BIOC_SVSCRUB_S		"Scrubbing"
#define BIOC_SVREBUILD		0x05
#define BIOC_SVREBUILD_S	"Rebuild"
#define BIOC_SVINVALID		0xff
#define BIOC_SVINVALID_S	"Invalid"
	uint64_t	bv_size;	/* size of the disk */
	int		bv_level;	/* raid level */
	int		bv_nodisk;	/* nr of drives */
	int		bv_cache;	/* cache mode */
#define BIOC_CVUNKNOWN		0x00
#define BIOC_CVUNKNOWN_S	""
#define BIOC_CVWRITEBACK	0x01
#define BIOC_CVWRITEBACK_S	"WB"
#define BIOC_CVWRITETHROUGH	0x02
#define BIOC_CVWRITETHROUGH_S	"WT"

	char		bv_dev[16];	/* device */
	char		bv_vendor[32];	/* scsi string */
};

#define BIOCALARM _IOWR('B', 35, struct bioc_alarm)
struct bioc_alarm {
	struct bio	ba_bio;
	int		ba_opcode;

	int		ba_status;	/* only used with get state */
#define BIOC_SADISABLE		0x00	/* disable alarm */
#define BIOC_SAENABLE		0x01	/* enable alarm */
#define BIOC_SASILENCE		0x02	/* silence alarm */
#define BIOC_GASTATUS		0x03	/* get status */
#define BIOC_SATEST		0x04	/* test alarm */
};

#define BIOCBLINK _IOWR('B', 36, struct bioc_blink)
struct bioc_blink {
	struct bio	bb_bio;
	u_int16_t	bb_channel;
	u_int16_t	bb_target;

	int		bb_status;	/* current status */
#define BIOC_SBUNBLINK		0x00	/* disable blinking */
#define BIOC_SBBLINK		0x01	/* enable blink */
#define BIOC_SBALARM		0x02	/* enable alarm blink */
};

#define BIOCSETSTATE _IOWR('B', 37, struct bioc_setstate)
struct bioc_setstate {
	struct bio	bs_bio;
	u_int16_t	bs_channel;
	u_int16_t	bs_target;
	u_int16_t	bs_lun;
	u_int16_t	bs_other_id_type; /* use other_id instead of ctl */
#define BIOC_SSOTHER_UNUSED	0x00
#define BIOC_SSOTHER_DEVT	0x01
	int		bs_other_id;	/* cram dev_t or other id in here */

	int		bs_status;	/* change to this status */
#define BIOC_SSONLINE		0x00	/* online disk */
#define BIOC_SSOFFLINE		0x01	/* offline disk */
#define BIOC_SSHOTSPARE		0x02	/* mark as hotspare */
#define BIOC_SSREBUILD		0x03	/* rebuild on this disk */
	int		bs_volid;	/* volume id for rebuild */
};

#define BIOCCREATERAID _IOWR('B', 38, struct bioc_createraid)
struct bioc_createraid {
	struct bio	bc_bio;
	void		*bc_dev_list;
	u_int16_t	bc_dev_list_len;
	int32_t		bc_key_disk;
#define BIOC_CRMAXLEN		1024
	u_int16_t	bc_level;
	u_int32_t	bc_flags;
#define BIOC_SCFORCE		0x01	/* do not assemble, force create */
#define BIOC_SCDEVT		0x02	/* dev_t array or string in dev_list */
#define BIOC_SCNOAUTOASSEMBLE	0x04	/* do not assemble during autoconf */
#define BIOC_SCBOOTABLE		0x08	/* device is bootable */
	u_int32_t	bc_opaque_size;
	u_int32_t	bc_opaque_flags;
#define	BIOC_SOINVALID		0x00	/* no opaque pointer */
#define	BIOC_SOIN		0x01	/* kernel perspective direction */
#define BIOC_SOOUT		0x02	/* kernel perspective direction */
	u_int32_t	bc_opaque_status;
#define	BIOC_SOINOUT_FAILED	0x00	/* operation failed */
#define	BIOC_SOINOUT_OK		0x01	/* operation succeeded */
	void		*bc_opaque;
};

#define BIOCDELETERAID _IOWR('B', 39, struct bioc_deleteraid)
struct bioc_deleteraid {
	struct bio	bd_bio;
	u_int32_t	bd_flags;
#define BIOC_SDCLEARMETA	0x01	/* clear metadata region */
	char		bd_dev[16];	/* device */
};

#define BIOCDISCIPLINE _IOWR('B', 40, struct bioc_discipline)
struct bioc_discipline {
	struct bio	bd_bio;
	char		bd_dev[16];
	u_int32_t	bd_cmd;
	u_int32_t	bd_size;
	void		*bd_data;
};

#define BIOCINSTALLBOOT _IOWR('B', 41, struct bioc_installboot)
struct bioc_installboot {
	struct bio	bb_bio;
	char		bb_dev[16];
	void		*bb_bootblk;
	void		*bb_bootldr;
	u_int32_t	bb_bootblk_size;
	u_int32_t	bb_bootldr_size;
};

#define BIOCPATROL _IOWR('B', 42, struct bioc_patrol)
struct bioc_patrol {
	struct bio	bp_bio;
	int		bp_opcode;
#define BIOC_SPSTOP		0x00	/* stop patrol */
#define BIOC_SPSTART		0x01	/* start patrol */
#define BIOC_GPSTATUS		0x02	/* get status */
#define BIOC_SPDISABLE		0x03	/* disable patrol */
#define BIOC_SPAUTO		0x04	/* enable patrol as auto */
#define BIOC_SPMANUAL		0x05	/* enable patrol as manual */

	int		bp_mode;
#define	BIOC_SPMAUTO		0x00
#define	BIOC_SPMMANUAL		0x01
#define BIOC_SPMDISABLED	0x02
	int		bp_status;	/* only used with get state */
#define	BIOC_SPSSTOPPED		0x00
#define	BIOC_SPSREADY		0x01
#define BIOC_SPSACTIVE		0x02
#define BIOC_SPSABORTED		0xff

	int		bp_autoival;
	int		bp_autonext;
	int		bp_autonow;
};

/* kernel and userspace defines */
#define BIOC_INQ		0x0001
#define BIOC_DISK		0x0002
#define BIOC_VOL		0x0004
#define BIOC_ALARM		0x0008
#define BIOC_BLINK		0x0010
#define BIOC_SETSTATE		0x0020
#define BIOC_CREATERAID		0x0040
#define BIOC_DELETERAID		0x0080
#define BIOC_DISCIPLINE		0x0100
#define BIOC_INSTALLBOOT	0x0200
#define BIOC_PATROL		0x0400

/* user space defines */
#define BIOC_DEVLIST		0x10000

#ifdef _KERNEL
int	bio_register(struct device *, int (*)(struct device *, u_long,
	    caddr_t));
void	bio_unregister(struct device *);

void	bio_status_init(struct bio_status *, struct device *);
void	bio_status(struct bio_status *, int, int, const char *, va_list *);

void	bio_info(struct bio_status *, int, const char *, ...);
void	bio_warn(struct bio_status *, int, const char *, ...);
void	bio_error(struct bio_status *, int, const char *, ...);
#endif

#endif /* _SYS_DEV_BIOVAR_H_ */
