/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com)
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <scsi/scsi_device.h>
#include <scsi/osd_sense.h>

#include "exofs.h"

int exofs_check_ok_resid(struct osd_request *or, u64 *in_resid, u64 *out_resid)
{
	struct osd_sense_info osi;
	int ret = osd_req_decode_sense(or, &osi);

	if (ret) { /* translate to Linux codes */
		if (osi.additional_code == scsi_invalid_field_in_cdb) {
			if (osi.cdb_field_offset == OSD_CFO_STARTING_BYTE)
				ret = -EFAULT;
			if (osi.cdb_field_offset == OSD_CFO_OBJECT_ID)
				ret = -ENOENT;
			else
				ret = -EINVAL;
		} else if (osi.additional_code == osd_quota_error)
			ret = -ENOSPC;
		else
			ret = -EIO;
	}

	/* FIXME: should be include in osd_sense_info */
	if (in_resid)
		*in_resid = or->in.req ? or->in.req->resid_len : 0;

	if (out_resid)
		*out_resid = or->out.req ? or->out.req->resid_len : 0;

	return ret;
}

void exofs_make_credential(u8 cred_a[OSD_CAP_LEN], const struct osd_obj_id *obj)
{
	osd_sec_init_nosec_doall_caps(cred_a, obj, false, true);
}

/*
 * Perform a synchronous OSD operation.
 */
int exofs_sync_op(struct osd_request *or, int timeout, uint8_t *credential)
{
	int ret;

	or->timeout = timeout;
	ret = osd_finalize_request(or, 0, credential, NULL);
	if (ret) {
		EXOFS_DBGMSG("Faild to osd_finalize_request() => %d\n", ret);
		return ret;
	}

	ret = osd_execute_request(or);

	if (ret)
		EXOFS_DBGMSG("osd_execute_request() => %d\n", ret);
	/* osd_req_decode_sense(or, ret); */
	return ret;
}

/*
 * Perform an asynchronous OSD operation.
 */
int exofs_async_op(struct osd_request *or, osd_req_done_fn *async_done,
		   void *caller_context, u8 *cred)
{
	int ret;

	ret = osd_finalize_request(or, 0, cred, NULL);
	if (ret) {
		EXOFS_DBGMSG("Faild to osd_finalize_request() => %d\n", ret);
		return ret;
	}

	ret = osd_execute_request_async(or, async_done, caller_context);

	if (ret)
		EXOFS_DBGMSG("osd_execute_request_async() => %d\n", ret);
	return ret;
}

int extract_attr_from_req(struct osd_request *or, struct osd_attr *attr)
{
	struct osd_attr cur_attr = {.attr_page = 0}; /* start with zeros */
	void *iter = NULL;
	int nelem;

	do {
		nelem = 1;
		osd_req_decode_get_attr_list(or, &cur_attr, &nelem, &iter);
		if ((cur_attr.attr_page == attr->attr_page) &&
		    (cur_attr.attr_id == attr->attr_id)) {
			attr->len = cur_attr.len;
			attr->val_ptr = cur_attr.val_ptr;
			return 0;
		}
	} while (iter);

	return -EIO;
}
