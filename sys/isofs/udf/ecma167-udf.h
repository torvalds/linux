/*	$OpenBSD: ecma167-udf.h,v 1.10 2022/01/11 03:13:59 jsg Exp $	*/
/* $NetBSD: ecma167-udf.h,v 1.10 2008/06/24 15:30:33 reinoud Exp $ */

/*-
 * Copyright (c) 2003, 2004, 2005, 2006, 2008 Reinoud Zandijk
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
 * 
 * Extended and adapted for UDFv2.50+ bij Reinoud Zandijk based on the
 * original by Scott Long.
 * 
 * 20030508 Made some small typo and explanatory comments
 * 20030510 Added UDF 2.01 structures
 * 20030519 Added/correct comments on multi-partitioned logical volume space
 * 20050616 Added pseudo overwrite
 * 20050624 Added the missing extended attribute types and `magic values'.
 * 20051106 Reworked some implementation use parts
 *
 */


#ifndef _FS_UDF_ECMA167_UDF_H_
#define _FS_UDF_ECMA167_UDF_H_


/* ecma167-udf.h */

/* Volume recognition sequence ECMA 167 rev. 3 16.1 */
struct vrs_desc {
	uint8_t			struct_type;
	uint8_t			identifier[5];
	uint8_t			version;
	uint8_t			data[2041];
} __packed;


#define VRS_NSR02		"NSR02"
#define VRS_NSR03		"NSR03"
#define VRS_BEA01		"BEA01"
#define VRS_TEA01		"TEA01"
#define VRS_CD001		"CD001"
#define VRS_CDW02		"CDW02"


/* Structure/definitions/constants a la ECMA 167 rev. 3 */


#define MAX_TAGID_VOLUMES 9
/* Tag identifiers */
enum {
	TAGID_SPARING_TABLE = 	  0,
	TAGID_PRI_VOL =		  1,
	TAGID_ANCHOR =		  2,
	TAGID_VOL = 		  3,
	TAGID_IMP_VOL =		  4,
	TAGID_PARTITION =	  5,
	TAGID_LOGVOL =		  6,
	TAGID_UNALLOC_SPACE =	  7,
	TAGID_TERM =		  8,
	TAGID_LOGVOL_INTEGRITY=	  9,
	TAGID_FSD =		256,
	TAGID_FID =		257,
	TAGID_ALLOCEXTENT = 	258,
	TAGID_INDIRECTENTRY =	259,
	TAGID_ICB_TERM =	260,
	TAGID_FENTRY =		261,
	TAGID_EXTATTR_HDR =	262,
	TAGID_UNALL_SP_ENTRY =	263,
	TAGID_SPACE_BITMAP = 	264,
	TAGID_PART_INTEGRITY = 	265,
	TAGID_EXTFENTRY =	266,
	TAGID_MAX =		266
};


enum {
	UDF_DOMAIN_FLAG_HARD_WRITE_PROTECT = 1,
	UDF_DOMAIN_FLAG_SOFT_WRITE_PROTECT = 2
};


enum {
	UDF_ACCESSTYPE_NOT_SPECIFIED   = 0,	/* unknown				*/
	UDF_ACCESSTYPE_PSEUDO_OVERWITE = 0,	/* pseudo overwritable, e.g. BD-R's LOW */
	UDF_ACCESSTYPE_READ_ONLY       = 1,	/* really only readable			*/
	UDF_ACCESSTYPE_WRITE_ONCE      = 2,	/* write once and you're done		*/
	UDF_ACCESSTYPE_REWRITABLE      = 3,	/* may need extra work to rewrite	*/
	UDF_ACCESSTYPE_OVERWRITABLE    = 4	/* no limits on rewriting; e.g. harddisc*/
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
#define UDF_DESC_TAG_LENGTH 16


/* Recorded Address [4/7.1] */
struct lb_addr {			/* within partition space */
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
	uint32_t	lb_num;
} __packed;


/* Long Allocation Descriptor [4/14.14.2] */
struct UDF_ADImp_use {
	uint16_t	flags;
	uint32_t	unique_id;
} __packed;
#define UDF_ADIMP_FLAGS_EXTENT_ERASED 1


struct long_ad {
	uint32_t	len;
	struct lb_addr	loc;			/* within a logical volume mapped partition space !! */
	union {
		uint8_t	bytes[6];
		struct UDF_ADImp_use im_used;
	} __packed impl;
} __packed;
#define longad_uniqueid impl.im_used.unique_id


/* Extended Allocation Descriptor [4/14.14.3] ; identifies an extent of allocation descriptors ; also in UDF ? */
struct ext_ad {
	uint32_t	ex_len;
	uint32_t	rec_len;
	uint32_t	inf_len;
	struct lb_addr	ex_loc;
	uint8_t		reserved[2];
} __packed;


/* ICB : Information Control Block; positioning */
union icb {
	struct short_ad	s_ad;
	struct long_ad	l_ad;
	struct ext_ad	e_ad;
} __packed;


/* short/long/ext extent have flags encoded in length */
#define UDF_EXT_ALLOCATED              (0<<30)
#define UDF_EXT_FREED                  (1<<30)
#define UDF_EXT_ALLOCATED_BUT_NOT_USED (1<<30)
#define UDF_EXT_FREE                   (2<<30)
#define UDF_EXT_REDIRECT               (3<<30)
#define UDF_EXT_FLAGS(len) ((len) & (3<<30))
#define UDF_EXT_LEN(len)   ((len) & ((1<<30)-1))
#define UDF_EXT_MAXLEN     ((1<<30)-1)


/* Character set spec [1/7.2.1] */
struct charspec {
	uint8_t		type;
	uint8_t		inf[63];
} __packed;


struct pathcomp {
	uint8_t		type;
	uint8_t		l_ci;
	uint16_t	comp_filever;
	uint8_t		ident[256];
} __packed;
#define	UDF_PATH_COMP_SIZE 4
#define UDF_PATH_COMP_RESERVED		0
#define UDF_PATH_COMP_ROOT		1
#define UDF_PATH_COMP_MOUNTROOT		2
#define UDF_PATH_COMP_PARENTDIR		3
#define UDF_PATH_COMP_CURDIR		4
#define UDF_PATH_COMP_NAME		5


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
#define UDF_TIMESTAMP_SIZE 12


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
#define UDF_ICB_TAG_FLAGS_ALLOC_MASK	0x03
#define UDF_ICB_SHORT_ALLOC		0x00
#define UDF_ICB_LONG_ALLOC		0x01
#define UDF_ICB_EXT_ALLOC		0x02
#define UDF_ICB_INTERN_ALLOC		0x03

#define UDF_ICB_TAG_FLAGS_DIRORDERED	(1<< 3)
#define UDF_ICB_TAG_FLAGS_NONRELOC	(1<< 4)
#define UDF_ICB_TAG_FLAGS_CONTIGUOUS	(1<< 9)
#define UDF_ICB_TAG_FLAGS_MULTIPLEVERS	(1<<12)

#define	UDF_ICB_TAG_FLAGS_SETUID	(1<< 6)
#define	UDF_ICB_TAG_FLAGS_SETGID	(1<< 7)
#define	UDF_ICB_TAG_FLAGS_STICKY	(1<< 8)

#define UDF_ICB_FILETYPE_UNKNOWN	  0
#define UDF_ICB_FILETYPE_UNALLOCSPACE	  1
#define UDF_ICB_FILETYPE_PARTINTEGRITY    2
#define UDF_ICB_FILETYPE_INDIRECTENTRY	  3
#define UDF_ICB_FILETYPE_DIRECTORY	  4
#define UDF_ICB_FILETYPE_RANDOMACCESS	  5
#define UDF_ICB_FILETYPE_BLOCKDEVICE	  6
#define UDF_ICB_FILETYPE_CHARDEVICE	  7
#define UDF_ICB_FILETYPE_EXTATTRREC	  8
#define UDF_ICB_FILETYPE_FIFO		  9
#define UDF_ICB_FILETYPE_SOCKET		 10
#define UDF_ICB_FILETYPE_TERM		 11
#define UDF_ICB_FILETYPE_SYMLINK	 12
#define UDF_ICB_FILETYPE_STREAMDIR	 13
#define UDF_ICB_FILETYPE_VAT		248
#define UDF_ICB_FILETYPE_REALTIME	249
#define UDF_ICB_FILETYPE_META_MAIN	250
#define UDF_ICB_FILETYPE_META_MIRROR	251


/* Anchor Volume Descriptor Pointer [3/10.2] */
struct anchor_vdp {
	struct desc_tag		tag;
	struct extent_ad	main_vds_ex;		/* to main volume descriptor set      ; 16 sectors min */
	struct extent_ad	reserve_vds_ex;		/* copy of main volume descriptor set ; 16 sectors min */
} __packed;


/* Volume Descriptor Pointer [3/10.3] */
struct vol_desc_ptr {
	struct desc_tag		tag;			/* use for extending the volume descriptor space */
	uint32_t		vds_number;
	struct extent_ad	next_vds_ex;		/* points to the next block for volume descriptor space */
} __packed;


/* Primary Volume Descriptor [3/10.1] */
struct pri_vol_desc {
	struct desc_tag		tag;
	uint32_t		seq_num;		/* MAX prevail */
	uint32_t		pvd_num;		/* assigned by author; 0 is special as in it may only occur once */
	char			vol_id[32];		/* KEY ; main identifier of this disc */
	uint16_t		vds_num;		/* volume descriptor number; i.e. what volume number is it */
	uint16_t		max_vol_seq;		/* maximum volume descriptor number known */
	uint16_t		ichg_lvl;
	uint16_t		max_ichg_lvl;
	uint32_t		charset_list;
	uint32_t		max_charset_list;
	char			volset_id[128];		/* KEY ; if part of a multi-disc set or a band of volumes */
	struct charspec		desc_charset;		/* KEY according to ECMA 167 */
	struct charspec		explanatory_charset;
	struct extent_ad	vol_abstract;
	struct extent_ad	vol_copyright;
	struct regid		app_id;
	struct timestamp	time;
	struct regid		imp_id;
	uint8_t			imp_use[64];
	uint32_t		prev_vds_loc;		/* location of predecessor _lov ? */
	uint16_t		flags;			/* bit 0 : if set indicates volume set name is meaningful */
	uint8_t			reserved[22];
} __packed;


/* UDF specific implementation use part of the implementation use volume descriptor */
struct udf_lv_info {
	struct charspec		lvi_charset;
	char			logvol_id[128];

	char			lvinfo1[36];
	char			lvinfo2[36];
	char			lvinfo3[36];

	struct regid		impl_id;
	uint8_t			impl_use[128];
} __packed;


/* Implementation use Volume Descriptor */
struct impvol_desc {
	struct desc_tag		tag;
	uint32_t		seq_num;
	struct regid		impl_id;
	union {
		struct udf_lv_info	lv_info;
		char			impl_use[460];
	} __packed _impl_use;
} __packed;


/* Logical Volume Descriptor [3/10.6] */
struct logvol_desc {
	struct desc_tag		tag;
	uint32_t		seq_num;		/* MAX prevail */
	struct charspec		desc_charset;		/* KEY */
	char			logvol_id[128];		/* KEY */
	uint32_t		lb_size;
	struct regid		domain_id;
	union {
		struct long_ad	fsd_loc;		/* to fileset descriptor SEQUENCE */
		uint8_t		logvol_content_use[16];
	} __packed _lvd_use;
	uint32_t		mt_l;			/* Partition map length */
	uint32_t		n_pm;			/* Number of partition maps */
	struct regid		imp_id;
	uint8_t			imp_use[128];
	struct extent_ad	integrity_seq_loc;
	uint8_t			maps[1];
} __packed;
#define lv_fsd_loc _lvd_use.fsd_loc

#define UDF_INTEGRITY_OPEN	0
#define UDF_INTEGRITY_CLOSED	1


#define	UDF_PMAP_SIZE	64

/* Type 1 Partition Map [3/10.7.2] */
struct part_map_1 {
	uint8_t			type;
	uint8_t			len;
	uint16_t		vol_seq_num;
	uint16_t		part_num;
} __packed;


/* Type 2 Partition Map [3/10.7.3] */
struct part_map_2 {
	uint8_t			type;
	uint8_t			len;
	uint8_t			reserved[2];
	struct regid		part_id;
	uint16_t		vol_seq_num;
	uint16_t		part_num;
	uint8_t			reserved2[24];
} __packed;


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
	uint8_t			n_st;		/* Number of redundant sparing tables range 1-4 */
	uint8_t			reserved1;
	uint32_t		st_size;	/* size of EACH sparing table  */
	uint32_t		st_loc[1];	/* locations of sparing tables */
} __packed;


/* Metadata Partition Map [UDF 2.50/2.2.10] */
struct part_map_meta {
	uint8_t			type;
	uint8_t			len;
	uint8_t			reserved[2];
	struct regid		id;
	uint16_t		vol_seq_num;
	uint16_t		part_num;
	uint32_t		meta_file_lbn;		/* logical block number for file entry within part_num */
	uint32_t		meta_mirror_file_lbn;
	uint32_t		meta_bitmap_file_lbn;
	uint32_t		alloc_unit_size;	/* allocation unit size in blocks */
	uint16_t		alignment_unit_size;	/* alignment necessary in blocks  */
	uint8_t			flags;
	uint8_t			reserved1[5];
} __packed;
#define METADATA_DUPLICATED	1


union udf_pmap {
	uint8_t			data[UDF_PMAP_SIZE];
	struct part_map_1	pm1;
	struct part_map_2	pm2;
	struct part_map_virt	pmv;
	struct part_map_spare	pms;
	struct part_map_meta	pmm;
} __packed;


/* Sparing Map Entry [UDF 2.01/2.2.11] */
struct spare_map_entry {
	uint32_t		org;			/* partition relative address  */
	uint32_t		map;			/* absolute disc address (!) can be in partition, but doesn't have to be */
} __packed;


/* Sparing Table [UDF 2.01/2.2.11] */
struct udf_sparing_table {
	struct desc_tag		tag;
	struct regid		id;
	uint16_t		rt_l;			/* Relocation Table len */
	uint8_t			reserved[2];
	uint32_t		seq_num;
	struct spare_map_entry	entries[1];
} __packed;


#define UDF_NO_PREV_VAT		0xffffffff
/* UDF 1.50 VAT suffix [UDF 2.2.10 (UDF 1.50 spec)] */
struct udf_oldvat_tail {
	struct regid		id;			/* "*UDF Virtual Alloc Tbl" */
	uint32_t		prev_vat;
} __packed;


/* VAT table [UDF 2.0.1/2.2.10] */
struct udf_vat {
	uint16_t		header_len;
	uint16_t		impl_use_len;
	char			logvol_id[128];		/* newer version of the LVD one */
	uint32_t		prev_vat;
	uint32_t		num_files;
	uint32_t		num_directories;
	uint16_t		min_udf_readver;
	uint16_t		min_udf_writever;
	uint16_t		max_udf_writever;
	uint16_t		reserved;
	uint8_t			data[1];		/* impl.use followed by VAT entries (uint32_t) */
} __packed;


/* Space bitmap descriptor as found in the partition header descriptor */
struct space_bitmap_desc {
	struct desc_tag		tag;			/* TagId 264			*/
	uint32_t		num_bits;		/* number of bits		*/
	uint32_t		num_bytes;		/* bytes that contain it	*/
	uint8_t			data[1];
} __packed;


/* Unalloc space entry as found in the partition header descriptor */
struct space_entry_desc {
	struct desc_tag		tag;			/* TagId 263			*/
	struct icb_tag		icbtag;			/* type 1			*/
	uint32_t		l_ad;			/* in bytes			*/
	uint8_t			entry[1];
} __packed;


/* Partition header descriptor; in the contents_use of part_desc */
struct part_hdr_desc {
	struct short_ad		unalloc_space_table;
	struct short_ad		unalloc_space_bitmap;
	struct short_ad		part_integrity_table;	/* has to be ZERO for UDF */
	struct short_ad		freed_space_table;
	struct short_ad		freed_space_bitmap;
	uint8_t			reserved[88];
} __packed;


/* Partition Descriptor [3/10.5] */
struct part_desc {
	struct desc_tag		tag;
	uint32_t		seq_num;		/* MAX prevailing */
	uint16_t		flags;			/* bit 0 : if set the space is allocated */
	uint16_t		part_num;		/* KEY */
	struct regid		contents;
	union {
		struct part_hdr_desc	part_hdr;
		uint8_t			contents_use[128];
	} _impl_use;
	uint32_t		access_type;		/* R/W, WORM etc. */
	uint32_t		start_loc;		/* start of partition with given length */
	uint32_t		part_len;
	struct regid		imp_id;
	uint8_t			imp_use[128];
	uint8_t			reserved[156];
} __packed;
#define pd_part_hdr _impl_use.part_hdr
#define UDF_PART_FLAG_ALLOCATED		1


/* Unallocated Space Descriptor (UDF 2.01/2.2.5) */
struct unalloc_sp_desc {
	struct desc_tag		tag;
	uint32_t		seq_num;	/* MAX prevailing */
	uint32_t		alloc_desc_num;
	struct extent_ad	alloc_desc[1];
} __packed;


/* Logical Volume Integrity Descriptor [3/30.10] */
struct logvolhdr {
	uint64_t		next_unique_id;
	/* rest reserved */
} __packed;


struct udf_logvol_info {
	struct regid		impl_id;
	uint32_t		num_files;
	uint32_t		num_directories;
	uint16_t		min_udf_readver;
	uint16_t		min_udf_writever;
	uint16_t		max_udf_writever;
} __packed;


struct logvol_int_desc {
	struct desc_tag		tag;
	struct timestamp	time;
	uint32_t		integrity_type;
	struct extent_ad	next_extent;
	union {
		struct logvolhdr  logvolhdr;
		int8_t		  reserved[32];
	} __packed _impl_use;
	uint32_t		num_part;
	uint32_t		l_iu;
	uint32_t		tables[1];	/* Freespace table, Sizetable, Implementation use */
} __packed;
#define lvint_next_unique_id _impl_use.logvolhdr.next_unique_id


/* File Set Descriptor [4/14.1] */
struct fileset_desc {
	struct desc_tag		tag;
	struct timestamp	time;
	uint16_t		ichg_lvl;
	uint16_t		max_ichg_lvl;
	uint32_t		charset_list;
	uint32_t		max_charset_list;
	uint32_t		fileset_num;			/* key! */
	uint32_t		fileset_desc_num;
	struct charspec		logvol_id_charset;
	char			logvol_id[128];			/* for recovery			*/
	struct charspec		fileset_charset;
	char			fileset_id[32];			/* Mountpoint !!		*/
	char			copyright_file_id[32];
	char			abstract_file_id[32];
	struct long_ad		rootdir_icb;			/* to rootdir; icb->virtual ?	*/
	struct regid		domain_id;
	struct long_ad		next_ex;			/* to the next fileset_desc extent */
	struct long_ad		streamdir_icb;			/* streamdir; needed?		*/
	uint8_t			reserved[32];
} __packed;


/* File Identifier Descriptor [4/14.4] */
struct fileid_desc {
	struct desc_tag		tag;
	uint16_t		file_version_num;
	uint8_t			file_char;
	uint8_t			l_fi;	/* Length of file identifier area */
	struct long_ad		icb;
	uint16_t		l_iu;	/* Length of implementation use area */
	uint8_t			data[0];
} __packed;
#define	UDF_FID_SIZE	38
#define	UDF_FILE_CHAR_VIS	(1 << 0) /* Invisible */
#define	UDF_FILE_CHAR_DIR	(1 << 1) /* Directory */
#define	UDF_FILE_CHAR_DEL	(1 << 2) /* Deleted */
#define	UDF_FILE_CHAR_PAR	(1 << 3) /* Parent Directory */
#define	UDF_FILE_CHAR_META	(1 << 4) /* Stream metadata */


/* Extended attributes [4/14.10.1] */
struct extattrhdr_desc {
	struct desc_tag		tag;
	uint32_t		impl_attr_loc;	/* offsets within this descriptor */
	uint32_t		appl_attr_loc;	/* ditto */
} __packed;
#define UDF_IMPL_ATTR_LOC_NOT_PRESENT 0xffffffff
#define UDF_APPL_ATTR_LOC_NOT_PRESENT 0xffffffff


/* Extended attribute entry [4/48.10.2] */
struct extattr_entry {
	uint32_t		type;
	uint8_t			subtype;
	uint8_t			reserved[3];
	uint32_t		a_l;
} __packed;


/* Extended attribute entry; type 2048 [4/48.10.8] */
struct impl_extattr_entry {
	struct extattr_entry    hdr;
	uint32_t		iu_l;
	struct regid		imp_id;
	uint8_t			data[1];
} __packed;


/* Extended attribute entry; type 65 536 [4/48.10.9] */
struct appl_extattr_entry {
	struct extattr_entry    hdr;
	uint32_t		au_l;
	struct regid		appl_id;
	uint8_t			data[1];
} __packed;


/* File Times attribute entry; type 5 or type 6 [4/48.10.5], [4/48.10.6] */
struct filetimes_extattr_entry {
	struct extattr_entry    hdr;
	uint32_t		d_l;		/* length of times[] data following */
	uint32_t		existence;	/* bitmask */
	struct timestamp	times[1];	/* in order of ascending bits */
} __packed;
#define UDF_FILETIMES_ATTR_NO	5
#define UDF_FILETIMES_FILE_CREATION	1
#define UDF_FILETIMES_FILE_DELETION	4
#define UDF_FILETIMES_FILE_EFFECTIVE	8
#define UDF_FILETIMES_FILE_BACKUPED	16
#define UDF_FILETIMES_ATTR_SIZE(no)	(20 + (no)*sizeof(struct timestamp))


/* Device Specification Extended Attribute [4/4.10.7] */
struct device_extattr_entry {
	struct extattr_entry	hdr;
	uint32_t		iu_l;		/* length of implementation use */
	uint32_t		major;
	uint32_t		minor;
	uint8_t			data[1];	/* UDF: if nonzero length, contain developer ID regid */
} __packed;
#define UDF_DEVICESPEC_ATTR_NO	12


/* VAT LV extension Extended Attribute [UDF 3.3.4.5.1.3] 1.50 errata */
struct vatlvext_extattr_entry {
	uint64_t		unique_id_chk;	/* needs to be copy of ICB's */
	uint32_t		num_files;
	uint32_t		num_directories;
	char			logvol_id[128];	/* replaces logvol name */
} __packed;


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


/* Extended File Entry [4/48.17] */
struct extfile_entry {
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
	uint64_t		obj_size;
	uint64_t		logblks_rec;
	struct timestamp	atime;
	struct timestamp	mtime;
	struct timestamp	ctime;
	struct timestamp	attrtime;
	uint32_t		ckpoint;
	uint32_t		reserved1;
	struct long_ad		ex_attr_icb;
	struct long_ad		streamdir_icb;
	struct regid		imp_id;
	uint64_t		unique_id;
	uint32_t		l_ea;	/* Length of extended attribute area */
	uint32_t		l_ad;	/* Length of allocation descriptors */
	uint8_t			data[1];
} __packed;
#define	UDF_EXTFENTRY_SIZE	216


/* Indirect entry [ecma 48.7] */
struct indirect_entry {
	struct desc_tag		tag;
	struct icb_tag		icbtag;
	struct long_ad		indirect_icb;
} __packed;


/* Allocation extent descriptor [ecma 48.5] */
struct alloc_ext_entry {
	struct desc_tag		tag;
	uint32_t		prev_entry;
	uint32_t		l_ad;
	uint8_t			data[1];
} __packed;


union dscrptr {
	struct desc_tag		 tag;
	struct anchor_vdp	 avdp;
	struct vol_desc_ptr	 vdp;
	struct pri_vol_desc	 pvd;
	struct logvol_desc	 lvd;
	struct unalloc_sp_desc	 usd;
	struct logvol_int_desc	 lvid;
	struct impvol_desc	 ivd;
	struct part_desc	 pd;
	struct fileset_desc	 fsd;
	struct fileid_desc	 fid;
	struct file_entry	 fe;
	struct extfile_entry	 efe;
	struct extattrhdr_desc	 eahd;
	struct indirect_entry	 inde;
	struct alloc_ext_entry	 aee;
	struct udf_sparing_table spt;
	struct space_bitmap_desc sbd;
	struct space_entry_desc	 sed;
} __packed;

/* Useful defines */

#define	GETICB(ad_type, fentry, offset)	\
	(struct ad_type *)&fentry->data[offset]

#define	GETICBLEN(ad_type, icb)	letoh32(((struct ad_type *)(icb))->len)

#endif /* !_FS_UDF_ECMA167_UDF_H_ */

