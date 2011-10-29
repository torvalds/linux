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

#include <linux/slab.h>
#include <asm/div64.h>

#include <scsi/osd_ore.h>

#define ORE_ERR(fmt, a...) printk(KERN_ERR "ore: " fmt, ##a)

#ifdef CONFIG_EXOFS_DEBUG
#define ORE_DBGMSG(fmt, a...) \
	printk(KERN_NOTICE "ore @%s:%d: " fmt, __func__, __LINE__, ##a)
#else
#define ORE_DBGMSG(fmt, a...) \
	do { if (0) printk(fmt, ##a); } while (0)
#endif

/* u64 has problems with printk this will cast it to unsigned long long */
#define _LLU(x) (unsigned long long)(x)

#define ORE_DBGMSG2(M...) do {} while (0)
/* #define ORE_DBGMSG2 ORE_DBGMSG */

MODULE_AUTHOR("Boaz Harrosh <bharrosh@panasas.com>");
MODULE_DESCRIPTION("Objects Raid Engine ore.ko");
MODULE_LICENSE("GPL");

static u8 *_ios_cred(struct ore_io_state *ios, unsigned index)
{
	return ios->comps->comps[index & ios->comps->single_comp].cred;
}

static struct osd_obj_id *_ios_obj(struct ore_io_state *ios, unsigned index)
{
	return &ios->comps->comps[index & ios->comps->single_comp].obj;
}

static struct osd_dev *_ios_od(struct ore_io_state *ios, unsigned index)
{
	return ios->comps->ods[index];
}

int  ore_get_rw_state(struct ore_layout *layout, struct ore_components *comps,
		      bool is_reading, u64 offset, u64 length,
		      struct ore_io_state **pios)
{
	struct ore_io_state *ios;

	/*TODO: Maybe use kmem_cach per sbi of size
	 * exofs_io_state_size(layout->s_numdevs)
	 */
	ios = kzalloc(ore_io_state_size(comps->numdevs), GFP_KERNEL);
	if (unlikely(!ios)) {
		ORE_DBGMSG("Failed kzalloc bytes=%d\n",
			     ore_io_state_size(comps->numdevs));
		*pios = NULL;
		return -ENOMEM;
	}

	ios->layout = layout;
	ios->comps = comps;
	ios->offset = offset;
	ios->length = length;
	ios->reading = is_reading;

	*pios = ios;
	return 0;
}
EXPORT_SYMBOL(ore_get_rw_state);

int  ore_get_io_state(struct ore_layout *layout, struct ore_components *comps,
		      struct ore_io_state **ios)
{
	return ore_get_rw_state(layout, comps, true, 0, 0, ios);
}
EXPORT_SYMBOL(ore_get_io_state);

void ore_put_io_state(struct ore_io_state *ios)
{
	if (ios) {
		unsigned i;

		for (i = 0; i < ios->numdevs; i++) {
			struct ore_per_dev_state *per_dev = &ios->per_dev[i];

			if (per_dev->or)
				osd_end_request(per_dev->or);
			if (per_dev->bio)
				bio_put(per_dev->bio);
		}

		kfree(ios);
	}
}
EXPORT_SYMBOL(ore_put_io_state);

static void _sync_done(struct ore_io_state *ios, void *p)
{
	struct completion *waiting = p;

	complete(waiting);
}

static void _last_io(struct kref *kref)
{
	struct ore_io_state *ios = container_of(
					kref, struct ore_io_state, kref);

	ios->done(ios, ios->private);
}

static void _done_io(struct osd_request *or, void *p)
{
	struct ore_io_state *ios = p;

	kref_put(&ios->kref, _last_io);
}

static int ore_io_execute(struct ore_io_state *ios)
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

		ret = osd_finalize_request(or, 0, _ios_cred(ios, i), NULL);
		if (unlikely(ret)) {
			ORE_DBGMSG("Failed to osd_finalize_request() => %d\n",
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
		ret = ore_check_io(ios, NULL);
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

int ore_check_io(struct ore_io_state *ios, u64 *resid)
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
			ORE_DBGMSG("start read offset passed end of file "
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
EXPORT_SYMBOL(ore_check_io);

/*
 * L - logical offset into the file
 *
 * U - The number of bytes in a stripe within a group
 *
 *	U = stripe_unit * group_width
 *
 * T - The number of bytes striped within a group of component objects
 *     (before advancing to the next group)
 *
 *	T = stripe_unit * group_width * group_depth
 *
 * S - The number of bytes striped across all component objects
 *     before the pattern repeats
 *
 *	S = stripe_unit * group_width * group_depth * group_count
 *
 * M - The "major" (i.e., across all components) stripe number
 *
 *	M = L / S
 *
 * G - Counts the groups from the beginning of the major stripe
 *
 *	G = (L - (M * S)) / T	[or (L % S) / T]
 *
 * H - The byte offset within the group
 *
 *	H = (L - (M * S)) % T	[or (L % S) % T]
 *
 * N - The "minor" (i.e., across the group) stripe number
 *
 *	N = H / U
 *
 * C - The component index coresponding to L
 *
 *	C = (H - (N * U)) / stripe_unit + G * group_width
 *	[or (L % U) / stripe_unit + G * group_width]
 *
 * O - The component offset coresponding to L
 *
 *	O = L % stripe_unit + N * stripe_unit + M * group_depth * stripe_unit
 */
struct _striping_info {
	u64 obj_offset;
	u64 group_length;
	u64 M; /* for truncate */
	unsigned dev;
	unsigned unit_off;
};

static void _calc_stripe_info(struct ore_layout *layout, u64 file_offset,
			      struct _striping_info *si)
{
	u32	stripe_unit = layout->stripe_unit;
	u32	group_width = layout->group_width;
	u64	group_depth = layout->group_depth;

	u32	U = stripe_unit * group_width;
	u64	T = U * group_depth;
	u64	S = T * layout->group_count;
	u64	M = div64_u64(file_offset, S);

	/*
	G = (L - (M * S)) / T
	H = (L - (M * S)) % T
	*/
	u64	LmodS = file_offset - M * S;
	u32	G = div64_u64(LmodS, T);
	u64	H = LmodS - G * T;

	u32	N = div_u64(H, U);

	/* "H - (N * U)" is just "H % U" so it's bound to u32 */
	si->dev = (u32)(H - (N * U)) / stripe_unit + G * group_width;
	si->dev *= layout->mirrors_p1;

	div_u64_rem(file_offset, stripe_unit, &si->unit_off);

	si->obj_offset = si->unit_off + (N * stripe_unit) +
				  (M * group_depth * stripe_unit);

	si->group_length = T - H;
	si->M = M;
}

static int _add_stripe_unit(struct ore_io_state *ios,  unsigned *cur_pg,
		unsigned pgbase, struct ore_per_dev_state *per_dev,
		int cur_len)
{
	unsigned pg = *cur_pg;
	struct request_queue *q =
			osd_request_queue(_ios_od(ios, per_dev->dev));

	per_dev->length += cur_len;

	if (per_dev->bio == NULL) {
		unsigned pages_in_stripe = ios->layout->group_width *
					(ios->layout->stripe_unit / PAGE_SIZE);
		unsigned bio_size = (ios->nr_pages + pages_in_stripe) /
						ios->layout->group_width;

		per_dev->bio = bio_kmalloc(GFP_KERNEL, bio_size);
		if (unlikely(!per_dev->bio)) {
			ORE_DBGMSG("Failed to allocate BIO size=%u\n",
				     bio_size);
			return -ENOMEM;
		}
	}

	while (cur_len > 0) {
		unsigned pglen = min_t(unsigned, PAGE_SIZE - pgbase, cur_len);
		unsigned added_len;

		BUG_ON(ios->nr_pages <= pg);
		cur_len -= pglen;

		added_len = bio_add_pc_page(q, per_dev->bio, ios->pages[pg],
					    pglen, pgbase);
		if (unlikely(pglen != added_len))
			return -ENOMEM;
		pgbase = 0;
		++pg;
	}
	BUG_ON(cur_len);

	*cur_pg = pg;
	return 0;
}

static int _prepare_one_group(struct ore_io_state *ios, u64 length,
			      struct _striping_info *si)
{
	unsigned stripe_unit = ios->layout->stripe_unit;
	unsigned mirrors_p1 = ios->layout->mirrors_p1;
	unsigned devs_in_group = ios->layout->group_width * mirrors_p1;
	unsigned dev = si->dev;
	unsigned first_dev = dev - (dev % devs_in_group);
	unsigned max_comp = ios->numdevs ? ios->numdevs - mirrors_p1 : 0;
	unsigned cur_pg = ios->pages_consumed;
	int ret = 0;

	while (length) {
		struct ore_per_dev_state *per_dev = &ios->per_dev[dev];
		unsigned cur_len, page_off = 0;

		if (!per_dev->length) {
			per_dev->dev = dev;
			if (dev < si->dev) {
				per_dev->offset = si->obj_offset + stripe_unit -
								   si->unit_off;
				cur_len = stripe_unit;
			} else if (dev == si->dev) {
				per_dev->offset = si->obj_offset;
				cur_len = stripe_unit - si->unit_off;
				page_off = si->unit_off & ~PAGE_MASK;
				BUG_ON(page_off && (page_off != ios->pgbase));
			} else { /* dev > si->dev */
				per_dev->offset = si->obj_offset - si->unit_off;
				cur_len = stripe_unit;
			}

			if (max_comp < dev)
				max_comp = dev;
		} else {
			cur_len = stripe_unit;
		}
		if (cur_len >= length)
			cur_len = length;

		ret = _add_stripe_unit(ios, &cur_pg, page_off , per_dev,
				       cur_len);
		if (unlikely(ret))
			goto out;

		dev += mirrors_p1;
		dev = (dev % devs_in_group) + first_dev;

		length -= cur_len;
	}
out:
	ios->numdevs = max_comp + mirrors_p1;
	ios->pages_consumed = cur_pg;
	return ret;
}

static int _prepare_for_striping(struct ore_io_state *ios)
{
	u64 length = ios->length;
	u64 offset = ios->offset;
	struct _striping_info si;
	int ret = 0;

	if (!ios->pages) {
		if (ios->kern_buff) {
			struct ore_per_dev_state *per_dev = &ios->per_dev[0];

			_calc_stripe_info(ios->layout, ios->offset, &si);
			per_dev->offset = si.obj_offset;
			per_dev->dev = si.dev;

			/* no cross device without page array */
			BUG_ON((ios->layout->group_width > 1) &&
			       (si.unit_off + ios->length >
				ios->layout->stripe_unit));
		}
		ios->numdevs = ios->layout->mirrors_p1;
		return 0;
	}

	while (length) {
		_calc_stripe_info(ios->layout, offset, &si);

		if (length < si.group_length)
			si.group_length = length;

		ret = _prepare_one_group(ios, si.group_length, &si);
		if (unlikely(ret))
			goto out;

		offset += si.group_length;
		length -= si.group_length;
	}

out:
	return ret;
}

int ore_create(struct ore_io_state *ios)
{
	int i, ret;

	for (i = 0; i < ios->comps->numdevs; i++) {
		struct osd_request *or;

		or = osd_start_request(_ios_od(ios, i), GFP_KERNEL);
		if (unlikely(!or)) {
			ORE_ERR("%s: osd_start_request failed\n", __func__);
			ret = -ENOMEM;
			goto out;
		}
		ios->per_dev[i].or = or;
		ios->numdevs++;

		osd_req_create_object(or, _ios_obj(ios, i));
	}
	ret = ore_io_execute(ios);

out:
	return ret;
}
EXPORT_SYMBOL(ore_create);

int ore_remove(struct ore_io_state *ios)
{
	int i, ret;

	for (i = 0; i < ios->comps->numdevs; i++) {
		struct osd_request *or;

		or = osd_start_request(_ios_od(ios, i), GFP_KERNEL);
		if (unlikely(!or)) {
			ORE_ERR("%s: osd_start_request failed\n", __func__);
			ret = -ENOMEM;
			goto out;
		}
		ios->per_dev[i].or = or;
		ios->numdevs++;

		osd_req_remove_object(or, _ios_obj(ios, i));
	}
	ret = ore_io_execute(ios);

out:
	return ret;
}
EXPORT_SYMBOL(ore_remove);

static int _write_mirror(struct ore_io_state *ios, int cur_comp)
{
	struct ore_per_dev_state *master_dev = &ios->per_dev[cur_comp];
	unsigned dev = ios->per_dev[cur_comp].dev;
	unsigned last_comp = cur_comp + ios->layout->mirrors_p1;
	int ret = 0;

	if (ios->pages && !master_dev->length)
		return 0; /* Just an empty slot */

	for (; cur_comp < last_comp; ++cur_comp, ++dev) {
		struct ore_per_dev_state *per_dev = &ios->per_dev[cur_comp];
		struct osd_request *or;

		or = osd_start_request(_ios_od(ios, dev), GFP_KERNEL);
		if (unlikely(!or)) {
			ORE_ERR("%s: osd_start_request failed\n", __func__);
			ret = -ENOMEM;
			goto out;
		}
		per_dev->or = or;
		per_dev->offset = master_dev->offset;

		if (ios->pages) {
			struct bio *bio;

			if (per_dev != master_dev) {
				bio = bio_kmalloc(GFP_KERNEL,
						  master_dev->bio->bi_max_vecs);
				if (unlikely(!bio)) {
					ORE_DBGMSG(
					      "Failed to allocate BIO size=%u\n",
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
				bio->bi_rw |= REQ_WRITE;
			}

			osd_req_write(or, _ios_obj(ios, dev), per_dev->offset,
				      bio, per_dev->length);
			ORE_DBGMSG("write(0x%llx) offset=0x%llx "
				      "length=0x%llx dev=%d\n",
				     _LLU(_ios_obj(ios, dev)->id),
				     _LLU(per_dev->offset),
				     _LLU(per_dev->length), dev);
		} else if (ios->kern_buff) {
			ret = osd_req_write_kern(or, _ios_obj(ios, dev),
						 per_dev->offset,
						 ios->kern_buff, ios->length);
			if (unlikely(ret))
				goto out;
			ORE_DBGMSG2("write_kern(0x%llx) offset=0x%llx "
				      "length=0x%llx dev=%d\n",
				     _LLU(_ios_obj(ios, dev)->id),
				     _LLU(per_dev->offset),
				     _LLU(ios->length), dev);
		} else {
			osd_req_set_attributes(or, _ios_obj(ios, dev));
			ORE_DBGMSG2("obj(0x%llx) set_attributes=%d dev=%d\n",
				     _LLU(_ios_obj(ios, dev)->id),
				     ios->out_attr_len, dev);
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

int ore_write(struct ore_io_state *ios)
{
	int i;
	int ret;

	ret = _prepare_for_striping(ios);
	if (unlikely(ret))
		return ret;

	for (i = 0; i < ios->numdevs; i += ios->layout->mirrors_p1) {
		ret = _write_mirror(ios, i);
		if (unlikely(ret))
			return ret;
	}

	ret = ore_io_execute(ios);
	return ret;
}
EXPORT_SYMBOL(ore_write);

static int _read_mirror(struct ore_io_state *ios, unsigned cur_comp)
{
	struct osd_request *or;
	struct ore_per_dev_state *per_dev = &ios->per_dev[cur_comp];
	struct osd_obj_id *obj = _ios_obj(ios, cur_comp);
	unsigned first_dev = (unsigned)obj->id;

	if (ios->pages && !per_dev->length)
		return 0; /* Just an empty slot */

	first_dev = per_dev->dev + first_dev % ios->layout->mirrors_p1;
	or = osd_start_request(_ios_od(ios, first_dev), GFP_KERNEL);
	if (unlikely(!or)) {
		ORE_ERR("%s: osd_start_request failed\n", __func__);
		return -ENOMEM;
	}
	per_dev->or = or;

	if (ios->pages) {
		osd_req_read(or, obj, per_dev->offset,
				per_dev->bio, per_dev->length);
		ORE_DBGMSG("read(0x%llx) offset=0x%llx length=0x%llx"
			     " dev=%d\n", _LLU(obj->id),
			     _LLU(per_dev->offset), _LLU(per_dev->length),
			     first_dev);
	} else if (ios->kern_buff) {
		int ret = osd_req_read_kern(or, obj, per_dev->offset,
					    ios->kern_buff, ios->length);
		ORE_DBGMSG2("read_kern(0x%llx) offset=0x%llx "
			      "length=0x%llx dev=%d ret=>%d\n",
			      _LLU(obj->id), _LLU(per_dev->offset),
			      _LLU(ios->length), first_dev, ret);
		if (unlikely(ret))
			return ret;
	} else {
		osd_req_get_attributes(or, obj);
		ORE_DBGMSG2("obj(0x%llx) get_attributes=%d dev=%d\n",
			      _LLU(obj->id),
			      ios->in_attr_len, first_dev);
	}
	if (ios->out_attr)
		osd_req_add_set_attr_list(or, ios->out_attr, ios->out_attr_len);

	if (ios->in_attr)
		osd_req_add_get_attr_list(or, ios->in_attr, ios->in_attr_len);

	return 0;
}

int ore_read(struct ore_io_state *ios)
{
	int i;
	int ret;

	ret = _prepare_for_striping(ios);
	if (unlikely(ret))
		return ret;

	for (i = 0; i < ios->numdevs; i += ios->layout->mirrors_p1) {
		ret = _read_mirror(ios, i);
		if (unlikely(ret))
			return ret;
	}

	ret = ore_io_execute(ios);
	return ret;
}
EXPORT_SYMBOL(ore_read);

int extract_attr_from_ios(struct ore_io_state *ios, struct osd_attr *attr)
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
EXPORT_SYMBOL(extract_attr_from_ios);

static int _truncate_mirrors(struct ore_io_state *ios, unsigned cur_comp,
			     struct osd_attr *attr)
{
	int last_comp = cur_comp + ios->layout->mirrors_p1;

	for (; cur_comp < last_comp; ++cur_comp) {
		struct ore_per_dev_state *per_dev = &ios->per_dev[cur_comp];
		struct osd_request *or;

		or = osd_start_request(_ios_od(ios, cur_comp), GFP_KERNEL);
		if (unlikely(!or)) {
			ORE_ERR("%s: osd_start_request failed\n", __func__);
			return -ENOMEM;
		}
		per_dev->or = or;

		osd_req_set_attributes(or, _ios_obj(ios, cur_comp));
		osd_req_add_set_attr_list(or, attr, 1);
	}

	return 0;
}

struct _trunc_info {
	struct _striping_info si;
	u64 prev_group_obj_off;
	u64 next_group_obj_off;

	unsigned first_group_dev;
	unsigned nex_group_dev;
	unsigned max_devs;
};

void _calc_trunk_info(struct ore_layout *layout, u64 file_offset,
		       struct _trunc_info *ti)
{
	unsigned stripe_unit = layout->stripe_unit;

	_calc_stripe_info(layout, file_offset, &ti->si);

	ti->prev_group_obj_off = ti->si.M * stripe_unit;
	ti->next_group_obj_off = ti->si.M ? (ti->si.M - 1) * stripe_unit : 0;

	ti->first_group_dev = ti->si.dev - (ti->si.dev % layout->group_width);
	ti->nex_group_dev = ti->first_group_dev + layout->group_width;
	ti->max_devs = layout->group_width * layout->group_count;
}

int ore_truncate(struct ore_layout *layout, struct ore_components *comps,
		   u64 size)
{
	struct ore_io_state *ios;
	struct exofs_trunc_attr {
		struct osd_attr attr;
		__be64 newsize;
	} *size_attrs;
	struct _trunc_info ti;
	int i, ret;

	ret = ore_get_io_state(layout, comps, &ios);
	if (unlikely(ret))
		return ret;

	_calc_trunk_info(ios->layout, size, &ti);

	size_attrs = kcalloc(ti.max_devs, sizeof(*size_attrs),
			     GFP_KERNEL);
	if (unlikely(!size_attrs)) {
		ret = -ENOMEM;
		goto out;
	}

	ios->numdevs = ios->comps->numdevs;

	for (i = 0; i < ti.max_devs; ++i) {
		struct exofs_trunc_attr *size_attr = &size_attrs[i];
		u64 obj_size;

		if (i < ti.first_group_dev)
			obj_size = ti.prev_group_obj_off;
		else if (i >= ti.nex_group_dev)
			obj_size = ti.next_group_obj_off;
		else if (i < ti.si.dev) /* dev within this group */
			obj_size = ti.si.obj_offset +
				      ios->layout->stripe_unit - ti.si.unit_off;
		else if (i == ti.si.dev)
			obj_size = ti.si.obj_offset;
		else /* i > ti.dev */
			obj_size = ti.si.obj_offset - ti.si.unit_off;

		size_attr->newsize = cpu_to_be64(obj_size);
		size_attr->attr = g_attr_logical_length;
		size_attr->attr.val_ptr = &size_attr->newsize;

		ORE_DBGMSG("trunc(0x%llx) obj_offset=0x%llx dev=%d\n",
			     _LLU(comps->comps->obj.id), _LLU(obj_size), i);
		ret = _truncate_mirrors(ios, i * ios->layout->mirrors_p1,
					&size_attr->attr);
		if (unlikely(ret))
			goto out;
	}
	ret = ore_io_execute(ios);

out:
	kfree(size_attrs);
	ore_put_io_state(ios);
	return ret;
}
EXPORT_SYMBOL(ore_truncate);

const struct osd_attr g_attr_logical_length = ATTR_DEF(
	OSD_APAGE_OBJECT_INFORMATION, OSD_ATTR_OI_LOGICAL_LENGTH, 8);
EXPORT_SYMBOL(g_attr_logical_length);
