/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998, 1999, 2002 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
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

#include <sys/types.h>
#include <sys/param.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include <cam/cam.h>
#include <cam/scsi/scsi_all.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_pass.h>
#include "camlib.h"


static const char *nonrewind_devs[] = {
	"sa"
};

char cam_errbuf[CAM_ERRBUF_SIZE];

static struct cam_device *cam_real_open_device(const char *path, int flags,
					       struct cam_device *device,
					       const char *given_path,
					       const char *given_dev_name,
					       int given_unit_number);
static struct cam_device *cam_lookup_pass(const char *dev_name, int unit,
					  int flags, const char *given_path,
					  struct cam_device *device);

/*
 * Send a ccb to a passthrough device.
 */
int
cam_send_ccb(struct cam_device *device, union ccb *ccb)
{
	return(ioctl(device->fd, CAMIOCOMMAND, ccb));
}

/*
 * Malloc a CCB, zero out the header and set its path, target and lun ids.
 */
union ccb *
cam_getccb(struct cam_device *dev)
{
	union ccb *ccb;

	ccb = (union ccb *)malloc(sizeof(union ccb));
	if (ccb != NULL) {
		bzero(&ccb->ccb_h, sizeof(struct ccb_hdr));
		ccb->ccb_h.path_id = dev->path_id;
		ccb->ccb_h.target_id = dev->target_id;
		ccb->ccb_h.target_lun = dev->target_lun;
	}

	return(ccb);
}

/*
 * Free a CCB.
 */
void
cam_freeccb(union ccb *ccb)
{
	free(ccb);
}

/*
 * Take a device name or path passed in by the user, and attempt to figure
 * out the device name and unit number.  Some possible device name formats are:
 * /dev/foo0
 * foo0
 * nfoo0
 *
 * Some peripheral drivers create separate device nodes with 'n' prefix for
 * non-rewind operations.  Currently only sa(4) tape driver has this feature.
 * We extract pure peripheral name as device name for this special case.
 *
 * Input parameters:  device name/path, length of devname string
 * Output:            device name, unit number
 * Return values:     returns 0 for success, -1 for failure
 */
int
cam_get_device(const char *path, char *dev_name, int devnamelen, int *unit)
{
	char *tmpstr, *tmpstr2;
	char *newpath;
	int unit_offset;
	int i;

	if (path == NULL) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: device pathname was NULL", __func__);
		return(-1);
	}

	/*
	 * We can be rather destructive to the path string.  Make a copy of
	 * it so we don't hose the user's string.
	 */
	newpath = (char *)strdup(path);
	if (newpath == NULL)
		return (-1);

	tmpstr = newpath;

	/*
	 * Check to see whether we have an absolute pathname.
	 */
	if (*tmpstr == '/') {
		tmpstr2 = tmpstr;
		tmpstr = strrchr(tmpstr2, '/');
		/* We know that tmpstr2 contains a '/', so strrchr can't fail */
		assert(tmpstr != NULL && *tmpstr != '\0');
		tmpstr++;
	}

	if (*tmpstr == '\0') {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: no text after slash", __func__);
		free(newpath);
		return(-1);
	}

	/*
	 * Check to see whether the user has given us a nonrewound tape
	 * device.
	 */
	if (*tmpstr == 'n' || *tmpstr == 'e') {
		for (i = 0; i < sizeof(nonrewind_devs)/sizeof(char *); i++) {
			int len = strlen(nonrewind_devs[i]);
			if (strncmp(tmpstr + 1, nonrewind_devs[i], len) == 0) {
				if (isdigit(tmpstr[len + 1])) {
					tmpstr++;
					break;
				}
			}
		}
	}

	/*
	 * We should now have just a device name and unit number.
	 * That means that there must be at least 2 characters.
	 * If we only have 1, we don't have a valid device name.
	 */
	if (strlen(tmpstr) < 2) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: must have both device name and unit number",
		    __func__);
		free(newpath);
		return(-1);
	}

	/*
	 * If the first character of the string is a digit, then the user
	 * has probably given us all numbers.  Point out the error.
	 */
	if (isdigit(*tmpstr)) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: device name cannot begin with a number",
		    __func__);
		free(newpath);
		return(-1);
	}

	/*
	 * At this point, if the last character of the string isn't a
	 * number, we know the user either didn't give us a device number,
	 * or he gave us a device name/number format we don't recognize.
	 */
	if (!isdigit(tmpstr[strlen(tmpstr) - 1])) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: unable to find device unit number", __func__);
		free(newpath);
		return(-1);
	}

	/*
	 * Attempt to figure out where the device name ends and the unit
	 * number begins.  As long as unit_offset is at least 1 less than
	 * the length of the string, we can still potentially have a device
	 * name at the front of the string.  When we get to something that
	 * isn't a digit, we've hit the device name.  Because of the check
	 * above, we know that this cannot happen when unit_offset == 1.
	 * Therefore it is okay to decrement unit_offset -- it won't cause
	 * us to go past the end of the character array.
	 */
	for (unit_offset = 1;
	    (unit_offset < (strlen(tmpstr)))
	    && (isdigit(tmpstr[strlen(tmpstr) - unit_offset])); unit_offset++);

	unit_offset--;

	/*
	 * Grab the unit number.
	 */
	*unit = atoi(&tmpstr[strlen(tmpstr) - unit_offset]);

	/*
	 * Put a null in place of the first number of the unit number so
	 * that all we have left is the device name.
	 */
	tmpstr[strlen(tmpstr) - unit_offset] = '\0';

	strlcpy(dev_name, tmpstr, devnamelen);

	/* Clean up allocated memory */
	free(newpath);

	return(0);

}

/*
 * Backwards compatible wrapper for the real open routine.  This translates
 * a pathname into a device name and unit number for use with the real open
 * routine.
 */
struct cam_device *
cam_open_device(const char *path, int flags)
{
	int unit;
	char dev_name[DEV_IDLEN + 1];

	/*
	 * cam_get_device() has already put an error message in cam_errbuf,
	 * so we don't need to.
	 */
	if (cam_get_device(path, dev_name, sizeof(dev_name), &unit) == -1)
		return(NULL);

	return(cam_lookup_pass(dev_name, unit, flags, path, NULL));
}

/*
 * Open the passthrough device for a given bus, target and lun, if the
 * passthrough device exists.
 */
struct cam_device *
cam_open_btl(path_id_t path_id, target_id_t target_id, lun_id_t target_lun,
	     int flags, struct cam_device *device)
{
	union ccb ccb;
	struct periph_match_pattern *match_pat;
	int fd, bufsize;

	if ((fd = open(XPT_DEVICE, O_RDWR)) < 0) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: couldn't open %s\n%s: %s", __func__, XPT_DEVICE,
		    __func__, strerror(errno));
		return(NULL);
	}

	bzero(&ccb, sizeof(union ccb));
	ccb.ccb_h.func_code = XPT_DEV_MATCH;
	ccb.ccb_h.path_id = CAM_XPT_PATH_ID;
	ccb.ccb_h.target_id = CAM_TARGET_WILDCARD;
	ccb.ccb_h.target_lun = CAM_LUN_WILDCARD;

	/* Setup the result buffer */
	bufsize = sizeof(struct dev_match_result);
	ccb.cdm.match_buf_len = bufsize;
	ccb.cdm.matches = (struct dev_match_result *)malloc(bufsize);
	if (ccb.cdm.matches == NULL) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: couldn't malloc match buffer", __func__);
		close(fd);
		return(NULL);
	}
	ccb.cdm.num_matches = 0;

	/* Setup the pattern buffer */
	ccb.cdm.num_patterns = 1;
	ccb.cdm.pattern_buf_len = sizeof(struct dev_match_pattern);
	ccb.cdm.patterns = (struct dev_match_pattern *)malloc(
		sizeof(struct dev_match_pattern));
	if (ccb.cdm.patterns == NULL) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: couldn't malloc pattern buffer", __func__);
		free(ccb.cdm.matches);
		ccb.cdm.matches = NULL;
		close(fd);
		return(NULL);
	}
	ccb.cdm.patterns[0].type = DEV_MATCH_PERIPH;
	match_pat = &ccb.cdm.patterns[0].pattern.periph_pattern;

	/*
	 * We're looking for the passthrough device associated with this
	 * particular bus/target/lun.
	 */
	sprintf(match_pat->periph_name, "pass");
	match_pat->path_id = path_id;
	match_pat->target_id = target_id;
	match_pat->target_lun = target_lun;
	/* Now set the flags to indicate what we're looking for. */
	match_pat->flags = PERIPH_MATCH_PATH | PERIPH_MATCH_TARGET |
			   PERIPH_MATCH_LUN | PERIPH_MATCH_NAME;

	if (ioctl(fd, CAMIOCOMMAND, &ccb) == -1) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: CAMIOCOMMAND ioctl failed\n"
		    "%s: %s", __func__, __func__, strerror(errno));
		goto btl_bailout;
	}

	/*
	 * Check for an outright error.
	 */
	if ((ccb.ccb_h.status != CAM_REQ_CMP)
	 || ((ccb.cdm.status != CAM_DEV_MATCH_LAST)
	   && (ccb.cdm.status != CAM_DEV_MATCH_MORE))) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: CAM error %#x, CDM error %d "
		    "returned from XPT_DEV_MATCH ccb", __func__,
		    ccb.ccb_h.status, ccb.cdm.status);
		goto btl_bailout;
	}

	if (ccb.cdm.status == CAM_DEV_MATCH_MORE) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: CDM reported more than one"
		    " passthrough device at %d:%d:%jx!!\n",
		    __func__, path_id, target_id, (uintmax_t)target_lun);
		goto btl_bailout;
	}

	if (ccb.cdm.num_matches == 0) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: no passthrough device found at"
		    " %d:%d:%jx", __func__, path_id, target_id,
		    (uintmax_t)target_lun);
		goto btl_bailout;
	}

	switch(ccb.cdm.matches[0].type) {
	case DEV_MATCH_PERIPH: {
		int pass_unit;
		char dev_path[256];
		struct periph_match_result *periph_result;

		periph_result = &ccb.cdm.matches[0].result.periph_result;
		pass_unit = periph_result->unit_number;
		free(ccb.cdm.matches);
		ccb.cdm.matches = NULL;
		free(ccb.cdm.patterns);
		ccb.cdm.patterns = NULL;
		close(fd);
		sprintf(dev_path, "/dev/pass%d", pass_unit);
		return(cam_real_open_device(dev_path, flags, device, NULL,
					    NULL, 0));
		break; /* NOTREACHED */
	}
	default:
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: asked for a peripheral match, but"
		    " got a bus or device match", __func__);
		goto btl_bailout;
		break; /* NOTREACHED */
	}

btl_bailout:
	free(ccb.cdm.matches);
	ccb.cdm.matches = NULL;
	free(ccb.cdm.patterns);
	ccb.cdm.patterns = NULL;
	close(fd);
	return(NULL);
}

struct cam_device *
cam_open_spec_device(const char *dev_name, int unit, int flags,
		     struct cam_device *device)
{
	return(cam_lookup_pass(dev_name, unit, flags, NULL, device));
}

struct cam_device *
cam_open_pass(const char *path, int flags, struct cam_device *device)
{
	return(cam_real_open_device(path, flags, device, path, NULL, 0));
}

static struct cam_device *
cam_lookup_pass(const char *dev_name, int unit, int flags,
		const char *given_path, struct cam_device *device)
{
	int fd;
	union ccb ccb;
	char dev_path[256];

	/*
	 * The flags argument above only applies to the actual passthrough
	 * device open, not our open of the given device to find the
	 * passthrough device.
	 */
	if ((fd = open(XPT_DEVICE, O_RDWR)) < 0) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: couldn't open %s\n%s: %s", __func__, XPT_DEVICE,
		    __func__, strerror(errno));
		return(NULL);
	}

	/* This isn't strictly necessary for the GETPASSTHRU ioctl. */
	ccb.ccb_h.func_code = XPT_GDEVLIST;

	/* These two are necessary for the GETPASSTHRU ioctl to work. */
	strlcpy(ccb.cgdl.periph_name, dev_name, sizeof(ccb.cgdl.periph_name));
	ccb.cgdl.unit_number = unit;

	/*
	 * Attempt to get the passthrough device.  This ioctl will fail if
	 * the device name is null, if the device doesn't exist, or if the
	 * passthrough driver isn't in the kernel.
	 */
	if (ioctl(fd, CAMGETPASSTHRU, &ccb) == -1) {
		char tmpstr[256];

		/*
		 * If we get ENOENT from the transport layer version of
		 * the CAMGETPASSTHRU ioctl, it means one of two things:
		 * either the device name/unit number passed in doesn't
		 * exist, or the passthrough driver isn't in the kernel.
		 */
		if (errno == ENOENT) {
			snprintf(tmpstr, sizeof(tmpstr),
				 "\n%s: either the pass driver isn't in "
				 "your kernel\n%s: or %s%d doesn't exist",
				 __func__, __func__, dev_name, unit);
		}
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: CAMGETPASSTHRU ioctl failed\n"
		    "%s: %s%s", __func__, __func__, strerror(errno),
		    (errno == ENOENT) ? tmpstr : "");

		close(fd);
		return(NULL);
	}

	close(fd);

	/*
	 * If the ioctl returned the right status, but we got an error back
	 * in the ccb, that means that the kernel found the device the user
	 * passed in, but was unable to find the passthrough device for
	 * the device the user gave us.
	 */
	if (ccb.cgdl.status == CAM_GDEVLIST_ERROR) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: device %s%d does not exist!",
		    __func__, dev_name, unit);
		return(NULL);
	}

	sprintf(dev_path, "/dev/%s%d", ccb.cgdl.periph_name,
		ccb.cgdl.unit_number);

	return(cam_real_open_device(dev_path, flags, device, NULL,
				    dev_name, unit));
}

/*
 * Open a given device.  The path argument isn't strictly necessary, but it
 * is copied into the cam_device structure as a convenience to the user.
 */
static struct cam_device *
cam_real_open_device(const char *path, int flags, struct cam_device *device,
		     const char *given_path, const char *given_dev_name,
		     int given_unit_number)
{
	union ccb ccb;
	int fd = -1, malloced_device = 0;

	/*
	 * See if the user wants us to malloc a device for him.
	 */
	if (device == NULL) {
		if ((device = (struct cam_device *)malloc(
		     sizeof(struct cam_device))) == NULL) {
			snprintf(cam_errbuf, nitems(cam_errbuf),
			    "%s: device structure malloc"
			    " failed\n%s: %s", __func__, __func__,
			    strerror(errno));
			return(NULL);
		}
		device->fd = -1;
		malloced_device = 1;
	}

	/*
	 * If the user passed in a path, save it for him.
	 */
	if (given_path != NULL)
		strlcpy(device->device_path, given_path,
			sizeof(device->device_path));
	else
		device->device_path[0] = '\0';

	/*
	 * If the user passed in a device name and unit number pair, save
	 * those as well.
	 */
	if (given_dev_name != NULL)
		strlcpy(device->given_dev_name, given_dev_name,
			sizeof(device->given_dev_name));
	else
		device->given_dev_name[0] = '\0';
	device->given_unit_number = given_unit_number;

	if ((fd = open(path, flags)) < 0) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: couldn't open passthrough device %s\n"
		    "%s: %s", __func__, path, __func__,
		    strerror(errno));
		goto crod_bailout;
	}

	device->fd = fd;

	bzero(&ccb, sizeof(union ccb));

	/*
	 * Unlike the transport layer version of the GETPASSTHRU ioctl,
	 * we don't have to set any fields.
	 */
	ccb.ccb_h.func_code = XPT_GDEVLIST;

	/*
	 * We're only doing this to get some information on the device in
	 * question.  Otherwise, we'd have to pass in yet another
	 * parameter: the passthrough driver unit number.
	 */
	if (ioctl(fd, CAMGETPASSTHRU, &ccb) == -1) {
		/*
		 * At this point we know the passthrough device must exist
		 * because we just opened it above.  The only way this
		 * ioctl can fail is if the ccb size is wrong.
		 */
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: CAMGETPASSTHRU ioctl failed\n"
		    "%s: %s", __func__, __func__, strerror(errno));
		goto crod_bailout;
	}

	/*
	 * If the ioctl returned the right status, but we got an error back
	 * in the ccb, that means that the kernel found the device the user
	 * passed in, but was unable to find the passthrough device for
	 * the device the user gave us.
	 */
	if (ccb.cgdl.status == CAM_GDEVLIST_ERROR) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: passthrough device does not exist!", __func__);
		goto crod_bailout;
	}

	device->dev_unit_num = ccb.cgdl.unit_number;
	strlcpy(device->device_name, ccb.cgdl.periph_name,
		sizeof(device->device_name));
	device->path_id = ccb.ccb_h.path_id;
	device->target_id = ccb.ccb_h.target_id;
	device->target_lun = ccb.ccb_h.target_lun;

	ccb.ccb_h.func_code = XPT_PATH_INQ;
	if (ioctl(fd, CAMIOCOMMAND, &ccb) == -1) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: Path Inquiry CCB failed\n"
		    "%s: %s", __func__, __func__, strerror(errno));
		goto crod_bailout;
	}
	strlcpy(device->sim_name, ccb.cpi.dev_name, sizeof(device->sim_name));
	device->sim_unit_number = ccb.cpi.unit_number;
	device->bus_id = ccb.cpi.bus_id;

	/*
	 * It doesn't really matter what is in the payload for a getdev
	 * CCB, the kernel doesn't look at it.
	 */
	ccb.ccb_h.func_code = XPT_GDEV_TYPE;
	if (ioctl(fd, CAMIOCOMMAND, &ccb) == -1) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: Get Device Type CCB failed\n"
		    "%s: %s", __func__, __func__, strerror(errno));
		goto crod_bailout;
	}
	device->pd_type = SID_TYPE(&ccb.cgd.inq_data);
	bcopy(&ccb.cgd.inq_data, &device->inq_data,
	      sizeof(struct scsi_inquiry_data));
	device->serial_num_len = ccb.cgd.serial_num_len;
	bcopy(&ccb.cgd.serial_num, &device->serial_num, device->serial_num_len);

	/*
	 * Zero the payload, the kernel does look at the flags.
	 */
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb.cts);

	/*
	 * Get transfer settings for this device.
	 */
	ccb.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;

	ccb.cts.type = CTS_TYPE_CURRENT_SETTINGS;

	if (ioctl(fd, CAMIOCOMMAND, &ccb) == -1) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: Get Transfer Settings CCB failed\n"
		    "%s: %s", __func__, __func__, strerror(errno));
		goto crod_bailout;
	}
	if (ccb.cts.transport == XPORT_SPI) {
		struct ccb_trans_settings_spi *spi =
		    &ccb.cts.xport_specific.spi;
		device->sync_period = spi->sync_period;
		device->sync_offset = spi->sync_offset;
		device->bus_width = spi->bus_width;
	} else {
		device->sync_period = 0;
		device->sync_offset = 0;
		device->bus_width = 0;
	}

	return(device);

crod_bailout:

	if (fd >= 0)
		close(fd);

	if (malloced_device)
		free(device);

	return(NULL);
}

void
cam_close_device(struct cam_device *dev)
{
	if (dev == NULL)
		return;

	cam_close_spec_device(dev);

	free(dev);
}

void
cam_close_spec_device(struct cam_device *dev)
{
	if (dev == NULL)
		return;

	if (dev->fd >= 0) {
		close(dev->fd);
		dev->fd = -1;
	}
}

char *
cam_path_string(struct cam_device *dev, char *str, int len)
{
	if (dev == NULL) {
		snprintf(str, len, "No path");
		return(str);
	}

	snprintf(str, len, "(%s%d:%s%d:%d:%d:%jx): ",
		 (dev->device_name[0] != '\0') ? dev->device_name : "pass",
		 dev->dev_unit_num,
		 (dev->sim_name[0] != '\0') ? dev->sim_name : "unknown",
		 dev->sim_unit_number,
		 dev->bus_id,
		 dev->target_id,
		 (uintmax_t)dev->target_lun);

	return(str);
}

/*
 * Malloc/duplicate a CAM device structure.
 */
struct cam_device *
cam_device_dup(struct cam_device *device)
{
	struct cam_device *newdev;

	if (device == NULL) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: device is NULL", __func__);
		return (NULL);
	}

	newdev = malloc(sizeof(struct cam_device));
	if (newdev == NULL) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: couldn't malloc CAM device structure", __func__);
		return (NULL);
	}

	bcopy(device, newdev, sizeof(struct cam_device));

	return(newdev);
}

/*
 * Copy a CAM device structure.
 */
void
cam_device_copy(struct cam_device *src, struct cam_device *dst)
{

	if (src == NULL) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: source device struct was NULL", __func__);
		return;
	}

	if (dst == NULL) {
		snprintf(cam_errbuf, nitems(cam_errbuf),
		    "%s: destination device struct was NULL", __func__);
		return;
	}

	bcopy(src, dst, sizeof(struct cam_device));

}
