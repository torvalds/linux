/*	$OpenBSD: atasmart.h,v 1.3 2004/01/16 22:41:52 grange Exp $	*/

/*
 * Copyright (c) 2002 Alexander Yurchenko <grange@rt.mipt.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* ATA SMART subcommands */
#define ATA_SMART_READ		0xd0 /* SMART read data */
#define ATA_SMART_THRESHOLD	0xd1 /* SMART read threshold */
#define ATA_SMART_AUTOSAVE	0xd2 /* SMART en-/disable attr. autosave */
#define ATA_SMART_SAVE		0xd3 /* SMART save attributes */
#define ATA_SMART_OFFLINE	0xd4 /* SMART execute offline immediately */
#define ATA_SMART_READLOG	0xd5 /* SMART read log */
#define ATA_SMART_WRITELOG	0xd6 /* SMART write log */
#define ATA_SMART_EN		0xd8 /* SMART enable operations */
#define ATA_SMART_DS		0xd9 /* SMART disable operations */
#define ATA_SMART_STATUS	0xda /* SMART return status */

/* device attribute */
struct attribute {
	u_int8_t  id;		/* Attribute ID */
	u_int16_t status;	/* Status flags */
	u_int8_t  value;	/* Attribute value */
	u_int8_t  raw[8];	/* Vendor specific */
} __packed;

/* read data sector */
struct smart_read {
	u_int16_t revision;	/* Data structure revision */
	struct attribute attribute[30];	/* Device attribute */
	u_int8_t  offstat;	/* Off-line data collection status */
#define SMART_OFFSTAT_NOTSTART	0x00
#define SMART_OFFSTAT_COMPLETE	0x02
#define SMART_OFFSTAT_SUSPEND	0x04
#define SMART_OFFSTAT_INTR	0x05
#define SMART_OFFSTAT_ERROR	0x06
	u_int8_t  selfstat;	/* Self-test execution status */
#define SMART_SELFSTAT_COMPLETE	0x00
#define SMART_SELFSTAT_ABORT	0x01
#define SMART_SELFSTAT_INTR	0x02
#define SMART_SELFSTAT_ERROR	0x03
#define SMART_SELFSTAT_UNKFAIL	0x04
#define SMART_SELFSTAT_ELFAIL	0x05
#define SMART_SELFSTAT_SRVFAIL	0x06
#define SMART_SELFSTAT_RDFAIL	0x07
#define SMART_SELFSTAT_PROGRESS	0x0f
	u_int16_t time;		/* Time	to complete data collection activity */
	u_int8_t  vendor1;	/* Vendor specific */
	u_int8_t  offcap;	/* Off-line data collection capability */
#define SMART_OFFCAP_EXEC	0x01
#define SMART_OFFCAP_ABORT	0x04
#define SMART_OFFCAP_READSCAN	0x08
#define SMART_OFFCAP_SELFTEST	0x10
	u_int16_t smartcap;	/* SMART capability */
#define SMART_SMARTCAP_SAVE	0x01
#define SMART_SMARTCAP_AUTOSAVE	0x02
	u_int8_t  errcap;	/* Error logging capability */
#define SMART_ERRCAP_ERRLOG	0x01
	u_int8_t  vendor2;	/* Vendor specific */
	u_int8_t  shtime;	/* Short self-test polling time */
	u_int8_t  extime;	/* Extended self-test polling time */
	u_int8_t  res[12];	/* Reserved */
	u_int8_t  vendor3[125];	/* Vendor specific */
	u_int8_t  cksum;	/* Data structure checksum */
};

/* threshold entry */
struct threshold {
	u_int8_t  id;		/* Threshold ID */
	u_int8_t  value;	/* Threshold value */
	u_int8_t  reserve[10];
};

/* read thresholds sector */
struct smart_threshold {
	u_int16_t revision;	/* Data structure revision */
	struct threshold threshold[30];
	u_int8_t  reserve[18];
	u_int8_t  vendor[131];
	u_int8_t  cksum;	/* Data structure checksum */
};

/* log directory entry */
struct smart_log_dir_entry {
	u_int8_t  sec_num;
	u_int8_t  res;
};

/* log directory sector */
struct smart_log_dir {
	u_int16_t version;
	struct    smart_log_dir_entry entry[255];
};

/* command data structure */
struct smart_log_cmd {
	u_int8_t reg_ctl;
	u_int8_t reg_feat;
	u_int8_t reg_seccnt;
	u_int8_t reg_lbalo;
	u_int8_t reg_lbamid;
	u_int8_t reg_lbahi;
	u_int8_t reg_dev;
	u_int8_t reg_cmd;
	u_int8_t time1;
	u_int8_t time2;
	u_int8_t time3;
	u_int8_t time4;
};

/* error data structure */
struct smart_log_err {
	u_int8_t res;
	u_int8_t reg_err;
	u_int8_t reg_seccnt;
	u_int8_t reg_lbalo;
	u_int8_t reg_lbamid;
	u_int8_t reg_lbahi;
	u_int8_t reg_dev;
	u_int8_t reg_stat;
	u_int8_t ext[19];
	u_int8_t state;
#define SMART_LOG_STATE_UNK	0x0
#define SMART_LOG_STATE_SLEEP	0x1
#define SMART_LOG_STATE_ACTIDL	0x2
#define SMART_LOG_STATE_OFFSELF	0x3
	u_int8_t time1;
	u_int8_t time2;
};

/* error log data structure */
struct smart_log_errdata {
	struct smart_log_cmd cmd[5];
	struct smart_log_err err;
};

/* summary error log sector */
struct smart_log_sum {
	u_int8_t  version;
	u_int8_t  index;
	struct    smart_log_errdata errdata[5];
	u_int16_t err_cnt;
	u_int8_t  res[57];
	u_int8_t  cksum;
};

/* comprehensive error log sector */
struct smart_log_comp {
	u_int8_t  version;
	u_int8_t  index;
	struct    smart_log_errdata errdata[5];
	u_int16_t err_cnt;
	u_int8_t  res[57];
	u_int8_t  cksum;
};

/* self-test descriptor entry */
struct smart_log_self_desc {
	u_int8_t  reg_lbalo;
	u_int8_t  selfstat;
	u_int8_t  time1;
	u_int8_t  time2;
	u_int8_t  chkpnt;
	u_int8_t  lbafail1;
	u_int8_t  lbafail2;
	u_int8_t  lbafail3;
	u_int8_t  lbafail4;
	u_int8_t  vendor[15];
};

/* self-test log sector */
struct smart_log_self {
	u_int16_t rev;
	struct    smart_log_self_desc desc[21];
	u_int8_t  vendor[2];
	u_int8_t  index;
	u_int8_t  res[2];
	u_int8_t  cksum;
};

#define SMART_SELFSTAT_PCNT(s) ((s & 0x0f) * 10)
#define SMART_SELFSTAT_STAT(s) (s >> 4)

#define SMART_OFFLINE_COLLECT	0
#define SMART_OFFLINE_SHORTOFF	1
#define SMART_OFFLINE_EXTENOFF	2
#define SMART_OFFLINE_ABORT	127
#define SMART_OFFLINE_SHORTCAP	129
#define SMART_OFFLINE_EXTENCAP	130

#define SMART_AUTOSAVE_DS	0x00
#define SMART_AUTOSAVE_EN	0xf1

#define SMART_READLOG_DIR	0x00
#define SMART_READLOG_SUM	0x01
#define SMART_READLOG_COMP	0x02
#define SMART_READLOG_ECOMP	0x03
#define SMART_READLOG_SELF	0x06
#define SMART_READLOG_ESELF	0x07

#define SMART_LOG_MSECT		0x01
