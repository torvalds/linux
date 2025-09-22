/*	$OpenBSD: atactl.c,v 1.49 2023/04/30 00:58:38 yasuoka Exp $	*/
/*	$NetBSD: atactl.c,v 1.4 1999/02/24 18:49:14 jwise Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ken Hornstein.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * atactl(8) - a program to control ATA devices.
 */

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/ioctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include <dev/ata/atareg.h>
#include <dev/ic/wdcreg.h>
#include <dev/ic/wdcevent.h>
#include <sys/ataio.h>

#include "atasec.h"
#include "atasmart.h"

struct command {
	const char *cmd_name;
	void (*cmd_func)(int, char *[]);
};

struct bitinfo {
	u_int bitmask;
	const char *string;
};

struct valinfo {
	int value;
	const char *string;
};

int  main(int, char *[]);
__dead void usage(void);
void ata_command(struct atareq *);
void print_bitinfo(const char *, u_int, struct bitinfo *);
int  strtoval(const char *, struct valinfo *);
const char *valtostr(int, struct valinfo *, const char *);

int	fd;				/* file descriptor for device */

extern char *__progname;		/* from crt0.o */

void    device_dump(int, char*[]);
void	device_identify(int, char *[]);
void	device_setidle(int, char *[]);
void	device_idle(int, char *[]);
void	device_checkpower(int, char *[]);
void	device_acoustic(int, char *[]);
void	device_apm(int, char *[]);
void	device_feature(int, char *[]);
void	device_sec_setpass(int, char *[]);
void	device_sec_unlock(int, char *[]);
void	device_sec_erase(int, char *[]);
void	device_sec_freeze(int, char *[]);
void	device_sec_disablepass(int, char *[]);
void	device_smart_enable(int, char *[]);
void	device_smart_disable(int, char *[]);
void	device_smart_status(int, char *[]);
void	device_smart_autosave(int, char *[]);
void	device_smart_offline(int, char *[]);
void	device_smart_read(int, char *[]);
void	device_smart_readlog(int, char *[]);
void	device_attr(int, char *[]);

void	smart_print_errdata(struct smart_log_errdata *);
int	smart_cksum(u_int8_t *, size_t);

char 	*sec_getpass(int, int);

struct command commands[] = {
	{ "dump",		device_dump },
	{ "identify",		device_identify },
	{ "setidle",		device_setidle },
	{ "setstandby",		device_setidle },
	{ "idle",		device_idle },
	{ "standby",		device_idle },
	{ "sleep",		device_idle },
	{ "checkpower",		device_checkpower },
	{ "acousticdisable",	device_feature },
	{ "acousticset",	device_acoustic },
	{ "apmdisable",		device_feature },
	{ "apmset",		device_apm },
	{ "poddisable",		device_feature },
	{ "podenable",		device_feature },
	{ "puisdisable",	device_feature },
	{ "puisenable",		device_feature },
	{ "puisspinup",		device_feature },
	{ "readaheaddisable",	device_feature },
	{ "readaheadenable",	device_feature },
	{ "secsetpass",		device_sec_setpass },
	{ "secunlock",		device_sec_unlock },
	{ "secerase",		device_sec_erase },
	{ "secfreeze",		device_sec_freeze },
	{ "secdisablepass",	device_sec_disablepass },
	{ "smartenable", 	device_smart_enable },
	{ "smartdisable", 	device_smart_disable },
	{ "smartstatus", 	device_smart_status },
	{ "smartautosave",	device_smart_autosave },
	{ "smartoffline",	device_smart_offline },
	{ "smartread",		device_smart_read },
	{ "smartreadlog",	device_smart_readlog },
	{ "readattr",		device_attr },
	{ "writecachedisable",	device_feature },
	{ "writecacheenable",	device_feature },
	{ NULL,		NULL },
};

/*
 * Tables containing bitmasks used for error reporting and
 * device identification.
 */

struct bitinfo ata_caps[] = {
	{ ATA_CAP_STBY, "ATA standby timer values" },
	{ WDC_CAP_IORDY, "IORDY operation" },
	{ WDC_CAP_IORDY_DSBL, "IORDY disabling" },
	{ 0, NULL },
};

struct bitinfo ata_vers[] = {
	{ WDC_VER_ATA1,	 "ATA-1" },
	{ WDC_VER_ATA2,	 "ATA-2" },
	{ WDC_VER_ATA3,	 "ATA-3" },
	{ WDC_VER_ATA4,	 "ATA-4" },
	{ WDC_VER_ATA5,	 "ATA-5" },
	{ WDC_VER_ATA6,	 "ATA-6" },
	{ WDC_VER_ATA7,	 "ATA-7" },
	{ WDC_VER_ATA8,	 "ATA-8" },
	{ WDC_VER_ATA9,	 "ATA-9" },
	{ WDC_VER_ATA10, "ATA-10" },
	{ WDC_VER_ATA11, "ATA-11" },
	{ WDC_VER_ATA12, "ATA-12" },
	{ WDC_VER_ATA13, "ATA-13" },
	{ WDC_VER_ATA14, "ATA-14" },
	{ 0, NULL },
};

struct bitinfo ata_cmd_set1[] = {
	{ WDC_CMD1_NOP, "NOP command" },
	{ WDC_CMD1_RB, "READ BUFFER command" },
	{ WDC_CMD1_WB, "WRITE BUFFER command" },
	{ WDC_CMD1_HPA, "Host Protected Area feature set" },
	{ WDC_CMD1_DVRST, "DEVICE RESET command" },
	{ WDC_CMD1_SRV, "SERVICE interrupt" },
	{ WDC_CMD1_RLSE, "Release interrupt" },
	{ WDC_CMD1_AHEAD, "Read look-ahead" },
	{ WDC_CMD1_CACHE, "Write cache" },
	{ WDC_CMD1_PKT, "PACKET command feature set" },
	{ WDC_CMD1_PM, "Power Management feature set" },
	{ WDC_CMD1_REMOV, "Removable Media feature set" },
	{ WDC_CMD1_SEC, "Security Mode feature set" },
	{ WDC_CMD1_SMART, "SMART feature set" },
	{ 0, NULL },
};

struct bitinfo ata_cmd_set2[] = {
	{ ATAPI_CMD2_FCE, "Flush Cache Ext command" },
	{ ATAPI_CMD2_FC, "Flush Cache command" },
	{ ATAPI_CMD2_DCO, "Device Configuration Overlay feature set" },
	{ ATAPI_CMD2_48AD, "48bit address feature set" },
	{ ATAPI_CMD2_AAM, "Automatic Acoustic Management feature set" },
	{ ATAPI_CMD2_SM, "Set Max security extension commands" },
	{ ATAPI_CMD2_SF, "Set Features subcommand required" },
	{ ATAPI_CMD2_PUIS, "Power-up in standby feature set" },
	{ WDC_CMD2_RMSN, "Removable Media Status Notification feature set" },
	{ ATA_CMD2_APM, "Advanced Power Management feature set" },
	{ ATA_CMD2_CFA, "CFA feature set" },
	{ ATA_CMD2_RWQ, "READ/WRITE DMA QUEUED commands" },
	{ WDC_CMD2_DM, "DOWNLOAD MICROCODE command" },
	{ 0, NULL },
};

struct bitinfo ata_cmd_ext[] = {
	{ ATAPI_CMDE_IIUF, "IDLE IMMEDIATE with UNLOAD FEATURE" },
	{ ATAPI_CMDE_MSER, "Media serial number" },
	{ ATAPI_CMDE_TEST, "SMART self-test" },
	{ ATAPI_CMDE_SLOG, "SMART error logging" },
	{ 0, NULL },
};

/*
 * Tables containing bitmasks and values used for
 * SMART commands.
 */

struct bitinfo smart_offcap[] = {
	{ SMART_OFFCAP_EXEC, "execute immediate" },
	{ SMART_OFFCAP_ABORT, "abort/restart" },
	{ SMART_OFFCAP_READSCAN, "read scanning" },
	{ SMART_OFFCAP_SELFTEST, "self-test routines" },
	{ 0, NULL}
};

struct bitinfo smart_smartcap[] = {
	{ SMART_SMARTCAP_SAVE, "saving SMART data" },
	{ SMART_SMARTCAP_AUTOSAVE, "enable/disable attribute autosave" },
	{ 0, NULL }
};

struct valinfo smart_autosave[] = {
	{ SMART_AUTOSAVE_EN, "enable" },
	{ SMART_AUTOSAVE_DS, "disable" },
	{ 0, NULL }
};

struct valinfo smart_offline[] = {
	{ SMART_OFFLINE_COLLECT, "collect" },
	{ SMART_OFFLINE_SHORTOFF, "shortoffline" },
	{ SMART_OFFLINE_EXTENOFF, "extenoffline" },
	{ SMART_OFFLINE_ABORT, "abort" },
	{ SMART_OFFLINE_SHORTCAP, "shortcaptive" },
	{ SMART_OFFLINE_EXTENCAP, "extencaptive" },
	{ 0, NULL }
};

struct valinfo smart_readlog[] = {
	{ SMART_READLOG_DIR, "directory" },
	{ SMART_READLOG_SUM, "summary" },
	{ SMART_READLOG_COMP, "comp" },
	{ SMART_READLOG_SELF, "selftest" },
	{ 0, NULL }
};

struct valinfo smart_offstat[] = {
	{ SMART_OFFSTAT_NOTSTART, "never started" },
	{ SMART_OFFSTAT_COMPLETE, "completed ok" },
	{ SMART_OFFSTAT_SUSPEND, "suspended by an interrupting command" },
	{ SMART_OFFSTAT_INTR, "aborted by an interrupting command" },
	{ SMART_OFFSTAT_ERROR, "aborted due to fatal error" },
	{ 0, NULL }
};

struct valinfo smart_selfstat[] = {
	{ SMART_SELFSTAT_COMPLETE, "completed ok or not started" },
	{ SMART_SELFSTAT_ABORT, "aborted" },
	{ SMART_SELFSTAT_INTR, "hardware or software reset" },
	{ SMART_SELFSTAT_ERROR, "fatal error" },
	{ SMART_SELFSTAT_UNKFAIL, "unknown test element failed" },
	{ SMART_SELFSTAT_ELFAIL, "electrical test element failed" },
	{ SMART_SELFSTAT_SRVFAIL, "servo test element failed" },
	{ SMART_SELFSTAT_RDFAIL, "read test element failed" },
	{ 0, NULL }
};

struct valinfo smart_logstat[] = {
	{ SMART_LOG_STATE_UNK, "unknown" },
	{ SMART_LOG_STATE_SLEEP, "sleep" },
	{ SMART_LOG_STATE_ACTIDL, "active/idle" },
	{ SMART_LOG_STATE_OFFSELF, "off-line or self-test" },
	{ 0, NULL }
};

/*
 * Tables containing values used for reading
 * device attributes.
 */

struct valinfo ibm_attr_names[] = {
	{ 1, "Raw Read Error Rate" },
	{ 2, "Throughput Performance" },
	{ 3, "Spin Up Time" },
	{ 4, "Start/Stop Count" },
	{ 5, "Reallocated Sector Count" },
	{ 6, "Read Channel Margin" },
	{ 7, "Seek Error Rate" },
	{ 8, "Seek Time Performance" },
	{ 9, "Power-On Hours Count" },
	{ 10, "Spin Retry Count" },
	{ 11, "Calibration Retry Count" },
	{ 12, "Device Power Cycle Count" },
	{ 13, "Soft Read Error Rate" },
	{ 100, "Erase/Program Cycles" },
	{ 103, "Translation Table Rebuild" },
	{ 160, "Uncorrectable Error Count" },
	{ 170, "Reserved Block Count" },
	{ 171, "Program Fail Count" },
	{ 172, "Erase Fail Count" },
	{ 173, "Wear Worst Case Erase Count" },
	{ 174, "Power-Off Retract Count" },
	{ 175, "Program Fail Count" },
	{ 176, "Erase Fail Count" },
	{ 177, "Wear Leveling Count" },
	{ 178, "Used Reserved Block Count" },
	{ 179, "Used Reserved Block Count Total" },
	{ 180, "Unused Reserved Block Count Total" },
	{ 181, "Program Fail Count Total" },
	{ 182, "Erase Fail Count" },
	{ 183, "Runtime Bad Block" },
	{ 184, "End-to-End error" },
	{ 185, "Head Stability" },
	{ 186, "Induced Op-Vibration Detection" },
	{ 187, "Reported Uncorrectable Errors" },
	{ 188, "Command Timeout" },
	{ 189, "High Fly Writes" },
	{ 190, "Airflow Temperature" },
	{ 191, "G-Sense Error Rate" },
	{ 192, "Power-Off Retract Count" },
	{ 193, "Load Cycle Count" },
	{ 194, "Temperature" },
	{ 195, "Hardware ECC Recovered" },
	{ 196, "Reallocation Event Count" },
	{ 197, "Current Pending Sector Count" },
	{ 198, "Off-Line Scan Uncorrectable Sector Count" },
	{ 199, "Ultra DMA CRC Error Count" },
	{ 200, "Write Error Rate" },
	{ 201, "Soft Read Error Rate" },
	{ 202, "Data Address Mark Errors" },
	{ 203, "Run Out Cancel" },
	{ 204, "Soft ECC Correction" },
	{ 205, "Thermal Asperity Check" },
	{ 206, "Flying Height" },
	{ 207, "Spin High Current" },
	{ 208, "Spin Buzz" },
	{ 209, "Offline Seek Performance" },
	{ 220, "Disk Shift" },
	{ 221, "G-Sense Error Rate" },
	{ 222, "Loaded Hours" },
	{ 223, "Load/Unload Retry Count" },
	{ 224, "Load Friction" },
	{ 225, "Load/Unload Cycle Count" },
	{ 226, "Load-In Time" },
	{ 227, "Torque Amplification Count" },
	{ 228, "Power-Off Retract Count" },
	{ 230, "GMR Head Amplitude" },
	{ 231, "Temperature" },
	{ 232, "Available reserved space" },
	{ 233, "Media wearout indicator" },
	{ 235, "Power-Off Retract Count" },
	{ 240, "Head Flying Hours" },
	{ 241, "Total LBAs Written" },
	{ 242, "Total LBAs Read" },
	{ 249, "NAND Writes (1GB)" },
	{ 250, "Read Error Retry Rate" },
	{ 254, "Free Fall Sensor" },
	{ 0, NULL },
};

#define MAKEWORD(b1, b2) \
	(b2 << 8 | b1)
#define MAKEDWORD(b1, b2, b3, b4) \
	(b4 << 24 | b3 << 16 | b2 << 8 | b1)

int
main(int argc, char *argv[])
{
	struct command	*cmdp;

	if (argc < 2)
		usage();

	/*
	 * Open the device
	 */
	if ((fd = opendev(argv[1], O_RDWR, OPENDEV_PART, NULL)) == -1)
		err(1, "%s", argv[1]);

	/* Skip program name and device name. */
	if (argc != 2) {
		argv += 2;
		argc -= 2;
	} else {
		argv[1] = "identify";
		argv += 1;
		argc -= 1;
	}

	/* Look up and call the command. */
	for (cmdp = commands; cmdp->cmd_name != NULL; cmdp++)
		if (strcmp(argv[0], cmdp->cmd_name) == 0)
			break;
	if (cmdp->cmd_name == NULL)
		errx(1, "unknown command: %s", argv[0]);

	(cmdp->cmd_func)(argc, argv);

	return (0);
}

__dead void
usage(void)
{

	fprintf(stderr, "usage: %s device [command [arg]]\n", __progname);
	exit(1);
}

/*
 * Wrapper that calls ATAIOCCOMMAND and checks for errors
 */
void
ata_command(struct atareq *req)
{
	if (ioctl(fd, ATAIOCCOMMAND, req) == -1)
		err(1, "ATAIOCCOMMAND failed");

	switch (req->retsts) {

	case ATACMD_OK:
		return;
	case ATACMD_TIMEOUT:
		errx(1, "ATA command timed out");
	case ATACMD_DF:
		errx(1, "ATA device returned a Device Fault");
	case ATACMD_ERROR:
		if (req->error & WDCE_ABRT)
			errx(1, "ATA device returned Aborted Command");
		else
			errx(1, "ATA device returned error register %0x",
			    req->error);
	default:
		errx(1, "ATAIOCCOMMAND returned unknown result code %d",
		    req->retsts);
	}
}

/*
 * Print out strings associated with particular bitmasks
 */
void
print_bitinfo(const char *f, u_int bits, struct bitinfo *binfo)
{

	for (; binfo->bitmask != 0; binfo++)
		if (bits & binfo->bitmask)
			printf(f, binfo->string);
}

/*
 * strtoval():
 *    returns value associated with given string,
 *    if no value found -1 is returned.
 */
int
strtoval(const char *str, struct valinfo *vinfo)
{
	for (; vinfo->string != NULL; vinfo++)
		if (strcmp(str, vinfo->string) == 0)
			return (vinfo->value);
	return (-1);
}

/*
 * valtostr():
 *    returns string associated with given value,
 *    if no string found def value is returned.
 */
const char *
valtostr(int val, struct valinfo *vinfo, const char *def)
{
	for (; vinfo->string != NULL; vinfo++)
		if (val == vinfo->value)
			return (vinfo->string);
	return (def);
}

/*
 * DEVICE COMMANDS
 */

/*
 * device dump:
 *
 * extract issued ATA requests from the log buffer
 */
void
device_dump(int argc, char *argv[])
{
	unsigned char buf[131072];
	atagettrace_t agt;
	unsigned int total;
	unsigned int p = 0;
	int type;
	const char *types[] = { NULL, "status", "error", "ATAPI",
	    "ATAPI done", "ATA cmd", "ATA", "select slave",
	    "select master", "register read", "ATA LBA48" };
	int num_types = sizeof(types) / sizeof(types[0]);
	int info;
	int entrysize;
	int i;
	int flags;

	if (argc != 1)
		goto usage;

	memset(&agt, 0, sizeof(agt));
	agt.buf_size = sizeof(buf);
	agt.buf = buf;

	if (ioctl(fd, ATAIOGETTRACE, &agt) == -1)
		err(1, "ATAIOGETTRACE failed");

	total = agt.bytes_copied;

	/* Parse entries */
	while (p < total) {
		type = buf[p++];
		if (p >= total)
			return;
		if (type <= 0 || type >= num_types)
			return;

		info = buf[p++];
		if (p >= total)
			return;
		entrysize = (info & 0x1f);

		printf ("ch %d", (info >> 5) & 0x7);
		printf(": %s", types[type]);

		switch (type) {
		case WDCEVENT_STATUS:
			if (entrysize != 1)
				return;

			printf(": 0x%x", buf[p]);
			if (buf[p] & WDCS_BSY)
				printf(" BSY");
			if (buf[p] & WDCS_DRDY)
				printf(" DRDY");
			if (buf[p] & WDCS_DWF)
				printf(" DWF");
			if (buf[p] & WDCS_DSC)
				printf(" DSC");
			if (buf[p] & WDCS_DRQ)
				printf(" DRQ");
			if (buf[p] & WDCS_CORR)
				printf(" CORR");
			if (buf[p] & WDCS_IDX)
				printf(" IDX");
			if (buf[p] & WDCS_ERR)
				printf(" ERR");

			p++;
			entrysize = 0;
			break;
		case WDCEVENT_ERROR:
			if (entrysize != 1)
				return;

			printf(": 0x%x", buf[p]);
			if (buf[p] & WDCE_BBK)
				printf(" BBK/CRC");
			if (buf[p] & WDCE_UNC)
				printf(" UNC");
			if (buf[p] & WDCE_MC)
				printf(" MC");
			if (buf[p] & WDCE_IDNF)
				printf(" IDNF");
			if (buf[p] & WDCE_MCR)
				printf(" MCR");
			if (buf[p] & WDCE_ABRT)
				printf(" ABRT");
			if (buf[p] & WDCE_TK0NF)
				printf(" TK0NF");
			if (buf[p] & WDCE_AMNF)
				printf(" AMNF");

			p++;
			entrysize = 0;
			break;
		case WDCEVENT_ATAPI_CMD:
			if (entrysize < 2 || p + 2 > total)
				return;

			flags = (buf[p] << 8) + buf[p + 1];
			printf(": flags 0x%x", flags);
			if (flags & 0x0100)
				printf(" MEDIA");
			if (flags & 0x0080)
				printf(" SENSE");
			if (flags & 0x0040)
				printf(" DMA");
			if (flags & 0x0020)
				printf(" POLL");
			if (flags & 0x0004)
				printf(" TIMEOUT");
			if (flags & 0x0002)
				printf(" ATAPI");

			p += 2;
			entrysize -= 2;
			break;
		case WDCEVENT_ATAPI_DONE:
			if (entrysize != 3 || p + 3 > total)
				return;

			flags = (buf[p] << 8) + buf[p + 1];
			printf(": flags 0x%x", flags);
			if (flags & 0x0100)
				printf(" MEDIA");
			if (flags & 0x0080)
				printf(" SENSE");
			if (flags & 0x0040)
				printf(" DMA");
			if (flags & 0x0020)
				printf(" POLL");
			if (flags & 0x0004)
				printf(" TIMEOUT");
			if (flags & 0x0002)
				printf(" ATAPI");

			printf(", error 0x%x", buf[p + 2]);
			switch (buf[p + 2]) {
			case 1:
				printf(" (sense)");
				break;
			case 2:
				printf(" (driver failure)");
				break;
			case 3:
				printf(" (timeout)");
				break;
			case 4:
				printf(" (busy)");
				break;
			case 5:
				printf(" (ATAPI sense)");
				break;
			case 8:
				printf(" (reset)");
				break;
			}

			p += 3;
			entrysize  = 0;
			break;
		case WDCEVENT_ATA_LONG:
			if (entrysize != 7 || p + 7 > total)
				return;

			printf(": ");
			switch (buf[p + 6]) {
			case WDCC_READDMA:
				printf("READ DMA");
				break;
			case WDCC_WRITEDMA:
				printf("WRITE DMA");
				break;
			default:
				printf("CMD 0x%x", buf[p + 6]);
			}
			printf(" head %d, precomp %d, cyl_hi %d, "
			    "cyl_lo %d, sec %d, cnt %d",
			    buf[p], buf[p + 1], buf[p + 2], buf[p + 3],
			    buf[p + 4], buf[p + 5]);

			p += 7;
			entrysize = 0;
			break;
		case WDCEVENT_REG:
			if (entrysize != 3 || p + 3 > total)
				return;

			switch (buf[p]) {
			case 1:
				printf(": error");
				break;
			case 2:
				printf(": ireason");
				break;
			case 3:
				printf(": lba_lo");
				break;
			case 4:
				printf(": lba_mi");
				break;
			case 5:
				printf(": lba_hi");
				break;
			case 6:
				printf(": sdh");
				break;
			case 7:
				printf(": status");
				break;
			case 8:
				printf(": altstatus");
				break;
			default:
				printf(": unknown register %d", buf[p]);
			}
			printf(": 0x%x", (buf[p + 1] << 8) + buf[p + 2]);

			p += 3;
			entrysize = 0;
			break;
		case WDCEVENT_ATA_EXT:
			if (entrysize != 9 || p + 9 > total)
				return;

			printf(": ");
			switch (buf[p + 8]) {
			case WDCC_READDMA_EXT:
				printf("READ DMA EXT");
				break;
			case WDCC_WRITEDMA_EXT:
				printf("WRITE DMA EXT");
				break;
			default:
				printf("CMD 0x%x", buf[p + 8]);
			}
			printf(" lba_hi1 %d, lba_hi2 %d, "
			    "lba_mi1 %d, lba_mi2 %d, lba_lo1 %d, lba_lo2 %d, "
			    "count1 %d, count2 %d",
			    buf[p], buf[p + 1], buf[p + 2], buf[p + 3],
			    buf[p + 4], buf[p + 5], buf[p + 6],
			    buf[p + 7]);

			p += 9;
			entrysize = 0;
			break;
		}

		if (entrysize > 0)
			printf(":");
		for (i = 0; i < entrysize; i++) {
			printf (" 0x%02x", buf[p]);
			if (++p >= total)
				break;
		}
		printf("\n");
	}

	return;

usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}

/*
 * device_identify:
 *
 *	Display the identity of the device
 */
void
device_identify(int argc, char *argv[])
{
	struct ataparams *inqbuf;
	struct atareq req;
	char inbuf[DEV_BSIZE];
	u_int64_t capacity;
	u_int8_t *s;

	if (argc != 1)
		goto usage;

	memset(&inbuf, 0, sizeof(inbuf));
	memset(&req, 0, sizeof(req));

	inqbuf = (struct ataparams *) inbuf;

	req.flags = ATACMD_READ;
	req.command = WDCC_IDENTIFY;
	req.databuf = (caddr_t) inbuf;
	req.datalen = sizeof(inbuf);
	req.timeout = 1000;

	ata_command(&req);

	if (BYTE_ORDER == BIG_ENDIAN) {
		swap16_multi((u_int16_t *)inbuf, 10);
		swap16_multi(((u_int16_t *)inbuf) + 20, 3);
		swap16_multi(((u_int16_t *)inbuf) + 47, sizeof(inbuf) / 2 - 47);
	}

	if (!((inqbuf->atap_config & WDC_CFG_ATAPI_MASK) == WDC_CFG_ATAPI &&
	      ((inqbuf->atap_model[0] == 'N' &&
		  inqbuf->atap_model[1] == 'E') ||
	       (inqbuf->atap_model[0] == 'F' &&
		  inqbuf->atap_model[1] == 'X')))) {
		swap16_multi((u_int16_t *)(inqbuf->atap_model),
		    sizeof(inqbuf->atap_model) / 2);
		swap16_multi((u_int16_t *)(inqbuf->atap_serial),
		    sizeof(inqbuf->atap_serial) / 2);
		swap16_multi((u_int16_t *)(inqbuf->atap_revision),
		    sizeof(inqbuf->atap_revision) / 2);
	}

	/*
	 * Strip blanks off of the info strings.
	 */

	for (s = &inqbuf->atap_model[sizeof(inqbuf->atap_model) - 1];
	    s >= inqbuf->atap_model && *s == ' '; s--)
		*s = '\0';

	for (s = &inqbuf->atap_revision[sizeof(inqbuf->atap_revision) - 1];
	    s >= inqbuf->atap_revision && *s == ' '; s--)
		*s = '\0';

	for (s = &inqbuf->atap_serial[sizeof(inqbuf->atap_serial) - 1];
	    s >= inqbuf->atap_serial && *s == ' '; s--)
		*s = '\0';

	printf("Model: %.*s, Rev: %.*s, Serial #: %.*s\n",
	    (int) sizeof(inqbuf->atap_model), inqbuf->atap_model,
	    (int) sizeof(inqbuf->atap_revision), inqbuf->atap_revision,
	    (int) sizeof(inqbuf->atap_serial), inqbuf->atap_serial);

	printf("Device type: %s, %s\n", inqbuf->atap_config & WDC_CFG_ATAPI ?
	       "ATAPI" : "ATA", inqbuf->atap_config & ATA_CFG_FIXED ? "fixed" :
	       "removable");

	if (inqbuf->atap_cmd2_en & ATAPI_CMD2_48AD)
		capacity = ((u_int64_t)inqbuf->atap_max_lba[3] << 48) |
		    ((u_int64_t)inqbuf->atap_max_lba[2] << 32) |
		    ((u_int64_t)inqbuf->atap_max_lba[1] << 16) |
		    (u_int64_t)inqbuf->atap_max_lba[0];
	else
		capacity = (inqbuf->atap_capacity[1] << 16) |
		    inqbuf->atap_capacity[0];
	printf("Cylinders: %d, heads: %d, sec/track: %d, total "
	    "sectors: %llu\n", inqbuf->atap_cylinders,
	    inqbuf->atap_heads, inqbuf->atap_sectors, capacity);

	if ((inqbuf->atap_cmd_set2 & ATA_CMD2_RWQ) &&
	    (inqbuf->atap_queuedepth & WDC_QUEUE_DEPTH_MASK))
		printf("Device supports command queue depth of %d\n",
		    (inqbuf->atap_queuedepth & WDC_QUEUE_DEPTH_MASK) + 1);

	printf("Device capabilities:\n");
	print_bitinfo("\t%s\n", inqbuf->atap_capabilities1, ata_caps);

	if (inqbuf->atap_ata_major != 0 && inqbuf->atap_ata_major != 0xffff) {
		printf("Device supports the following standards:\n");
		print_bitinfo("%s ", inqbuf->atap_ata_major, ata_vers);
		printf("\n");
	}

	if ((inqbuf->atap_cmd_set1 & WDC_CMD1_SEC) &&
	    inqbuf->atap_mpasswd_rev != 0 &&
	    inqbuf->atap_mpasswd_rev != 0xffff)
		printf("Master password revision code 0x%04x\n",
		    inqbuf->atap_mpasswd_rev);

	if (inqbuf->atap_cmd_set1 != 0 && inqbuf->atap_cmd_set1 != 0xffff &&
	    inqbuf->atap_cmd_set2 != 0 && inqbuf->atap_cmd_set2 != 0xffff) {
		printf("Device supports the following command sets:\n");
		print_bitinfo("\t%s\n", inqbuf->atap_cmd_set1, ata_cmd_set1);
		print_bitinfo("\t%s\n", inqbuf->atap_cmd_set2, ata_cmd_set2);
		print_bitinfo("\t%s\n", inqbuf->atap_cmd_ext, ata_cmd_ext);
	}

	if (inqbuf->atap_cmd_def != 0 && inqbuf->atap_cmd_def != 0xffff) {
		printf("Device has enabled the following command "
		    "sets/features:\n");
		print_bitinfo("\t%s\n", inqbuf->atap_cmd1_en, ata_cmd_set1);
		print_bitinfo("\t%s\n", inqbuf->atap_cmd2_en, ata_cmd_set2);
	}

	return;

usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}

/*
 * device idle:
 *
 * issue the IDLE IMMEDIATE command to the drive
 */
void
device_idle(int argc, char *argv[])
{
	struct atareq req;

	if (argc != 1)
		goto usage;

	memset(&req, 0, sizeof(req));

	if (strcmp(argv[0], "idle") == 0)
		req.command = WDCC_IDLE_IMMED;
	else if (strcmp(argv[0], "standby") == 0)
		req.command = WDCC_STANDBY_IMMED;
	else
		req.command = WDCC_SLEEP;

	req.timeout = 1000;

	ata_command(&req);

	return;
usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}

/*
 * SECURITY SET PASSWORD command
 */
void
device_sec_setpass(int argc, char *argv[])
{
	struct atareq req;
	struct sec_password pwd;
	char *pass, inbuf[DEV_BSIZE];
	struct ataparams *inqbuf = (struct ataparams *)inbuf;

	if (argc < 2)
		goto usage;

	memset(&pwd, 0, sizeof(pwd));

	if (strcmp(argv[1], "user") == 0 && argc == 3)
		pwd.ctrl |= SEC_PASSWORD_USER;
	else if (strcmp(argv[1], "master") == 0 && argc == 2)
		pwd.ctrl |= SEC_PASSWORD_MASTER;
	else
		goto usage;
	if (argc == 3) {
		if (strcmp(argv[2], "high") == 0)
			pwd.ctrl |= SEC_LEVEL_HIGH;
		else if (strcmp(argv[2], "maximum") == 0)
			pwd.ctrl |= SEC_LEVEL_MAX;
		else
			goto usage;
	}

	/*
	 * Issue IDENTIFY command to obtain master password
	 * revision code and decrement its value.
	 * The valid revision codes are 0x0001 through 0xfffe.
	 * If the device returns 0x0000 or 0xffff as a revision
	 * code then the master password revision code is not
	 * supported so don't touch it.
	 */
	memset(&inbuf, 0, sizeof(inbuf));
	memset(&req, 0, sizeof(req));

	req.command = WDCC_IDENTIFY;
	req.timeout = 1000;
	req.flags = ATACMD_READ;
	req.databuf = (caddr_t)inbuf;
	req.datalen = sizeof(inbuf);

	ata_command(&req);

	pwd.revision = inqbuf->atap_mpasswd_rev;
	if (pwd.revision != 0 && pwd.revision != 0xffff && --pwd.revision == 0)
		pwd.revision = 0xfffe;

	pass = sec_getpass(pwd.ctrl & SEC_PASSWORD_MASTER, 1);
	memcpy(pwd.password, pass, strlen(pass));

	memset(&req, 0, sizeof(req));

	req.command = ATA_SEC_SET_PASSWORD;
	req.timeout = 1000;
	req.flags = ATACMD_WRITE;
	req.databuf = (caddr_t)&pwd;
	req.datalen = sizeof(pwd);

	ata_command(&req);

	return;
usage:
	fprintf(stderr, "usage: %s device %s user high | maximum\n",
	    __progname, argv[0]);
	fprintf(stderr, "       %s device %s master\n", __progname, argv[0]);
	exit(1);
}

/*
 * SECURITY UNLOCK command
 */
void
device_sec_unlock(int argc, char *argv[])
{
	struct atareq req;
	struct sec_password pwd;
	char *pass;

	if (argc != 2)
		goto usage;

	memset(&pwd, 0, sizeof(pwd));

	if (strcmp(argv[1], "user") == 0)
		pwd.ctrl |= SEC_PASSWORD_USER;
	else if (strcmp(argv[1], "master") == 0)
		pwd.ctrl |= SEC_PASSWORD_MASTER;
	else
		goto usage;

	pass = sec_getpass(pwd.ctrl & SEC_PASSWORD_MASTER, 0);
	memcpy(pwd.password, pass, strlen(pass));

	memset(&req, 0, sizeof(req));

	req.command = ATA_SEC_UNLOCK;
	req.timeout = 1000;
	req.flags = ATACMD_WRITE;
	req.databuf = (caddr_t)&pwd;
	req.datalen = sizeof(pwd);

	ata_command(&req);

	return;
usage:
	fprintf(stderr, "usage: %s device %s user | master\n", __progname,
	    argv[0]);
	exit(1);
}

/*
 * SECURITY ERASE UNIT command
 */
void
device_sec_erase(int argc, char *argv[])
{
	struct atareq req;
	struct sec_password pwd;
	char *pass;

	if (argc < 2)
		goto usage;

	memset(&pwd, 0, sizeof(pwd));

	if (strcmp(argv[1], "user") == 0)
		pwd.ctrl |= SEC_PASSWORD_USER;
	else if (strcmp(argv[1], "master") == 0)
		pwd.ctrl |= SEC_PASSWORD_MASTER;
	else
		goto usage;
	if (argc == 2)
		pwd.ctrl |= SEC_ERASE_NORMAL;
	else if (argc == 3 && strcmp(argv[2], "enhanced") == 0)
		pwd.ctrl |= SEC_ERASE_ENHANCED;
	else
		goto usage;

	pass = sec_getpass(pwd.ctrl & SEC_PASSWORD_MASTER, 0);
	memcpy(pwd.password, pass, strlen(pass));

	 /* Issue SECURITY ERASE PREPARE command before */
	memset(&req, 0, sizeof(req));

	req.command = ATA_SEC_ERASE_PREPARE;
	req.timeout = 1000;

	ata_command(&req);

	memset(&req, 0, sizeof(req));

	req.command = ATA_SEC_ERASE_UNIT;
	req.timeout = 1000;
	req.flags = ATACMD_WRITE;
	req.databuf = (caddr_t)&pwd;
	req.datalen = sizeof(pwd);

	ata_command(&req);

	return;
usage:
	fprintf(stderr, "usage: %s device %s user | master [enhanced]\n",
	    __progname, argv[0]);
	exit(1);
}

/*
 * SECURITY FREEZE LOCK command
 */
void
device_sec_freeze(int argc, char *argv[])
{
	struct atareq req;

	if (argc != 1)
		goto usage;

	memset(&req, 0, sizeof(req));

	req.command = ATA_SEC_FREEZE_LOCK;
	req.timeout = 1000;

	ata_command(&req);

	return;
usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}

/*
 * SECURITY DISABLE PASSWORD command
 */
void
device_sec_disablepass(int argc, char *argv[])
{
	struct atareq req;
	struct sec_password pwd;
	char *pass;

	if (argc != 2)
		goto usage;

	memset(&pwd, 0, sizeof(pwd));

	if (strcmp(argv[1], "user") == 0)
		pwd.ctrl |= SEC_PASSWORD_USER;
	else if (strcmp(argv[1], "master") == 0)
		pwd.ctrl |= SEC_PASSWORD_MASTER;
	else
		goto usage;

	pass = sec_getpass(pwd.ctrl & SEC_PASSWORD_MASTER, 0);
	memcpy(pwd.password, pass, strlen(pass));

	memset(&req, 0, sizeof(req));

	req.command = ATA_SEC_DISABLE_PASSWORD;
	req.timeout = 1000;
	req.flags = ATACMD_WRITE;
	req.databuf = (caddr_t)&pwd;
	req.datalen = sizeof(pwd);

	ata_command(&req);

	return;
usage:
	fprintf(stderr, "usage: %s device %s user | master\n", __progname,
	    argv[0]);
	exit(1);
}

char *
sec_getpass(int ident, int confirm)
{
	char *pass, buf[33];

	if ((pass = getpass(ident ? "Master password:" :
	    "User password:")) == NULL)
		err(1, "getpass()");
	if (strlen(pass) > 32)
		errx(1, "password too long");
	if (confirm) {
		strlcpy(buf, pass, sizeof(buf));
		if ((pass = getpass(ident ? "Retype master password:" :
		    "Retype user password:")) == NULL)
			err(1, "getpass()");
		if (strcmp(pass, buf) != 0)
			errx(1, "password mismatch");
	}

	return (pass);
}

/*
 * SMART ENABLE OPERATIONS command
 */
void
device_smart_enable(int argc, char *argv[])
{
	struct atareq req;

	if (argc != 1)
		goto usage;

	memset(&req, 0, sizeof(req));

	req.command = ATAPI_SMART;
	req.cylinder = 0xc24f;
	req.timeout = 1000;
	req.features = ATA_SMART_EN;

	ata_command(&req);

	return;
usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}

/*
 * SMART DISABLE OPERATIONS command
 */
void
device_smart_disable(int argc, char *argv[])
{
	struct atareq req;

	if (argc != 1)
		goto usage;

	memset(&req, 0, sizeof(req));

	req.command = ATAPI_SMART;
	req.cylinder = 0xc24f;
	req.timeout = 1000;
	req.features = ATA_SMART_DS;

	ata_command(&req);

	return;
usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}

/*
 * SMART STATUS command
 */
void
device_smart_status(int argc, char *argv[])
{
	struct atareq req;

	if (argc != 1)
		goto usage;

	memset(&req, 0, sizeof(req));

	req.command = ATAPI_SMART;
	req.cylinder = 0xc24f;
	req.timeout = 1000;
	req.features = ATA_SMART_STATUS;

	ata_command(&req);

	if (req.cylinder == 0xc24f)
		printf("No SMART threshold exceeded\n");
	else if (req.cylinder == 0x2cf4) {
		errx(2, "SMART threshold exceeded!");
	} else {
		errx(1, "Unknown response %02x!", req.cylinder);
	}

	return;
usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}

/*
 * SMART ENABLE/DISABLE ATTRIBUTE AUTOSAVE command
 */
void
device_smart_autosave(int argc, char *argv[])
{
	struct atareq req;
	int val;

	if (argc != 2)
		goto usage;

	memset(&req, 0, sizeof(req));

	req.command = ATAPI_SMART;
	req.cylinder = 0xc24f;
	req.timeout = 1000;
	req.features = ATA_SMART_AUTOSAVE;
	if ((val = strtoval(argv[1], smart_autosave)) == -1)
		goto usage;
	req.sec_num = val;

	ata_command(&req);

	return;
usage:
	fprintf(stderr, "usage: %s device %s enable | disable\n", __progname,
	    argv[0]);
	exit(1);
}

/*
 * SMART EXECUTE OFF-LINE IMMEDIATE command
 */
void
device_smart_offline(int argc, char *argv[])
{
	struct atareq req;
	int val;

	if (argc != 2)
		goto usage;

	memset(&req, 0, sizeof(req));

	req.command = ATAPI_SMART;
	req.cylinder = 0xc24f;
	req.timeout = 1000;
	req.features = ATA_SMART_OFFLINE;
	if ((val = strtoval(argv[1], smart_offline)) == -1)
		goto usage;
	req.sec_num = val;

	ata_command(&req);

	return;
usage:
	fprintf(stderr, "usage: %s device %s subcommand\n", __progname,
	    argv[0]);
	exit(1);
}

/*
 * SMART READ DATA command
 */
void
device_smart_read(int argc, char *argv[])
{
	struct atareq req;
	struct smart_read data;

	if (argc != 1)
		goto usage;

	memset(&req, 0, sizeof(req));
	memset(&data, 0, sizeof(data));

	req.command = ATAPI_SMART;
	req.cylinder = 0xc24f;
	req.timeout = 1000;
	req.features = ATA_SMART_READ;
	req.flags = ATACMD_READ;
	req.databuf = (caddr_t)&data;
	req.datalen = sizeof(data);

	ata_command(&req);

	if (smart_cksum((u_int8_t *)&data, sizeof(data)) != 0)
		errx(1, "Checksum mismatch");

	printf("Off-line data collection:\n");
	printf("    status: %s\n",
	    valtostr(data.offstat & 0x7f, smart_offstat, "?"));
	printf("    activity completion time: %d seconds\n",
	    letoh16(data.time));
	printf("    capabilities:\n");
	print_bitinfo("\t%s\n", data.offcap, smart_offcap);
	printf("Self-test execution:\n");
	printf("    status: %s\n", valtostr(SMART_SELFSTAT_STAT(data.selfstat),
	    smart_selfstat, "?"));
	if (SMART_SELFSTAT_STAT(data.selfstat) == SMART_SELFSTAT_PROGRESS)
		printf("remains %d%% of total time\n",
		    SMART_SELFSTAT_PCNT(data.selfstat));
	printf("    recommended polling time:\n");
	printf("\tshort routine: %d minutes\n", data.shtime);
	printf("\textended routine: %d minutes\n", data.extime);
	printf("SMART capabilities:\n");
	print_bitinfo("    %s\n", letoh16(data.smartcap), smart_smartcap);
	printf("Error logging: ");
	if (data.errcap & SMART_ERRCAP_ERRLOG)
		printf("supported\n");
	else
		printf("not supported\n");

	return;
usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}

/*
 * SMART READ LOG command
 */
void
device_smart_readlog(int argc, char *argv[])
{
	struct atareq req;
	int val;
	u_int8_t inbuf[DEV_BSIZE];

	if (argc != 2)
		goto usage;

	memset(&req, 0, sizeof(req));
	memset(&inbuf, 0, sizeof(inbuf));

	req.command = ATAPI_SMART;
	req.cylinder = 0xc24f;
	req.timeout = 1000;
	req.features = ATA_SMART_READLOG;
	req.flags = ATACMD_READ;
	req.sec_count = 1;
	req.databuf = (caddr_t)inbuf;
	req.datalen = sizeof(inbuf);
	if ((val = strtoval(argv[1], smart_readlog)) == -1)
		goto usage;
	req.sec_num = val;

	ata_command(&req);

	if (strcmp(argv[1], "directory") == 0) {
		struct smart_log_dir *data = (struct smart_log_dir *)inbuf;
		int i;

		if (data->version != SMART_LOG_MSECT) {
			printf("Device doesn't support multi-sector logs\n");
			return;
		}

		for (i = 0; i < 255; i++)
			printf("Log address %d: %d sectors\n", i + 1,
			    data->entry[i].sec_num);
	} else if (strcmp(argv[1], "summary") == 0) {
		struct smart_log_sum *data = (struct smart_log_sum *)inbuf;
		int i, n, nerr;

		if (smart_cksum(inbuf, sizeof(inbuf)) != 0)
			errx(1, "Checksum mismatch");

		if (data->index == 0) {
			printf("No log entries\n");
			return;
		}

		nerr = letoh16(data->err_cnt);
		printf("Error count: %d\n\n", nerr);
		/*
		 * Five error log data structures form a circular
		 * buffer. data->index points to the most recent
		 * record and err_cnt contains total error number.
		 * We pass from the most recent record to the
		 * latest one.
		 */
		i = data->index - 1;
		n = 0;
		do {
			printf("Error %d:\n", n + 1);
			smart_print_errdata(&data->errdata[i--]);
			if (i == -1)
				i = 4;
		} while (++n < (nerr > 5 ? 5 : nerr));
	} else if (strcmp(argv[1], "comp") == 0) {
		struct smart_log_comp *data = (struct smart_log_comp *)inbuf;
		u_int8_t *newbuf;
		int i, n, nerr, nsect;

		if (smart_cksum(inbuf, sizeof(inbuf)) != 0)
			errx(1, "Checksum mismatch");

		if (data->index == 0) {
			printf("No log entries\n");
			return;
		}

		i = data->index - 1;
		nerr = letoh16(data->err_cnt);
		printf("Error count: %d\n", nerr);
		/*
		 * From the first sector we obtain total error number
		 * and calculate necessary number of sectors to read.
		 * All read error data structures form a circular
		 * buffer and we pass from the most recent record to
		 * the latest one.
		 */
		nsect = nerr / 5 + (nerr % 5 != 0 ? 1 : 0);
		if ((newbuf = calloc(nsect, DEV_BSIZE)) == NULL)
			err(1, "calloc()");
		memset(&req, 0, sizeof(req));
		req.flags = ATACMD_READ;
		req.command = ATAPI_SMART;
		req.features = ATA_SMART_READLOG;
		req.sec_count = nsect;
		req.sec_num = SMART_READLOG_COMP;
		req.cylinder = 0xc24f;
		req.databuf = (caddr_t)newbuf;
		req.datalen = nsect * DEV_BSIZE;
		req.timeout = 1000;
		ata_command(&req);

		n = 0;
		data = (struct smart_log_comp *)
		    (newbuf + (nsect - 1) * DEV_BSIZE);
		do {
			printf("Error %d:\n", n + 1);
			smart_print_errdata(&data->errdata[i-- % 5]);
			if (i == -1)
				i = 254;
			if (i % 5 == 4)
				data = (struct smart_log_comp *)
				    (newbuf + (i / 5) * DEV_BSIZE);
		} while (++n < nerr);
	} else if (strcmp(argv[1], "selftest") == 0) {
		struct smart_log_self *data = (struct smart_log_self *)inbuf;
		int i, n;

		if (smart_cksum(inbuf, sizeof(inbuf)) != 0)
			errx(1, "Checksum mismatch");

		if (data->index == 0) {
			printf("No log entries\n");
			return;
		}

		/* circular buffer of 21 entries */
		i = data->index - 1;
		n = 0;
		do {
			/* don't print empty entries */
			if ((data->desc[i].time1 | data->desc[i].time2) == 0)
				break;
			printf("Test %d\n", n + 1);
			printf("    LBA Low: 0x%x\n", data->desc[i].reg_lbalo);
			printf("    status: %s\n",
			    valtostr(SMART_SELFSTAT_STAT(
			    data->desc[i].selfstat),
			    smart_selfstat, "?"));
			printf("    timestamp: %d\n",
			    MAKEWORD(data->desc[i].time1,
				     data->desc[i].time2));
			printf("    failure checkpoint byte: 0x%x\n",
			    data->desc[i].chkpnt);
			printf("    failing LBA: 0x%x\n",
			    MAKEDWORD(data->desc[i].lbafail1,
				      data->desc[i].lbafail2,
				      data->desc[i].lbafail3,
				      data->desc[i].lbafail4));
			if (--i == -1)
				i = 20;
		} while (++n < 21);
	}

	return;
usage:
	fprintf(stderr, "usage: %s device %s log\n", __progname, argv[0]);
	exit(1);
}

#define SMART_PRINTREG(str, reg)				\
	printf(str "0x%02x\t0x%02x\t0x%02x\t0x%02x\t0x%02x\n",	\
	    data->cmd[0].reg,					\
	    data->cmd[1].reg,					\
	    data->cmd[2].reg,					\
	    data->cmd[3].reg,					\
	    data->cmd[4].reg)

void
smart_print_errdata(struct smart_log_errdata *data)
{
	printf("    error register: 0x%x\n", data->err.reg_err);
	printf("    sector count register: 0x%x\n", data->err.reg_seccnt);
	printf("    LBA Low register: 0x%x\n", data->err.reg_lbalo);
	printf("    LBA Mid register: 0x%x\n", data->err.reg_lbamid);
	printf("    LBA High register: 0x%x\n", data->err.reg_lbahi);
	printf("    device register: 0x%x\n", data->err.reg_dev);
	printf("    status register: 0x%x\n", data->err.reg_stat);
	printf("    state: %s\n", valtostr(data->err.state, smart_logstat, "?"));
	printf("    timestamp: %d\n", MAKEWORD(data->err.time1,
					       data->err.time2));
	printf("    history:\n");
	SMART_PRINTREG("\tcontrol register:\t", reg_ctl);
	SMART_PRINTREG("\tfeatures register:\t", reg_feat);
	SMART_PRINTREG("\tsector count register:\t", reg_seccnt);
	SMART_PRINTREG("\tLBA Low register:\t", reg_lbalo);
	SMART_PRINTREG("\tLBA Mid register:\t", reg_lbamid);
	SMART_PRINTREG("\tLBA High register:\t", reg_lbahi);
	SMART_PRINTREG("\tdevice register:\t", reg_dev);
	SMART_PRINTREG("\tcommand register:\t", reg_cmd);
	printf("\ttimestamp:\t\t"
	    "%d\t%d\t%d\t%d\t%d\n",
	    MAKEDWORD(data->cmd[0].time1, data->cmd[0].time2,
		      data->cmd[0].time3, data->cmd[0].time4),
	    MAKEDWORD(data->cmd[1].time1, data->cmd[1].time2,
		      data->cmd[1].time3, data->cmd[1].time4),
	    MAKEDWORD(data->cmd[2].time1, data->cmd[2].time2,
		      data->cmd[2].time3, data->cmd[2].time4),
	    MAKEDWORD(data->cmd[3].time1, data->cmd[3].time2,
		      data->cmd[3].time3, data->cmd[3].time4),
	    MAKEDWORD(data->cmd[4].time1, data->cmd[4].time2,
		      data->cmd[4].time3, data->cmd[4].time4));
}

int
smart_cksum(u_int8_t *data, size_t len)
{
	u_int8_t sum = 0;
	size_t i;

	for (i = 0; i < len; i++)
		sum += data[i];

	return (sum);
}

/*
 * Read device attributes
 */
void
device_attr(int argc, char *argv[])
{
	struct atareq req;
	struct smart_read attr_val;
	struct smart_threshold attr_thr;
	struct attribute *attr;
	struct threshold *thr;
	const char *attr_name;
	static const char hex[]="0123456789abcdef";
	char raw[13], *format;
	int i, k, threshold_exceeded = 0;

	if (argc != 1)
		goto usage;

	memset(&req, 0, sizeof(req));
	memset(&attr_val, 0, sizeof(attr_val));	/* XXX */
	memset(&attr_thr, 0, sizeof(attr_thr));	/* XXX */

	req.command = ATAPI_SMART;
	req.cylinder = 0xc24f;		/* LBA High = C2h, LBA Mid = 4Fh */
	req.timeout = 1000;

	req.features = ATA_SMART_READ;
	req.flags = ATACMD_READ;
	req.databuf = (caddr_t)&attr_val;
	req.datalen = sizeof(attr_val);
	ata_command(&req);

	req.features = ATA_SMART_THRESHOLD;
	req.flags = ATACMD_READ;
	req.databuf = (caddr_t)&attr_thr;
	req.datalen = sizeof(attr_thr);
	ata_command(&req);

	if (smart_cksum((u_int8_t *)&attr_val, sizeof(attr_val)) != 0)
		errx(1, "Checksum mismatch (attr_val)");

	if (smart_cksum((u_int8_t *)&attr_thr, sizeof(attr_thr)) != 0)
		errx(1, "Checksum mismatch (attr_thr)");

	attr = attr_val.attribute;
	thr = attr_thr.threshold;

	printf("Attributes table revision: %d\n", attr_val.revision);
	printf("ID\tAttribute name\t\t\tThreshold\tValue\tRaw\n");
	for (i = 0; i < 30; i++) {
		if (thr[i].id != 0 && thr[i].id == attr[i].id) {
			attr_name = valtostr(thr[i].id, ibm_attr_names,
			    "Unknown");

			for (k = 0; k < 6; k++) {
				u_int8_t b;
				b = attr[i].raw[6 - k];
				raw[k + k] = hex[b >> 4];
				raw[k + k + 1] = hex[b & 0x0f];
			}
			raw[k + k] = '\0';
			if (thr[i].value >= attr[i].value) {
				++threshold_exceeded;
				format = "%3d    *%-32.32s %3d\t\t%3d\t0x%s\n";
			} else {
				format = "%3d\t%-32.32s %3d\t\t%3d\t0x%s\n";
			}
			printf(format, thr[i].id, attr_name,
			    thr[i].value, attr[i].value, raw);
		}
	}
	if (threshold_exceeded)
		fprintf(stderr, "One or more threshold values exceeded!\n");

	return;

usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}

/*
 * Set the automatic acoustic management on the disk.
 */
void
device_acoustic(int argc, char *argv[])
{
	u_char acoustic;
	struct atareq req;
	const char *errstr;

	if (argc != 2)
		goto usage;

	acoustic = strtonum(argv[1], 0, 126, &errstr);
	if (errstr)
		errx(1, "Acoustic management value \"%s\" is %s "
		    "(valid values: 0 - 126)", argv[1], errstr);

	memset(&req, 0, sizeof(req));

	req.sec_count = acoustic + 0x80;

	req.command = SET_FEATURES ;
	req.features = WDSF_AAM_EN ;
	req.timeout = 1000;

	ata_command(&req);

	return;

usage:
	fprintf(stderr, "usage: %s device %s acoustic-management-level\n",
	    __progname, argv[0]);
	exit(1);
}

/*
 * Set the advanced power managmement on the disk. Power management
 * levels are translated from user-range 0-253 to ATAPI levels 1-0xFD
 * to keep a uniform interface to the user.
 */
void
device_apm(int argc, char *argv[])
{
	u_char power;
	struct atareq req;
	const char *errstr;

	if (argc != 2)
		goto usage;

	power = strtonum(argv[1], 0, 253, &errstr);
	if (errstr)
		errx(1, "Advanced power management value \"%s\" is %s "
		    "(valid values: 0 - 253)", argv[1], errstr);

	memset(&req, 0, sizeof(req));

	req.sec_count = power + 0x01;

	req.command = SET_FEATURES ;
	req.features = WDSF_APM_EN ;
	req.timeout = 1000;

	ata_command(&req);

	return;

usage:
	fprintf(stderr, "usage: %s device %s power-management-level\n",
	    __progname, argv[0]);
	exit(1);
}

/*
 * En/disable features (the automatic acoustic managmement, Advanced Power
 * Management) on the disk.
 */
void
device_feature(int argc, char *argv[])
{
	struct atareq req;

	if (argc != 1)
		goto usage;

	memset(&req, 0, sizeof(req));

	req.command = SET_FEATURES ;

	if (strcmp(argv[0], "acousticdisable") == 0)
		req.features = WDSF_AAM_DS;
	else if (strcmp(argv[0], "readaheadenable") == 0)
		req.features = WDSF_READAHEAD_EN;
	else if (strcmp(argv[0], "readaheaddisable") == 0)
		req.features = WDSF_READAHEAD_DS;
	else if (strcmp(argv[0], "writecacheenable") == 0)
		req.features = WDSF_EN_WR_CACHE;
	else if (strcmp(argv[0], "writecachedisable") == 0)
		req.features = WDSF_WRITE_CACHE_DS;
	else if (strcmp(argv[0], "apmdisable") == 0)
		req.features = WDSF_APM_DS;
	else if (strcmp(argv[0], "podenable") == 0)
		req.features = WDSF_POD_EN;
	else if (strcmp(argv[0], "poddisable") == 0)
		req.features = WDSF_POD_DS;
	else if (strcmp(argv[0], "puisenable") == 0)
		req.features = WDSF_PUIS_EN;
	else if (strcmp(argv[0], "puisdisable") == 0)
		req.features = WDSF_PUIS_DS;
	else if (strcmp(argv[0], "puisspinup") == 0)
		req.features = WDSF_PUIS_SPINUP;
	else
		goto usage;

	req.timeout = 1000;

	ata_command(&req);

	return;

usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}

/*
 * Set the idle timer on the disk.  Set it for either idle mode or
 * standby mode, depending on how we were invoked.
 */
void
device_setidle(int argc, char *argv[])
{
	unsigned long idle;
	struct atareq req;
	char *end;

	if (argc != 2)
		goto usage;

	idle = strtoul(argv[1], &end, 0);

	if (*end != '\0' || idle > 19800)
		errx(1, "Invalid idle time: \"%s\" "
		    "(valid values: 1 - 19800)", argv[1]);

	if (idle != 0 && idle < 5)
		errx(1, "Idle timer must be at least 5 seconds");

	memset(&req, 0, sizeof(req));

	if (idle <= 240 * 5)
		req.sec_count = idle / 5;
	else
		req.sec_count = idle / (30 * 60) + 240;

	if (strcmp(argv[0], "setstandby") == 0)
		req.command = WDCC_STANDBY;
	else if (strcmp(argv[0], "setidle") == 0)
		req.command = WDCC_IDLE;
	else
		goto usage;
	req.timeout = 1000;

	ata_command(&req);

	return;

usage:
	fprintf(stderr, "usage: %s device %s %s\n", __progname, argv[0],
	    (strcmp(argv[0], "setidle") == 0) ? "idle-timer" : "standby-timer");
	exit(1);
}

/*
 * Query the device for the current power mode
 */
void
device_checkpower(int argc, char *argv[])
{
	struct atareq req;

	if (argc != 1)
		goto usage;

	memset(&req, 0, sizeof(req));

	req.command = WDCC_CHECK_PWR;
	req.timeout = 1000;
	req.flags = ATACMD_READREG;

	ata_command(&req);

	printf("Current power status: ");

	switch (req.sec_count) {
	case 0x00:
		printf("Standby mode\n");
		break;
	case 0x80:
		printf("Idle mode\n");
		break;
	case 0xff:
		printf("Active mode\n");
		break;
	default:
		printf("Unknown power code (%02x)\n", req.sec_count);
	}

	return;
usage:
	fprintf(stderr, "usage: %s device %s\n", __progname, argv[0]);
	exit(1);
}
