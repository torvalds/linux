/*	$OpenBSD: scsi_tape.h,v 1.11 2019/09/27 23:07:42 krw Exp $	*/
/*	$NetBSD: scsi_tape.h,v 1.9 1996/05/24 02:04:47 thorpej Exp $	*/

/*
 * Copyright (c) 1994 Charles Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*
 * Originally written by Julian Elischer (julian@tfs.com)
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

/*
 * SCSI tape interface description
 */

#ifndef	_SCSI_SCSI_TAPE_H
#define _SCSI_SCSI_TAPE_H

/*
 * SCSI command formats
 */

#define	READ			0x08
#define WRITE			0x0a
struct scsi_rw_tape {
	u_int8_t opcode;
	u_int8_t byte2;
#define	SRW_FIXED		0x01
	u_int8_t len[3];
	u_int8_t control;
};

#define	SPACE			0x11
struct scsi_space {
	u_int8_t opcode;
	u_int8_t byte2;
#define	SS_CODE			0x03
#define SP_BLKS			0x00
#define SP_FILEMARKS		0x01
#define SP_SEQ_FILEMARKS	0x02
#define	SP_EOM			0x03
	u_int8_t number[3];
	u_int8_t control;
};

#define	WRITE_FILEMARKS		0x10
struct scsi_write_filemarks {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t number[3];
	u_int8_t control;
};

#define REWIND			0x01
struct scsi_rewind {
	u_int8_t opcode;
	u_int8_t byte2;
#define	SR_IMMED		0x01
	u_int8_t unused[3];
	u_int8_t control;
};

#define LOAD			0x1b
struct scsi_load {
	u_int8_t opcode;
	u_int8_t byte2;
#define	SL_IMMED		0x01
	u_int8_t unused[2];
	u_int8_t how;
#define LD_UNLOAD		0x00
#define LD_LOAD			0x01
#define LD_RETENSION		0x02
	u_int8_t control;
};

#define	ERASE			0x19
struct scsi_erase {
	u_int8_t opcode;
	u_int8_t byte2;
#define	SE_LONG			0x01
#define	SE_IMMED		0x02
	u_int8_t unused[3];
	u_int8_t control;
};

#define	READ_BLOCK_LIMITS	0x05
struct scsi_block_limits {
	u_int8_t opcode;
	u_int8_t byte2;
	u_int8_t unused[3];
	u_int8_t control;
};

struct scsi_block_limits_data {
	u_int8_t reserved;
	u_int8_t max_length[3];	/* Most significant */
	u_int8_t min_length[2];	/* Most significant */
};

/* See SCSI-II spec 9.3.3.1 */
struct scsi_tape_dev_conf_page {
	u_int8_t pagecode;	/* 0x10 */
	u_int8_t pagelength;	/* 0x0e */
	u_int8_t byte2;
#define	SMT_CAP			0x40	/* change active partition */
#define	SMT_CAF			0x20	/* change active format */
#define	SMT_AFMASK		0x1f	/* active format mask */
	u_int8_t active_partition;
	u_int8_t wb_full_ratio;
	u_int8_t rb_empty_ratio;
	u_int8_t wrdelay_time[2];
	u_int8_t byte8;
#define	SMT_DBR			0x80	/* data buffer recovery */
#define	SMT_BIS			0x40	/* block identifiers supported */
#define	SMT_RSMK		0x20	/* report setmarks */
#define	SMT_AVC			0x10	/* automatic velocity control */
#define SMT_SOCF_MASK		0xc0	/* stop on consecutive formats */
#define	SMT_RBO			0x20	/* recover buffer order */
#define	SMT_REW			0x10	/* report early warning */
	u_int8_t gap_size;
	u_int8_t byte10;
#define	SMT_EODDEFINED		0xe0	/* EOD defined */
#define	SMT_EEG			0x10	/* enable EOD generation */
#define	SMT_SEW			0x80	/* synchronize at early warning */
	u_int8_t ew_bufsize[3];
	u_int8_t sel_comp_alg;
#define	SMT_COMP_NONE		0x00
#define	SMT_COMP_DEFAULT	0x01
	u_int8_t reserved;
};

/* defines for the device specific byte in the mode select/sense header */
#define	SMH_DSP_SPEED		0x0F
#define	SMH_DSP_BUFF_MODE	0x70
#define	SMH_DSP_BUFF_MODE_OFF	0x00
#define	SMH_DSP_BUFF_MODE_ON	0x10
#define	SMH_DSP_BUFF_MODE_MLTI	0x20

/**********************************************************************
			from the scsi2 spec
                Value Tracks Density(bpi) Code Type  Reference     Note
                0x1     9       800       NRZI  R    X3.22-1983    2
                0x2     9      1600       PE    R    X3.39-1986    2
                0x3     9      6250       GCR   R    X3.54-1986    2
                0x5    4/9     8000       GCR   C    X3.136-1986   1
                0x6     9      3200       PE    R    X3.157-1987   2
                0x7     4      6400       IMFM  C    X3.116-1986   1
                0x8     4      8000       GCR   CS   X3.158-1986   1
                0x9    18     37871       GCR   C    X3B5/87-099   2
                0xA    22      6667       MFM   C    X3B5/86-199   1
                0xB     4      1600       PE    C    X3.56-1986    1
                0xC    24     12690       GCR   C    HI-TC1        1,5
                0xD    24     25380       GCR   C    HI-TC2        1,5
                0xF    15     10000       GCR   C    QIC-120       1,5
                0x10   18     10000       GCR   C    QIC-150       1,5
                0x11   26     16000       GCR   C    QIC-320(525?) 1,5
                0x12   30     51667       RLL   C    QIC-1350      1,5
                0x13    1     61000       DDS   CS    X3B5/88-185A 4
                0x14    1     43245       RLL   CS    X3.202-1991  4
                0x15    1     45434       RLL   CS    ECMA TC17    4
                0x16   48     10000       MFM   C     X3.193-1990  1
                0x17   48     42500       MFM   C     X3B5/91-174  1
		0x45   73     67733       RLL   C     QIC3095

                where Code means:
                NRZI Non Return to Zero, change on ones
                GCR  Group Code Recording
                PE   Phase Encoded
                IMFM Inverted Modified Frequency Modulation
                MFM  Modified Frequency Modulation
                DDS  Dat Data Storage
                RLL  Run Length Encoding

                where Type means:
                R    Reel-to-Reel
                C    Cartridge
                CS   cassette

                where Notes means:
                1    Serial Recorded
                2    Parallel Recorded
                3    Old format know as QIC-11
                4    Helical Scan
                5    Not ANSI standard, rather industry standard.
********************************************************************/

#define	HALFINCH_800	0x01
#define	HALFINCH_1600	0x02
#define	HALFINCH_6250	0x03
#define	QIC_11		0x04	/* from Archive 150S Theory of Op. XXX	*/
#define QIC_24		0x05	/* may be bad, works for CIPHER ST150S XXX */
#define QIC_120		0x0f
#define QIC_150		0x10
#define QIC_320		0x11
#define QIC_525		0x11
#define QIC_1320	0x12
#define DDS		0x13
#define DAT_1		0x13
#define QIC_3080	0x29
#define QIC_3095	0x45

#endif /* _SCSI_SCSI_TAPE_H */
