/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: read.c,v 1.38 2004/11/16 20:36:12 dwmw2 Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include <linux/compiler.h>
#include "nodelist.h"
#include "compr.h"

int jffs2_read_dnode(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
		     struct jffs2_full_dnode *fd, unsigned char *buf,
		     int ofs, int len)
{
	struct jffs2_raw_inode *ri;
	size_t readlen;
	uint32_t crc;
	unsigned char *decomprbuf = NULL;
	unsigned char *readbuf = NULL;
	int ret = 0;

	ri = jffs2_alloc_raw_inode();
	if (!ri)
		return -ENOMEM;

	ret = jffs2_flash_read(c, ref_offset(fd->raw), sizeof(*ri), &readlen, (char *)ri);
	if (ret) {
		jffs2_free_raw_inode(ri);
		printk(KERN_WARNING "Error reading node from 0x%08x: %d\n", ref_offset(fd->raw), ret);
		return ret;
	}
	if (readlen != sizeof(*ri)) {
		jffs2_free_raw_inode(ri);
		printk(KERN_WARNING "Short read from 0x%08x: wanted 0x%zx bytes, got 0x%zx\n", 
		       ref_offset(fd->raw), sizeof(*ri), readlen);
		return -EIO;
	}
	crc = crc32(0, ri, sizeof(*ri)-8);

	D1(printk(KERN_DEBUG "Node read from %08x: node_crc %08x, calculated CRC %08x. dsize %x, csize %x, offset %x, buf %p\n",
		  ref_offset(fd->raw), je32_to_cpu(ri->node_crc),
		  crc, je32_to_cpu(ri->dsize), je32_to_cpu(ri->csize),
		  je32_to_cpu(ri->offset), buf));
	if (crc != je32_to_cpu(ri->node_crc)) {
		printk(KERN_WARNING "Node CRC %08x != calculated CRC %08x for node at %08x\n",
		       je32_to_cpu(ri->node_crc), crc, ref_offset(fd->raw));
		ret = -EIO;
		goto out_ri;
	}
	/* There was a bug where we wrote hole nodes out with csize/dsize
	   swapped. Deal with it */
	if (ri->compr == JFFS2_COMPR_ZERO && !je32_to_cpu(ri->dsize) && 
	    je32_to_cpu(ri->csize)) {
		ri->dsize = ri->csize;
		ri->csize = cpu_to_je32(0);
	}

	D1(if(ofs + len > je32_to_cpu(ri->dsize)) {
		printk(KERN_WARNING "jffs2_read_dnode() asked for %d bytes at %d from %d-byte node\n",
		       len, ofs, je32_to_cpu(ri->dsize));
		ret = -EINVAL;
		goto out_ri;
	});

	
	if (ri->compr == JFFS2_COMPR_ZERO) {
		memset(buf, 0, len);
		goto out_ri;
	}

	/* Cases:
	   Reading whole node and it's uncompressed - read directly to buffer provided, check CRC.
	   Reading whole node and it's compressed - read into comprbuf, check CRC and decompress to buffer provided 
	   Reading partial node and it's uncompressed - read into readbuf, check CRC, and copy 
	   Reading partial node and it's compressed - read into readbuf, check checksum, decompress to decomprbuf and copy
	*/
	if (ri->compr == JFFS2_COMPR_NONE && len == je32_to_cpu(ri->dsize)) {
		readbuf = buf;
	} else {
		readbuf = kmalloc(je32_to_cpu(ri->csize), GFP_KERNEL);
		if (!readbuf) {
			ret = -ENOMEM;
			goto out_ri;
		}
	}
	if (ri->compr != JFFS2_COMPR_NONE) {
		if (len < je32_to_cpu(ri->dsize)) {
			decomprbuf = kmalloc(je32_to_cpu(ri->dsize), GFP_KERNEL);
			if (!decomprbuf) {
				ret = -ENOMEM;
				goto out_readbuf;
			}
		} else {
			decomprbuf = buf;
		}
	} else {
		decomprbuf = readbuf;
	}

	D2(printk(KERN_DEBUG "Read %d bytes to %p\n", je32_to_cpu(ri->csize),
		  readbuf));
	ret = jffs2_flash_read(c, (ref_offset(fd->raw)) + sizeof(*ri),
			       je32_to_cpu(ri->csize), &readlen, readbuf);

	if (!ret && readlen != je32_to_cpu(ri->csize))
		ret = -EIO;
	if (ret)
		goto out_decomprbuf;

	crc = crc32(0, readbuf, je32_to_cpu(ri->csize));
	if (crc != je32_to_cpu(ri->data_crc)) {
		printk(KERN_WARNING "Data CRC %08x != calculated CRC %08x for node at %08x\n",
		       je32_to_cpu(ri->data_crc), crc, ref_offset(fd->raw));
		ret = -EIO;
		goto out_decomprbuf;
	}
	D2(printk(KERN_DEBUG "Data CRC matches calculated CRC %08x\n", crc));
	if (ri->compr != JFFS2_COMPR_NONE) {
		D2(printk(KERN_DEBUG "Decompress %d bytes from %p to %d bytes at %p\n",
			  je32_to_cpu(ri->csize), readbuf, je32_to_cpu(ri->dsize), decomprbuf)); 
		ret = jffs2_decompress(c, f, ri->compr | (ri->usercompr << 8), readbuf, decomprbuf, je32_to_cpu(ri->csize), je32_to_cpu(ri->dsize));
		if (ret) {
			printk(KERN_WARNING "Error: jffs2_decompress returned %d\n", ret);
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
	jffs2_free_raw_inode(ri);

	return ret;
}

int jffs2_read_inode_range(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
			   unsigned char *buf, uint32_t offset, uint32_t len)
{
	uint32_t end = offset + len;
	struct jffs2_node_frag *frag;
	int ret;

	D1(printk(KERN_DEBUG "jffs2_read_inode_range: ino #%u, range 0x%08x-0x%08x\n",
		  f->inocache->ino, offset, offset+len));

	frag = jffs2_lookup_node_frag(&f->fragtree, offset);

	/* XXX FIXME: Where a single physical node actually shows up in two
	   frags, we read it twice. Don't do that. */
	/* Now we're pointing at the first frag which overlaps our page */
	while(offset < end) {
		D2(printk(KERN_DEBUG "jffs2_read_inode_range: offset %d, end %d\n", offset, end));
		if (unlikely(!frag || frag->ofs > offset)) {
			uint32_t holesize = end - offset;
			if (frag) {
				D1(printk(KERN_NOTICE "Eep. Hole in ino #%u fraglist. frag->ofs = 0x%08x, offset = 0x%08x\n", f->inocache->ino, frag->ofs, offset));
				holesize = min(holesize, frag->ofs - offset);
				D2(jffs2_print_frag_list(f));
			}
			D1(printk(KERN_DEBUG "Filling non-frag hole from %d-%d\n", offset, offset+holesize));
			memset(buf, 0, holesize);
			buf += holesize;
			offset += holesize;
			continue;
		} else if (unlikely(!frag->node)) {
			uint32_t holeend = min(end, frag->ofs + frag->size);
			D1(printk(KERN_DEBUG "Filling frag hole from %d-%d (frag 0x%x 0x%x)\n", offset, holeend, frag->ofs, frag->ofs + frag->size));
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
			D1(printk(KERN_DEBUG "Reading %d-%d from node at 0x%08x (%d)\n",
				  frag->ofs+fragofs, frag->ofs+fragofs+readlen,
				  ref_offset(frag->node->raw), ref_flags(frag->node->raw)));
			ret = jffs2_read_dnode(c, f, frag->node, buf, fragofs + frag->ofs - frag->node->ofs, readlen);
			D2(printk(KERN_DEBUG "node read done\n"));
			if (ret) {
				D1(printk(KERN_DEBUG"jffs2_read_inode_range error %d\n",ret));
				memset(buf, 0, readlen);
				return ret;
			}
			buf += readlen;
			offset += readlen;
			frag = frag_next(frag);
			D2(printk(KERN_DEBUG "node read was OK. Looping\n"));
		}
	}
	return 0;
}

/* Core function to read symlink target. */
char *jffs2_getlink(struct jffs2_sb_info *c, struct jffs2_inode_info *f)
{
	char *buf;
	int ret;

	down(&f->sem);

	if (!f->metadata) {
		printk(KERN_NOTICE "No metadata for symlink inode #%u\n", f->inocache->ino);
		up(&f->sem);
		return ERR_PTR(-EINVAL);
	}
	buf = kmalloc(f->metadata->size+1, GFP_USER);
	if (!buf) {
		up(&f->sem);
		return ERR_PTR(-ENOMEM);
	}
	buf[f->metadata->size]=0;

	ret = jffs2_read_dnode(c, f, f->metadata, buf, 0, f->metadata->size);

	up(&f->sem);

	if (ret) {
		kfree(buf);
		return ERR_PTR(ret);
	}
	return buf;
}
