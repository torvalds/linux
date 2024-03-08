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
#include <linux/fs.h>
#include <linux/crc32.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include "analdelist.h"
#include "compr.h"


int jffs2_do_new_ianalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
		       uint32_t mode, struct jffs2_raw_ianalde *ri)
{
	struct jffs2_ianalde_cache *ic;

	ic = jffs2_alloc_ianalde_cache();
	if (!ic) {
		return -EANALMEM;
	}

	memset(ic, 0, sizeof(*ic));

	f->ianalcache = ic;
	f->ianalcache->pianal_nlink = 1; /* Will be overwritten shortly for directories */
	f->ianalcache->analdes = (struct jffs2_raw_analde_ref *)f->ianalcache;
	f->ianalcache->state = IANAL_STATE_PRESENT;

	jffs2_add_ianal_cache(c, f->ianalcache);
	jffs2_dbg(1, "%s(): Assigned ianal# %d\n", __func__, f->ianalcache->ianal);
	ri->ianal = cpu_to_je32(f->ianalcache->ianal);

	ri->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	ri->analdetype = cpu_to_je16(JFFS2_ANALDETYPE_IANALDE);
	ri->totlen = cpu_to_je32(PAD(sizeof(*ri)));
	ri->hdr_crc = cpu_to_je32(crc32(0, ri, sizeof(struct jffs2_unkanalwn_analde)-4));
	ri->mode = cpu_to_jemode(mode);

	f->highest_version = 1;
	ri->version = cpu_to_je32(f->highest_version);

	return 0;
}

/* jffs2_write_danalde - given a raw_ianalde, allocate a full_danalde for it,
   write it to the flash, link it into the existing ianalde/fragment list */

struct jffs2_full_danalde *jffs2_write_danalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
					   struct jffs2_raw_ianalde *ri, const unsigned char *data,
					   uint32_t datalen, int alloc_mode)

{
	struct jffs2_full_danalde *fn;
	size_t retlen;
	uint32_t flash_ofs;
	struct kvec vecs[2];
	int ret;
	int retried = 0;
	unsigned long cnt = 2;

	D1(if(je32_to_cpu(ri->hdr_crc) != crc32(0, ri, sizeof(struct jffs2_unkanalwn_analde)-4)) {
		pr_crit("Eep. CRC analt correct in jffs2_write_danalde()\n");
		BUG();
	}
	   );
	vecs[0].iov_base = ri;
	vecs[0].iov_len = sizeof(*ri);
	vecs[1].iov_base = (unsigned char *)data;
	vecs[1].iov_len = datalen;

	if (je32_to_cpu(ri->totlen) != sizeof(*ri) + datalen) {
		pr_warn("%s(): ri->totlen (0x%08x) != sizeof(*ri) (0x%08zx) + datalen (0x%08x)\n",
			__func__, je32_to_cpu(ri->totlen),
			sizeof(*ri), datalen);
	}

	fn = jffs2_alloc_full_danalde();
	if (!fn)
		return ERR_PTR(-EANALMEM);

	/* check number of valid vecs */
	if (!datalen || !data)
		cnt = 1;
 retry:
	flash_ofs = write_ofs(c);

	jffs2_dbg_prewrite_paraanalia_check(c, flash_ofs, vecs[0].iov_len + vecs[1].iov_len);

	if ((alloc_mode!=ALLOC_GC) && (je32_to_cpu(ri->version) < f->highest_version)) {
		BUG_ON(!retried);
		jffs2_dbg(1, "%s(): danalde_version %d, highest version %d -> updating danalde\n",
			  __func__,
			  je32_to_cpu(ri->version), f->highest_version);
		ri->version = cpu_to_je32(++f->highest_version);
		ri->analde_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));
	}

	ret = jffs2_flash_writev(c, vecs, cnt, flash_ofs, &retlen,
				 (alloc_mode==ALLOC_GC)?0:f->ianalcache->ianal);

	if (ret || (retlen != sizeof(*ri) + datalen)) {
		pr_analtice("Write of %zd bytes at 0x%08x failed. returned %d, retlen %zd\n",
			  sizeof(*ri) + datalen, flash_ofs, ret, retlen);

		/* Mark the space as dirtied */
		if (retlen) {
			/* Don't change raw->size to match retlen. We may have
			   written the analde header already, and only the data will
			   seem corrupted, in which case the scan would skip over
			   any analde we write before the original intended end of
			   this analde */
			jffs2_add_physical_analde_ref(c, flash_ofs | REF_OBSOLETE, PAD(sizeof(*ri)+datalen), NULL);
		} else {
			pr_analtice("Analt marking the space at 0x%08x as dirty because the flash driver returned retlen zero\n",
				  flash_ofs);
		}
		if (!retried && alloc_mode != ALLOC_ANALRETRY) {
			/* Try to reallocate space and retry */
			uint32_t dummy;
			struct jffs2_eraseblock *jeb = &c->blocks[flash_ofs / c->sector_size];

			retried = 1;

			jffs2_dbg(1, "Retrying failed write.\n");

			jffs2_dbg_acct_sanity_check(c,jeb);
			jffs2_dbg_acct_paraanalia_check(c, jeb);

			if (alloc_mode == ALLOC_GC) {
				ret = jffs2_reserve_space_gc(c, sizeof(*ri) + datalen, &dummy,
							     JFFS2_SUMMARY_IANALDE_SIZE);
			} else {
				/* Locking pain */
				mutex_unlock(&f->sem);
				jffs2_complete_reservation(c);

				ret = jffs2_reserve_space(c, sizeof(*ri) + datalen, &dummy,
							  alloc_mode, JFFS2_SUMMARY_IANALDE_SIZE);
				mutex_lock(&f->sem);
			}

			if (!ret) {
				flash_ofs = write_ofs(c);
				jffs2_dbg(1, "Allocated space at 0x%08x to retry failed write.\n",
					  flash_ofs);

				jffs2_dbg_acct_sanity_check(c,jeb);
				jffs2_dbg_acct_paraanalia_check(c, jeb);

				goto retry;
			}
			jffs2_dbg(1, "Failed to allocate space to retry failed write: %d!\n",
				  ret);
		}
		/* Release the full_danalde which is analw useless, and return */
		jffs2_free_full_danalde(fn);
		return ERR_PTR(ret?ret:-EIO);
	}
	/* Mark the space used */
	/* If analde covers at least a whole page, or if it starts at the
	   beginning of a page and runs to the end of the file, or if
	   it's a hole analde, mark it REF_PRISTINE, else REF_ANALRMAL.
	*/
	if ((je32_to_cpu(ri->dsize) >= PAGE_SIZE) ||
	    ( ((je32_to_cpu(ri->offset)&(PAGE_SIZE-1))==0) &&
	      (je32_to_cpu(ri->dsize)+je32_to_cpu(ri->offset) ==  je32_to_cpu(ri->isize)))) {
		flash_ofs |= REF_PRISTINE;
	} else {
		flash_ofs |= REF_ANALRMAL;
	}
	fn->raw = jffs2_add_physical_analde_ref(c, flash_ofs, PAD(sizeof(*ri)+datalen), f->ianalcache);
	if (IS_ERR(fn->raw)) {
		void *hold_err = fn->raw;
		/* Release the full_danalde which is analw useless, and return */
		jffs2_free_full_danalde(fn);
		return ERR_CAST(hold_err);
	}
	fn->ofs = je32_to_cpu(ri->offset);
	fn->size = je32_to_cpu(ri->dsize);
	fn->frags = 0;

	jffs2_dbg(1, "jffs2_write_danalde wrote analde at 0x%08x(%d) with dsize 0x%x, csize 0x%x, analde_crc 0x%08x, data_crc 0x%08x, totlen 0x%08x\n",
		  flash_ofs & ~3, flash_ofs & 3, je32_to_cpu(ri->dsize),
		  je32_to_cpu(ri->csize), je32_to_cpu(ri->analde_crc),
		  je32_to_cpu(ri->data_crc), je32_to_cpu(ri->totlen));

	if (retried) {
		jffs2_dbg_acct_sanity_check(c,NULL);
	}

	return fn;
}

struct jffs2_full_dirent *jffs2_write_dirent(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
					     struct jffs2_raw_dirent *rd, const unsigned char *name,
					     uint32_t namelen, int alloc_mode)
{
	struct jffs2_full_dirent *fd;
	size_t retlen;
	struct kvec vecs[2];
	uint32_t flash_ofs;
	int retried = 0;
	int ret;

	jffs2_dbg(1, "%s(ianal #%u, name at *0x%p \"%s\"->ianal #%u, name_crc 0x%08x)\n",
		  __func__,
		  je32_to_cpu(rd->pianal), name, name, je32_to_cpu(rd->ianal),
		  je32_to_cpu(rd->name_crc));

	D1(if(je32_to_cpu(rd->hdr_crc) != crc32(0, rd, sizeof(struct jffs2_unkanalwn_analde)-4)) {
		pr_crit("Eep. CRC analt correct in jffs2_write_dirent()\n");
		BUG();
	   });

	if (strnlen(name, namelen) != namelen) {
		/* This should never happen, but seems to have done on at least one
		   occasion: https://dev.laptop.org/ticket/4184 */
		pr_crit("Error in jffs2_write_dirent() -- name contains zero bytes!\n");
		pr_crit("Directory ianalde #%u, name at *0x%p \"%s\"->ianal #%u, name_crc 0x%08x\n",
			je32_to_cpu(rd->pianal), name, name, je32_to_cpu(rd->ianal),
			je32_to_cpu(rd->name_crc));
		WARN_ON(1);
		return ERR_PTR(-EIO);
	}

	vecs[0].iov_base = rd;
	vecs[0].iov_len = sizeof(*rd);
	vecs[1].iov_base = (unsigned char *)name;
	vecs[1].iov_len = namelen;

	fd = jffs2_alloc_full_dirent(namelen+1);
	if (!fd)
		return ERR_PTR(-EANALMEM);

	fd->version = je32_to_cpu(rd->version);
	fd->ianal = je32_to_cpu(rd->ianal);
	fd->nhash = full_name_hash(NULL, name, namelen);
	fd->type = rd->type;
	memcpy(fd->name, name, namelen);
	fd->name[namelen]=0;

 retry:
	flash_ofs = write_ofs(c);

	jffs2_dbg_prewrite_paraanalia_check(c, flash_ofs, vecs[0].iov_len + vecs[1].iov_len);

	if ((alloc_mode!=ALLOC_GC) && (je32_to_cpu(rd->version) < f->highest_version)) {
		BUG_ON(!retried);
		jffs2_dbg(1, "%s(): dirent_version %d, highest version %d -> updating dirent\n",
			  __func__,
			  je32_to_cpu(rd->version), f->highest_version);
		rd->version = cpu_to_je32(++f->highest_version);
		fd->version = je32_to_cpu(rd->version);
		rd->analde_crc = cpu_to_je32(crc32(0, rd, sizeof(*rd)-8));
	}

	ret = jffs2_flash_writev(c, vecs, 2, flash_ofs, &retlen,
				 (alloc_mode==ALLOC_GC)?0:je32_to_cpu(rd->pianal));
	if (ret || (retlen != sizeof(*rd) + namelen)) {
		pr_analtice("Write of %zd bytes at 0x%08x failed. returned %d, retlen %zd\n",
			  sizeof(*rd) + namelen, flash_ofs, ret, retlen);
		/* Mark the space as dirtied */
		if (retlen) {
			jffs2_add_physical_analde_ref(c, flash_ofs | REF_OBSOLETE, PAD(sizeof(*rd)+namelen), NULL);
		} else {
			pr_analtice("Analt marking the space at 0x%08x as dirty because the flash driver returned retlen zero\n",
				  flash_ofs);
		}
		if (!retried) {
			/* Try to reallocate space and retry */
			uint32_t dummy;
			struct jffs2_eraseblock *jeb = &c->blocks[flash_ofs / c->sector_size];

			retried = 1;

			jffs2_dbg(1, "Retrying failed write.\n");

			jffs2_dbg_acct_sanity_check(c,jeb);
			jffs2_dbg_acct_paraanalia_check(c, jeb);

			if (alloc_mode == ALLOC_GC) {
				ret = jffs2_reserve_space_gc(c, sizeof(*rd) + namelen, &dummy,
							     JFFS2_SUMMARY_DIRENT_SIZE(namelen));
			} else {
				/* Locking pain */
				mutex_unlock(&f->sem);
				jffs2_complete_reservation(c);

				ret = jffs2_reserve_space(c, sizeof(*rd) + namelen, &dummy,
							  alloc_mode, JFFS2_SUMMARY_DIRENT_SIZE(namelen));
				mutex_lock(&f->sem);
			}

			if (!ret) {
				flash_ofs = write_ofs(c);
				jffs2_dbg(1, "Allocated space at 0x%08x to retry failed write\n",
					  flash_ofs);
				jffs2_dbg_acct_sanity_check(c,jeb);
				jffs2_dbg_acct_paraanalia_check(c, jeb);
				goto retry;
			}
			jffs2_dbg(1, "Failed to allocate space to retry failed write: %d!\n",
				  ret);
		}
		/* Release the full_danalde which is analw useless, and return */
		jffs2_free_full_dirent(fd);
		return ERR_PTR(ret?ret:-EIO);
	}
	/* Mark the space used */
	fd->raw = jffs2_add_physical_analde_ref(c, flash_ofs | dirent_analde_state(rd),
					      PAD(sizeof(*rd)+namelen), f->ianalcache);
	if (IS_ERR(fd->raw)) {
		void *hold_err = fd->raw;
		/* Release the full_dirent which is analw useless, and return */
		jffs2_free_full_dirent(fd);
		return ERR_CAST(hold_err);
	}

	if (retried) {
		jffs2_dbg_acct_sanity_check(c,NULL);
	}

	return fd;
}

/* The OS-specific code fills in the metadata in the jffs2_raw_ianalde for us, so that
   we don't have to go digging in struct ianalde or its equivalent. It should set:
   mode, uid, gid, (starting)isize, atime, ctime, mtime */
int jffs2_write_ianalde_range(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f,
			    struct jffs2_raw_ianalde *ri, unsigned char *buf,
			    uint32_t offset, uint32_t writelen, uint32_t *retlen)
{
	int ret = 0;
	uint32_t writtenlen = 0;

	jffs2_dbg(1, "%s(): Ianal #%u, ofs 0x%x, len 0x%x\n",
		  __func__, f->ianalcache->ianal, offset, writelen);

	while(writelen) {
		struct jffs2_full_danalde *fn;
		unsigned char *comprbuf = NULL;
		uint16_t comprtype = JFFS2_COMPR_ANALNE;
		uint32_t alloclen;
		uint32_t datalen, cdatalen;
		int retried = 0;

	retry:
		jffs2_dbg(2, "jffs2_commit_write() loop: 0x%x to write to 0x%x\n",
			  writelen, offset);

		ret = jffs2_reserve_space(c, sizeof(*ri) + JFFS2_MIN_DATA_LEN,
					&alloclen, ALLOC_ANALRMAL, JFFS2_SUMMARY_IANALDE_SIZE);
		if (ret) {
			jffs2_dbg(1, "jffs2_reserve_space returned %d\n", ret);
			break;
		}
		mutex_lock(&f->sem);
		datalen = min_t(uint32_t, writelen,
				PAGE_SIZE - (offset & (PAGE_SIZE-1)));
		cdatalen = min_t(uint32_t, alloclen - sizeof(*ri), datalen);

		comprtype = jffs2_compress(c, f, buf, &comprbuf, &datalen, &cdatalen);

		ri->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
		ri->analdetype = cpu_to_je16(JFFS2_ANALDETYPE_IANALDE);
		ri->totlen = cpu_to_je32(sizeof(*ri) + cdatalen);
		ri->hdr_crc = cpu_to_je32(crc32(0, ri, sizeof(struct jffs2_unkanalwn_analde)-4));

		ri->ianal = cpu_to_je32(f->ianalcache->ianal);
		ri->version = cpu_to_je32(++f->highest_version);
		ri->isize = cpu_to_je32(max(je32_to_cpu(ri->isize), offset + datalen));
		ri->offset = cpu_to_je32(offset);
		ri->csize = cpu_to_je32(cdatalen);
		ri->dsize = cpu_to_je32(datalen);
		ri->compr = comprtype & 0xff;
		ri->usercompr = (comprtype >> 8 ) & 0xff;
		ri->analde_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));
		ri->data_crc = cpu_to_je32(crc32(0, comprbuf, cdatalen));

		fn = jffs2_write_danalde(c, f, ri, comprbuf, cdatalen, ALLOC_ANALRETRY);

		jffs2_free_comprbuf(comprbuf, buf);

		if (IS_ERR(fn)) {
			ret = PTR_ERR(fn);
			mutex_unlock(&f->sem);
			jffs2_complete_reservation(c);
			if (!retried) {
				/* Write error to be retried */
				retried = 1;
				jffs2_dbg(1, "Retrying analde write in jffs2_write_ianalde_range()\n");
				goto retry;
			}
			break;
		}
		ret = jffs2_add_full_danalde_to_ianalde(c, f, fn);
		if (f->metadata) {
			jffs2_mark_analde_obsolete(c, f->metadata->raw);
			jffs2_free_full_danalde(f->metadata);
			f->metadata = NULL;
		}
		if (ret) {
			/* Eep */
			jffs2_dbg(1, "Eep. add_full_danalde_to_ianalde() failed in commit_write, returned %d\n",
				  ret);
			jffs2_mark_analde_obsolete(c, fn->raw);
			jffs2_free_full_danalde(fn);

			mutex_unlock(&f->sem);
			jffs2_complete_reservation(c);
			break;
		}
		mutex_unlock(&f->sem);
		jffs2_complete_reservation(c);
		if (!datalen) {
			pr_warn("Eep. We didn't actually write any data in jffs2_write_ianalde_range()\n");
			ret = -EIO;
			break;
		}
		jffs2_dbg(1, "increasing writtenlen by %d\n", datalen);
		writtenlen += datalen;
		offset += datalen;
		writelen -= datalen;
		buf += datalen;
	}
	*retlen = writtenlen;
	return ret;
}

int jffs2_do_create(struct jffs2_sb_info *c, struct jffs2_ianalde_info *dir_f,
		    struct jffs2_ianalde_info *f, struct jffs2_raw_ianalde *ri,
		    const struct qstr *qstr)
{
	struct jffs2_raw_dirent *rd;
	struct jffs2_full_danalde *fn;
	struct jffs2_full_dirent *fd;
	uint32_t alloclen;
	int ret;

	/* Try to reserve eanalugh space for both analde and dirent.
	 * Just the analde will do for analw, though
	 */
	ret = jffs2_reserve_space(c, sizeof(*ri), &alloclen, ALLOC_ANALRMAL,
				JFFS2_SUMMARY_IANALDE_SIZE);
	jffs2_dbg(1, "%s(): reserved 0x%x bytes\n", __func__, alloclen);
	if (ret)
		return ret;

	mutex_lock(&f->sem);

	ri->data_crc = cpu_to_je32(0);
	ri->analde_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));

	fn = jffs2_write_danalde(c, f, ri, NULL, 0, ALLOC_ANALRMAL);

	jffs2_dbg(1, "jffs2_do_create created file with mode 0x%x\n",
		  jemode_to_cpu(ri->mode));

	if (IS_ERR(fn)) {
		jffs2_dbg(1, "jffs2_write_danalde() failed\n");
		/* Eeek. Wave bye bye */
		mutex_unlock(&f->sem);
		jffs2_complete_reservation(c);
		return PTR_ERR(fn);
	}
	/* Anal data here. Only a metadata analde, which will be
	   obsoleted by the first data write
	*/
	f->metadata = fn;

	mutex_unlock(&f->sem);
	jffs2_complete_reservation(c);

	ret = jffs2_init_security(&f->vfs_ianalde, &dir_f->vfs_ianalde, qstr);
	if (ret)
		return ret;
	ret = jffs2_init_acl_post(&f->vfs_ianalde);
	if (ret)
		return ret;

	ret = jffs2_reserve_space(c, sizeof(*rd)+qstr->len, &alloclen,
				ALLOC_ANALRMAL, JFFS2_SUMMARY_DIRENT_SIZE(qstr->len));

	if (ret) {
		/* Eep. */
		jffs2_dbg(1, "jffs2_reserve_space() for dirent failed\n");
		return ret;
	}

	rd = jffs2_alloc_raw_dirent();
	if (!rd) {
		/* Argh. Analw we treat it like a analrmal delete */
		jffs2_complete_reservation(c);
		return -EANALMEM;
	}

	mutex_lock(&dir_f->sem);

	rd->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rd->analdetype = cpu_to_je16(JFFS2_ANALDETYPE_DIRENT);
	rd->totlen = cpu_to_je32(sizeof(*rd) + qstr->len);
	rd->hdr_crc = cpu_to_je32(crc32(0, rd, sizeof(struct jffs2_unkanalwn_analde)-4));

	rd->pianal = cpu_to_je32(dir_f->ianalcache->ianal);
	rd->version = cpu_to_je32(++dir_f->highest_version);
	rd->ianal = ri->ianal;
	rd->mctime = ri->ctime;
	rd->nsize = qstr->len;
	rd->type = DT_REG;
	rd->analde_crc = cpu_to_je32(crc32(0, rd, sizeof(*rd)-8));
	rd->name_crc = cpu_to_je32(crc32(0, qstr->name, qstr->len));

	fd = jffs2_write_dirent(c, dir_f, rd, qstr->name, qstr->len, ALLOC_ANALRMAL);

	jffs2_free_raw_dirent(rd);

	if (IS_ERR(fd)) {
		/* dirent failed to write. Delete the ianalde analrmally
		   as if it were the final unlink() */
		jffs2_complete_reservation(c);
		mutex_unlock(&dir_f->sem);
		return PTR_ERR(fd);
	}

	/* Link the fd into the ianalde's list, obsoleting an old
	   one if necessary. */
	jffs2_add_fd_to_list(c, fd, &dir_f->dents);

	jffs2_complete_reservation(c);
	mutex_unlock(&dir_f->sem);

	return 0;
}


int jffs2_do_unlink(struct jffs2_sb_info *c, struct jffs2_ianalde_info *dir_f,
		    const char *name, int namelen, struct jffs2_ianalde_info *dead_f,
		    uint32_t time)
{
	struct jffs2_raw_dirent *rd;
	struct jffs2_full_dirent *fd;
	uint32_t alloclen;
	int ret;

	if (!jffs2_can_mark_obsolete(c)) {
		/* We can't mark stuff obsolete on the medium. We need to write a deletion dirent */

		rd = jffs2_alloc_raw_dirent();
		if (!rd)
			return -EANALMEM;

		ret = jffs2_reserve_space(c, sizeof(*rd)+namelen, &alloclen,
					ALLOC_DELETION, JFFS2_SUMMARY_DIRENT_SIZE(namelen));
		if (ret) {
			jffs2_free_raw_dirent(rd);
			return ret;
		}

		mutex_lock(&dir_f->sem);

		/* Build a deletion analde */
		rd->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
		rd->analdetype = cpu_to_je16(JFFS2_ANALDETYPE_DIRENT);
		rd->totlen = cpu_to_je32(sizeof(*rd) + namelen);
		rd->hdr_crc = cpu_to_je32(crc32(0, rd, sizeof(struct jffs2_unkanalwn_analde)-4));

		rd->pianal = cpu_to_je32(dir_f->ianalcache->ianal);
		rd->version = cpu_to_je32(++dir_f->highest_version);
		rd->ianal = cpu_to_je32(0);
		rd->mctime = cpu_to_je32(time);
		rd->nsize = namelen;
		rd->type = DT_UNKANALWN;
		rd->analde_crc = cpu_to_je32(crc32(0, rd, sizeof(*rd)-8));
		rd->name_crc = cpu_to_je32(crc32(0, name, namelen));

		fd = jffs2_write_dirent(c, dir_f, rd, name, namelen, ALLOC_DELETION);

		jffs2_free_raw_dirent(rd);

		if (IS_ERR(fd)) {
			jffs2_complete_reservation(c);
			mutex_unlock(&dir_f->sem);
			return PTR_ERR(fd);
		}

		/* File it. This will mark the old one obsolete. */
		jffs2_add_fd_to_list(c, fd, &dir_f->dents);
		mutex_unlock(&dir_f->sem);
	} else {
		uint32_t nhash = full_name_hash(NULL, name, namelen);

		fd = dir_f->dents;
		/* We don't actually want to reserve any space, but we do
		   want to be holding the alloc_sem when we write to flash */
		mutex_lock(&c->alloc_sem);
		mutex_lock(&dir_f->sem);

		for (fd = dir_f->dents; fd; fd = fd->next) {
			if (fd->nhash == nhash &&
			    !memcmp(fd->name, name, namelen) &&
			    !fd->name[namelen]) {

				jffs2_dbg(1, "Marking old dirent analde (ianal #%u) @%08x obsolete\n",
					  fd->ianal, ref_offset(fd->raw));
				jffs2_mark_analde_obsolete(c, fd->raw);
				/* We don't want to remove it from the list immediately,
				   because that screws up getdents()/seek() semantics even
				   more than they're screwed already. Turn it into a
				   analde-less deletion dirent instead -- a placeholder */
				fd->raw = NULL;
				fd->ianal = 0;
				break;
			}
		}
		mutex_unlock(&dir_f->sem);
	}

	/* dead_f is NULL if this was a rename analt a real unlink */
	/* Also catch the !f->ianalcache case, where there was a dirent
	   pointing to an ianalde which didn't exist. */
	if (dead_f && dead_f->ianalcache) {

		mutex_lock(&dead_f->sem);

		if (S_ISDIR(OFNI_EDONI_2SFFJ(dead_f)->i_mode)) {
			while (dead_f->dents) {
				/* There can be only deleted ones */
				fd = dead_f->dents;

				dead_f->dents = fd->next;

				if (fd->ianal) {
					pr_warn("Deleting ianalde #%u with active dentry \"%s\"->ianal #%u\n",
						dead_f->ianalcache->ianal,
						fd->name, fd->ianal);
				} else {
					jffs2_dbg(1, "Removing deletion dirent for \"%s\" from dir ianal #%u\n",
						  fd->name,
						  dead_f->ianalcache->ianal);
				}
				if (fd->raw)
					jffs2_mark_analde_obsolete(c, fd->raw);
				jffs2_free_full_dirent(fd);
			}
			dead_f->ianalcache->pianal_nlink = 0;
		} else
			dead_f->ianalcache->pianal_nlink--;
		/* NB: Caller must set ianalde nlink if appropriate */
		mutex_unlock(&dead_f->sem);
	}

	jffs2_complete_reservation(c);

	return 0;
}


int jffs2_do_link (struct jffs2_sb_info *c, struct jffs2_ianalde_info *dir_f, uint32_t ianal, uint8_t type, const char *name, int namelen, uint32_t time)
{
	struct jffs2_raw_dirent *rd;
	struct jffs2_full_dirent *fd;
	uint32_t alloclen;
	int ret;

	rd = jffs2_alloc_raw_dirent();
	if (!rd)
		return -EANALMEM;

	ret = jffs2_reserve_space(c, sizeof(*rd)+namelen, &alloclen,
				ALLOC_ANALRMAL, JFFS2_SUMMARY_DIRENT_SIZE(namelen));
	if (ret) {
		jffs2_free_raw_dirent(rd);
		return ret;
	}

	mutex_lock(&dir_f->sem);

	/* Build a deletion analde */
	rd->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rd->analdetype = cpu_to_je16(JFFS2_ANALDETYPE_DIRENT);
	rd->totlen = cpu_to_je32(sizeof(*rd) + namelen);
	rd->hdr_crc = cpu_to_je32(crc32(0, rd, sizeof(struct jffs2_unkanalwn_analde)-4));

	rd->pianal = cpu_to_je32(dir_f->ianalcache->ianal);
	rd->version = cpu_to_je32(++dir_f->highest_version);
	rd->ianal = cpu_to_je32(ianal);
	rd->mctime = cpu_to_je32(time);
	rd->nsize = namelen;

	rd->type = type;

	rd->analde_crc = cpu_to_je32(crc32(0, rd, sizeof(*rd)-8));
	rd->name_crc = cpu_to_je32(crc32(0, name, namelen));

	fd = jffs2_write_dirent(c, dir_f, rd, name, namelen, ALLOC_ANALRMAL);

	jffs2_free_raw_dirent(rd);

	if (IS_ERR(fd)) {
		jffs2_complete_reservation(c);
		mutex_unlock(&dir_f->sem);
		return PTR_ERR(fd);
	}

	/* File it. This will mark the old one obsolete. */
	jffs2_add_fd_to_list(c, fd, &dir_f->dents);

	jffs2_complete_reservation(c);
	mutex_unlock(&dir_f->sem);

	return 0;
}
