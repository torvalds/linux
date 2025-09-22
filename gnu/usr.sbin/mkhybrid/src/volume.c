/*
**	volume.c: prepare HFS volume for mkhybrid
**
**	James Pearson 17/7/97
**	modified JCP 29/7/97 to improve allocation sizes to cut
**	down on wasted space. Now uses the HFS "allocation" size rounded
**	up to the nearest 2048 bytes. Savings can be significant with
**	a large volume containing lots of smallish files.
**
**	Updated for v1.12 - now uses the built in RELOCATED_DIRECTORY
**	flag for finding the real directory location JCP 8/1/97
*/

#ifdef APPLE_HYB

#include "config.h"
#include "mkisofs.h"
#include "volume.h"
#include "write.h"
#include <errno.h>

/* from desktop.c */
int make_desktop(hfsvol *, int);
/* from libhfs_iso/hfs.c */
void hfs_vsetbless(hfsvol *, unsigned long);

static hfsvol *vol_save = 0;	/* used to "destroy" an HFS volume */

int DECL(copy_to_mac_vol, (hfsvol *, struct directory *));

/*
**	AlcSiz: find allocation size for given volume size
*/
int
AlcSiz(int vlen)
{
	int	lpa, drAlBlkSiz;

	/* code extracted from hfs_format() */
	lpa = 1 + vlen / 65536;
	drAlBlkSiz = lpa * HFS_BLOCKSZ;

	/* now set our "allocation size" to the allocation block rounded
	   up to the nearest SECTOR_SIZE (2048 bytes)  */
	drAlBlkSiz = V_ROUND_UP(drAlBlkSiz, SECTOR_SIZE);

	return(drAlBlkSiz);
}

/*
**	XClpSiz: find the default size of the catalog/extent file
*/
int
XClpSiz(int vlen)
{
	int	olpa, lpa, drNmAlBlks, drAlBlkSiz;
	int	vbmsz, drXTClpSiz;

	/* code extracted from hfs_format() */

	/* get the lpa from our calculated allocation block size */
	drAlBlkSiz = AlcSiz(vlen);
	lpa = drAlBlkSiz/HFS_BLOCKSZ;

	vbmsz = (vlen / lpa + 4095) / 4096;
	drNmAlBlks = (vlen - 5 - vbmsz) / lpa;
	drXTClpSiz = drNmAlBlks / 128 * drAlBlkSiz;

	/* make allowances because we have possibly rounded up the
	   allocation size */

	/* get the "original" lpa " */
	olpa = 1 + vlen / 65536;

	/* adjust size upwards */
	drXTClpSiz = (drXTClpSiz*lpa)/olpa;

	/* round up to the nearest alloaction size */
	drXTClpSiz = V_ROUND_UP(drXTClpSiz, drAlBlkSiz);

	return(drXTClpSiz);
}

/*
**	get_vol_size: get the size of the volume including the extent/catalog
*/
int
get_vol_size(int vblen)
{
	int	drXTClpSiz, drAlBlkSiz;
	int	new_vblen;

	/* try to estimate a "volume size" based on the code
	   in hfs_format - we need the size of the catalog/extents
	   and Desktop files included in the volume, as we add this
	   to the end of the ISO volume */

	drXTClpSiz = XClpSiz(vblen);
	drAlBlkSiz = AlcSiz(vblen);

	/* catalog file is set at CTC times (default twice) the extents file
	   size - hence the (ctc_size + 1) below. The Desktop starts of the
	   same size as the "clump size" == 4 x drAlBlkSiz, plus a spare
	   drAlBlkSiz for the alternative MDB */

	new_vblen = vblen + ((hce->ctc_size + 1)*drXTClpSiz + 5*drAlBlkSiz)/HFS_BLOCKSZ;

	return (new_vblen);
}

/*
**	write_fork: "write" file data to the volume
**
**	This is used to update the HFS file internal structures
**	but no data is actually written (it's trapped deep down in
**	libhfs).
*/
int
write_fork(hfsfile *hfp, long tot)
{
	char	blk[HFS_BLOCKSZ];
	unsigned short start;
	long	len;

	len = tot;
	/* we need to know where this fork starts */
	start = hfs_get_drAllocPtr(hfp);

	/* loop through the data a block at a time */
	while (len >= HFS_BLOCKSZ)
	{
	    if(hfs_write(hfp, blk, HFS_BLOCKSZ) < 0)
		return(-1);
	    len -= HFS_BLOCKSZ;
	}
	/* write out anything left */
	if (len)
	    if(hfs_write(hfp, blk, len) < 0)
		return(-1);

	/* set the start of the allocation search to be immediately
	   after this fork */
	hfs_set_drAllocPtr(hfp, start, tot);

	return(0);
}

/*
**	make_mac_volume: "create" an HFS volume using the ISO data
**
**	The HFS volume structures are set up (but no data is written yet).
**
**	ISO volumes have a allocation size of 2048 bytes - regardless
**	of the size of the volume. HFS allocation size is depends on volume
**	size, so we may have to update the ISO structures to add in any
**	padding.
*/
int FDECL2(make_mac_volume, struct directory *, dpnt, int, start_extent)
{
	char vol_name[HFS_MAX_VLEN+1];	/* Mac volume name */
	hfsvol *vol;			/* Mac volume */
	int vlen, vblen;		/* vol length (bytes, blocks) */
	int Csize, lastCsize;		/* allocation sizes */
	int ret = 0;			/* return value */
	int loop = 1;

	/* umount volume if we have had a previous attempt */
	if (vol_save)
	    if (hfs_umount(vol_save, 0) < 0)
		return (-1);

	/* set the default clump size to the ISO block size */
	lastCsize = SECTOR_SIZE;

	if (verbose > 1)
	    fprintf(stderr, "Creating HFS Volume info\n");

	/* name or copy ISO volume name to Mac Volume name */
	strncpy(vol_name, hfs_volume_id ? hfs_volume_id : volume_id, HFS_MAX_VLEN);
	vol_name[HFS_MAX_VLEN] = '\0';

	/* get initial size of HFS volume (size of ISO volume) */
	vblen = last_extent * BLK_CONV;

	/* add on size of extents/catalog file, but this may mean
	   the allocation size will change, so loop round until the allocation
	   size doesn't change */
	while (loop) {
	    hce->XTCsize = XClpSiz(vblen);
	    vblen = get_vol_size(vblen);
	    Csize = AlcSiz(vblen);

	    if (Csize == lastCsize) {
		/* allocation size hasn't changed, so carry on */
		loop = 0;
	    }
	    else {
		/* allocation size has changed, so update ISO volume size */
		if ((vlen = get_adj_size(Csize)) < 0) {
		    snprintf(hce->error, ERROR_SIZE, 
		    	"too many files for HFS volume");
		    return (-1);
		}
		vlen += V_ROUND_UP(start_extent * SECTOR_SIZE, Csize);
		vblen = vlen /  HFS_BLOCKSZ;
		lastCsize = Csize;
	    }
	}

	/* set vlen to size in bytes */
/*	vlen = hce->hfs_vol_size = vblen * HFS_BLOCKSZ; */
	/* take off the label/map size */
	vblen -= hce->hfs_map_size;
	vlen = hce->hfs_vol_size = vblen * HFS_BLOCKSZ;

	/* set the default allocation size for libhfs */
	hce->Csize = Csize;

	/* format and mount the "volume" */
	if (hfs_format(hce, 0, vol_name) < 0)
	{
	    snprintf(hce->error, ERROR_SIZE, "can't HFS format %s",vol_name);
	    return(-1);
	}

	/* update the ISO structures with new start extents and any padding
	   required */
	if (Csize != SECTOR_SIZE) {
	    last_extent = adj_size(Csize, start_extent, hce->hfs_hdr_size + hce->hfs_map_size);
	    adj_size_other(dpnt);
	}

	if ((vol = hfs_mount(hce, 0, 0)) == 0)
	{
	    snprintf(hce->error, ERROR_SIZE, "can't HFS mount %s",vol_name);
	    return(-1);
	}

	/* save the volume for possible later use */
	vol_save = vol;

	/* Recursively "copy" the files to the volume - we need to
	   know the first allocation block in the volume as starting blocks
	   of files are relative to this.
	*/
	ret = copy_to_mac_vol(vol, dpnt);
	if (ret < 0)
	    return(ret);

	/* make the Desktop files - I *think* this stops the Mac
	   rebuilding the desktop when the CD is mounted on a Mac
	   These will be ignored if they already exist */
	if (create_dt)
	    ret = make_desktop(vol, last_extent*BLK_CONV);
	if (ret < 0)
	    return(ret);

	/* close the volume */
	if (hfs_flush(vol) < 0)
	    return(-1);

	/* unmount and set the start blocks for the catalog/extents files */
	if (hfs_umount(vol, last_extent*BLK_CONV) < 0)
	    return(-1);

	return(Csize);
}

#define TEN 10		/* well, it is! */
#define LCHAR "_"

/*	copy_to_mac_vol: copy all files in a directory to corresponding
**			 Mac folder.
**
**	Files are copied recursively to corresponding folders on the Mac
**	volume. The caller routine needs to do a hfs_chdir before calling this
**	routine. 
*/
int FDECL2(copy_to_mac_vol, hfsvol *, vol, struct directory *, node)
{
	struct directory_entry * s_entry;	/* ISO directory entry */
	struct directory_entry * s_entry1;	/* tmp ISO directory entry */
	struct directory *dpnt;			/* ISO directory */

	hfsfile *hfp;				/* HFS file */
	hfsdirent *ent;				/* HFS file entities */
	long id;				/* current HFS folder */
	long dext, rext;			/* real data/rsrc start blk */
	int ret;				/* result code */
	int new_name;				/* HFS file has modified name */

	int	tens;
	int	digits;
	int	i;

	/* store the current HFS directory ID */
	if ((id = hfs_getcwd(vol)) == 0)
	    return(-1);

	if (verbose > 1)
	    fprintf(stderr,"HFS scanning %s\n", node->whole_name);

	/* loop through the ISO directory entries and process files */
	for(s_entry = node->contents; s_entry; s_entry = s_entry->next)
	{
	    /* ignore directory and associated (rsrc) files */
	    if(s_entry->isorec.flags[0])
		    continue;

	    /* ignore any non-Mac type file */
	    if(!s_entry->hfs_ent)
		    continue;

#ifdef DEBUG
	    fprintf(stderr," Name = %s", s_entry->whole_name); 
	    fprintf(stderr,"   Startb =  %d\n", s_entry->starting_block);
#endif /* DEBUG */

	    ent = s_entry->hfs_ent;

	    /* create file */
	    i = HFS_MAX_FLEN - strlen(ent->name);
	    new_name = 0;
	    tens = TEN;
	    digits = 1;

	    while (1)
	    {
		/* try to open file - if it exists, then append '_' to
		   the name and try again */
		errno = 0;
		if ((hfs_create(vol, ent->name, ent->type, ent->creator)) < 0)
		{
		    if (errno != EEXIST )
		    {
			/* not an "exist" error, or we can't append as
			   the filename is already HFS_MAX_FLEN chars */
			snprintf(hce->error, ERROR_SIZE, 
				"can't HFS create file %s",
				s_entry->whole_name);
			return(-1);
		    }
		    else if (i == 0)
		    {
			/* File name at max HFS length - make unique name */
			if (!new_name) new_name++;

			sprintf(ent->name + HFS_MAX_FLEN - digits - 1,
					"%s%d", LCHAR, new_name);
			new_name++;
			if (new_name == tens) {
			    tens *= TEN;
			    digits++;
			}
		    }
		    else
		    {
			/* append '_' to get new name */
			strcat(ent->name, LCHAR);
			i--;
			new_name = 1;
		    }
		}
		else
		    break;
	    }

	    /* warn that we have a new name */
	    if (new_name && verbose > 0)
	    {
		fprintf(stderr, "Using HFS name: %s for %s\n", ent->name,
			s_entry->whole_name);
	    }

	    /* open file */
	    if ((hfp = hfs_open(vol, ent->name)) == 0)
	    {
		snprintf(hce->error, ERROR_SIZE, "can't HFS open %s", 
		    s_entry->whole_name);
		return(-1);
	    }

	    /* if it has a data fork, then "write" it out */
	    if (ent->dsize)
		write_fork(hfp, ent->dsize);

	    /* if it has a resource fork, set the fork and "write" it out */
	    if (ent->rsize)
	    {
		if ((hfs_setfork(hfp, 1)) < 0)
		    return(-1);
		write_fork(hfp, ent->rsize);
	    }

	    /* update any HFS file attributes */
	    if ((hfs_fsetattr(hfp, ent)) < 0)
	    {
		snprintf(hce->error, ERROR_SIZE, "can't HFS set attributes %s",
			s_entry->whole_name);
		return(-1);
	    }

	    /* get the ISO starting block of data fork (may be zero)
	       and convert to the equivalent HFS block */
	    if (ent->dsize)
		dext = s_entry->starting_block * BLK_CONV;
	    else
		dext = 0;

	    /* if the file has a resource fork (associated file), get it's
	       ISO starting block and convert as above */
	    if (s_entry->assoc && ent->rsize)
		rext = s_entry->assoc->starting_block * BLK_CONV;
	    else
		rext = 0;

	    /* close the file and update the starting blocks */
	    if (hfs_close(hfp, dext, rext) < 0)
	    {
		snprintf(hce->error, ERROR_SIZE, "can't HFS close file %s",
			s_entry->whole_name);
		return(-1);
	    }
	}

	/* process sub-directories  - have a slight problem here,
	   if the directory had been relocated, then we need to find
	   the real directory - we do this by first finding the real
	   directory_entry, and then finding it's directory info */

	/* following code taken from joliet.c */
	for(s_entry=node->contents;s_entry;s_entry=s_entry->next)
	{
	    if((s_entry->de_flags & RELOCATED_DIRECTORY) != 0)
	    {
		/* if the directory has been reloacted, then search the
		   relocated directory for the real entry */
		for(s_entry1=reloc_dir->contents;s_entry1;s_entry1=s_entry1->next)
		{
		    if(s_entry1->parent_rec == s_entry)
			break;
		}

		/* have a problem - can't find the real directory */
		if(s_entry1 == NULL)
		{
		    snprintf(hce->error, ERROR_SIZE,
		    	"can't locate relocated directory %s", 
			s_entry->whole_name);
		    return(-1);
		}
	    }
	    else
		s_entry1 = s_entry;

	    /* now have the correct entry - now find the actual directory */
	    if ((s_entry1->isorec.flags[0] & 2) && strcmp(s_entry1->name,".") && strcmp(s_entry1->name,".."))
	    {
		if((s_entry->de_flags & RELOCATED_DIRECTORY) != 0)
		    dpnt = reloc_dir->subdir;
		else
		    dpnt = node->subdir;

		while(1)
		{
		    if (dpnt->self == s_entry1)
			break;
		    dpnt = dpnt->next;
		    if(!dpnt)
		    {
			snprintf(hce->error, ERROR_SIZE, 
			    "can't find directory location %s", 
			    s_entry1->whole_name);
			return (-1);
		    }
		}
		/* now have the correct directory - so do the HFS stuff */
		ent = dpnt->hfs_ent;

		/* if we don't have hfs entries, then this is a "deep"
		   directory - this will be processed later */
		if (!ent)
		    continue;

		/* make sub-folder */
		i = HFS_MAX_FLEN - strlen(ent->name);
		new_name = 0;
		tens = TEN;
		digits = 1;

		while (1)
		{
		    /* try to create new directory  - if it exists, then
		       append '_' to the name and try again */
		    errno = 0;
		    if (hfs_mkdir(vol, ent->name) < 0)
		    {
			if (errno != EEXIST)
			{
			    /* not an "exist" error, or we can't append as
			       the filename is already HFS_MAX_FLEN chars */
			    snprintf(hce->error, ERROR_SIZE, 
			    	"can't HFS create folder %s",
				s_entry->whole_name);
			    return(-1);
			}
			else if (i == 0)
			{
			    /* File name at max HFS length - make unique name */
			    if (!new_name) new_name++;

			    sprintf(ent->name + HFS_MAX_FLEN - digits - 1,
					"%s%d", LCHAR, new_name);
			    new_name++;
			    if (new_name == tens) {
				tens *= TEN;
				digits++;
			    }
			}
			else
			{
			    /* append '_' to get new name */
			    strcat(ent->name, LCHAR);
			    i--;
			    new_name = 1;
			}
		    }
		    else
			break;
		}

		/* warn that we have a new name */
		if (new_name && verbose > 0)
		{
		    fprintf(stderr, "Using HFS name: %s for %s\n", ent->name,
			s_entry->whole_name);
		}

		/* see if we need to "bless" this folder */
		if (hfs_bless && strcmp(s_entry->whole_name, hfs_bless) == 0) {
		    hfs_stat(vol, ent->name, ent);
		    hfs_vsetbless(vol, ent->cnid);
		    if (verbose > 0) {
			fprintf(stderr, "Blessing %s (%s)\n",
				ent->name, s_entry->whole_name);
		    }
		    /* stop any further checks */
		    hfs_bless = NULL;
		}

		/* change to sub-folder */
		if (hfs_chdir(vol, ent->name) < 0)
		    return(-1);

		/* recursively copy files ... */
		ret = copy_to_mac_vol(vol, dpnt);
		if (ret < 0)
		    return(ret);

		/* change back to this folder */
		if (hfs_setcwd(vol, id) < 0)
		    return(-1);
	    }
	}

	return(0);
}
#endif /* APPLE_HYB */ 

