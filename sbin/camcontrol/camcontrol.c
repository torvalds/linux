/*
 * Copyright (c) 1997-2007 Kenneth D. Merry
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/endian.h>
#include <sys/sbuf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <fcntl.h>
#include <ctype.h>
#include <err.h>
#include <libutil.h>
#ifndef MINIMALISTIC
#include <limits.h>
#include <inttypes.h>
#endif

#include <cam/cam.h>
#include <cam/cam_debug.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/scsi/scsi_pass.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/smp_all.h>
#include <cam/ata/ata_all.h>
#include <cam/mmc/mmc_all.h>
#include <camlib.h>
#include "camcontrol.h"
#ifdef WITH_NVME
#include "nvmecontrol_ext.h"
#endif

typedef enum {
	CAM_CMD_NONE		= 0x00000000,
	CAM_CMD_DEVLIST		= 0x00000001,
	CAM_CMD_TUR		= 0x00000002,
	CAM_CMD_INQUIRY		= 0x00000003,
	CAM_CMD_STARTSTOP	= 0x00000004,
	CAM_CMD_RESCAN		= 0x00000005,
	CAM_CMD_READ_DEFECTS	= 0x00000006,
	CAM_CMD_MODE_PAGE	= 0x00000007,
	CAM_CMD_SCSI_CMD	= 0x00000008,
	CAM_CMD_DEVTREE		= 0x00000009,
	CAM_CMD_USAGE		= 0x0000000a,
	CAM_CMD_DEBUG		= 0x0000000b,
	CAM_CMD_RESET		= 0x0000000c,
	CAM_CMD_FORMAT		= 0x0000000d,
	CAM_CMD_TAG		= 0x0000000e,
	CAM_CMD_RATE		= 0x0000000f,
	CAM_CMD_DETACH		= 0x00000010,
	CAM_CMD_REPORTLUNS	= 0x00000011,
	CAM_CMD_READCAP		= 0x00000012,
	CAM_CMD_IDENTIFY	= 0x00000013,
	CAM_CMD_IDLE		= 0x00000014,
	CAM_CMD_STANDBY		= 0x00000015,
	CAM_CMD_SLEEP		= 0x00000016,
	CAM_CMD_SMP_CMD		= 0x00000017,
	CAM_CMD_SMP_RG		= 0x00000018,
	CAM_CMD_SMP_PC		= 0x00000019,
	CAM_CMD_SMP_PHYLIST	= 0x0000001a,
	CAM_CMD_SMP_MANINFO	= 0x0000001b,
	CAM_CMD_DOWNLOAD_FW	= 0x0000001c,
	CAM_CMD_SECURITY	= 0x0000001d,
	CAM_CMD_HPA		= 0x0000001e,
	CAM_CMD_SANITIZE	= 0x0000001f,
	CAM_CMD_PERSIST		= 0x00000020,
	CAM_CMD_APM		= 0x00000021,
	CAM_CMD_AAM		= 0x00000022,
	CAM_CMD_ATTRIB		= 0x00000023,
	CAM_CMD_OPCODES		= 0x00000024,
	CAM_CMD_REPROBE		= 0x00000025,
	CAM_CMD_ZONE		= 0x00000026,
	CAM_CMD_EPC		= 0x00000027,
	CAM_CMD_TIMESTAMP	= 0x00000028,
	CAM_CMD_MMCSD_CMD	= 0x00000029
} cam_cmdmask;

typedef enum {
	CAM_ARG_NONE		= 0x00000000,
	CAM_ARG_VERBOSE		= 0x00000001,
	CAM_ARG_DEVICE		= 0x00000002,
	CAM_ARG_BUS		= 0x00000004,
	CAM_ARG_TARGET		= 0x00000008,
	CAM_ARG_LUN		= 0x00000010,
	CAM_ARG_EJECT		= 0x00000020,
	CAM_ARG_UNIT		= 0x00000040,
	CAM_ARG_FORMAT_BLOCK	= 0x00000080,
	CAM_ARG_FORMAT_BFI	= 0x00000100,
	CAM_ARG_FORMAT_PHYS	= 0x00000200,
	CAM_ARG_PLIST		= 0x00000400,
	CAM_ARG_GLIST		= 0x00000800,
	CAM_ARG_GET_SERIAL	= 0x00001000,
	CAM_ARG_GET_STDINQ	= 0x00002000,
	CAM_ARG_GET_XFERRATE	= 0x00004000,
	CAM_ARG_INQ_MASK	= 0x00007000,
	CAM_ARG_TIMEOUT		= 0x00020000,
	CAM_ARG_CMD_IN		= 0x00040000,
	CAM_ARG_CMD_OUT		= 0x00080000,
	CAM_ARG_ERR_RECOVER	= 0x00200000,
	CAM_ARG_RETRIES		= 0x00400000,
	CAM_ARG_START_UNIT	= 0x00800000,
	CAM_ARG_DEBUG_INFO	= 0x01000000,
	CAM_ARG_DEBUG_TRACE	= 0x02000000,
	CAM_ARG_DEBUG_SUBTRACE	= 0x04000000,
	CAM_ARG_DEBUG_CDB	= 0x08000000,
	CAM_ARG_DEBUG_XPT	= 0x10000000,
	CAM_ARG_DEBUG_PERIPH	= 0x20000000,
	CAM_ARG_DEBUG_PROBE	= 0x40000000,
} cam_argmask;

struct camcontrol_opts {
	const char	*optname;
	uint32_t	cmdnum;
	cam_argmask	argnum;
	const char	*subopt;
};

#ifndef MINIMALISTIC
struct ata_res_pass16 {
	u_int16_t reserved[5];
	u_int8_t flags;
	u_int8_t error;
	u_int8_t sector_count_exp;
	u_int8_t sector_count;
	u_int8_t lba_low_exp;
	u_int8_t lba_low;
	u_int8_t lba_mid_exp;
	u_int8_t lba_mid;
	u_int8_t lba_high_exp;
	u_int8_t lba_high;
	u_int8_t device;
	u_int8_t status;
};

struct ata_set_max_pwd
{
	u_int16_t reserved1;
	u_int8_t password[32];
	u_int16_t reserved2[239];
};

static struct scsi_nv task_attrs[] = {
	{ "simple", MSG_SIMPLE_Q_TAG },
	{ "head", MSG_HEAD_OF_Q_TAG },
	{ "ordered", MSG_ORDERED_Q_TAG },
	{ "iwr", MSG_IGN_WIDE_RESIDUE },
	{ "aca", MSG_ACA_TASK }
};

static const char scsicmd_opts[] = "a:c:dfi:o:r";
static const char readdefect_opts[] = "f:GPqsS:X";
static const char negotiate_opts[] = "acD:M:O:qR:T:UW:";
static const char smprg_opts[] = "l";
static const char smppc_opts[] = "a:A:d:lm:M:o:p:s:S:T:";
static const char smpphylist_opts[] = "lq";
static char pwd_opt;
#endif

static struct camcontrol_opts option_table[] = {
#ifndef MINIMALISTIC
	{"tur", CAM_CMD_TUR, CAM_ARG_NONE, NULL},
	{"inquiry", CAM_CMD_INQUIRY, CAM_ARG_NONE, "DSR"},
	{"identify", CAM_CMD_IDENTIFY, CAM_ARG_NONE, NULL},
	{"start", CAM_CMD_STARTSTOP, CAM_ARG_START_UNIT, NULL},
	{"stop", CAM_CMD_STARTSTOP, CAM_ARG_NONE, NULL},
	{"load", CAM_CMD_STARTSTOP, CAM_ARG_START_UNIT | CAM_ARG_EJECT, NULL},
	{"eject", CAM_CMD_STARTSTOP, CAM_ARG_EJECT, NULL},
	{"reportluns", CAM_CMD_REPORTLUNS, CAM_ARG_NONE, "clr:"},
	{"readcapacity", CAM_CMD_READCAP, CAM_ARG_NONE, "bhHlNqs"},
	{"reprobe", CAM_CMD_REPROBE, CAM_ARG_NONE, NULL},
#endif /* MINIMALISTIC */
	{"rescan", CAM_CMD_RESCAN, CAM_ARG_NONE, NULL},
	{"reset", CAM_CMD_RESET, CAM_ARG_NONE, NULL},
#ifndef MINIMALISTIC
	{"cmd", CAM_CMD_SCSI_CMD, CAM_ARG_NONE, scsicmd_opts},
	{"mmcsdcmd", CAM_CMD_MMCSD_CMD, CAM_ARG_NONE, "c:a:f:Wb:l:41S:I"},
	{"command", CAM_CMD_SCSI_CMD, CAM_ARG_NONE, scsicmd_opts},
	{"smpcmd", CAM_CMD_SMP_CMD, CAM_ARG_NONE, "r:R:"},
	{"smprg", CAM_CMD_SMP_RG, CAM_ARG_NONE, smprg_opts},
	{"smpreportgeneral", CAM_CMD_SMP_RG, CAM_ARG_NONE, smprg_opts},
	{"smppc", CAM_CMD_SMP_PC, CAM_ARG_NONE, smppc_opts},
	{"smpphycontrol", CAM_CMD_SMP_PC, CAM_ARG_NONE, smppc_opts},
	{"smpplist", CAM_CMD_SMP_PHYLIST, CAM_ARG_NONE, smpphylist_opts},
	{"smpphylist", CAM_CMD_SMP_PHYLIST, CAM_ARG_NONE, smpphylist_opts},
	{"smpmaninfo", CAM_CMD_SMP_MANINFO, CAM_ARG_NONE, "l"},
	{"defects", CAM_CMD_READ_DEFECTS, CAM_ARG_NONE, readdefect_opts},
	{"defectlist", CAM_CMD_READ_DEFECTS, CAM_ARG_NONE, readdefect_opts},
#endif /* MINIMALISTIC */
	{"devlist", CAM_CMD_DEVTREE, CAM_ARG_NONE, "-b"},
#ifndef MINIMALISTIC
	{"periphlist", CAM_CMD_DEVLIST, CAM_ARG_NONE, NULL},
	{"modepage", CAM_CMD_MODE_PAGE, CAM_ARG_NONE, "bdelm:P:"},
	{"tags", CAM_CMD_TAG, CAM_ARG_NONE, "N:q"},
	{"negotiate", CAM_CMD_RATE, CAM_ARG_NONE, negotiate_opts},
	{"rate", CAM_CMD_RATE, CAM_ARG_NONE, negotiate_opts},
	{"debug", CAM_CMD_DEBUG, CAM_ARG_NONE, "IPTSXcp"},
	{"format", CAM_CMD_FORMAT, CAM_ARG_NONE, "qrwy"},
	{"sanitize", CAM_CMD_SANITIZE, CAM_ARG_NONE, "a:c:IP:qrUwy"},
	{"idle", CAM_CMD_IDLE, CAM_ARG_NONE, "t:"},
	{"standby", CAM_CMD_STANDBY, CAM_ARG_NONE, "t:"},
	{"sleep", CAM_CMD_SLEEP, CAM_ARG_NONE, ""},
	{"apm", CAM_CMD_APM, CAM_ARG_NONE, "l:"},
	{"aam", CAM_CMD_AAM, CAM_ARG_NONE, "l:"},
	{"fwdownload", CAM_CMD_DOWNLOAD_FW, CAM_ARG_NONE, "f:qsy"},
	{"security", CAM_CMD_SECURITY, CAM_ARG_NONE, "d:e:fh:k:l:qs:T:U:y"},
	{"hpa", CAM_CMD_HPA, CAM_ARG_NONE, "Pflp:qs:U:y"},
	{"persist", CAM_CMD_PERSIST, CAM_ARG_NONE, "ai:I:k:K:o:ps:ST:U"},
	{"attrib", CAM_CMD_ATTRIB, CAM_ARG_NONE, "a:ce:F:p:r:s:T:w:V:"},
	{"opcodes", CAM_CMD_OPCODES, CAM_ARG_NONE, "No:s:T"},
	{"zone", CAM_CMD_ZONE, CAM_ARG_NONE, "ac:l:No:P:"},
	{"epc", CAM_CMD_EPC, CAM_ARG_NONE, "c:dDeHp:Pr:sS:T:"},
	{"timestamp", CAM_CMD_TIMESTAMP, CAM_ARG_NONE, "f:mrsUT:"},
#endif /* MINIMALISTIC */
	{"help", CAM_CMD_USAGE, CAM_ARG_NONE, NULL},
	{"-?", CAM_CMD_USAGE, CAM_ARG_NONE, NULL},
	{"-h", CAM_CMD_USAGE, CAM_ARG_NONE, NULL},
	{NULL, 0, 0, NULL}
};

struct cam_devitem {
	struct device_match_result dev_match;
	int num_periphs;
	struct periph_match_result *periph_matches;
	struct scsi_vpd_device_id *device_id;
	int device_id_len;
	STAILQ_ENTRY(cam_devitem) links;
};

struct cam_devlist {
	STAILQ_HEAD(, cam_devitem) dev_queue;
	path_id_t path_id;
};

static cam_cmdmask cmdlist;
static cam_argmask arglist;

camcontrol_optret getoption(struct camcontrol_opts *table, char *arg,
			    uint32_t *cmdnum, cam_argmask *argnum,
			    const char **subopt);
#ifndef MINIMALISTIC
static int getdevlist(struct cam_device *device);
#endif /* MINIMALISTIC */
static int getdevtree(int argc, char **argv, char *combinedopt);
static int print_dev_scsi(struct device_match_result *dev_result, char *tmpstr);
static int print_dev_ata(struct device_match_result *dev_result, char *tmpstr);
static int print_dev_semb(struct device_match_result *dev_result, char *tmpstr);
static int print_dev_mmcsd(struct device_match_result *dev_result,
    char *tmpstr);
#ifdef WITH_NVME
static int print_dev_nvme(struct device_match_result *dev_result, char *tmpstr);
#endif
#ifndef MINIMALISTIC
static int testunitready(struct cam_device *device, int task_attr,
			 int retry_count, int timeout, int quiet);
static int scsistart(struct cam_device *device, int startstop, int loadeject,
		     int task_attr, int retry_count, int timeout);
static int scsiinquiry(struct cam_device *device, int task_attr,
		       int retry_count, int timeout);
static int scsiserial(struct cam_device *device, int task_attr,
		      int retry_count, int timeout);
#endif /* MINIMALISTIC */
static int parse_btl(char *tstr, path_id_t *bus, target_id_t *target,
		     lun_id_t *lun, cam_argmask *arglst);
static int dorescan_or_reset(int argc, char **argv, int rescan);
static int rescan_or_reset_bus(path_id_t bus, int rescan);
static int scanlun_or_reset_dev(path_id_t bus, target_id_t target,
    lun_id_t lun, int scan);
#ifndef MINIMALISTIC
static int readdefects(struct cam_device *device, int argc, char **argv,
		       char *combinedopt, int task_attr, int retry_count,
		       int timeout);
static void modepage(struct cam_device *device, int argc, char **argv,
		     char *combinedopt, int task_attr, int retry_count,
		     int timeout);
static int scsicmd(struct cam_device *device, int argc, char **argv,
		   char *combinedopt, int task_attr, int retry_count,
		   int timeout);
static int smpcmd(struct cam_device *device, int argc, char **argv,
		  char *combinedopt, int retry_count, int timeout);
static int mmcsdcmd(struct cam_device *device, int argc, char **argv,
		  char *combinedopt, int retry_count, int timeout);
static int smpreportgeneral(struct cam_device *device, int argc, char **argv,
			    char *combinedopt, int retry_count, int timeout);
static int smpphycontrol(struct cam_device *device, int argc, char **argv,
			 char *combinedopt, int retry_count, int timeout);
static int smpmaninfo(struct cam_device *device, int argc, char **argv,
		      char *combinedopt, int retry_count, int timeout);
static int getdevid(struct cam_devitem *item);
static int buildbusdevlist(struct cam_devlist *devlist);
static void freebusdevlist(struct cam_devlist *devlist);
static struct cam_devitem *findsasdevice(struct cam_devlist *devlist,
					 uint64_t sasaddr);
static int smpphylist(struct cam_device *device, int argc, char **argv,
		      char *combinedopt, int retry_count, int timeout);
static int tagcontrol(struct cam_device *device, int argc, char **argv,
		      char *combinedopt);
static void cts_print(struct cam_device *device,
		      struct ccb_trans_settings *cts);
static void cpi_print(struct ccb_pathinq *cpi);
static int get_cpi(struct cam_device *device, struct ccb_pathinq *cpi);
static int get_cgd(struct cam_device *device, struct ccb_getdev *cgd);
static int get_print_cts(struct cam_device *device, int user_settings,
			 int quiet, struct ccb_trans_settings *cts);
static int ratecontrol(struct cam_device *device, int task_attr,
		       int retry_count, int timeout, int argc, char **argv,
		       char *combinedopt);
static int scsiformat(struct cam_device *device, int argc, char **argv,
		      char *combinedopt, int task_attr, int retry_count,
		      int timeout);
static int scsisanitize(struct cam_device *device, int argc, char **argv,
			char *combinedopt, int task_attr, int retry_count,
			int timeout);
static int scsireportluns(struct cam_device *device, int argc, char **argv,
			  char *combinedopt, int task_attr, int retry_count,
			  int timeout);
static int scsireadcapacity(struct cam_device *device, int argc, char **argv,
			    char *combinedopt, int task_attr, int retry_count,
			    int timeout);
static int atapm(struct cam_device *device, int argc, char **argv,
		 char *combinedopt, int retry_count, int timeout);
static int atasecurity(struct cam_device *device, int retry_count, int timeout,
		       int argc, char **argv, char *combinedopt);
static int atahpa(struct cam_device *device, int retry_count, int timeout,
		  int argc, char **argv, char *combinedopt);
static int scsiprintoneopcode(struct cam_device *device, int req_opcode,
			      int sa_set, int req_sa, uint8_t *buf,
			      uint32_t valid_len);
static int scsiprintopcodes(struct cam_device *device, int td_req, uint8_t *buf,
			    uint32_t valid_len);
static int scsiopcodes(struct cam_device *device, int argc, char **argv,
		       char *combinedopt, int task_attr, int retry_count,
		       int timeout, int verbose);
static int scsireprobe(struct cam_device *device);

#endif /* MINIMALISTIC */
#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

camcontrol_optret
getoption(struct camcontrol_opts *table, char *arg, uint32_t *cmdnum,
	  cam_argmask *argnum, const char **subopt)
{
	struct camcontrol_opts *opts;
	int num_matches = 0;

	for (opts = table; (opts != NULL) && (opts->optname != NULL);
	     opts++) {
		if (strncmp(opts->optname, arg, strlen(arg)) == 0) {
			*cmdnum = opts->cmdnum;
			*argnum = opts->argnum;
			*subopt = opts->subopt;
			if (++num_matches > 1)
				return (CC_OR_AMBIGUOUS);
		}
	}

	if (num_matches > 0)
		return (CC_OR_FOUND);
	else
		return (CC_OR_NOT_FOUND);
}

#ifndef MINIMALISTIC
static int
getdevlist(struct cam_device *device)
{
	union ccb *ccb;
	char status[32];
	int error = 0;

	ccb = cam_getccb(device);

	ccb->ccb_h.func_code = XPT_GDEVLIST;
	ccb->ccb_h.flags = CAM_DIR_NONE;
	ccb->ccb_h.retry_count = 1;
	ccb->cgdl.index = 0;
	ccb->cgdl.status = CAM_GDEVLIST_MORE_DEVS;
	while (ccb->cgdl.status == CAM_GDEVLIST_MORE_DEVS) {
		if (cam_send_ccb(device, ccb) < 0) {
			perror("error getting device list");
			cam_freeccb(ccb);
			return (1);
		}

		status[0] = '\0';

		switch (ccb->cgdl.status) {
			case CAM_GDEVLIST_MORE_DEVS:
				strcpy(status, "MORE");
				break;
			case CAM_GDEVLIST_LAST_DEVICE:
				strcpy(status, "LAST");
				break;
			case CAM_GDEVLIST_LIST_CHANGED:
				strcpy(status, "CHANGED");
				break;
			case CAM_GDEVLIST_ERROR:
				strcpy(status, "ERROR");
				error = 1;
				break;
		}

		fprintf(stdout, "%s%d:  generation: %d index: %d status: %s\n",
			ccb->cgdl.periph_name,
			ccb->cgdl.unit_number,
			ccb->cgdl.generation,
			ccb->cgdl.index,
			status);

		/*
		 * If the list has changed, we need to start over from the
		 * beginning.
		 */
		if (ccb->cgdl.status == CAM_GDEVLIST_LIST_CHANGED)
			ccb->cgdl.index = 0;
	}

	cam_freeccb(ccb);

	return (error);
}
#endif /* MINIMALISTIC */

static int
getdevtree(int argc, char **argv, char *combinedopt)
{
	union ccb ccb;
	int bufsize, fd;
	unsigned int i;
	int need_close = 0;
	int error = 0;
	int skip_device = 0;
	int busonly = 0;
	int c;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c) {
		case 'b':
			if ((arglist & CAM_ARG_VERBOSE) == 0)
				busonly = 1;
			break;
		default:
			break;
		}
	}

	if ((fd = open(XPT_DEVICE, O_RDWR)) == -1) {
		warn("couldn't open %s", XPT_DEVICE);
		return (1);
	}

	bzero(&ccb, sizeof(union ccb));

	ccb.ccb_h.path_id = CAM_XPT_PATH_ID;
	ccb.ccb_h.target_id = CAM_TARGET_WILDCARD;
	ccb.ccb_h.target_lun = CAM_LUN_WILDCARD;

	ccb.ccb_h.func_code = XPT_DEV_MATCH;
	bufsize = sizeof(struct dev_match_result) * 100;
	ccb.cdm.match_buf_len = bufsize;
	ccb.cdm.matches = (struct dev_match_result *)malloc(bufsize);
	if (ccb.cdm.matches == NULL) {
		warnx("can't malloc memory for matches");
		close(fd);
		return (1);
	}
	ccb.cdm.num_matches = 0;

	/*
	 * We fetch all nodes, since we display most of them in the default
	 * case, and all in the verbose case.
	 */
	ccb.cdm.num_patterns = 0;
	ccb.cdm.pattern_buf_len = 0;

	/*
	 * We do the ioctl multiple times if necessary, in case there are
	 * more than 100 nodes in the EDT.
	 */
	do {
		if (ioctl(fd, CAMIOCOMMAND, &ccb) == -1) {
			warn("error sending CAMIOCOMMAND ioctl");
			error = 1;
			break;
		}

		if ((ccb.ccb_h.status != CAM_REQ_CMP)
		 || ((ccb.cdm.status != CAM_DEV_MATCH_LAST)
		    && (ccb.cdm.status != CAM_DEV_MATCH_MORE))) {
			warnx("got CAM error %#x, CDM error %d\n",
			      ccb.ccb_h.status, ccb.cdm.status);
			error = 1;
			break;
		}

		for (i = 0; i < ccb.cdm.num_matches; i++) {
			switch (ccb.cdm.matches[i].type) {
			case DEV_MATCH_BUS: {
				struct bus_match_result *bus_result;

				/*
				 * Only print the bus information if the
				 * user turns on the verbose flag.
				 */
				if ((busonly == 0) &&
				    (arglist & CAM_ARG_VERBOSE) == 0)
					break;

				bus_result =
					&ccb.cdm.matches[i].result.bus_result;

				if (need_close) {
					fprintf(stdout, ")\n");
					need_close = 0;
				}

				fprintf(stdout, "scbus%d on %s%d bus %d%s\n",
					bus_result->path_id,
					bus_result->dev_name,
					bus_result->unit_number,
					bus_result->bus_id,
					(busonly ? "" : ":"));
				break;
			}
			case DEV_MATCH_DEVICE: {
				struct device_match_result *dev_result;
				char tmpstr[256];

				if (busonly == 1)
					break;

				dev_result =
				     &ccb.cdm.matches[i].result.device_result;

				if ((dev_result->flags
				     & DEV_RESULT_UNCONFIGURED)
				 && ((arglist & CAM_ARG_VERBOSE) == 0)) {
					skip_device = 1;
					break;
				} else
					skip_device = 0;

				if (dev_result->protocol == PROTO_SCSI) {
					if (print_dev_scsi(dev_result,
					    &tmpstr[0]) != 0) {
						skip_device = 1;
						break;
					}
				} else if (dev_result->protocol == PROTO_ATA ||
				    dev_result->protocol == PROTO_SATAPM) {
					if (print_dev_ata(dev_result,
					    &tmpstr[0]) != 0) {
						skip_device = 1;
						break;
					}
				} else if (dev_result->protocol == PROTO_MMCSD){
					if (print_dev_mmcsd(dev_result,
					    &tmpstr[0]) != 0) {
						skip_device = 1;
						break;
					}
				} else if (dev_result->protocol == PROTO_SEMB) {
					if (print_dev_semb(dev_result,
					    &tmpstr[0]) != 0) {
						skip_device = 1;
						break;
					}
#ifdef WITH_NVME
				} else if (dev_result->protocol == PROTO_NVME) {
					if (print_dev_nvme(dev_result,
					    &tmpstr[0]) != 0) {
						skip_device = 1;
						break;
					}
#endif
				} else {
				    sprintf(tmpstr, "<>");
				}
				if (need_close) {
					fprintf(stdout, ")\n");
					need_close = 0;
				}

				fprintf(stdout, "%-33s  at scbus%d "
					"target %d lun %jx (",
					tmpstr,
					dev_result->path_id,
					dev_result->target_id,
					(uintmax_t)dev_result->target_lun);

				need_close = 1;

				break;
			}
			case DEV_MATCH_PERIPH: {
				struct periph_match_result *periph_result;

				periph_result =
				      &ccb.cdm.matches[i].result.periph_result;

				if (busonly || skip_device != 0)
					break;

				if (need_close > 1)
					fprintf(stdout, ",");

				fprintf(stdout, "%s%d",
					periph_result->periph_name,
					periph_result->unit_number);

				need_close++;
				break;
			}
			default:
				fprintf(stdout, "unknown match type\n");
				break;
			}
		}

	} while ((ccb.ccb_h.status == CAM_REQ_CMP)
		&& (ccb.cdm.status == CAM_DEV_MATCH_MORE));

	if (need_close)
		fprintf(stdout, ")\n");

	close(fd);

	return (error);
}

static int
print_dev_scsi(struct device_match_result *dev_result, char *tmpstr)
{
	char vendor[16], product[48], revision[16];

	cam_strvis(vendor, dev_result->inq_data.vendor,
	    sizeof(dev_result->inq_data.vendor), sizeof(vendor));
	cam_strvis(product, dev_result->inq_data.product,
	    sizeof(dev_result->inq_data.product), sizeof(product));
	cam_strvis(revision, dev_result->inq_data.revision,
	    sizeof(dev_result->inq_data.revision), sizeof(revision));
	sprintf(tmpstr, "<%s %s %s>", vendor, product, revision);

	return (0);
}

static int
print_dev_ata(struct device_match_result *dev_result, char *tmpstr)
{
	char product[48], revision[16];

	cam_strvis(product, dev_result->ident_data.model,
	    sizeof(dev_result->ident_data.model), sizeof(product));
	cam_strvis(revision, dev_result->ident_data.revision,
	    sizeof(dev_result->ident_data.revision), sizeof(revision));
	sprintf(tmpstr, "<%s %s>", product, revision);

	return (0);
}

static int
print_dev_semb(struct device_match_result *dev_result, char *tmpstr)
{
	struct sep_identify_data *sid;
	char vendor[16], product[48], revision[16], fw[5];

	sid = (struct sep_identify_data *)&dev_result->ident_data;
	cam_strvis(vendor, sid->vendor_id,
	    sizeof(sid->vendor_id), sizeof(vendor));
	cam_strvis(product, sid->product_id,
	    sizeof(sid->product_id), sizeof(product));
	cam_strvis(revision, sid->product_rev,
	    sizeof(sid->product_rev), sizeof(revision));
	cam_strvis(fw, sid->firmware_rev,
	    sizeof(sid->firmware_rev), sizeof(fw));
	sprintf(tmpstr, "<%s %s %s %s>", vendor, product, revision, fw);

	return (0);
}

static int
print_dev_mmcsd(struct device_match_result *dev_result, char *tmpstr)
{
	union ccb *ccb;
	struct ccb_dev_advinfo *advi;
	struct cam_device *dev;
	struct mmc_params mmc_ident_data;

	dev = cam_open_btl(dev_result->path_id, dev_result->target_id,
	    dev_result->target_lun, O_RDWR, NULL);
	if (dev == NULL) {
		warnx("%s", cam_errbuf);
		return (1);
	}

	ccb = cam_getccb(dev);
	if (ccb == NULL) {
		warnx("couldn't allocate CCB");
		cam_close_device(dev);
		return (1);
	}

	advi = &ccb->cdai;
	advi->ccb_h.flags = CAM_DIR_IN;
	advi->ccb_h.func_code = XPT_DEV_ADVINFO;
	advi->flags = CDAI_FLAG_NONE;
	advi->buftype = CDAI_TYPE_MMC_PARAMS;
	advi->bufsiz = sizeof(struct mmc_params);
	advi->buf = (uint8_t *)&mmc_ident_data;

	if (cam_send_ccb(dev, ccb) < 0) {
		warn("error sending CAMIOCOMMAND ioctl");
		cam_freeccb(ccb);
		cam_close_device(dev);
		return (1);
	}

	if (strlen(mmc_ident_data.model) > 0) {
		sprintf(tmpstr, "<%s>", mmc_ident_data.model);
	} else {
		sprintf(tmpstr, "<%s card>",
		    mmc_ident_data.card_features &
		    CARD_FEATURE_SDIO ? "SDIO" : "unknown");
	}

	cam_freeccb(ccb);
	cam_close_device(dev);
	return (0);
}

#ifdef WITH_NVME
static int
nvme_get_cdata(struct cam_device *dev, struct nvme_controller_data *cdata)
{
	union ccb *ccb;
	struct ccb_dev_advinfo *advi;

	ccb = cam_getccb(dev);
	if (ccb == NULL) {
		warnx("couldn't allocate CCB");
		cam_close_device(dev);
		return (1);
	}

	advi = &ccb->cdai;
	advi->ccb_h.flags = CAM_DIR_IN;
	advi->ccb_h.func_code = XPT_DEV_ADVINFO;
	advi->flags = CDAI_FLAG_NONE;
	advi->buftype = CDAI_TYPE_NVME_CNTRL;
	advi->bufsiz = sizeof(struct nvme_controller_data);
	advi->buf = (uint8_t *)cdata;

	if (cam_send_ccb(dev, ccb) < 0) {
		warn("error sending CAMIOCOMMAND ioctl");
		cam_freeccb(ccb);
		cam_close_device(dev);
		return(1);
	}
	if (advi->ccb_h.status != CAM_REQ_CMP) {
		warnx("got CAM error %#x", advi->ccb_h.status);
		cam_freeccb(ccb);
		cam_close_device(dev);
		return(1);
	}
	cam_freeccb(ccb);
	return 0;
}

static int
print_dev_nvme(struct device_match_result *dev_result, char *tmpstr)
{
	struct cam_device *dev;
	struct nvme_controller_data cdata;
	char vendor[64], product[64];

	dev = cam_open_btl(dev_result->path_id, dev_result->target_id,
	    dev_result->target_lun, O_RDWR, NULL);
	if (dev == NULL) {
		warnx("%s", cam_errbuf);
		return (1);
	}

	if (nvme_get_cdata(dev, &cdata))
		return (1);

	cam_strvis(vendor, cdata.mn, sizeof(cdata.mn), sizeof(vendor));
	cam_strvis(product, cdata.fr, sizeof(cdata.fr), sizeof(product));
	sprintf(tmpstr, "<%s %s>", vendor, product);

	cam_close_device(dev);
	return (0);
}
#endif

#ifndef MINIMALISTIC
static int
testunitready(struct cam_device *device, int task_attr, int retry_count,
	      int timeout, int quiet)
{
	int error = 0;
	union ccb *ccb;

	ccb = cam_getccb(device);

	scsi_test_unit_ready(&ccb->csio,
			     /* retries */ retry_count,
			     /* cbfcnp */ NULL,
			     /* tag_action */ task_attr,
			     /* sense_len */ SSD_FULL_SIZE,
			     /* timeout */ timeout ? timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		if (quiet == 0)
			perror("error sending test unit ready");

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		cam_freeccb(ccb);
		return (1);
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		if (quiet == 0)
			fprintf(stdout, "Unit is ready\n");
	} else {
		if (quiet == 0)
			fprintf(stdout, "Unit is not ready\n");
		error = 1;

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
	}

	cam_freeccb(ccb);

	return (error);
}

static int
scsistart(struct cam_device *device, int startstop, int loadeject,
	  int task_attr, int retry_count, int timeout)
{
	union ccb *ccb;
	int error = 0;

	ccb = cam_getccb(device);

	/*
	 * If we're stopping, send an ordered tag so the drive in question
	 * will finish any previously queued writes before stopping.  If
	 * the device isn't capable of tagged queueing, or if tagged
	 * queueing is turned off, the tag action is a no-op.  We override
	 * the default simple tag, although this also has the effect of
	 * overriding the user's wishes if he wanted to specify a simple
	 * tag.
	 */
	if ((startstop == 0)
	 && (task_attr == MSG_SIMPLE_Q_TAG))
		task_attr = MSG_ORDERED_Q_TAG;

	scsi_start_stop(&ccb->csio,
			/* retries */ retry_count,
			/* cbfcnp */ NULL,
			/* tag_action */ task_attr,
			/* start/stop */ startstop,
			/* load_eject */ loadeject,
			/* immediate */ 0,
			/* sense_len */ SSD_FULL_SIZE,
			/* timeout */ timeout ? timeout : 120000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		perror("error sending start unit");

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		cam_freeccb(ccb);
		return (1);
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
		if (startstop) {
			fprintf(stdout, "Unit started successfully");
			if (loadeject)
				fprintf(stdout,", Media loaded\n");
			else
				fprintf(stdout,"\n");
		} else {
			fprintf(stdout, "Unit stopped successfully");
			if (loadeject)
				fprintf(stdout, ", Media ejected\n");
			else
				fprintf(stdout, "\n");
		}
	else {
		error = 1;
		if (startstop)
			fprintf(stdout,
				"Error received from start unit command\n");
		else
			fprintf(stdout,
				"Error received from stop unit command\n");

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
	}

	cam_freeccb(ccb);

	return (error);
}

int
scsidoinquiry(struct cam_device *device, int argc, char **argv,
	      char *combinedopt, int task_attr, int retry_count, int timeout)
{
	int c;
	int error = 0;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c) {
		case 'D':
			arglist |= CAM_ARG_GET_STDINQ;
			break;
		case 'R':
			arglist |= CAM_ARG_GET_XFERRATE;
			break;
		case 'S':
			arglist |= CAM_ARG_GET_SERIAL;
			break;
		default:
			break;
		}
	}

	/*
	 * If the user didn't specify any inquiry options, he wants all of
	 * them.
	 */
	if ((arglist & CAM_ARG_INQ_MASK) == 0)
		arglist |= CAM_ARG_INQ_MASK;

	if (arglist & CAM_ARG_GET_STDINQ)
		error = scsiinquiry(device, task_attr, retry_count, timeout);

	if (error != 0)
		return (error);

	if (arglist & CAM_ARG_GET_SERIAL)
		scsiserial(device, task_attr, retry_count, timeout);

	if (arglist & CAM_ARG_GET_XFERRATE)
		error = camxferrate(device);

	return (error);
}

static int
scsiinquiry(struct cam_device *device, int task_attr, int retry_count,
	    int timeout)
{
	union ccb *ccb;
	struct scsi_inquiry_data *inq_buf;
	int error = 0;

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("couldn't allocate CCB");
		return (1);
	}

	/* cam_getccb cleans up the header, caller has to zero the payload */
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	inq_buf = (struct scsi_inquiry_data *)malloc(
		sizeof(struct scsi_inquiry_data));

	if (inq_buf == NULL) {
		cam_freeccb(ccb);
		warnx("can't malloc memory for inquiry\n");
		return (1);
	}
	bzero(inq_buf, sizeof(*inq_buf));

	/*
	 * Note that although the size of the inquiry buffer is the full
	 * 256 bytes specified in the SCSI spec, we only tell the device
	 * that we have allocated SHORT_INQUIRY_LENGTH bytes.  There are
	 * two reasons for this:
	 *
	 *  - The SCSI spec says that when a length field is only 1 byte,
	 *    a value of 0 will be interpreted as 256.  Therefore
	 *    scsi_inquiry() will convert an inq_len (which is passed in as
	 *    a u_int32_t, but the field in the CDB is only 1 byte) of 256
	 *    to 0.  Evidently, very few devices meet the spec in that
	 *    regard.  Some devices, like many Seagate disks, take the 0 as
	 *    0, and don't return any data.  One Pioneer DVD-R drive
	 *    returns more data than the command asked for.
	 *
	 *    So, since there are numerous devices that just don't work
	 *    right with the full inquiry size, we don't send the full size.
	 *
	 *  - The second reason not to use the full inquiry data length is
	 *    that we don't need it here.  The only reason we issue a
	 *    standard inquiry is to get the vendor name, device name,
	 *    and revision so scsi_print_inquiry() can print them.
	 *
	 * If, at some point in the future, more inquiry data is needed for
	 * some reason, this code should use a procedure similar to the
	 * probe code.  i.e., issue a short inquiry, and determine from
	 * the additional length passed back from the device how much
	 * inquiry data the device supports.  Once the amount the device
	 * supports is determined, issue an inquiry for that amount and no
	 * more.
	 *
	 * KDM, 2/18/2000
	 */
	scsi_inquiry(&ccb->csio,
		     /* retries */ retry_count,
		     /* cbfcnp */ NULL,
		     /* tag_action */ task_attr,
		     /* inq_buf */ (u_int8_t *)inq_buf,
		     /* inq_len */ SHORT_INQUIRY_LENGTH,
		     /* evpd */ 0,
		     /* page_code */ 0,
		     /* sense_len */ SSD_FULL_SIZE,
		     /* timeout */ timeout ? timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		perror("error sending SCSI inquiry");

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		cam_freeccb(ccb);
		return (1);
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		error = 1;

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
	}

	cam_freeccb(ccb);

	if (error != 0) {
		free(inq_buf);
		return (error);
	}

	fprintf(stdout, "%s%d: ", device->device_name,
		device->dev_unit_num);
	scsi_print_inquiry(inq_buf);

	free(inq_buf);

	return (0);
}

static int
scsiserial(struct cam_device *device, int task_attr, int retry_count,
	   int timeout)
{
	union ccb *ccb;
	struct scsi_vpd_unit_serial_number *serial_buf;
	char serial_num[SVPD_SERIAL_NUM_SIZE + 1];
	int error = 0;

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("couldn't allocate CCB");
		return (1);
	}

	/* cam_getccb cleans up the header, caller has to zero the payload */
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	serial_buf = (struct scsi_vpd_unit_serial_number *)
		malloc(sizeof(*serial_buf));

	if (serial_buf == NULL) {
		cam_freeccb(ccb);
		warnx("can't malloc memory for serial number");
		return (1);
	}

	scsi_inquiry(&ccb->csio,
		     /*retries*/ retry_count,
		     /*cbfcnp*/ NULL,
		     /* tag_action */ task_attr,
		     /* inq_buf */ (u_int8_t *)serial_buf,
		     /* inq_len */ sizeof(*serial_buf),
		     /* evpd */ 1,
		     /* page_code */ SVPD_UNIT_SERIAL_NUMBER,
		     /* sense_len */ SSD_FULL_SIZE,
		     /* timeout */ timeout ? timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		warn("error getting serial number");

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		cam_freeccb(ccb);
		free(serial_buf);
		return (1);
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		error = 1;

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
	}

	cam_freeccb(ccb);

	if (error != 0) {
		free(serial_buf);
		return (error);
	}

	bcopy(serial_buf->serial_num, serial_num, serial_buf->length);
	serial_num[serial_buf->length] = '\0';

	if ((arglist & CAM_ARG_GET_STDINQ)
	 || (arglist & CAM_ARG_GET_XFERRATE))
		fprintf(stdout, "%s%d: Serial Number ",
			device->device_name, device->dev_unit_num);

	fprintf(stdout, "%.60s\n", serial_num);

	free(serial_buf);

	return (0);
}

int
camxferrate(struct cam_device *device)
{
	struct ccb_pathinq cpi;
	u_int32_t freq = 0;
	u_int32_t speed = 0;
	union ccb *ccb;
	u_int mb;
	int retval = 0;

	if ((retval = get_cpi(device, &cpi)) != 0)
		return (1);

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("couldn't allocate CCB");
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cts);

	ccb->ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
	ccb->cts.type = CTS_TYPE_CURRENT_SETTINGS;

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		const char error_string[] = "error getting transfer settings";

		if (retval < 0)
			warn(error_string);
		else
			warnx(error_string);

		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);

		retval = 1;

		goto xferrate_bailout;

	}

	speed = cpi.base_transfer_speed;
	freq = 0;
	if (ccb->cts.transport == XPORT_SPI) {
		struct ccb_trans_settings_spi *spi =
		    &ccb->cts.xport_specific.spi;

		if ((spi->valid & CTS_SPI_VALID_SYNC_RATE) != 0) {
			freq = scsi_calc_syncsrate(spi->sync_period);
			speed = freq;
		}
		if ((spi->valid & CTS_SPI_VALID_BUS_WIDTH) != 0) {
			speed *= (0x01 << spi->bus_width);
		}
	} else if (ccb->cts.transport == XPORT_FC) {
		struct ccb_trans_settings_fc *fc =
		    &ccb->cts.xport_specific.fc;

		if (fc->valid & CTS_FC_VALID_SPEED)
			speed = fc->bitrate;
	} else if (ccb->cts.transport == XPORT_SAS) {
		struct ccb_trans_settings_sas *sas =
		    &ccb->cts.xport_specific.sas;

		if (sas->valid & CTS_SAS_VALID_SPEED)
			speed = sas->bitrate;
	} else if (ccb->cts.transport == XPORT_ATA) {
		struct ccb_trans_settings_pata *pata =
		    &ccb->cts.xport_specific.ata;

		if (pata->valid & CTS_ATA_VALID_MODE)
			speed = ata_mode2speed(pata->mode);
	} else if (ccb->cts.transport == XPORT_SATA) {
		struct	ccb_trans_settings_sata *sata =
		    &ccb->cts.xport_specific.sata;

		if (sata->valid & CTS_SATA_VALID_REVISION)
			speed = ata_revision2speed(sata->revision);
	}

	mb = speed / 1000;
	if (mb > 0) {
		fprintf(stdout, "%s%d: %d.%03dMB/s transfers",
			device->device_name, device->dev_unit_num,
			mb, speed % 1000);
	} else {
		fprintf(stdout, "%s%d: %dKB/s transfers",
			device->device_name, device->dev_unit_num,
			speed);
	}

	if (ccb->cts.transport == XPORT_SPI) {
		struct ccb_trans_settings_spi *spi =
		    &ccb->cts.xport_specific.spi;

		if (((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) != 0)
		 && (spi->sync_offset != 0))
			fprintf(stdout, " (%d.%03dMHz, offset %d", freq / 1000,
				freq % 1000, spi->sync_offset);

		if (((spi->valid & CTS_SPI_VALID_BUS_WIDTH) != 0)
		 && (spi->bus_width > 0)) {
			if (((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) != 0)
			 && (spi->sync_offset != 0)) {
				fprintf(stdout, ", ");
			} else {
				fprintf(stdout, " (");
			}
			fprintf(stdout, "%dbit)", 8 * (0x01 << spi->bus_width));
		} else if (((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) != 0)
		 && (spi->sync_offset != 0)) {
			fprintf(stdout, ")");
		}
	} else if (ccb->cts.transport == XPORT_ATA) {
		struct ccb_trans_settings_pata *pata =
		    &ccb->cts.xport_specific.ata;

		printf(" (");
		if (pata->valid & CTS_ATA_VALID_MODE)
			printf("%s, ", ata_mode2string(pata->mode));
		if ((pata->valid & CTS_ATA_VALID_ATAPI) && pata->atapi != 0)
			printf("ATAPI %dbytes, ", pata->atapi);
		if (pata->valid & CTS_ATA_VALID_BYTECOUNT)
			printf("PIO %dbytes", pata->bytecount);
		printf(")");
	} else if (ccb->cts.transport == XPORT_SATA) {
		struct ccb_trans_settings_sata *sata =
		    &ccb->cts.xport_specific.sata;

		printf(" (");
		if (sata->valid & CTS_SATA_VALID_REVISION)
			printf("SATA %d.x, ", sata->revision);
		else
			printf("SATA, ");
		if (sata->valid & CTS_SATA_VALID_MODE)
			printf("%s, ", ata_mode2string(sata->mode));
		if ((sata->valid & CTS_SATA_VALID_ATAPI) && sata->atapi != 0)
			printf("ATAPI %dbytes, ", sata->atapi);
		if (sata->valid & CTS_SATA_VALID_BYTECOUNT)
			printf("PIO %dbytes", sata->bytecount);
		printf(")");
	}

	if (ccb->cts.protocol == PROTO_SCSI) {
		struct ccb_trans_settings_scsi *scsi =
		    &ccb->cts.proto_specific.scsi;
		if (scsi->valid & CTS_SCSI_VALID_TQ) {
			if (scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) {
				fprintf(stdout, ", Command Queueing Enabled");
			}
		}
	}

	fprintf(stdout, "\n");

xferrate_bailout:

	cam_freeccb(ccb);

	return (retval);
}

static void
atahpa_print(struct ata_params *parm, u_int64_t hpasize, int header)
{
	u_int32_t lbasize = (u_int32_t)parm->lba_size_1 |
				((u_int32_t)parm->lba_size_2 << 16);

	u_int64_t lbasize48 = ((u_int64_t)parm->lba_size48_1) |
				((u_int64_t)parm->lba_size48_2 << 16) |
				((u_int64_t)parm->lba_size48_3 << 32) |
				((u_int64_t)parm->lba_size48_4 << 48);

	if (header) {
		printf("\nFeature                      "
		       "Support  Enabled   Value\n");
	}

	printf("Host Protected Area (HPA)      ");
	if (parm->support.command1 & ATA_SUPPORT_PROTECTED) {
		u_int64_t lba = lbasize48 ? lbasize48 : lbasize;
		printf("yes      %s     %ju/%ju\n", (hpasize > lba) ? "yes" : "no ",
			lba, hpasize);

		printf("HPA - Security                 ");
		if (parm->support.command1 & ATA_SUPPORT_MAXSECURITY)
			printf("yes\n");
		else
			printf("no\n");
	} else {
		printf("no\n");
	}
}

static int
atasata(struct ata_params *parm)
{


	if (parm->satacapabilities != 0xffff &&
	    parm->satacapabilities != 0x0000)
		return 1;

	return 0;
}

static void
atacapprint(struct ata_params *parm)
{
	u_int32_t lbasize = (u_int32_t)parm->lba_size_1 |
				((u_int32_t)parm->lba_size_2 << 16);

	u_int64_t lbasize48 = ((u_int64_t)parm->lba_size48_1) |
				((u_int64_t)parm->lba_size48_2 << 16) |
				((u_int64_t)parm->lba_size48_3 << 32) |
				((u_int64_t)parm->lba_size48_4 << 48);

	printf("\n");
	printf("protocol              ");
	printf("ATA/ATAPI-%d", ata_version(parm->version_major));
	if (parm->satacapabilities && parm->satacapabilities != 0xffff) {
		if (parm->satacapabilities & ATA_SATA_GEN3)
			printf(" SATA 3.x\n");
		else if (parm->satacapabilities & ATA_SATA_GEN2)
			printf(" SATA 2.x\n");
		else if (parm->satacapabilities & ATA_SATA_GEN1)
			printf(" SATA 1.x\n");
		else
			printf(" SATA\n");
	}
	else
		printf("\n");
	printf("device model          %.40s\n", parm->model);
	printf("firmware revision     %.8s\n", parm->revision);
	printf("serial number         %.20s\n", parm->serial);
	if (parm->enabled.extension & ATA_SUPPORT_64BITWWN) {
		printf("WWN                   %04x%04x%04x%04x\n",
		    parm->wwn[0], parm->wwn[1], parm->wwn[2], parm->wwn[3]);
	}
	if (parm->enabled.extension & ATA_SUPPORT_MEDIASN) {
		printf("media serial number   %.30s\n",
		    parm->media_serial);
	}

	printf("cylinders             %d\n", parm->cylinders);
	printf("heads                 %d\n", parm->heads);
	printf("sectors/track         %d\n", parm->sectors);
	printf("sector size           logical %u, physical %lu, offset %lu\n",
	    ata_logical_sector_size(parm),
	    (unsigned long)ata_physical_sector_size(parm),
	    (unsigned long)ata_logical_sector_offset(parm));

	if (parm->config == ATA_PROTO_CFA ||
	    (parm->support.command2 & ATA_SUPPORT_CFA))
		printf("CFA supported\n");

	printf("LBA%ssupported         ",
		parm->capabilities1 & ATA_SUPPORT_LBA ? " " : " not ");
	if (lbasize)
		printf("%d sectors\n", lbasize);
	else
		printf("\n");

	printf("LBA48%ssupported       ",
		parm->support.command2 & ATA_SUPPORT_ADDRESS48 ? " " : " not ");
	if (lbasize48)
		printf("%ju sectors\n", (uintmax_t)lbasize48);
	else
		printf("\n");

	printf("PIO supported         PIO");
	switch (ata_max_pmode(parm)) {
	case ATA_PIO4:
		printf("4");
		break;
	case ATA_PIO3:
		printf("3");
		break;
	case ATA_PIO2:
		printf("2");
		break;
	case ATA_PIO1:
		printf("1");
		break;
	default:
		printf("0");
	}
	if ((parm->capabilities1 & ATA_SUPPORT_IORDY) == 0)
		printf(" w/o IORDY");
	printf("\n");

	printf("DMA%ssupported         ",
		parm->capabilities1 & ATA_SUPPORT_DMA ? " " : " not ");
	if (parm->capabilities1 & ATA_SUPPORT_DMA) {
		if (parm->mwdmamodes & 0xff) {
			printf("WDMA");
			if (parm->mwdmamodes & 0x04)
				printf("2");
			else if (parm->mwdmamodes & 0x02)
				printf("1");
			else if (parm->mwdmamodes & 0x01)
				printf("0");
			printf(" ");
		}
		if ((parm->atavalid & ATA_FLAG_88) &&
		    (parm->udmamodes & 0xff)) {
			printf("UDMA");
			if (parm->udmamodes & 0x40)
				printf("6");
			else if (parm->udmamodes & 0x20)
				printf("5");
			else if (parm->udmamodes & 0x10)
				printf("4");
			else if (parm->udmamodes & 0x08)
				printf("3");
			else if (parm->udmamodes & 0x04)
				printf("2");
			else if (parm->udmamodes & 0x02)
				printf("1");
			else if (parm->udmamodes & 0x01)
				printf("0");
			printf(" ");
		}
	}
	printf("\n");

	if (parm->media_rotation_rate == 1) {
		printf("media RPM             non-rotating\n");
	} else if (parm->media_rotation_rate >= 0x0401 &&
	    parm->media_rotation_rate <= 0xFFFE) {
		printf("media RPM             %d\n",
			parm->media_rotation_rate);
	}

	printf("Zoned-Device Commands ");
	switch (parm->support3 & ATA_SUPPORT_ZONE_MASK) {
		case ATA_SUPPORT_ZONE_DEV_MANAGED:
			printf("device managed\n");
			break;
		case ATA_SUPPORT_ZONE_HOST_AWARE:
			printf("host aware\n");
			break;
		default:
			printf("no\n");
	}

	printf("\nFeature                      "
		"Support  Enabled   Value           Vendor\n");
	printf("read ahead                     %s	%s\n",
		parm->support.command1 & ATA_SUPPORT_LOOKAHEAD ? "yes" : "no",
		parm->enabled.command1 & ATA_SUPPORT_LOOKAHEAD ? "yes" : "no");
	printf("write cache                    %s	%s\n",
		parm->support.command1 & ATA_SUPPORT_WRITECACHE ? "yes" : "no",
		parm->enabled.command1 & ATA_SUPPORT_WRITECACHE ? "yes" : "no");
	printf("flush cache                    %s	%s\n",
		parm->support.command2 & ATA_SUPPORT_FLUSHCACHE ? "yes" : "no",
		parm->enabled.command2 & ATA_SUPPORT_FLUSHCACHE ? "yes" : "no");
	printf("overlap                        %s\n",
		parm->capabilities1 & ATA_SUPPORT_OVERLAP ? "yes" : "no");
	printf("Tagged Command Queuing (TCQ)   %s	%s",
		parm->support.command2 & ATA_SUPPORT_QUEUED ? "yes" : "no",
		parm->enabled.command2 & ATA_SUPPORT_QUEUED ? "yes" : "no");
		if (parm->support.command2 & ATA_SUPPORT_QUEUED) {
			printf("	%d tags\n",
			    ATA_QUEUE_LEN(parm->queue) + 1);
		} else
			printf("\n");
	printf("Native Command Queuing (NCQ)   ");
	if (parm->satacapabilities != 0xffff &&
	    (parm->satacapabilities & ATA_SUPPORT_NCQ)) {
		printf("yes		%d tags\n",
		    ATA_QUEUE_LEN(parm->queue) + 1);
	} else
		printf("no\n");

	printf("NCQ Queue Management           %s\n", atasata(parm) &&
		parm->satacapabilities2 & ATA_SUPPORT_NCQ_QMANAGEMENT ?
		"yes" : "no");
	printf("NCQ Streaming                  %s\n", atasata(parm) &&
		parm->satacapabilities2 & ATA_SUPPORT_NCQ_STREAM ?
		"yes" : "no");
	printf("Receive & Send FPDMA Queued    %s\n", atasata(parm) &&
		parm->satacapabilities2 & ATA_SUPPORT_RCVSND_FPDMA_QUEUED ?
		"yes" : "no");

	printf("SMART                          %s	%s\n",
		parm->support.command1 & ATA_SUPPORT_SMART ? "yes" : "no",
		parm->enabled.command1 & ATA_SUPPORT_SMART ? "yes" : "no");
	printf("microcode download             %s	%s\n",
		parm->support.command2 & ATA_SUPPORT_MICROCODE ? "yes" : "no",
		parm->enabled.command2 & ATA_SUPPORT_MICROCODE ? "yes" : "no");
	printf("security                       %s	%s\n",
		parm->support.command1 & ATA_SUPPORT_SECURITY ? "yes" : "no",
		parm->enabled.command1 & ATA_SUPPORT_SECURITY ? "yes" : "no");
	printf("power management               %s	%s\n",
		parm->support.command1 & ATA_SUPPORT_POWERMGT ? "yes" : "no",
		parm->enabled.command1 & ATA_SUPPORT_POWERMGT ? "yes" : "no");
	printf("advanced power management      %s	%s",
		parm->support.command2 & ATA_SUPPORT_APM ? "yes" : "no",
		parm->enabled.command2 & ATA_SUPPORT_APM ? "yes" : "no");
		if (parm->support.command2 & ATA_SUPPORT_APM) {
			printf("	%d/0x%02X\n",
			    parm->apm_value & 0xff, parm->apm_value & 0xff);
		} else
			printf("\n");
	printf("automatic acoustic management  %s	%s",
		parm->support.command2 & ATA_SUPPORT_AUTOACOUSTIC ? "yes" :"no",
		parm->enabled.command2 & ATA_SUPPORT_AUTOACOUSTIC ? "yes" :"no");
		if (parm->support.command2 & ATA_SUPPORT_AUTOACOUSTIC) {
			printf("	%d/0x%02X	%d/0x%02X\n",
			    ATA_ACOUSTIC_CURRENT(parm->acoustic),
			    ATA_ACOUSTIC_CURRENT(parm->acoustic),
			    ATA_ACOUSTIC_VENDOR(parm->acoustic),
			    ATA_ACOUSTIC_VENDOR(parm->acoustic));
		} else
			printf("\n");
	printf("media status notification      %s	%s\n",
		parm->support.command2 & ATA_SUPPORT_NOTIFY ? "yes" : "no",
		parm->enabled.command2 & ATA_SUPPORT_NOTIFY ? "yes" : "no");
	printf("power-up in Standby            %s	%s\n",
		parm->support.command2 & ATA_SUPPORT_STANDBY ? "yes" : "no",
		parm->enabled.command2 & ATA_SUPPORT_STANDBY ? "yes" : "no");
	printf("write-read-verify              %s	%s",
		parm->support2 & ATA_SUPPORT_WRITEREADVERIFY ? "yes" : "no",
		parm->enabled2 & ATA_SUPPORT_WRITEREADVERIFY ? "yes" : "no");
		if (parm->support2 & ATA_SUPPORT_WRITEREADVERIFY) {
			printf("	%d/0x%x\n",
			    parm->wrv_mode, parm->wrv_mode);
		} else
			printf("\n");
	printf("unload                         %s	%s\n",
		parm->support.extension & ATA_SUPPORT_UNLOAD ? "yes" : "no",
		parm->enabled.extension & ATA_SUPPORT_UNLOAD ? "yes" : "no");
	printf("general purpose logging        %s	%s\n",
		parm->support.extension & ATA_SUPPORT_GENLOG ? "yes" : "no",
		parm->enabled.extension & ATA_SUPPORT_GENLOG ? "yes" : "no");
	printf("free-fall                      %s	%s\n",
		parm->support2 & ATA_SUPPORT_FREEFALL ? "yes" : "no",
		parm->enabled2 & ATA_SUPPORT_FREEFALL ? "yes" : "no");
	printf("Data Set Management (DSM/TRIM) ");
	if (parm->support_dsm & ATA_SUPPORT_DSM_TRIM) {
		printf("yes\n");
		printf("DSM - max 512byte blocks       ");
		if (parm->max_dsm_blocks == 0x00)
			printf("yes              not specified\n");
		else
			printf("yes              %d\n",
				parm->max_dsm_blocks);

		printf("DSM - deterministic read       ");
		if (parm->support3 & ATA_SUPPORT_DRAT) {
			if (parm->support3 & ATA_SUPPORT_RZAT)
				printf("yes              zeroed\n");
			else
				printf("yes              any value\n");
		} else {
			printf("no\n");
		}
	} else {
		printf("no\n");
	}
}

static int
scsi_cam_pass_16_send(struct cam_device *device, union ccb *ccb, int quiet)
{
	struct ata_pass_16 *ata_pass_16;
	struct ata_cmd ata_cmd;

	ata_pass_16 = (struct ata_pass_16 *)ccb->csio.cdb_io.cdb_bytes;
	ata_cmd.command = ata_pass_16->command;
	ata_cmd.control = ata_pass_16->control;
	ata_cmd.features = ata_pass_16->features;

	if (arglist & CAM_ARG_VERBOSE) {
		warnx("sending ATA %s via pass_16 with timeout of %u msecs",
		      ata_op_string(&ata_cmd),
		      ccb->csio.ccb_h.timeout);
	}

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		if (quiet != 1 || arglist & CAM_ARG_VERBOSE) {
			warn("error sending ATA %s via pass_16",
			     ata_op_string(&ata_cmd));
		}

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		return (1);
	}

	if (!(ata_pass_16->flags & AP_FLAG_CHK_COND) &&
	    (ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if (quiet != 1 || arglist & CAM_ARG_VERBOSE) {
			warnx("ATA %s via pass_16 failed",
			      ata_op_string(&ata_cmd));
		}
		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		return (1);
	}

	return (0);
}


static int
ata_cam_send(struct cam_device *device, union ccb *ccb, int quiet)
{
	if (arglist & CAM_ARG_VERBOSE) {
		warnx("sending ATA %s with timeout of %u msecs",
		      ata_op_string(&(ccb->ataio.cmd)),
		      ccb->ataio.ccb_h.timeout);
	}

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		if (quiet != 1 || arglist & CAM_ARG_VERBOSE) {
			warn("error sending ATA %s",
			     ata_op_string(&(ccb->ataio.cmd)));
		}

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		return (1);
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if (quiet != 1 || arglist & CAM_ARG_VERBOSE) {
			warnx("ATA %s failed: %d",
			      ata_op_string(&(ccb->ataio.cmd)), quiet);
		}

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		return (1);
	}

	return (0);
}

static int
ata_do_pass_16(struct cam_device *device, union ccb *ccb, int retries,
	       u_int32_t flags, u_int8_t protocol, u_int8_t ata_flags,
	       u_int8_t tag_action, u_int8_t command, u_int8_t features,
	       u_int64_t lba, u_int8_t sector_count, u_int8_t *data_ptr,
	       u_int16_t dxfer_len, int timeout, int quiet)
{
	if (data_ptr != NULL) {
		ata_flags |= AP_FLAG_BYT_BLOK_BYTES |
			    AP_FLAG_TLEN_SECT_CNT;
		if (flags & CAM_DIR_OUT)
			ata_flags |= AP_FLAG_TDIR_TO_DEV;
		else
			ata_flags |= AP_FLAG_TDIR_FROM_DEV;
	} else {
		ata_flags |= AP_FLAG_TLEN_NO_DATA;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	scsi_ata_pass_16(&ccb->csio,
			 retries,
			 NULL,
			 flags,
			 tag_action,
			 protocol,
			 ata_flags,
			 features,
			 sector_count,
			 lba,
			 command,
			 /*control*/0,
			 data_ptr,
			 dxfer_len,
			 /*sense_len*/SSD_FULL_SIZE,
			 timeout);

	return scsi_cam_pass_16_send(device, ccb, quiet);
}

static int
ata_try_pass_16(struct cam_device *device)
{
	struct ccb_pathinq cpi;

	if (get_cpi(device, &cpi) != 0) {
		warnx("couldn't get CPI");
		return (-1);
	}

	if (cpi.protocol == PROTO_SCSI) {
		/* possibly compatible with pass_16 */
		return (1);
	}

	/* likely not compatible with pass_16 */
	return (0);
}

static int
ata_do_28bit_cmd(struct cam_device *device, union ccb *ccb, int retries,
		 u_int32_t flags, u_int8_t protocol, u_int8_t tag_action,
		 u_int8_t command, u_int8_t features, u_int32_t lba,
		 u_int8_t sector_count, u_int8_t *data_ptr, u_int16_t dxfer_len,
		 int timeout, int quiet)
{


	switch (ata_try_pass_16(device)) {
	case -1:
		return (1);
	case 1:
		/* Try using SCSI Passthrough */
		return ata_do_pass_16(device, ccb, retries, flags, protocol,
				      0, tag_action, command, features, lba,
				      sector_count, data_ptr, dxfer_len,
				      timeout, quiet);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->ataio);
	cam_fill_ataio(&ccb->ataio,
		       retries,
		       NULL,
		       flags,
		       tag_action,
		       data_ptr,
		       dxfer_len,
		       timeout);

	ata_28bit_cmd(&ccb->ataio, command, features, lba, sector_count);
	return ata_cam_send(device, ccb, quiet);
}

static int
ata_do_cmd(struct cam_device *device, union ccb *ccb, int retries,
	   u_int32_t flags, u_int8_t protocol, u_int8_t ata_flags,
	   u_int8_t tag_action, u_int8_t command, u_int8_t features,
	   u_int64_t lba, u_int8_t sector_count, u_int8_t *data_ptr,
	   u_int16_t dxfer_len, int timeout, int force48bit)
{
	int retval;

	retval = ata_try_pass_16(device);
	if (retval == -1)
		return (1);

	if (retval == 1) {
		int error;

		/* Try using SCSI Passthrough */
		error = ata_do_pass_16(device, ccb, retries, flags, protocol,
				      ata_flags, tag_action, command, features,
				      lba, sector_count, data_ptr, dxfer_len,
				      timeout, 0);

		if (ata_flags & AP_FLAG_CHK_COND) {
			/* Decode ata_res from sense data */
			struct ata_res_pass16 *res_pass16;
			struct ata_res *res;
			u_int i;
			u_int16_t *ptr;

			/* sense_data is 4 byte aligned */
			ptr = (uint16_t*)(uintptr_t)&ccb->csio.sense_data;
			for (i = 0; i < sizeof(*res_pass16) / 2; i++)
				ptr[i] = le16toh(ptr[i]);

			/* sense_data is 4 byte aligned */
			res_pass16 = (struct ata_res_pass16 *)(uintptr_t)
			    &ccb->csio.sense_data;
			res = &ccb->ataio.res;
			res->flags = res_pass16->flags;
			res->status = res_pass16->status;
			res->error = res_pass16->error;
			res->lba_low = res_pass16->lba_low;
			res->lba_mid = res_pass16->lba_mid;
			res->lba_high = res_pass16->lba_high;
			res->device = res_pass16->device;
			res->lba_low_exp = res_pass16->lba_low_exp;
			res->lba_mid_exp = res_pass16->lba_mid_exp;
			res->lba_high_exp = res_pass16->lba_high_exp;
			res->sector_count = res_pass16->sector_count;
			res->sector_count_exp = res_pass16->sector_count_exp;
		}

		return (error);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->ataio);
	cam_fill_ataio(&ccb->ataio,
		       retries,
		       NULL,
		       flags,
		       tag_action,
		       data_ptr,
		       dxfer_len,
		       timeout);

	if (force48bit || lba > ATA_MAX_28BIT_LBA)
		ata_48bit_cmd(&ccb->ataio, command, features, lba, sector_count);
	else
		ata_28bit_cmd(&ccb->ataio, command, features, lba, sector_count);

	if (ata_flags & AP_FLAG_CHK_COND)
		ccb->ataio.cmd.flags |= CAM_ATAIO_NEEDRESULT;

	return ata_cam_send(device, ccb, 0);
}

static void
dump_data(uint16_t *ptr, uint32_t len)
{
	u_int i;

	for (i = 0; i < len / 2; i++) {
		if ((i % 8) == 0)
			printf(" %3d: ", i);
		printf("%04hx ", ptr[i]);
		if ((i % 8) == 7)
			printf("\n");
	}
	if ((i % 8) != 7)
		printf("\n");
}

static int
atahpa_proc_resp(struct cam_device *device, union ccb *ccb,
		 int is48bit, u_int64_t *hpasize)
{
	struct ata_res *res;

	res = &ccb->ataio.res;
	if (res->status & ATA_STATUS_ERROR) {
		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
			printf("error = 0x%02x, sector_count = 0x%04x, "
			       "device = 0x%02x, status = 0x%02x\n",
			       res->error, res->sector_count,
			       res->device, res->status);
		}

		if (res->error & ATA_ERROR_ID_NOT_FOUND) {
			warnx("Max address has already been set since "
			      "last power-on or hardware reset");
		}

		return (1);
	}

	if (arglist & CAM_ARG_VERBOSE) {
		fprintf(stdout, "%s%d: Raw native max data:\n",
			device->device_name, device->dev_unit_num);
		/* res is 4 byte aligned */
		dump_data((uint16_t*)(uintptr_t)res, sizeof(struct ata_res));

		printf("error = 0x%02x, sector_count = 0x%04x, device = 0x%02x, "
		       "status = 0x%02x\n", res->error, res->sector_count,
		       res->device, res->status);
	}

	if (hpasize != NULL) {
		if (is48bit) {
			*hpasize = (((u_int64_t)((res->lba_high_exp << 16) |
			    (res->lba_mid_exp << 8) | res->lba_low_exp) << 24) |
			    ((res->lba_high << 16) | (res->lba_mid << 8) |
			    res->lba_low)) + 1;
		} else {
			*hpasize = (((res->device & 0x0f) << 24) |
			    (res->lba_high << 16) | (res->lba_mid << 8) |
			    res->lba_low) + 1;
		}
	}

	return (0);
}

static int
ata_read_native_max(struct cam_device *device, int retry_count,
		      u_int32_t timeout, union ccb *ccb,
		      struct ata_params *parm, u_int64_t *hpasize)
{
	int error;
	u_int cmd, is48bit;
	u_int8_t protocol;

	is48bit = parm->support.command2 & ATA_SUPPORT_ADDRESS48;
	protocol = AP_PROTO_NON_DATA;

	if (is48bit) {
		cmd = ATA_READ_NATIVE_MAX_ADDRESS48;
		protocol |= AP_EXTEND;
	} else {
		cmd = ATA_READ_NATIVE_MAX_ADDRESS;
	}

	error = ata_do_cmd(device,
			   ccb,
			   retry_count,
			   /*flags*/CAM_DIR_NONE,
			   /*protocol*/protocol,
			   /*ata_flags*/AP_FLAG_CHK_COND,
			   /*tag_action*/MSG_SIMPLE_Q_TAG,
			   /*command*/cmd,
			   /*features*/0,
			   /*lba*/0,
			   /*sector_count*/0,
			   /*data_ptr*/NULL,
			   /*dxfer_len*/0,
			   timeout ? timeout : 1000,
			   is48bit);

	if (error)
		return (error);

	return atahpa_proc_resp(device, ccb, is48bit, hpasize);
}

static int
atahpa_set_max(struct cam_device *device, int retry_count,
	      u_int32_t timeout, union ccb *ccb,
	      int is48bit, u_int64_t maxsize, int persist)
{
	int error;
	u_int cmd;
	u_int8_t protocol;

	protocol = AP_PROTO_NON_DATA;

	if (is48bit) {
		cmd = ATA_SET_MAX_ADDRESS48;
		protocol |= AP_EXTEND;
	} else {
		cmd = ATA_SET_MAX_ADDRESS;
	}

	/* lba's are zero indexed so the max lba is requested max - 1 */
	if (maxsize)
		maxsize--;

	error = ata_do_cmd(device,
			   ccb,
			   retry_count,
			   /*flags*/CAM_DIR_NONE,
			   /*protocol*/protocol,
			   /*ata_flags*/AP_FLAG_CHK_COND,
			   /*tag_action*/MSG_SIMPLE_Q_TAG,
			   /*command*/cmd,
			   /*features*/ATA_HPA_FEAT_MAX_ADDR,
			   /*lba*/maxsize,
			   /*sector_count*/persist,
			   /*data_ptr*/NULL,
			   /*dxfer_len*/0,
			   timeout ? timeout : 1000,
			   is48bit);

	if (error)
		return (error);

	return atahpa_proc_resp(device, ccb, is48bit, NULL);
}

static int
atahpa_password(struct cam_device *device, int retry_count,
		u_int32_t timeout, union ccb *ccb,
		int is48bit, struct ata_set_max_pwd *pwd)
{
	int error;
	u_int cmd;
	u_int8_t protocol;

	protocol = AP_PROTO_PIO_OUT;
	cmd = (is48bit) ? ATA_SET_MAX_ADDRESS48 : ATA_SET_MAX_ADDRESS;

	error = ata_do_cmd(device,
			   ccb,
			   retry_count,
			   /*flags*/CAM_DIR_OUT,
			   /*protocol*/protocol,
			   /*ata_flags*/AP_FLAG_CHK_COND,
			   /*tag_action*/MSG_SIMPLE_Q_TAG,
			   /*command*/cmd,
			   /*features*/ATA_HPA_FEAT_SET_PWD,
			   /*lba*/0,
			   /*sector_count*/0,
			   /*data_ptr*/(u_int8_t*)pwd,
			   /*dxfer_len*/sizeof(struct ata_set_max_pwd),
			   timeout ? timeout : 1000,
			   is48bit);

	if (error)
		return (error);

	return atahpa_proc_resp(device, ccb, is48bit, NULL);
}

static int
atahpa_lock(struct cam_device *device, int retry_count,
	    u_int32_t timeout, union ccb *ccb, int is48bit)
{
	int error;
	u_int cmd;
	u_int8_t protocol;

	protocol = AP_PROTO_NON_DATA;
	cmd = (is48bit) ? ATA_SET_MAX_ADDRESS48 : ATA_SET_MAX_ADDRESS;

	error = ata_do_cmd(device,
			   ccb,
			   retry_count,
			   /*flags*/CAM_DIR_NONE,
			   /*protocol*/protocol,
			   /*ata_flags*/AP_FLAG_CHK_COND,
			   /*tag_action*/MSG_SIMPLE_Q_TAG,
			   /*command*/cmd,
			   /*features*/ATA_HPA_FEAT_LOCK,
			   /*lba*/0,
			   /*sector_count*/0,
			   /*data_ptr*/NULL,
			   /*dxfer_len*/0,
			   timeout ? timeout : 1000,
			   is48bit);

	if (error)
		return (error);

	return atahpa_proc_resp(device, ccb, is48bit, NULL);
}

static int
atahpa_unlock(struct cam_device *device, int retry_count,
	      u_int32_t timeout, union ccb *ccb,
	      int is48bit, struct ata_set_max_pwd *pwd)
{
	int error;
	u_int cmd;
	u_int8_t protocol;

	protocol = AP_PROTO_PIO_OUT;
	cmd = (is48bit) ? ATA_SET_MAX_ADDRESS48 : ATA_SET_MAX_ADDRESS;

	error = ata_do_cmd(device,
			   ccb,
			   retry_count,
			   /*flags*/CAM_DIR_OUT,
			   /*protocol*/protocol,
			   /*ata_flags*/AP_FLAG_CHK_COND,
			   /*tag_action*/MSG_SIMPLE_Q_TAG,
			   /*command*/cmd,
			   /*features*/ATA_HPA_FEAT_UNLOCK,
			   /*lba*/0,
			   /*sector_count*/0,
			   /*data_ptr*/(u_int8_t*)pwd,
			   /*dxfer_len*/sizeof(struct ata_set_max_pwd),
			   timeout ? timeout : 1000,
			   is48bit);

	if (error)
		return (error);

	return atahpa_proc_resp(device, ccb, is48bit, NULL);
}

static int
atahpa_freeze_lock(struct cam_device *device, int retry_count,
		   u_int32_t timeout, union ccb *ccb, int is48bit)
{
	int error;
	u_int cmd;
	u_int8_t protocol;

	protocol = AP_PROTO_NON_DATA;
	cmd = (is48bit) ? ATA_SET_MAX_ADDRESS48 : ATA_SET_MAX_ADDRESS;

	error = ata_do_cmd(device,
			   ccb,
			   retry_count,
			   /*flags*/CAM_DIR_NONE,
			   /*protocol*/protocol,
			   /*ata_flags*/AP_FLAG_CHK_COND,
			   /*tag_action*/MSG_SIMPLE_Q_TAG,
			   /*command*/cmd,
			   /*features*/ATA_HPA_FEAT_FREEZE,
			   /*lba*/0,
			   /*sector_count*/0,
			   /*data_ptr*/NULL,
			   /*dxfer_len*/0,
			   timeout ? timeout : 1000,
			   is48bit);

	if (error)
		return (error);

	return atahpa_proc_resp(device, ccb, is48bit, NULL);
}


int
ata_do_identify(struct cam_device *device, int retry_count, int timeout,
		union ccb *ccb, struct ata_params** ident_bufp)
{
	struct ata_params *ident_buf;
	struct ccb_pathinq cpi;
	struct ccb_getdev cgd;
	u_int i, error;
	int16_t *ptr;
	u_int8_t command, retry_command;

	if (get_cpi(device, &cpi) != 0) {
		warnx("couldn't get CPI");
		return (-1);
	}

	/* Neither PROTO_ATAPI or PROTO_SATAPM are used in cpi.protocol */
	if (cpi.protocol == PROTO_ATA) {
		if (get_cgd(device, &cgd) != 0) {
			warnx("couldn't get CGD");
			return (-1);
		}

		command = (cgd.protocol == PROTO_ATA) ?
		    ATA_ATA_IDENTIFY : ATA_ATAPI_IDENTIFY;
		retry_command = 0;
	} else {
		/* We don't know which for sure so try both */
		command = ATA_ATA_IDENTIFY;
		retry_command = ATA_ATAPI_IDENTIFY;
	}

	ptr = (uint16_t *)calloc(1, sizeof(struct ata_params));
	if (ptr == NULL) {
		warnx("can't calloc memory for identify\n");
		return (1);
	}

	error = ata_do_28bit_cmd(device,
				 ccb,
				 /*retries*/retry_count,
				 /*flags*/CAM_DIR_IN,
				 /*protocol*/AP_PROTO_PIO_IN,
				 /*tag_action*/MSG_SIMPLE_Q_TAG,
				 /*command*/command,
				 /*features*/0,
				 /*lba*/0,
				 /*sector_count*/0,
				 /*data_ptr*/(u_int8_t *)ptr,
				 /*dxfer_len*/sizeof(struct ata_params),
				 /*timeout*/timeout ? timeout : 30 * 1000,
				 /*quiet*/1);

	if (error != 0) {
		if (retry_command == 0) {
			free(ptr);
			return (1);
		}
		error = ata_do_28bit_cmd(device,
					 ccb,
					 /*retries*/retry_count,
					 /*flags*/CAM_DIR_IN,
					 /*protocol*/AP_PROTO_PIO_IN,
					 /*tag_action*/MSG_SIMPLE_Q_TAG,
					 /*command*/retry_command,
					 /*features*/0,
					 /*lba*/0,
					 /*sector_count*/0,
					 /*data_ptr*/(u_int8_t *)ptr,
					 /*dxfer_len*/sizeof(struct ata_params),
					 /*timeout*/timeout ? timeout : 30 * 1000,
					 /*quiet*/0);

		if (error != 0) {
			free(ptr);
			return (1);
		}
	}

	error = 1;
	for (i = 0; i < sizeof(struct ata_params) / 2; i++) {
		ptr[i] = le16toh(ptr[i]);
		if (ptr[i] != 0)
			error = 0;
	}

	if (arglist & CAM_ARG_VERBOSE) {
		fprintf(stdout, "%s%d: Raw identify data:\n",
		    device->device_name, device->dev_unit_num);
		dump_data(ptr, sizeof(struct ata_params));
	}

	/* check for invalid (all zero) response */
	if (error != 0) {
		warnx("Invalid identify response detected");
		free(ptr);
		return (error);
	}

	ident_buf = (struct ata_params *)ptr;
	if (strncmp(ident_buf->model, "FX", 2) &&
	    strncmp(ident_buf->model, "NEC", 3) &&
	    strncmp(ident_buf->model, "Pioneer", 7) &&
	    strncmp(ident_buf->model, "SHARP", 5)) {
		ata_bswap(ident_buf->model, sizeof(ident_buf->model));
		ata_bswap(ident_buf->revision, sizeof(ident_buf->revision));
		ata_bswap(ident_buf->serial, sizeof(ident_buf->serial));
		ata_bswap(ident_buf->media_serial, sizeof(ident_buf->media_serial));
	}
	ata_btrim(ident_buf->model, sizeof(ident_buf->model));
	ata_bpack(ident_buf->model, ident_buf->model, sizeof(ident_buf->model));
	ata_btrim(ident_buf->revision, sizeof(ident_buf->revision));
	ata_bpack(ident_buf->revision, ident_buf->revision, sizeof(ident_buf->revision));
	ata_btrim(ident_buf->serial, sizeof(ident_buf->serial));
	ata_bpack(ident_buf->serial, ident_buf->serial, sizeof(ident_buf->serial));
	ata_btrim(ident_buf->media_serial, sizeof(ident_buf->media_serial));
	ata_bpack(ident_buf->media_serial, ident_buf->media_serial,
	    sizeof(ident_buf->media_serial));

	*ident_bufp = ident_buf;

	return (0);
}


static int
ataidentify(struct cam_device *device, int retry_count, int timeout)
{
	union ccb *ccb;
	struct ata_params *ident_buf;
	u_int64_t hpasize;

	if ((ccb = cam_getccb(device)) == NULL) {
		warnx("couldn't allocate CCB");
		return (1);
	}

	if (ata_do_identify(device, retry_count, timeout, ccb, &ident_buf) != 0) {
		cam_freeccb(ccb);
		return (1);
	}

	if (ident_buf->support.command1 & ATA_SUPPORT_PROTECTED) {
		if (ata_read_native_max(device, retry_count, timeout, ccb,
					ident_buf, &hpasize) != 0) {
			cam_freeccb(ccb);
			return (1);
		}
	} else {
		hpasize = 0;
	}

	printf("%s%d: ", device->device_name, device->dev_unit_num);
	ata_print_ident(ident_buf);
	camxferrate(device);
	atacapprint(ident_buf);
	atahpa_print(ident_buf, hpasize, 0);

	free(ident_buf);
	cam_freeccb(ccb);

	return (0);
}

#ifdef WITH_NVME
static int
nvmeidentify(struct cam_device *device, int retry_count __unused, int timeout __unused)
{
	struct nvme_controller_data cdata;

	if (nvme_get_cdata(device, &cdata))
		return (1);
	nvme_print_controller(&cdata);

	return (0);
}
#endif

static int
identify(struct cam_device *device, int retry_count, int timeout)
{
#ifdef WITH_NVME
	struct ccb_pathinq cpi;

	if (get_cpi(device, &cpi) != 0) {
		warnx("couldn't get CPI");
		return (-1);
	}

	if (cpi.protocol == PROTO_NVME) {
		return (nvmeidentify(device, retry_count, timeout));
	}
#endif
	return (ataidentify(device, retry_count, timeout));
}
#endif /* MINIMALISTIC */


#ifndef MINIMALISTIC
enum {
	ATA_SECURITY_ACTION_PRINT,
	ATA_SECURITY_ACTION_FREEZE,
	ATA_SECURITY_ACTION_UNLOCK,
	ATA_SECURITY_ACTION_DISABLE,
	ATA_SECURITY_ACTION_ERASE,
	ATA_SECURITY_ACTION_ERASE_ENHANCED,
	ATA_SECURITY_ACTION_SET_PASSWORD
};

static void
atasecurity_print_time(u_int16_t tw)
{

	if (tw == 0)
		printf("unspecified");
	else if (tw >= 255)
		printf("> 508 min");
	else
		printf("%i min", 2 * tw);
}

static u_int32_t
atasecurity_erase_timeout_msecs(u_int16_t timeout)
{

	if (timeout == 0)
		return 2 * 3600 * 1000; /* default: two hours */
	else if (timeout > 255)
		return (508 + 60) * 60 * 1000; /* spec says > 508 minutes */

	return ((2 * timeout) + 5) * 60 * 1000; /* add a 5min margin */
}


static void
atasecurity_notify(u_int8_t command, struct ata_security_password *pwd)
{
	struct ata_cmd cmd;

	bzero(&cmd, sizeof(cmd));
	cmd.command = command;
	printf("Issuing %s", ata_op_string(&cmd));

	if (pwd != NULL) {
		char pass[sizeof(pwd->password)+1];

		/* pwd->password may not be null terminated */
		pass[sizeof(pwd->password)] = '\0';
		strncpy(pass, pwd->password, sizeof(pwd->password));
		printf(" password='%s', user='%s'",
			pass,
			(pwd->ctrl & ATA_SECURITY_PASSWORD_MASTER) ?
			"master" : "user");

		if (command == ATA_SECURITY_SET_PASSWORD) {
			printf(", mode='%s'",
			       (pwd->ctrl & ATA_SECURITY_LEVEL_MAXIMUM) ?
			       "maximum" : "high");
		}
	}

	printf("\n");
}

static int
atasecurity_freeze(struct cam_device *device, union ccb *ccb,
		   int retry_count, u_int32_t timeout, int quiet)
{

	if (quiet == 0)
		atasecurity_notify(ATA_SECURITY_FREEZE_LOCK, NULL);

	return ata_do_28bit_cmd(device,
				ccb,
				retry_count,
				/*flags*/CAM_DIR_NONE,
				/*protocol*/AP_PROTO_NON_DATA,
				/*tag_action*/MSG_SIMPLE_Q_TAG,
				/*command*/ATA_SECURITY_FREEZE_LOCK,
				/*features*/0,
				/*lba*/0,
				/*sector_count*/0,
				/*data_ptr*/NULL,
				/*dxfer_len*/0,
				/*timeout*/timeout,
				/*quiet*/0);
}

static int
atasecurity_unlock(struct cam_device *device, union ccb *ccb,
		   int retry_count, u_int32_t timeout,
		   struct ata_security_password *pwd, int quiet)
{

	if (quiet == 0)
		atasecurity_notify(ATA_SECURITY_UNLOCK, pwd);

	return ata_do_28bit_cmd(device,
				ccb,
				retry_count,
				/*flags*/CAM_DIR_OUT,
				/*protocol*/AP_PROTO_PIO_OUT,
				/*tag_action*/MSG_SIMPLE_Q_TAG,
				/*command*/ATA_SECURITY_UNLOCK,
				/*features*/0,
				/*lba*/0,
				/*sector_count*/0,
				/*data_ptr*/(u_int8_t *)pwd,
				/*dxfer_len*/sizeof(*pwd),
				/*timeout*/timeout,
				/*quiet*/0);
}

static int
atasecurity_disable(struct cam_device *device, union ccb *ccb,
		    int retry_count, u_int32_t timeout,
		    struct ata_security_password *pwd, int quiet)
{

	if (quiet == 0)
		atasecurity_notify(ATA_SECURITY_DISABLE_PASSWORD, pwd);
	return ata_do_28bit_cmd(device,
				ccb,
				retry_count,
				/*flags*/CAM_DIR_OUT,
				/*protocol*/AP_PROTO_PIO_OUT,
				/*tag_action*/MSG_SIMPLE_Q_TAG,
				/*command*/ATA_SECURITY_DISABLE_PASSWORD,
				/*features*/0,
				/*lba*/0,
				/*sector_count*/0,
				/*data_ptr*/(u_int8_t *)pwd,
				/*dxfer_len*/sizeof(*pwd),
				/*timeout*/timeout,
				/*quiet*/0);
}


static int
atasecurity_erase_confirm(struct cam_device *device,
			  struct ata_params* ident_buf)
{

	printf("\nYou are about to ERASE ALL DATA from the following"
	       " device:\n%s%d,%s%d: ", device->device_name,
	       device->dev_unit_num, device->given_dev_name,
	       device->given_unit_number);
	ata_print_ident(ident_buf);

	for(;;) {
		char str[50];
		printf("\nAre you SURE you want to ERASE ALL DATA? (yes/no) ");

		if (fgets(str, sizeof(str), stdin) != NULL) {
			if (strncasecmp(str, "yes", 3) == 0) {
				return (1);
			} else if (strncasecmp(str, "no", 2) == 0) {
				return (0);
			} else {
				printf("Please answer \"yes\" or "
				       "\"no\"\n");
			}
		}
	}

	/* NOTREACHED */
	return (0);
}

static int
atasecurity_erase(struct cam_device *device, union ccb *ccb,
		  int retry_count, u_int32_t timeout,
		  u_int32_t erase_timeout,
		  struct ata_security_password *pwd, int quiet)
{
	int error;

	if (quiet == 0)
		atasecurity_notify(ATA_SECURITY_ERASE_PREPARE, NULL);

	error = ata_do_28bit_cmd(device,
				 ccb,
				 retry_count,
				 /*flags*/CAM_DIR_NONE,
				 /*protocol*/AP_PROTO_NON_DATA,
				 /*tag_action*/MSG_SIMPLE_Q_TAG,
				 /*command*/ATA_SECURITY_ERASE_PREPARE,
				 /*features*/0,
				 /*lba*/0,
				 /*sector_count*/0,
				 /*data_ptr*/NULL,
				 /*dxfer_len*/0,
				 /*timeout*/timeout,
				 /*quiet*/0);

	if (error != 0)
		return error;

	if (quiet == 0)
		atasecurity_notify(ATA_SECURITY_ERASE_UNIT, pwd);

	error = ata_do_28bit_cmd(device,
				 ccb,
				 retry_count,
				 /*flags*/CAM_DIR_OUT,
				 /*protocol*/AP_PROTO_PIO_OUT,
				 /*tag_action*/MSG_SIMPLE_Q_TAG,
				 /*command*/ATA_SECURITY_ERASE_UNIT,
				 /*features*/0,
				 /*lba*/0,
				 /*sector_count*/0,
				 /*data_ptr*/(u_int8_t *)pwd,
				 /*dxfer_len*/sizeof(*pwd),
				 /*timeout*/erase_timeout,
				 /*quiet*/0);

	if (error == 0 && quiet == 0)
		printf("\nErase Complete\n");

	return error;
}

static int
atasecurity_set_password(struct cam_device *device, union ccb *ccb,
			 int retry_count, u_int32_t timeout,
			 struct ata_security_password *pwd, int quiet)
{

	if (quiet == 0)
		atasecurity_notify(ATA_SECURITY_SET_PASSWORD, pwd);

	return ata_do_28bit_cmd(device,
				 ccb,
				 retry_count,
				 /*flags*/CAM_DIR_OUT,
				 /*protocol*/AP_PROTO_PIO_OUT,
				 /*tag_action*/MSG_SIMPLE_Q_TAG,
				 /*command*/ATA_SECURITY_SET_PASSWORD,
				 /*features*/0,
				 /*lba*/0,
				 /*sector_count*/0,
				 /*data_ptr*/(u_int8_t *)pwd,
				 /*dxfer_len*/sizeof(*pwd),
				 /*timeout*/timeout,
				 /*quiet*/0);
}

static void
atasecurity_print(struct ata_params *parm)
{

	printf("\nSecurity Option           Value\n");
	if (arglist & CAM_ARG_VERBOSE) {
		printf("status                    %04x\n",
		       parm->security_status);
	}
	printf("supported                 %s\n",
		parm->security_status & ATA_SECURITY_SUPPORTED ? "yes" : "no");
	if (!(parm->security_status & ATA_SECURITY_SUPPORTED))
		return;
	printf("enabled                   %s\n",
		parm->security_status & ATA_SECURITY_ENABLED ? "yes" : "no");
	printf("drive locked              %s\n",
		parm->security_status & ATA_SECURITY_LOCKED ? "yes" : "no");
	printf("security config frozen    %s\n",
		parm->security_status & ATA_SECURITY_FROZEN ? "yes" : "no");
	printf("count expired             %s\n",
		parm->security_status & ATA_SECURITY_COUNT_EXP ? "yes" : "no");
	printf("security level            %s\n",
		parm->security_status & ATA_SECURITY_LEVEL ? "maximum" : "high");
	printf("enhanced erase supported  %s\n",
		parm->security_status & ATA_SECURITY_ENH_SUPP ? "yes" : "no");
	printf("erase time                ");
	atasecurity_print_time(parm->erase_time);
	printf("\n");
	printf("enhanced erase time       ");
	atasecurity_print_time(parm->enhanced_erase_time);
	printf("\n");
	printf("master password rev       %04x%s\n",
		parm->master_passwd_revision,
		parm->master_passwd_revision == 0x0000 ||
		parm->master_passwd_revision == 0xFFFF ?  " (unsupported)" : "");
}

/*
 * Validates and copies the password in optarg to the passed buffer.
 * If the password in optarg is the same length as the buffer then
 * the data will still be copied but no null termination will occur.
 */
static int
ata_getpwd(u_int8_t *passwd, int max, char opt)
{
	int len;

	len = strlen(optarg);
	if (len > max) {
		warnx("-%c password is too long", opt);
		return (1);
	} else if (len == 0) {
		warnx("-%c password is missing", opt);
		return (1);
	} else if (optarg[0] == '-'){
		warnx("-%c password starts with '-' (generic arg?)", opt);
		return (1);
	} else if (strlen(passwd) != 0 && strcmp(passwd, optarg) != 0) {
		warnx("-%c password conflicts with existing password from -%c",
		      opt, pwd_opt);
		return (1);
	}

	/* Callers pass in a buffer which does NOT need to be terminated */
	strncpy(passwd, optarg, max);
	pwd_opt = opt;

	return (0);
}

enum {
	ATA_HPA_ACTION_PRINT,
	ATA_HPA_ACTION_SET_MAX,
	ATA_HPA_ACTION_SET_PWD,
	ATA_HPA_ACTION_LOCK,
	ATA_HPA_ACTION_UNLOCK,
	ATA_HPA_ACTION_FREEZE_LOCK
};

static int
atahpa_set_confirm(struct cam_device *device, struct ata_params* ident_buf,
		   u_int64_t maxsize, int persist)
{
	printf("\nYou are about to configure HPA to limit the user accessible\n"
	       "sectors to %ju %s on the device:\n%s%d,%s%d: ", maxsize,
	       persist ? "persistently" : "temporarily",
	       device->device_name, device->dev_unit_num,
	       device->given_dev_name, device->given_unit_number);
	ata_print_ident(ident_buf);

	for(;;) {
		char str[50];
		printf("\nAre you SURE you want to configure HPA? (yes/no) ");

		if (NULL != fgets(str, sizeof(str), stdin)) {
			if (0 == strncasecmp(str, "yes", 3)) {
				return (1);
			} else if (0 == strncasecmp(str, "no", 2)) {
				return (0);
			} else {
				printf("Please answer \"yes\" or "
				       "\"no\"\n");
			}
		}
	}

	/* NOTREACHED */
	return (0);
}

static int
atahpa(struct cam_device *device, int retry_count, int timeout,
       int argc, char **argv, char *combinedopt)
{
	union ccb *ccb;
	struct ata_params *ident_buf;
	struct ccb_getdev cgd;
	struct ata_set_max_pwd pwd;
	int error, confirm, quiet, c, action, actions, persist;
	int security, is48bit, pwdsize;
	u_int64_t hpasize, maxsize;

	actions = 0;
	confirm = 0;
	quiet = 0;
	maxsize = 0;
	persist = 0;
	security = 0;

	memset(&pwd, 0, sizeof(pwd));

	/* default action is to print hpa information */
	action = ATA_HPA_ACTION_PRINT;
	pwdsize = sizeof(pwd.password);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c){
		case 's':
			action = ATA_HPA_ACTION_SET_MAX;
			maxsize = strtoumax(optarg, NULL, 0);
			actions++;
			break;

		case 'p':
			if (ata_getpwd(pwd.password, pwdsize, c) != 0)
				return (1);
			action = ATA_HPA_ACTION_SET_PWD;
			security = 1;
			actions++;
			break;

		case 'l':
			action = ATA_HPA_ACTION_LOCK;
			security = 1;
			actions++;
			break;

		case 'U':
			if (ata_getpwd(pwd.password, pwdsize, c) != 0)
				return (1);
			action = ATA_HPA_ACTION_UNLOCK;
			security = 1;
			actions++;
			break;

		case 'f':
			action = ATA_HPA_ACTION_FREEZE_LOCK;
			security = 1;
			actions++;
			break;

		case 'P':
			persist = 1;
			break;

		case 'y':
			confirm++;
			break;

		case 'q':
			quiet++;
			break;
		}
	}

	if (actions > 1) {
		warnx("too many hpa actions specified");
		return (1);
	}

	if (get_cgd(device, &cgd) != 0) {
		warnx("couldn't get CGD");
		return (1);
	}

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("couldn't allocate CCB");
		return (1);
	}

	error = ata_do_identify(device, retry_count, timeout, ccb, &ident_buf);
	if (error != 0) {
		cam_freeccb(ccb);
		return (1);
	}

	if (quiet == 0) {
		printf("%s%d: ", device->device_name, device->dev_unit_num);
		ata_print_ident(ident_buf);
		camxferrate(device);
	}

	if (action == ATA_HPA_ACTION_PRINT) {
		error = ata_read_native_max(device, retry_count, timeout, ccb,
					    ident_buf, &hpasize);
		if (error == 0)
			atahpa_print(ident_buf, hpasize, 1);

		cam_freeccb(ccb);
		free(ident_buf);
		return (error);
	}

	if (!(ident_buf->support.command1 & ATA_SUPPORT_PROTECTED)) {
		warnx("HPA is not supported by this device");
		cam_freeccb(ccb);
		free(ident_buf);
		return (1);
	}

	if (security && !(ident_buf->support.command1 & ATA_SUPPORT_MAXSECURITY)) {
		warnx("HPA Security is not supported by this device");
		cam_freeccb(ccb);
		free(ident_buf);
		return (1);
	}

	is48bit = ident_buf->support.command2 & ATA_SUPPORT_ADDRESS48;

	/*
	 * The ATA spec requires:
	 * 1. Read native max addr is called directly before set max addr
	 * 2. Read native max addr is NOT called before any other set max call
	 */
	switch(action) {
	case ATA_HPA_ACTION_SET_MAX:
		if (confirm == 0 &&
		    atahpa_set_confirm(device, ident_buf, maxsize,
		    persist) == 0) {
			cam_freeccb(ccb);
			free(ident_buf);
			return (1);
		}

		error = ata_read_native_max(device, retry_count, timeout,
					    ccb, ident_buf, &hpasize);
		if (error == 0) {
			error = atahpa_set_max(device, retry_count, timeout,
					       ccb, is48bit, maxsize, persist);
			if (error == 0) {
				/* redo identify to get new lba values */
				error = ata_do_identify(device, retry_count,
							timeout, ccb,
							&ident_buf);
				atahpa_print(ident_buf, hpasize, 1);
			}
		}
		break;

	case ATA_HPA_ACTION_SET_PWD:
		error = atahpa_password(device, retry_count, timeout,
					ccb, is48bit, &pwd);
		if (error == 0)
			printf("HPA password has been set\n");
		break;

	case ATA_HPA_ACTION_LOCK:
		error = atahpa_lock(device, retry_count, timeout,
				    ccb, is48bit);
		if (error == 0)
			printf("HPA has been locked\n");
		break;

	case ATA_HPA_ACTION_UNLOCK:
		error = atahpa_unlock(device, retry_count, timeout,
				      ccb, is48bit, &pwd);
		if (error == 0)
			printf("HPA has been unlocked\n");
		break;

	case ATA_HPA_ACTION_FREEZE_LOCK:
		error = atahpa_freeze_lock(device, retry_count, timeout,
					   ccb, is48bit);
		if (error == 0)
			printf("HPA has been frozen\n");
		break;

	default:
		errx(1, "Option currently not supported");
	}

	cam_freeccb(ccb);
	free(ident_buf);

	return (error);
}

static int
atasecurity(struct cam_device *device, int retry_count, int timeout,
	    int argc, char **argv, char *combinedopt)
{
	union ccb *ccb;
	struct ata_params *ident_buf;
	int error, confirm, quiet, c, action, actions, setpwd;
	int security_enabled, erase_timeout, pwdsize;
	struct ata_security_password pwd;

	actions = 0;
	setpwd = 0;
	erase_timeout = 0;
	confirm = 0;
	quiet = 0;

	memset(&pwd, 0, sizeof(pwd));

	/* default action is to print security information */
	action = ATA_SECURITY_ACTION_PRINT;

	/* user is master by default as its safer that way */
	pwd.ctrl |= ATA_SECURITY_PASSWORD_MASTER;
	pwdsize = sizeof(pwd.password);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c){
		case 'f':
			action = ATA_SECURITY_ACTION_FREEZE;
			actions++;
			break;

		case 'U':
			if (strcasecmp(optarg, "user") == 0) {
				pwd.ctrl |= ATA_SECURITY_PASSWORD_USER;
				pwd.ctrl &= ~ATA_SECURITY_PASSWORD_MASTER;
			} else if (strcasecmp(optarg, "master") == 0) {
				pwd.ctrl |= ATA_SECURITY_PASSWORD_MASTER;
				pwd.ctrl &= ~ATA_SECURITY_PASSWORD_USER;
			} else {
				warnx("-U argument '%s' is invalid (must be "
				      "'user' or 'master')", optarg);
				return (1);
			}
			break;

		case 'l':
			if (strcasecmp(optarg, "high") == 0) {
				pwd.ctrl |= ATA_SECURITY_LEVEL_HIGH;
				pwd.ctrl &= ~ATA_SECURITY_LEVEL_MAXIMUM;
			} else if (strcasecmp(optarg, "maximum") == 0) {
				pwd.ctrl |= ATA_SECURITY_LEVEL_MAXIMUM;
				pwd.ctrl &= ~ATA_SECURITY_LEVEL_HIGH;
			} else {
				warnx("-l argument '%s' is unknown (must be "
				      "'high' or 'maximum')", optarg);
				return (1);
			}
			break;

		case 'k':
			if (ata_getpwd(pwd.password, pwdsize, c) != 0)
				return (1);
			action = ATA_SECURITY_ACTION_UNLOCK;
			actions++;
			break;

		case 'd':
			if (ata_getpwd(pwd.password, pwdsize, c) != 0)
				return (1);
			action = ATA_SECURITY_ACTION_DISABLE;
			actions++;
			break;

		case 'e':
			if (ata_getpwd(pwd.password, pwdsize, c) != 0)
				return (1);
			action = ATA_SECURITY_ACTION_ERASE;
			actions++;
			break;

		case 'h':
			if (ata_getpwd(pwd.password, pwdsize, c) != 0)
				return (1);
			pwd.ctrl |= ATA_SECURITY_ERASE_ENHANCED;
			action = ATA_SECURITY_ACTION_ERASE_ENHANCED;
			actions++;
			break;

		case 's':
			if (ata_getpwd(pwd.password, pwdsize, c) != 0)
				return (1);
			setpwd = 1;
			if (action == ATA_SECURITY_ACTION_PRINT)
				action = ATA_SECURITY_ACTION_SET_PASSWORD;
			/*
			 * Don't increment action as this can be combined
			 * with other actions.
			 */
			break;

		case 'y':
			confirm++;
			break;

		case 'q':
			quiet++;
			break;

		case 'T':
			erase_timeout = atoi(optarg) * 1000;
			break;
		}
	}

	if (actions > 1) {
		warnx("too many security actions specified");
		return (1);
	}

	if ((ccb = cam_getccb(device)) == NULL) {
		warnx("couldn't allocate CCB");
		return (1);
	}

	error = ata_do_identify(device, retry_count, timeout, ccb, &ident_buf);
	if (error != 0) {
		cam_freeccb(ccb);
		return (1);
	}

	if (quiet == 0) {
		printf("%s%d: ", device->device_name, device->dev_unit_num);
		ata_print_ident(ident_buf);
		camxferrate(device);
	}

	if (action == ATA_SECURITY_ACTION_PRINT) {
		atasecurity_print(ident_buf);
		free(ident_buf);
		cam_freeccb(ccb);
		return (0);
	}

	if ((ident_buf->support.command1 & ATA_SUPPORT_SECURITY) == 0) {
		warnx("Security not supported");
		free(ident_buf);
		cam_freeccb(ccb);
		return (1);
	}

	/* default timeout 15 seconds the same as linux hdparm */
	timeout = timeout ? timeout : 15 * 1000;

	security_enabled = ident_buf->security_status & ATA_SECURITY_ENABLED;

	/* first set the password if requested */
	if (setpwd == 1) {
		/* confirm we can erase before setting the password if erasing */
		if (confirm == 0 &&
		    (action == ATA_SECURITY_ACTION_ERASE_ENHANCED ||
		    action == ATA_SECURITY_ACTION_ERASE) &&
		    atasecurity_erase_confirm(device, ident_buf) == 0) {
			cam_freeccb(ccb);
			free(ident_buf);
			return (error);
		}

		if (pwd.ctrl & ATA_SECURITY_PASSWORD_MASTER) {
			pwd.revision = ident_buf->master_passwd_revision;
			if (pwd.revision != 0 && pwd.revision != 0xfff &&
			    --pwd.revision == 0) {
				pwd.revision = 0xfffe;
			}
		}
		error = atasecurity_set_password(device, ccb, retry_count,
						 timeout, &pwd, quiet);
		if (error != 0) {
			cam_freeccb(ccb);
			free(ident_buf);
			return (error);
		}
		security_enabled = 1;
	}

	switch(action) {
	case ATA_SECURITY_ACTION_FREEZE:
		error = atasecurity_freeze(device, ccb, retry_count,
					   timeout, quiet);
		break;

	case ATA_SECURITY_ACTION_UNLOCK:
		if (security_enabled) {
			if (ident_buf->security_status & ATA_SECURITY_LOCKED) {
				error = atasecurity_unlock(device, ccb,
					retry_count, timeout, &pwd, quiet);
			} else {
				warnx("Can't unlock, drive is not locked");
				error = 1;
			}
		} else {
			warnx("Can't unlock, security is disabled");
			error = 1;
		}
		break;

	case ATA_SECURITY_ACTION_DISABLE:
		if (security_enabled) {
			/* First unlock the drive if its locked */
			if (ident_buf->security_status & ATA_SECURITY_LOCKED) {
				error = atasecurity_unlock(device, ccb,
							   retry_count,
							   timeout,
							   &pwd,
							   quiet);
			}

			if (error == 0) {
				error = atasecurity_disable(device,
							    ccb,
							    retry_count,
							    timeout,
							    &pwd,
							    quiet);
			}
		} else {
			warnx("Can't disable security (already disabled)");
			error = 1;
		}
		break;

	case ATA_SECURITY_ACTION_ERASE:
		if (security_enabled) {
			if (erase_timeout == 0) {
				erase_timeout = atasecurity_erase_timeout_msecs(
				    ident_buf->erase_time);
			}

			error = atasecurity_erase(device, ccb, retry_count,
			    timeout, erase_timeout, &pwd, quiet);
		} else {
			warnx("Can't secure erase (security is disabled)");
			error = 1;
		}
		break;

	case ATA_SECURITY_ACTION_ERASE_ENHANCED:
		if (security_enabled) {
			if (ident_buf->security_status & ATA_SECURITY_ENH_SUPP) {
				if (erase_timeout == 0) {
					erase_timeout =
					    atasecurity_erase_timeout_msecs(
						ident_buf->enhanced_erase_time);
				}

				error = atasecurity_erase(device, ccb,
							  retry_count, timeout,
							  erase_timeout, &pwd,
							  quiet);
			} else {
				warnx("Enhanced erase is not supported");
				error = 1;
			}
		} else {
			warnx("Can't secure erase (enhanced), "
			      "(security is disabled)");
			error = 1;
		}
		break;
	}

	cam_freeccb(ccb);
	free(ident_buf);

	return (error);
}
#endif /* MINIMALISTIC */

/*
 * Parse out a bus, or a bus, target and lun in the following
 * format:
 * bus
 * bus:target
 * bus:target:lun
 *
 * Returns the number of parsed components, or 0.
 */
static int
parse_btl(char *tstr, path_id_t *bus, target_id_t *target, lun_id_t *lun,
    cam_argmask *arglst)
{
	char *tmpstr;
	int convs = 0;

	while (isspace(*tstr) && (*tstr != '\0'))
		tstr++;

	tmpstr = (char *)strtok(tstr, ":");
	if ((tmpstr != NULL) && (*tmpstr != '\0')) {
		*bus = strtol(tmpstr, NULL, 0);
		*arglst |= CAM_ARG_BUS;
		convs++;
		tmpstr = (char *)strtok(NULL, ":");
		if ((tmpstr != NULL) && (*tmpstr != '\0')) {
			*target = strtol(tmpstr, NULL, 0);
			*arglst |= CAM_ARG_TARGET;
			convs++;
			tmpstr = (char *)strtok(NULL, ":");
			if ((tmpstr != NULL) && (*tmpstr != '\0')) {
				*lun = strtol(tmpstr, NULL, 0);
				*arglst |= CAM_ARG_LUN;
				convs++;
			}
		}
	}

	return convs;
}

static int
dorescan_or_reset(int argc, char **argv, int rescan)
{
	static const char must[] =
		"you must specify \"all\", a bus, or a bus:target:lun to %s";
	int rv, error = 0;
	path_id_t bus = CAM_BUS_WILDCARD;
	target_id_t target = CAM_TARGET_WILDCARD;
	lun_id_t lun = CAM_LUN_WILDCARD;
	char *tstr;

	if (argc < 3) {
		warnx(must, rescan? "rescan" : "reset");
		return (1);
	}

	tstr = argv[optind];
	while (isspace(*tstr) && (*tstr != '\0'))
		tstr++;
	if (strncasecmp(tstr, "all", strlen("all")) == 0)
		arglist |= CAM_ARG_BUS;
	else if (isdigit(*tstr)) {
		rv = parse_btl(argv[optind], &bus, &target, &lun, &arglist);
		if (rv != 1 && rv != 3) {
			warnx(must, rescan? "rescan" : "reset");
			return (1);
		}
	} else {
		char name[30];
		int unit;
		int fd = -1;
		union ccb ccb;

		/*
		 * Note that resetting or rescanning a device used to
		 * require a bus or bus:target:lun.  This is because the
		 * device in question may not exist and you're trying to
		 * get the controller to rescan to find it.  It may also be
		 * because the device is hung / unresponsive, and opening
		 * an unresponsive device is not desireable.
		 *
		 * It can be more convenient to reference a device by
		 * peripheral name and unit number, though, and it is
		 * possible to get the bus:target:lun for devices that
		 * currently exist in the EDT.  So this can work for
		 * devices that we want to reset, or devices that exist
		 * that we want to rescan, but not devices that do not
		 * exist yet.
		 *
		 * So, we are careful here to look up the bus/target/lun
		 * for the device the user wants to operate on, specified
		 * by peripheral instance (e.g. da0, pass32) without
		 * actually opening that device.  The process is similar to
		 * what cam_lookup_pass() does, except that we don't
		 * actually open the passthrough driver instance in the end.
		 */

		if (cam_get_device(tstr, name, sizeof(name), &unit) == -1) {
			warnx("%s", cam_errbuf);
			error = 1;
			goto bailout;
		}

		if ((fd = open(XPT_DEVICE, O_RDWR)) == -1) {
			warn("Unable to open %s", XPT_DEVICE);
			error = 1;
			goto bailout;
		}

		bzero(&ccb, sizeof(ccb));

		/*
		 * The function code isn't strictly necessary for the
		 * GETPASSTHRU ioctl.
		 */
		ccb.ccb_h.func_code = XPT_GDEVLIST;

		/*
		 * These two are necessary for the GETPASSTHRU ioctl to
		 * work.
		 */
		strlcpy(ccb.cgdl.periph_name, name,
			sizeof(ccb.cgdl.periph_name));
		ccb.cgdl.unit_number = unit;

		/*
		 * Attempt to get the passthrough device.  This ioctl will
		 * fail if the device name is null, if the device doesn't
		 * exist, or if the passthrough driver isn't in the kernel.
		 */
		if (ioctl(fd, CAMGETPASSTHRU, &ccb) == -1) {
			warn("Unable to find bus:target:lun for device %s%d",
			    name, unit);
			error = 1;
			close(fd);
			goto bailout;
		}
		if ((ccb.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			const struct cam_status_entry *entry;

			entry = cam_fetch_status_entry(ccb.ccb_h.status);
			warnx("Unable to find bus:target_lun for device %s%d, "
			    "CAM status: %s (%#x)", name, unit,
			    entry ? entry->status_text : "Unknown",
			    ccb.ccb_h.status);
			error = 1;
			close(fd);
			goto bailout;
		}

		/*
		 * The kernel fills in the bus/target/lun.  We don't
		 * need the passthrough device name and unit number since
		 * we aren't going to open it.
		 */
		bus = ccb.ccb_h.path_id;
		target = ccb.ccb_h.target_id;
		lun = ccb.ccb_h.target_lun;

		arglist |= CAM_ARG_BUS | CAM_ARG_TARGET | CAM_ARG_LUN;

		close(fd);
	}

	if ((arglist & CAM_ARG_BUS)
	    && (arglist & CAM_ARG_TARGET)
	    && (arglist & CAM_ARG_LUN))
		error = scanlun_or_reset_dev(bus, target, lun, rescan);
	else
		error = rescan_or_reset_bus(bus, rescan);

bailout:

	return (error);
}

static int
rescan_or_reset_bus(path_id_t bus, int rescan)
{
	union ccb *ccb = NULL, *matchccb = NULL;
	int fd = -1, retval;
	int bufsize;

	retval = 0;

	if ((fd = open(XPT_DEVICE, O_RDWR)) < 0) {
		warnx("error opening transport layer device %s", XPT_DEVICE);
		warn("%s", XPT_DEVICE);
		return (1);
	}

	ccb = malloc(sizeof(*ccb));
	if (ccb == NULL) {
		warn("failed to allocate CCB");
		retval = 1;
		goto bailout;
	}
	bzero(ccb, sizeof(*ccb));

	if (bus != CAM_BUS_WILDCARD) {
		ccb->ccb_h.func_code = rescan ? XPT_SCAN_BUS : XPT_RESET_BUS;
		ccb->ccb_h.path_id = bus;
		ccb->ccb_h.target_id = CAM_TARGET_WILDCARD;
		ccb->ccb_h.target_lun = CAM_LUN_WILDCARD;
		ccb->crcn.flags = CAM_FLAG_NONE;

		/* run this at a low priority */
		ccb->ccb_h.pinfo.priority = 5;

		if (ioctl(fd, CAMIOCOMMAND, ccb) == -1) {
			warn("CAMIOCOMMAND ioctl failed");
			retval = 1;
			goto bailout;
		}

		if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
			fprintf(stdout, "%s of bus %d was successful\n",
			    rescan ? "Re-scan" : "Reset", bus);
		} else {
			fprintf(stdout, "%s of bus %d returned error %#x\n",
				rescan ? "Re-scan" : "Reset", bus,
				ccb->ccb_h.status & CAM_STATUS_MASK);
			retval = 1;
		}

		goto bailout;
	}


	/*
	 * The right way to handle this is to modify the xpt so that it can
	 * handle a wildcarded bus in a rescan or reset CCB.  At the moment
	 * that isn't implemented, so instead we enumerate the buses and
	 * send the rescan or reset to those buses in the case where the
	 * given bus is -1 (wildcard).  We don't send a rescan or reset
	 * to the xpt bus; sending a rescan to the xpt bus is effectively a
	 * no-op, sending a rescan to the xpt bus would result in a status of
	 * CAM_REQ_INVALID.
	 */
	matchccb = malloc(sizeof(*matchccb));
	if (matchccb == NULL) {
		warn("failed to allocate CCB");
		retval = 1;
		goto bailout;
	}
	bzero(matchccb, sizeof(*matchccb));
	matchccb->ccb_h.func_code = XPT_DEV_MATCH;
	matchccb->ccb_h.path_id = CAM_BUS_WILDCARD;
	bufsize = sizeof(struct dev_match_result) * 20;
	matchccb->cdm.match_buf_len = bufsize;
	matchccb->cdm.matches=(struct dev_match_result *)malloc(bufsize);
	if (matchccb->cdm.matches == NULL) {
		warnx("can't malloc memory for matches");
		retval = 1;
		goto bailout;
	}
	matchccb->cdm.num_matches = 0;

	matchccb->cdm.num_patterns = 1;
	matchccb->cdm.pattern_buf_len = sizeof(struct dev_match_pattern);

	matchccb->cdm.patterns = (struct dev_match_pattern *)malloc(
		matchccb->cdm.pattern_buf_len);
	if (matchccb->cdm.patterns == NULL) {
		warnx("can't malloc memory for patterns");
		retval = 1;
		goto bailout;
	}
	matchccb->cdm.patterns[0].type = DEV_MATCH_BUS;
	matchccb->cdm.patterns[0].pattern.bus_pattern.flags = BUS_MATCH_ANY;

	do {
		unsigned int i;

		if (ioctl(fd, CAMIOCOMMAND, matchccb) == -1) {
			warn("CAMIOCOMMAND ioctl failed");
			retval = 1;
			goto bailout;
		}

		if ((matchccb->ccb_h.status != CAM_REQ_CMP)
		 || ((matchccb->cdm.status != CAM_DEV_MATCH_LAST)
		   && (matchccb->cdm.status != CAM_DEV_MATCH_MORE))) {
			warnx("got CAM error %#x, CDM error %d\n",
			      matchccb->ccb_h.status, matchccb->cdm.status);
			retval = 1;
			goto bailout;
		}

		for (i = 0; i < matchccb->cdm.num_matches; i++) {
			struct bus_match_result *bus_result;

			/* This shouldn't happen. */
			if (matchccb->cdm.matches[i].type != DEV_MATCH_BUS)
				continue;

			bus_result =&matchccb->cdm.matches[i].result.bus_result;

			/*
			 * We don't want to rescan or reset the xpt bus.
			 * See above.
			 */
			if (bus_result->path_id == CAM_XPT_PATH_ID)
				continue;

			ccb->ccb_h.func_code = rescan ? XPT_SCAN_BUS :
						       XPT_RESET_BUS;
			ccb->ccb_h.path_id = bus_result->path_id;
			ccb->ccb_h.target_id = CAM_TARGET_WILDCARD;
			ccb->ccb_h.target_lun = CAM_LUN_WILDCARD;
			ccb->crcn.flags = CAM_FLAG_NONE;

			/* run this at a low priority */
			ccb->ccb_h.pinfo.priority = 5;

			if (ioctl(fd, CAMIOCOMMAND, ccb) == -1) {
				warn("CAMIOCOMMAND ioctl failed");
				retval = 1;
				goto bailout;
			}

			if ((ccb->ccb_h.status & CAM_STATUS_MASK)==CAM_REQ_CMP){
				fprintf(stdout, "%s of bus %d was successful\n",
					rescan? "Re-scan" : "Reset",
					bus_result->path_id);
			} else {
				/*
				 * Don't bail out just yet, maybe the other
				 * rescan or reset commands will complete
				 * successfully.
				 */
				fprintf(stderr, "%s of bus %d returned error "
					"%#x\n", rescan? "Re-scan" : "Reset",
					bus_result->path_id,
					ccb->ccb_h.status & CAM_STATUS_MASK);
				retval = 1;
			}
		}
	} while ((matchccb->ccb_h.status == CAM_REQ_CMP)
		 && (matchccb->cdm.status == CAM_DEV_MATCH_MORE));

bailout:

	if (fd != -1)
		close(fd);

	if (matchccb != NULL) {
		free(matchccb->cdm.patterns);
		free(matchccb->cdm.matches);
		free(matchccb);
	}
	free(ccb);

	return (retval);
}

static int
scanlun_or_reset_dev(path_id_t bus, target_id_t target, lun_id_t lun, int scan)
{
	union ccb ccb;
	struct cam_device *device;
	int fd;

	device = NULL;

	if (bus == CAM_BUS_WILDCARD) {
		warnx("invalid bus number %d", bus);
		return (1);
	}

	if (target == CAM_TARGET_WILDCARD) {
		warnx("invalid target number %d", target);
		return (1);
	}

	if (lun == CAM_LUN_WILDCARD) {
		warnx("invalid lun number %jx", (uintmax_t)lun);
		return (1);
	}

	fd = -1;

	bzero(&ccb, sizeof(union ccb));

	if (scan) {
		if ((fd = open(XPT_DEVICE, O_RDWR)) < 0) {
			warnx("error opening transport layer device %s\n",
			    XPT_DEVICE);
			warn("%s", XPT_DEVICE);
			return (1);
		}
	} else {
		device = cam_open_btl(bus, target, lun, O_RDWR, NULL);
		if (device == NULL) {
			warnx("%s", cam_errbuf);
			return (1);
		}
	}

	ccb.ccb_h.func_code = (scan)? XPT_SCAN_LUN : XPT_RESET_DEV;
	ccb.ccb_h.path_id = bus;
	ccb.ccb_h.target_id = target;
	ccb.ccb_h.target_lun = lun;
	ccb.ccb_h.timeout = 5000;
	ccb.crcn.flags = CAM_FLAG_NONE;

	/* run this at a low priority */
	ccb.ccb_h.pinfo.priority = 5;

	if (scan) {
		if (ioctl(fd, CAMIOCOMMAND, &ccb) < 0) {
			warn("CAMIOCOMMAND ioctl failed");
			close(fd);
			return (1);
		}
	} else {
		if (cam_send_ccb(device, &ccb) < 0) {
			warn("error sending XPT_RESET_DEV CCB");
			cam_close_device(device);
			return (1);
		}
	}

	if (scan)
		close(fd);
	else
		cam_close_device(device);

	/*
	 * An error code of CAM_BDR_SENT is normal for a BDR request.
	 */
	if (((ccb.ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
	 || ((!scan)
	  && ((ccb.ccb_h.status & CAM_STATUS_MASK) == CAM_BDR_SENT))) {
		fprintf(stdout, "%s of %d:%d:%jx was successful\n",
		    scan? "Re-scan" : "Reset", bus, target, (uintmax_t)lun);
		return (0);
	} else {
		fprintf(stdout, "%s of %d:%d:%jx returned error %#x\n",
		    scan? "Re-scan" : "Reset", bus, target, (uintmax_t)lun,
		    ccb.ccb_h.status & CAM_STATUS_MASK);
		return (1);
	}
}

#ifndef MINIMALISTIC

static struct scsi_nv defect_list_type_map[] = {
	{ "block", SRDD10_BLOCK_FORMAT },
	{ "extbfi", SRDD10_EXT_BFI_FORMAT },
	{ "extphys", SRDD10_EXT_PHYS_FORMAT },
	{ "longblock", SRDD10_LONG_BLOCK_FORMAT },
	{ "bfi", SRDD10_BYTES_FROM_INDEX_FORMAT },
	{ "phys", SRDD10_PHYSICAL_SECTOR_FORMAT }
};

static int
readdefects(struct cam_device *device, int argc, char **argv,
	    char *combinedopt, int task_attr, int retry_count, int timeout)
{
	union ccb *ccb = NULL;
	struct scsi_read_defect_data_hdr_10 *hdr10 = NULL;
	struct scsi_read_defect_data_hdr_12 *hdr12 = NULL;
	size_t hdr_size = 0, entry_size = 0;
	int use_12byte = 0;
	int hex_format = 0;
	u_int8_t *defect_list = NULL;
	u_int8_t list_format = 0;
	int list_type_set = 0;
	u_int32_t dlist_length = 0;
	u_int32_t returned_length = 0, valid_len = 0;
	u_int32_t num_returned = 0, num_valid = 0;
	u_int32_t max_possible_size = 0, hdr_max = 0;
	u_int32_t starting_offset = 0;
	u_int8_t returned_format, returned_type;
	unsigned int i;
	int summary = 0, quiet = 0;
	int c, error = 0;
	int lists_specified = 0;
	int get_length = 1, first_pass = 1;
	int mads = 0;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c){
		case 'f':
		{
			scsi_nv_status status;
			int entry_num = 0;

			status = scsi_get_nv(defect_list_type_map,
			    sizeof(defect_list_type_map) /
			    sizeof(defect_list_type_map[0]), optarg,
			    &entry_num, SCSI_NV_FLAG_IG_CASE);

			if (status == SCSI_NV_FOUND) {
				list_format = defect_list_type_map[
				    entry_num].value;
				list_type_set = 1;
			} else {
				warnx("%s: %s %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid", "defect list type",
				    optarg);
				error = 1;
				goto defect_bailout;
			}
			break;
		}
		case 'G':
			arglist |= CAM_ARG_GLIST;
			break;
		case 'P':
			arglist |= CAM_ARG_PLIST;
			break;
		case 'q':
			quiet = 1;
			break;
		case 's':
			summary = 1;
			break;
		case 'S': {
			char *endptr;

			starting_offset = strtoul(optarg, &endptr, 0);
			if (*endptr != '\0') {
				error = 1;
				warnx("invalid starting offset %s", optarg);
				goto defect_bailout;
			}
			break;
		}
		case 'X':
			hex_format = 1;
			break;
		default:
			break;
		}
	}

	if (list_type_set == 0) {
		error = 1;
		warnx("no defect list format specified");
		goto defect_bailout;
	}

	if (arglist & CAM_ARG_PLIST) {
		list_format |= SRDD10_PLIST;
		lists_specified++;
	}

	if (arglist & CAM_ARG_GLIST) {
		list_format |= SRDD10_GLIST;
		lists_specified++;
	}

	/*
	 * This implies a summary, and was the previous behavior.
	 */
	if (lists_specified == 0)
		summary = 1;

	ccb = cam_getccb(device);

retry_12byte:

	/*
	 * We start off asking for just the header to determine how much
	 * defect data is available.  Some Hitachi drives return an error
	 * if you ask for more data than the drive has.  Once we know the
	 * length, we retry the command with the returned length.
	 */
	if (use_12byte == 0)
		dlist_length = sizeof(*hdr10);
	else
		dlist_length = sizeof(*hdr12);

retry:
	if (defect_list != NULL) {
		free(defect_list);
		defect_list = NULL;
	}
	defect_list = malloc(dlist_length);
	if (defect_list == NULL) {
		warnx("can't malloc memory for defect list");
		error = 1;
		goto defect_bailout;
	}

next_batch:
	bzero(defect_list, dlist_length);

	/*
	 * cam_getccb() zeros the CCB header only.  So we need to zero the
	 * payload portion of the ccb.
	 */
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	scsi_read_defects(&ccb->csio,
			  /*retries*/ retry_count,
			  /*cbfcnp*/ NULL,
			  /*tag_action*/ task_attr,
			  /*list_format*/ list_format,
			  /*addr_desc_index*/ starting_offset,
			  /*data_ptr*/ defect_list,
			  /*dxfer_len*/ dlist_length,
			  /*minimum_cmd_size*/ use_12byte ? 12 : 0,
			  /*sense_len*/ SSD_FULL_SIZE,
			  /*timeout*/ timeout ? timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (cam_send_ccb(device, ccb) < 0) {
		perror("error reading defect list");

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		error = 1;
		goto defect_bailout;
	}

	valid_len = ccb->csio.dxfer_len - ccb->csio.resid;

	if (use_12byte == 0) {
		hdr10 = (struct scsi_read_defect_data_hdr_10 *)defect_list;
		hdr_size = sizeof(*hdr10);
		hdr_max = SRDDH10_MAX_LENGTH;

		if (valid_len >= hdr_size) {
			returned_length = scsi_2btoul(hdr10->length);
			returned_format = hdr10->format;
		} else {
			returned_length = 0;
			returned_format = 0;
		}
	} else {
		hdr12 = (struct scsi_read_defect_data_hdr_12 *)defect_list;
		hdr_size = sizeof(*hdr12);
		hdr_max = SRDDH12_MAX_LENGTH;

		if (valid_len >= hdr_size) {
			returned_length = scsi_4btoul(hdr12->length);
			returned_format = hdr12->format;
		} else {
			returned_length = 0;
			returned_format = 0;
		}
	}

	returned_type = returned_format & SRDDH10_DLIST_FORMAT_MASK;
	switch (returned_type) {
	case SRDD10_BLOCK_FORMAT:
		entry_size = sizeof(struct scsi_defect_desc_block);
		break;
	case SRDD10_LONG_BLOCK_FORMAT:
		entry_size = sizeof(struct scsi_defect_desc_long_block);
		break;
	case SRDD10_EXT_PHYS_FORMAT:
	case SRDD10_PHYSICAL_SECTOR_FORMAT:
		entry_size = sizeof(struct scsi_defect_desc_phys_sector);
		break;
	case SRDD10_EXT_BFI_FORMAT:
	case SRDD10_BYTES_FROM_INDEX_FORMAT:
		entry_size = sizeof(struct scsi_defect_desc_bytes_from_index);
		break;
	default:
		warnx("Unknown defect format 0x%x\n", returned_type);
		error = 1;
		goto defect_bailout;
		break;
	}

	max_possible_size = (hdr_max / entry_size) * entry_size;
	num_returned = returned_length / entry_size;
	num_valid = min(returned_length, valid_len - hdr_size);
	num_valid /= entry_size;

	if (get_length != 0) {
		get_length = 0;

		if ((ccb->ccb_h.status & CAM_STATUS_MASK) ==
		     CAM_SCSI_STATUS_ERROR) {
			struct scsi_sense_data *sense;
			int error_code, sense_key, asc, ascq;

			sense = &ccb->csio.sense_data;
			scsi_extract_sense_len(sense, ccb->csio.sense_len -
			    ccb->csio.sense_resid, &error_code, &sense_key,
			    &asc, &ascq, /*show_errors*/ 1);

			/*
			 * If the drive is reporting that it just doesn't
			 * support the defect list format, go ahead and use
			 * the length it reported.  Otherwise, the length
			 * may not be valid, so use the maximum.
			 */
			if ((sense_key == SSD_KEY_RECOVERED_ERROR)
			 && (asc == 0x1c) && (ascq == 0x00)
			 && (returned_length > 0)) {
				if ((use_12byte == 0)
				 && (returned_length >= max_possible_size)) {
					get_length = 1;
					use_12byte = 1;
					goto retry_12byte;
				}
				dlist_length = returned_length + hdr_size;
			} else if ((sense_key == SSD_KEY_RECOVERED_ERROR)
				&& (asc == 0x1f) && (ascq == 0x00)
				&& (returned_length > 0)) {
				/* Partial defect list transfer */
				/*
				 * Hitachi drives return this error
				 * along with a partial defect list if they
				 * have more defects than the 10 byte
				 * command can support.  Retry with the 12
				 * byte command.
				 */
				if (use_12byte == 0) {
					get_length = 1;
					use_12byte = 1;
					goto retry_12byte;
				}
				dlist_length = returned_length + hdr_size;
			} else if ((sense_key == SSD_KEY_ILLEGAL_REQUEST)
				&& (asc == 0x24) && (ascq == 0x00)) {
				/* Invalid field in CDB */
				/*
				 * SBC-3 says that if the drive has more
				 * defects than can be reported with the
				 * 10 byte command, it should return this
	 			 * error and no data.  Retry with the 12
				 * byte command.
				 */
				if (use_12byte == 0) {
					get_length = 1;
					use_12byte = 1;
					goto retry_12byte;
				}
				dlist_length = returned_length + hdr_size;
			} else {
				/*
				 * If we got a SCSI error and no valid length,
				 * just use the 10 byte maximum.  The 12
				 * byte maximum is too large.
				 */
				if (returned_length == 0)
					dlist_length = SRDD10_MAX_LENGTH;
				else {
					if ((use_12byte == 0)
					 && (returned_length >=
					     max_possible_size)) {
						get_length = 1;
						use_12byte = 1;
						goto retry_12byte;
					}
					dlist_length = returned_length +
					    hdr_size;
				}
			}
		} else if ((ccb->ccb_h.status & CAM_STATUS_MASK) !=
			    CAM_REQ_CMP){
			error = 1;
			warnx("Error reading defect header");
			if (arglist & CAM_ARG_VERBOSE)
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
			goto defect_bailout;
		} else {
			if ((use_12byte == 0)
			 && (returned_length >= max_possible_size)) {
				get_length = 1;
				use_12byte = 1;
				goto retry_12byte;
			}
			dlist_length = returned_length + hdr_size;
		}
		if (summary != 0) {
			fprintf(stdout, "%u", num_returned);
			if (quiet == 0) {
				fprintf(stdout, " defect%s",
					(num_returned != 1) ? "s" : "");
			}
			fprintf(stdout, "\n");

			goto defect_bailout;
		}

		/*
		 * We always limit the list length to the 10-byte maximum
		 * length (0xffff).  The reason is that some controllers
		 * can't handle larger I/Os, and we can transfer the entire
		 * 10 byte list in one shot.  For drives that support the 12
		 * byte read defects command, we'll step through the list
		 * by specifying a starting offset.  For drives that don't
		 * support the 12 byte command's starting offset, we'll
		 * just display the first 64K.
		 */
		dlist_length = min(dlist_length, SRDD10_MAX_LENGTH);

		goto retry;
	}


	if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_SCSI_STATUS_ERROR)
	 && (ccb->csio.scsi_status == SCSI_STATUS_CHECK_COND)
	 && ((ccb->ccb_h.status & CAM_AUTOSNS_VALID) != 0)) {
		struct scsi_sense_data *sense;
		int error_code, sense_key, asc, ascq;

		sense = &ccb->csio.sense_data;
		scsi_extract_sense_len(sense, ccb->csio.sense_len -
		    ccb->csio.sense_resid, &error_code, &sense_key, &asc,
		    &ascq, /*show_errors*/ 1);

		/*
		 * According to the SCSI spec, if the disk doesn't support
		 * the requested format, it will generally return a sense
		 * key of RECOVERED ERROR, and an additional sense code
		 * of "DEFECT LIST NOT FOUND".  HGST drives also return
		 * Primary/Grown defect list not found errors.  So just
		 * check for an ASC of 0x1c.
		 */
		if ((sense_key == SSD_KEY_RECOVERED_ERROR)
		 && (asc == 0x1c)) {
			const char *format_str;

			format_str = scsi_nv_to_str(defect_list_type_map,
			    sizeof(defect_list_type_map) /
			    sizeof(defect_list_type_map[0]),
			    list_format & SRDD10_DLIST_FORMAT_MASK);
			warnx("requested defect format %s not available",
			    format_str ? format_str : "unknown");

			format_str = scsi_nv_to_str(defect_list_type_map,
			    sizeof(defect_list_type_map) /
			    sizeof(defect_list_type_map[0]), returned_type);
			if (format_str != NULL) {
				warnx("Device returned %s format",
				    format_str);
			} else {
				error = 1;
				warnx("Device returned unknown defect"
				     " data format %#x", returned_type);
				goto defect_bailout;
			}
		} else {
			error = 1;
			warnx("Error returned from read defect data command");
			if (arglist & CAM_ARG_VERBOSE)
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
			goto defect_bailout;
		}
	} else if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		error = 1;
		warnx("Error returned from read defect data command");
		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		goto defect_bailout;
	}

	if (first_pass != 0) {
		fprintf(stderr, "Got %d defect", num_returned);

		if ((lists_specified == 0) || (num_returned == 0)) {
			fprintf(stderr, "s.\n");
			goto defect_bailout;
		} else if (num_returned == 1)
			fprintf(stderr, ":\n");
		else
			fprintf(stderr, "s:\n");

		first_pass = 0;
	}

	/*
	 * XXX KDM  I should probably clean up the printout format for the
	 * disk defects.
	 */
	switch (returned_type) {
	case SRDD10_PHYSICAL_SECTOR_FORMAT:
	case SRDD10_EXT_PHYS_FORMAT:
	{
		struct scsi_defect_desc_phys_sector *dlist;

		dlist = (struct scsi_defect_desc_phys_sector *)
			(defect_list + hdr_size);

		for (i = 0; i < num_valid; i++) {
			uint32_t sector;

			sector = scsi_4btoul(dlist[i].sector);
			if (returned_type == SRDD10_EXT_PHYS_FORMAT) {
				mads = (sector & SDD_EXT_PHYS_MADS) ?
				       0 : 1;
				sector &= ~SDD_EXT_PHYS_FLAG_MASK;
			}
			if (hex_format == 0)
				fprintf(stdout, "%d:%d:%d%s",
					scsi_3btoul(dlist[i].cylinder),
					dlist[i].head,
					scsi_4btoul(dlist[i].sector),
					mads ? " - " : "\n");
			else
				fprintf(stdout, "0x%x:0x%x:0x%x%s",
					scsi_3btoul(dlist[i].cylinder),
					dlist[i].head,
					scsi_4btoul(dlist[i].sector),
					mads ? " - " : "\n");
			mads = 0;
		}
		if (num_valid < num_returned) {
			starting_offset += num_valid;
			goto next_batch;
		}
		break;
	}
	case SRDD10_BYTES_FROM_INDEX_FORMAT:
	case SRDD10_EXT_BFI_FORMAT:
	{
		struct scsi_defect_desc_bytes_from_index *dlist;

		dlist = (struct scsi_defect_desc_bytes_from_index *)
			(defect_list + hdr_size);

		for (i = 0; i < num_valid; i++) {
			uint32_t bfi;

			bfi = scsi_4btoul(dlist[i].bytes_from_index);
			if (returned_type == SRDD10_EXT_BFI_FORMAT) {
				mads = (bfi & SDD_EXT_BFI_MADS) ? 1 : 0;
				bfi &= ~SDD_EXT_BFI_FLAG_MASK;
			}
			if (hex_format == 0)
				fprintf(stdout, "%d:%d:%d%s",
					scsi_3btoul(dlist[i].cylinder),
					dlist[i].head,
					scsi_4btoul(dlist[i].bytes_from_index),
					mads ? " - " : "\n");
			else
				fprintf(stdout, "0x%x:0x%x:0x%x%s",
					scsi_3btoul(dlist[i].cylinder),
					dlist[i].head,
					scsi_4btoul(dlist[i].bytes_from_index),
					mads ? " - " : "\n");

			mads = 0;
		}
		if (num_valid < num_returned) {
			starting_offset += num_valid;
			goto next_batch;
		}
		break;
	}
	case SRDDH10_BLOCK_FORMAT:
	{
		struct scsi_defect_desc_block *dlist;

		dlist = (struct scsi_defect_desc_block *)
			(defect_list + hdr_size);

		for (i = 0; i < num_valid; i++) {
			if (hex_format == 0)
				fprintf(stdout, "%u\n",
					scsi_4btoul(dlist[i].address));
			else
				fprintf(stdout, "0x%x\n",
					scsi_4btoul(dlist[i].address));
		}

		if (num_valid < num_returned) {
			starting_offset += num_valid;
			goto next_batch;
		}

		break;
	}
	case SRDD10_LONG_BLOCK_FORMAT:
	{
		struct scsi_defect_desc_long_block *dlist;

		dlist = (struct scsi_defect_desc_long_block *)
			(defect_list + hdr_size);

		for (i = 0; i < num_valid; i++) {
			if (hex_format == 0)
				fprintf(stdout, "%ju\n",
					(uintmax_t)scsi_8btou64(
					dlist[i].address));
			else
				fprintf(stdout, "0x%jx\n",
					(uintmax_t)scsi_8btou64(
					dlist[i].address));
		}

		if (num_valid < num_returned) {
			starting_offset += num_valid;
			goto next_batch;
		}
		break;
	}
	default:
		fprintf(stderr, "Unknown defect format 0x%x\n",
			returned_type);
		error = 1;
		break;
	}
defect_bailout:

	if (defect_list != NULL)
		free(defect_list);

	if (ccb != NULL)
		cam_freeccb(ccb);

	return (error);
}
#endif /* MINIMALISTIC */

#if 0
void
reassignblocks(struct cam_device *device, u_int32_t *blocks, int num_blocks)
{
	union ccb *ccb;

	ccb = cam_getccb(device);

	cam_freeccb(ccb);
}
#endif

#ifndef MINIMALISTIC
void
mode_sense(struct cam_device *device, int dbd, int pc, int page, int subpage,
	   int task_attr, int retry_count, int timeout, u_int8_t *data,
	   int datalen)
{
	union ccb *ccb;
	int retval;

	ccb = cam_getccb(device);

	if (ccb == NULL)
		errx(1, "mode_sense: couldn't allocate CCB");

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	scsi_mode_sense_subpage(&ccb->csio,
			/* retries */ retry_count,
			/* cbfcnp */ NULL,
			/* tag_action */ task_attr,
			/* dbd */ dbd,
			/* pc */ pc << 6,
			/* page */ page,
			/* subpage */ subpage,
			/* param_buf */ data,
			/* param_len */ datalen,
			/* minimum_cmd_size */ 0,
			/* sense_len */ SSD_FULL_SIZE,
			/* timeout */ timeout ? timeout : 5000);

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
		cam_freeccb(ccb);
		cam_close_device(device);
		if (retval < 0)
			err(1, "error sending mode sense command");
		else
			errx(1, "error sending mode sense command");
	}

	cam_freeccb(ccb);
}

void
mode_select(struct cam_device *device, int save_pages, int task_attr,
	    int retry_count, int timeout, u_int8_t *data, int datalen)
{
	union ccb *ccb;
	int retval;

	ccb = cam_getccb(device);

	if (ccb == NULL)
		errx(1, "mode_select: couldn't allocate CCB");

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	scsi_mode_select(&ccb->csio,
			 /* retries */ retry_count,
			 /* cbfcnp */ NULL,
			 /* tag_action */ task_attr,
			 /* scsi_page_fmt */ 1,
			 /* save_pages */ save_pages,
			 /* param_buf */ data,
			 /* param_len */ datalen,
			 /* sense_len */ SSD_FULL_SIZE,
			 /* timeout */ timeout ? timeout : 5000);

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
		cam_freeccb(ccb);
		cam_close_device(device);

		if (retval < 0)
			err(1, "error sending mode select command");
		else
			errx(1, "error sending mode select command");

	}

	cam_freeccb(ccb);
}

void
modepage(struct cam_device *device, int argc, char **argv, char *combinedopt,
	 int task_attr, int retry_count, int timeout)
{
	char *str_subpage;
	int c, page = -1, subpage = -1, pc = 0;
	int binary = 0, dbd = 0, edit = 0, list = 0;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c) {
		case 'b':
			binary = 1;
			break;
		case 'd':
			dbd = 1;
			break;
		case 'e':
			edit = 1;
			break;
		case 'l':
			list++;
			break;
		case 'm':
			str_subpage = optarg;
			strsep(&str_subpage, ",");
			page = strtol(optarg, NULL, 0);
			if (str_subpage)
			    subpage = strtol(str_subpage, NULL, 0);
			else
			    subpage = 0;
			if (page < 0)
				errx(1, "invalid mode page %d", page);
			if (subpage < 0)
				errx(1, "invalid mode subpage %d", subpage);
			break;
		case 'P':
			pc = strtol(optarg, NULL, 0);
			if ((pc < 0) || (pc > 3))
				errx(1, "invalid page control field %d", pc);
			break;
		default:
			break;
		}
	}

	if (page == -1 && list == 0)
		errx(1, "you must specify a mode page!");

	if (list != 0) {
		mode_list(device, dbd, pc, list > 1, task_attr, retry_count,
			  timeout);
	} else {
		mode_edit(device, dbd, pc, page, subpage, edit, binary,
		    task_attr, retry_count, timeout);
	}
}

static int
scsicmd(struct cam_device *device, int argc, char **argv, char *combinedopt,
	int task_attr, int retry_count, int timeout)
{
	union ccb *ccb;
	u_int32_t flags = CAM_DIR_NONE;
	u_int8_t *data_ptr = NULL;
	u_int8_t cdb[20];
	u_int8_t atacmd[12];
	struct get_hook hook;
	int c, data_bytes = 0, valid_bytes;
	int cdb_len = 0;
	int atacmd_len = 0;
	int dmacmd = 0;
	int fpdmacmd = 0;
	int need_res = 0;
	char *datastr = NULL, *tstr, *resstr = NULL;
	int error = 0;
	int fd_data = 0, fd_res = 0;
	int retval;

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("scsicmd: error allocating ccb");
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(ccb);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c) {
		case 'a':
			tstr = optarg;
			while (isspace(*tstr) && (*tstr != '\0'))
				tstr++;
			hook.argc = argc - optind;
			hook.argv = argv + optind;
			hook.got = 0;
			atacmd_len = buff_encode_visit(atacmd, sizeof(atacmd), tstr,
						    iget, &hook);
			/*
			 * Increment optind by the number of arguments the
			 * encoding routine processed.  After each call to
			 * getopt(3), optind points to the argument that
			 * getopt should process _next_.  In this case,
			 * that means it points to the first command string
			 * argument, if there is one.  Once we increment
			 * this, it should point to either the next command
			 * line argument, or it should be past the end of
			 * the list.
			 */
			optind += hook.got;
			break;
		case 'c':
			tstr = optarg;
			while (isspace(*tstr) && (*tstr != '\0'))
				tstr++;
			hook.argc = argc - optind;
			hook.argv = argv + optind;
			hook.got = 0;
			cdb_len = buff_encode_visit(cdb, sizeof(cdb), tstr,
						    iget, &hook);
			/*
			 * Increment optind by the number of arguments the
			 * encoding routine processed.  After each call to
			 * getopt(3), optind points to the argument that
			 * getopt should process _next_.  In this case,
			 * that means it points to the first command string
			 * argument, if there is one.  Once we increment
			 * this, it should point to either the next command
			 * line argument, or it should be past the end of
			 * the list.
			 */
			optind += hook.got;
			break;
		case 'd':
			dmacmd = 1;
			break;
		case 'f':
			fpdmacmd = 1;
			break;
		case 'i':
			if (arglist & CAM_ARG_CMD_OUT) {
				warnx("command must either be "
				      "read or write, not both");
				error = 1;
				goto scsicmd_bailout;
			}
			arglist |= CAM_ARG_CMD_IN;
			flags = CAM_DIR_IN;
			data_bytes = strtol(optarg, NULL, 0);
			if (data_bytes <= 0) {
				warnx("invalid number of input bytes %d",
				      data_bytes);
				error = 1;
				goto scsicmd_bailout;
			}
			hook.argc = argc - optind;
			hook.argv = argv + optind;
			hook.got = 0;
			optind++;
			datastr = cget(&hook, NULL);
			/*
			 * If the user supplied "-" instead of a format, he
			 * wants the data to be written to stdout.
			 */
			if ((datastr != NULL)
			 && (datastr[0] == '-'))
				fd_data = 1;

			data_ptr = (u_int8_t *)malloc(data_bytes);
			if (data_ptr == NULL) {
				warnx("can't malloc memory for data_ptr");
				error = 1;
				goto scsicmd_bailout;
			}
			break;
		case 'o':
			if (arglist & CAM_ARG_CMD_IN) {
				warnx("command must either be "
				      "read or write, not both");
				error = 1;
				goto scsicmd_bailout;
			}
			arglist |= CAM_ARG_CMD_OUT;
			flags = CAM_DIR_OUT;
			data_bytes = strtol(optarg, NULL, 0);
			if (data_bytes <= 0) {
				warnx("invalid number of output bytes %d",
				      data_bytes);
				error = 1;
				goto scsicmd_bailout;
			}
			hook.argc = argc - optind;
			hook.argv = argv + optind;
			hook.got = 0;
			datastr = cget(&hook, NULL);
			data_ptr = (u_int8_t *)malloc(data_bytes);
			if (data_ptr == NULL) {
				warnx("can't malloc memory for data_ptr");
				error = 1;
				goto scsicmd_bailout;
			}
			bzero(data_ptr, data_bytes);
			/*
			 * If the user supplied "-" instead of a format, he
			 * wants the data to be read from stdin.
			 */
			if ((datastr != NULL)
			 && (datastr[0] == '-'))
				fd_data = 1;
			else
				buff_encode_visit(data_ptr, data_bytes, datastr,
						  iget, &hook);
			optind += hook.got;
			break;
		case 'r':
			need_res = 1;
			hook.argc = argc - optind;
			hook.argv = argv + optind;
			hook.got = 0;
			resstr = cget(&hook, NULL);
			if ((resstr != NULL) && (resstr[0] == '-'))
				fd_res = 1;
			optind += hook.got;
			break;
		default:
			break;
		}
	}

	/*
	 * If fd_data is set, and we're writing to the device, we need to
	 * read the data the user wants written from stdin.
	 */
	if ((fd_data == 1) && (arglist & CAM_ARG_CMD_OUT)) {
		ssize_t amt_read;
		int amt_to_read = data_bytes;
		u_int8_t *buf_ptr = data_ptr;

		for (amt_read = 0; amt_to_read > 0;
		     amt_read = read(STDIN_FILENO, buf_ptr, amt_to_read)) {
			if (amt_read == -1) {
				warn("error reading data from stdin");
				error = 1;
				goto scsicmd_bailout;
			}
			amt_to_read -= amt_read;
			buf_ptr += amt_read;
		}
	}

	if (arglist & CAM_ARG_ERR_RECOVER)
		flags |= CAM_PASS_ERR_RECOVER;

	/* Disable freezing the device queue */
	flags |= CAM_DEV_QFRZDIS;

	if (cdb_len) {
		/*
		 * This is taken from the SCSI-3 draft spec.
		 * (T10/1157D revision 0.3)
		 * The top 3 bits of an opcode are the group code.
		 * The next 5 bits are the command code.
		 * Group 0:  six byte commands
		 * Group 1:  ten byte commands
		 * Group 2:  ten byte commands
		 * Group 3:  reserved
		 * Group 4:  sixteen byte commands
		 * Group 5:  twelve byte commands
		 * Group 6:  vendor specific
		 * Group 7:  vendor specific
		 */
		switch((cdb[0] >> 5) & 0x7) {
			case 0:
				cdb_len = 6;
				break;
			case 1:
			case 2:
				cdb_len = 10;
				break;
			case 3:
			case 6:
			case 7:
				/* computed by buff_encode_visit */
				break;
			case 4:
				cdb_len = 16;
				break;
			case 5:
				cdb_len = 12;
				break;
		}

		/*
		 * We should probably use csio_build_visit or something like that
		 * here, but it's easier to encode arguments as you go.  The
		 * alternative would be skipping the CDB argument and then encoding
		 * it here, since we've got the data buffer argument by now.
		 */
		bcopy(cdb, &ccb->csio.cdb_io.cdb_bytes, cdb_len);

		cam_fill_csio(&ccb->csio,
		      /*retries*/ retry_count,
		      /*cbfcnp*/ NULL,
		      /*flags*/ flags,
		      /*tag_action*/ task_attr,
		      /*data_ptr*/ data_ptr,
		      /*dxfer_len*/ data_bytes,
		      /*sense_len*/ SSD_FULL_SIZE,
		      /*cdb_len*/ cdb_len,
		      /*timeout*/ timeout ? timeout : 5000);
	} else {
		atacmd_len = 12;
		bcopy(atacmd, &ccb->ataio.cmd.command, atacmd_len);
		if (need_res)
			ccb->ataio.cmd.flags |= CAM_ATAIO_NEEDRESULT;
		if (dmacmd)
			ccb->ataio.cmd.flags |= CAM_ATAIO_DMA;
		if (fpdmacmd)
			ccb->ataio.cmd.flags |= CAM_ATAIO_FPDMA;

		cam_fill_ataio(&ccb->ataio,
		      /*retries*/ retry_count,
		      /*cbfcnp*/ NULL,
		      /*flags*/ flags,
		      /*tag_action*/ 0,
		      /*data_ptr*/ data_ptr,
		      /*dxfer_len*/ data_bytes,
		      /*timeout*/ timeout ? timeout : 5000);
	}

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		const char warnstr[] = "error sending command";

		if (retval < 0)
			warn(warnstr);
		else
			warnx(warnstr);

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		error = 1;
		goto scsicmd_bailout;
	}

	if (atacmd_len && need_res) {
		if (fd_res == 0) {
			buff_decode_visit(&ccb->ataio.res.status, 11, resstr,
					  arg_put, NULL);
			fprintf(stdout, "\n");
		} else {
			fprintf(stdout,
			    "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
			    ccb->ataio.res.status,
			    ccb->ataio.res.error,
			    ccb->ataio.res.lba_low,
			    ccb->ataio.res.lba_mid,
			    ccb->ataio.res.lba_high,
			    ccb->ataio.res.device,
			    ccb->ataio.res.lba_low_exp,
			    ccb->ataio.res.lba_mid_exp,
			    ccb->ataio.res.lba_high_exp,
			    ccb->ataio.res.sector_count,
			    ccb->ataio.res.sector_count_exp);
			fflush(stdout);
		}
	}

	if (cdb_len)
		valid_bytes = ccb->csio.dxfer_len - ccb->csio.resid;
	else
		valid_bytes = ccb->ataio.dxfer_len - ccb->ataio.resid;
	if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
	 && (arglist & CAM_ARG_CMD_IN)
	 && (valid_bytes > 0)) {
		if (fd_data == 0) {
			buff_decode_visit(data_ptr, valid_bytes, datastr,
					  arg_put, NULL);
			fprintf(stdout, "\n");
		} else {
			ssize_t amt_written;
			int amt_to_write = valid_bytes;
			u_int8_t *buf_ptr = data_ptr;

			for (amt_written = 0; (amt_to_write > 0) &&
			     (amt_written =write(1, buf_ptr,amt_to_write))> 0;){
				amt_to_write -= amt_written;
				buf_ptr += amt_written;
			}
			if (amt_written == -1) {
				warn("error writing data to stdout");
				error = 1;
				goto scsicmd_bailout;
			} else if ((amt_written == 0)
				&& (amt_to_write > 0)) {
				warnx("only wrote %u bytes out of %u",
				      valid_bytes - amt_to_write, valid_bytes);
			}
		}
	}

scsicmd_bailout:

	if ((data_bytes > 0) && (data_ptr != NULL))
		free(data_ptr);

	cam_freeccb(ccb);

	return (error);
}

static int
camdebug(int argc, char **argv, char *combinedopt)
{
	int c, fd;
	path_id_t bus = CAM_BUS_WILDCARD;
	target_id_t target = CAM_TARGET_WILDCARD;
	lun_id_t lun = CAM_LUN_WILDCARD;
	char *tstr, *tmpstr = NULL;
	union ccb ccb;
	int error = 0;

	bzero(&ccb, sizeof(union ccb));

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c) {
		case 'I':
			arglist |= CAM_ARG_DEBUG_INFO;
			ccb.cdbg.flags |= CAM_DEBUG_INFO;
			break;
		case 'P':
			arglist |= CAM_ARG_DEBUG_PERIPH;
			ccb.cdbg.flags |= CAM_DEBUG_PERIPH;
			break;
		case 'S':
			arglist |= CAM_ARG_DEBUG_SUBTRACE;
			ccb.cdbg.flags |= CAM_DEBUG_SUBTRACE;
			break;
		case 'T':
			arglist |= CAM_ARG_DEBUG_TRACE;
			ccb.cdbg.flags |= CAM_DEBUG_TRACE;
			break;
		case 'X':
			arglist |= CAM_ARG_DEBUG_XPT;
			ccb.cdbg.flags |= CAM_DEBUG_XPT;
			break;
		case 'c':
			arglist |= CAM_ARG_DEBUG_CDB;
			ccb.cdbg.flags |= CAM_DEBUG_CDB;
			break;
		case 'p':
			arglist |= CAM_ARG_DEBUG_PROBE;
			ccb.cdbg.flags |= CAM_DEBUG_PROBE;
			break;
		default:
			break;
		}
	}

	if ((fd = open(XPT_DEVICE, O_RDWR)) < 0) {
		warnx("error opening transport layer device %s", XPT_DEVICE);
		warn("%s", XPT_DEVICE);
		return (1);
	}
	argc -= optind;
	argv += optind;

	if (argc <= 0) {
		warnx("you must specify \"off\", \"all\" or a bus,");
		warnx("bus:target, or bus:target:lun");
		close(fd);
		return (1);
	}

	tstr = *argv;

	while (isspace(*tstr) && (*tstr != '\0'))
		tstr++;

	if (strncmp(tstr, "off", 3) == 0) {
		ccb.cdbg.flags = CAM_DEBUG_NONE;
		arglist &= ~(CAM_ARG_DEBUG_INFO|CAM_ARG_DEBUG_PERIPH|
			     CAM_ARG_DEBUG_TRACE|CAM_ARG_DEBUG_SUBTRACE|
			     CAM_ARG_DEBUG_XPT|CAM_ARG_DEBUG_PROBE);
	} else if (strncmp(tstr, "all", 3) != 0) {
		tmpstr = (char *)strtok(tstr, ":");
		if ((tmpstr != NULL) && (*tmpstr != '\0')){
			bus = strtol(tmpstr, NULL, 0);
			arglist |= CAM_ARG_BUS;
			tmpstr = (char *)strtok(NULL, ":");
			if ((tmpstr != NULL) && (*tmpstr != '\0')){
				target = strtol(tmpstr, NULL, 0);
				arglist |= CAM_ARG_TARGET;
				tmpstr = (char *)strtok(NULL, ":");
				if ((tmpstr != NULL) && (*tmpstr != '\0')){
					lun = strtol(tmpstr, NULL, 0);
					arglist |= CAM_ARG_LUN;
				}
			}
		} else {
			error = 1;
			warnx("you must specify \"all\", \"off\", or a bus,");
			warnx("bus:target, or bus:target:lun to debug");
		}
	}

	if (error == 0) {

		ccb.ccb_h.func_code = XPT_DEBUG;
		ccb.ccb_h.path_id = bus;
		ccb.ccb_h.target_id = target;
		ccb.ccb_h.target_lun = lun;

		if (ioctl(fd, CAMIOCOMMAND, &ccb) == -1) {
			warn("CAMIOCOMMAND ioctl failed");
			error = 1;
		}

		if (error == 0) {
			if ((ccb.ccb_h.status & CAM_STATUS_MASK) ==
			     CAM_FUNC_NOTAVAIL) {
				warnx("CAM debugging not available");
				warnx("you need to put options CAMDEBUG in"
				      " your kernel config file!");
				error = 1;
			} else if ((ccb.ccb_h.status & CAM_STATUS_MASK) !=
				    CAM_REQ_CMP) {
				warnx("XPT_DEBUG CCB failed with status %#x",
				      ccb.ccb_h.status);
				error = 1;
			} else {
				if (ccb.cdbg.flags == CAM_DEBUG_NONE) {
					fprintf(stderr,
						"Debugging turned off\n");
				} else {
					fprintf(stderr,
						"Debugging enabled for "
						"%d:%d:%jx\n",
						bus, target, (uintmax_t)lun);
				}
			}
		}
		close(fd);
	}

	return (error);
}

static int
tagcontrol(struct cam_device *device, int argc, char **argv,
	   char *combinedopt)
{
	int c;
	union ccb *ccb;
	int numtags = -1;
	int retval = 0;
	int quiet = 0;
	char pathstr[1024];

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("tagcontrol: error allocating ccb");
		return (1);
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c) {
		case 'N':
			numtags = strtol(optarg, NULL, 0);
			if (numtags < 0) {
				warnx("tag count %d is < 0", numtags);
				retval = 1;
				goto tagcontrol_bailout;
			}
			break;
		case 'q':
			quiet++;
			break;
		default:
			break;
		}
	}

	cam_path_string(device, pathstr, sizeof(pathstr));

	if (numtags >= 0) {
		CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->crs);
		ccb->ccb_h.func_code = XPT_REL_SIMQ;
		ccb->ccb_h.flags = CAM_DEV_QFREEZE;
		ccb->crs.release_flags = RELSIM_ADJUST_OPENINGS;
		ccb->crs.openings = numtags;


		if (cam_send_ccb(device, ccb) < 0) {
			perror("error sending XPT_REL_SIMQ CCB");
			retval = 1;
			goto tagcontrol_bailout;
		}

		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			warnx("XPT_REL_SIMQ CCB failed");
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
			retval = 1;
			goto tagcontrol_bailout;
		}


		if (quiet == 0)
			fprintf(stdout, "%stagged openings now %d\n",
				pathstr, ccb->crs.openings);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cgds);

	ccb->ccb_h.func_code = XPT_GDEV_STATS;

	if (cam_send_ccb(device, ccb) < 0) {
		perror("error sending XPT_GDEV_STATS CCB");
		retval = 1;
		goto tagcontrol_bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		warnx("XPT_GDEV_STATS CCB failed");
		cam_error_print(device, ccb, CAM_ESF_ALL,
				CAM_EPF_ALL, stderr);
		retval = 1;
		goto tagcontrol_bailout;
	}

	if (arglist & CAM_ARG_VERBOSE) {
		fprintf(stdout, "%s", pathstr);
		fprintf(stdout, "dev_openings  %d\n", ccb->cgds.dev_openings);
		fprintf(stdout, "%s", pathstr);
		fprintf(stdout, "dev_active    %d\n", ccb->cgds.dev_active);
		fprintf(stdout, "%s", pathstr);
		fprintf(stdout, "allocated     %d\n", ccb->cgds.allocated);
		fprintf(stdout, "%s", pathstr);
		fprintf(stdout, "queued        %d\n", ccb->cgds.queued);
		fprintf(stdout, "%s", pathstr);
		fprintf(stdout, "held          %d\n", ccb->cgds.held);
		fprintf(stdout, "%s", pathstr);
		fprintf(stdout, "mintags       %d\n", ccb->cgds.mintags);
		fprintf(stdout, "%s", pathstr);
		fprintf(stdout, "maxtags       %d\n", ccb->cgds.maxtags);
	} else {
		if (quiet == 0) {
			fprintf(stdout, "%s", pathstr);
			fprintf(stdout, "device openings: ");
		}
		fprintf(stdout, "%d\n", ccb->cgds.dev_openings +
			ccb->cgds.dev_active);
	}

tagcontrol_bailout:

	cam_freeccb(ccb);
	return (retval);
}

static void
cts_print(struct cam_device *device, struct ccb_trans_settings *cts)
{
	char pathstr[1024];

	cam_path_string(device, pathstr, sizeof(pathstr));

	if (cts->transport == XPORT_SPI) {
		struct ccb_trans_settings_spi *spi =
		    &cts->xport_specific.spi;

		if ((spi->valid & CTS_SPI_VALID_SYNC_RATE) != 0) {

			fprintf(stdout, "%ssync parameter: %d\n", pathstr,
				spi->sync_period);

			if (spi->sync_offset != 0) {
				u_int freq;

				freq = scsi_calc_syncsrate(spi->sync_period);
				fprintf(stdout, "%sfrequency: %d.%03dMHz\n",
					pathstr, freq / 1000, freq % 1000);
			}
		}

		if (spi->valid & CTS_SPI_VALID_SYNC_OFFSET) {
			fprintf(stdout, "%soffset: %d\n", pathstr,
			    spi->sync_offset);
		}

		if (spi->valid & CTS_SPI_VALID_BUS_WIDTH) {
			fprintf(stdout, "%sbus width: %d bits\n", pathstr,
				(0x01 << spi->bus_width) * 8);
		}

		if (spi->valid & CTS_SPI_VALID_DISC) {
			fprintf(stdout, "%sdisconnection is %s\n", pathstr,
				(spi->flags & CTS_SPI_FLAGS_DISC_ENB) ?
				"enabled" : "disabled");
		}
	}
	if (cts->transport == XPORT_FC) {
		struct ccb_trans_settings_fc *fc =
		    &cts->xport_specific.fc;

		if (fc->valid & CTS_FC_VALID_WWNN)
			fprintf(stdout, "%sWWNN: 0x%llx\n", pathstr,
			    (long long) fc->wwnn);
		if (fc->valid & CTS_FC_VALID_WWPN)
			fprintf(stdout, "%sWWPN: 0x%llx\n", pathstr,
			    (long long) fc->wwpn);
		if (fc->valid & CTS_FC_VALID_PORT)
			fprintf(stdout, "%sPortID: 0x%x\n", pathstr, fc->port);
		if (fc->valid & CTS_FC_VALID_SPEED)
			fprintf(stdout, "%stransfer speed: %d.%03dMB/s\n",
			    pathstr, fc->bitrate / 1000, fc->bitrate % 1000);
	}
	if (cts->transport == XPORT_SAS) {
		struct ccb_trans_settings_sas *sas =
		    &cts->xport_specific.sas;

		if (sas->valid & CTS_SAS_VALID_SPEED)
			fprintf(stdout, "%stransfer speed: %d.%03dMB/s\n",
			    pathstr, sas->bitrate / 1000, sas->bitrate % 1000);
	}
	if (cts->transport == XPORT_ATA) {
		struct ccb_trans_settings_pata *pata =
		    &cts->xport_specific.ata;

		if ((pata->valid & CTS_ATA_VALID_MODE) != 0) {
			fprintf(stdout, "%sATA mode: %s\n", pathstr,
				ata_mode2string(pata->mode));
		}
		if ((pata->valid & CTS_ATA_VALID_ATAPI) != 0) {
			fprintf(stdout, "%sATAPI packet length: %d\n", pathstr,
				pata->atapi);
		}
		if ((pata->valid & CTS_ATA_VALID_BYTECOUNT) != 0) {
			fprintf(stdout, "%sPIO transaction length: %d\n",
				pathstr, pata->bytecount);
		}
	}
	if (cts->transport == XPORT_SATA) {
		struct ccb_trans_settings_sata *sata =
		    &cts->xport_specific.sata;

		if ((sata->valid & CTS_SATA_VALID_REVISION) != 0) {
			fprintf(stdout, "%sSATA revision: %d.x\n", pathstr,
				sata->revision);
		}
		if ((sata->valid & CTS_SATA_VALID_MODE) != 0) {
			fprintf(stdout, "%sATA mode: %s\n", pathstr,
				ata_mode2string(sata->mode));
		}
		if ((sata->valid & CTS_SATA_VALID_ATAPI) != 0) {
			fprintf(stdout, "%sATAPI packet length: %d\n", pathstr,
				sata->atapi);
		}
		if ((sata->valid & CTS_SATA_VALID_BYTECOUNT) != 0) {
			fprintf(stdout, "%sPIO transaction length: %d\n",
				pathstr, sata->bytecount);
		}
		if ((sata->valid & CTS_SATA_VALID_PM) != 0) {
			fprintf(stdout, "%sPMP presence: %d\n", pathstr,
				sata->pm_present);
		}
		if ((sata->valid & CTS_SATA_VALID_TAGS) != 0) {
			fprintf(stdout, "%sNumber of tags: %d\n", pathstr,
				sata->tags);
		}
		if ((sata->valid & CTS_SATA_VALID_CAPS) != 0) {
			fprintf(stdout, "%sSATA capabilities: %08x\n", pathstr,
				sata->caps);
		}
	}
	if (cts->protocol == PROTO_ATA) {
		struct ccb_trans_settings_ata *ata=
		    &cts->proto_specific.ata;

		if (ata->valid & CTS_ATA_VALID_TQ) {
			fprintf(stdout, "%stagged queueing: %s\n", pathstr,
				(ata->flags & CTS_ATA_FLAGS_TAG_ENB) ?
				"enabled" : "disabled");
		}
	}
	if (cts->protocol == PROTO_SCSI) {
		struct ccb_trans_settings_scsi *scsi=
		    &cts->proto_specific.scsi;

		if (scsi->valid & CTS_SCSI_VALID_TQ) {
			fprintf(stdout, "%stagged queueing: %s\n", pathstr,
				(scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) ?
				"enabled" : "disabled");
		}
	}
#ifdef WITH_NVME
	if (cts->protocol == PROTO_NVME) {
		struct ccb_trans_settings_nvme *nvmex =
		    &cts->xport_specific.nvme;

		if (nvmex->valid & CTS_NVME_VALID_SPEC) {
			fprintf(stdout, "%sNVMe Spec: %d.%d\n", pathstr,
			    NVME_MAJOR(nvmex->spec),
			    NVME_MINOR(nvmex->spec));
		}
		if (nvmex->valid & CTS_NVME_VALID_LINK) {
			fprintf(stdout, "%sPCIe lanes: %d (%d max)\n", pathstr,
			    nvmex->lanes, nvmex->max_lanes);
			fprintf(stdout, "%sPCIe Generation: %d (%d max)\n", pathstr,
			    nvmex->speed, nvmex->max_speed);
		}
	}
#endif
}

/*
 * Get a path inquiry CCB for the specified device.
 */
static int
get_cpi(struct cam_device *device, struct ccb_pathinq *cpi)
{
	union ccb *ccb;
	int retval = 0;

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("get_cpi: couldn't allocate CCB");
		return (1);
	}
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cpi);
	ccb->ccb_h.func_code = XPT_PATH_INQ;
	if (cam_send_ccb(device, ccb) < 0) {
		warn("get_cpi: error sending Path Inquiry CCB");
		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		retval = 1;
		goto get_cpi_bailout;
	}
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		retval = 1;
		goto get_cpi_bailout;
	}
	bcopy(&ccb->cpi, cpi, sizeof(struct ccb_pathinq));

get_cpi_bailout:
	cam_freeccb(ccb);
	return (retval);
}

/*
 * Get a get device CCB for the specified device.
 */
static int
get_cgd(struct cam_device *device, struct ccb_getdev *cgd)
{
	union ccb *ccb;
	int retval = 0;

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("get_cgd: couldn't allocate CCB");
		return (1);
	}
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cgd);
	ccb->ccb_h.func_code = XPT_GDEV_TYPE;
	if (cam_send_ccb(device, ccb) < 0) {
		warn("get_cgd: error sending Path Inquiry CCB");
		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		retval = 1;
		goto get_cgd_bailout;
	}
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		retval = 1;
		goto get_cgd_bailout;
	}
	bcopy(&ccb->cgd, cgd, sizeof(struct ccb_getdev));

get_cgd_bailout:
	cam_freeccb(ccb);
	return (retval);
}

/*
 * Returns 1 if the device has the VPD page, 0 if it does not, and -1 on an
 * error.
 */
int
dev_has_vpd_page(struct cam_device *dev, uint8_t page_id, int retry_count,
		 int timeout, int verbosemode)
{
	union ccb *ccb = NULL;
	struct scsi_vpd_supported_page_list sup_pages;
	int i;
	int retval = 0;

	ccb = cam_getccb(dev);
	if (ccb == NULL) {
		warn("Unable to allocate CCB");
		retval = -1;
		goto bailout;
	}

	/* cam_getccb cleans up the header, caller has to zero the payload */
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	bzero(&sup_pages, sizeof(sup_pages));

	scsi_inquiry(&ccb->csio,
		     /*retries*/ retry_count,
		     /*cbfcnp*/ NULL,
		     /* tag_action */ MSG_SIMPLE_Q_TAG,
		     /* inq_buf */ (u_int8_t *)&sup_pages,
		     /* inq_len */ sizeof(sup_pages),
		     /* evpd */ 1,
		     /* page_code */ SVPD_SUPPORTED_PAGE_LIST,
		     /* sense_len */ SSD_FULL_SIZE,
		     /* timeout */ timeout ? timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (retry_count != 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(dev, ccb) < 0) {
		cam_freeccb(ccb);
		ccb = NULL;
		retval = -1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if (verbosemode != 0)
			cam_error_print(dev, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		retval = -1;
		goto bailout;
	}

	for (i = 0; i < sup_pages.length; i++) {
		if (sup_pages.list[i] == page_id) {
			retval = 1;
			goto bailout;
		}
	}
bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	return (retval);
}

/*
 * devtype is filled in with the type of device.
 * Returns 0 for success, non-zero for failure.
 */
int
get_device_type(struct cam_device *dev, int retry_count, int timeout,
		    int verbosemode, camcontrol_devtype *devtype)
{
	struct ccb_getdev cgd;
	int retval = 0;

	retval = get_cgd(dev, &cgd);
	if (retval != 0)
		goto bailout;

	switch (cgd.protocol) {
	case PROTO_SCSI:
		break;
	case PROTO_ATA:
	case PROTO_ATAPI:
	case PROTO_SATAPM:
		*devtype = CC_DT_ATA;
		goto bailout;
		break; /*NOTREACHED*/
	default:
		*devtype = CC_DT_UNKNOWN;
		goto bailout;
		break; /*NOTREACHED*/
	}

	/*
	 * Check for the ATA Information VPD page (0x89).  If this is an
	 * ATA device behind a SCSI to ATA translation layer, this VPD page
	 * should be present.
	 *
	 * If that VPD page isn't present, or we get an error back from the
	 * INQUIRY command, we'll just treat it as a normal SCSI device.
	 */
	retval = dev_has_vpd_page(dev, SVPD_ATA_INFORMATION, retry_count,
				  timeout, verbosemode);
	if (retval == 1)
		*devtype = CC_DT_ATA_BEHIND_SCSI;
	else
		*devtype = CC_DT_SCSI;

	retval = 0;

bailout:
	return (retval);
}

int
build_ata_cmd(union ccb *ccb, uint32_t retry_count, uint32_t flags,
    uint8_t tag_action, uint8_t protocol, uint8_t ata_flags, uint16_t features,
    uint16_t sector_count, uint64_t lba, uint8_t command, uint32_t auxiliary,
    uint8_t *data_ptr, uint32_t dxfer_len, uint8_t *cdb_storage,
    size_t cdb_storage_len, uint8_t sense_len, uint32_t timeout,
    int is48bit, camcontrol_devtype devtype)
{
	int retval = 0;

	if (devtype == CC_DT_ATA) {
		cam_fill_ataio(&ccb->ataio,
		    /*retries*/ retry_count,
		    /*cbfcnp*/ NULL,
		    /*flags*/ flags,
		    /*tag_action*/ tag_action,
		    /*data_ptr*/ data_ptr,
		    /*dxfer_len*/ dxfer_len,
		    /*timeout*/ timeout);
		if (is48bit || lba > ATA_MAX_28BIT_LBA)
			ata_48bit_cmd(&ccb->ataio, command, features, lba,
			    sector_count);
		else
			ata_28bit_cmd(&ccb->ataio, command, features, lba,
			    sector_count);

		if (auxiliary != 0) {
			ccb->ataio.ata_flags |= ATA_FLAG_AUX;
			ccb->ataio.aux = auxiliary;
		}

		if (ata_flags & AP_FLAG_CHK_COND)
			ccb->ataio.cmd.flags |= CAM_ATAIO_NEEDRESULT;

		if ((protocol & AP_PROTO_MASK) == AP_PROTO_DMA)
			ccb->ataio.cmd.flags |= CAM_ATAIO_DMA;
		else if ((protocol & AP_PROTO_MASK) == AP_PROTO_FPDMA)
			ccb->ataio.cmd.flags |= CAM_ATAIO_FPDMA;
	} else {
		if (is48bit || lba > ATA_MAX_28BIT_LBA)
			protocol |= AP_EXTEND;

		retval = scsi_ata_pass(&ccb->csio,
		    /*retries*/ retry_count,
		    /*cbfcnp*/ NULL,
		    /*flags*/ flags,
		    /*tag_action*/ tag_action,
		    /*protocol*/ protocol,
		    /*ata_flags*/ ata_flags,
		    /*features*/ features,
		    /*sector_count*/ sector_count,
		    /*lba*/ lba,
		    /*command*/ command,
		    /*device*/ 0,
		    /*icc*/ 0,
		    /*auxiliary*/ auxiliary,
		    /*control*/ 0,
		    /*data_ptr*/ data_ptr,
		    /*dxfer_len*/ dxfer_len,
		    /*cdb_storage*/ cdb_storage,
		    /*cdb_storage_len*/ cdb_storage_len,
		    /*minimum_cmd_size*/ 0,
		    /*sense_len*/ sense_len,
		    /*timeout*/ timeout);
	}

	return (retval);
}

int
get_ata_status(struct cam_device *dev, union ccb *ccb, uint8_t *error,
	       uint16_t *count, uint64_t *lba, uint8_t *device, uint8_t *status)
{
	int retval = 0;

	switch (ccb->ccb_h.func_code) {
	case XPT_SCSI_IO: {
		uint8_t opcode;
		int error_code = 0, sense_key = 0, asc = 0, ascq = 0;

		/*
		 * In this case, we have SCSI ATA PASS-THROUGH command, 12
		 * or 16 byte, and need to see what
		 */
		if (ccb->ccb_h.flags & CAM_CDB_POINTER)
			opcode = ccb->csio.cdb_io.cdb_ptr[0];
		else
			opcode = ccb->csio.cdb_io.cdb_bytes[0];
		if ((opcode != ATA_PASS_12)
		 && (opcode != ATA_PASS_16)) {
			retval = 1;
			warnx("%s: unsupported opcode %02x", __func__, opcode);
			goto bailout;
		}

		retval = scsi_extract_sense_ccb(ccb, &error_code, &sense_key,
						&asc, &ascq);
		/* Note: the _ccb() variant returns 0 for an error */
		if (retval == 0) {
			retval = 1;
			goto bailout;
		} else
			retval = 0;

		switch (error_code) {
		case SSD_DESC_CURRENT_ERROR:
		case SSD_DESC_DEFERRED_ERROR: {
			struct scsi_sense_data_desc *sense;
			struct scsi_sense_ata_ret_desc *desc;
			uint8_t *desc_ptr;

			sense = (struct scsi_sense_data_desc *)
			    &ccb->csio.sense_data;

			desc_ptr = scsi_find_desc(sense, ccb->csio.sense_len -
			    ccb->csio.sense_resid, SSD_DESC_ATA);
			if (desc_ptr == NULL) {
				cam_error_print(dev, ccb, CAM_ESF_ALL,
				    CAM_EPF_ALL, stderr);
				retval = 1;
				goto bailout;
			}
			desc = (struct scsi_sense_ata_ret_desc *)desc_ptr;

			*error = desc->error;
			*count = (desc->count_15_8 << 8) |
				  desc->count_7_0;
			*lba = ((uint64_t)desc->lba_47_40 << 40) |
			       ((uint64_t)desc->lba_39_32 << 32) |
			       ((uint64_t)desc->lba_31_24 << 24) |
			       (desc->lba_23_16 << 16) |
			       (desc->lba_15_8  <<  8) |
				desc->lba_7_0;
			*device = desc->device;
			*status = desc->status;

			/*
			 * If the extend bit isn't set, the result is for a
			 * 12-byte ATA PASS-THROUGH command or a 16 or 32 byte
			 * command without the extend bit set.  This means
			 * that the device is supposed to return 28-bit
			 * status.  The count field is only 8 bits, and the
			 * LBA field is only 8 bits.
			 */
			if ((desc->flags & SSD_DESC_ATA_FLAG_EXTEND) == 0){
				*count &= 0xff;
				*lba &= 0x0fffffff;
			}
			break;
		}
		case SSD_CURRENT_ERROR:
		case SSD_DEFERRED_ERROR: {
#if 0
			struct scsi_sense_data_fixed *sense;
#endif
			/*
			 * XXX KDM need to support fixed sense data.
			 */
			warnx("%s: Fixed sense data not supported yet",
			    __func__);
			retval = 1;
			goto bailout;
			break; /*NOTREACHED*/
		}
		default:
			retval = 1;
			goto bailout;
			break;
		}

		break;
	}
	case XPT_ATA_IO: {
		struct ata_res *res;

		/*
		 * In this case, we have an ATA command, and we need to
		 * fill in the requested values from the result register
		 * set.
		 */
		res = &ccb->ataio.res;
		*error = res->error;
		*status = res->status;
		*device = res->device;
		*count = res->sector_count;
		*lba = (res->lba_high << 16) |
		       (res->lba_mid << 8) |
		       (res->lba_low);
		if (res->flags & CAM_ATAIO_48BIT) {
			*count |= (res->sector_count_exp << 8);
			*lba |= ((uint64_t)res->lba_low_exp << 24) |
				((uint64_t)res->lba_mid_exp << 32) |
				((uint64_t)res->lba_high_exp << 40);
		} else {
			*lba |= (res->device & 0xf) << 24;
		}
		break;
	}
	default:
		retval = 1;
		break;
	}
bailout:
	return (retval);
}

static void
cpi_print(struct ccb_pathinq *cpi)
{
	char adapter_str[1024];
	uint64_t i;

	snprintf(adapter_str, sizeof(adapter_str),
		 "%s%d:", cpi->dev_name, cpi->unit_number);

	fprintf(stdout, "%s SIM/HBA version: %d\n", adapter_str,
		cpi->version_num);

	for (i = 1; i < UINT8_MAX; i = i << 1) {
		const char *str;

		if ((i & cpi->hba_inquiry) == 0)
			continue;

		fprintf(stdout, "%s supports ", adapter_str);

		switch(i) {
		case PI_MDP_ABLE:
			str = "MDP message";
			break;
		case PI_WIDE_32:
			str = "32 bit wide SCSI";
			break;
		case PI_WIDE_16:
			str = "16 bit wide SCSI";
			break;
		case PI_SDTR_ABLE:
			str = "SDTR message";
			break;
		case PI_LINKED_CDB:
			str = "linked CDBs";
			break;
		case PI_TAG_ABLE:
			str = "tag queue messages";
			break;
		case PI_SOFT_RST:
			str = "soft reset alternative";
			break;
		case PI_SATAPM:
			str = "SATA Port Multiplier";
			break;
		default:
			str = "unknown PI bit set";
			break;
		}
		fprintf(stdout, "%s\n", str);
	}

	for (i = 1; i < UINT32_MAX; i = i << 1) {
		const char *str;

		if ((i & cpi->hba_misc) == 0)
			continue;

		fprintf(stdout, "%s ", adapter_str);

		switch(i) {
		case PIM_ATA_EXT:
			str = "can understand ata_ext requests";
			break;
		case PIM_EXTLUNS:
			str = "64bit extended LUNs supported";
			break;
		case PIM_SCANHILO:
			str = "bus scans from high ID to low ID";
			break;
		case PIM_NOREMOVE:
			str = "removable devices not included in scan";
			break;
		case PIM_NOINITIATOR:
			str = "initiator role not supported";
			break;
		case PIM_NOBUSRESET:
			str = "user has disabled initial BUS RESET or"
			      " controller is in target/mixed mode";
			break;
		case PIM_NO_6_BYTE:
			str = "do not send 6-byte commands";
			break;
		case PIM_SEQSCAN:
			str = "scan bus sequentially";
			break;
		case PIM_UNMAPPED:
			str = "unmapped I/O supported";
			break;
		case PIM_NOSCAN:
			str = "does its own scanning";
			break;
		default:
			str = "unknown PIM bit set";
			break;
		}
		fprintf(stdout, "%s\n", str);
	}

	for (i = 1; i < UINT16_MAX; i = i << 1) {
		const char *str;

		if ((i & cpi->target_sprt) == 0)
			continue;

		fprintf(stdout, "%s supports ", adapter_str);
		switch(i) {
		case PIT_PROCESSOR:
			str = "target mode processor mode";
			break;
		case PIT_PHASE:
			str = "target mode phase cog. mode";
			break;
		case PIT_DISCONNECT:
			str = "disconnects in target mode";
			break;
		case PIT_TERM_IO:
			str = "terminate I/O message in target mode";
			break;
		case PIT_GRP_6:
			str = "group 6 commands in target mode";
			break;
		case PIT_GRP_7:
			str = "group 7 commands in target mode";
			break;
		default:
			str = "unknown PIT bit set";
			break;
		}

		fprintf(stdout, "%s\n", str);
	}
	fprintf(stdout, "%s HBA engine count: %d\n", adapter_str,
		cpi->hba_eng_cnt);
	fprintf(stdout, "%s maximum target: %d\n", adapter_str,
		cpi->max_target);
	fprintf(stdout, "%s maximum LUN: %d\n", adapter_str,
		cpi->max_lun);
	fprintf(stdout, "%s highest path ID in subsystem: %d\n",
		adapter_str, cpi->hpath_id);
	fprintf(stdout, "%s initiator ID: %d\n", adapter_str,
		cpi->initiator_id);
	fprintf(stdout, "%s SIM vendor: %s\n", adapter_str, cpi->sim_vid);
	fprintf(stdout, "%s HBA vendor: %s\n", adapter_str, cpi->hba_vid);
	fprintf(stdout, "%s HBA vendor ID: 0x%04x\n",
	    adapter_str, cpi->hba_vendor);
	fprintf(stdout, "%s HBA device ID: 0x%04x\n",
	    adapter_str, cpi->hba_device);
	fprintf(stdout, "%s HBA subvendor ID: 0x%04x\n",
	    adapter_str, cpi->hba_subvendor);
	fprintf(stdout, "%s HBA subdevice ID: 0x%04x\n",
	    adapter_str, cpi->hba_subdevice);
	fprintf(stdout, "%s bus ID: %d\n", adapter_str, cpi->bus_id);
	fprintf(stdout, "%s base transfer speed: ", adapter_str);
	if (cpi->base_transfer_speed > 1000)
		fprintf(stdout, "%d.%03dMB/sec\n",
			cpi->base_transfer_speed / 1000,
			cpi->base_transfer_speed % 1000);
	else
		fprintf(stdout, "%dKB/sec\n",
			(cpi->base_transfer_speed % 1000) * 1000);
	fprintf(stdout, "%s maximum transfer size: %u bytes\n",
	    adapter_str, cpi->maxio);
}

static int
get_print_cts(struct cam_device *device, int user_settings, int quiet,
	      struct ccb_trans_settings *cts)
{
	int retval;
	union ccb *ccb;

	retval = 0;
	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("get_print_cts: error allocating ccb");
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cts);

	ccb->ccb_h.func_code = XPT_GET_TRAN_SETTINGS;

	if (user_settings == 0)
		ccb->cts.type = CTS_TYPE_CURRENT_SETTINGS;
	else
		ccb->cts.type = CTS_TYPE_USER_SETTINGS;

	if (cam_send_ccb(device, ccb) < 0) {
		perror("error sending XPT_GET_TRAN_SETTINGS CCB");
		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		retval = 1;
		goto get_print_cts_bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		warnx("XPT_GET_TRANS_SETTINGS CCB failed");
		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		retval = 1;
		goto get_print_cts_bailout;
	}

	if (quiet == 0)
		cts_print(device, &ccb->cts);

	if (cts != NULL)
		bcopy(&ccb->cts, cts, sizeof(struct ccb_trans_settings));

get_print_cts_bailout:

	cam_freeccb(ccb);

	return (retval);
}

static int
ratecontrol(struct cam_device *device, int task_attr, int retry_count,
	    int timeout, int argc, char **argv, char *combinedopt)
{
	int c;
	union ccb *ccb;
	int user_settings = 0;
	int retval = 0;
	int disc_enable = -1, tag_enable = -1;
	int mode = -1;
	int offset = -1;
	double syncrate = -1;
	int bus_width = -1;
	int quiet = 0;
	int change_settings = 0, send_tur = 0;
	struct ccb_pathinq cpi;

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("ratecontrol: error allocating ccb");
		return (1);
	}
	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c){
		case 'a':
			send_tur = 1;
			break;
		case 'c':
			user_settings = 0;
			break;
		case 'D':
			if (strncasecmp(optarg, "enable", 6) == 0)
				disc_enable = 1;
			else if (strncasecmp(optarg, "disable", 7) == 0)
				disc_enable = 0;
			else {
				warnx("-D argument \"%s\" is unknown", optarg);
				retval = 1;
				goto ratecontrol_bailout;
			}
			change_settings = 1;
			break;
		case 'M':
			mode = ata_string2mode(optarg);
			if (mode < 0) {
				warnx("unknown mode '%s'", optarg);
				retval = 1;
				goto ratecontrol_bailout;
			}
			change_settings = 1;
			break;
		case 'O':
			offset = strtol(optarg, NULL, 0);
			if (offset < 0) {
				warnx("offset value %d is < 0", offset);
				retval = 1;
				goto ratecontrol_bailout;
			}
			change_settings = 1;
			break;
		case 'q':
			quiet++;
			break;
		case 'R':
			syncrate = atof(optarg);
			if (syncrate < 0) {
				warnx("sync rate %f is < 0", syncrate);
				retval = 1;
				goto ratecontrol_bailout;
			}
			change_settings = 1;
			break;
		case 'T':
			if (strncasecmp(optarg, "enable", 6) == 0)
				tag_enable = 1;
			else if (strncasecmp(optarg, "disable", 7) == 0)
				tag_enable = 0;
			else {
				warnx("-T argument \"%s\" is unknown", optarg);
				retval = 1;
				goto ratecontrol_bailout;
			}
			change_settings = 1;
			break;
		case 'U':
			user_settings = 1;
			break;
		case 'W':
			bus_width = strtol(optarg, NULL, 0);
			if (bus_width < 0) {
				warnx("bus width %d is < 0", bus_width);
				retval = 1;
				goto ratecontrol_bailout;
			}
			change_settings = 1;
			break;
		default:
			break;
		}
	}
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cpi);
	/*
	 * Grab path inquiry information, so we can determine whether
	 * or not the initiator is capable of the things that the user
	 * requests.
	 */
	ccb->ccb_h.func_code = XPT_PATH_INQ;
	if (cam_send_ccb(device, ccb) < 0) {
		perror("error sending XPT_PATH_INQ CCB");
		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
		retval = 1;
		goto ratecontrol_bailout;
	}
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		warnx("XPT_PATH_INQ CCB failed");
		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
		retval = 1;
		goto ratecontrol_bailout;
	}
	bcopy(&ccb->cpi, &cpi, sizeof(struct ccb_pathinq));
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cts);
	if (quiet == 0) {
		fprintf(stdout, "%s parameters:\n",
		    user_settings ? "User" : "Current");
	}
	retval = get_print_cts(device, user_settings, quiet, &ccb->cts);
	if (retval != 0)
		goto ratecontrol_bailout;

	if (arglist & CAM_ARG_VERBOSE)
		cpi_print(&cpi);

	if (change_settings) {
		int didsettings = 0;
		struct ccb_trans_settings_spi *spi = NULL;
		struct ccb_trans_settings_pata *pata = NULL;
		struct ccb_trans_settings_sata *sata = NULL;
		struct ccb_trans_settings_ata *ata = NULL;
		struct ccb_trans_settings_scsi *scsi = NULL;

		if (ccb->cts.transport == XPORT_SPI)
			spi = &ccb->cts.xport_specific.spi;
		if (ccb->cts.transport == XPORT_ATA)
			pata = &ccb->cts.xport_specific.ata;
		if (ccb->cts.transport == XPORT_SATA)
			sata = &ccb->cts.xport_specific.sata;
		if (ccb->cts.protocol == PROTO_ATA)
			ata = &ccb->cts.proto_specific.ata;
		if (ccb->cts.protocol == PROTO_SCSI)
			scsi = &ccb->cts.proto_specific.scsi;
		ccb->cts.xport_specific.valid = 0;
		ccb->cts.proto_specific.valid = 0;
		if (spi && disc_enable != -1) {
			spi->valid |= CTS_SPI_VALID_DISC;
			if (disc_enable == 0)
				spi->flags &= ~CTS_SPI_FLAGS_DISC_ENB;
			else
				spi->flags |= CTS_SPI_FLAGS_DISC_ENB;
			didsettings++;
		}
		if (tag_enable != -1) {
			if ((cpi.hba_inquiry & PI_TAG_ABLE) == 0) {
				warnx("HBA does not support tagged queueing, "
				      "so you cannot modify tag settings");
				retval = 1;
				goto ratecontrol_bailout;
			}
			if (ata) {
				ata->valid |= CTS_SCSI_VALID_TQ;
				if (tag_enable == 0)
					ata->flags &= ~CTS_ATA_FLAGS_TAG_ENB;
				else
					ata->flags |= CTS_ATA_FLAGS_TAG_ENB;
				didsettings++;
			} else if (scsi) {
				scsi->valid |= CTS_SCSI_VALID_TQ;
				if (tag_enable == 0)
					scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
				else
					scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
				didsettings++;
			}
		}
		if (spi && offset != -1) {
			if ((cpi.hba_inquiry & PI_SDTR_ABLE) == 0) {
				warnx("HBA is not capable of changing offset");
				retval = 1;
				goto ratecontrol_bailout;
			}
			spi->valid |= CTS_SPI_VALID_SYNC_OFFSET;
			spi->sync_offset = offset;
			didsettings++;
		}
		if (spi && syncrate != -1) {
			int prelim_sync_period;

			if ((cpi.hba_inquiry & PI_SDTR_ABLE) == 0) {
				warnx("HBA is not capable of changing "
				      "transfer rates");
				retval = 1;
				goto ratecontrol_bailout;
			}
			spi->valid |= CTS_SPI_VALID_SYNC_RATE;
			/*
			 * The sync rate the user gives us is in MHz.
			 * We need to translate it into KHz for this
			 * calculation.
			 */
			syncrate *= 1000;
			/*
			 * Next, we calculate a "preliminary" sync period
			 * in tenths of a nanosecond.
			 */
			if (syncrate == 0)
				prelim_sync_period = 0;
			else
				prelim_sync_period = 10000000 / syncrate;
			spi->sync_period =
				scsi_calc_syncparam(prelim_sync_period);
			didsettings++;
		}
		if (sata && syncrate != -1) {
			if ((cpi.hba_inquiry & PI_SDTR_ABLE) == 0) {
				warnx("HBA is not capable of changing "
				      "transfer rates");
				retval = 1;
				goto ratecontrol_bailout;
			}
			if  (!user_settings) {
				warnx("You can modify only user rate "
				    "settings for SATA");
				retval = 1;
				goto ratecontrol_bailout;
			}
			sata->revision = ata_speed2revision(syncrate * 100);
			if (sata->revision < 0) {
				warnx("Invalid rate %f", syncrate);
				retval = 1;
				goto ratecontrol_bailout;
			}
			sata->valid |= CTS_SATA_VALID_REVISION;
			didsettings++;
		}
		if ((pata || sata) && mode != -1) {
			if ((cpi.hba_inquiry & PI_SDTR_ABLE) == 0) {
				warnx("HBA is not capable of changing "
				      "transfer rates");
				retval = 1;
				goto ratecontrol_bailout;
			}
			if  (!user_settings) {
				warnx("You can modify only user mode "
				    "settings for ATA/SATA");
				retval = 1;
				goto ratecontrol_bailout;
			}
			if (pata) {
				pata->mode = mode;
				pata->valid |= CTS_ATA_VALID_MODE;
			} else {
				sata->mode = mode;
				sata->valid |= CTS_SATA_VALID_MODE;
			}
			didsettings++;
		}
		/*
		 * The bus_width argument goes like this:
		 * 0 == 8 bit
		 * 1 == 16 bit
		 * 2 == 32 bit
		 * Therefore, if you shift the number of bits given on the
		 * command line right by 4, you should get the correct
		 * number.
		 */
		if (spi && bus_width != -1) {
			/*
			 * We might as well validate things here with a
			 * decipherable error message, rather than what
			 * will probably be an indecipherable error message
			 * by the time it gets back to us.
			 */
			if ((bus_width == 16)
			 && ((cpi.hba_inquiry & PI_WIDE_16) == 0)) {
				warnx("HBA does not support 16 bit bus width");
				retval = 1;
				goto ratecontrol_bailout;
			} else if ((bus_width == 32)
				&& ((cpi.hba_inquiry & PI_WIDE_32) == 0)) {
				warnx("HBA does not support 32 bit bus width");
				retval = 1;
				goto ratecontrol_bailout;
			} else if ((bus_width != 8)
				&& (bus_width != 16)
				&& (bus_width != 32)) {
				warnx("Invalid bus width %d", bus_width);
				retval = 1;
				goto ratecontrol_bailout;
			}
			spi->valid |= CTS_SPI_VALID_BUS_WIDTH;
			spi->bus_width = bus_width >> 4;
			didsettings++;
		}
		if  (didsettings == 0) {
			goto ratecontrol_bailout;
		}
		ccb->ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
		if (cam_send_ccb(device, ccb) < 0) {
			perror("error sending XPT_SET_TRAN_SETTINGS CCB");
			if (arglist & CAM_ARG_VERBOSE) {
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
			}
			retval = 1;
			goto ratecontrol_bailout;
		}
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			warnx("XPT_SET_TRANS_SETTINGS CCB failed");
			if (arglist & CAM_ARG_VERBOSE) {
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
			}
			retval = 1;
			goto ratecontrol_bailout;
		}
	}
	if (send_tur) {
		retval = testunitready(device, task_attr, retry_count, timeout,
				       (arglist & CAM_ARG_VERBOSE) ? 0 : 1);
		/*
		 * If the TUR didn't succeed, just bail.
		 */
		if (retval != 0) {
			if (quiet == 0)
				fprintf(stderr, "Test Unit Ready failed\n");
			goto ratecontrol_bailout;
		}
	}
	if ((change_settings || send_tur) && !quiet &&
	    (ccb->cts.transport == XPORT_ATA ||
	     ccb->cts.transport == XPORT_SATA || send_tur)) {
		fprintf(stdout, "New parameters:\n");
		retval = get_print_cts(device, user_settings, 0, NULL);
	}

ratecontrol_bailout:
	cam_freeccb(ccb);
	return (retval);
}

static int
scsiformat(struct cam_device *device, int argc, char **argv,
	   char *combinedopt, int task_attr, int retry_count, int timeout)
{
	union ccb *ccb;
	int c;
	int ycount = 0, quiet = 0;
	int error = 0, retval = 0;
	int use_timeout = 10800 * 1000;
	int immediate = 1;
	struct format_defect_list_header fh;
	u_int8_t *data_ptr = NULL;
	u_int32_t dxfer_len = 0;
	u_int8_t byte2 = 0;
	int num_warnings = 0;
	int reportonly = 0;

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("scsiformat: error allocating ccb");
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c) {
		case 'q':
			quiet++;
			break;
		case 'r':
			reportonly = 1;
			break;
		case 'w':
			immediate = 0;
			break;
		case 'y':
			ycount++;
			break;
		}
	}

	if (reportonly)
		goto doreport;

	if (quiet == 0) {
		fprintf(stdout, "You are about to REMOVE ALL DATA from the "
			"following device:\n");

		error = scsidoinquiry(device, argc, argv, combinedopt,
				      task_attr, retry_count, timeout);

		if (error != 0) {
			warnx("scsiformat: error sending inquiry");
			goto scsiformat_bailout;
		}
	}

	if (ycount == 0) {
		if (!get_confirmation()) {
			error = 1;
			goto scsiformat_bailout;
		}
	}

	if (timeout != 0)
		use_timeout = timeout;

	if (quiet == 0) {
		fprintf(stdout, "Current format timeout is %d seconds\n",
			use_timeout / 1000);
	}

	/*
	 * If the user hasn't disabled questions and didn't specify a
	 * timeout on the command line, ask them if they want the current
	 * timeout.
	 */
	if ((ycount == 0)
	 && (timeout == 0)) {
		char str[1024];
		int new_timeout = 0;

		fprintf(stdout, "Enter new timeout in seconds or press\n"
			"return to keep the current timeout [%d] ",
			use_timeout / 1000);

		if (fgets(str, sizeof(str), stdin) != NULL) {
			if (str[0] != '\0')
				new_timeout = atoi(str);
		}

		if (new_timeout != 0) {
			use_timeout = new_timeout * 1000;
			fprintf(stdout, "Using new timeout value %d\n",
				use_timeout / 1000);
		}
	}

	/*
	 * Keep this outside the if block below to silence any unused
	 * variable warnings.
	 */
	bzero(&fh, sizeof(fh));

	/*
	 * If we're in immediate mode, we've got to include the format
	 * header
	 */
	if (immediate != 0) {
		fh.byte2 = FU_DLH_IMMED;
		data_ptr = (u_int8_t *)&fh;
		dxfer_len = sizeof(fh);
		byte2 = FU_FMT_DATA;
	} else if (quiet == 0) {
		fprintf(stdout, "Formatting...");
		fflush(stdout);
	}

	scsi_format_unit(&ccb->csio,
			 /* retries */ retry_count,
			 /* cbfcnp */ NULL,
			 /* tag_action */ task_attr,
			 /* byte2 */ byte2,
			 /* ileave */ 0,
			 /* data_ptr */ data_ptr,
			 /* dxfer_len */ dxfer_len,
			 /* sense_len */ SSD_FULL_SIZE,
			 /* timeout */ use_timeout);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((immediate == 0)
	   && ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP))) {
		const char errstr[] = "error sending format command";

		if (retval < 0)
			warn(errstr);
		else
			warnx(errstr);

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
		error = 1;
		goto scsiformat_bailout;
	}

	/*
	 * If we ran in non-immediate mode, we already checked for errors
	 * above and printed out any necessary information.  If we're in
	 * immediate mode, we need to loop through and get status
	 * information periodically.
	 */
	if (immediate == 0) {
		if (quiet == 0) {
			fprintf(stdout, "Format Complete\n");
		}
		goto scsiformat_bailout;
	}

doreport:
	do {
		cam_status status;

		CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

		/*
		 * There's really no need to do error recovery or
		 * retries here, since we're just going to sit in a
		 * loop and wait for the device to finish formatting.
		 */
		scsi_test_unit_ready(&ccb->csio,
				     /* retries */ 0,
				     /* cbfcnp */ NULL,
				     /* tag_action */ task_attr,
				     /* sense_len */ SSD_FULL_SIZE,
				     /* timeout */ 5000);

		/* Disable freezing the device queue */
		ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

		retval = cam_send_ccb(device, ccb);

		/*
		 * If we get an error from the ioctl, bail out.  SCSI
		 * errors are expected.
		 */
		if (retval < 0) {
			warn("error sending CAMIOCOMMAND ioctl");
			if (arglist & CAM_ARG_VERBOSE) {
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
			}
			error = 1;
			goto scsiformat_bailout;
		}

		status = ccb->ccb_h.status & CAM_STATUS_MASK;

		if ((status != CAM_REQ_CMP)
		 && (status == CAM_SCSI_STATUS_ERROR)
		 && ((ccb->ccb_h.status & CAM_AUTOSNS_VALID) != 0)) {
			struct scsi_sense_data *sense;
			int error_code, sense_key, asc, ascq;

			sense = &ccb->csio.sense_data;
			scsi_extract_sense_len(sense, ccb->csio.sense_len -
			    ccb->csio.sense_resid, &error_code, &sense_key,
			    &asc, &ascq, /*show_errors*/ 1);

			/*
			 * According to the SCSI-2 and SCSI-3 specs, a
			 * drive that is in the middle of a format should
			 * return NOT READY with an ASC of "logical unit
			 * not ready, format in progress".  The sense key
			 * specific bytes will then be a progress indicator.
			 */
			if ((sense_key == SSD_KEY_NOT_READY)
			 && (asc == 0x04) && (ascq == 0x04)) {
				uint8_t sks[3];

				if ((scsi_get_sks(sense, ccb->csio.sense_len -
				     ccb->csio.sense_resid, sks) == 0)
				 && (quiet == 0)) {
					uint32_t val;
					u_int64_t percentage;

					val = scsi_2btoul(&sks[1]);
					percentage = 10000ull * val;

					fprintf(stdout,
						"\rFormatting:  %ju.%02u %% "
						"(%u/%d) done",
						(uintmax_t)(percentage /
						(0x10000 * 100)),
						(unsigned)((percentage /
						0x10000) % 100),
						val, 0x10000);
					fflush(stdout);
				} else if ((quiet == 0)
					&& (++num_warnings <= 1)) {
					warnx("Unexpected SCSI Sense Key "
					      "Specific value returned "
					      "during format:");
					scsi_sense_print(device, &ccb->csio,
							 stderr);
					warnx("Unable to print status "
					      "information, but format will "
					      "proceed.");
					warnx("will exit when format is "
					      "complete");
				}
				sleep(1);
			} else {
				warnx("Unexpected SCSI error during format");
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
				error = 1;
				goto scsiformat_bailout;
			}

		} else if (status != CAM_REQ_CMP) {
			warnx("Unexpected CAM status %#x", status);
			if (arglist & CAM_ARG_VERBOSE)
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
			error = 1;
			goto scsiformat_bailout;
		}

	} while((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP);

	if (quiet == 0)
		fprintf(stdout, "\nFormat Complete\n");

scsiformat_bailout:

	cam_freeccb(ccb);

	return (error);
}

static int
scsisanitize(struct cam_device *device, int argc, char **argv,
	     char *combinedopt, int task_attr, int retry_count, int timeout)
{
	union ccb *ccb;
	u_int8_t action = 0;
	int c;
	int ycount = 0, quiet = 0;
	int error = 0, retval = 0;
	int use_timeout = 10800 * 1000;
	int immediate = 1;
	int invert = 0;
	int passes = 0;
	int ause = 0;
	int fd = -1;
	const char *pattern = NULL;
	u_int8_t *data_ptr = NULL;
	u_int32_t dxfer_len = 0;
	u_int8_t byte2 = 0;
	int num_warnings = 0;
	int reportonly = 0;

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("scsisanitize: error allocating ccb");
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch(c) {
		case 'a':
			if (strcasecmp(optarg, "overwrite") == 0)
				action = SSZ_SERVICE_ACTION_OVERWRITE;
			else if (strcasecmp(optarg, "block") == 0)
				action = SSZ_SERVICE_ACTION_BLOCK_ERASE;
			else if (strcasecmp(optarg, "crypto") == 0)
				action = SSZ_SERVICE_ACTION_CRYPTO_ERASE;
			else if (strcasecmp(optarg, "exitfailure") == 0)
				action = SSZ_SERVICE_ACTION_EXIT_MODE_FAILURE;
			else {
				warnx("invalid service operation \"%s\"",
				      optarg);
				error = 1;
				goto scsisanitize_bailout;
			}
			break;
		case 'c':
			passes = strtol(optarg, NULL, 0);
			if (passes < 1 || passes > 31) {
				warnx("invalid passes value %d", passes);
				error = 1;
				goto scsisanitize_bailout;
			}
			break;
		case 'I':
			invert = 1;
			break;
		case 'P':
			pattern = optarg;
			break;
		case 'q':
			quiet++;
			break;
		case 'U':
			ause = 1;
			break;
		case 'r':
			reportonly = 1;
			break;
		case 'w':
			immediate = 0;
			break;
		case 'y':
			ycount++;
			break;
		}
	}

	if (reportonly)
		goto doreport;

	if (action == 0) {
		warnx("an action is required");
		error = 1;
		goto scsisanitize_bailout;
	} else if (action == SSZ_SERVICE_ACTION_OVERWRITE) {
		struct scsi_sanitize_parameter_list *pl;
		struct stat sb;
		ssize_t sz, amt;

		if (pattern == NULL) {
			warnx("overwrite action requires -P argument");
			error = 1;
			goto scsisanitize_bailout;
		}
		fd = open(pattern, O_RDONLY);
		if (fd < 0) {
			warn("cannot open pattern file %s", pattern);
			error = 1;
			goto scsisanitize_bailout;
		}
		if (fstat(fd, &sb) < 0) {
			warn("cannot stat pattern file %s", pattern);
			error = 1;
			goto scsisanitize_bailout;
		}
		sz = sb.st_size;
		if (sz > SSZPL_MAX_PATTERN_LENGTH) {
			warnx("pattern file size exceeds maximum value %d",
			      SSZPL_MAX_PATTERN_LENGTH);
			error = 1;
			goto scsisanitize_bailout;
		}
		dxfer_len = sizeof(*pl) + sz;
		data_ptr = calloc(1, dxfer_len);
		if (data_ptr == NULL) {
			warnx("cannot allocate parameter list buffer");
			error = 1;
			goto scsisanitize_bailout;
		}

		amt = read(fd, data_ptr + sizeof(*pl), sz);
		if (amt < 0) {
			warn("cannot read pattern file");
			error = 1;
			goto scsisanitize_bailout;
		} else if (amt != sz) {
			warnx("short pattern file read");
			error = 1;
			goto scsisanitize_bailout;
		}

		pl = (struct scsi_sanitize_parameter_list *)data_ptr;
		if (passes == 0)
			pl->byte1 = 1;
		else
			pl->byte1 = passes;
		if (invert != 0)
			pl->byte1 |= SSZPL_INVERT;
		scsi_ulto2b(sz, pl->length);
	} else {
		const char *arg;

		if (passes != 0)
			arg = "-c";
		else if (invert != 0)
			arg = "-I";
		else if (pattern != NULL)
			arg = "-P";
		else
			arg = NULL;
		if (arg != NULL) {
			warnx("%s argument only valid with overwrite "
			      "operation", arg);
			error = 1;
			goto scsisanitize_bailout;
		}
	}

	if (quiet == 0) {
		fprintf(stdout, "You are about to REMOVE ALL DATA from the "
			"following device:\n");

		error = scsidoinquiry(device, argc, argv, combinedopt,
				      task_attr, retry_count, timeout);

		if (error != 0) {
			warnx("scsisanitize: error sending inquiry");
			goto scsisanitize_bailout;
		}
	}

	if (ycount == 0) {
		if (!get_confirmation()) {
			error = 1;
			goto scsisanitize_bailout;
		}
	}

	if (timeout != 0)
		use_timeout = timeout;

	if (quiet == 0) {
		fprintf(stdout, "Current sanitize timeout is %d seconds\n",
			use_timeout / 1000);
	}

	/*
	 * If the user hasn't disabled questions and didn't specify a
	 * timeout on the command line, ask them if they want the current
	 * timeout.
	 */
	if ((ycount == 0)
	 && (timeout == 0)) {
		char str[1024];
		int new_timeout = 0;

		fprintf(stdout, "Enter new timeout in seconds or press\n"
			"return to keep the current timeout [%d] ",
			use_timeout / 1000);

		if (fgets(str, sizeof(str), stdin) != NULL) {
			if (str[0] != '\0')
				new_timeout = atoi(str);
		}

		if (new_timeout != 0) {
			use_timeout = new_timeout * 1000;
			fprintf(stdout, "Using new timeout value %d\n",
				use_timeout / 1000);
		}
	}

	byte2 = action;
	if (ause != 0)
		byte2 |= SSZ_UNRESTRICTED_EXIT;
	if (immediate != 0)
		byte2 |= SSZ_IMMED;

	scsi_sanitize(&ccb->csio,
		      /* retries */ retry_count,
		      /* cbfcnp */ NULL,
		      /* tag_action */ task_attr,
		      /* byte2 */ byte2,
		      /* control */ 0,
		      /* data_ptr */ data_ptr,
		      /* dxfer_len */ dxfer_len,
		      /* sense_len */ SSD_FULL_SIZE,
		      /* timeout */ use_timeout);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		warn("error sending sanitize command");
		error = 1;
		goto scsisanitize_bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		struct scsi_sense_data *sense;
		int error_code, sense_key, asc, ascq;

		if ((ccb->ccb_h.status & CAM_STATUS_MASK) ==
		    CAM_SCSI_STATUS_ERROR) {
			sense = &ccb->csio.sense_data;
			scsi_extract_sense_len(sense, ccb->csio.sense_len -
			    ccb->csio.sense_resid, &error_code, &sense_key,
			    &asc, &ascq, /*show_errors*/ 1);

			if (sense_key == SSD_KEY_ILLEGAL_REQUEST &&
			    asc == 0x20 && ascq == 0x00)
				warnx("sanitize is not supported by "
				      "this device");
			else
				warnx("error sanitizing this device");
		} else
			warnx("error sanitizing this device");

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
		error = 1;
		goto scsisanitize_bailout;
	}

	/*
	 * If we ran in non-immediate mode, we already checked for errors
	 * above and printed out any necessary information.  If we're in
	 * immediate mode, we need to loop through and get status
	 * information periodically.
	 */
	if (immediate == 0) {
		if (quiet == 0) {
			fprintf(stdout, "Sanitize Complete\n");
		}
		goto scsisanitize_bailout;
	}

doreport:
	do {
		cam_status status;

		CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

		/*
		 * There's really no need to do error recovery or
		 * retries here, since we're just going to sit in a
		 * loop and wait for the device to finish sanitizing.
		 */
		scsi_test_unit_ready(&ccb->csio,
				     /* retries */ 0,
				     /* cbfcnp */ NULL,
				     /* tag_action */ task_attr,
				     /* sense_len */ SSD_FULL_SIZE,
				     /* timeout */ 5000);

		/* Disable freezing the device queue */
		ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

		retval = cam_send_ccb(device, ccb);

		/*
		 * If we get an error from the ioctl, bail out.  SCSI
		 * errors are expected.
		 */
		if (retval < 0) {
			warn("error sending CAMIOCOMMAND ioctl");
			if (arglist & CAM_ARG_VERBOSE) {
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
			}
			error = 1;
			goto scsisanitize_bailout;
		}

		status = ccb->ccb_h.status & CAM_STATUS_MASK;

		if ((status != CAM_REQ_CMP)
		 && (status == CAM_SCSI_STATUS_ERROR)
		 && ((ccb->ccb_h.status & CAM_AUTOSNS_VALID) != 0)) {
			struct scsi_sense_data *sense;
			int error_code, sense_key, asc, ascq;

			sense = &ccb->csio.sense_data;
			scsi_extract_sense_len(sense, ccb->csio.sense_len -
			    ccb->csio.sense_resid, &error_code, &sense_key,
			    &asc, &ascq, /*show_errors*/ 1);

			/*
			 * According to the SCSI-3 spec, a drive that is in the
			 * middle of a sanitize should return NOT READY with an
			 * ASC of "logical unit not ready, sanitize in
			 * progress". The sense key specific bytes will then
			 * be a progress indicator.
			 */
			if ((sense_key == SSD_KEY_NOT_READY)
			 && (asc == 0x04) && (ascq == 0x1b)) {
				uint8_t sks[3];

				if ((scsi_get_sks(sense, ccb->csio.sense_len -
				     ccb->csio.sense_resid, sks) == 0)
				 && (quiet == 0)) {
					int val;
					u_int64_t percentage;

					val = scsi_2btoul(&sks[1]);
					percentage = 10000 * val;

					fprintf(stdout,
						"\rSanitizing:  %ju.%02u %% "
						"(%d/%d) done",
						(uintmax_t)(percentage /
						(0x10000 * 100)),
						(unsigned)((percentage /
						0x10000) % 100),
						val, 0x10000);
					fflush(stdout);
				} else if ((quiet == 0)
					&& (++num_warnings <= 1)) {
					warnx("Unexpected SCSI Sense Key "
					      "Specific value returned "
					      "during sanitize:");
					scsi_sense_print(device, &ccb->csio,
							 stderr);
					warnx("Unable to print status "
					      "information, but sanitze will "
					      "proceed.");
					warnx("will exit when sanitize is "
					      "complete");
				}
				sleep(1);
			} else {
				warnx("Unexpected SCSI error during sanitize");
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
				error = 1;
				goto scsisanitize_bailout;
			}

		} else if (status != CAM_REQ_CMP) {
			warnx("Unexpected CAM status %#x", status);
			if (arglist & CAM_ARG_VERBOSE)
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
			error = 1;
			goto scsisanitize_bailout;
		}
	} while((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP);

	if (quiet == 0)
		fprintf(stdout, "\nSanitize Complete\n");

scsisanitize_bailout:
	if (fd >= 0)
		close(fd);
	if (data_ptr != NULL)
		free(data_ptr);
	cam_freeccb(ccb);

	return (error);
}

static int
scsireportluns(struct cam_device *device, int argc, char **argv,
	       char *combinedopt, int task_attr, int retry_count, int timeout)
{
	union ccb *ccb;
	int c, countonly, lunsonly;
	struct scsi_report_luns_data *lundata;
	int alloc_len;
	uint8_t report_type;
	uint32_t list_len, i, j;
	int retval;

	retval = 0;
	lundata = NULL;
	report_type = RPL_REPORT_DEFAULT;
	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("%s: error allocating ccb", __func__);
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	countonly = 0;
	lunsonly = 0;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'c':
			countonly++;
			break;
		case 'l':
			lunsonly++;
			break;
		case 'r':
			if (strcasecmp(optarg, "default") == 0)
				report_type = RPL_REPORT_DEFAULT;
			else if (strcasecmp(optarg, "wellknown") == 0)
				report_type = RPL_REPORT_WELLKNOWN;
			else if (strcasecmp(optarg, "all") == 0)
				report_type = RPL_REPORT_ALL;
			else {
				warnx("%s: invalid report type \"%s\"",
				      __func__, optarg);
				retval = 1;
				goto bailout;
			}
			break;
		default:
			break;
		}
	}

	if ((countonly != 0)
	 && (lunsonly != 0)) {
		warnx("%s: you can only specify one of -c or -l", __func__);
		retval = 1;
		goto bailout;
	}
	/*
	 * According to SPC-4, the allocation length must be at least 16
	 * bytes -- enough for the header and one LUN.
	 */
	alloc_len = sizeof(*lundata) + 8;

retry:

	lundata = malloc(alloc_len);

	if (lundata == NULL) {
		warn("%s: error mallocing %d bytes", __func__, alloc_len);
		retval = 1;
		goto bailout;
	}

	scsi_report_luns(&ccb->csio,
			 /*retries*/ retry_count,
			 /*cbfcnp*/ NULL,
			 /*tag_action*/ task_attr,
			 /*select_report*/ report_type,
			 /*rpl_buf*/ lundata,
			 /*alloc_len*/ alloc_len,
			 /*sense_len*/ SSD_FULL_SIZE,
			 /*timeout*/ timeout ? timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		warn("error sending REPORT LUNS command");

		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);

		retval = 1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL, CAM_EPF_ALL, stderr);
		retval = 1;
		goto bailout;
	}


	list_len = scsi_4btoul(lundata->length);

	/*
	 * If we need to list the LUNs, and our allocation
	 * length was too short, reallocate and retry.
	 */
	if ((countonly == 0)
	 && (list_len > (alloc_len - sizeof(*lundata)))) {
		alloc_len = list_len + sizeof(*lundata);
		free(lundata);
		goto retry;
	}

	if (lunsonly == 0)
		fprintf(stdout, "%u LUN%s found\n", list_len / 8,
			((list_len / 8) > 1) ? "s" : "");

	if (countonly != 0)
		goto bailout;

	for (i = 0; i < (list_len / 8); i++) {
		int no_more;

		no_more = 0;
		for (j = 0; j < sizeof(lundata->luns[i].lundata); j += 2) {
			if (j != 0)
				fprintf(stdout, ",");
			switch (lundata->luns[i].lundata[j] &
				RPL_LUNDATA_ATYP_MASK) {
			case RPL_LUNDATA_ATYP_PERIPH:
				if ((lundata->luns[i].lundata[j] &
				    RPL_LUNDATA_PERIPH_BUS_MASK) != 0)
					fprintf(stdout, "%d:",
						lundata->luns[i].lundata[j] &
						RPL_LUNDATA_PERIPH_BUS_MASK);
				else if ((j == 0)
				      && ((lundata->luns[i].lundata[j+2] &
					  RPL_LUNDATA_PERIPH_BUS_MASK) == 0))
					no_more = 1;

				fprintf(stdout, "%d",
					lundata->luns[i].lundata[j+1]);
				break;
			case RPL_LUNDATA_ATYP_FLAT: {
				uint8_t tmplun[2];
				tmplun[0] = lundata->luns[i].lundata[j] &
					RPL_LUNDATA_FLAT_LUN_MASK;
				tmplun[1] = lundata->luns[i].lundata[j+1];

				fprintf(stdout, "%d", scsi_2btoul(tmplun));
				no_more = 1;
				break;
			}
			case RPL_LUNDATA_ATYP_LUN:
				fprintf(stdout, "%d:%d:%d",
					(lundata->luns[i].lundata[j+1] &
					RPL_LUNDATA_LUN_BUS_MASK) >> 5,
					lundata->luns[i].lundata[j] &
					RPL_LUNDATA_LUN_TARG_MASK,
					lundata->luns[i].lundata[j+1] &
					RPL_LUNDATA_LUN_LUN_MASK);
				break;
			case RPL_LUNDATA_ATYP_EXTLUN: {
				int field_len_code, eam_code;

				eam_code = lundata->luns[i].lundata[j] &
					RPL_LUNDATA_EXT_EAM_MASK;
				field_len_code = (lundata->luns[i].lundata[j] &
					RPL_LUNDATA_EXT_LEN_MASK) >> 4;

				if ((eam_code == RPL_LUNDATA_EXT_EAM_WK)
				 && (field_len_code == 0x00)) {
					fprintf(stdout, "%d",
						lundata->luns[i].lundata[j+1]);
				} else if ((eam_code ==
					    RPL_LUNDATA_EXT_EAM_NOT_SPEC)
					&& (field_len_code == 0x03)) {
					uint8_t tmp_lun[8];

					/*
					 * This format takes up all 8 bytes.
					 * If we aren't starting at offset 0,
					 * that's a bug.
					 */
					if (j != 0) {
						fprintf(stdout, "Invalid "
							"offset %d for "
							"Extended LUN not "
							"specified format", j);
						no_more = 1;
						break;
					}
					bzero(tmp_lun, sizeof(tmp_lun));
					bcopy(&lundata->luns[i].lundata[j+1],
					      &tmp_lun[1], sizeof(tmp_lun) - 1);
					fprintf(stdout, "%#jx",
					       (intmax_t)scsi_8btou64(tmp_lun));
					no_more = 1;
				} else {
					fprintf(stderr, "Unknown Extended LUN"
						"Address method %#x, length "
						"code %#x", eam_code,
						field_len_code);
					no_more = 1;
				}
				break;
			}
			default:
				fprintf(stderr, "Unknown LUN address method "
					"%#x\n", lundata->luns[i].lundata[0] &
					RPL_LUNDATA_ATYP_MASK);
				break;
			}
			/*
			 * For the flat addressing method, there are no
			 * other levels after it.
			 */
			if (no_more != 0)
				break;
		}
		fprintf(stdout, "\n");
	}

bailout:

	cam_freeccb(ccb);

	free(lundata);

	return (retval);
}

static int
scsireadcapacity(struct cam_device *device, int argc, char **argv,
		 char *combinedopt, int task_attr, int retry_count, int timeout)
{
	union ccb *ccb;
	int blocksizeonly, humanize, numblocks, quiet, sizeonly, baseten, longonly;
	struct scsi_read_capacity_data rcap;
	struct scsi_read_capacity_data_long rcaplong;
	uint64_t maxsector;
	uint32_t block_len;
	int retval;
	int c;

	blocksizeonly = 0;
	humanize = 0;
	longonly = 0;
	numblocks = 0;
	quiet = 0;
	sizeonly = 0;
	baseten = 0;
	retval = 0;

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("%s: error allocating ccb", __func__);
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'b':
			blocksizeonly++;
			break;
		case 'h':
			humanize++;
			baseten = 0;
			break;
		case 'H':
			humanize++;
			baseten++;
			break;
		case 'l':
			longonly++;
			break;
		case 'N':
			numblocks++;
			break;
		case 'q':
			quiet++;
			break;
		case 's':
			sizeonly++;
			break;
		default:
			break;
		}
	}

	if ((blocksizeonly != 0)
	 && (numblocks != 0)) {
		warnx("%s: you can only specify one of -b or -N", __func__);
		retval = 1;
		goto bailout;
	}

	if ((blocksizeonly != 0)
	 && (sizeonly != 0)) {
		warnx("%s: you can only specify one of -b or -s", __func__);
		retval = 1;
		goto bailout;
	}

	if ((humanize != 0)
	 && (quiet != 0)) {
		warnx("%s: you can only specify one of -h/-H or -q", __func__);
		retval = 1;
		goto bailout;
	}

	if ((humanize != 0)
	 && (blocksizeonly != 0)) {
		warnx("%s: you can only specify one of -h/-H or -b", __func__);
		retval = 1;
		goto bailout;
	}

	if (longonly != 0)
		goto long_only;

	scsi_read_capacity(&ccb->csio,
			   /*retries*/ retry_count,
			   /*cbfcnp*/ NULL,
			   /*tag_action*/ task_attr,
			   &rcap,
			   SSD_FULL_SIZE,
			   /*timeout*/ timeout ? timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		warn("error sending READ CAPACITY command");

		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);

		retval = 1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL, CAM_EPF_ALL, stderr);
		retval = 1;
		goto bailout;
	}

	maxsector = scsi_4btoul(rcap.addr);
	block_len = scsi_4btoul(rcap.length);

	/*
	 * A last block of 2^32-1 means that the true capacity is over 2TB,
	 * and we need to issue the long READ CAPACITY to get the real
	 * capacity.  Otherwise, we're all set.
	 */
	if (maxsector != 0xffffffff)
		goto do_print;

long_only:
	scsi_read_capacity_16(&ccb->csio,
			      /*retries*/ retry_count,
			      /*cbfcnp*/ NULL,
			      /*tag_action*/ task_attr,
			      /*lba*/ 0,
			      /*reladdr*/ 0,
			      /*pmi*/ 0,
			      /*rcap_buf*/ (uint8_t *)&rcaplong,
			      /*rcap_buf_len*/ sizeof(rcaplong),
			      /*sense_len*/ SSD_FULL_SIZE,
			      /*timeout*/ timeout ? timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (arglist & CAM_ARG_ERR_RECOVER)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		warn("error sending READ CAPACITY (16) command");

		if (arglist & CAM_ARG_VERBOSE)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);

		retval = 1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL, CAM_EPF_ALL, stderr);
		retval = 1;
		goto bailout;
	}

	maxsector = scsi_8btou64(rcaplong.addr);
	block_len = scsi_4btoul(rcaplong.length);

do_print:
	if (blocksizeonly == 0) {
		/*
		 * Humanize implies !quiet, and also implies numblocks.
		 */
		if (humanize != 0) {
			char tmpstr[6];
			int64_t tmpbytes;
			int ret;

			tmpbytes = (maxsector + 1) * block_len;
			ret = humanize_number(tmpstr, sizeof(tmpstr),
					      tmpbytes, "", HN_AUTOSCALE,
					      HN_B | HN_DECIMAL |
					      ((baseten != 0) ?
					      HN_DIVISOR_1000 : 0));
			if (ret == -1) {
				warnx("%s: humanize_number failed!", __func__);
				retval = 1;
				goto bailout;
			}
			fprintf(stdout, "Device Size: %s%s", tmpstr,
				(sizeonly == 0) ?  ", " : "\n");
		} else if (numblocks != 0) {
			fprintf(stdout, "%s%ju%s", (quiet == 0) ?
				"Blocks: " : "", (uintmax_t)maxsector + 1,
				(sizeonly == 0) ? ", " : "\n");
		} else {
			fprintf(stdout, "%s%ju%s", (quiet == 0) ?
				"Last Block: " : "", (uintmax_t)maxsector,
				(sizeonly == 0) ? ", " : "\n");
		}
	}
	if (sizeonly == 0)
		fprintf(stdout, "%s%u%s\n", (quiet == 0) ?
			"Block Length: " : "", block_len, (quiet == 0) ?
			" bytes" : "");
bailout:
	cam_freeccb(ccb);

	return (retval);
}

static int
smpcmd(struct cam_device *device, int argc, char **argv, char *combinedopt,
       int retry_count, int timeout)
{
	int c, error = 0;
	union ccb *ccb;
	uint8_t *smp_request = NULL, *smp_response = NULL;
	int request_size = 0, response_size = 0;
	int fd_request = 0, fd_response = 0;
	char *datastr = NULL;
	struct get_hook hook;
	int retval;
	int flags = 0;

	/*
	 * Note that at the moment we don't support sending SMP CCBs to
	 * devices that aren't probed by CAM.
	 */
	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->smpio);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'R':
			arglist |= CAM_ARG_CMD_IN;
			response_size = strtol(optarg, NULL, 0);
			if (response_size <= 0) {
				warnx("invalid number of response bytes %d",
				      response_size);
				error = 1;
				goto smpcmd_bailout;
			}
			hook.argc = argc - optind;
			hook.argv = argv + optind;
			hook.got = 0;
			optind++;
			datastr = cget(&hook, NULL);
			/*
			 * If the user supplied "-" instead of a format, he
			 * wants the data to be written to stdout.
			 */
			if ((datastr != NULL)
			 && (datastr[0] == '-'))
				fd_response = 1;

			smp_response = (u_int8_t *)malloc(response_size);
			if (smp_response == NULL) {
				warn("can't malloc memory for SMP response");
				error = 1;
				goto smpcmd_bailout;
			}
			break;
		case 'r':
			arglist |= CAM_ARG_CMD_OUT;
			request_size = strtol(optarg, NULL, 0);
			if (request_size <= 0) {
				warnx("invalid number of request bytes %d",
				      request_size);
				error = 1;
				goto smpcmd_bailout;
			}
			hook.argc = argc - optind;
			hook.argv = argv + optind;
			hook.got = 0;
			datastr = cget(&hook, NULL);
			smp_request = (u_int8_t *)malloc(request_size);
			if (smp_request == NULL) {
				warn("can't malloc memory for SMP request");
				error = 1;
				goto smpcmd_bailout;
			}
			bzero(smp_request, request_size);
			/*
			 * If the user supplied "-" instead of a format, he
			 * wants the data to be read from stdin.
			 */
			if ((datastr != NULL)
			 && (datastr[0] == '-'))
				fd_request = 1;
			else
				buff_encode_visit(smp_request, request_size,
						  datastr,
						  iget, &hook);
			optind += hook.got;
			break;
		default:
			break;
		}
	}

	/*
	 * If fd_data is set, and we're writing to the device, we need to
	 * read the data the user wants written from stdin.
	 */
	if ((fd_request == 1) && (arglist & CAM_ARG_CMD_OUT)) {
		ssize_t amt_read;
		int amt_to_read = request_size;
		u_int8_t *buf_ptr = smp_request;

		for (amt_read = 0; amt_to_read > 0;
		     amt_read = read(STDIN_FILENO, buf_ptr, amt_to_read)) {
			if (amt_read == -1) {
				warn("error reading data from stdin");
				error = 1;
				goto smpcmd_bailout;
			}
			amt_to_read -= amt_read;
			buf_ptr += amt_read;
		}
	}

	if (((arglist & CAM_ARG_CMD_IN) == 0)
	 || ((arglist & CAM_ARG_CMD_OUT) == 0)) {
		warnx("%s: need both the request (-r) and response (-R) "
		      "arguments", __func__);
		error = 1;
		goto smpcmd_bailout;
	}

	flags |= CAM_DEV_QFRZDIS;

	cam_fill_smpio(&ccb->smpio,
		       /*retries*/ retry_count,
		       /*cbfcnp*/ NULL,
		       /*flags*/ flags,
		       /*smp_request*/ smp_request,
		       /*smp_request_len*/ request_size,
		       /*smp_response*/ smp_response,
		       /*smp_response_len*/ response_size,
		       /*timeout*/ timeout ? timeout : 5000);

	ccb->smpio.flags = SMP_FLAG_NONE;

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		const char warnstr[] = "error sending command";

		if (retval < 0)
			warn(warnstr);
		else
			warnx(warnstr);

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
	}

	if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
	 && (response_size > 0)) {
		if (fd_response == 0) {
			buff_decode_visit(smp_response, response_size,
					  datastr, arg_put, NULL);
			fprintf(stdout, "\n");
		} else {
			ssize_t amt_written;
			int amt_to_write = response_size;
			u_int8_t *buf_ptr = smp_response;

			for (amt_written = 0; (amt_to_write > 0) &&
			     (amt_written = write(STDOUT_FILENO, buf_ptr,
						  amt_to_write)) > 0;){
				amt_to_write -= amt_written;
				buf_ptr += amt_written;
			}
			if (amt_written == -1) {
				warn("error writing data to stdout");
				error = 1;
				goto smpcmd_bailout;
			} else if ((amt_written == 0)
				&& (amt_to_write > 0)) {
				warnx("only wrote %u bytes out of %u",
				      response_size - amt_to_write,
				      response_size);
			}
		}
	}
smpcmd_bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	if (smp_request != NULL)
		free(smp_request);

	if (smp_response != NULL)
		free(smp_response);

	return (error);
}

static int
mmcsdcmd(struct cam_device *device, int argc, char **argv, char *combinedopt,
       int retry_count, int timeout)
{
	int c, error = 0;
	union ccb *ccb;
	int32_t mmc_opcode = 0, mmc_arg = 0;
	int32_t mmc_flags = -1;
	int retval;
	int is_write = 0;
	int is_bw_4 = 0, is_bw_1 = 0;
	int is_highspeed = 0, is_stdspeed = 0;
	int is_info_request = 0;
	int flags = 0;
	uint8_t mmc_data_byte = 0;

	/* For IO_RW_EXTENDED command */
	uint8_t *mmc_data = NULL;
	struct mmc_data mmc_d;
	int mmc_data_len = 0;

	/*
	 * Note that at the moment we don't support sending SMP CCBs to
	 * devices that aren't probed by CAM.
	 */
	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		return (1);
	}

	bzero(&(&ccb->ccb_h)[1],
	      sizeof(union ccb) - sizeof(struct ccb_hdr));

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case '4':
			is_bw_4 = 1;
			break;
		case '1':
			is_bw_1 = 1;
			break;
		case 'S':
			if (!strcmp(optarg, "high"))
				is_highspeed = 1;
			else
				is_stdspeed = 1;
			break;
		case 'I':
			is_info_request = 1;
			break;
		case 'c':
			mmc_opcode = strtol(optarg, NULL, 0);
			if (mmc_opcode < 0) {
				warnx("invalid MMC opcode %d",
				      mmc_opcode);
				error = 1;
				goto mmccmd_bailout;
			}
			break;
		case 'a':
			mmc_arg = strtol(optarg, NULL, 0);
			if (mmc_arg < 0) {
				warnx("invalid MMC arg %d",
				      mmc_arg);
				error = 1;
				goto mmccmd_bailout;
			}
			break;
		case 'f':
			mmc_flags = strtol(optarg, NULL, 0);
			if (mmc_flags < 0) {
				warnx("invalid MMC flags %d",
				      mmc_flags);
				error = 1;
				goto mmccmd_bailout;
			}
			break;
		case 'l':
			mmc_data_len = strtol(optarg, NULL, 0);
			if (mmc_data_len <= 0) {
				warnx("invalid MMC data len %d",
				      mmc_data_len);
				error = 1;
				goto mmccmd_bailout;
			}
			break;
		case 'W':
			is_write = 1;
			break;
		case 'b':
			mmc_data_byte = strtol(optarg, NULL, 0);
			break;
		default:
			break;
		}
	}
	flags |= CAM_DEV_QFRZDIS; /* masks are broken?! */

	/* If flags are left default, supply the right flags */
	if (mmc_flags < 0)
		switch (mmc_opcode) {
		case MMC_GO_IDLE_STATE:
			mmc_flags = MMC_RSP_NONE | MMC_CMD_BC;
			break;
		case IO_SEND_OP_COND:
			mmc_flags = MMC_RSP_R4;
			break;
		case SD_SEND_RELATIVE_ADDR:
			mmc_flags = MMC_RSP_R6 | MMC_CMD_BCR;
			break;
		case MMC_SELECT_CARD:
			mmc_flags = MMC_RSP_R1B | MMC_CMD_AC;
			mmc_arg = mmc_arg << 16;
			break;
		case SD_IO_RW_DIRECT:
			mmc_flags = MMC_RSP_R5 | MMC_CMD_AC;
			mmc_arg = SD_IO_RW_ADR(mmc_arg);
			if (is_write)
				mmc_arg |= SD_IO_RW_WR | SD_IO_RW_RAW | SD_IO_RW_DAT(mmc_data_byte);
			break;
		case SD_IO_RW_EXTENDED:
			mmc_flags = MMC_RSP_R5 | MMC_CMD_ADTC;
			mmc_arg = SD_IO_RW_ADR(mmc_arg);
			int len_arg = mmc_data_len;
			if (mmc_data_len == 512)
				len_arg = 0;

			// Byte mode
			mmc_arg |= SD_IOE_RW_LEN(len_arg) | SD_IO_RW_INCR;
			// Block mode
//                        mmc_arg |= SD_IOE_RW_BLK | SD_IOE_RW_LEN(len_arg) | SD_IO_RW_INCR;
			break;
		default:
			mmc_flags = MMC_RSP_R1;
			break;
		}

	// Switch bus width instead of sending IO command
	if (is_bw_4 || is_bw_1) {
		struct ccb_trans_settings_mmc *cts;
		ccb->ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
		ccb->ccb_h.flags = 0;
		cts = &ccb->cts.proto_specific.mmc;
		cts->ios.bus_width = is_bw_4 == 1 ? bus_width_4 : bus_width_1;
		cts->ios_valid = MMC_BW;
		if (((retval = cam_send_ccb(device, ccb)) < 0)
		    || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
			warn("Error sending command");
		} else {
			printf("Parameters set OK\n");
		}
		cam_freeccb(ccb);
		return (retval);
	}

	// Switch bus speed instead of sending IO command
	if (is_stdspeed || is_highspeed) {
		struct ccb_trans_settings_mmc *cts;
		ccb->ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
		ccb->ccb_h.flags = 0;
		cts = &ccb->cts.proto_specific.mmc;
		cts->ios.timing = is_highspeed == 1 ? bus_timing_hs : bus_timing_normal;
		cts->ios_valid = MMC_BT;
		if (((retval = cam_send_ccb(device, ccb)) < 0)
		    || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
			warn("Error sending command");
		} else {
			printf("Speed set OK (HS: %d)\n", is_highspeed);
		}
		cam_freeccb(ccb);
		return (retval);
	}

	// Get information about controller and its settings
	if (is_info_request) {
		ccb->ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		ccb->ccb_h.flags = 0;
		struct ccb_trans_settings_mmc *cts;
		cts = &ccb->cts.proto_specific.mmc;
		if (((retval = cam_send_ccb(device, ccb)) < 0)
		    || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
			warn("Error sending command");
			return (retval);
		}
		printf("Host controller information\n");
		printf("Host OCR: 0x%x\n", cts->host_ocr);
		printf("Min frequency: %u KHz\n", cts->host_f_min / 1000);
		printf("Max frequency: %u MHz\n", cts->host_f_max / 1000000);
		printf("Supported bus width: ");
		if (cts->host_caps & MMC_CAP_4_BIT_DATA)
			printf(" 4 bit\n");
		if (cts->host_caps & MMC_CAP_8_BIT_DATA)
			printf(" 8 bit\n");
		printf("\nCurrent settings:\n");
		printf("Bus width: ");
		switch (cts->ios.bus_width) {
		case bus_width_1:
			printf("1 bit\n");
			break;
		case bus_width_4:
			printf("4 bit\n");
			break;
		case bus_width_8:
			printf("8 bit\n");
			break;
		}
		printf("Freq: %d.%03d MHz%s\n",
		       cts->ios.clock / 1000000,
		       (cts->ios.clock / 1000) % 1000,
		       cts->ios.timing == bus_timing_hs ? "(high-speed timing)" : "");
		return (0);
	}

	printf("CMD %d arg %d flags %02x\n", mmc_opcode, mmc_arg, mmc_flags);

	if (mmc_data_len > 0) {
		flags |= CAM_DIR_IN;
		mmc_data = malloc(mmc_data_len);
		memset(mmc_data, 0, mmc_data_len);
		mmc_d.len = mmc_data_len;
		mmc_d.data = mmc_data;
		mmc_d.flags = MMC_DATA_READ;
	} else flags |= CAM_DIR_NONE;

	cam_fill_mmcio(&ccb->mmcio,
		       /*retries*/ retry_count,
		       /*cbfcnp*/ NULL,
		       /*flags*/ flags,
		       /*mmc_opcode*/ mmc_opcode,
		       /*mmc_arg*/ mmc_arg,
		       /*mmc_flags*/ mmc_flags,
		       /*mmc_data*/ mmc_data_len > 0 ? &mmc_d : NULL,
		       /*timeout*/ timeout ? timeout : 5000);

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		const char warnstr[] = "error sending command";

		if (retval < 0)
			warn(warnstr);
		else
			warnx(warnstr);

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
	}

	if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)) {
		printf("MMCIO: error %d, %08x %08x %08x %08x\n",
		       ccb->mmcio.cmd.error, ccb->mmcio.cmd.resp[0],
		       ccb->mmcio.cmd.resp[1],
		       ccb->mmcio.cmd.resp[2],
		       ccb->mmcio.cmd.resp[3]);

		switch (mmc_opcode) {
		case SD_IO_RW_DIRECT:
			printf("IO_RW_DIRECT: resp byte %02x, cur state %d\n",
			       SD_R5_DATA(ccb->mmcio.cmd.resp),
			       (ccb->mmcio.cmd.resp[0] >> 12) & 0x3);
			break;
		case SD_IO_RW_EXTENDED:
			printf("IO_RW_EXTENDED: read %d bytes w/o error:\n", mmc_data_len);
			hexdump(mmc_data, mmc_data_len, NULL, 0);
			break;
		case SD_SEND_RELATIVE_ADDR:
			printf("SEND_RELATIVE_ADDR: published RCA %02x\n", ccb->mmcio.cmd.resp[0] >> 16);
			break;
		default:
			printf("No command-specific decoder for CMD %d\n", mmc_opcode);
		}
	}
mmccmd_bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	if (mmc_data_len > 0 && mmc_data != NULL)
		free(mmc_data);

	return (error);
}

static int
smpreportgeneral(struct cam_device *device, int argc, char **argv,
		 char *combinedopt, int retry_count, int timeout)
{
	union ccb *ccb;
	struct smp_report_general_request *request = NULL;
	struct smp_report_general_response *response = NULL;
	struct sbuf *sb = NULL;
	int error = 0;
	int c, long_response = 0;
	int retval;

	/*
	 * Note that at the moment we don't support sending SMP CCBs to
	 * devices that aren't probed by CAM.
	 */
	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->smpio);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'l':
			long_response = 1;
			break;
		default:
			break;
		}
	}
	request = malloc(sizeof(*request));
	if (request == NULL) {
		warn("%s: unable to allocate %zd bytes", __func__,
		     sizeof(*request));
		error = 1;
		goto bailout;
	}

	response = malloc(sizeof(*response));
	if (response == NULL) {
		warn("%s: unable to allocate %zd bytes", __func__,
		     sizeof(*response));
		error = 1;
		goto bailout;
	}

try_long:
	smp_report_general(&ccb->smpio,
			   retry_count,
			   /*cbfcnp*/ NULL,
			   request,
			   /*request_len*/ sizeof(*request),
			   (uint8_t *)response,
			   /*response_len*/ sizeof(*response),
			   /*long_response*/ long_response,
			   timeout);

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		const char warnstr[] = "error sending command";

		if (retval < 0)
			warn(warnstr);
		else
			warnx(warnstr);

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
		error = 1;
		goto bailout;
	}

	/*
	 * If the device supports the long response bit, try again and see
	 * if we can get all of the data.
	 */
	if ((response->long_response & SMP_RG_LONG_RESPONSE)
	 && (long_response == 0)) {
		ccb->ccb_h.status = CAM_REQ_INPROG;
		CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->smpio);
		long_response = 1;
		goto try_long;
	}

	/*
	 * XXX KDM detect and decode SMP errors here.
	 */
	sb = sbuf_new_auto();
	if (sb == NULL) {
		warnx("%s: error allocating sbuf", __func__);
		goto bailout;
	}

	smp_report_general_sbuf(response, sizeof(*response), sb);

	if (sbuf_finish(sb) != 0) {
		warnx("%s: sbuf_finish", __func__);
		goto bailout;
	}

	printf("%s", sbuf_data(sb));

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	if (request != NULL)
		free(request);

	if (response != NULL)
		free(response);

	if (sb != NULL)
		sbuf_delete(sb);

	return (error);
}

static struct camcontrol_opts phy_ops[] = {
	{"nop", SMP_PC_PHY_OP_NOP, CAM_ARG_NONE, NULL},
	{"linkreset", SMP_PC_PHY_OP_LINK_RESET, CAM_ARG_NONE, NULL},
	{"hardreset", SMP_PC_PHY_OP_HARD_RESET, CAM_ARG_NONE, NULL},
	{"disable", SMP_PC_PHY_OP_DISABLE, CAM_ARG_NONE, NULL},
	{"clearerrlog", SMP_PC_PHY_OP_CLEAR_ERR_LOG, CAM_ARG_NONE, NULL},
	{"clearaffiliation", SMP_PC_PHY_OP_CLEAR_AFFILIATON, CAM_ARG_NONE,NULL},
	{"sataportsel", SMP_PC_PHY_OP_TRANS_SATA_PSS, CAM_ARG_NONE, NULL},
	{"clearitnl", SMP_PC_PHY_OP_CLEAR_STP_ITN_LS, CAM_ARG_NONE, NULL},
	{"setdevname", SMP_PC_PHY_OP_SET_ATT_DEV_NAME, CAM_ARG_NONE, NULL},
	{NULL, 0, 0, NULL}
};

static int
smpphycontrol(struct cam_device *device, int argc, char **argv,
	      char *combinedopt, int retry_count, int timeout)
{
	union ccb *ccb;
	struct smp_phy_control_request *request = NULL;
	struct smp_phy_control_response *response = NULL;
	int long_response = 0;
	int retval = 0;
	int phy = -1;
	uint32_t phy_operation = SMP_PC_PHY_OP_NOP;
	int phy_op_set = 0;
	uint64_t attached_dev_name = 0;
	int dev_name_set = 0;
	uint32_t min_plr = 0, max_plr = 0;
	uint32_t pp_timeout_val = 0;
	int slumber_partial = 0;
	int set_pp_timeout_val = 0;
	int c;

	/*
	 * Note that at the moment we don't support sending SMP CCBs to
	 * devices that aren't probed by CAM.
	 */
	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->smpio);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'a':
		case 'A':
		case 's':
		case 'S': {
			int enable = -1;

			if (strcasecmp(optarg, "enable") == 0)
				enable = 1;
			else if (strcasecmp(optarg, "disable") == 0)
				enable = 2;
			else {
				warnx("%s: Invalid argument %s", __func__,
				      optarg);
				retval = 1;
				goto bailout;
			}
			switch (c) {
			case 's':
				slumber_partial |= enable <<
						   SMP_PC_SAS_SLUMBER_SHIFT;
				break;
			case 'S':
				slumber_partial |= enable <<
						   SMP_PC_SAS_PARTIAL_SHIFT;
				break;
			case 'a':
				slumber_partial |= enable <<
						   SMP_PC_SATA_SLUMBER_SHIFT;
				break;
			case 'A':
				slumber_partial |= enable <<
						   SMP_PC_SATA_PARTIAL_SHIFT;
				break;
			default:
				warnx("%s: programmer error", __func__);
				retval = 1;
				goto bailout;
				break; /*NOTREACHED*/
			}
			break;
		}
		case 'd':
			attached_dev_name = (uintmax_t)strtoumax(optarg,
								 NULL,0);
			dev_name_set = 1;
			break;
		case 'l':
			long_response = 1;
			break;
		case 'm':
			/*
			 * We don't do extensive checking here, so this
			 * will continue to work when new speeds come out.
			 */
			min_plr = strtoul(optarg, NULL, 0);
			if ((min_plr == 0)
			 || (min_plr > 0xf)) {
				warnx("%s: invalid link rate %x",
				      __func__, min_plr);
				retval = 1;
				goto bailout;
			}
			break;
		case 'M':
			/*
			 * We don't do extensive checking here, so this
			 * will continue to work when new speeds come out.
			 */
			max_plr = strtoul(optarg, NULL, 0);
			if ((max_plr == 0)
			 || (max_plr > 0xf)) {
				warnx("%s: invalid link rate %x",
				      __func__, max_plr);
				retval = 1;
				goto bailout;
			}
			break;
		case 'o': {
			camcontrol_optret optreturn;
			cam_argmask argnums;
			const char *subopt;

			if (phy_op_set != 0) {
				warnx("%s: only one phy operation argument "
				      "(-o) allowed", __func__);
				retval = 1;
				goto bailout;
			}

			phy_op_set = 1;

			/*
			 * Allow the user to specify the phy operation
			 * numerically, as well as with a name.  This will
			 * future-proof it a bit, so options that are added
			 * in future specs can be used.
			 */
			if (isdigit(optarg[0])) {
				phy_operation = strtoul(optarg, NULL, 0);
				if ((phy_operation == 0)
				 || (phy_operation > 0xff)) {
					warnx("%s: invalid phy operation %#x",
					      __func__, phy_operation);
					retval = 1;
					goto bailout;
				}
				break;
			}
			optreturn = getoption(phy_ops, optarg, &phy_operation,
					      &argnums, &subopt);

			if (optreturn == CC_OR_AMBIGUOUS) {
				warnx("%s: ambiguous option %s", __func__,
				      optarg);
				usage(0);
				retval = 1;
				goto bailout;
			} else if (optreturn == CC_OR_NOT_FOUND) {
				warnx("%s: option %s not found", __func__,
				      optarg);
				usage(0);
				retval = 1;
				goto bailout;
			}
			break;
		}
		case 'p':
			phy = atoi(optarg);
			break;
		case 'T':
			pp_timeout_val = strtoul(optarg, NULL, 0);
			if (pp_timeout_val > 15) {
				warnx("%s: invalid partial pathway timeout "
				      "value %u, need a value less than 16",
				      __func__, pp_timeout_val);
				retval = 1;
				goto bailout;
			}
			set_pp_timeout_val = 1;
			break;
		default:
			break;
		}
	}

	if (phy == -1) {
		warnx("%s: a PHY (-p phy) argument is required",__func__);
		retval = 1;
		goto bailout;
	}

	if (((dev_name_set != 0)
	  && (phy_operation != SMP_PC_PHY_OP_SET_ATT_DEV_NAME))
	 || ((phy_operation == SMP_PC_PHY_OP_SET_ATT_DEV_NAME)
	  && (dev_name_set == 0))) {
		warnx("%s: -d name and -o setdevname arguments both "
		      "required to set device name", __func__);
		retval = 1;
		goto bailout;
	}

	request = malloc(sizeof(*request));
	if (request == NULL) {
		warn("%s: unable to allocate %zd bytes", __func__,
		     sizeof(*request));
		retval = 1;
		goto bailout;
	}

	response = malloc(sizeof(*response));
	if (response == NULL) {
		warn("%s: unable to allocate %zd bytes", __func__,
		     sizeof(*response));
		retval = 1;
		goto bailout;
	}

	smp_phy_control(&ccb->smpio,
			retry_count,
			/*cbfcnp*/ NULL,
			request,
			sizeof(*request),
			(uint8_t *)response,
			sizeof(*response),
			long_response,
			/*expected_exp_change_count*/ 0,
			phy,
			phy_operation,
			(set_pp_timeout_val != 0) ? 1 : 0,
			attached_dev_name,
			min_plr,
			max_plr,
			slumber_partial,
			pp_timeout_val,
			timeout);

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		const char warnstr[] = "error sending command";

		if (retval < 0)
			warn(warnstr);
		else
			warnx(warnstr);

		if (arglist & CAM_ARG_VERBOSE) {
			/*
			 * Use CAM_EPF_NORMAL so we only get one line of
			 * SMP command decoding.
			 */
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_NORMAL, stderr);
		}
		retval = 1;
		goto bailout;
	}

	/* XXX KDM print out something here for success? */
bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	if (request != NULL)
		free(request);

	if (response != NULL)
		free(response);

	return (retval);
}

static int
smpmaninfo(struct cam_device *device, int argc, char **argv,
	   char *combinedopt, int retry_count, int timeout)
{
	union ccb *ccb;
	struct smp_report_manuf_info_request request;
	struct smp_report_manuf_info_response response;
	struct sbuf *sb = NULL;
	int long_response = 0;
	int retval = 0;
	int c;

	/*
	 * Note that at the moment we don't support sending SMP CCBs to
	 * devices that aren't probed by CAM.
	 */
	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->smpio);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'l':
			long_response = 1;
			break;
		default:
			break;
		}
	}
	bzero(&request, sizeof(request));
	bzero(&response, sizeof(response));

	smp_report_manuf_info(&ccb->smpio,
			      retry_count,
			      /*cbfcnp*/ NULL,
			      &request,
			      sizeof(request),
			      (uint8_t *)&response,
			      sizeof(response),
			      long_response,
			      timeout);

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		const char warnstr[] = "error sending command";

		if (retval < 0)
			warn(warnstr);
		else
			warnx(warnstr);

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
		retval = 1;
		goto bailout;
	}

	sb = sbuf_new_auto();
	if (sb == NULL) {
		warnx("%s: error allocating sbuf", __func__);
		goto bailout;
	}

	smp_report_manuf_info_sbuf(&response, sizeof(response), sb);

	if (sbuf_finish(sb) != 0) {
		warnx("%s: sbuf_finish", __func__);
		goto bailout;
	}

	printf("%s", sbuf_data(sb));

bailout:

	if (ccb != NULL)
		cam_freeccb(ccb);

	if (sb != NULL)
		sbuf_delete(sb);

	return (retval);
}

static int
getdevid(struct cam_devitem *item)
{
	int retval = 0;
	union ccb *ccb = NULL;

	struct cam_device *dev;

	dev = cam_open_btl(item->dev_match.path_id,
			   item->dev_match.target_id,
			   item->dev_match.target_lun, O_RDWR, NULL);

	if (dev == NULL) {
		warnx("%s", cam_errbuf);
		retval = 1;
		goto bailout;
	}

	item->device_id_len = 0;

	ccb = cam_getccb(dev);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		retval = 1;
		goto bailout;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->cdai);

	/*
	 * On the first try, we just probe for the size of the data, and
	 * then allocate that much memory and try again.
	 */
retry:
	ccb->ccb_h.func_code = XPT_DEV_ADVINFO;
	ccb->ccb_h.flags = CAM_DIR_IN;
	ccb->cdai.flags = CDAI_FLAG_NONE;
	ccb->cdai.buftype = CDAI_TYPE_SCSI_DEVID;
	ccb->cdai.bufsiz = item->device_id_len;
	if (item->device_id_len != 0)
		ccb->cdai.buf = (uint8_t *)item->device_id;

	if (cam_send_ccb(dev, ccb) < 0) {
		warn("%s: error sending XPT_GDEV_ADVINFO CCB", __func__);
		retval = 1;
		goto bailout;
	}

	if (ccb->ccb_h.status != CAM_REQ_CMP) {
		warnx("%s: CAM status %#x", __func__, ccb->ccb_h.status);
		retval = 1;
		goto bailout;
	}

	if (item->device_id_len == 0) {
		/*
		 * This is our first time through.  Allocate the buffer,
		 * and then go back to get the data.
		 */
		if (ccb->cdai.provsiz == 0) {
			warnx("%s: invalid .provsiz field returned with "
			     "XPT_GDEV_ADVINFO CCB", __func__);
			retval = 1;
			goto bailout;
		}
		item->device_id_len = ccb->cdai.provsiz;
		item->device_id = malloc(item->device_id_len);
		if (item->device_id == NULL) {
			warn("%s: unable to allocate %d bytes", __func__,
			     item->device_id_len);
			retval = 1;
			goto bailout;
		}
		ccb->ccb_h.status = CAM_REQ_INPROG;
		goto retry;
	}

bailout:
	if (dev != NULL)
		cam_close_device(dev);

	if (ccb != NULL)
		cam_freeccb(ccb);

	return (retval);
}

/*
 * XXX KDM merge this code with getdevtree()?
 */
static int
buildbusdevlist(struct cam_devlist *devlist)
{
	union ccb ccb;
	int bufsize, fd = -1;
	struct dev_match_pattern *patterns;
	struct cam_devitem *item = NULL;
	int skip_device = 0;
	int retval = 0;

	if ((fd = open(XPT_DEVICE, O_RDWR)) == -1) {
		warn("couldn't open %s", XPT_DEVICE);
		return (1);
	}

	bzero(&ccb, sizeof(union ccb));

	ccb.ccb_h.path_id = CAM_XPT_PATH_ID;
	ccb.ccb_h.target_id = CAM_TARGET_WILDCARD;
	ccb.ccb_h.target_lun = CAM_LUN_WILDCARD;

	ccb.ccb_h.func_code = XPT_DEV_MATCH;
	bufsize = sizeof(struct dev_match_result) * 100;
	ccb.cdm.match_buf_len = bufsize;
	ccb.cdm.matches = (struct dev_match_result *)malloc(bufsize);
	if (ccb.cdm.matches == NULL) {
		warnx("can't malloc memory for matches");
		close(fd);
		return (1);
	}
	ccb.cdm.num_matches = 0;
	ccb.cdm.num_patterns = 2;
	ccb.cdm.pattern_buf_len = sizeof(struct dev_match_pattern) *
		ccb.cdm.num_patterns;

	patterns = (struct dev_match_pattern *)malloc(ccb.cdm.pattern_buf_len);
	if (patterns == NULL) {
		warnx("can't malloc memory for patterns");
		retval = 1;
		goto bailout;
	}

	ccb.cdm.patterns = patterns;
	bzero(patterns, ccb.cdm.pattern_buf_len);

	patterns[0].type = DEV_MATCH_DEVICE;
	patterns[0].pattern.device_pattern.flags = DEV_MATCH_PATH;
	patterns[0].pattern.device_pattern.path_id = devlist->path_id;
	patterns[1].type = DEV_MATCH_PERIPH;
	patterns[1].pattern.periph_pattern.flags = PERIPH_MATCH_PATH;
	patterns[1].pattern.periph_pattern.path_id = devlist->path_id;

	/*
	 * We do the ioctl multiple times if necessary, in case there are
	 * more than 100 nodes in the EDT.
	 */
	do {
		unsigned int i;

		if (ioctl(fd, CAMIOCOMMAND, &ccb) == -1) {
			warn("error sending CAMIOCOMMAND ioctl");
			retval = 1;
			goto bailout;
		}

		if ((ccb.ccb_h.status != CAM_REQ_CMP)
		 || ((ccb.cdm.status != CAM_DEV_MATCH_LAST)
		    && (ccb.cdm.status != CAM_DEV_MATCH_MORE))) {
			warnx("got CAM error %#x, CDM error %d\n",
			      ccb.ccb_h.status, ccb.cdm.status);
			retval = 1;
			goto bailout;
		}

		for (i = 0; i < ccb.cdm.num_matches; i++) {
			switch (ccb.cdm.matches[i].type) {
			case DEV_MATCH_DEVICE: {
				struct device_match_result *dev_result;

				dev_result =
				     &ccb.cdm.matches[i].result.device_result;

				if (dev_result->flags &
				    DEV_RESULT_UNCONFIGURED) {
					skip_device = 1;
					break;
				} else
					skip_device = 0;

				item = malloc(sizeof(*item));
				if (item == NULL) {
					warn("%s: unable to allocate %zd bytes",
					     __func__, sizeof(*item));
					retval = 1;
					goto bailout;
				}
				bzero(item, sizeof(*item));
				bcopy(dev_result, &item->dev_match,
				      sizeof(*dev_result));
				STAILQ_INSERT_TAIL(&devlist->dev_queue, item,
						   links);

				if (getdevid(item) != 0) {
					retval = 1;
					goto bailout;
				}
				break;
			}
			case DEV_MATCH_PERIPH: {
				struct periph_match_result *periph_result;

				periph_result =
				      &ccb.cdm.matches[i].result.periph_result;

				if (skip_device != 0)
					break;
				item->num_periphs++;
				item->periph_matches = realloc(
					item->periph_matches,
					item->num_periphs *
					sizeof(struct periph_match_result));
				if (item->periph_matches == NULL) {
					warn("%s: error allocating periph "
					     "list", __func__);
					retval = 1;
					goto bailout;
				}
				bcopy(periph_result, &item->periph_matches[
				      item->num_periphs - 1],
				      sizeof(*periph_result));
				break;
			}
			default:
				fprintf(stderr, "%s: unexpected match "
					"type %d\n", __func__,
					ccb.cdm.matches[i].type);
				retval = 1;
				goto bailout;
				break; /*NOTREACHED*/
			}
		}
	} while ((ccb.ccb_h.status == CAM_REQ_CMP)
		&& (ccb.cdm.status == CAM_DEV_MATCH_MORE));
bailout:

	if (fd != -1)
		close(fd);

	free(patterns);

	free(ccb.cdm.matches);

	if (retval != 0)
		freebusdevlist(devlist);

	return (retval);
}

static void
freebusdevlist(struct cam_devlist *devlist)
{
	struct cam_devitem *item, *item2;

	STAILQ_FOREACH_SAFE(item, &devlist->dev_queue, links, item2) {
		STAILQ_REMOVE(&devlist->dev_queue, item, cam_devitem,
			      links);
		free(item->device_id);
		free(item->periph_matches);
		free(item);
	}
}

static struct cam_devitem *
findsasdevice(struct cam_devlist *devlist, uint64_t sasaddr)
{
	struct cam_devitem *item;

	STAILQ_FOREACH(item, &devlist->dev_queue, links) {
		struct scsi_vpd_id_descriptor *idd;

		/*
		 * XXX KDM look for LUN IDs as well?
		 */
		idd = scsi_get_devid(item->device_id,
					   item->device_id_len,
					   scsi_devid_is_sas_target);
		if (idd == NULL)
			continue;

		if (scsi_8btou64(idd->identifier) == sasaddr)
			return (item);
	}

	return (NULL);
}

static int
smpphylist(struct cam_device *device, int argc, char **argv,
	   char *combinedopt, int retry_count, int timeout)
{
	struct smp_report_general_request *rgrequest = NULL;
	struct smp_report_general_response *rgresponse = NULL;
	struct smp_discover_request *disrequest = NULL;
	struct smp_discover_response *disresponse = NULL;
	struct cam_devlist devlist;
	union ccb *ccb;
	int long_response = 0;
	int num_phys = 0;
	int quiet = 0;
	int retval;
	int i, c;

	/*
	 * Note that at the moment we don't support sending SMP CCBs to
	 * devices that aren't probed by CAM.
	 */
	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->smpio);
	STAILQ_INIT(&devlist.dev_queue);

	rgrequest = malloc(sizeof(*rgrequest));
	if (rgrequest == NULL) {
		warn("%s: unable to allocate %zd bytes", __func__,
		     sizeof(*rgrequest));
		retval = 1;
		goto bailout;
	}

	rgresponse = malloc(sizeof(*rgresponse));
	if (rgresponse == NULL) {
		warn("%s: unable to allocate %zd bytes", __func__,
		     sizeof(*rgresponse));
		retval = 1;
		goto bailout;
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'l':
			long_response = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		default:
			break;
		}
	}

	smp_report_general(&ccb->smpio,
			   retry_count,
			   /*cbfcnp*/ NULL,
			   rgrequest,
			   /*request_len*/ sizeof(*rgrequest),
			   (uint8_t *)rgresponse,
			   /*response_len*/ sizeof(*rgresponse),
			   /*long_response*/ long_response,
			   timeout);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (((retval = cam_send_ccb(device, ccb)) < 0)
	 || ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)) {
		const char warnstr[] = "error sending command";

		if (retval < 0)
			warn(warnstr);
		else
			warnx(warnstr);

		if (arglist & CAM_ARG_VERBOSE) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
		retval = 1;
		goto bailout;
	}

	num_phys = rgresponse->num_phys;

	if (num_phys == 0) {
		if (quiet == 0)
			fprintf(stdout, "%s: No Phys reported\n", __func__);
		retval = 1;
		goto bailout;
	}

	devlist.path_id = device->path_id;

	retval = buildbusdevlist(&devlist);
	if (retval != 0)
		goto bailout;

	if (quiet == 0) {
		fprintf(stdout, "%d PHYs:\n", num_phys);
		fprintf(stdout, "PHY  Attached SAS Address\n");
	}

	disrequest = malloc(sizeof(*disrequest));
	if (disrequest == NULL) {
		warn("%s: unable to allocate %zd bytes", __func__,
		     sizeof(*disrequest));
		retval = 1;
		goto bailout;
	}

	disresponse = malloc(sizeof(*disresponse));
	if (disresponse == NULL) {
		warn("%s: unable to allocate %zd bytes", __func__,
		     sizeof(*disresponse));
		retval = 1;
		goto bailout;
	}

	for (i = 0; i < num_phys; i++) {
		struct cam_devitem *item;
		struct device_match_result *dev_match;
		char vendor[16], product[48], revision[16];
		char tmpstr[256];
		int j;

		CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->smpio);

		ccb->ccb_h.status = CAM_REQ_INPROG;
		ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

		smp_discover(&ccb->smpio,
			     retry_count,
			     /*cbfcnp*/ NULL,
			     disrequest,
			     sizeof(*disrequest),
			     (uint8_t *)disresponse,
			     sizeof(*disresponse),
			     long_response,
			     /*ignore_zone_group*/ 0,
			     /*phy*/ i,
			     timeout);

		if (((retval = cam_send_ccb(device, ccb)) < 0)
		 || (((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
		  && (disresponse->function_result != SMP_FR_PHY_VACANT))) {
			const char warnstr[] = "error sending command";

			if (retval < 0)
				warn(warnstr);
			else
				warnx(warnstr);

			if (arglist & CAM_ARG_VERBOSE) {
				cam_error_print(device, ccb, CAM_ESF_ALL,
						CAM_EPF_ALL, stderr);
			}
			retval = 1;
			goto bailout;
		}

		if (disresponse->function_result == SMP_FR_PHY_VACANT) {
			if (quiet == 0)
				fprintf(stdout, "%3d  <vacant>\n", i);
			continue;
		}

		if (disresponse->attached_device == SMP_DIS_AD_TYPE_NONE) {
			item = NULL;
		} else {
			item = findsasdevice(&devlist,
			    scsi_8btou64(disresponse->attached_sas_address));
		}

		if ((quiet == 0)
		 || (item != NULL)) {
			fprintf(stdout, "%3d  0x%016jx", i,
				(uintmax_t)scsi_8btou64(
				disresponse->attached_sas_address));
			if (item == NULL) {
				fprintf(stdout, "\n");
				continue;
			}
		} else if (quiet != 0)
			continue;

		dev_match = &item->dev_match;

		if (dev_match->protocol == PROTO_SCSI) {
			cam_strvis(vendor, dev_match->inq_data.vendor,
				   sizeof(dev_match->inq_data.vendor),
				   sizeof(vendor));
			cam_strvis(product, dev_match->inq_data.product,
				   sizeof(dev_match->inq_data.product),
				   sizeof(product));
			cam_strvis(revision, dev_match->inq_data.revision,
				   sizeof(dev_match->inq_data.revision),
				   sizeof(revision));
			sprintf(tmpstr, "<%s %s %s>", vendor, product,
				revision);
		} else if ((dev_match->protocol == PROTO_ATA)
			|| (dev_match->protocol == PROTO_SATAPM)) {
			cam_strvis(product, dev_match->ident_data.model,
				   sizeof(dev_match->ident_data.model),
				   sizeof(product));
			cam_strvis(revision, dev_match->ident_data.revision,
				   sizeof(dev_match->ident_data.revision),
				   sizeof(revision));
			sprintf(tmpstr, "<%s %s>", product, revision);
		} else {
			sprintf(tmpstr, "<>");
		}
		fprintf(stdout, "   %-33s ", tmpstr);

		/*
		 * If we have 0 periphs, that's a bug...
		 */
		if (item->num_periphs == 0) {
			fprintf(stdout, "\n");
			continue;
		}

		fprintf(stdout, "(");
		for (j = 0; j < item->num_periphs; j++) {
			if (j > 0)
				fprintf(stdout, ",");

			fprintf(stdout, "%s%d",
				item->periph_matches[j].periph_name,
				item->periph_matches[j].unit_number);

		}
		fprintf(stdout, ")\n");
	}
bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	free(rgrequest);

	free(rgresponse);

	free(disrequest);

	free(disresponse);

	freebusdevlist(&devlist);

	return (retval);
}

static int
atapm(struct cam_device *device, int argc, char **argv,
		 char *combinedopt, int retry_count, int timeout)
{
	union ccb *ccb;
	int retval = 0;
	int t = -1;
	int c;
	u_char cmd, sc;

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("%s: error allocating ccb", __func__);
		return (1);
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 't':
			t = atoi(optarg);
			break;
		default:
			break;
		}
	}
	if (strcmp(argv[1], "idle") == 0) {
		if (t == -1)
			cmd = ATA_IDLE_IMMEDIATE;
		else
			cmd = ATA_IDLE_CMD;
	} else if (strcmp(argv[1], "standby") == 0) {
		if (t == -1)
			cmd = ATA_STANDBY_IMMEDIATE;
		else
			cmd = ATA_STANDBY_CMD;
	} else {
		cmd = ATA_SLEEP;
		t = -1;
	}

	if (t < 0)
		sc = 0;
	else if (t <= (240 * 5))
		sc = (t + 4) / 5;
	else if (t <= (252 * 5))
		/* special encoding for 21 minutes */
		sc = 252;
	else if (t <= (11 * 30 * 60))
		sc = (t - 1) / (30 * 60) + 241;
	else
		sc = 253;

	retval = ata_do_28bit_cmd(device,
	    ccb,
	    /*retries*/retry_count,
	    /*flags*/CAM_DIR_NONE,
	    /*protocol*/AP_PROTO_NON_DATA,
	    /*tag_action*/MSG_SIMPLE_Q_TAG,
	    /*command*/cmd,
	    /*features*/0,
	    /*lba*/0,
	    /*sector_count*/sc,
	    /*data_ptr*/NULL,
	    /*dxfer_len*/0,
	    /*timeout*/timeout ? timeout : 30 * 1000,
	    /*quiet*/1);

	cam_freeccb(ccb);
	return (retval);
}

static int
ataaxm(struct cam_device *device, int argc, char **argv,
		 char *combinedopt, int retry_count, int timeout)
{
	union ccb *ccb;
	int retval = 0;
	int l = -1;
	int c;
	u_char cmd, sc;

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("%s: error allocating ccb", __func__);
		return (1);
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'l':
			l = atoi(optarg);
			break;
		default:
			break;
		}
	}
	sc = 0;
	if (strcmp(argv[1], "apm") == 0) {
		if (l == -1)
			cmd = 0x85;
		else {
			cmd = 0x05;
			sc = l;
		}
	} else /* aam */ {
		if (l == -1)
			cmd = 0xC2;
		else {
			cmd = 0x42;
			sc = l;
		}
	}

	retval = ata_do_28bit_cmd(device,
	    ccb,
	    /*retries*/retry_count,
	    /*flags*/CAM_DIR_NONE,
	    /*protocol*/AP_PROTO_NON_DATA,
	    /*tag_action*/MSG_SIMPLE_Q_TAG,
	    /*command*/ATA_SETFEATURES,
	    /*features*/cmd,
	    /*lba*/0,
	    /*sector_count*/sc,
	    /*data_ptr*/NULL,
	    /*dxfer_len*/0,
	    /*timeout*/timeout ? timeout : 30 * 1000,
	    /*quiet*/1);

	cam_freeccb(ccb);
	return (retval);
}

int
scsigetopcodes(struct cam_device *device, int opcode_set, int opcode,
	       int show_sa_errors, int sa_set, int service_action,
	       int timeout_desc, int task_attr, int retry_count, int timeout,
	       int verbosemode, uint32_t *fill_len, uint8_t **data_ptr)
{
	union ccb *ccb = NULL;
	uint8_t *buf = NULL;
	uint32_t alloc_len = 0, num_opcodes;
	uint32_t valid_len = 0;
	uint32_t avail_len = 0;
	struct scsi_report_supported_opcodes_all *all_hdr;
	struct scsi_report_supported_opcodes_one *one;
	int options = 0;
	int retval = 0;

	/*
	 * Make it clear that we haven't yet allocated or filled anything.
	 */
	*fill_len = 0;
	*data_ptr = NULL;

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("couldn't allocate CCB");
		retval = 1;
		goto bailout;
	}

	/* cam_getccb cleans up the header, caller has to zero the payload */
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	if (opcode_set != 0) {
		options |= RSO_OPTIONS_OC;
		num_opcodes = 1;
		alloc_len = sizeof(*one) + CAM_MAX_CDBLEN;
	} else {
		num_opcodes = 256;
		alloc_len = sizeof(*all_hdr) + (num_opcodes *
		    sizeof(struct scsi_report_supported_opcodes_descr));
	}

	if (timeout_desc != 0) {
		options |= RSO_RCTD;
		alloc_len += num_opcodes *
		    sizeof(struct scsi_report_supported_opcodes_timeout);
	}

	if (sa_set != 0) {
		options |= RSO_OPTIONS_OC_SA;
		if (show_sa_errors != 0)
			options &= ~RSO_OPTIONS_OC;
	}

retry_alloc:
	if (buf != NULL) {
		free(buf);
		buf = NULL;
	}

	buf = malloc(alloc_len);
	if (buf == NULL) {
		warn("Unable to allocate %u bytes", alloc_len);
		retval = 1;
		goto bailout;
	}
	bzero(buf, alloc_len);

	scsi_report_supported_opcodes(&ccb->csio,
				      /*retries*/ retry_count,
				      /*cbfcnp*/ NULL,
				      /*tag_action*/ task_attr,
				      /*options*/ options,
				      /*req_opcode*/ opcode,
				      /*req_service_action*/ service_action,
				      /*data_ptr*/ buf,
				      /*dxfer_len*/ alloc_len,
				      /*sense_len*/ SSD_FULL_SIZE,
				      /*timeout*/ timeout ? timeout : 10000);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (retry_count != 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		perror("error sending REPORT SUPPORTED OPERATION CODES");
		retval = 1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if (verbosemode != 0)
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		retval = 1;
		goto bailout;
	}

	valid_len = ccb->csio.dxfer_len - ccb->csio.resid;

	if (((options & RSO_OPTIONS_MASK) == RSO_OPTIONS_ALL)
	 && (valid_len >= sizeof(*all_hdr))) {
		all_hdr = (struct scsi_report_supported_opcodes_all *)buf;
		avail_len = scsi_4btoul(all_hdr->length) + sizeof(*all_hdr);
	} else if (((options & RSO_OPTIONS_MASK) != RSO_OPTIONS_ALL)
		&& (valid_len >= sizeof(*one))) {
		uint32_t cdb_length;

		one = (struct scsi_report_supported_opcodes_one *)buf;
		cdb_length = scsi_2btoul(one->cdb_length);
		avail_len = sizeof(*one) + cdb_length;
		if (one->support & RSO_ONE_CTDP) {
			struct scsi_report_supported_opcodes_timeout *td;

			td = (struct scsi_report_supported_opcodes_timeout *)
			    &buf[avail_len];
			if (valid_len >= (avail_len + sizeof(td->length))) {
				avail_len += scsi_2btoul(td->length) +
				    sizeof(td->length);
			} else {
				avail_len += sizeof(*td);
			}
		}
	}

	/*
	 * avail_len could be zero if we didn't get enough data back from
	 * thet target to determine
	 */
	if ((avail_len != 0)
	 && (avail_len > valid_len)) {
		alloc_len = avail_len;
		goto retry_alloc;
	}

	*fill_len = valid_len;
	*data_ptr = buf;
bailout:
	if (retval != 0)
		free(buf);

	cam_freeccb(ccb);

	return (retval);
}

static int
scsiprintoneopcode(struct cam_device *device, int req_opcode, int sa_set,
		   int req_sa, uint8_t *buf, uint32_t valid_len)
{
	struct scsi_report_supported_opcodes_one *one;
	struct scsi_report_supported_opcodes_timeout *td;
	uint32_t cdb_len = 0, td_len = 0;
	const char *op_desc = NULL;
	unsigned int i;
	int retval = 0;

	one = (struct scsi_report_supported_opcodes_one *)buf;

	/*
	 * If we don't have the full single opcode descriptor, no point in
	 * continuing.
	 */
	if (valid_len < __offsetof(struct scsi_report_supported_opcodes_one,
	    cdb_length)) {
		warnx("Only %u bytes returned, not enough to verify support",
		      valid_len);
		retval = 1;
		goto bailout;
	}

	op_desc = scsi_op_desc(req_opcode, &device->inq_data);

	printf("%s (0x%02x)", op_desc != NULL ? op_desc : "UNKNOWN",
	       req_opcode);
	if (sa_set != 0)
		printf(", SA 0x%x", req_sa);
	printf(": ");

	switch (one->support & RSO_ONE_SUP_MASK) {
	case RSO_ONE_SUP_UNAVAIL:
		printf("No command support information currently available\n");
		break;
	case RSO_ONE_SUP_NOT_SUP:
		printf("Command not supported\n");
		retval = 1;
		goto bailout;
		break; /*NOTREACHED*/
	case RSO_ONE_SUP_AVAIL:
		printf("Command is supported, complies with a SCSI standard\n");
		break;
	case RSO_ONE_SUP_VENDOR:
		printf("Command is supported, vendor-specific "
		       "implementation\n");
		break;
	default:
		printf("Unknown command support flags 0x%#x\n",
		       one->support & RSO_ONE_SUP_MASK);
		break;
	}

	/*
	 * If we don't have the CDB length, it isn't exactly an error, the
	 * command probably isn't supported.
	 */
	if (valid_len < __offsetof(struct scsi_report_supported_opcodes_one,
	    cdb_usage))
		goto bailout;

	cdb_len = scsi_2btoul(one->cdb_length);

	/*
	 * If our valid data doesn't include the full reported length,
	 * return.  The caller should have detected this and adjusted his
	 * allocation length to get all of the available data.
	 */
	if (valid_len < sizeof(*one) + cdb_len) {
		retval = 1;
		goto bailout;
	}

	/*
	 * If all we have is the opcode, there is no point in printing out
	 * the usage bitmap.
	 */
	if (cdb_len <= 1) {
		retval = 1;
		goto bailout;
	}

	printf("CDB usage bitmap:");
	for (i = 0; i < cdb_len; i++) {
		printf(" %02x", one->cdb_usage[i]);
	}
	printf("\n");

	/*
	 * If we don't have a timeout descriptor, we're done.
	 */
	if ((one->support & RSO_ONE_CTDP) == 0)
		goto bailout;

	/*
	 * If we don't have enough valid length to include the timeout
	 * descriptor length, we're done.
	 */
	if (valid_len < (sizeof(*one) + cdb_len + sizeof(td->length)))
		goto bailout;

	td = (struct scsi_report_supported_opcodes_timeout *)
	    &buf[sizeof(*one) + cdb_len];
	td_len = scsi_2btoul(td->length);
	td_len += sizeof(td->length);

	/*
	 * If we don't have the full timeout descriptor, we're done.
	 */
	if (td_len < sizeof(*td))
		goto bailout;

	/*
	 * If we don't have enough valid length to contain the full timeout
	 * descriptor, we're done.
	 */
	if (valid_len < (sizeof(*one) + cdb_len + td_len))
		goto bailout;

	printf("Timeout information:\n");
	printf("Command-specific:    0x%02x\n", td->cmd_specific);
	printf("Nominal timeout:     %u seconds\n",
	       scsi_4btoul(td->nominal_time));
	printf("Recommended timeout: %u seconds\n",
	       scsi_4btoul(td->recommended_time));

bailout:
	return (retval);
}

static int
scsiprintopcodes(struct cam_device *device, int td_req, uint8_t *buf,
		 uint32_t valid_len)
{
	struct scsi_report_supported_opcodes_all *hdr;
	struct scsi_report_supported_opcodes_descr *desc;
	uint32_t avail_len = 0, used_len = 0;
	uint8_t *cur_ptr;
	int retval = 0;

	if (valid_len < sizeof(*hdr)) {
		warnx("%s: not enough returned data (%u bytes) opcode list",
		      __func__, valid_len);
		retval = 1;
		goto bailout;
	}
	hdr = (struct scsi_report_supported_opcodes_all *)buf;
	avail_len = scsi_4btoul(hdr->length);
	avail_len += sizeof(hdr->length);
	/*
	 * Take the lesser of the amount of data the drive claims is
	 * available, and the amount of data the HBA says was returned.
	 */
	avail_len = MIN(avail_len, valid_len);

	used_len = sizeof(hdr->length);

	printf("%-6s %4s %8s ",
	       "Opcode", "SA", "CDB len" );

	if (td_req != 0)
		printf("%5s %6s %6s ", "CS", "Nom", "Rec");
	printf(" Description\n");

	while ((avail_len - used_len) > sizeof(*desc)) {
		struct scsi_report_supported_opcodes_timeout *td;
		uint32_t td_len;
		const char *op_desc = NULL;

		cur_ptr = &buf[used_len];
		desc = (struct scsi_report_supported_opcodes_descr *)cur_ptr;

		op_desc = scsi_op_desc(desc->opcode, &device->inq_data);
		if (op_desc == NULL)
			op_desc = "UNKNOWN";

		printf("0x%02x   %#4x %8u ", desc->opcode,
		       scsi_2btoul(desc->service_action),
		       scsi_2btoul(desc->cdb_length));

		used_len += sizeof(*desc);

		if ((desc->flags & RSO_CTDP) == 0) {
			printf(" %s\n", op_desc);
			continue;
		}

		/*
		 * If we don't have enough space to fit a timeout
		 * descriptor, then we're done.
		 */
		if (avail_len - used_len < sizeof(*td)) {
			used_len = avail_len;
			printf(" %s\n", op_desc);
			continue;
		}
		cur_ptr = &buf[used_len];
		td = (struct scsi_report_supported_opcodes_timeout *)cur_ptr;
		td_len = scsi_2btoul(td->length);
		td_len += sizeof(td->length);

		used_len += td_len;
		/*
		 * If the given timeout descriptor length is less than what
		 * we understand, skip it.
		 */
		if (td_len < sizeof(*td)) {
			printf(" %s\n", op_desc);
			continue;
		}

		printf(" 0x%02x %6u %6u  %s\n", td->cmd_specific,
		       scsi_4btoul(td->nominal_time),
		       scsi_4btoul(td->recommended_time), op_desc);
	}
bailout:
	return (retval);
}

static int
scsiopcodes(struct cam_device *device, int argc, char **argv,
	    char *combinedopt, int task_attr, int retry_count, int timeout,
	    int verbosemode)
{
	int c;
	uint32_t opcode = 0, service_action = 0;
	int td_set = 0, opcode_set = 0, sa_set = 0;
	int show_sa_errors = 1;
	uint32_t valid_len = 0;
	uint8_t *buf = NULL;
	char *endptr;
	int retval = 0;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'N':
			show_sa_errors = 0;
			break;
		case 'o':
			opcode = strtoul(optarg, &endptr, 0);
			if (*endptr != '\0') {
				warnx("Invalid opcode \"%s\", must be a number",
				      optarg);
				retval = 1;
				goto bailout;
			}
			if (opcode > 0xff) {
				warnx("Invalid opcode 0x%#x, must be between"
				      "0 and 0xff inclusive", opcode);
				retval = 1;
				goto bailout;
			}
			opcode_set = 1;
			break;
		case 's':
			service_action = strtoul(optarg, &endptr, 0);
			if (*endptr != '\0') {
				warnx("Invalid service action \"%s\", must "
				      "be a number", optarg);
				retval = 1;
				goto bailout;
			}
			if (service_action > 0xffff) {
				warnx("Invalid service action 0x%#x, must "
				      "be between 0 and 0xffff inclusive",
				      service_action);
				retval = 1;
			}
			sa_set = 1;
			break;
		case 'T':
			td_set = 1;
			break;
		default:
			break;
		}
	}

	if ((sa_set != 0)
	 && (opcode_set == 0)) {
		warnx("You must specify an opcode with -o if a service "
		      "action is given");
		retval = 1;
		goto bailout;
	}
	retval = scsigetopcodes(device, opcode_set, opcode, show_sa_errors,
				sa_set, service_action, td_set, task_attr,
				retry_count, timeout, verbosemode, &valid_len,
				&buf);
	if (retval != 0)
		goto bailout;

	if ((opcode_set != 0)
	 || (sa_set != 0)) {
		retval = scsiprintoneopcode(device, opcode, sa_set,
					    service_action, buf, valid_len);
	} else {
		retval = scsiprintopcodes(device, td_set, buf, valid_len);
	}

bailout:
	free(buf);

	return (retval);
}

#endif /* MINIMALISTIC */

static int
scsireprobe(struct cam_device *device)
{
	union ccb *ccb;
	int retval = 0;

	ccb = cam_getccb(device);

	if (ccb == NULL) {
		warnx("%s: error allocating ccb", __func__);
		return (1);
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	ccb->ccb_h.func_code = XPT_REPROBE_LUN;

	if (cam_send_ccb(device, ccb) < 0) {
		warn("error sending XPT_REPROBE_LUN CCB");
		retval = 1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL, CAM_EPF_ALL, stderr);
		retval = 1;
		goto bailout;
	}

bailout:
	cam_freeccb(ccb);

	return (retval);
}

void
usage(int printlong)
{

	fprintf(printlong ? stdout : stderr,
"usage:  camcontrol <command>  [device id][generic args][command args]\n"
"        camcontrol devlist    [-b] [-v]\n"
#ifndef MINIMALISTIC
"        camcontrol periphlist [dev_id][-n dev_name] [-u unit]\n"
"        camcontrol tur        [dev_id][generic args]\n"
"        camcontrol inquiry    [dev_id][generic args] [-D] [-S] [-R]\n"
"        camcontrol identify   [dev_id][generic args] [-v]\n"
"        camcontrol reportluns [dev_id][generic args] [-c] [-l] [-r report]\n"
"        camcontrol readcap    [dev_id][generic args] [-b] [-h] [-H] [-N]\n"
"                              [-q] [-s] [-l]\n"
"        camcontrol start      [dev_id][generic args]\n"
"        camcontrol stop       [dev_id][generic args]\n"
"        camcontrol load       [dev_id][generic args]\n"
"        camcontrol eject      [dev_id][generic args]\n"
"        camcontrol reprobe    [dev_id][generic args]\n"
#endif /* MINIMALISTIC */
"        camcontrol rescan     <all | bus[:target:lun] | dev_id>\n"
"        camcontrol reset      <all | bus[:target:lun] | dev_id>\n"
#ifndef MINIMALISTIC
"        camcontrol defects    [dev_id][generic args] <-f format> [-P][-G]\n"
"                              [-q][-s][-S offset][-X]\n"
"        camcontrol modepage   [dev_id][generic args] <-m page | -l>\n"
"                              [-P pagectl][-e | -b][-d]\n"
"        camcontrol cmd        [dev_id][generic args]\n"
"                              <-a cmd [args] | -c cmd [args]>\n"
"                              [-d] [-f] [-i len fmt|-o len fmt [args]] [-r fmt]\n"
"        camcontrol smpcmd     [dev_id][generic args]\n"
"                              <-r len fmt [args]> <-R len fmt [args]>\n"
"        camcontrol smprg      [dev_id][generic args][-l]\n"
"        camcontrol smppc      [dev_id][generic args] <-p phy> [-l]\n"
"                              [-o operation][-d name][-m rate][-M rate]\n"
"                              [-T pp_timeout][-a enable|disable]\n"
"                              [-A enable|disable][-s enable|disable]\n"
"                              [-S enable|disable]\n"
"        camcontrol smpphylist [dev_id][generic args][-l][-q]\n"
"        camcontrol smpmaninfo [dev_id][generic args][-l]\n"
"        camcontrol debug      [-I][-P][-T][-S][-X][-c]\n"
"                              <all|bus[:target[:lun]]|off>\n"
"        camcontrol tags       [dev_id][generic args] [-N tags] [-q] [-v]\n"
"        camcontrol negotiate  [dev_id][generic args] [-a][-c]\n"
"                              [-D <enable|disable>][-M mode][-O offset]\n"
"                              [-q][-R syncrate][-v][-T <enable|disable>]\n"
"                              [-U][-W bus_width]\n"
"        camcontrol format     [dev_id][generic args][-q][-r][-w][-y]\n"
"        camcontrol sanitize   [dev_id][generic args]\n"
"                              [-a overwrite|block|crypto|exitfailure]\n"
"                              [-c passes][-I][-P pattern][-q][-U][-r][-w]\n"
"                              [-y]\n"
"        camcontrol idle       [dev_id][generic args][-t time]\n"
"        camcontrol standby    [dev_id][generic args][-t time]\n"
"        camcontrol sleep      [dev_id][generic args]\n"
"        camcontrol apm        [dev_id][generic args][-l level]\n"
"        camcontrol aam        [dev_id][generic args][-l level]\n"
"        camcontrol fwdownload [dev_id][generic args] <-f fw_image> [-q]\n"
"                              [-s][-y]\n"
"        camcontrol security   [dev_id][generic args]\n"
"                              <-d pwd | -e pwd | -f | -h pwd | -k pwd>\n"
"                              [-l <high|maximum>] [-q] [-s pwd] [-T timeout]\n"
"                              [-U <user|master>] [-y]\n"
"        camcontrol hpa        [dev_id][generic args] [-f] [-l] [-P] [-p pwd]\n"
"                              [-q] [-s max_sectors] [-U pwd] [-y]\n"
"        camcontrol persist    [dev_id][generic args] <-i action|-o action>\n"
"                              [-a][-I tid][-k key][-K sa_key][-p][-R rtp]\n"
"                              [-s scope][-S][-T type][-U]\n"
"        camcontrol attrib     [dev_id][generic args] <-r action|-w attr>\n"
"                              [-a attr_num][-c][-e elem][-F form1,form1]\n"
"                              [-p part][-s start][-T type][-V vol]\n"
"        camcontrol opcodes    [dev_id][generic args][-o opcode][-s SA]\n"
"                              [-N][-T]\n"
"        camcontrol zone       [dev_id][generic args]<-c cmd> [-a] [-l LBA]\n"
"                              [-o rep_opts] [-P print_opts]\n"
"        camcontrol epc        [dev_id][generic_args]<-c cmd> [-d] [-D] [-e]\n"
"                              [-H] [-p power_cond] [-P] [-r rst_src] [-s]\n"
"                              [-S power_src] [-T timer]\n"
"        camcontrol timestamp  [dev_id][generic_args] <-r [-f format|-m|-U]>|\n"
"                              <-s <-f format -T time | -U >>\n"
"                              \n"
#endif /* MINIMALISTIC */
"        camcontrol help\n");
	if (!printlong)
		return;
#ifndef MINIMALISTIC
	fprintf(stdout,
"Specify one of the following options:\n"
"devlist     list all CAM devices\n"
"periphlist  list all CAM peripheral drivers attached to a device\n"
"tur         send a test unit ready to the named device\n"
"inquiry     send a SCSI inquiry command to the named device\n"
"identify    send a ATA identify command to the named device\n"
"reportluns  send a SCSI report luns command to the device\n"
"readcap     send a SCSI read capacity command to the device\n"
"start       send a Start Unit command to the device\n"
"stop        send a Stop Unit command to the device\n"
"load        send a Start Unit command to the device with the load bit set\n"
"eject       send a Stop Unit command to the device with the eject bit set\n"
"reprobe     update capacity information of the given device\n"
"rescan      rescan all buses, the given bus, bus:target:lun or device\n"
"reset       reset all buses, the given bus, bus:target:lun or device\n"
"defects     read the defect list of the specified device\n"
"modepage    display or edit (-e) the given mode page\n"
"cmd         send the given SCSI command, may need -i or -o as well\n"
"smpcmd      send the given SMP command, requires -o and -i\n"
"smprg       send the SMP Report General command\n"
"smppc       send the SMP PHY Control command, requires -p\n"
"smpphylist  display phys attached to a SAS expander\n"
"smpmaninfo  send the SMP Report Manufacturer Info command\n"
"debug       turn debugging on/off for a bus, target, or lun, or all devices\n"
"tags        report or set the number of transaction slots for a device\n"
"negotiate   report or set device negotiation parameters\n"
"format      send the SCSI FORMAT UNIT command to the named device\n"
"sanitize    send the SCSI SANITIZE command to the named device\n"
"idle        send the ATA IDLE command to the named device\n"
"standby     send the ATA STANDBY command to the named device\n"
"sleep       send the ATA SLEEP command to the named device\n"
"fwdownload  program firmware of the named device with the given image\n"
"security    report or send ATA security commands to the named device\n"
"persist     send the SCSI PERSISTENT RESERVE IN or OUT commands\n"
"attrib      send the SCSI READ or WRITE ATTRIBUTE commands\n"
"opcodes     send the SCSI REPORT SUPPORTED OPCODES command\n"
"zone        manage Zoned Block (Shingled) devices\n"
"epc         send ATA Extended Power Conditions commands\n"
"timestamp   report or set the device's timestamp\n"
"help        this message\n"
"Device Identifiers:\n"
"bus:target        specify the bus and target, lun defaults to 0\n"
"bus:target:lun    specify the bus, target and lun\n"
"deviceUNIT        specify the device name, like \"da4\" or \"cd2\"\n"
"Generic arguments:\n"
"-v                be verbose, print out sense information\n"
"-t timeout        command timeout in seconds, overrides default timeout\n"
"-n dev_name       specify device name, e.g. \"da\", \"cd\"\n"
"-u unit           specify unit number, e.g. \"0\", \"5\"\n"
"-E                have the kernel attempt to perform SCSI error recovery\n"
"-C count          specify the SCSI command retry count (needs -E to work)\n"
"-Q task_attr      specify ordered, simple or head tag type for SCSI cmds\n"
"modepage arguments:\n"
"-l                list all available mode pages\n"
"-m page           specify the mode page to view or edit\n"
"-e                edit the specified mode page\n"
"-b                force view to binary mode\n"
"-d                disable block descriptors for mode sense\n"
"-P pgctl          page control field 0-3\n"
"defects arguments:\n"
"-f format         specify defect list format (block, bfi or phys)\n"
"-G                get the grown defect list\n"
"-P                get the permanent defect list\n"
"inquiry arguments:\n"
"-D                get the standard inquiry data\n"
"-S                get the serial number\n"
"-R                get the transfer rate, etc.\n"
"reportluns arguments:\n"
"-c                only report a count of available LUNs\n"
"-l                only print out luns, and not a count\n"
"-r <reporttype>   specify \"default\", \"wellknown\" or \"all\"\n"
"readcap arguments\n"
"-b                only report the blocksize\n"
"-h                human readable device size, base 2\n"
"-H                human readable device size, base 10\n"
"-N                print the number of blocks instead of last block\n"
"-q                quiet, print numbers only\n"
"-s                only report the last block/device size\n"
"cmd arguments:\n"
"-c cdb [args]     specify the SCSI CDB\n"
"-i len fmt        specify input data and input data format\n"
"-o len fmt [args] specify output data and output data fmt\n"
"smpcmd arguments:\n"
"-r len fmt [args] specify the SMP command to be sent\n"
"-R len fmt [args] specify SMP response format\n"
"smprg arguments:\n"
"-l                specify the long response format\n"
"smppc arguments:\n"
"-p phy            specify the PHY to operate on\n"
"-l                specify the long request/response format\n"
"-o operation      specify the phy control operation\n"
"-d name           set the attached device name\n"
"-m rate           set the minimum physical link rate\n"
"-M rate           set the maximum physical link rate\n"
"-T pp_timeout     set the partial pathway timeout value\n"
"-a enable|disable enable or disable SATA slumber\n"
"-A enable|disable enable or disable SATA partial phy power\n"
"-s enable|disable enable or disable SAS slumber\n"
"-S enable|disable enable or disable SAS partial phy power\n"
"smpphylist arguments:\n"
"-l                specify the long response format\n"
"-q                only print phys with attached devices\n"
"smpmaninfo arguments:\n"
"-l                specify the long response format\n"
"debug arguments:\n"
"-I                CAM_DEBUG_INFO -- scsi commands, errors, data\n"
"-T                CAM_DEBUG_TRACE -- routine flow tracking\n"
"-S                CAM_DEBUG_SUBTRACE -- internal routine command flow\n"
"-c                CAM_DEBUG_CDB -- print out SCSI CDBs only\n"
"tags arguments:\n"
"-N tags           specify the number of tags to use for this device\n"
"-q                be quiet, don't report the number of tags\n"
"-v                report a number of tag-related parameters\n"
"negotiate arguments:\n"
"-a                send a test unit ready after negotiation\n"
"-c                report/set current negotiation settings\n"
"-D <arg>          \"enable\" or \"disable\" disconnection\n"
"-M mode           set ATA mode\n"
"-O offset         set command delay offset\n"
"-q                be quiet, don't report anything\n"
"-R syncrate       synchronization rate in MHz\n"
"-T <arg>          \"enable\" or \"disable\" tagged queueing\n"
"-U                report/set user negotiation settings\n"
"-W bus_width      set the bus width in bits (8, 16 or 32)\n"
"-v                also print a Path Inquiry CCB for the controller\n"
"format arguments:\n"
"-q                be quiet, don't print status messages\n"
"-r                run in report only mode\n"
"-w                don't send immediate format command\n"
"-y                don't ask any questions\n"
"sanitize arguments:\n"
"-a operation      operation mode: overwrite, block, crypto or exitfailure\n"
"-c passes         overwrite passes to perform (1 to 31)\n"
"-I                invert overwrite pattern after each pass\n"
"-P pattern        path to overwrite pattern file\n"
"-q                be quiet, don't print status messages\n"
"-r                run in report only mode\n"
"-U                run operation in unrestricted completion exit mode\n"
"-w                don't send immediate sanitize command\n"
"-y                don't ask any questions\n"
"idle/standby arguments:\n"
"-t <arg>          number of seconds before respective state.\n"
"fwdownload arguments:\n"
"-f fw_image       path to firmware image file\n"
"-q                don't print informational messages, only errors\n"
"-s                run in simulation mode\n"
"-v                print info for every firmware segment sent to device\n"
"-y                don't ask any questions\n"
"security arguments:\n"
"-d pwd            disable security using the given password for the selected\n"
"                  user\n"
"-e pwd            erase the device using the given pwd for the selected user\n"
"-f                freeze the security configuration of the specified device\n"
"-h pwd            enhanced erase the device using the given pwd for the\n"
"                  selected user\n"
"-k pwd            unlock the device using the given pwd for the selected\n"
"                  user\n"
"-l <high|maximum> specifies which security level to set: high or maximum\n"
"-q                be quiet, do not print any status messages\n"
"-s pwd            password the device (enable security) using the given\n"
"                  pwd for the selected user\n"
"-T timeout        overrides the timeout (seconds) used for erase operation\n"
"-U <user|master>  specifies which user to set: user or master\n"
"-y                don't ask any questions\n"
"hpa arguments:\n"
"-f                freeze the HPA configuration of the device\n"
"-l                lock the HPA configuration of the device\n"
"-P                make the HPA max sectors persist\n"
"-p pwd            Set the HPA configuration password required for unlock\n"
"                  calls\n"
"-q                be quiet, do not print any status messages\n"
"-s sectors        configures the maximum user accessible sectors of the\n"
"                  device\n"
"-U pwd            unlock the HPA configuration of the device\n"
"-y                don't ask any questions\n"
"persist arguments:\n"
"-i action         specify read_keys, read_reservation, report_cap, or\n"
"                  read_full_status\n"
"-o action         specify register, register_ignore, reserve, release,\n"
"                  clear, preempt, preempt_abort, register_move, replace_lost\n"
"-a                set the All Target Ports (ALL_TG_PT) bit\n"
"-I tid            specify a Transport ID, e.g.: sas,0x1234567812345678\n"
"-k key            specify the Reservation Key\n"
"-K sa_key         specify the Service Action Reservation Key\n"
"-p                set the Activate Persist Through Power Loss bit\n"
"-R rtp            specify the Relative Target Port\n"
"-s scope          specify the scope: lun, extent, element or a number\n"
"-S                specify Transport ID for register, requires -I\n"
"-T res_type       specify the reservation type: read_shared, wr_ex, rd_ex,\n"
"                  ex_ac, wr_ex_ro, ex_ac_ro, wr_ex_ar, ex_ac_ar\n"
"-U                unregister the current initiator for register_move\n"
"attrib arguments:\n"
"-r action         specify attr_values, attr_list, lv_list, part_list, or\n"
"                  supp_attr\n"
"-w attr           specify an attribute to write, one -w argument per attr\n"
"-a attr_num       only display this attribute number\n"
"-c                get cached attributes\n"
"-e elem_addr      request attributes for the given element in a changer\n"
"-F form1,form2    output format, comma separated list: text_esc, text_raw,\n"
"                  nonascii_esc, nonascii_trim, nonascii_raw, field_all,\n"
"                  field_none, field_desc, field_num, field_size, field_rw\n"
"-p partition      request attributes for the given partition\n"
"-s start_attr     request attributes starting at the given number\n"
"-T elem_type      specify the element type (used with -e)\n"
"-V logical_vol    specify the logical volume ID\n"
"opcodes arguments:\n"
"-o opcode         specify the individual opcode to list\n"
"-s service_action specify the service action for the opcode\n"
"-N                do not return SCSI error for unsupported SA\n"
"-T                request nominal and recommended timeout values\n"
"zone arguments:\n"
"-c cmd            required: rz, open, close, finish, or rwp\n"
"-a                apply the action to all zones\n"
"-l LBA            specify the zone starting LBA\n"
"-o rep_opts       report zones options: all, empty, imp_open, exp_open,\n"
"                  closed, full, ro, offline, reset, nonseq, nonwp\n"
"-P print_opt      report zones printing:  normal, summary, script\n"
"epc arguments:\n"
"-c cmd            required: restore, goto, timer, state, enable, disable,\n"
"                  source, status, list\n"
"-d                disable power mode (timer, state)\n"
"-D                delayed entry (goto)\n"
"-e                enable power mode (timer, state)\n"
"-H                hold power mode (goto)\n"
"-p power_cond     Idle_a, Idle_b, Idle_c, Standby_y, Standby_z (timer,\n"
"                  state, goto)\n"
"-P                only display power mode (status)\n"
"-r rst_src        restore settings from: default, saved (restore)\n"
"-s                save mode (timer, state, restore)\n"
"-S power_src      set power source: battery, nonbattery (source)\n"
"-T timer          set timer, seconds, .1 sec resolution (timer)\n"
"timestamp arguments:\n"
"-r                report the timestamp of the device\n"
"-f format         report the timestamp of the device with the given\n"
"                  strftime(3) format string\n"
"-m                report the timestamp of the device as milliseconds since\n"
"                  January 1st, 1970\n"
"-U                report the time with UTC instead of the local time zone\n"
"-s                set the timestamp of the device\n"
"-f format         the format of the time string passed into strptime(3)\n"
"-T time           the time value passed into strptime(3)\n"
"-U                set the timestamp of the device to UTC time\n"
);
#endif /* MINIMALISTIC */
}

int
main(int argc, char **argv)
{
	int c;
	char *device = NULL;
	int unit = 0;
	struct cam_device *cam_dev = NULL;
	int timeout = 0, retry_count = 1;
	camcontrol_optret optreturn;
	char *tstr;
	const char *mainopt = "C:En:Q:t:u:v";
	const char *subopt = NULL;
	char combinedopt[256];
	int error = 0, optstart = 2;
	int task_attr = MSG_SIMPLE_Q_TAG;
	int devopen = 1;
#ifndef MINIMALISTIC
	path_id_t bus;
	target_id_t target;
	lun_id_t lun;
#endif /* MINIMALISTIC */

	cmdlist = CAM_CMD_NONE;
	arglist = CAM_ARG_NONE;

	if (argc < 2) {
		usage(0);
		exit(1);
	}

	/*
	 * Get the base option.
	 */
	optreturn = getoption(option_table,argv[1], &cmdlist, &arglist,&subopt);

	if (optreturn == CC_OR_AMBIGUOUS) {
		warnx("ambiguous option %s", argv[1]);
		usage(0);
		exit(1);
	} else if (optreturn == CC_OR_NOT_FOUND) {
		warnx("option %s not found", argv[1]);
		usage(0);
		exit(1);
	}

	/*
	 * Ahh, getopt(3) is a pain.
	 *
	 * This is a gross hack.  There really aren't many other good
	 * options (excuse the pun) for parsing options in a situation like
	 * this.  getopt is kinda braindead, so you end up having to run
	 * through the options twice, and give each invocation of getopt
	 * the option string for the other invocation.
	 *
	 * You would think that you could just have two groups of options.
	 * The first group would get parsed by the first invocation of
	 * getopt, and the second group would get parsed by the second
	 * invocation of getopt.  It doesn't quite work out that way.  When
	 * the first invocation of getopt finishes, it leaves optind pointing
	 * to the argument _after_ the first argument in the second group.
	 * So when the second invocation of getopt comes around, it doesn't
	 * recognize the first argument it gets and then bails out.
	 *
	 * A nice alternative would be to have a flag for getopt that says
	 * "just keep parsing arguments even when you encounter an unknown
	 * argument", but there isn't one.  So there's no real clean way to
	 * easily parse two sets of arguments without having one invocation
	 * of getopt know about the other.
	 *
	 * Without this hack, the first invocation of getopt would work as
	 * long as the generic arguments are first, but the second invocation
	 * (in the subfunction) would fail in one of two ways.  In the case
	 * where you don't set optreset, it would fail because optind may be
	 * pointing to the argument after the one it should be pointing at.
	 * In the case where you do set optreset, and reset optind, it would
	 * fail because getopt would run into the first set of options, which
	 * it doesn't understand.
	 *
	 * All of this would "sort of" work if you could somehow figure out
	 * whether optind had been incremented one option too far.  The
	 * mechanics of that, however, are more daunting than just giving
	 * both invocations all of the expect options for either invocation.
	 *
	 * Needless to say, I wouldn't mind if someone invented a better
	 * (non-GPL!) command line parsing interface than getopt.  I
	 * wouldn't mind if someone added more knobs to getopt to make it
	 * work better.  Who knows, I may talk myself into doing it someday,
	 * if the standards weenies let me.  As it is, it just leads to
	 * hackery like this and causes people to avoid it in some cases.
	 *
	 * KDM, September 8th, 1998
	 */
	if (subopt != NULL)
		sprintf(combinedopt, "%s%s", mainopt, subopt);
	else
		sprintf(combinedopt, "%s", mainopt);

	/*
	 * For these options we do not parse optional device arguments and
	 * we do not open a passthrough device.
	 */
	if ((cmdlist == CAM_CMD_RESCAN)
	 || (cmdlist == CAM_CMD_RESET)
	 || (cmdlist == CAM_CMD_DEVTREE)
	 || (cmdlist == CAM_CMD_USAGE)
	 || (cmdlist == CAM_CMD_DEBUG))
		devopen = 0;

#ifndef MINIMALISTIC
	if ((devopen == 1)
	 && (argc > 2 && argv[2][0] != '-')) {
		char name[30];
		int rv;

		if (isdigit(argv[2][0])) {
			/* device specified as bus:target[:lun] */
			rv = parse_btl(argv[2], &bus, &target, &lun, &arglist);
			if (rv < 2)
				errx(1, "numeric device specification must "
				     "be either bus:target, or "
				     "bus:target:lun");
			/* default to 0 if lun was not specified */
			if ((arglist & CAM_ARG_LUN) == 0) {
				lun = 0;
				arglist |= CAM_ARG_LUN;
			}
			optstart++;
		} else {
			if (cam_get_device(argv[2], name, sizeof name, &unit)
			    == -1)
				errx(1, "%s", cam_errbuf);
			device = strdup(name);
			arglist |= CAM_ARG_DEVICE | CAM_ARG_UNIT;
			optstart++;
		}
	}
#endif /* MINIMALISTIC */
	/*
	 * Start getopt processing at argv[2/3], since we've already
	 * accepted argv[1..2] as the command name, and as a possible
	 * device name.
	 */
	optind = optstart;

	/*
	 * Now we run through the argument list looking for generic
	 * options, and ignoring options that possibly belong to
	 * subfunctions.
	 */
	while ((c = getopt(argc, argv, combinedopt))!= -1){
		switch(c) {
			case 'C':
				retry_count = strtol(optarg, NULL, 0);
				if (retry_count < 0)
					errx(1, "retry count %d is < 0",
					     retry_count);
				arglist |= CAM_ARG_RETRIES;
				break;
			case 'E':
				arglist |= CAM_ARG_ERR_RECOVER;
				break;
			case 'n':
				arglist |= CAM_ARG_DEVICE;
				tstr = optarg;
				while (isspace(*tstr) && (*tstr != '\0'))
					tstr++;
				device = (char *)strdup(tstr);
				break;
			case 'Q': {
				char *endptr;
				int table_entry = 0;

				tstr = optarg;
				while (isspace(*tstr) && (*tstr != '\0'))
					tstr++;
				if (isdigit(*tstr)) {
					task_attr = strtol(tstr, &endptr, 0);
					if (*endptr != '\0') {
						errx(1, "Invalid queue option "
						    "%s", tstr);
					}
				} else {
					size_t table_size;
					scsi_nv_status status;

					table_size = sizeof(task_attrs) /
						     sizeof(task_attrs[0]);
					status = scsi_get_nv(task_attrs,
					    table_size, tstr, &table_entry,
					    SCSI_NV_FLAG_IG_CASE);
					if (status == SCSI_NV_FOUND)
						task_attr = task_attrs[
						    table_entry].value;
					else {
						errx(1, "%s option %s",
						  (status == SCSI_NV_AMBIGUOUS)?
						    "ambiguous" : "invalid",
						    tstr);
					}
				}
				break;
			}
			case 't':
				timeout = strtol(optarg, NULL, 0);
				if (timeout < 0)
					errx(1, "invalid timeout %d", timeout);
				/* Convert the timeout from seconds to ms */
				timeout *= 1000;
				arglist |= CAM_ARG_TIMEOUT;
				break;
			case 'u':
				arglist |= CAM_ARG_UNIT;
				unit = strtol(optarg, NULL, 0);
				break;
			case 'v':
				arglist |= CAM_ARG_VERBOSE;
				break;
			default:
				break;
		}
	}

#ifndef MINIMALISTIC
	/*
	 * For most commands we'll want to open the passthrough device
	 * associated with the specified device.  In the case of the rescan
	 * commands, we don't use a passthrough device at all, just the
	 * transport layer device.
	 */
	if (devopen == 1) {
		if (((arglist & (CAM_ARG_BUS|CAM_ARG_TARGET)) == 0)
		 && (((arglist & CAM_ARG_DEVICE) == 0)
		  || ((arglist & CAM_ARG_UNIT) == 0))) {
			errx(1, "subcommand \"%s\" requires a valid device "
			     "identifier", argv[1]);
		}

		if ((cam_dev = ((arglist & (CAM_ARG_BUS | CAM_ARG_TARGET))?
				cam_open_btl(bus, target, lun, O_RDWR, NULL) :
				cam_open_spec_device(device,unit,O_RDWR,NULL)))
		     == NULL)
			errx(1,"%s", cam_errbuf);
	}
#endif /* MINIMALISTIC */

	/*
	 * Reset optind to 2, and reset getopt, so these routines can parse
	 * the arguments again.
	 */
	optind = optstart;
	optreset = 1;

	switch(cmdlist) {
#ifndef MINIMALISTIC
	case CAM_CMD_DEVLIST:
		error = getdevlist(cam_dev);
		break;
	case CAM_CMD_HPA:
		error = atahpa(cam_dev, retry_count, timeout,
			       argc, argv, combinedopt);
		break;
#endif /* MINIMALISTIC */
	case CAM_CMD_DEVTREE:
		error = getdevtree(argc, argv, combinedopt);
		break;
#ifndef MINIMALISTIC
	case CAM_CMD_TUR:
		error = testunitready(cam_dev, task_attr, retry_count,
		    timeout, 0);
		break;
	case CAM_CMD_INQUIRY:
		error = scsidoinquiry(cam_dev, argc, argv, combinedopt,
				      task_attr, retry_count, timeout);
		break;
	case CAM_CMD_IDENTIFY:
		error = identify(cam_dev, retry_count, timeout);
		break;
	case CAM_CMD_STARTSTOP:
		error = scsistart(cam_dev, arglist & CAM_ARG_START_UNIT,
				  arglist & CAM_ARG_EJECT, task_attr,
				  retry_count, timeout);
		break;
#endif /* MINIMALISTIC */
	case CAM_CMD_RESCAN:
		error = dorescan_or_reset(argc, argv, 1);
		break;
	case CAM_CMD_RESET:
		error = dorescan_or_reset(argc, argv, 0);
		break;
#ifndef MINIMALISTIC
	case CAM_CMD_READ_DEFECTS:
		error = readdefects(cam_dev, argc, argv, combinedopt,
				    task_attr, retry_count, timeout);
		break;
	case CAM_CMD_MODE_PAGE:
		modepage(cam_dev, argc, argv, combinedopt,
			 task_attr, retry_count, timeout);
		break;
	case CAM_CMD_SCSI_CMD:
		error = scsicmd(cam_dev, argc, argv, combinedopt,
				task_attr, retry_count, timeout);
		break;
	case CAM_CMD_MMCSD_CMD:
		error = mmcsdcmd(cam_dev, argc, argv, combinedopt,
					retry_count, timeout);
		break;
	case CAM_CMD_SMP_CMD:
		error = smpcmd(cam_dev, argc, argv, combinedopt,
			       retry_count, timeout);
		break;
	case CAM_CMD_SMP_RG:
		error = smpreportgeneral(cam_dev, argc, argv,
					 combinedopt, retry_count,
					 timeout);
		break;
	case CAM_CMD_SMP_PC:
		error = smpphycontrol(cam_dev, argc, argv, combinedopt,
				      retry_count, timeout);
		break;
	case CAM_CMD_SMP_PHYLIST:
		error = smpphylist(cam_dev, argc, argv, combinedopt,
				   retry_count, timeout);
		break;
	case CAM_CMD_SMP_MANINFO:
		error = smpmaninfo(cam_dev, argc, argv, combinedopt,
				   retry_count, timeout);
		break;
	case CAM_CMD_DEBUG:
		error = camdebug(argc, argv, combinedopt);
		break;
	case CAM_CMD_TAG:
		error = tagcontrol(cam_dev, argc, argv, combinedopt);
		break;
	case CAM_CMD_RATE:
		error = ratecontrol(cam_dev, task_attr, retry_count,
				    timeout, argc, argv, combinedopt);
		break;
	case CAM_CMD_FORMAT:
		error = scsiformat(cam_dev, argc, argv,
				   combinedopt, task_attr, retry_count,
				   timeout);
		break;
	case CAM_CMD_REPORTLUNS:
		error = scsireportluns(cam_dev, argc, argv,
				       combinedopt, task_attr,
				       retry_count, timeout);
		break;
	case CAM_CMD_READCAP:
		error = scsireadcapacity(cam_dev, argc, argv,
					 combinedopt, task_attr,
					 retry_count, timeout);
		break;
	case CAM_CMD_IDLE:
	case CAM_CMD_STANDBY:
	case CAM_CMD_SLEEP:
		error = atapm(cam_dev, argc, argv,
			      combinedopt, retry_count, timeout);
		break;
	case CAM_CMD_APM:
	case CAM_CMD_AAM:
		error = ataaxm(cam_dev, argc, argv,
			      combinedopt, retry_count, timeout);
		break;
	case CAM_CMD_SECURITY:
		error = atasecurity(cam_dev, retry_count, timeout,
				    argc, argv, combinedopt);
		break;
	case CAM_CMD_DOWNLOAD_FW:
		error = fwdownload(cam_dev, argc, argv, combinedopt,
		    arglist & CAM_ARG_VERBOSE, task_attr, retry_count,
		    timeout);
		break;
	case CAM_CMD_SANITIZE:
		error = scsisanitize(cam_dev, argc, argv,
				     combinedopt, task_attr,
				     retry_count, timeout);
		break;
	case CAM_CMD_PERSIST:
		error = scsipersist(cam_dev, argc, argv, combinedopt,
		    task_attr, retry_count, timeout,
		    arglist & CAM_ARG_VERBOSE,
		    arglist & CAM_ARG_ERR_RECOVER);
		break;
	case CAM_CMD_ATTRIB:
		error = scsiattrib(cam_dev, argc, argv, combinedopt,
		    task_attr, retry_count, timeout,
		    arglist & CAM_ARG_VERBOSE,
		    arglist & CAM_ARG_ERR_RECOVER);
		break;
	case CAM_CMD_OPCODES:
		error = scsiopcodes(cam_dev, argc, argv, combinedopt,
		    task_attr, retry_count, timeout,
		    arglist & CAM_ARG_VERBOSE);
		break;
	case CAM_CMD_REPROBE:
		error = scsireprobe(cam_dev);
		break;
	case CAM_CMD_ZONE:
		error = zone(cam_dev, argc, argv, combinedopt,
		    task_attr, retry_count, timeout,
		    arglist & CAM_ARG_VERBOSE);
		break;
	case CAM_CMD_EPC:
		error = epc(cam_dev, argc, argv, combinedopt,
		    retry_count, timeout, arglist & CAM_ARG_VERBOSE);
		break;
	case CAM_CMD_TIMESTAMP:
		error = timestamp(cam_dev, argc, argv, combinedopt,
		    task_attr, retry_count, timeout,
		    arglist & CAM_ARG_VERBOSE);
		break;
#endif /* MINIMALISTIC */
	case CAM_CMD_USAGE:
		usage(1);
		break;
	default:
		usage(0);
		error = 1;
		break;
	}

	if (cam_dev != NULL)
		cam_close_device(cam_dev);

	exit(error);
}
