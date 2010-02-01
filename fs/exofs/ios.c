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
#include <asm/div64.h>

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

int exofs_get_io_state(struct exofs_layout *layout,
		       struct exofs_io_state **pios)
{
	struct exofs_io_state *ios;

	/*TODO: Maybe use kmem_cach per sbi of size
	 * exofs_io_state_size(layout->s_numdevs)
	 */
	ios = kzalloc(exofs_io_state_size(layout->s_numdevs), GFP_KERNEL);
	if (unlikely(!ios)) {
		EXOFS_DBGMSG("Faild kzalloc bytes=%d\n",
			     exofs_io_state_size(layout->s_numdevs));
		*pios = NULL;
		return -ENOMEM;
	}

	ios->layout = layout;
	ios->obj.partition = layout->s_pid;
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

unsigned exofs_layout_od_id(struct exofs_layout *layout,
			    osd_id obj_no, unsigned layout_index)
{
/*	switch (layout->lay_func) {
	case LAYOUT_MOVING_WINDOW:
	{*/
		unsigned dev_mod = obj_no;

		return (layout_index + dev_mod * layout->mirrors_p1) %
							      layout->s_numdevs;
/*	}
	case LAYOUT_FUNC_IMPLICT:
		return layout->devs[layout_index];
	}*/
}

static inline struct osd_dev *exofs_ios_od(struct exofs_io_state *ios,
					   unsigned layout_index)
{
	return ios->layout->s_ods[
		exofs_layout_od_id(ios->layout, ios->obj.id, layout_index)];
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
				_LLU(ios->per_dev[i].offset),
				_LLU(ios->per_dev[i].length));

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

/* REMOVEME: After review
   Some quoteing from the standard

   L = logical offset into the file
   W = number of data components in a stripe
   S = W * stripe_unit (S is Stripe length)
   N = L / S (N is the stripe Number)
   C = (L-(N*S)) / stripe_unit (C is the component)
   O = (N*stripe_unit)+(L%stripe_unit) (O is the object's offset)
*/

static void _offset_dev_unit_off(struct exofs_io_state *ios, u64 file_offset,
			u64 *obj_offset, unsigned *dev, unsigned *unit_off)
{
	unsigned stripe_unit = ios->layout->stripe_unit;
	unsigned stripe_length = stripe_unit * ios->layout->group_width;
	u64 stripe_no = file_offset;
	unsigned stripe_mod = do_div(stripe_no, stripe_length);

	*unit_off = stripe_mod % stripe_unit;
	*obj_offset = stripe_no * stripe_unit + *unit_off;
	*dev = stripe_mod / stripe_unit * ios->layout->mirrors_p1;
}

static int _add_stripe_unit(struct exofs_io_state *ios,  unsigned *cur_bvec,
		     struct exofs_per_dev_state *per_dev, int cur_len)
{
	unsigned bv = *cur_bvec;
	struct request_queue *q =
			osd_request_queue(exofs_ios_od(ios, per_dev->dev));

	per_dev->length += cur_len;

	if (per_dev->bio == NULL) {
		unsigned pages_in_stripe = ios->layout->group_width *
					(ios->layout->stripe_unit / PAGE_SIZE);
		unsigned bio_size = (ios->bio->bi_vcnt + pages_in_stripe) /
						ios->layout->group_width;

		per_dev->bio = bio_kmalloc(GFP_KERNEL, bio_size);
		if (unlikely(!per_dev->bio)) {
			EXOFS_DBGMSG("Faild to allocate BIO size=%u\n",
				     bio_size);
			return -ENOMEM;
		}
	}

	while (cur_len > 0) {
		int added_len;
		struct bio_vec *bvec = &ios->bio->bi_io_vec[bv];

		BUG_ON(ios->bio->bi_vcnt <= bv);
		cur_len -= bvec->bv_len;

		added_len = bio_add_pc_page(q, per_dev->bio, bvec->bv_page,
					    bvec->bv_len, bvec->bv_offset);
		if (unlikely(bvec->bv_len != added_len))
			return -ENOMEM;
		++bv;
	}
	BUG_ON(cur_len);

	*cur_bvec = bv;
	return 0;
}

static int _prepare_for_striping(struct exofs_io_state *ios)
{
	u64 length = ios->length;
	u64 offset = ios->offset;
	unsigned stripe_unit = ios->layout->stripe_unit;
	unsigned comp = 0;
	unsigned stripes = 0;
	unsigned cur_bvec = 0;
	int ret;

	if (!ios->bio) {
		if (ios->kern_buff) {
			struct exofs_per_dev_state *per_dev = &ios->per_dev[0];
			unsigned unit_off;

			_offset_dev_unit_off(ios, offset, &per_dev->offset,
					     &per_dev->dev, &unit_off);
			/* no cross device without page array */
			BUG_ON((ios->layout->group_width > 1) &&
			       (unit_off + length > stripe_unit));
		}
		ios->numdevs = ios->layout->mirrors_p1;
		return 0;
	}

	while (length) {
		struct exofs_per_dev_state *per_dev = &ios->per_dev[comp];
		unsigned cur_len;

		if (!per_dev->length) {
			unsigned unit_off;

			_offset_dev_unit_off(ios, offset, &per_dev->offset,
					     &per_dev->dev, &unit_off);
			stripes++;
			cur_len = min_t(u64, stripe_unit - unit_off, length);
			offset += cur_len;
		} else {
			cur_len = min_t(u64, stripe_unit, length);
		}

		ret = _add_stripe_unit(ios, &cur_bvec, per_dev, cur_len);
		if (unlikely(ret))
			goto out;

		comp += ios->layout->mirrors_p1;
		comp %= ios->layout->s_numdevs;

		length -= cur_len;
	}
out:
	ios->numdevs = stripes * ios->layout->mirrors_p1;
	return ret;
}

int exofs_sbi_create(struct exofs_io_state *ios)
{
	int i, ret;

	for (i = 0; i < ios->layout->s_numdevs; i++) {
		struct osd_request *or;

		or = osd_start_request(exofs_ios_od(ios, i), GFP_KERNEL);
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

	for (i = 0; i < ios->layout->s_numdevs; i++) {
		struct osd_request *or;

		or = osd_start_request(exofs_ios_od(ios, i), GFP_KERNEL);
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

static int _sbi_write_mirror(struct exofs_io_state *ios, int cur_comp)
{
	struct exofs_per_dev_state *master_dev = &ios->per_dev[cur_comp];
	unsigned dev = ios->per_dev[cur_comp].dev;
	unsigned last_comp = cur_comp + ios->layout->mirrors_p1;
	int ret = 0;

	for (; cur_comp < last_comp; ++cur_comp, ++dev) {
		struct exofs_per_dev_state *per_dev = &ios->per_dev[cur_comp];
		struct osd_request *or;

		or = osd_start_request(exofs_ios_od(ios, dev), GFP_KERNEL);
		if (unlikely(!or)) {
			EXOFS_ERR("%s: osd_start_request failed\n", __func__);
			ret = -ENOMEM;
			goto out;
		}
		per_dev->or = or;
		per_dev->offset = master_dev->offset;

		if (ios->bio) {
			struct bio *bio;

			if (per_dev != master_dev) {
				bio = bio_kmalloc(GFP_KERNEL,
						  master_dev->bio->bi_max_vecs);
				if (unlikely(!bio)) {
					EXOFS_DBGMSG(
					      "Faild to allocate BIO size=%u\n",
					      master_dev->bio->bi_max_vecs);
					ret = -ENOMEM;
					goto out;
				}

				__bio_clone(bio, master_dev->bio);
				bio->bi_bdev = NULL;
				bio->bi_next = NULL;
				per_dev->length = master_dev->length;
				per_dev->bio =  bio;
				per_dev->dev = dev;
			} else {
				bio = master_dev->bio;
				/* FIXME: bio_set_dir() */
				bio->bi_rw |= (1 << BIO_RW);
			}

			osd_req_write(or, &ios->obj, per_dev->offset, bio,
				      per_dev->length);
			EXOFS_DBGMSG("write(0x%llx) offset=0x%llx "
				      "length=0x%llx dev=%d\n",
				     _LLU(ios->obj.id), _LLU(per_dev->offset),
				     _LLU(per_dev->length), dev);
		} else if (ios->kern_buff) {
			ret = osd_req_write_kern(or, &ios->obj, per_dev->offset,
					   ios->kern_buff, ios->length);
			if (unlikely(ret))
				goto out;
			EXOFS_DBGMSG2("write_kern(0x%llx) offset=0x%llx "
				      "length=0x%llx dev=%d\n",
				     _LLU(ios->obj.id), _LLU(per_dev->offset),
				     _LLU(ios->length), dev);
		} else {
			osd_req_set_attributes(or, &ios->obj);
			EXOFS_DBGMSG2("obj(0x%llx) set_attributes=%d dev=%d\n",
				     _LLU(ios->obj.id), ios->out_attr_len, dev);
		}

		if (ios->out_attr)
			osd_req_add_set_attr_list(or, ios->out_attr,
						  ios->out_attr_len);

		if (ios->in_attr)
			osd_req_add_get_attr_list(or, ios->in_attr,
						  ios->in_attr_len);
	}

out:
	return ret;
}

int exofs_sbi_write(struct exofs_io_state *ios)
{
	int i;
	int ret;

	ret = _prepare_for_striping(ios);
	if (unlikely(ret))
		return ret;

	for (i = 0; i < ios->numdevs; i += ios->layout->mirrors_p1) {
		ret = _sbi_write_mirror(ios, i);
		if (unlikely(ret))
			return ret;
	}

	ret = exofs_io_execute(ios);
	return ret;
}

static int _sbi_read_mirror(struct exofs_io_state *ios, unsigned cur_comp)
{
	struct osd_request *or;
	struct exofs_per_dev_state *per_dev = &ios->per_dev[cur_comp];
	unsigned first_dev = (unsigned)ios->obj.id;

	first_dev = per_dev->dev + first_dev % ios->layout->mirrors_p1;
	or = osd_start_request(exofs_ios_od(ios, first_dev), GFP_KERNEL);
	if (unlikely(!or)) {
		EXOFS_ERR("%s: osd_start_request failed\n", __func__);
		return -ENOMEM;
	}
	per_dev->or = or;

	if (ios->bio) {
		osd_req_read(or, &ios->obj, per_dev->offset,
				per_dev->bio, per_dev->length);
		EXOFS_DBGMSG("read(0x%llx) offset=0x%llx length=0x%llx"
			     " dev=%d\n", _LLU(ios->obj.id),
			     _LLU(per_dev->offset), _LLU(per_dev->length),
			     first_dev);
	} else if (ios->kern_buff) {
		int ret = osd_req_read_kern(or, &ios->obj, per_dev->offset,
					    ios->kern_buff, ios->length);
		EXOFS_DBGMSG2("read_kern(0x%llx) offset=0x%llx "
			      "length=0x%llx dev=%d ret=>%d\n",
			      _LLU(ios->obj.id), _LLU(per_dev->offset),
			      _LLU(ios->length), first_dev, ret);
		if (unlikely(ret))
			return ret;
	} else {
		osd_req_get_attributes(or, &ios->obj);
		EXOFS_DBGMSG2("obj(0x%llx) get_attributes=%d dev=%d\n",
			      _LLU(ios->obj.id), ios->in_attr_len, first_dev);
	}
	if (ios->out_attr)
		osd_req_add_set_attr_list(or, ios->out_attr, ios->out_attr_len);

	if (ios->in_attr)
		osd_req_add_get_attr_list(or, ios->in_attr, ios->in_attr_len);

	return 0;
}

int exofs_sbi_read(struct exofs_io_state *ios)
{
	int i;
	int ret;

	ret = _prepare_for_striping(ios);
	if (unlikely(ret))
		return ret;

	for (i = 0; i < ios->numdevs; i += ios->layout->mirrors_p1) {
		ret = _sbi_read_mirror(ios, i);
		if (unlikely(ret))
			return ret;
	}

	ret = exofs_io_execute(ios);
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

static int _truncate_mirrors(struct exofs_io_state *ios, unsigned cur_comp,
			     struct osd_attr *attr)
{
	int last_comp = cur_comp + ios->layout->mirrors_p1;

	for (; cur_comp < last_comp; ++cur_comp) {
		struct exofs_per_dev_state *per_dev = &ios->per_dev[cur_comp];
		struct osd_request *or;

		or = osd_start_request(exofs_ios_od(ios, cur_comp), GFP_KERNEL);
		if (unlikely(!or)) {
			EXOFS_ERR("%s: osd_start_request failed\n", __func__);
			return -ENOMEM;
		}
		per_dev->or = or;

		osd_req_set_attributes(or, &ios->obj);
		osd_req_add_set_attr_list(or, attr, 1);
	}

	return 0;
}

int exofs_oi_truncate(struct exofs_i_info *oi, u64 size)
{
	struct exofs_sb_info *sbi = oi->vfs_inode.i_sb->s_fs_info;
	struct exofs_io_state *ios;
	struct exofs_trunc_attr {
		struct osd_attr attr;
		__be64 newsize;
	} *size_attrs;
	u64 this_obj_size;
	unsigned dev;
	unsigned unit_off;
	int i, ret;

	ret = exofs_get_io_state(&sbi->layout, &ios);
	if (unlikely(ret))
		return ret;

	size_attrs = kcalloc(ios->layout->group_width, sizeof(*size_attrs),
			     GFP_KERNEL);
	if (unlikely(!size_attrs)) {
		ret = -ENOMEM;
		goto out;
	}

	ios->obj.id = exofs_oi_objno(oi);
	ios->cred = oi->i_cred;

	ios->numdevs = ios->layout->s_numdevs;
	_offset_dev_unit_off(ios, size, &this_obj_size, &dev, &unit_off);

	for (i = 0; i < ios->layout->group_width; ++i) {
		struct exofs_trunc_attr *size_attr = &size_attrs[i];
		u64 obj_size;

		if (i < dev)
			obj_size = this_obj_size +
					ios->layout->stripe_unit - unit_off;
		else if (i == dev)
			obj_size = this_obj_size;
		else /* i > dev */
			obj_size = this_obj_size - unit_off;

		size_attr->newsize = cpu_to_be64(obj_size);
		size_attr->attr = g_attr_logical_length;
		size_attr->attr.val_ptr = &size_attr->newsize;

		ret = _truncate_mirrors(ios, i * ios->layout->mirrors_p1,
					&size_attr->attr);
		if (unlikely(ret))
			goto out;
	}
	ret = exofs_io_execute(ios);

out:
	kfree(size_attrs);
	exofs_put_io_state(ios);
	return ret;
}
