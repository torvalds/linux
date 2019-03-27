/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008, 2009 Yahoo!, Inc.
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <libutil.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <cam/scsi/scsi_all.h>
#include "mfiutil.h"

MFI_TABLE(top, drive);

/*
 * Print the name of a drive either by drive number as %2u or by enclosure:slot
 * as Exx:Sxx (or both).  Use default unless command line options override it
 * and the command allows this (which we usually do unless we already print
 * both).  We prefer pinfo if given, otherwise try to look it up by device_id.
 */
const char *
mfi_drive_name(struct mfi_pd_info *pinfo, uint16_t device_id, uint32_t def)
{
	struct mfi_pd_info info;
	static char buf[16];
	char *p;
	int error, fd, len;

	if ((def & MFI_DNAME_HONOR_OPTS) != 0 &&
	    (mfi_opts & (MFI_DNAME_ES|MFI_DNAME_DEVICE_ID)) != 0)
		def = mfi_opts & (MFI_DNAME_ES|MFI_DNAME_DEVICE_ID);

	buf[0] = '\0';
	if (pinfo == NULL && def & MFI_DNAME_ES) {
		/* Fallback in case of error, just ignore flags. */
		if (device_id == 0xffff)
			snprintf(buf, sizeof(buf), "MISSING");
		else
			snprintf(buf, sizeof(buf), "%2u", device_id);

		fd = mfi_open(mfi_unit, O_RDWR);
		if (fd < 0) {
			warn("mfi_open");
			return (buf);
		}

		/* Get the info for this drive. */
		if (mfi_pd_get_info(fd, device_id, &info, NULL) < 0) {
			warn("Failed to fetch info for drive %2u", device_id);
			close(fd);
			return (buf);
		}

		close(fd);
		pinfo = &info;
	}

	p = buf;
	len = sizeof(buf);
	if (def & MFI_DNAME_DEVICE_ID) {
		if (device_id == 0xffff)
			error = snprintf(p, len, "MISSING");
		else
			error = snprintf(p, len, "%2u", device_id);
		if (error >= 0) {
			p += error;
			len -= error;
		}
	}
	if ((def & (MFI_DNAME_ES|MFI_DNAME_DEVICE_ID)) ==
	    (MFI_DNAME_ES|MFI_DNAME_DEVICE_ID) && len >= 2) {
		*p++ = ' ';
		len--;
		*p = '\0';
		len--;
	}
	if (def & MFI_DNAME_ES) {
		if (pinfo->encl_device_id == 0xffff)
			error = snprintf(p, len, "S%u",
			    pinfo->slot_number);
		else if (pinfo->encl_device_id == pinfo->ref.v.device_id)
			error = snprintf(p, len, "E%u",
			    pinfo->encl_index);
		else
			error = snprintf(p, len, "E%u:S%u",
			    pinfo->encl_index, pinfo->slot_number);
		if (error >= 0) {
			p += error;
			len -= error;
		}
	}

	return (buf);
}

const char *
mfi_pdstate(enum mfi_pd_state state)
{
	static char buf[16];

	switch (state) {
	case MFI_PD_STATE_UNCONFIGURED_GOOD:
		return ("UNCONFIGURED GOOD");
	case MFI_PD_STATE_UNCONFIGURED_BAD:
		return ("UNCONFIGURED BAD");
	case MFI_PD_STATE_HOT_SPARE:
		return ("HOT SPARE");
	case MFI_PD_STATE_OFFLINE:
		return ("OFFLINE");
	case MFI_PD_STATE_FAILED:
		return ("FAILED");
	case MFI_PD_STATE_REBUILD:
		return ("REBUILD");
	case MFI_PD_STATE_ONLINE:
		return ("ONLINE");
	case MFI_PD_STATE_COPYBACK:
		return ("COPYBACK");
	case MFI_PD_STATE_SYSTEM:
		return ("JBOD");
	default:
		sprintf(buf, "PSTATE 0x%04x", state);
		return (buf);
	}
}

int
mfi_lookup_drive(int fd, char *drive, uint16_t *device_id)
{
	struct mfi_pd_list *list;
	long val;
	int error;
	u_int i;
	char *cp;
	uint8_t encl, slot;

	/* Look for a raw device id first. */
	val = strtol(drive, &cp, 0);
	if (*cp == '\0') {
		if (val < 0 || val >= 0xffff)
			goto bad;
		*device_id = val;
		return (0);
	}

	/* Support for MegaCli style [Exx]:Syy notation. */
	if (toupper(drive[0]) == 'E' || toupper(drive[0]) == 'S') {
		if (drive[1] == '\0')
			goto bad;
		cp = drive;
		if (toupper(drive[0]) == 'E') {
			cp++;			/* Eat 'E' */
			val = strtol(cp, &cp, 0);
			if (val < 0 || val > 0xff || *cp != ':')
				goto bad;
			encl = val;
			cp++;			/* Eat ':' */
			if (toupper(*cp) != 'S')
				goto bad;
		} else
			encl = 0xff;
		cp++;				/* Eat 'S' */
		if (*cp == '\0')
			goto bad;
		val = strtol(cp, &cp, 0);
		if (val < 0 || val > 0xff || *cp != '\0')
			goto bad;
		slot = val;

		if (mfi_pd_get_list(fd, &list, NULL) < 0) {
			error = errno;
			warn("Failed to fetch drive list");
			return (error);
		}

		for (i = 0; i < list->count; i++) {
			if (list->addr[i].scsi_dev_type != 0)
				continue;

			if (((encl == 0xff &&
			    list->addr[i].encl_device_id == 0xffff) ||
			    list->addr[i].encl_index == encl) &&
			    list->addr[i].slot_number == slot) {
				*device_id = list->addr[i].device_id;
				free(list);
				return (0);
			}
		}
		free(list);
		warnx("Unknown drive %s", drive);
		return (EINVAL);
	}

bad:
	warnx("Invalid drive number %s", drive);
	return (EINVAL);
}

static void
mbox_store_device_id(uint8_t *mbox, uint16_t device_id)
{

	mbox[0] = device_id & 0xff;
	mbox[1] = device_id >> 8;
}

void
mbox_store_pdref(uint8_t *mbox, union mfi_pd_ref *ref)
{

	mbox[0] = ref->v.device_id & 0xff;
	mbox[1] = ref->v.device_id >> 8;
	mbox[2] = ref->v.seq_num & 0xff;
	mbox[3] = ref->v.seq_num >> 8;
}

int
mfi_pd_get_list(int fd, struct mfi_pd_list **listp, uint8_t *statusp)
{
	struct mfi_pd_list *list;
	uint32_t list_size;

	/*
	 * Keep fetching the list in a loop until we have a large enough
	 * buffer to hold the entire list.
	 */
	list = NULL;
	list_size = 1024;
fetch:
	list = reallocf(list, list_size);
	if (list == NULL)
		return (-1);
	if (mfi_dcmd_command(fd, MFI_DCMD_PD_GET_LIST, list, list_size, NULL,
	    0, statusp) < 0) {
		free(list);
		return (-1);
	}

	if (list->size > list_size) {
		list_size = list->size;
		goto fetch;
	}

	*listp = list;
	return (0);
}

int
mfi_pd_get_info(int fd, uint16_t device_id, struct mfi_pd_info *info,
    uint8_t *statusp)
{
	uint8_t mbox[2];

	mbox_store_device_id(&mbox[0], device_id);
	return (mfi_dcmd_command(fd, MFI_DCMD_PD_GET_INFO, info,
	    sizeof(struct mfi_pd_info), mbox, 2, statusp));
}

static void
cam_strvis(char *dst, const char *src, int srclen, int dstlen)
{

	/* Trim leading/trailing spaces, nulls. */
	while (srclen > 0 && src[0] == ' ')
		src++, srclen--;
	while (srclen > 0
	    && (src[srclen-1] == ' ' || src[srclen-1] == '\0'))
		srclen--;

	while (srclen > 0 && dstlen > 1) {
		char *cur_pos = dst;

		if (*src < 0x20) {
			/* SCSI-II Specifies that these should never occur. */
			/* non-printable character */
			if (dstlen > 4) {
				*cur_pos++ = '\\';
				*cur_pos++ = ((*src & 0300) >> 6) + '0';
				*cur_pos++ = ((*src & 0070) >> 3) + '0';
				*cur_pos++ = ((*src & 0007) >> 0) + '0';
			} else {
				*cur_pos++ = '?';
			}
		} else {
			/* normal character */
			*cur_pos++ = *src;
		}
		src++;
		srclen--;
		dstlen -= cur_pos - dst;
		dst = cur_pos;
	}
	*dst = '\0';
}

/* Borrowed heavily from scsi_all.c:scsi_print_inquiry(). */
const char *
mfi_pd_inq_string(struct mfi_pd_info *info)
{
	struct scsi_inquiry_data iqd, *inq_data = &iqd;
	char vendor[16], product[48], revision[16], rstr[12], serial[SID_VENDOR_SPECIFIC_0_SIZE];
	static char inq_string[64];

	memcpy(inq_data, info->inquiry_data,
	    (sizeof (iqd) <  sizeof (info->inquiry_data))?
	    sizeof (iqd) : sizeof (info->inquiry_data));
	if (SID_QUAL_IS_VENDOR_UNIQUE(inq_data))
		return (NULL);
	if (SID_TYPE(inq_data) != T_DIRECT)
		return (NULL);
	if (SID_QUAL(inq_data) != SID_QUAL_LU_CONNECTED)
		return (NULL);

	cam_strvis(vendor, inq_data->vendor, sizeof(inq_data->vendor),
	    sizeof(vendor));
	cam_strvis(product, inq_data->product, sizeof(inq_data->product),
	    sizeof(product));
	cam_strvis(revision, inq_data->revision, sizeof(inq_data->revision),
	    sizeof(revision));
	cam_strvis(serial, (char *)inq_data->vendor_specific0, sizeof(inq_data->vendor_specific0),
	    sizeof(serial));

	/* Hack for SATA disks, no idea how to tell speed. */
	if (strcmp(vendor, "ATA") == 0) {
		snprintf(inq_string, sizeof(inq_string), "<%s %s serial=%s> SATA",
		    product, revision, serial);
		return (inq_string);
	}

	switch (SID_ANSI_REV(inq_data)) {
	case SCSI_REV_CCS:
		strcpy(rstr, "SCSI-CCS");
		break;
	case 5:
		strcpy(rstr, "SAS");
		break;
	default:
		snprintf(rstr, sizeof (rstr), "SCSI-%d",
		    SID_ANSI_REV(inq_data));
		break;
	}
	snprintf(inq_string, sizeof(inq_string), "<%s %s %s serial=%s> %s", vendor,
	    product, revision, serial, rstr);
	return (inq_string);
}

/* Helper function to set a drive to a given state. */
static int
drive_set_state(char *drive, uint16_t new_state)
{
	struct mfi_pd_info info;
	uint16_t device_id;
	uint8_t mbox[6];
	int error, fd;

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	error = mfi_lookup_drive(fd, drive, &device_id);
	if (error) {
		close(fd);
		return (error);
	}

	/* Get the info for this drive. */
	if (mfi_pd_get_info(fd, device_id, &info, NULL) < 0) {
		error = errno;
		warn("Failed to fetch info for drive %u", device_id);
		close(fd);
		return (error);
	}

	/* Try to change the state. */
	if (info.fw_state == new_state) {
		warnx("Drive %u is already in the desired state", device_id);
		close(fd);
		return (EINVAL);
	}

	mbox_store_pdref(&mbox[0], &info.ref);
	mbox[4] = new_state & 0xff;
	mbox[5] = new_state >> 8;
	if (mfi_dcmd_command(fd, MFI_DCMD_PD_STATE_SET, NULL, 0, mbox, 6,
	    NULL) < 0) {
		error = errno;
		warn("Failed to set drive %u to %s", device_id,
		    mfi_pdstate(new_state));
		close(fd);
		return (error);
	}

	close(fd);

	return (0);
}

static int
fail_drive(int ac, char **av)
{

	if (ac != 2) {
		warnx("fail: %s", ac > 2 ? "extra arguments" :
		    "drive required");
		return (EINVAL);
	}

	return (drive_set_state(av[1], MFI_PD_STATE_FAILED));
}
MFI_COMMAND(top, fail, fail_drive);

static int
good_drive(int ac, char **av)
{

	if (ac != 2) {
		warnx("good: %s", ac > 2 ? "extra arguments" :
		    "drive required");
		return (EINVAL);
	}

	return (drive_set_state(av[1], MFI_PD_STATE_UNCONFIGURED_GOOD));
}
MFI_COMMAND(top, good, good_drive);

static int
rebuild_drive(int ac, char **av)
{

	if (ac != 2) {
		warnx("rebuild: %s", ac > 2 ? "extra arguments" :
		    "drive required");
		return (EINVAL);
	}

	return (drive_set_state(av[1], MFI_PD_STATE_REBUILD));
}
MFI_COMMAND(top, rebuild, rebuild_drive);

static int
syspd_drive(int ac, char **av)
{

	if (ac != 2) {
		warnx("syspd: %s", ac > 2 ? "extra arguments" :
		    "drive required");
		return (EINVAL);
	}

	return (drive_set_state(av[1], MFI_PD_STATE_SYSTEM));
}
MFI_COMMAND(top, syspd, syspd_drive);

static int
start_rebuild(int ac, char **av)
{
	struct mfi_pd_info info;
	uint16_t device_id;
	uint8_t mbox[4];
	int error, fd;

	if (ac != 2) {
		warnx("start rebuild: %s", ac > 2 ? "extra arguments" :
		    "drive required");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	error = mfi_lookup_drive(fd, av[1], &device_id);
	if (error) {
		close(fd);
		return (error);
	}

	/* Get the info for this drive. */
	if (mfi_pd_get_info(fd, device_id, &info, NULL) < 0) {
		error = errno;
		warn("Failed to fetch info for drive %u", device_id);
		close(fd);
		return (error);
	}

	/* Check the state, must be REBUILD. */
	if (info.fw_state != MFI_PD_STATE_REBUILD) {
		warnx("Drive %d is not in the REBUILD state", device_id);
		close(fd);
		return (EINVAL);
	}

	/* Start the rebuild. */
	mbox_store_pdref(&mbox[0], &info.ref);
	if (mfi_dcmd_command(fd, MFI_DCMD_PD_REBUILD_START, NULL, 0, mbox, 4,
	    NULL) < 0) {
		error = errno;
		warn("Failed to start rebuild on drive %u", device_id);
		close(fd);
		return (error);
	}
	close(fd);

	return (0);
}
MFI_COMMAND(start, rebuild, start_rebuild);

static int
abort_rebuild(int ac, char **av)
{
	struct mfi_pd_info info;
	uint16_t device_id;
	uint8_t mbox[4];
	int error, fd;

	if (ac != 2) {
		warnx("abort rebuild: %s", ac > 2 ? "extra arguments" :
		    "drive required");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	error = mfi_lookup_drive(fd, av[1], &device_id);
	if (error) {
		close(fd);
		return (error);
	}

	/* Get the info for this drive. */
	if (mfi_pd_get_info(fd, device_id, &info, NULL) < 0) {
		error = errno;
		warn("Failed to fetch info for drive %u", device_id);
		close(fd);
		return (error);
	}

	/* Check the state, must be REBUILD. */
	if (info.fw_state != MFI_PD_STATE_REBUILD) {
		warn("Drive %d is not in the REBUILD state", device_id);
		close(fd);
		return (EINVAL);
	}

	/* Abort the rebuild. */
	mbox_store_pdref(&mbox[0], &info.ref);
	if (mfi_dcmd_command(fd, MFI_DCMD_PD_REBUILD_ABORT, NULL, 0, mbox, 4,
	    NULL) < 0) {
		error = errno;
		warn("Failed to abort rebuild on drive %u", device_id);
		close(fd);
		return (error);
	}
	close(fd);

	return (0);
}
MFI_COMMAND(abort, rebuild, abort_rebuild);

static int
drive_progress(int ac, char **av)
{
	struct mfi_pd_info info;
	uint16_t device_id;
	int error, fd;

	if (ac != 2) {
		warnx("drive progress: %s", ac > 2 ? "extra arguments" :
		    "drive required");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	error = mfi_lookup_drive(fd, av[1], &device_id);
	if (error) {
		close(fd);
		return (error);
	}

	/* Get the info for this drive. */
	if (mfi_pd_get_info(fd, device_id, &info, NULL) < 0) {
		error = errno;
		warn("Failed to fetch info for drive %u", device_id);
		close(fd);
		return (error);
	}
	close(fd);

	/* Display any of the active events. */
	if (info.prog_info.active & MFI_PD_PROGRESS_REBUILD)
		mfi_display_progress("Rebuild", &info.prog_info.rbld);
	if (info.prog_info.active & MFI_PD_PROGRESS_PATROL)
		mfi_display_progress("Patrol Read", &info.prog_info.patrol);
	if (info.prog_info.active & MFI_PD_PROGRESS_CLEAR)
		mfi_display_progress("Clear", &info.prog_info.clear);
	if ((info.prog_info.active & (MFI_PD_PROGRESS_REBUILD |
	    MFI_PD_PROGRESS_PATROL | MFI_PD_PROGRESS_CLEAR)) == 0)
		printf("No activity in progress for drive %s.\n",
		mfi_drive_name(NULL, device_id,
		    MFI_DNAME_DEVICE_ID|MFI_DNAME_HONOR_OPTS));

	return (0);
}
MFI_COMMAND(drive, progress, drive_progress);

static int
drive_clear(int ac, char **av)
{
	struct mfi_pd_info info;
	uint32_t opcode;
	uint16_t device_id;
	uint8_t mbox[4];
	char *s1;
	int error, fd;

	if (ac != 3) {
		warnx("drive clear: %s", ac > 3 ? "extra arguments" :
		    "drive and action requires");
		return (EINVAL);
	}

	for (s1 = av[2]; *s1 != '\0'; s1++)
		*s1 = tolower(*s1);
	if (strcmp(av[2], "start") == 0)
		opcode = MFI_DCMD_PD_CLEAR_START;
	else if ((strcmp(av[2], "stop") == 0) || (strcmp(av[2], "abort") == 0))
		opcode = MFI_DCMD_PD_CLEAR_ABORT;
	else {
		warnx("drive clear: invalid action, must be 'start' or 'stop'\n");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	error = mfi_lookup_drive(fd, av[1], &device_id);
	if (error) {
		close(fd);
		return (error);
	}

	/* Get the info for this drive. */
	if (mfi_pd_get_info(fd, device_id, &info, NULL) < 0) {
		error = errno;
		warn("Failed to fetch info for drive %u", device_id);
		close(fd);
		return (error);
	}

	mbox_store_pdref(&mbox[0], &info.ref);
	if (mfi_dcmd_command(fd, opcode, NULL, 0, mbox, 4, NULL) < 0) {
		error = errno;
		warn("Failed to %s clear on drive %u",
		    opcode == MFI_DCMD_PD_CLEAR_START ? "start" : "stop",
		    device_id);
		close(fd);
		return (error);
	}

	close(fd);
	return (0);
}
MFI_COMMAND(drive, clear, drive_clear);

static int
drive_locate(int ac, char **av)
{
	uint16_t device_id;
	uint32_t opcode;
	int error, fd;
	uint8_t mbox[4];

	if (ac != 3) {
		warnx("locate: %s", ac > 3 ? "extra arguments" :
		    "drive and state required");
		return (EINVAL);
	}

	if (strcasecmp(av[2], "on") == 0 || strcasecmp(av[2], "start") == 0)
		opcode = MFI_DCMD_PD_LOCATE_START;
	else if (strcasecmp(av[2], "off") == 0 ||
	    strcasecmp(av[2], "stop") == 0)
		opcode = MFI_DCMD_PD_LOCATE_STOP;
	else {
		warnx("locate: invalid state %s", av[2]);
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	error = mfi_lookup_drive(fd, av[1], &device_id);
	if (error) {
		close(fd);
		return (error);
	}


	mbox_store_device_id(&mbox[0], device_id);
	mbox[2] = 0;
	mbox[3] = 0;
	if (mfi_dcmd_command(fd, opcode, NULL, 0, mbox, 4, NULL) < 0) {
		error = errno;
		warn("Failed to %s locate on drive %u",
		    opcode == MFI_DCMD_PD_LOCATE_START ? "start" : "stop",
		    device_id);
		close(fd);
		return (error);
	}
	close(fd);

	return (0);
}
MFI_COMMAND(top, locate, drive_locate);
