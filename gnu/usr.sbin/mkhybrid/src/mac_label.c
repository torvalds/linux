/*
**	mac_label.c: generate Mactintosh partition maps and label
**
**	Taken from "mkisofs 1.05 PLUS" by Andy Polyakov <appro@fy.chalmers.se>
**	(see http://fy.chalmers.se/~appro/mkisofs_plus.html for details)
**
**	The format of the HFS driver file:
**
**	HFS CD Label Block				512 bytes
**	Driver Partition Map (for 2048 byte blocks)	512 bytes
**	Driver Partition Map (for 512 byte blocks)	512 bytes
**	Empty						512 bytes
**	Driver Partition				N x 2048 bytes
**	HFS Partition Boot Block			1024 bytes
**
**	File of the above format can be extracted from a CD using
**	apple_driver.c
**
**	James Pearson 16/5/98
*/

#include <config.h>
#include <mkisofs.h>
#include "mac_label_proto.h"
#include <mac_label.h>

/* from libhfs_iso/data.h */
short d_getw(unsigned char *);

int
gen_mac_label(defer *mac_boot)
{
    FILE     *fp;
    MacLabel *mac_label;
    MacPart  *mac_part;
    char     *buffer = hce->hfs_map;
    int       block_size;
    int	      have_hfs_boot = 0;
    char      tmp[SECTOR_SIZE];
    struct stat stat_buf;
    mac_partition_table mpm[2];
    int        mpc = 0;
    int	       i;

    /* If we have a boot file, then open and check it */
    if (mac_boot->name) {
	if (stat(mac_boot->name, &stat_buf) < 0) {
	    snprintf(hce->error, ERROR_SIZE, 
	    	"unable to stat HFS boot file %s", mac_boot->name);
	    return (-1);
	}


	if ((fp = fopen(mac_boot->name, "rb")) == NULL) {
	    snprintf(hce->error, ERROR_SIZE,
	    	"unable to open HFS boot file %s", mac_boot->name);
	    return (-1);
	}

	if (fread(tmp, 1, SECTOR_SIZE, fp) != SECTOR_SIZE) {
	    snprintf(hce->error, ERROR_SIZE, 
	    	"unable to read HFS boot file %s", mac_boot->name);
	    return (-1);
	}


	/* check we have a bootable partition */
	mac_part = (MacPart*)(tmp+HFS_BLOCKSZ);

	if (!(IS_MAC_PART(mac_part) && !strncmp(mac_part->pmPartType, pmPartType_2, 12))) {
	    snprintf(hce->error, ERROR_SIZE, "%s is not a HFS boot file", 
	    	mac_boot->name);
	    return (-1);
	}

	/* check we have a boot block as well - last 2 blocks of file */

	if (fseek(fp, -2 * HFS_BLOCKSZ, 2) != 0) {
	    snprintf(hce->error, ERROR_SIZE, 
	    	"unable to seek HFS boot file %s", mac_boot->name);
	    return (-1);
	}

	/* overwrite (empty) boot block for our HFS volume */
	if (fread(hce->hfs_hdr, 2, HFS_BLOCKSZ, fp) != HFS_BLOCKSZ) {
	    snprintf(hce->error, ERROR_SIZE, 
	    	"unable to read HFS boot block %s", mac_boot->name);
	    return (-1);
	}

	fclose (fp);

	/* check boot block is valid */
	if (d_getw((unsigned char *)hce->hfs_hdr) != HFS_BB_SIGWORD) {
	    snprintf(hce->error, ERROR_SIZE,
	    	"%s does not contain a valid boot block", mac_boot->name);
	    return (-1);
	}

	/* collect info about boot file for later user - skip over the boot
	   file header */
	mac_boot->size = stat_buf.st_size - SECTOR_SIZE - 2*HFS_BLOCKSZ;
	mac_boot->off = SECTOR_SIZE;
	mac_boot->pad = 0;

	/* get size in SECTOR_SIZE blocks - shouldn't need to round up */
	mpm[mpc].size = ROUND_UP(mac_boot->size)/SECTOR_SIZE;

	mpm[mpc].ntype = PM2;
	mpm[mpc].type = mac_part->pmPartType;
	mpm[mpc].start = mac_boot->extent = last_extent;
	mpm[mpc].name = 0;

	/* flag that we have a boot file */
	have_hfs_boot++;

	/* add boot file size to the total size */
	last_extent += mpm[mpc].size;
	hfs_extra += mpm[mpc].size;

	mpc++;
    }

    /* set info about our hybrid volume */
    mpm[mpc].ntype = PM4;
    mpm[mpc].type = pmPartType_4;
    mpm[mpc].start = hce->hfs_map_size / BLK_CONV;
    mpm[mpc].size = last_extent - mpm[mpc].start;
    mpm[mpc].name = volume_id;

    mpc++;

    if (verbose > 1)
	fprintf(stderr, "Creating HFS Label %s %s\n", mac_boot->name ?
		"with boot file" : "", mac_boot->name ? mac_boot->name : "" );

    /* for a bootable CD, block size is SECTOR_SIZE */
    block_size = have_hfs_boot ? SECTOR_SIZE : HFS_BLOCKSZ;

    /* create the CD label */
    mac_label = (MacLabel *)buffer;
    mac_label->sbSig [0] = 'E';
    mac_label->sbSig [1] = 'R';
    set_722 (mac_label->sbBlkSize,block_size);
    set_732 (mac_label->sbBlkCount,last_extent*(SECTOR_SIZE/block_size));
    set_722 (mac_label->sbDevType,1);
    set_722 (mac_label->sbDevId,1);

    /* create the partition map entry */
    mac_part = (MacPart*)(buffer+block_size);
    mac_part->pmSig [0] = 'P';
    mac_part->pmSig [1] = 'M';
    set_732 (mac_part->pmMapBlkCnt,mpc+1);
    set_732 (mac_part->pmPyPartStart,1);
    set_732 (mac_part->pmPartBlkCnt,mpc+1);
    strncpy (mac_part->pmPartName,"Apple",sizeof(mac_part->pmPartName));
    strncpy (mac_part->pmPartType,"Apple_partition_map",sizeof(mac_part->pmPartType));
    set_732 (mac_part->pmLgDataStart,0);
    set_732 (mac_part->pmDataCnt,mpc+1);
    set_732 (mac_part->pmPartStatus,PM_STAT_DEFAULT);

    /* create partition map entries for our partitions */
    for (i=0;i<mpc;i++) {
	mac_part = (MacPart*)(buffer + (i+2)*block_size);
	if (mpm[i].ntype == PM2) {
	    /* get driver label and patch it */
	    memcpy (mac_label, tmp, HFS_BLOCKSZ);
	    set_732 (mac_label->sbBlkCount,last_extent*(SECTOR_SIZE/block_size));
	    set_732 (mac_label->ddBlock,(mpm[i].start)*(SECTOR_SIZE/block_size));
	    memcpy (mac_part, tmp+HFS_BLOCKSZ, HFS_BLOCKSZ);
	    set_732 (mac_part->pmMapBlkCnt,mpc+1);
	    set_732 (mac_part->pmPyPartStart,(mpm[i].start)*(SECTOR_SIZE/block_size));
	}
	else {
	    mac_part->pmSig [0] = 'P';
	    mac_part->pmSig [1] = 'M';
	    set_732 (mac_part->pmMapBlkCnt,mpc+1);
	    set_732 (mac_part->pmPyPartStart,mpm[i].start*(SECTOR_SIZE/HFS_BLOCKSZ));
	    set_732 (mac_part->pmPartBlkCnt,mpm[i].size*(SECTOR_SIZE/HFS_BLOCKSZ));
	    strncpy (mac_part->pmPartName,mpm[i].name,sizeof(mac_part->pmPartName));
	    strncpy (mac_part->pmPartType,mpm[i].type,sizeof(mac_part->pmPartType));
	    set_732 (mac_part->pmLgDataStart,0);
	    set_732 (mac_part->pmDataCnt,mpm[i].size*(SECTOR_SIZE/HFS_BLOCKSZ));
	    set_732 (mac_part->pmPartStatus,PM_STAT_DEFAULT);
	}
    }

    if (have_hfs_boot) { /* generate 512 partition table as well */
	mac_part = (MacPart*)(buffer+HFS_BLOCKSZ);
	if (mpc<3) { /* don't have to interleave with 2048 table */
	    mac_part->pmSig [0] = 'P';
	    mac_part->pmSig [1] = 'M';
	    set_732 (mac_part->pmMapBlkCnt,mpc+1);
	    set_732 (mac_part->pmPyPartStart,1);
	    set_732 (mac_part->pmPartBlkCnt,mpc+1);
	    strncpy (mac_part->pmPartName,"Apple",sizeof(mac_part->pmPartName));
	    strncpy (mac_part->pmPartType,"Apple_partition_map",sizeof(mac_part->pmPartType));
	    set_732 (mac_part->pmLgDataStart,0);
	    set_732 (mac_part->pmDataCnt,mpc+1);
	    set_732 (mac_part->pmPartStatus,PM_STAT_DEFAULT);
	    mac_part++; /* +HFS_BLOCKSZ */
	}
	for (i=0;i<mpc;i++,mac_part++) {
	    if (mac_part == (MacPart*)(buffer+SECTOR_SIZE)) mac_part++; /* jump over 2048 partition entry */
	    if (mpm[i].ntype == PM2) {
		memcpy (mac_part, tmp+HFS_BLOCKSZ*2, HFS_BLOCKSZ);
		if (!IS_MAC_PART(mac_part)) { mac_part--; continue; }
		set_732 (mac_part->pmMapBlkCnt,mpc+1);
		set_732 (mac_part->pmPyPartStart,(mpm[i].start)*(SECTOR_SIZE/HFS_BLOCKSZ));
	    }
	    else {
		mac_part->pmSig [0] = 'P';
		mac_part->pmSig [1] = 'M';
		set_732 (mac_part->pmMapBlkCnt,mpc+1);
		set_732 (mac_part->pmPyPartStart,mpm[i].start*(SECTOR_SIZE/HFS_BLOCKSZ));
		set_732 (mac_part->pmPartBlkCnt,mpm[i].size*(SECTOR_SIZE/HFS_BLOCKSZ));
		strncpy (mac_part->pmPartName,mpm[i].name,sizeof(mac_part->pmPartName));
		strncpy (mac_part->pmPartType,mpm[i].type,sizeof(mac_part->pmPartType));
		set_732 (mac_part->pmLgDataStart,0);
		set_732 (mac_part->pmDataCnt,mpm[i].size*(SECTOR_SIZE/HFS_BLOCKSZ));
		set_732 (mac_part->pmPartStatus,PM_STAT_DEFAULT);
	    }
	}
    }

    return (0);
}

/*
**	autostart: make the HFS CD use the QuickTime 2.0 Autostart feature.
**
**	based on information from Eric Eisenhart <eric@sonic.net> and
**	http://developer.apple.com/qa/qtpc/qtpc12.html and
**	http://developer.apple.com/dev/techsupport/develop/issue26/macqa.html
**
**	The name of the AutoStart file is stored in the area allocated for
**	the Clipboard name. This area begins 106 bytes into the sector of
**	block 0, with the first four bytes at that offset containing the
**	hex value 0x006A7068. This value indicates that an AutoStart
**	filename follows. After this 4-byte tag, 12 bytes remain, starting
**	at offset 110. In these 12 bytes, the name of the AutoStart file is
**	stored as a Pascal string, giving you up to 11 characters to identify
**	the file. The file must reside in the root directory of the HFS
**	volume or partition.
*/

int
autostart()
{
	int	len, i;

	if((len = strlen(autoname)) > 11)
	    return (-1);

	hce->hfs_hdr[106] = 0x00;
	hce->hfs_hdr[107] = 0x6A;
	hce->hfs_hdr[108] = 0x70; 
	hce->hfs_hdr[109] = 0x68;
	hce->hfs_hdr[110] = len;

	for(i=0;i<len;i++)
	    hce->hfs_hdr[111+i] = autoname[i];

	return (0);
}
