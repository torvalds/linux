/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001, 2002 Scott Long <scottl@freebsd.org>
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

/* ecma167-udf.h */
/* Structure/definitions/constants a la ECMA 167 rev. 3 */

/* Tag identifiers */
enum {
	TAGID_PRI_VOL =		1,
	TAGID_ANCHOR =		2,
	TAGID_VOL = 		3,
	TAGID_IMP_VOL =		4,
	TAGID_PARTITION =	5,
	TAGID_LOGVOL =		6,
	TAGID_UNALLOC_SPACE =	7,
	TAGID_TERM =		8,
	TAGID_LOGVOL_INTEGRITY = 9,
	TAGID_FSD =		256,
	TAGID_FID =		257,
	TAGID_FENTRY =		261
};

/* Descriptor tag [3/7.2] */
struct desc_tag {
	uint16_t	id;
	uint16_t	descriptor_ver;
	uint8_t		cksum;
	uint8_t		reserved;
	uint16_t	serial_num;
	uint16_t	desc_crc;
	uint16_t	desc_crc_len;
	uint32_t	tag_loc;
} __packed;

/* Recorded Address [4/7.1] */
struct lb_addr {
	uint32_t	lb_num;
	uint16_t	part_num;
} __packed;

/* Extent Descriptor [3/7.1] */
struct extent_ad {
	uint32_t	len;
	uint32_t	loc;
} __packed;

/* Short Allocation Descriptor [4/14.14.1] */
struct short_ad {
	uint32_t	len;
	uint32_t	pos;
} __packed;

/* Long Allocation Descriptor [4/14.14.2] */
struct long_ad {
	uint32_t	len;
	struct lb_addr	loc;
	uint16_t	ad_flags;
	uint32_t	ad_id;
} __packed;

/* Extended Allocation Descriptor [4/14.14.3] */
struct ext_ad {
	uint32_t	ex_len;
	uint32_t	rec_len;
	uint32_t	inf_len;
	struct lb_addr	ex_loc;
	uint8_t		reserved[2];
} __packed;

union icb {
	struct short_ad	s_ad;
	struct long_ad	l_ad;
	struct ext_ad	e_ad;
};

/* Character set spec [1/7.2.1] */
struct charspec {
	uint8_t		type;
	uint8_t		inf[63];
} __packed;

/* Timestamp [1/7.3] */
struct timestamp {
	uint16_t	type_tz;
	uint16_t	year;
	uint8_t		month;
	uint8_t		day;
	uint8_t		hour;
	uint8_t		minute;
	uint8_t		second;
	uint8_t		centisec;
	uint8_t		hund_usec;
	uint8_t		usec;
} __packed;

/* Entity Identifier [1/7.4] */
#define	UDF_REGID_ID_SIZE	23
struct regid {
	uint8_t		flags;
	uint8_t		id[UDF_REGID_ID_SIZE];
	uint8_t		id_suffix[8];
} __packed;

/* ICB Tag [4/14.6] */
struct icb_tag {
	uint32_t	prev_num_dirs;
	uint16_t	strat_type;
	uint8_t		strat_param[2];
	uint16_t	max_num_entries;
	uint8_t		reserved;
	uint8_t		file_type;
	struct lb_addr	parent_icb;
	uint16_t	flags;
} __packed;
#define	UDF_ICB_TAG_FLAGS_SETUID	0x40
#define	UDF_ICB_TAG_FLAGS_SETGID	0x80
#define	UDF_ICB_TAG_FLAGS_STICKY	0x100

/* Anchor Volume Descriptor Pointer [3/10.2] */
struct anchor_vdp {
	struct desc_tag		tag;
	struct extent_ad	main_vds_ex;
	struct extent_ad	reserve_vds_ex;
} __packed;

/* Volume Descriptor Pointer [3/10.3] */
struct vol_desc_ptr {
	struct desc_tag		tag;
	uint32_t		vds_number;
	struct extent_ad	next_vds_ex;
} __packed;

/* Primary Volume Descriptor [3/10.1] */
struct pri_vol_desc {
	struct desc_tag		tag;
	uint32_t		seq_num;
	uint32_t		pdv_num;
	char			vol_id[32];
	uint16_t		vds_num;
	uint16_t		max_vol_seq;
	uint16_t		ichg_lvl;
	uint16_t		max_ichg_lvl;
	uint32_t		charset_list;
	uint32_t		max_charset_list;
	char			volset_id[128];
	struct charspec		desc_charset;
	struct charspec		explanatory_charset;
	struct extent_ad	vol_abstract;
	struct extent_ad	vol_copyright;
	struct regid		app_id;
	struct timestamp	time;
	struct regid		imp_id;
	uint8_t			imp_use[64];
	uint32_t		prev_vds_lov;
	uint16_t		flags;
	uint8_t			reserved[22];
} __packed;

/* Logical Volume Descriptor [3/10.6] */
struct logvol_desc {
	struct desc_tag		tag;
	uint32_t		seq_num;
	struct charspec		desc_charset;
	char			logvol_id[128];
	uint32_t		lb_size;
	struct regid		domain_id;
	union {
		struct long_ad	fsd_loc;
		uint8_t		logvol_content_use[16];
	} _lvd_use;
	uint32_t		mt_l; /* Partition map length */
	uint32_t		n_pm; /* Number of partition maps */
	struct regid		imp_id;
	uint8_t			imp_use[128];
	struct extent_ad	integrity_seq_id;
	uint8_t			maps[1];
} __packed;

/* Type 1 Partition Map [3/10.7.2] */
struct part_map_1 {
	uint8_t			type;
	uint8_t			len;
	uint16_t		vol_seq_num;
	uint16_t		part_num;
} __packed;

#define	UDF_PMAP_TYPE1_SIZE	6

/* Type 2 Partition Map [3/10.7.3] */
struct part_map_2 {
	uint8_t			type;
	uint8_t			len;
	uint8_t			part_id[62];
} __packed;

#define	UDF_PMAP_TYPE2_SIZE	64

/* Virtual Partition Map [UDF 2.01/2.2.8] */
struct part_map_virt {
	uint8_t			type;
	uint8_t			len;
	uint8_t			reserved[2];
	struct regid		id;
	uint16_t		vol_seq_num;
	uint16_t		part_num;
	uint8_t			reserved1[24];
} __packed;

/* Sparable Partition Map [UDF 2.01/2.2.9] */
struct part_map_spare {
	uint8_t			type;
	uint8_t			len;
	uint8_t			reserved[2];
	struct regid		id;
	uint16_t		vol_seq_num;
	uint16_t		part_num;
	uint16_t		packet_len;
	uint8_t			n_st;	/* Number of Sparing Tables */
	uint8_t			reserved1;
	uint32_t		st_size;
	uint32_t		st_loc[1];
} __packed;

union udf_pmap {
	struct part_map_1	pm1;
	struct part_map_2	pm2;
	struct part_map_virt	pmv;
	struct part_map_spare	pms;
};

/* Sparing Map Entry [UDF 2.01/2.2.11] */
struct spare_map_entry {
	uint32_t		org;
	uint32_t		map;
} __packed;

/* Sparing Table [UDF 2.01/2.2.11] */
struct udf_sparing_table {
	struct desc_tag		tag;
	struct regid		id;
	uint16_t		rt_l;	/* Relocation Table len */
	uint8_t			reserved[2];
	uint32_t		seq_num;
	struct spare_map_entry	entries[1];
} __packed;

/* Partition Descriptor [3/10.5] */
struct part_desc {
	struct desc_tag	tag;
	uint32_t	seq_num;
	uint16_t	flags;
	uint16_t	part_num;
	struct regid	contents;
	uint8_t		contents_use[128];
	uint32_t	access_type;
	uint32_t	start_loc;
	uint32_t	part_len;
	struct regid	imp_id;
	uint8_t		imp_use[128];
	uint8_t		reserved[156];
} __packed;

/* File Set Descriptor [4/14.1] */
struct fileset_desc {
	struct desc_tag		tag;
	struct timestamp	time;
	uint16_t		ichg_lvl;
	uint16_t		max_ichg_lvl;
	uint32_t		charset_list;
	uint32_t		max_charset_list;
	uint32_t		fileset_num;
	uint32_t		fileset_desc_num;
	struct charspec		logvol_id_charset;
	char			logvol_id[128];
	struct charspec		fileset_charset;
	char			fileset_id[32];
	char			copyright_file_id[32];
	char			abstract_file_id[32];
	struct long_ad		rootdir_icb;
	struct regid		domain_id;
	struct long_ad		next_ex;
	struct long_ad		streamdir_icb;
	uint8_t			reserved[32];
} __packed;

/* File Identifier Descriptor [4/14.4] */
struct fileid_desc {
	struct desc_tag	tag;
	uint16_t	file_num;
	uint8_t		file_char;
	uint8_t		l_fi;	/* Length of file identifier area */
	struct long_ad	icb;
	uint16_t	l_iu;	/* Length of implementation use area */
	uint8_t		data[1];
} __packed;
#define	UDF_FID_SIZE	38
#define	UDF_FILE_CHAR_VIS	(1 << 0) /* Visible */
#define	UDF_FILE_CHAR_DIR	(1 << 1) /* Directory */
#define	UDF_FILE_CHAR_DEL	(1 << 2) /* Deleted */
#define	UDF_FILE_CHAR_PAR	(1 << 3) /* Parent Directory */
#define	UDF_FILE_CHAR_META	(1 << 4) /* Stream metadata */

/* File Entry [4/14.9] */
struct file_entry {
	struct desc_tag		tag;
	struct icb_tag		icbtag;
	uint32_t		uid;
	uint32_t		gid;
	uint32_t		perm;
	uint16_t		link_cnt;
	uint8_t			rec_format;
	uint8_t			rec_disp_attr;
	uint32_t		rec_len;
	uint64_t		inf_len;
	uint64_t		logblks_rec;
	struct timestamp	atime;
	struct timestamp	mtime;
	struct timestamp	attrtime;
	uint32_t		ckpoint;
	struct long_ad		ex_attr_icb;
	struct regid		imp_id;
	uint64_t		unique_id;
	uint32_t		l_ea;	/* Length of extended attribute area */
	uint32_t		l_ad;	/* Length of allocation descriptors */
	uint8_t			data[1];
} __packed;
#define	UDF_FENTRY_SIZE	176
#define	UDF_FENTRY_PERM_USER_MASK	0x07
#define	UDF_FENTRY_PERM_GRP_MASK	0xE0
#define	UDF_FENTRY_PERM_OWNER_MASK	0x1C00

/* Path Component [4/14.16.1] */
struct path_component {
	uint8_t			type;
	uint8_t			length;
	uint16_t		version;
	uint8_t			identifier[1];
} __packed;
#define	UDF_PATH_ROOT		2
#define	UDF_PATH_DOTDOT		3
#define	UDF_PATH_DOT		4
#define	UDF_PATH_PATH		5

union dscrptr {
	struct desc_tag		tag;
	struct anchor_vdp	avdp;
	struct vol_desc_ptr	vdp;
	struct pri_vol_desc	pvd;
	struct logvol_desc	lvd;
	struct part_desc	pd;
	struct fileset_desc	fsd;
	struct fileid_desc	fid;
	struct file_entry	fe;
};

/* Useful defines */

#define	GETICB(ad_type, fentry, offset)	\
	(struct ad_type *)&fentry->data[offset]

#define	GETICBLEN(ad_type, icb)	le32toh(((struct ad_type *)(icb))->len)
