/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1998 Kenneth D. Merry.
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
 *
 * $FreeBSD$
 */

#ifndef _CAMCONTROL_H
#define _CAMCONTROL_H

typedef enum {
	CC_OR_NOT_FOUND,
	CC_OR_AMBIGUOUS,
	CC_OR_FOUND
} camcontrol_optret;

typedef enum {
	CC_DT_NONE,
	CC_DT_SCSI,
	CC_DT_ATA_BEHIND_SCSI,
	CC_DT_ATA,
	CC_DT_UNKNOWN
} camcontrol_devtype;

/*
 * get_hook: Structure for evaluating args in a callback.
 */
struct get_hook
{
	int argc;
	char **argv;
	int got;
};

extern int verbose;

int ata_do_identify(struct cam_device *device, int retry_count, int timeout,
		    union ccb *ccb, struct ata_params **ident_bufp);
int dev_has_vpd_page(struct cam_device *dev, uint8_t page_id, int retry_count,
		     int timeout, int verbosemode);
int get_device_type(struct cam_device *dev, int retry_count, int timeout,
		    int verbosemode, camcontrol_devtype *devtype);
int build_ata_cmd(union ccb *ccb, uint32_t retry_count, uint32_t flags,
		  uint8_t tag_action, uint8_t protocol, uint8_t ata_flags,
		  uint16_t features, uint16_t sector_count, uint64_t lba,
		  uint8_t command, uint32_t auxiliary, uint8_t *data_ptr,
		  uint32_t dxfer_len, uint8_t *cdb_storage,
		  size_t cdb_storage_len, uint8_t sense_len, uint32_t timeout,
		  int is48bit, camcontrol_devtype devtype);
int get_ata_status(struct cam_device *dev, union ccb *ccb, uint8_t *error,
		   uint16_t *count, uint64_t *lba, uint8_t *device,
		   uint8_t *status);
int camxferrate(struct cam_device *device);
int fwdownload(struct cam_device *device, int argc, char **argv,
	       char *combinedopt, int printerrors, int task_attr,
	       int retry_count, int timeout);
int zone(struct cam_device *device, int argc, char **argv, char *combinedopt,
	 int task_attr, int retry_count, int timeout, int verbosemode);
int epc(struct cam_device *device, int argc, char **argv, char *combinedopt,
	int retry_count, int timeout, int verbosemode);
int timestamp(struct cam_device *device, int argc, char **argv,
	      char *combinedopt, int task_attr, int retry_count, int timeout,
	      int verbosemode);
void mode_sense(struct cam_device *device, int dbd, int pc, int page,
		int subpage, int task_attr, int retry_count, int timeout,
		uint8_t *data, int datalen);
void mode_select(struct cam_device *device, int save_pages, int task_attr,
		 int retry_count, int timeout, u_int8_t *data, int datalen);
void mode_edit(struct cam_device *device, int dbd, int pc, int page,
	       int subpage, int edit, int binary, int task_attr,
	       int retry_count, int timeout);
void mode_list(struct cam_device *device, int dbd, int pc, int subpages,
	       int task_attr, int retry_count, int timeout);
int scsidoinquiry(struct cam_device *device, int argc, char **argv,
		  char *combinedopt, int task_attr, int retry_count,
		  int timeout);
int scsigetopcodes(struct cam_device *device, int opcode_set, int opcode,
		   int show_sa_errors, int sa_set, int service_action,
		   int timeout_desc, int task_attr, int retry_count,
		   int timeout, int verbosemode, uint32_t *fill_len,
		   uint8_t **data_ptr);
int scsipersist(struct cam_device *device, int argc, char **argv,
		char *combinedopt, int task_attr, int retry_count,
		int timeout, int verbose, int err_recover);
int scsiattrib(struct cam_device *device, int argc, char **argv,
	       char *combinedopt, int task_attr, int retry_count, int timeout,
	       int verbose, int err_recover);
char *cget(void *hook, char *name);
int iget(void *hook, char *name);
void arg_put(void *hook, int letter, void *arg, int count, char *name);
int get_confirmation(void);
void usage(int printlong);
#endif /* _CAMCONTROL_H */
