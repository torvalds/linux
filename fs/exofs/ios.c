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

#include "exofs.h"

#define EXOFS_DBGMSG2(M...) do {} while (0)
/* #define EXOFS_DBGMSG2 EXOFS_DBGMSG */

void exofs_make_credential(u8 cred_a[OSD_CAP_LEN], const struct osd_obj_id *obj)
{
	osd_sec_init_nosec_doall_caps(cred_a, obj, false, true);
}

int exofs_read_kern(struct osd_dev *od, u8 *cred, struct osd_obj_id *obj,
		    u64 offset, void *p, unsigned length)
{
	struct osd_request *or = osd_start_request(od, GFP_KERNEL);
/*	struct osd_sense_info osi = {.key = 0};*/
	int ret;

	if (unlikely(!or)) {
		EXOFS_DBGMSG("%s: osd_start_request failed.\n", __func__);
		return -ENOMEM;
	}
	ret = osd_req_read_kern(or, obj, offset, p, length);
	if (unlikely(ret)) {
		EXOFS_DBGMSG("%s: osd_req_read_kern failed.\n", __func__);
		goto out;
	}

	ret = osd_finalize_request(or, 0, cred, NULL);
	if (unlikely(ret)) {
		EXOFS_DBGMSG("Faild to osd_finalize_request() => %d\n", ret);
		goto out;
	}

	ret = osd_execute_request(or);
	if (unlikely(ret))
		EXOFS_DBGMSG("osd_execute_request() => %d\n", ret);
	/* osd_req_decode_sense(or, ret); */

out:
	osd_end_request(or);
	return ret;
}

int exofs_get_io_state(struct exofs_sb_info *sbi, struct exofs_io_state** pios)
{
	struct exofs_io_state *ios;

	/*TODO: Maybe use kmem_cach per sbi of size
	 * exofs_io_state_size(sbi->s_numdevs)
	 */
	ios = kzalloc(exofs_io_state_size(sbi->s_numdevs), GFP_KERNEL);
	if (unlikely(!ios)) {
		EXOFS_DBGMSG("Faild kzalloc bytes=%d\n",
			     exofs_io_state_size(sbi->s_numdevs));
		*pios = NULL;
		return -ENOMEM;
	}

	ios->sbi = sbi;
	ios->obj.partition = sbi->s_pid;
	*pios = ios;
	return 0;
}

void exofs_put_io_state(struct exofs_io_state *ios)
{
	if (ios) {
		unsigned i;

		for (i = 0; i < ios->numdevs; i++) {
			struct exofs_per_dev_state *per_dev = &ios->per_dev[i];

			if (per_dev->or)
				osd_end_request(per_dev->or);
			if (per_dev->bio)
				bio_put(per_dev->bio);
		}

		kfree(ios);
	}
}

static void _sync_done(struct exofs_io_state *ios, void *p)
{
	struct completion *waiting = p;

	complete(waiting);
}

static void _last_io(struct kref *kref)
{
	struct exofs_io_state *ios = container_of(
					kref, struct exofs_io_state, kref);

	ios->done(ios, ios->private);
}

static void _done_io(struct osd_request *or, void *p)
{
	struct exofs_io_state *ios = p;

	kref_put(&ios->kref, _last_io);
}

static int exofs_io_execute(struct exofs_io_state *ios)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	bool sync = (ios->done == NULL);
	int i, ret;

	if (sync) {
		ios->done = _sync_done;
		ios->private = &wait;
	}

	for (i = 0; i < ios->numdevs; i++) {
		struct osd_request *or = ios->per_dev[i].or;
		if (unlikely(!or))
			continue;

		ret = osd_finalize_request(or, 0, ios->cred, NULL);
		if (unlikely(ret)) {
			EXOFS_DBGMSG("Faild to osd_finalize_request() => %d\n",
				     ret);
			return ret;
		}
	}

	kref_init(&ios->kref);

	for (i = 0; i < ios->numdevs; i++) {
		struct osd_request *or = ios->per_dev[i].or;
		if (unlikely(!or))
			continue;

		kref_get(&ios->kref);
		osd_execute_request_async(or, _done_io, ios);
	}

	kref_put(&ios->kref, _last_io);
	ret = 0;

	if (sync) {
		wait_for_completion(&wait);
		ret = exofs_check_io(ios, NULL);
	}
	return ret;
}

static void _clear_bio(struct bio *bio)
{
	struct bio_vec *bv;
	unsigned i;

	__bio_for_each_segment(bv, bio, i, 0) {
		unsigned this_count = bv->bv_len;

		if (likely(PAGE_SIZE == this_count))
			clear_highpage(bv->bv_page);
		else
			zero_user(bv->bv_page, bv->bv_offset, this_count);
	}
}

int exofs_check_io(struct exofs_io_state *ios, u64 *resid)
{
	enum osd_err_priority acumulated_osd_err = 0;
	int acumulated_lin_err = 0;
	int i;

	for (i = 0; i < ios->numdevs; i++) {
		struct osd_sense_info osi;
		struct osd_request *or = ios->per_dev[i].or;
		int ret;

		if (unlikely(!or))
			continue;

		ret = osd_req_decode_sense(or, &osi);
		if (likely(!ret))
			continue;

		if (OSD_ERR_PRI_CLEAR_PAGES == osi.osd_err_pri) {
			/* start read offset passed endof file */
			_clear_bio(ios->per_dev[i].bio);
			EXOFS_DBGMSG("start read offset passed end of file "
				"offset=0x%llx, length=0x%llx\n",
				_LLU(ios->offset),
				_LLU(ios->length));

			continue; /* we recovered */
		}

		if (osi.osd_err_pri >= acumulated_osd_err) {
			acumulated_osd_err = osi.osd_err_pri;
			acumulated_lin_err = ret;
		}
	}

	/* TODO: raid specific residual calculations */
	if (resid) {
		if (likely(!acumulated_lin_err))
			*resid = 0;
		else
			*resid = ios->length;
	}

	return acumulated_lin_err;
}

int exofs_sbi_create(struct exofs_io_state *ios)
{
	int i, ret;

	for (i = 0; i < ios->sbi->s_numdevs; i++) {
		struct osd_request *or;

		or = osd_start_request(ios->sbi->s_ods[i], GFP_KERNEL);
		if (unlikely(!or)) {
			EXOFS_ERR("%s: osd_start_request failed\n", __func__);
			ret = -ENOMEM;
			goto out;
		}
		ios->per_dev[i].or = or;
		ios->numdevs++;

		osd_req_create_object(or, &ios->obj);
	}
	ret = exofs_io_execute(ios);

out:
	return ret;
}

int exofs_sbi_remove(struct exofs_io_state *ios)
{
	int i, ret;

	for (i = 0; i < ios->sbi->s_numdevs; i++) {
		struct osd_request *or;

		or = osd_start_request(ios->sbi->s_ods[i], GFP_KERNEL);
		if (unlikely(!or)) {
			EXOFS_ERR("%s: osd_start_request failed\n", __func__);
			ret = -ENOMEM;
			goto out;
		}
		ios->per_dev[i].or = or;
		ios->numdevs++;

		osd_req_remove_object(or, &ios->obj);
	}
	ret = exofs_io_execute(ios);

out:
	return ret;
}

int exofs_sbi_write(struct exofs_io_state *ios)
{
	int i, ret;

	for (i = 0; i < ios->sbi->s_numdevs; i++) {
		struct osd_request *or;

		or = osd_start_request(ios->sbi->s_ods[i], GFP_KERNEL);
		if (unlikely(!or)) {
			EXOFS_ERR("%s: osd_start_request failed\n", __func__);
			ret = -ENOMEM;
			goto out;
		}
		ios->per_dev[i].or = or;
		ios->numdevs++;

		if (ios->bio) {
			struct bio *bio;

			if (i != 0) {
				bio = bio_kmalloc(GFP_KERNEL,
						  ios->bio->bi_max_vecs);
				if (unlikely(!bio)) {
					EXOFS_DBGMSG(
					      "Faild to allocate BIO size=%u\n",
					      ios->bio->bi_max_vecs);
					ret = -ENOMEM;
					goto out;
				}

				__bio_clone(bio, ios->bio);
				bio->bi_bdev = NULL;
				bio->bi_next = NULL;
				ios->per_dev[i].bio =  bio;
			} else {
				bio = ios->bio;
			}

			osd_req_write(or, &ios->obj, ios->offset, bio,
				      ios->length);
			EXOFS_DBGMSG("write(0x%llx) offset=0x%llx "
				      "length=0x%llx dev=%d\n",
				     _LLU(ios->obj.id), _LLU(ios->offset),
				     _LLU(ios->length), i);
		} else if (ios->kern_buff) {
			osd_req_write_kern(or, &ios->obj, ios->offset,
					   ios->kern_buff, ios->length);
			EXOFS_DBGMSG2("write_kern(0x%llx) offset=0x%llx "
				      "length=0x%llx dev=%d\n",
				     _LLU(ios->obj.id), _LLU(ios->offset),
				     _LLU(ios->length), i);
		} else {
			osd_req_set_attributes(or, &ios->obj);
			EXOFS_DBGMSG2("obj(0x%llx) set_attributes=%d dev=%d\n",
				     _LLU(ios->obj.id), ios->out_attr_len, i);
		}

		if (ios->out_attr)
			osd_req_add_set_attr_list(or, ios->out_attr,
						  ios->out_attr_len);

		if (ios->in_attr)
			osd_req_add_get_attr_list(or, ios->in_attr,
						  ios->in_attr_len);
	}
	ret = exofs_io_execute(ios);

out:
	return ret;
}

int exofs_sbi_read(struct exofs_io_state *ios)
{
	int i, ret;

	for (i = 0; i < 1; i++) {
		struct osd_request *or;
		unsigned first_dev = (unsigned)ios->obj.id;

		first_dev %= ios->sbi->s_numdevs;
		or = osd_start_request(ios->sbi->s_ods[first_dev], GFP_KERNEL);
		if (unlikely(!or)) {
			EXOFS_ERR("%s: osd_start_request failed\n", __func__);
			ret = -ENOMEM;
			goto out;
		}
		ios->per_dev[i].or = or;
		ios->numdevs++;

		if (ios->bio) {
			osd_req_read(or, &ios->obj, ios->offset, ios->bio,
				     ios->length);
			EXOFS_DBGMSG("read(0x%llx) offset=0x%llx length=0x%llx"
				      " dev=%d\n", _LLU(ios->obj.id),
				     _LLU(ios->offset),
				     _LLU(ios->length),
				     first_dev);
		} else if (ios->kern_buff) {
			osd_req_read_kern(or, &ios->obj, ios->offset,
					   ios->kern_buff, ios->length);
			EXOFS_DBGMSG2("read_kern(0x%llx) offset=0x%llx "
				      "length=0x%llx dev=%d\n",
				     _LLU(ios->obj.id),
				     _LLU(ios->offset),
				     _LLU(ios->length),
				     first_dev);
		} else {
			osd_req_get_attributes(or, &ios->obj);
			EXOFS_DBGMSG2("obj(0x%llx) get_attributes=%d dev=%d\n",
				     _LLU(ios->obj.id), ios->in_attr_len,
				     first_dev);
		}

		if (ios->out_attr)
			osd_req_add_set_attr_list(or, ios->out_attr,
						  ios->out_attr_len);

		if (ios->in_attr)
			osd_req_add_get_attr_list(or, ios->in_attr,
						  ios->in_attr_len);
	}
	ret = exofs_io_execute(ios);

out:
	return ret;
}

int extract_attr_from_ios(struct exofs_io_state *ios, struct osd_attr *attr)
{
	struct osd_attr cur_attr = {.attr_page = 0}; /* start with zeros */
	void *iter = NULL;
	int nelem;

	do {
		nelem = 1;
		osd_req_decode_get_attr_list(ios->per_dev[0].or,
					     &cur_attr, &nelem, &iter);
		if ((cur_attr.attr_page == attr->attr_page) &&
		    (cur_attr.attr_id == attr->attr_id)) {
			attr->len = cur_attr.len;
			attr->val_ptr = cur_attr.val_ptr;
			return 0;
		}
	} while (iter);

	return -EIO;
}

int exofs_oi_truncate(struct exofs_i_info *oi, u64 size)
{
	struct exofs_sb_info *sbi = oi->vfs_inode.i_sb->s_fs_info;
	struct exofs_io_state *ios;
	struct osd_attr attr;
	__be64 newsize;
	int i, ret;

	if (exofs_get_io_state(sbi, &ios))
		return -ENOMEM;

	ios->obj.id = exofs_oi_objno(oi);
	ios->cred = oi->i_cred;

	newsize = cpu_to_be64(size);
	attr = g_attr_logical_length;
	attr.val_ptr = &newsize;

	for (i = 0; i < sbi->s_numdevs; i++) {
		struct osd_request *or;

		or = osd_start_request(sbi->s_ods[i], GFP_KERNEL);
		if (unlikely(!or)) {
			EXOFS_ERR("%s: osd_start_request failed\n", __func__);
			ret = -ENOMEM;
			goto out;
		}
		ios->per_dev[i].or = or;
		ios->numdevs++;

		osd_req_set_attributes(or, &ios->obj);
		osd_req_add_set_attr_list(or, &attr, 1);
	}
	ret = exofs_io_execute(ios);

out:
	exofs_put_io_state(ios);
	return ret;
}
