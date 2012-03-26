/*
 * Copyright (C) 2011
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Public Declarations of the ORE API
 *
 * This file is part of the ORE (Object Raid Engine) library.
 *
 * ORE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation. (GPL v2)
 *
 * ORE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the ORE; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __ORE_H__
#define __ORE_H__

#include <scsi/osd_initiator.h>
#include <scsi/osd_attributes.h>
#include <scsi/osd_sec.h>
#include <linux/pnfs_osd_xdr.h>
#include <linux/bug.h>

struct ore_comp {
	struct osd_obj_id	obj;
	u8			cred[OSD_CAP_LEN];
};

struct ore_layout {
	/* Our way of looking at the data_map */
	enum pnfs_osd_raid_algorithm4
		 raid_algorithm;
	unsigned stripe_unit;
	unsigned mirrors_p1;

	unsigned group_width;
	unsigned parity;
	u64	 group_depth;
	unsigned group_count;

	/* Cached often needed calculations filled in by
	 * ore_verify_layout
	 */
	unsigned long max_io_length;	/* Max length that should be passed to
					 * ore_get_rw_state
					 */
};

struct ore_dev {
	struct osd_dev *od;
};

struct ore_components {
	unsigned	first_dev;		/* First logical device no    */
	unsigned	numdevs;		/* Num of devices in array    */
	/* If @single_comp == EC_SINGLE_COMP, @comps points to a single
	 * component. else there are @numdevs components
	 */
	enum EC_COMP_USAGE {
		EC_SINGLE_COMP = 0, EC_MULTPLE_COMPS = 0xffffffff
	}		single_comp;
	struct ore_comp	*comps;

	/* Array of pointers to ore_dev-* . User will usually have these pointed
	 * too a bigger struct which contain an "ore_dev ored" member and use
	 * container_of(oc->ods[i], struct foo_dev, ored) to access the bigger
	 * structure.
	 */
	struct ore_dev	**ods;
};

/* ore_comp_dev Recievies a logical device index */
static inline struct osd_dev *ore_comp_dev(
	const struct ore_components *oc, unsigned i)
{
	BUG_ON((i < oc->first_dev) || (oc->first_dev + oc->numdevs <= i));
	return oc->ods[i - oc->first_dev]->od;
}

static inline void ore_comp_set_dev(
	struct ore_components *oc, unsigned i, struct osd_dev *od)
{
	oc->ods[i - oc->first_dev]->od = od;
}

struct ore_striping_info {
	u64 offset;
	u64 obj_offset;
	u64 length;
	u64 first_stripe_start; /* only used in raid writes */
	u64 M; /* for truncate */
	unsigned bytes_in_stripe;
	unsigned dev;
	unsigned par_dev;
	unsigned unit_off;
	unsigned cur_pg;
	unsigned cur_comp;
};

struct ore_io_state;
typedef void (*ore_io_done_fn)(struct ore_io_state *ios, void *private);
struct _ore_r4w_op {
	/* @Priv given here is passed ios->private */
	struct page * (*get_page)(void *priv, u64 page_index, bool *uptodate);
	void (*put_page)(void *priv, struct page *page);
};

struct ore_io_state {
	struct kref		kref;
	struct ore_striping_info si;

	void			*private;
	ore_io_done_fn	done;

	struct ore_layout	*layout;
	struct ore_components	*oc;

	/* Global read/write IO*/
	loff_t			offset;
	unsigned long		length;
	void			*kern_buff;

	struct page		**pages;
	unsigned		nr_pages;
	unsigned		pgbase;
	unsigned		pages_consumed;

	/* Attributes */
	unsigned		in_attr_len;
	struct osd_attr		*in_attr;
	unsigned		out_attr_len;
	struct osd_attr		*out_attr;

	bool			reading;

	/* House keeping of Parity pages */
	bool			extra_part_alloc;
	struct page		**parity_pages;
	unsigned		max_par_pages;
	unsigned		cur_par_page;
	unsigned		sgs_per_dev;
	struct __stripe_pages_2d *sp2d;
	struct ore_io_state	 *ios_read_4_write;
	const struct _ore_r4w_op *r4w;

	/* Variable array of size numdevs */
	unsigned numdevs;
	struct ore_per_dev_state {
		struct osd_request *or;
		struct bio *bio;
		loff_t offset;
		unsigned length;
		unsigned last_sgs_total;
		unsigned dev;
		struct osd_sg_entry *sglist;
		unsigned cur_sg;
	} per_dev[];
};

static inline unsigned ore_io_state_size(unsigned numdevs)
{
	return sizeof(struct ore_io_state) +
		sizeof(struct ore_per_dev_state) * numdevs;
}

/* ore.c */
int ore_verify_layout(unsigned total_comps, struct ore_layout *layout);
void ore_calc_stripe_info(struct ore_layout *layout, u64 file_offset,
			  u64 length, struct ore_striping_info *si);
int ore_get_rw_state(struct ore_layout *layout, struct ore_components *comps,
		     bool is_reading, u64 offset, u64 length,
		     struct ore_io_state **ios);
int ore_get_io_state(struct ore_layout *layout, struct ore_components *comps,
		     struct ore_io_state **ios);
void ore_put_io_state(struct ore_io_state *ios);

typedef void (*ore_on_dev_error)(struct ore_io_state *ios, struct ore_dev *od,
	unsigned dev_index, enum osd_err_priority oep,
	u64 dev_offset, u64  dev_len);
int ore_check_io(struct ore_io_state *ios, ore_on_dev_error rep);

int ore_create(struct ore_io_state *ios);
int ore_remove(struct ore_io_state *ios);
int ore_write(struct ore_io_state *ios);
int ore_read(struct ore_io_state *ios);
int ore_truncate(struct ore_layout *layout, struct ore_components *comps,
		 u64 size);

int extract_attr_from_ios(struct ore_io_state *ios, struct osd_attr *attr);

extern const struct osd_attr g_attr_logical_length;

#endif
