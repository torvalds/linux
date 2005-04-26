/*
 *  linux/fs/isofs/rock.c
 *
 *  (C) 1992, 1993  Eric Youngdale
 *
 *  Rock Ridge Extensions to iso9660
 */

#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>

#include "isofs.h"
#include "rock.h"

/* These functions are designed to read the system areas of a directory record
 * and extract relevant information.  There are different functions provided
 * depending upon what information we need at the time.  One function fills
 * out an inode structure, a second one extracts a filename, a third one
 * returns a symbolic link name, and a fourth one returns the extent number
 * for the file. */

#define SIG(A,B) ((A) | ((B) << 8)) /* isonum_721() */


/* This is a way of ensuring that we have something in the system
   use fields that is compatible with Rock Ridge */
#define CHECK_SP(FAIL)	       			\
      if(rr->u.SP.magic[0] != 0xbe) FAIL;	\
      if(rr->u.SP.magic[1] != 0xef) FAIL;       \
      ISOFS_SB(inode->i_sb)->s_rock_offset=rr->u.SP.skip;
/* We define a series of macros because each function must do exactly the
   same thing in certain places.  We use the macros to ensure that everything
   is done correctly */

#define CONTINUE_DECLS \
  int cont_extent = 0, cont_offset = 0, cont_size = 0;   \
  void *buffer = NULL

#define CHECK_CE	       			\
      {cont_extent = isonum_733(rr->u.CE.extent); \
      cont_offset = isonum_733(rr->u.CE.offset); \
      cont_size = isonum_733(rr->u.CE.size);}

#define SETUP_ROCK_RIDGE(DE,CHR,LEN)	      		      	\
  {LEN= sizeof(struct iso_directory_record) + DE->name_len[0];	\
  if(LEN & 1) LEN++;						\
  CHR = ((unsigned char *) DE) + LEN;				\
  LEN = *((unsigned char *) DE) - LEN;                          \
  if (LEN<0) LEN=0;                                             \
  if (ISOFS_SB(inode->i_sb)->s_rock_offset!=-1)                \
  {                                                             \
     LEN-=ISOFS_SB(inode->i_sb)->s_rock_offset;                \
     CHR+=ISOFS_SB(inode->i_sb)->s_rock_offset;                \
     if (LEN<0) LEN=0;                                          \
  }                                                             \
}                                     

#define MAYBE_CONTINUE(LABEL,DEV) \
  {if (buffer) { kfree(buffer); buffer = NULL; } \
  if (cont_extent){ \
    int block, offset, offset1; \
    struct buffer_head * pbh; \
    buffer = kmalloc(cont_size,GFP_KERNEL); \
    if (!buffer) goto out; \
    block = cont_extent; \
    offset = cont_offset; \
    offset1 = 0; \
    pbh = sb_bread(DEV->i_sb, block); \
    if(pbh){       \
      if (offset > pbh->b_size || offset + cont_size > pbh->b_size){	\
	brelse(pbh); \
	goto out; \
      } \
      memcpy(buffer + offset1, pbh->b_data + offset, cont_size - offset1); \
      brelse(pbh); \
      chr = (unsigned char *) buffer; \
      len = cont_size; \
      cont_extent = 0; \
      cont_size = 0; \
      cont_offset = 0; \
      goto LABEL; \
    }    \
    printk("Unable to read rock-ridge attributes\n");    \
  }}

/* return length of name field; 0: not found, -1: to be ignored */
int get_rock_ridge_filename(struct iso_directory_record * de,
			    char * retname, struct inode * inode)
{
  int len;
  unsigned char * chr;
  CONTINUE_DECLS;
  int retnamlen = 0, truncate=0;
 
  if (!ISOFS_SB(inode->i_sb)->s_rock) return 0;
  *retname = 0;

  SETUP_ROCK_RIDGE(de, chr, len);
 repeat:
  {
    struct rock_ridge * rr;
    int sig;
    
    while (len > 2){ /* There may be one byte for padding somewhere */
      rr = (struct rock_ridge *) chr;
      if (rr->len < 3) goto out; /* Something got screwed up here */
      sig = isonum_721(chr);
      chr += rr->len; 
      len -= rr->len;
      if (len < 0) goto out;	/* corrupted isofs */

      switch(sig){
      case SIG('R','R'):
	if((rr->u.RR.flags[0] & RR_NM) == 0) goto out;
	break;
      case SIG('S','P'):
	CHECK_SP(goto out);
	break;
      case SIG('C','E'):
	CHECK_CE;
	break;
      case SIG('N','M'):
	if (truncate) break;
	if (rr->len < 5) break;
        /*
	 * If the flags are 2 or 4, this indicates '.' or '..'.
	 * We don't want to do anything with this, because it
	 * screws up the code that calls us.  We don't really
	 * care anyways, since we can just use the non-RR
	 * name.
	 */
	if (rr->u.NM.flags & 6) {
	  break;
	}

	if (rr->u.NM.flags & ~1) {
	  printk("Unsupported NM flag settings (%d)\n",rr->u.NM.flags);
	  break;
	}
	if((strlen(retname) + rr->len - 5) >= 254) {
	  truncate = 1;
	  break;
	}
	strncat(retname, rr->u.NM.name, rr->len - 5);
	retnamlen += rr->len - 5;
	break;
      case SIG('R','E'):
	if (buffer) kfree(buffer);
	return -1;
      default:
	break;
      }
    }
  }
  MAYBE_CONTINUE(repeat,inode);
  if (buffer) kfree(buffer);
  return retnamlen; /* If 0, this file did not have a NM field */
 out:
  if(buffer) kfree(buffer);
  return 0;
}

static int
parse_rock_ridge_inode_internal(struct iso_directory_record *de,
				struct inode *inode, int regard_xa)
{
  int len;
  unsigned char * chr;
  int symlink_len = 0;
  CONTINUE_DECLS;

  if (!ISOFS_SB(inode->i_sb)->s_rock) return 0;

  SETUP_ROCK_RIDGE(de, chr, len);
  if (regard_xa)
   {
     chr+=14;
     len-=14;
     if (len<0) len=0;
   }
   
 repeat:
  {
    int cnt, sig;
    struct inode * reloc;
    struct rock_ridge * rr;
    int rootflag;
    
    while (len > 2){ /* There may be one byte for padding somewhere */
      rr = (struct rock_ridge *) chr;
      if (rr->len < 3) goto out; /* Something got screwed up here */
      sig = isonum_721(chr);
      chr += rr->len; 
      len -= rr->len;
      if (len < 0) goto out;	/* corrupted isofs */
      
      switch(sig){
#ifndef CONFIG_ZISOFS		/* No flag for SF or ZF */
      case SIG('R','R'):
	if((rr->u.RR.flags[0] & 
 	    (RR_PX | RR_TF | RR_SL | RR_CL)) == 0) goto out;
	break;
#endif
      case SIG('S','P'):
	CHECK_SP(goto out);
	break;
      case SIG('C','E'):
	CHECK_CE;
	break;
      case SIG('E','R'):
	ISOFS_SB(inode->i_sb)->s_rock = 1;
	printk(KERN_DEBUG "ISO 9660 Extensions: ");
	{ int p;
	  for(p=0;p<rr->u.ER.len_id;p++) printk("%c",rr->u.ER.data[p]);
	}
	  printk("\n");
	break;
      case SIG('P','X'):
	inode->i_mode  = isonum_733(rr->u.PX.mode);
	inode->i_nlink = isonum_733(rr->u.PX.n_links);
	inode->i_uid   = isonum_733(rr->u.PX.uid);
	inode->i_gid   = isonum_733(rr->u.PX.gid);
	break;
      case SIG('P','N'):
	{ int high, low;
	  high = isonum_733(rr->u.PN.dev_high);
	  low = isonum_733(rr->u.PN.dev_low);
	  /*
	   * The Rock Ridge standard specifies that if sizeof(dev_t) <= 4,
	   * then the high field is unused, and the device number is completely
	   * stored in the low field.  Some writers may ignore this subtlety,
	   * and as a result we test to see if the entire device number is
	   * stored in the low field, and use that.
	   */
	  if((low & ~0xff) && high == 0) {
	    inode->i_rdev = MKDEV(low >> 8, low & 0xff);
	  } else {
	    inode->i_rdev = MKDEV(high, low);
	  }
	}
	break;
      case SIG('T','F'):
	/* Some RRIP writers incorrectly place ctime in the TF_CREATE field.
	   Try to handle this correctly for either case. */
	cnt = 0; /* Rock ridge never appears on a High Sierra disk */
	if(rr->u.TF.flags & TF_CREATE) { 
	  inode->i_ctime.tv_sec = iso_date(rr->u.TF.times[cnt++].time, 0);
	  inode->i_ctime.tv_nsec = 0;
	}
	if(rr->u.TF.flags & TF_MODIFY) {
	  inode->i_mtime.tv_sec = iso_date(rr->u.TF.times[cnt++].time, 0);
	  inode->i_mtime.tv_nsec = 0;
	}
	if(rr->u.TF.flags & TF_ACCESS) {
	  inode->i_atime.tv_sec = iso_date(rr->u.TF.times[cnt++].time, 0);
	  inode->i_atime.tv_nsec = 0;
	}
	if(rr->u.TF.flags & TF_ATTRIBUTES) { 
	  inode->i_ctime.tv_sec = iso_date(rr->u.TF.times[cnt++].time, 0);
	  inode->i_ctime.tv_nsec = 0;
	} 
	break;
      case SIG('S','L'):
	{int slen;
	 struct SL_component * slp;
	 struct SL_component * oldslp;
	 slen = rr->len - 5;
	 slp = &rr->u.SL.link;
	 inode->i_size = symlink_len;
	 while (slen > 1){
	   rootflag = 0;
	   switch(slp->flags &~1){
	   case 0:
	     inode->i_size += slp->len;
	     break;
	   case 2:
	     inode->i_size += 1;
	     break;
	   case 4:
	     inode->i_size += 2;
	     break;
	   case 8:
	     rootflag = 1;
	     inode->i_size += 1;
	     break;
	   default:
	     printk("Symlink component flag not implemented\n");
	   }
	   slen -= slp->len + 2;
	   oldslp = slp;
	   slp = (struct SL_component *) (((char *) slp) + slp->len + 2);

	   if(slen < 2) {
	     if(    ((rr->u.SL.flags & 1) != 0) 
		    && ((oldslp->flags & 1) == 0) ) inode->i_size += 1;
	     break;
	   }

	   /*
	    * If this component record isn't continued, then append a '/'.
	    */
	   if (!rootflag && (oldslp->flags & 1) == 0)
		   inode->i_size += 1;
	 }
	}
	symlink_len = inode->i_size;
	break;
      case SIG('R','E'):
	printk(KERN_WARNING "Attempt to read inode for relocated directory\n");
	goto out;
      case SIG('C','L'):
	ISOFS_I(inode)->i_first_extent = isonum_733(rr->u.CL.location);
	reloc = isofs_iget(inode->i_sb, ISOFS_I(inode)->i_first_extent, 0);
	if (!reloc)
		goto out;
	inode->i_mode = reloc->i_mode;
	inode->i_nlink = reloc->i_nlink;
	inode->i_uid = reloc->i_uid;
	inode->i_gid = reloc->i_gid;
	inode->i_rdev = reloc->i_rdev;
	inode->i_size = reloc->i_size;
	inode->i_blocks = reloc->i_blocks;
	inode->i_atime = reloc->i_atime;
	inode->i_ctime = reloc->i_ctime;
	inode->i_mtime = reloc->i_mtime;
	iput(reloc);
	break;
#ifdef CONFIG_ZISOFS
      case SIG('Z','F'):
	      if ( !ISOFS_SB(inode->i_sb)->s_nocompress ) {
		      int algo;
		      algo = isonum_721(rr->u.ZF.algorithm);
		      if ( algo == SIG('p','z') ) {
			      int block_shift = isonum_711(&rr->u.ZF.parms[1]);
			      if ( block_shift < PAGE_CACHE_SHIFT || block_shift > 17 ) {
				      printk(KERN_WARNING "isofs: Can't handle ZF block size of 2^%d\n", block_shift);
			      } else {
				/* Note: we don't change i_blocks here */
				      ISOFS_I(inode)->i_file_format = isofs_file_compressed;
				/* Parameters to compression algorithm (header size, block size) */
				      ISOFS_I(inode)->i_format_parm[0] = isonum_711(&rr->u.ZF.parms[0]);
				      ISOFS_I(inode)->i_format_parm[1] = isonum_711(&rr->u.ZF.parms[1]);
				      inode->i_size = isonum_733(rr->u.ZF.real_size);
			      }
		      } else {
			      printk(KERN_WARNING "isofs: Unknown ZF compression algorithm: %c%c\n",
				     rr->u.ZF.algorithm[0], rr->u.ZF.algorithm[1]);
		      }
	      }
	      break;
#endif
      default:
	break;
      }
    }
  }
  MAYBE_CONTINUE(repeat,inode);
 out:
  if(buffer) kfree(buffer);
  return 0;
}

static char *get_symlink_chunk(char *rpnt, struct rock_ridge *rr, char *plimit)
{
	int slen;
	int rootflag;
	struct SL_component *oldslp;
	struct SL_component *slp;
	slen = rr->len - 5;
	slp = &rr->u.SL.link;
	while (slen > 1) {
		rootflag = 0;
		switch (slp->flags & ~1) {
		case 0:
			if (slp->len > plimit - rpnt)
				return NULL;
			memcpy(rpnt, slp->text, slp->len);
			rpnt+=slp->len;
			break;
		case 2:
			if (rpnt >= plimit)
				return NULL;
			*rpnt++='.';
			break;
		case 4:
			if (2 > plimit - rpnt)
				return NULL;
			*rpnt++='.';
			*rpnt++='.';
			break;
		case 8:
			if (rpnt >= plimit)
				return NULL;
			rootflag = 1;
			*rpnt++='/';
			break;
		default:
			printk("Symlink component flag not implemented (%d)\n",
			     slp->flags);
		}
		slen -= slp->len + 2;
		oldslp = slp;
		slp = (struct SL_component *) ((char *) slp + slp->len + 2);

		if (slen < 2) {
			/*
			 * If there is another SL record, and this component
			 * record isn't continued, then add a slash.
			 */
			if ((!rootflag) && (rr->u.SL.flags & 1) &&
			    !(oldslp->flags & 1)) {
				if (rpnt >= plimit)
					return NULL;
				*rpnt++='/';
			}
			break;
		}

		/*
		 * If this component record isn't continued, then append a '/'.
		 */
		if (!rootflag && !(oldslp->flags & 1)) {
			if (rpnt >= plimit)
				return NULL;
			*rpnt++='/';
		}
	}
	return rpnt;
}

int parse_rock_ridge_inode(struct iso_directory_record * de,
			   struct inode * inode)
{
   int result=parse_rock_ridge_inode_internal(de,inode,0);
   /* if rockridge flag was reset and we didn't look for attributes
    * behind eventual XA attributes, have a look there */
   if ((ISOFS_SB(inode->i_sb)->s_rock_offset==-1)
       &&(ISOFS_SB(inode->i_sb)->s_rock==2))
     {
	result=parse_rock_ridge_inode_internal(de,inode,14);
     }
   return result;
}

/* readpage() for symlinks: reads symlink contents into the page and either
   makes it uptodate and returns 0 or returns error (-EIO) */

static int rock_ridge_symlink_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
        struct iso_inode_info *ei = ISOFS_I(inode);
	char *link = kmap(page);
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	struct buffer_head *bh;
	char *rpnt = link;
	unsigned char *pnt;
	struct iso_directory_record *raw_inode;
	CONTINUE_DECLS;
	unsigned long block, offset;
	int sig;
	int len;
	unsigned char *chr;
	struct rock_ridge *rr;

	if (!ISOFS_SB(inode->i_sb)->s_rock)
		goto error;

	block = ei->i_iget5_block;
	lock_kernel();
	bh = sb_bread(inode->i_sb, block);
	if (!bh)
		goto out_noread;

        offset = ei->i_iget5_offset;
	pnt = (unsigned char *) bh->b_data + offset;

	raw_inode = (struct iso_directory_record *) pnt;

	/*
	 * If we go past the end of the buffer, there is some sort of error.
	 */
	if (offset + *pnt > bufsize)
		goto out_bad_span;

	/* Now test for possible Rock Ridge extensions which will override
	   some of these numbers in the inode structure. */

	SETUP_ROCK_RIDGE(raw_inode, chr, len);

      repeat:
	while (len > 2) { /* There may be one byte for padding somewhere */
		rr = (struct rock_ridge *) chr;
		if (rr->len < 3)
			goto out;	/* Something got screwed up here */
		sig = isonum_721(chr);
		chr += rr->len;
		len -= rr->len;
		if (len < 0)
			goto out;	/* corrupted isofs */

		switch (sig) {
		case SIG('R', 'R'):
			if ((rr->u.RR.flags[0] & RR_SL) == 0)
				goto out;
			break;
		case SIG('S', 'P'):
			CHECK_SP(goto out);
			break;
		case SIG('S', 'L'):
			rpnt = get_symlink_chunk(rpnt, rr,
						 link + (PAGE_SIZE - 1));
			if (rpnt == NULL)
				goto out;
			break;
		case SIG('C', 'E'):
			/* This tells is if there is a continuation record */
			CHECK_CE;
		default:
			break;
		}
	}
	MAYBE_CONTINUE(repeat, inode);
	if (buffer)
		kfree(buffer);

	if (rpnt == link)
		goto fail;
	brelse(bh);
	*rpnt = '\0';
	unlock_kernel();
	SetPageUptodate(page);
	kunmap(page);
	unlock_page(page);
	return 0;

	/* error exit from macro */
      out:
	if (buffer)
		kfree(buffer);
	goto fail;
      out_noread:
	printk("unable to read i-node block");
	goto fail;
      out_bad_span:
	printk("symlink spans iso9660 blocks\n");
      fail:
	brelse(bh);
	unlock_kernel();
      error:
	SetPageError(page);
	kunmap(page);
	unlock_page(page);
	return -EIO;
}

struct address_space_operations isofs_symlink_aops = {
	.readpage	= rock_ridge_symlink_readpage
};
