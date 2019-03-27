/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Sandvine Incorporated. All rights reserved.
 * Copyright (c) 2002-2011 Andre Albsmeier <andre@albsmeier.net>
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

/*
 * This software is derived from Andre Albsmeier's fwprog.c which contained
 * the following note:
 *
 * Many thanks goes to Marc Frajola <marc@terasolutions.com> from
 * TeraSolutions for the initial idea and his programme for upgrading
 * the firmware of I*M DDYS drives.
 */

/*
 * BEWARE:
 *
 * The fact that you see your favorite vendor listed below does not
 * imply that your equipment won't break when you use this software
 * with it. It only means that the firmware of at least one device type
 * of each vendor listed has been programmed successfully using this code.
 *
 * The -s option simulates a download but does nothing apart from that.
 * It can be used to check what chunk sizes would have been used with the
 * specified device.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <camlib.h>

#include "progress.h"

#include "camcontrol.h"

#define	WB_TIMEOUT 50000	/* 50 seconds */

typedef enum {
	VENDOR_HGST,
	VENDOR_HITACHI,
	VENDOR_HP,
	VENDOR_IBM,
	VENDOR_PLEXTOR,
	VENDOR_QUALSTAR,
	VENDOR_QUANTUM,
	VENDOR_SAMSUNG,
	VENDOR_SEAGATE,
	VENDOR_SMART,
	VENDOR_ATA,
	VENDOR_UNKNOWN
} fw_vendor_t;

/*
 * FW_TUR_READY:     The drive must return good status for a test unit ready.
 *
 * FW_TUR_NOT_READY: The drive must return not ready status for a test unit
 *		     ready.  You may want this in a removable media drive.
 *
 * FW_TUR_NA:	     It doesn't matter whether the drive is ready or not.
 * 		     This may be the case for a removable media drive.
 */
typedef enum {
	FW_TUR_NONE,
	FW_TUR_READY,
	FW_TUR_NOT_READY,
	FW_TUR_NA
} fw_tur_status;

/*
 * FW_TIMEOUT_DEFAULT:		Attempt to probe for a WRITE BUFFER timeout
 *				value from the drive.  If we get an answer,
 *				use the Recommended timeout.  Otherwise,
 * 				use the default value from the table.
 *
 * FW_TIMEOUT_DEV_REPORTED:	The timeout value was probed directly from
 *				the device.
 *
 * FW_TIMEOUT_NO_PROBE:		Do not ask the device for a WRITE BUFFER
 * 				timeout value.  Use the device-specific
 *				value.
 *
 * FW_TIMEOUT_USER_SPEC:	The user specified a timeout on the command
 *				line with the -t option.  This overrides any
 *				probe or default timeout.
 */
typedef enum {
	FW_TIMEOUT_DEFAULT,
	FW_TIMEOUT_DEV_REPORTED,
	FW_TIMEOUT_NO_PROBE,
	FW_TIMEOUT_USER_SPEC
} fw_timeout_type;

/*
 * type: 		Enumeration for the particular vendor.
 *
 * pattern:		Pattern to match for the Vendor ID from the SCSI
 *			Inquiry data.
 *
 * dev_type:		SCSI device type to match, or T_ANY to match any
 *			device from the given vendor.  Note that if there
 *			is a specific device type listed for a particular
 *			vendor, it must be listed before a T_ANY entry.
 *
 * max_pkt_size:	Maximum packet size when talking to a device.  Note
 *			that although large data sizes may be supported by
 *			the target device, they may not be supported by the
 *			OS or the controller.
 *
 * cdb_byte2:		This specifies byte 2 (byte 1 when counting from 0)
 *			of the CDB.  This is generally the WRITE BUFFER mode.
 *
 * cdb_byte2_last:	This specifies byte 2 for the last chunk of the
 *			download.
 *
 * inc_cdb_buffer_id:	Increment the buffer ID by 1 for each chunk sent
 *			down to the drive.
 *
 * inc_cdb_offset:	Increment the offset field in the CDB with the byte
 *			offset into the firmware file.
 *
 * tur_status:		Pay attention to whether the device is ready before
 *			upgrading the firmware, or not.  See above for the
 *			values.
 */
struct fw_vendor {
	fw_vendor_t type;
	const char *pattern;
	int dev_type;
	int max_pkt_size;
	u_int8_t cdb_byte2;
	u_int8_t cdb_byte2_last;
	int inc_cdb_buffer_id;
	int inc_cdb_offset;
	fw_tur_status tur_status;
	int timeout_ms;
	fw_timeout_type timeout_type;
};

/*
 * Vendor notes:
 *
 * HGST:     The packets need to be sent in multiples of 4K.
 *
 * IBM:      For LTO and TS drives, the buffer ID is ignored in mode 7 (and
 * 	     some other modes).  It treats the request as a firmware download.
 *           The offset (and therefore the length of each chunk sent) needs
 *           to be a multiple of the offset boundary specified for firmware
 *           (buffer ID 4) in the read buffer command.  At least for LTO-6,
 *           that seems to be 0, but using a 32K chunk size should satisfy
 *           most any alignment requirement.
 *
 * SmrtStor: Mode 5 is also supported, but since the firmware is 400KB or
 *           so, we can't fit it in a single request in most cases.
 */
static struct fw_vendor vendors_list[] = {
	{VENDOR_HGST,	 	"HGST",		T_DIRECT,
	0x1000, 0x07, 0x07, 1, 0, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_HITACHI, 	"HITACHI",	T_ANY,
	0x8000, 0x05, 0x05, 1, 0, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_HP,	 	"HP",		T_ANY,
	0x8000, 0x07, 0x07, 0, 1, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_IBM,		"IBM",		T_SEQUENTIAL,
	0x8000, 0x07, 0x07, 0, 1, FW_TUR_NA, 300 * 1000, FW_TIMEOUT_DEFAULT},
	{VENDOR_IBM,		"IBM",		T_ANY,
	0x8000, 0x05, 0x05, 1, 0, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_PLEXTOR,	"PLEXTOR",	T_ANY,
	0x2000, 0x04, 0x05, 0, 1, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_QUALSTAR,	"QUALSTAR",	T_ANY,
	0x2030, 0x05, 0x05, 0, 0, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_QUANTUM,	"QUANTUM",	T_ANY,
	0x2000, 0x04, 0x05, 0, 1, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_SAMSUNG,	"SAMSUNG",	T_ANY,
	0x8000, 0x07, 0x07, 0, 1, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_SEAGATE,	"SEAGATE",	T_ANY,
	0x8000, 0x07, 0x07, 0, 1, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_SMART,		"SmrtStor",	T_DIRECT,
	0x8000, 0x07, 0x07, 0, 1, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_HGST,	 	"WD",		T_DIRECT,
	0x1000, 0x07, 0x07, 1, 0, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},
	{VENDOR_HGST,	 	"WDC",		T_DIRECT,
	0x1000, 0x07, 0x07, 1, 0, FW_TUR_READY, WB_TIMEOUT, FW_TIMEOUT_DEFAULT},

	/*
	 * We match any ATA device.  This is really just a placeholder,
	 * since we won't actually send a WRITE BUFFER with any of the
	 * listed parameters.  If a SATA device is behind a SAS controller,
	 * the SCSI to ATA translation code (at least for LSI) doesn't
	 * generally translate a SCSI WRITE BUFFER into an ATA DOWNLOAD
	 * MICROCODE command.  So, we use the SCSI ATA PASS_THROUGH command
	 * to send the ATA DOWNLOAD MICROCODE command instead.
	 */
	{VENDOR_ATA,		"ATA",		T_ANY,
	 0x8000, 0x07, 0x07, 0, 1, FW_TUR_READY, WB_TIMEOUT,
	 FW_TIMEOUT_NO_PROBE},
	{VENDOR_UNKNOWN,	NULL,		T_ANY,
	0x0000, 0x00, 0x00, 0, 0, FW_TUR_NONE, WB_TIMEOUT, FW_TIMEOUT_DEFAULT}
};

struct fw_timeout_desc {
	fw_timeout_type timeout_type;
	const char *timeout_desc;
};

static const struct fw_timeout_desc fw_timeout_desc_table[] = {
	{ FW_TIMEOUT_DEFAULT, "the default" },
	{ FW_TIMEOUT_DEV_REPORTED, "recommended by this particular device" },
	{ FW_TIMEOUT_NO_PROBE, "the default" },
	{ FW_TIMEOUT_USER_SPEC, "what was specified on the command line" }
};

#ifndef ATA_DOWNLOAD_MICROCODE
#define ATA_DOWNLOAD_MICROCODE	0x92
#endif

#define USE_OFFSETS_FEATURE	0x3

#ifndef LOW_SECTOR_SIZE
#define LOW_SECTOR_SIZE		512
#endif

#define ATA_MAKE_LBA(o, p)	\
	((((((o) / LOW_SECTOR_SIZE) >> 8) & 0xff) << 16) | \
	  ((((o) / LOW_SECTOR_SIZE) & 0xff) << 8) | \
	  ((((p) / LOW_SECTOR_SIZE) >> 8) & 0xff))

#define ATA_MAKE_SECTORS(p)	(((p) / 512) & 0xff)

#ifndef UNKNOWN_MAX_PKT_SIZE
#define UNKNOWN_MAX_PKT_SIZE	0x8000
#endif

static struct fw_vendor *fw_get_vendor(struct cam_device *cam_dev,
				       struct ata_params *ident_buf);
static int fw_get_timeout(struct cam_device *cam_dev, struct fw_vendor *vp,
			  int task_attr, int retry_count, int timeout);
static int fw_validate_ibm(struct cam_device *dev, int retry_count,
			   int timeout, int fd, char *buf,
			    const char *fw_img_path, int quiet);
static char *fw_read_img(struct cam_device *dev, int retry_count,
			 int timeout, int quiet, const char *fw_img_path,
			 struct fw_vendor *vp, int *num_bytes);
static int fw_check_device_ready(struct cam_device *dev,
				 camcontrol_devtype devtype,
				 struct fw_vendor *vp, int printerrors,
				 int timeout);
static int fw_download_img(struct cam_device *cam_dev,
			   struct fw_vendor *vp, char *buf, int img_size,
			   int sim_mode, int printerrors, int quiet,
			   int retry_count, int timeout, const char */*name*/,
			   camcontrol_devtype devtype);

/*
 * Find entry in vendors list that belongs to
 * the vendor of given cam device.
 */
static struct fw_vendor *
fw_get_vendor(struct cam_device *cam_dev, struct ata_params *ident_buf)
{
	char vendor[42];
	struct fw_vendor *vp;

	if (cam_dev == NULL)
		return (NULL);

	if (ident_buf != NULL) {
		cam_strvis((u_char *)vendor, ident_buf->model,
		    sizeof(ident_buf->model), sizeof(vendor));
		for (vp = vendors_list; vp->pattern != NULL; vp++) {
			if (vp->type == VENDOR_ATA)
				return (vp);
		}
	} else {
		cam_strvis((u_char *)vendor, (u_char *)cam_dev->inq_data.vendor,
		    sizeof(cam_dev->inq_data.vendor), sizeof(vendor));
	}
	for (vp = vendors_list; vp->pattern != NULL; vp++) {
		if (!cam_strmatch((const u_char *)vendor,
		    (const u_char *)vp->pattern, strlen(vendor))) {
			if ((vp->dev_type == T_ANY)
			 || (vp->dev_type == SID_TYPE(&cam_dev->inq_data)))
				break;
		}
	}
	return (vp);
}

static int
fw_get_timeout(struct cam_device *cam_dev, struct fw_vendor *vp,
	       int task_attr, int retry_count, int timeout)
{
	struct scsi_report_supported_opcodes_one *one;
	struct scsi_report_supported_opcodes_timeout *td;
	uint8_t *buf = NULL;
	uint32_t fill_len = 0, cdb_len = 0, rec_timeout = 0;
	int retval = 0;

	/*
	 * If the user has specified a timeout on the command line, we let
	 * him override any default or probed value.
	 */
	if (timeout != 0) {
		vp->timeout_type = FW_TIMEOUT_USER_SPEC;
		vp->timeout_ms = timeout;
		goto bailout;
	}

	/*
	 * Check to see whether we should probe for a timeout for this
	 * device.
	 */
	if (vp->timeout_type == FW_TIMEOUT_NO_PROBE)
		goto bailout;

	retval = scsigetopcodes(/*device*/ cam_dev,
				/*opcode_set*/ 1,
				/*opcode*/ WRITE_BUFFER,
				/*show_sa_errors*/ 1,
				/*sa_set*/ 0,
				/*service_action*/ 0,
				/*timeout_desc*/ 1,
				/*task_attr*/ task_attr,
				/*retry_count*/ retry_count,
				/*timeout*/ 10000,
				/*verbose*/ 0,
				/*fill_len*/ &fill_len,
				/*data_ptr*/ &buf);
	/*
	 * It isn't an error if we can't get a timeout descriptor.  We just
	 * continue on with the default timeout.
	 */
	if (retval != 0) {
		retval = 0;
		goto bailout;
	}

	/*
	 * Even if the drive didn't return a SCSI error, if we don't have
	 * enough data to contain the one opcode descriptor, the CDB
	 * structure and a timeout descriptor, we don't have the timeout
	 * value we're looking for.  So we'll just fall back to the
	 * default value.
	 */
	if (fill_len < (sizeof(*one) + sizeof(struct scsi_write_buffer) +
	    sizeof(*td)))
		goto bailout;

	one = (struct scsi_report_supported_opcodes_one *)buf;

	/*
	 * If the drive claims to not support the WRITE BUFFER command...
	 * fall back to the default timeout value and let things fail on
	 * the actual firmware download.
	 */
	if ((one->support & RSO_ONE_SUP_MASK) == RSO_ONE_SUP_NOT_SUP)
		goto bailout;

	cdb_len = scsi_2btoul(one->cdb_length);
	td = (struct scsi_report_supported_opcodes_timeout *)
	    &buf[sizeof(*one) + cdb_len];

	rec_timeout = scsi_4btoul(td->recommended_time);
	/*
	 * If the recommended timeout is 0, then the device has probably
	 * returned a bogus value.
	 */
	if (rec_timeout == 0)
		goto bailout;

	/* CAM timeouts are in ms */
	rec_timeout *= 1000;

	vp->timeout_ms = rec_timeout;
	vp->timeout_type = FW_TIMEOUT_DEV_REPORTED;

bailout:
	return (retval);
}

#define	SVPD_IBM_FW_DESIGNATION		0x03

/*
 * IBM LTO and TS tape drives have an INQUIRY VPD page 0x3 with the following
 * format:
 */
struct fw_ibm_tape_fw_designation {
	uint8_t	device;
	uint8_t page_code;
	uint8_t reserved;
	uint8_t length;
	uint8_t ascii_length;
	uint8_t reserved2[3];
	uint8_t load_id[4];
	uint8_t fw_rev[4];
	uint8_t ptf_number[4];
	uint8_t patch_number[4];
	uint8_t ru_name[8];
	uint8_t lib_seq_num[5];
};

/*
 * The firmware for IBM tape drives has the following header format.  The
 * load_id and ru_name in the header file should match what is returned in
 * VPD page 0x3.
 */
struct fw_ibm_tape_fw_header {
	uint8_t unspec[4];
	uint8_t length[4];		/* Firmware and header! */
	uint8_t load_id[4];
	uint8_t fw_rev[4];
	uint8_t reserved[8];
	uint8_t ru_name[8];
};

static int
fw_validate_ibm(struct cam_device *dev, int retry_count, int timeout, int fd,
		char *buf, const char *fw_img_path, int quiet)
{
	union ccb *ccb;
	struct fw_ibm_tape_fw_designation vpd_page;
	struct fw_ibm_tape_fw_header *header;
	char drive_rev[sizeof(vpd_page.fw_rev) + 1];
	char file_rev[sizeof(vpd_page.fw_rev) + 1];
	int retval = 1;

	ccb = cam_getccb(dev);
	if (ccb == NULL) {
		warnx("couldn't allocate CCB");
		goto bailout;
	}

	/* cam_getccb cleans up the header, caller has to zero the payload */
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	bzero(&vpd_page, sizeof(vpd_page));

	scsi_inquiry(&ccb->csio,
		     /*retries*/ retry_count,
		     /*cbfcnp*/ NULL,
		     /* tag_action */ MSG_SIMPLE_Q_TAG,
		     /* inq_buf */ (u_int8_t *)&vpd_page,
		     /* inq_len */ sizeof(vpd_page),
		     /* evpd */ 1,
		     /* page_code */ SVPD_IBM_FW_DESIGNATION,
		     /* sense_len */ SSD_FULL_SIZE,
		     /* timeout */ timeout ? timeout : 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (retry_count != 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(dev, ccb) < 0) {
		warn("error getting firmware designation page");

		cam_error_print(dev, ccb, CAM_ESF_ALL,
				CAM_EPF_ALL, stderr);

		cam_freeccb(ccb);
		ccb = NULL;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(dev, ccb, CAM_ESF_ALL,
				CAM_EPF_ALL, stderr);
		goto bailout;
	}

	/*
	 * Read the firmware header only.
	 */
	if (read(fd, buf, sizeof(*header)) != sizeof(*header)) {
		warn("unable to read %zu bytes from %s", sizeof(*header),
		     fw_img_path);
		goto bailout;
	} 

	/* Rewind the file back to 0 for the full file read. */
	if (lseek(fd, 0, SEEK_SET) == -1) {
		warn("Unable to lseek");
		goto bailout;
	}

	header = (struct fw_ibm_tape_fw_header *)buf;

	bzero(drive_rev, sizeof(drive_rev));
	bcopy(vpd_page.fw_rev, drive_rev, sizeof(vpd_page.fw_rev));
	bzero(file_rev, sizeof(file_rev));
	bcopy(header->fw_rev, file_rev, sizeof(header->fw_rev));

	if (quiet == 0) {
		fprintf(stdout, "Current Drive Firmware version: %s\n",
			drive_rev);
		fprintf(stdout, "Firmware File version: %s\n", file_rev);
	}

	/*
	 * For IBM tape drives the load ID and RU name reported by the
	 * drive should match what is in the firmware file.
	 */
	if (bcmp(vpd_page.load_id, header->load_id,
		 MIN(sizeof(vpd_page.load_id), sizeof(header->load_id))) != 0) {
		warnx("Drive Firmware load ID 0x%x does not match firmware "
		      "file load ID 0x%x", scsi_4btoul(vpd_page.load_id),
		      scsi_4btoul(header->load_id));
		goto bailout;
	}

	if (bcmp(vpd_page.ru_name, header->ru_name,
		 MIN(sizeof(vpd_page.ru_name), sizeof(header->ru_name))) != 0) {
		warnx("Drive Firmware RU name 0x%jx does not match firmware "
		      "file RU name 0x%jx",
		      (uintmax_t)scsi_8btou64(vpd_page.ru_name),
		      (uintmax_t)scsi_8btou64(header->ru_name));
		goto bailout;
	}
	if (quiet == 0)
		fprintf(stdout, "Firmware file is valid for this drive.\n");
	retval = 0;
bailout:
	cam_freeccb(ccb);

	return (retval);
}

/*
 * Allocate a buffer and read fw image file into it
 * from given path. Number of bytes read is stored
 * in num_bytes.
 */
static char *
fw_read_img(struct cam_device *dev, int retry_count, int timeout, int quiet,
	    const char *fw_img_path, struct fw_vendor *vp, int *num_bytes)
{
	int fd;
	struct stat stbuf;
	char *buf;
	off_t img_size;
	int skip_bytes = 0;

	if ((fd = open(fw_img_path, O_RDONLY)) < 0) {
		warn("Could not open image file %s", fw_img_path);
		return (NULL);
	}
	if (fstat(fd, &stbuf) < 0) {
		warn("Could not stat image file %s", fw_img_path);
		goto bailout1;
	}
	if ((img_size = stbuf.st_size) == 0) {
		warnx("Zero length image file %s", fw_img_path);
		goto bailout1;
	}
	if ((buf = malloc(img_size)) == NULL) {
		warnx("Could not allocate buffer to read image file %s",
		    fw_img_path);
		goto bailout1;
	}
	/* Skip headers if applicable. */
	switch (vp->type) {
	case VENDOR_SEAGATE:
		if (read(fd, buf, 16) != 16) {
			warn("Could not read image file %s", fw_img_path);
			goto bailout;
		}
		if (lseek(fd, 0, SEEK_SET) == -1) {
			warn("Unable to lseek");
			goto bailout;
		}
		if ((strncmp(buf, "SEAGATE,SEAGATE ", 16) == 0) ||
		    (img_size % 512 == 80))
			skip_bytes = 80;
		break;
	case VENDOR_QUALSTAR:
		skip_bytes = img_size % 1030;
		break;
	case VENDOR_IBM: {
		if (vp->dev_type != T_SEQUENTIAL)
			break;
		if (fw_validate_ibm(dev, retry_count, timeout, fd, buf,
				    fw_img_path, quiet) != 0)
			goto bailout;
		break;
	}
	default:
		break;
	}
	if (skip_bytes != 0) {
		fprintf(stdout, "Skipping %d byte header.\n", skip_bytes);
		if (lseek(fd, skip_bytes, SEEK_SET) == -1) {
			warn("Could not lseek");
			goto bailout;
		}
		img_size -= skip_bytes;
	}
	/* Read image into a buffer. */
	if (read(fd, buf, img_size) != img_size) {
		warn("Could not read image file %s", fw_img_path);
		goto bailout;
	}
	*num_bytes = img_size;
	close(fd);
	return (buf);
bailout:
	free(buf);
bailout1:
	close(fd);
	*num_bytes = 0;
	return (NULL);
}

/*
 * Returns 0 for "success", where success means that the device has met the
 * requirement in the vendor structure for being ready or not ready when
 * firmware is downloaded.
 *
 * Returns 1 for a failure to be ready to accept a firmware download.
 * (e.g., a drive needs to be ready, but returns not ready)
 *
 * Returns -1 for any other failure.
 */
static int
fw_check_device_ready(struct cam_device *dev, camcontrol_devtype devtype,
		      struct fw_vendor *vp, int printerrors, int timeout)
{
	union ccb *ccb;
	int retval = 0;
	int16_t *ptr = NULL;
	size_t dxfer_len = 0;

	if ((ccb = cam_getccb(dev)) == NULL) {
		warnx("Could not allocate CCB");
		retval = -1;
		goto bailout;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(ccb);

	if (devtype != CC_DT_SCSI) {
		dxfer_len = sizeof(struct ata_params);

		ptr = (uint16_t *)malloc(dxfer_len);
		if (ptr == NULL) {
			warnx("can't malloc memory for identify");
			retval = -1;
			goto bailout;
		}
		bzero(ptr, dxfer_len);
	}

	switch (devtype) {
	case CC_DT_SCSI:
		scsi_test_unit_ready(&ccb->csio,
				     /*retries*/ 0,
				     /*cbfcnp*/ NULL,
				     /*tag_action*/ MSG_SIMPLE_Q_TAG,
		    		     /*sense_len*/ SSD_FULL_SIZE,
				     /*timeout*/ 5000);
		break;
	case CC_DT_ATA_BEHIND_SCSI:
	case CC_DT_ATA: {
		retval = build_ata_cmd(ccb,
			     /*retries*/ 1,
			     /*flags*/ CAM_DIR_IN,
			     /*tag_action*/ MSG_SIMPLE_Q_TAG,
			     /*protocol*/ AP_PROTO_PIO_IN,
			     /*ata_flags*/ AP_FLAG_BYT_BLOK_BYTES |
					   AP_FLAG_TLEN_SECT_CNT |
					   AP_FLAG_TDIR_FROM_DEV,
			     /*features*/ 0,
			     /*sector_count*/ (uint8_t) dxfer_len,
			     /*lba*/ 0,
			     /*command*/ ATA_ATA_IDENTIFY,
			     /*auxiliary*/ 0,
			     /*data_ptr*/ (uint8_t *)ptr,
			     /*dxfer_len*/ dxfer_len,
			     /*cdb_storage*/ NULL,
			     /*cdb_storage_len*/ 0,
			     /*sense_len*/ SSD_FULL_SIZE,
			     /*timeout*/ timeout ? timeout : 30 * 1000,
			     /*is48bit*/ 0,
			     /*devtype*/ devtype);
		if (retval != 0) {
			retval = -1;
			warnx("%s: build_ata_cmd() failed, likely "
			    "programmer error", __func__);
			goto bailout;
		}
		break;
	}
	default:
		warnx("Unknown disk type %d", devtype);
		retval = -1;
		goto bailout;
		break; /*NOTREACHED*/
	}

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	retval = cam_send_ccb(dev, ccb);
	if (retval != 0) {
		warn("error sending %s CCB", (devtype == CC_DT_SCSI) ?
		     "Test Unit Ready" : "Identify");
		retval = -1;
		goto bailout;
	}

	if (((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
	 && (vp->tur_status == FW_TUR_READY)) {
		warnx("Device is not ready");
		if (printerrors)
			cam_error_print(dev, ccb, CAM_ESF_ALL,
			    CAM_EPF_ALL, stderr);
		retval = 1;
		goto bailout;
	} else if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) 
		&& (vp->tur_status == FW_TUR_NOT_READY)) {
		warnx("Device cannot have media loaded when firmware is "
		    "downloaded");
		retval = 1;
		goto bailout;
	}
bailout:
	free(ptr);
	cam_freeccb(ccb);

	return (retval);
}

/* 
 * Download firmware stored in buf to cam_dev. If simulation mode
 * is enabled, only show what packet sizes would be sent to the 
 * device but do not sent any actual packets
 */
static int
fw_download_img(struct cam_device *cam_dev, struct fw_vendor *vp,
    char *buf, int img_size, int sim_mode, int printerrors, int quiet, 
    int retry_count, int timeout, const char *imgname,
    camcontrol_devtype devtype)
{
	struct scsi_write_buffer cdb;
	progress_t progress;
	int size = 0;
	union ccb *ccb = NULL;
	int pkt_count = 0;
	int max_pkt_size;
	u_int32_t pkt_size = 0;
	char *pkt_ptr = buf;
	u_int32_t offset;
	int last_pkt = 0;
	int retval = 0;

	/*
	 * Check to see whether the device is ready to accept a firmware
	 * download.
	 */
	retval = fw_check_device_ready(cam_dev, devtype, vp, printerrors,
				       timeout);
	if (retval != 0)
		goto bailout;

	if ((ccb = cam_getccb(cam_dev)) == NULL) {
		warnx("Could not allocate CCB");
		retval = 1;
		goto bailout;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(ccb);

	max_pkt_size = vp->max_pkt_size;
	if (max_pkt_size == 0)
		max_pkt_size = UNKNOWN_MAX_PKT_SIZE;

	pkt_size = max_pkt_size;
	progress_init(&progress, imgname, size = img_size);
	/* Download single fw packets. */
	do {
		if (img_size <= max_pkt_size) {
			last_pkt = 1;
			pkt_size = img_size;
		}
		progress_update(&progress, size - img_size);
		if (((sim_mode == 0) && (quiet == 0))
		 || ((sim_mode != 0) && (printerrors == 0)))
			progress_draw(&progress);
		bzero(&cdb, sizeof(cdb));
		switch (devtype) {
		case CC_DT_SCSI:
			cdb.opcode  = WRITE_BUFFER;
			cdb.control = 0;
			/* Parameter list length. */
			scsi_ulto3b(pkt_size, &cdb.length[0]);
			offset = vp->inc_cdb_offset ? (pkt_ptr - buf) : 0;
			scsi_ulto3b(offset, &cdb.offset[0]);
			cdb.byte2 = last_pkt ? vp->cdb_byte2_last :
					       vp->cdb_byte2;
			cdb.buffer_id = vp->inc_cdb_buffer_id ? pkt_count : 0;
			/* Zero out payload of ccb union after ccb header. */
			CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);
			/*
			 * Copy previously constructed cdb into ccb_scsiio
			 * struct.
			 */
			bcopy(&cdb, &ccb->csio.cdb_io.cdb_bytes[0],
			    sizeof(struct scsi_write_buffer));
			/* Fill rest of ccb_scsiio struct. */
			cam_fill_csio(&ccb->csio,		/* ccb_scsiio*/
			    retry_count,			/* retries*/
			    NULL,				/* cbfcnp*/
			    CAM_DIR_OUT | CAM_DEV_QFRZDIS,	/* flags*/
			    CAM_TAG_ACTION_NONE,		/* tag_action*/
			    (u_char *)pkt_ptr,			/* data_ptr*/
			    pkt_size,				/* dxfer_len*/
			    SSD_FULL_SIZE,			/* sense_len*/
			    sizeof(struct scsi_write_buffer),	/* cdb_len*/
			    timeout ? timeout : WB_TIMEOUT);	/* timeout*/
			break;
		case CC_DT_ATA:
		case CC_DT_ATA_BEHIND_SCSI: {
			uint32_t	off;

			off = (uint32_t)(pkt_ptr - buf);

			retval = build_ata_cmd(ccb,
			    /*retry_count*/ retry_count,
			    /*flags*/ CAM_DIR_OUT | CAM_DEV_QFRZDIS,
			    /*tag_action*/ CAM_TAG_ACTION_NONE,
			    /*protocol*/ AP_PROTO_PIO_OUT,
			    /*ata_flags*/ AP_FLAG_BYT_BLOK_BYTES |
					  AP_FLAG_TLEN_SECT_CNT |
					  AP_FLAG_TDIR_TO_DEV,
			    /*features*/ USE_OFFSETS_FEATURE,
			    /*sector_count*/ ATA_MAKE_SECTORS(pkt_size),
			    /*lba*/ ATA_MAKE_LBA(off, pkt_size),
			    /*command*/ ATA_DOWNLOAD_MICROCODE,
			    /*auxiliary*/ 0,
			    /*data_ptr*/ (uint8_t *)pkt_ptr,
			    /*dxfer_len*/ pkt_size,
			    /*cdb_storage*/ NULL,
			    /*cdb_storage_len*/ 0,
			    /*sense_len*/ SSD_FULL_SIZE,
			    /*timeout*/ timeout ? timeout : WB_TIMEOUT,
			    /*is48bit*/ 0,
			    /*devtype*/ devtype);

			if (retval != 0) {
				warnx("%s: build_ata_cmd() failed, likely "
				    "programmer error", __func__);
				goto bailout;
			}
			break;
		}
		default:
			warnx("Unknown device type %d", devtype);
			retval = 1;
			goto bailout;
			break; /*NOTREACHED*/
		}
		if (!sim_mode) {
			/* Execute the command. */
			if (cam_send_ccb(cam_dev, ccb) < 0 ||
			    (ccb->ccb_h.status & CAM_STATUS_MASK) !=
			    CAM_REQ_CMP) {
				warnx("Error writing image to device");
				if (printerrors)
					cam_error_print(cam_dev, ccb,
					    CAM_ESF_ALL, CAM_EPF_ALL, stderr);
				retval = 1;
				goto bailout;
			}
		} else if (printerrors) {
			cam_error_print(cam_dev, ccb, CAM_ESF_COMMAND, 0,
			    stdout);
		}

		/* Prepare next round. */
		pkt_count++;
		pkt_ptr += pkt_size;
		img_size -= pkt_size;
	} while(!last_pkt);
bailout:
	if (quiet == 0)
		progress_complete(&progress, size - img_size);
	cam_freeccb(ccb);
	return (retval);
}

int
fwdownload(struct cam_device *device, int argc, char **argv,
    char *combinedopt, int printerrors, int task_attr, int retry_count,
    int timeout)
{
	union ccb *ccb = NULL;
	struct fw_vendor *vp;
	char *fw_img_path = NULL;
	struct ata_params *ident_buf = NULL;
	camcontrol_devtype devtype;
	char *buf = NULL;
	int img_size;
	int c;
	int sim_mode = 0;
	int confirmed = 0;
	int quiet = 0;
	int retval = 0;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'f':
			fw_img_path = optarg;
			break;
		case 'q':
			quiet = 1;
			break;
		case 's':
			sim_mode = 1;
			break;
		case 'y':
			confirmed = 1;
			break;
		default:
			break;
		}
	}

	if (fw_img_path == NULL)
		errx(1, "you must specify a firmware image file using -f "
		     "option");

	retval = get_device_type(device, retry_count, timeout, printerrors,
				 &devtype);
	if (retval != 0)
		errx(1, "Unable to determine device type");

	if ((devtype == CC_DT_ATA)
	 || (devtype == CC_DT_ATA_BEHIND_SCSI)) {
		ccb = cam_getccb(device);
		if (ccb == NULL) {
			warnx("couldn't allocate CCB");
			retval = 1;
			goto bailout;
		}

		if (ata_do_identify(device, retry_count, timeout, ccb,
		    		    &ident_buf) != 0) {
			retval = 1;
			goto bailout;
		}
	} else if (devtype != CC_DT_SCSI)
		errx(1, "Unsupported device type %d", devtype);

	vp = fw_get_vendor(device, ident_buf);
	/*
	 * Bail out if we have an unknown vendor and this isn't an ATA
	 * disk.  For a SCSI disk, we have no chance of working properly
	 * with the default values in the VENDOR_UNKNOWN case.  For an ATA
	 * disk connected via an ATA transport, we may work for drives that
	 * support the ATA_DOWNLOAD_MICROCODE command.
	 */
	if (((vp == NULL)
	  || (vp->type == VENDOR_UNKNOWN))
	 && (devtype == CC_DT_SCSI))
		errx(1, "Unsupported device");

	retval = fw_get_timeout(device, vp, task_attr, retry_count, timeout);
	if (retval != 0) {
		warnx("Unable to get a firmware download timeout value");
		goto bailout;
	}

	buf = fw_read_img(device, retry_count, timeout, quiet, fw_img_path,
	    vp, &img_size);
	if (buf == NULL) {
		retval = 1;
		goto bailout;
	}

	if (!confirmed) {
		fprintf(stdout, "You are about to download firmware image (%s)"
		    " into the following device:\n",
		    fw_img_path);
		if (devtype == CC_DT_SCSI) {
			if (scsidoinquiry(device, argc, argv, combinedopt,
					  MSG_SIMPLE_Q_TAG, 0, 5000) != 0) {
				warnx("Error sending inquiry");
				retval = 1;
				goto bailout;
			}
		} else {
			printf("%s%d: ", device->device_name,
			    device->dev_unit_num);
			ata_print_ident(ident_buf);
			camxferrate(device);
			free(ident_buf);
		}
		fprintf(stdout, "Using a timeout of %u ms, which is %s.\n",
			vp->timeout_ms,
			fw_timeout_desc_table[vp->timeout_type].timeout_desc);
		fprintf(stdout, "\nIt may damage your drive. ");
		if (!get_confirmation()) {
			retval = 1;
			goto bailout;
		}
	}
	if ((sim_mode != 0) && (quiet == 0))
		fprintf(stdout, "Running in simulation mode\n");

	if (fw_download_img(device, vp, buf, img_size, sim_mode, printerrors,
	    quiet, retry_count, vp->timeout_ms, fw_img_path, devtype) != 0) {
		fprintf(stderr, "Firmware download failed\n");
		retval = 1;
		goto bailout;
	} else if (quiet == 0)
		fprintf(stdout, "Firmware download successful\n");

bailout:
	cam_freeccb(ccb);
	free(buf);
	return (retval);
}

