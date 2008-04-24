/*
 * misc.c
 *
 * PURPOSE
 *	Miscellaneous routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1998 Dave Boynton
 *  (C) 1998-2004 Ben Fennema
 *  (C) 1999-2000 Stelias Computing Inc
 *
 * HISTORY
 *
 *  04/19/99 blf  partial support for reading/writing specific EA's
 */

#include "udfdecl.h"

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/crc-itu-t.h>

#include "udf_i.h"
#include "udf_sb.h"

struct buffer_head *udf_tgetblk(struct super_block *sb, int block)
{
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_VARCONV))
		return sb_getblk(sb, udf_fixed_to_variable(block));
	else
		return sb_getblk(sb, block);
}

struct buffer_head *udf_tread(struct super_block *sb, int block)
{
	if (UDF_QUERY_FLAG(sb, UDF_FLAG_VARCONV))
		return sb_bread(sb, udf_fixed_to_variable(block));
	else
		return sb_bread(sb, block);
}

struct genericFormat *udf_add_extendedattr(struct inode *inode, uint32_t size,
					   uint32_t type, uint8_t loc)
{
	uint8_t *ea = NULL, *ad = NULL;
	int offset;
	uint16_t crclen;
	struct udf_inode_info *iinfo = UDF_I(inode);

	ea = iinfo->i_ext.i_data;
	if (iinfo->i_lenEAttr) {
		ad = iinfo->i_ext.i_data + iinfo->i_lenEAttr;
	} else {
		ad = ea;
		size += sizeof(struct extendedAttrHeaderDesc);
	}

	offset = inode->i_sb->s_blocksize - udf_file_entry_alloc_offset(inode) -
		iinfo->i_lenAlloc;

	/* TODO - Check for FreeEASpace */

	if (loc & 0x01 && offset >= size) {
		struct extendedAttrHeaderDesc *eahd;
		eahd = (struct extendedAttrHeaderDesc *)ea;

		if (iinfo->i_lenAlloc)
			memmove(&ad[size], ad, iinfo->i_lenAlloc);

		if (iinfo->i_lenEAttr) {
			/* check checksum/crc */
			if (eahd->descTag.tagIdent !=
					cpu_to_le16(TAG_IDENT_EAHD) ||
			    le32_to_cpu(eahd->descTag.tagLocation) !=
					iinfo->i_location.logicalBlockNum)
				return NULL;
		} else {
			struct udf_sb_info *sbi = UDF_SB(inode->i_sb);

			size -= sizeof(struct extendedAttrHeaderDesc);
			iinfo->i_lenEAttr +=
				sizeof(struct extendedAttrHeaderDesc);
			eahd->descTag.tagIdent = cpu_to_le16(TAG_IDENT_EAHD);
			if (sbi->s_udfrev >= 0x0200)
				eahd->descTag.descVersion = cpu_to_le16(3);
			else
				eahd->descTag.descVersion = cpu_to_le16(2);
			eahd->descTag.tagSerialNum =
					cpu_to_le16(sbi->s_serial_number);
			eahd->descTag.tagLocation = cpu_to_le32(
					iinfo->i_location.logicalBlockNum);
			eahd->impAttrLocation = cpu_to_le32(0xFFFFFFFF);
			eahd->appAttrLocation = cpu_to_le32(0xFFFFFFFF);
		}

		offset = iinfo->i_lenEAttr;
		if (type < 2048) {
			if (le32_to_cpu(eahd->appAttrLocation) <
					iinfo->i_lenEAttr) {
				uint32_t aal =
					le32_to_cpu(eahd->appAttrLocation);
				memmove(&ea[offset - aal + size],
					&ea[aal], offset - aal);
				offset -= aal;
				eahd->appAttrLocation =
						cpu_to_le32(aal + size);
			}
			if (le32_to_cpu(eahd->impAttrLocation) <
					iinfo->i_lenEAttr) {
				uint32_t ial =
					le32_to_cpu(eahd->impAttrLocation);
				memmove(&ea[offset - ial + size],
					&ea[ial], offset - ial);
				offset -= ial;
				eahd->impAttrLocation =
						cpu_to_le32(ial + size);
			}
		} else if (type < 65536) {
			if (le32_to_cpu(eahd->appAttrLocation) <
					iinfo->i_lenEAttr) {
				uint32_t aal =
					le32_to_cpu(eahd->appAttrLocation);
				memmove(&ea[offset - aal + size],
					&ea[aal], offset - aal);
				offset -= aal;
				eahd->appAttrLocation =
						cpu_to_le32(aal + size);
			}
		}
		/* rewrite CRC + checksum of eahd */
		crclen = sizeof(struct extendedAttrHeaderDesc) - sizeof(tag);
		eahd->descTag.descCRCLength = cpu_to_le16(crclen);
		eahd->descTag.descCRC = cpu_to_le16(crc_itu_t(0, (char *)eahd +
						sizeof(tag), crclen));
		eahd->descTag.tagChecksum = udf_tag_checksum(&eahd->descTag);
		iinfo->i_lenEAttr += size;
		return (struct genericFormat *)&ea[offset];
	}
	if (loc & 0x02)
		;

	return NULL;
}

struct genericFormat *udf_get_extendedattr(struct inode *inode, uint32_t type,
					   uint8_t subtype)
{
	struct genericFormat *gaf;
	uint8_t *ea = NULL;
	uint32_t offset;
	struct udf_inode_info *iinfo = UDF_I(inode);

	ea = iinfo->i_ext.i_data;

	if (iinfo->i_lenEAttr) {
		struct extendedAttrHeaderDesc *eahd;
		eahd = (struct extendedAttrHeaderDesc *)ea;

		/* check checksum/crc */
		if (eahd->descTag.tagIdent !=
				cpu_to_le16(TAG_IDENT_EAHD) ||
		    le32_to_cpu(eahd->descTag.tagLocation) !=
				iinfo->i_location.logicalBlockNum)
			return NULL;

		if (type < 2048)
			offset = sizeof(struct extendedAttrHeaderDesc);
		else if (type < 65536)
			offset = le32_to_cpu(eahd->impAttrLocation);
		else
			offset = le32_to_cpu(eahd->appAttrLocation);

		while (offset < iinfo->i_lenEAttr) {
			gaf = (struct genericFormat *)&ea[offset];
			if (le32_to_cpu(gaf->attrType) == type &&
					gaf->attrSubtype == subtype)
				return gaf;
			else
				offset += le32_to_cpu(gaf->attrLength);
		}
	}

	return NULL;
}

/*
 * udf_read_tagged
 *
 * PURPOSE
 *	Read the first block of a tagged descriptor.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
struct buffer_head *udf_read_tagged(struct super_block *sb, uint32_t block,
				    uint32_t location, uint16_t *ident)
{
	tag *tag_p;
	struct buffer_head *bh = NULL;

	/* Read the block */
	if (block == 0xFFFFFFFF)
		return NULL;

	bh = udf_tread(sb, block);
	if (!bh) {
		udf_debug("block=%d, location=%d: read failed\n",
			  block, location);
		return NULL;
	}

	tag_p = (tag *)(bh->b_data);

	*ident = le16_to_cpu(tag_p->tagIdent);

	if (location != le32_to_cpu(tag_p->tagLocation)) {
		udf_debug("location mismatch block %u, tag %u != %u\n",
			  block, le32_to_cpu(tag_p->tagLocation), location);
		goto error_out;
	}

	/* Verify the tag checksum */
	if (udf_tag_checksum(tag_p) != tag_p->tagChecksum) {
		printk(KERN_ERR "udf: tag checksum failed block %d\n", block);
		goto error_out;
	}

	/* Verify the tag version */
	if (tag_p->descVersion != cpu_to_le16(0x0002U) &&
	    tag_p->descVersion != cpu_to_le16(0x0003U)) {
		udf_debug("tag version 0x%04x != 0x0002 || 0x0003 block %d\n",
			  le16_to_cpu(tag_p->descVersion), block);
		goto error_out;
	}

	/* Verify the descriptor CRC */
	if (le16_to_cpu(tag_p->descCRCLength) + sizeof(tag) > sb->s_blocksize ||
	    le16_to_cpu(tag_p->descCRC) == crc_itu_t(0,
					bh->b_data + sizeof(tag),
					le16_to_cpu(tag_p->descCRCLength)))
		return bh;

	udf_debug("Crc failure block %d: crc = %d, crclen = %d\n", block,
	    le16_to_cpu(tag_p->descCRC), le16_to_cpu(tag_p->descCRCLength));

error_out:
	brelse(bh);
	return NULL;
}

struct buffer_head *udf_read_ptagged(struct super_block *sb, kernel_lb_addr loc,
				     uint32_t offset, uint16_t *ident)
{
	return udf_read_tagged(sb, udf_get_lb_pblock(sb, loc, offset),
			       loc.logicalBlockNum + offset, ident);
}

void udf_update_tag(char *data, int length)
{
	tag *tptr = (tag *)data;
	length -= sizeof(tag);

	tptr->descCRCLength = cpu_to_le16(length);
	tptr->descCRC = cpu_to_le16(crc_itu_t(0, data + sizeof(tag), length));
	tptr->tagChecksum = udf_tag_checksum(tptr);
}

void udf_new_tag(char *data, uint16_t ident, uint16_t version, uint16_t snum,
		 uint32_t loc, int length)
{
	tag *tptr = (tag *)data;
	tptr->tagIdent = cpu_to_le16(ident);
	tptr->descVersion = cpu_to_le16(version);
	tptr->tagSerialNum = cpu_to_le16(snum);
	tptr->tagLocation = cpu_to_le32(loc);
	udf_update_tag(data, length);
}

u8 udf_tag_checksum(const tag *t)
{
	u8 *data = (u8 *)t;
	u8 checksum = 0;
	int i;
	for (i = 0; i < sizeof(tag); ++i)
		if (i != 4) /* position of checksum */
			checksum += data[i];
	return checksum;
}
