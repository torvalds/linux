/*	$OpenBSD: cd.h,v 1.28 2019/11/29 14:06:21 krw Exp $	*/
/*	$NetBSD: scsi_cd.h,v 1.6 1996/03/19 03:06:39 mycroft Exp $	*/

/*
 * Written by Julian Elischer (julian@tfs.com)
 * for TRW Financial Systems.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * Ported to run under 386BSD by Julian Elischer (julian@tfs.com) Sept 1992
 */
#ifndef	_SCSI_CD_H
#define _SCSI_CD_H

/*
 *	Define two bits always in the same place in byte 2 (flag byte)
 */
#define	CD_RELADDR	0x01
#define	CD_MSF		0x02

/*
 * SCSI command format
 */

struct scsi_blank {
	u_int8_t opcode;
	u_int8_t byte2;
#define BLANK_DISC	0
#define BLANK_MINIMAL	1
	u_int8_t addr[4];
	u_int8_t unused[5];
	u_int8_t control;
};

struct scsi_close_track {
	u_int8_t opcode;
	u_int8_t flags;
#define CT_IMMED	1
	u_int8_t closefunc;
#define CT_CLOSE_TRACK	1
#define CT_CLOSE_SESS	2
#define CT_CLOSE_BORDER 3
	u_int8_t unused;
	u_int8_t track[2];
	u_int8_t unused1[3];
	u_int8_t control;
};

struct scsi_pause {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused[6];
	u_int8_t resume;
	u_int8_t control;
};
#define	PA_PAUSE	1
#define PA_RESUME	0

struct scsi_play_msf {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused;
	u_int8_t start_m;
	u_int8_t start_s;
	u_int8_t start_f;
	u_int8_t end_m;
	u_int8_t end_s;
	u_int8_t end_f;
	u_int8_t control;
};

struct scsi_play_track {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused[2];
	u_int8_t start_track;
	u_int8_t start_index;
	u_int8_t unused1;
	u_int8_t end_track;
	u_int8_t end_index;
	u_int8_t control;
};

struct scsi_play {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t blk_addr[4];
	u_int8_t unused;
	u_int8_t xfer_len[2];
	u_int8_t control;
};

struct scsi_play_big {
	u_int8_t opcode;
	u_int8_t byte2;	/* same as above */
	u_int8_t blk_addr[4];
	u_int8_t xfer_len[4];
	u_int8_t unused;
	u_int8_t control;
};

struct scsi_play_rel_big {
	u_int8_t opcode;
	u_int8_t byte2;	/* same as above */
	u_int8_t blk_addr[4];
	u_int8_t xfer_len[4];
	u_int8_t track;
	u_int8_t control;
};

struct scsi_read_header {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t blk_addr[4];
	u_int8_t unused;
	u_int8_t data_len[2];
	u_int8_t control;
};

struct scsi_read_subchannel {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t byte3;
#define	SRS_SUBQ	0x40
	u_int8_t subchan_format;
	u_int8_t unused[2];
	u_int8_t track;
	u_int8_t data_len[2];
	u_int8_t control;
};

struct scsi_read_toc {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused[4];
	u_int8_t from_track;
	u_int8_t data_len[2];
	u_int8_t control;
};

struct scsi_read_track_info {
	u_int8_t opcode;
	u_int8_t addrtype;
#define RTI_LBA		0
#define RTI_TRACK	1
#define RTI_BORDER	2
	u_int8_t addr[4];
	u_int8_t unused;
	u_int8_t data_len[2];
	u_int8_t control;
};

struct scsi_load_unload {
	u_int8_t opcode;
	u_int8_t reserved;
#define	IMMED	0x1
	u_int8_t reserved2[2];
	u_int8_t options;
#define START	0x1
#define	LOUNLO	0x2
	u_int8_t reserved4[3];
	u_int8_t slot;
	u_int8_t reserved5[2];
	u_int8_t control;
};

struct scsi_set_cd_speed {
	u_int8_t opcode;
	u_int8_t rotation;
#define ROTATE_CLV 0
#define ROTATE_CAV 1
	u_int8_t read[2];
	u_int8_t write[2];
	u_int8_t reserved[5];
	u_int8_t control;
};

/*
 * Opcodes
 */

#define READ_SUBCHANNEL		0x42	/* cdrom read Subchannel */
#define READ_TOC		0x43	/* cdrom read TOC */
#define READ_HEADER		0x44	/* cdrom read header */
#define PLAY			0x45	/* cdrom play 'play audio' mode */
#define PLAY_MSF		0x47	/* cdrom play Min,Sec,Frames mode */
#define PLAY_TRACK		0x48	/* cdrom play track/index mode */
#define PLAY_TRACK_REL		0x49	/* cdrom play track/index mode */
#define PAUSE			0x4b	/* cdrom pause in 'play audio' mode */
#define READ_TRACK_INFO		0x52	/* read track/rzone info */
#define CLOSE_TRACK		0x5b	/* close track/rzone/session/border */
#define BLANK			0xa1	/* cdrom blank */
#define PLAY_BIG		0xa5	/* cdrom pause in 'play audio' mode */
#define LOAD_UNLOAD		0xa6	/* cdrom load/unload media */
#define PLAY_TRACK_REL_BIG	0xa9	/* cdrom play track/index mode */
#define SET_CD_SPEED		0xbb	/* set cdrom read/write speed */

/*
 * Mode pages
 */

#define ERR_RECOVERY_PAGE	0x01
#define WRITE_PARAM_PAGE	0x05
#define AUDIO_PAGE		0x0e
#define CDVD_CAPABILITIES_PAGE	0x2a

struct cd_audio_page {
	u_int8_t page_code;
#define	CD_PAGE_CODE	0x3F
#define	CD_PAGE_PS	0x80
	u_int8_t param_len;
	u_int8_t flags;
#define		CD_PA_SOTC	0x02
#define		CD_PA_IMMED	0x04
	u_int8_t unused[2];
	u_int8_t format_lba;
#define		CD_PA_FORMAT_LBA	0x0F
#define		CD_PA_APR_VALID	0x80
	u_int8_t lb_per_sec[2];
	struct	port_control {
		u_int8_t channels;
#define	CHANNEL 0x0F
#define	CHANNEL_0 1
#define	CHANNEL_1 2
#define	CHANNEL_2 4
#define	CHANNEL_3 8
#define	LEFT_CHANNEL	CHANNEL_0
#define	RIGHT_CHANNEL	CHANNEL_1
#define MUTE_CHANNEL    0x0
#define BOTH_CHANNEL    LEFT_CHANNEL | RIGHT_CHANNEL
		u_int8_t volume;
	} port[4];
#define	LEFT_PORT	0
#define	RIGHT_PORT	1
};

/*
 * There are 2352 bytes in a CD digital audio frame.  One frame is 1/75 of a
 * second, at 44.1kHz sample rate, 16 bits/sample, 2 channels.
 *
 * The frame data have the two channels interleaved, with the left
 * channel first.  Samples are little endian 16-bit signed values.
 */
#define CD_DA_BLKSIZ		2352	/* # bytes in CD-DA frame */
#define CD_NORMAL_DENSITY_CODE	0x00	/* from Toshiba CD-ROM specs */
#define CD_DA_DENSITY_CODE	0x82	/* from Toshiba CD-ROM specs */

struct scsi_read_dvd_structure {
	u_int8_t	opcode;		/* GPCMD_READ_DVD_STRUCTURE */
	u_int8_t	reserved;
	u_int8_t	address[4];
	u_int8_t	layer;
	u_int8_t	format;
	u_int8_t	length[2];
	u_int8_t	agid;		/* bottom 6 bits reserved */
	u_int8_t	control;
};

struct scsi_read_dvd_structure_data {
	u_int8_t	len[2];		/* Big-endian length of valid data. */
	u_int8_t	reserved[2];
	u_int8_t	data[2048];
};

#endif /* _SCSI_CD_H */
