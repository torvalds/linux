/*
**	make_desktop: create "Desktop DB" and "Desktop DF" files.
**
**	These are set up to prevent the Mac "rebuilding the desktop"
**	when the CD is inserted ???
**
**	I don't know if these files should be populated, but I've just
**	created these files in their initial states:
**
**	Desktop DB:	Initial size == volume's clump size
**			first block contents found by using od ...
**			rest of file seems to be padding
**			No resource fork
**
**	Desktop DF:	Empty
**
**	If the files already exist, then set correct type/creator/flags
**
**	James Pearson 11/8/97
**	Adapted from mkhfs routines for mkhybrid
*/

#ifdef APPLE_HYB

#include <string.h>
#include <err.h>
#include "hfs.h"

#define	DB	"Desktop DB"
#define DBFC	"DMGR"
#define DBT	"BTFL"

#define	DF	"Desktop DF"
#define DFT	"DTFL"

/* from "data.h" - libhfs routines */
void d_putw(unsigned char *, short);
void d_putl(unsigned char *, long);
/* from volume.c */
void write_fork(hfsfile *, long);

extern hce_mem *hce;		/* libhfs/mkisofs extras */

int
make_desktop(hfsvol *vol, int end)
/* hfsvol  *vol;				   Mac volume */
{
	hfsfile		*hfp;			/* Mac file */
	hfsdirent	ent;			/* Mac finderinfo */
	unsigned short	clps;			/* clump size */
	unsigned short	blks;			/* blocks in a clump */
	unsigned char	*blk;			/* user data */

	/* set up default directory entries - not all these fields
	   are needed, but we'll set them up anyway ... */
	ent.rsize = 0;				/* resource size == 0 */
	strcpy(ent.creator, DBFC);		/* creator */
	strcpy(ent.type, DBT);			/* type */
	ent.crdate = ent.mddate = time(0);	/* date is now */
	ent.fdflags = HFS_FNDR_ISINVISIBLE;	/* invisible files */

	/* clear the DB file */
	blk = hce->hfs_ce + hce->hfs_ce_size*HFS_BLOCKSZ;
	blks = hce->hfs_dt_size;
	clps = blks*HFS_BLOCKSZ;

	memset(blk, 0, clps);

	/* create "Desktop DB" (if it doesn't exist) */
	if(hfs_create(vol, DB, ent.type, ent.creator) == 0)
	{
	    /* DB file size from hce_mem info */
	    /* set up "Desktop DB" data - following found by od'ing
	       the "Desktop DB" file */
	    d_putw(blk+8, 0x100);
	    d_putw(blk+10, 0x3);

	    d_putw(blk+32, 0x200);
	    d_putw(blk+34, 0x25);

	    d_putl(blk+36, blks);
	    d_putl(blk+40, blks - 1);

	    d_putw(blk+48, clps);
	    d_putw(blk+50, 0xff);

	    d_putw(blk+120, 0x20a);
	    d_putw(blk+122, 0x100);

	    d_putw(blk+248, 0x8000);

	    d_putl(blk+504, 0x1f800f8);
	    d_putl(blk+508, 0x78000e);

	    /* entries for "Desktop DB" */
	    ent.dsize = clps;			/* size = clump size */
	
	    /* open file */
	    if((hfp = hfs_open(vol, DB)) == 0)
		err(1, "%s", hfs_error);

	    /* "write" file */
	    write_fork(hfp, clps);

	    /* set DB file attributes */
	    if (hfs_fsetattr(hfp, &ent) < 0)
		err(1, "%s", hfs_error);

	    /* find the real start of the file */
	    end += hce->hfs_ce_size;

	    /* close DB file */
	    if (hfs_close(hfp, end, 0) < 0)
		err(1, "%s", hfs_error);
	}
	else
	{
	    /* if it already exists, then make sure it has the correct
	       type/creator and flags */
	    if(hfs_setattr(vol, DB, &ent) < 0)
		err(1, "%s", hfs_error);
	}

	/* setup "Desktop DF" file as an empty file */
	strcpy(ent.type, DFT);			/* type */
	ent.dsize = 0;				/* empty */

	/* create DF file (if it doesn't exist) - no need to open it */
	hfs_create(vol, DF, ent.type, ent.creator);

	/* set DB file attributes */
	if (hfs_setattr(vol, DF, &ent) < 0)
	    err(1, "%s", hfs_error);
	return 0;
}
#endif /* APPLE_HYB */ 
