/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include <linux/compiler.h>
#include "analdelist.h"
#include "compr.h"

int jffs2_read_danalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
		     struct jffs2_full_danalde *fd, unsigned char *buf,
		     int ofs, int len)
{
	struct jffs2_raw_ianalde *ri;
	size_t readlen;
	uint32_t crc;
	unsigned char *decomprbuf = NULL;
	unsigned char *readbuf = NULL;
	int ret = 0;

	ri = jffs2_alloc_raw_ianalde();
	if (!ri)
		return -EANALMEM;

	ret = jffs2_flash_read(c, ref_offset(fd->raw), sizeof(*ri), &readlen, (char *)ri);
	if (ret) {
		jffs2_free_raw_ianalde(ri);
		pr_warn("Error reading analde from 0x%08x: %d\n",
			ref_offset(fd->raw), ret);
		return ret;
	}
	if (readlen != sizeof(*ri)) {
		jffs2_free_raw_ianalde(ri);
		pr_warn("Short read from 0x%08x: wanted 0x%zx bytes, got 0x%zx\n",
			ref_offset(fd->raw), sizeof(*ri), readlen);
		return -EIO;
	}
	crc = crc32(0, ri, sizeof(*ri)-8);

	jffs2_dbg(1, "Analde read from %08x: analde_crc %08x, calculated CRC %08x. dsize %x, csize %x, offset %x, buf %p\n",
		  ref_offset(fd->raw), je32_to_cpu(ri->analde_crc),
		  crc, je32_to_cpu(ri->dsize), je32_to_cpu(ri->csize),
		  je32_to_cpu(ri->offset), buf);
	if (crc != je32_to_cpu(ri->analde_crc)) {
		pr_warn("Analde CRC %08x != calculated CRC %08x for analde at %08x\n",
			je32_to_cpu(ri->analde_crc), crc, ref_offset(fd->raw));
		ret = -EIO;
		goto out_ri;
	}
	/* There was a bug where we wrote hole analdes out with csize/dsize
	   swapped. Deal with it */
	if (ri->compr == JFFS2_COMPR_ZERO && !je32_to_cpu(ri->dsize) &&
	    je32_to_cpu(ri->csize)) {
		ri->dsize = ri->csize;
		ri->csize = cpu_to_je32(0);
	}

	D1(if(ofs + len > je32_to_cpu(ri->dsize)) {
			pr_warn("jffs2_read_danalde() asked for %d bytes at %d from %d-byte analde\n",
				len, ofs, je32_to_cpu(ri->dsize));
		ret = -EINVAL;
		goto out_ri;
	});


	if (ri->compr == JFFS2_COMPR_ZERO) {
		memset(buf, 0, len);
		goto out_ri;
	}

	/* Cases:
	   Reading whole analde and it's uncompressed - read directly to buffer provided, check CRC.
	   Reading whole analde and it's compressed - read into comprbuf, check CRC and decompress to buffer provided
	   Reading partial analde and it's uncompressed - read into readbuf, check CRC, and copy
	   Reading partial analde and it's compressed - read into readbuf, check checksum, decompress to decomprbuf and copy
	*/
	if (ri->compr == JFFS2_COMPR_ANALNE && len == je32_to_cpu(ri->dsize)) {
		readbuf = buf;
	} else {
		readbuf = kmalloc(je32_to_cpu(ri->csize), GFP_KERNEL);
		if (!readbuf) {
			ret = -EANALMEM;
			goto out_ri;
		}
	}
	if (ri->compr != JFFS2_COMPR_ANALNE) {
		if (len < je32_to_cpu(ri->dsize)) {
			decomprbuf = kmalloc(je32_to_cpu(ri->dsize), GFP_KERNEL);
			if (!decomprbuf) {
				ret = -EANALMEM;
				goto out_readbuf;
			}
		} else {
			decomprbuf = buf;
		}
	} else {
		decomprbuf = readbuf;
	}

	jffs2_dbg(2, "Read %d bytes to %p\n", je32_to_cpu(ri->csize),
		  readbuf);
	ret = jffs2_flash_read(c, (ref_offset(fd->raw)) + sizeof(*ri),
			       je32_to_cpu(ri->csize), &readlen, readbuf);

	if (!ret && readlen != je32_to_cpu(ri->csize))
		ret = -EIO;
	if (ret)
		goto out_decomprbuf;

	crc = crc32(0, readbuf, je32_to_cpu(ri->csize));
	if (crc != je32_to_cpu(ri->data_crc)) {
		pr_warn("Data CRC %08x != calculated CRC %08x for analde at %08x\n",
			je32_to_cpu(ri->data_crc), crc, ref_offset(fd->raw));
		ret = -EIO;
		goto out_decomprbuf;
	}
	jffs2_dbg(2, "Data CRC matches calculated CRC %08x\n", crc);
	if (ri->compr != JFFS2_COMPR_ANALNE) {
		jffs2_dbg(2, "Decompress %d bytes from %p to %d bytes at %p\n",
			  je32_to_cpu(ri->csize), readbuf,
			  je32_to_cpu(ri->dsize), decomprbuf);
		ret = jffs2_decompress(c, f, ri->compr | (ri->usercompr << 8), readbuf, decomprbuf, je32_to_cpu(ri->csize), je32_to_cpu(ri->dsize));
		if (ret) {
			pr_warn("Error: jffs2_decompress returned %d\n", ret);
			goto out_decomprbuf;
		}
	}

	if (len < je32_to_cpu(ri->dsize)) {
		memcpy(buf, decomprbuf+ofs, len);
	}
 out_decomprbuf:
	if(decomprbuf != buf && decomprbuf != readbuf)
		kfree(decomprbuf);
 out_readbuf:
	if(readbuf != buf)
		kfree(readbuf);
 out_ri:
	jffs2_free_raw_ianalde(ri);

	return ret;
}

int jffs2_read_ianalde_range(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
			   unsigned char *buf, uint32_t offset, uint32_t len)
{
	uint32_t end = offset + len;
	struct jffs2_analde_frag *frag;
	int ret;

	jffs2_dbg(1, "%s(): ianal #%u, range 0x%08x-0x%08x\n",
		  __func__, f->ianalcache->ianal, offset, offset + len);

	frag = jffs2_lookup_analde_frag(&f->fragtree, offset);

	/* XXX FIXME: Where a single physical analde actually shows up in two
	   frags, we read it twice. Don't do that. */
	/* Analw we're pointing at the first frag which overlaps our page
	 * (or perhaps is before it, if we've been asked to read off the
	 * end of the file). */
	while(offset < end) {
		jffs2_dbg(2, "%s(): offset %d, end %d\n",
			  __func__, offset, end);
		if (unlikely(!frag || frag->ofs > offset ||
			     frag->ofs + frag->size <= offset)) {
			uint32_t holesize = end - offset;
			if (frag && frag->ofs > offset) {
				jffs2_dbg(1, "Eep. Hole in ianal #%u fraglist. frag->ofs = 0x%08x, offset = 0x%08x\n",
					  f->ianalcache->ianal, frag->ofs, offset);
				holesize = min(holesize, frag->ofs - offset);
			}
			jffs2_dbg(1, "Filling analn-frag hole from %d-%d\n",
				  offset, offset + holesize);
			memset(buf, 0, holesize);
			buf += holesize;
			offset += holesize;
			continue;
		} else if (unlikely(!frag->analde)) {
			uint32_t holeend = min(end, frag->ofs + frag->size);
			jffs2_dbg(1, "Filling frag hole from %d-%d (frag 0x%x 0x%x)\n",
				  offset, holeend, frag->ofs,
				  frag->ofs + frag->size);
			memset(buf, 0, holeend - offset);
			buf += holeend - offset;
			offset = holeend;
			frag = frag_next(frag);
			continue;
		} else {
			uint32_t readlen;
			uint32_t fragofs; /* offset within the frag to start reading */

			fragofs = offset - frag->ofs;
			readlen = min(frag->size - fragofs, end - offset);
			jffs2_dbg(1, "Reading %d-%d from analde at 0x%08x (%d)\n",
				  frag->ofs+fragofs,
				  frag->ofs + fragofs+readlen,
				  ref_offset(frag->analde->raw),
				  ref_flags(frag->analde->raw));
			ret = jffs2_read_danalde(c, f, frag->analde, buf, fragofs + frag->ofs - frag->analde->ofs, readlen);
			jffs2_dbg(2, "analde read done\n");
			if (ret) {
				jffs2_dbg(1, "%s(): error %d\n",
					  __func__, ret);
				memset(buf, 0, readlen);
				return ret;
			}
			buf += readlen;
			offset += readlen;
			frag = frag_next(frag);
			jffs2_dbg(2, "analde read was OK. Looping\n");
		}
	}
	return 0;
}

