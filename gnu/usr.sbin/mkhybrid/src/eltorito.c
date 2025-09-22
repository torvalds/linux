/*
 * Program eltorito.c - Handle El Torito specific extensions to iso9660.
 * 

   Written by Michael Fulbright <msf@redhat.com> (1996).

   Copyright 1996 RedHat Software, Incorporated

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   */



#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include "config.h"
#include "mkisofs.h"
#include "iso9660.h"

/* used by Win32 for opening binary file - not used by Unix */
#ifndef O_BINARY
#define O_BINARY 0
#endif /* O_BINARY */

#undef MIN
#define MIN(a, b) (((a) < (b))? (a): (b))

static struct eltorito_validation_entry valid_desc;
static struct eltorito_defaultboot_entry default_desc;
static struct eltorito_boot_descriptor gboot_desc;
static struct eltorito_sectionheader_entry shdr_desc;
static struct eltorito_defaultboot_entry efi_desc;

static int tvd_write	__PR((FILE * outfile));

/*
 * Check for presence of boot catalog. If it does not exist then make it 
 */
void FDECL1(init_boot_catalog, const char *, path)
{

    int		  bcat;
    char		* bootpath;                /* filename of boot catalog */
    char		* buf;
    struct stat	  statbuf;
    
    bootpath = (char *) e_malloc(strlen(boot_catalog)+strlen(path)+2);
    strcpy(bootpath, path);
    if (bootpath[strlen(bootpath)-1] != '/') 
    {
	strcat(bootpath,"/");
    }
    
    strcat(bootpath, boot_catalog);
    
    /*
     * check for the file existing 
     */
#ifdef DEBUG_TORITO
    fprintf(stderr,"Looking for boot catalog file %s\n",bootpath);
#endif
    
    if (!stat_filter(bootpath, &statbuf)) 
    {
	/*
	 * make sure its big enough to hold what we want 
	 */
	if (statbuf.st_size == 2048) 
	{
	    /*
	     * printf("Boot catalog exists, so we do nothing\n"); 
	     */
	    free(bootpath);
	    return;
	}
	else 
	{
	    fprintf(stderr, "A boot catalog exists and appears corrupted.\n");
	    fprintf(stderr, "Please check the following file: %s.\n",bootpath);
	    fprintf(stderr, "This file must be removed before a bootable CD can be done.\n");
	    free(bootpath);
	    exit(1);
	}
    }
    
    /*
     * file does not exist, so we create it 
     * make it one CD sector long
     */
    bcat = open(bootpath, O_WRONLY | O_CREAT | O_BINARY, S_IROTH | S_IRGRP | S_IRWXU );
    if (bcat == -1) 
    {
	fprintf(stderr, "Error creating boot catalog, exiting...\n");
	perror("");
	exit(1);
    }
    
    buf = (char *) e_malloc( 2048 );
    write(bcat, buf, 2048);
    close(bcat);
    free(bootpath);
} /* init_boot_catalog(... */

void FDECL1(get_torito_desc, struct eltorito_boot_descriptor *, boot_desc)
{
    int				bootcat;
    int				checksum;
    unsigned char		      * checksum_ptr;
    struct directory_entry      * de = NULL;
    struct directory_entry      * de2;
    struct directory_entry      * efi_de = NULL;
    int				i;
    int				nsectors;
    
    memset(boot_desc, 0, sizeof(*boot_desc));
    boot_desc->id[0] = 0;
    memcpy(boot_desc->id2, ISO_STANDARD_ID, sizeof(ISO_STANDARD_ID) - 1);
    boot_desc->version[0] = 1;
    
    memcpy(boot_desc->system_id, EL_TORITO_ID, sizeof(EL_TORITO_ID));
    
    /*
     * search from root of iso fs to find boot catalog 
     */
    de2 = search_tree_file(root, boot_catalog);
    if (!de2) 
    {
	fprintf(stderr,"Uh oh, I cant find the boot catalog!\n");
	exit(1);
    }
    
    set_731(boot_desc->bootcat_ptr,
	    (unsigned int) get_733(de2->isorec.extent));
    
    /* 
     * now adjust boot catalog
     * lets find boot image first 
     */
    if (boot_image != NULL)
	de=search_tree_file(root, boot_image);
    if (efi_boot_image != NULL)
        efi_de=search_tree_file(root, efi_boot_image);

    if (de == NULL && efi_boot_image == NULL)
    {
	fprintf(stderr,"Uh oh, I cant find the boot image!\n");
	exit(1);
    } 
    
    /* 
     * we have the boot image, so write boot catalog information
     * Next we write out the primary descriptor for the disc 
     */
    memset(&valid_desc, 0, sizeof(valid_desc));
    valid_desc.headerid[0] = 1;
    valid_desc.arch[0] =
	(boot_image != NULL)? EL_TORITO_ARCH_x86 : EL_TORITO_ARCH_EFI;
    
    /*
     * we'll shove start of publisher id into id field, may get truncated
     * but who really reads this stuff!
     */
    if (publisher)
        memcpy_max(valid_desc.id,  publisher, MIN(23, strlen(publisher)));
    
    valid_desc.key1[0] = 0x55;
    valid_desc.key2[0] = 0xAA;
    
    /*
     * compute the checksum 
     */
    checksum=0;
    checksum_ptr = (unsigned char *) &valid_desc;
    for (i=0; i<sizeof(valid_desc); i+=2) 
    {
	/*
	 * skip adding in ckecksum word, since we dont have it yet! 
	 */
	if (i == 28)
	{
	    continue;
	}
	checksum += (unsigned int)checksum_ptr[i];
	checksum += ((unsigned int)checksum_ptr[i+1])*256;
    }
    
    /* 
     * now find out the real checksum 
     */
    checksum = -checksum;
    set_721(valid_desc.cksum, (unsigned int) checksum);
    
    if (de == NULL)
	goto skip_x86;
    /*
     * now make the initial/default entry for boot catalog 
     */
    memset(&default_desc, 0, sizeof(default_desc));
    default_desc.boot_id[0] = EL_TORITO_BOOTABLE;
    
    /*
     * use default BIOS loadpnt
     */ 
    set_721(default_desc.loadseg, 0);
    default_desc.arch[0] = EL_TORITO_ARCH_x86;
    
    /*
     * figure out size of boot image in sectors, for now hard code to
     * assume 512 bytes/sector on a bootable floppy
     */
    nsectors = ((de->size + 511) & ~(511))/512;
#ifdef APPLE_HYB
    /* NON-HFS change */
    if (verbose > 0 )
#endif /* APPLE_HYB */
	fprintf(stderr, "\nSize of boot image is %d sectors -> ", nsectors); 
    
    /*
     * choose size of emulated floppy based on boot image size 
     */
    if (nsectors == 2880 ) 
    {
	default_desc.boot_media[0] = EL_TORITO_MEDIA_144FLOP;
#ifdef APPLE_HYB
	/* NON-HFS change */
	if (verbose > 0 )
#endif /* APPLE_HYB */
	    fprintf(stderr, "Emulating a 1.44 meg floppy\n");
    }
    else if (nsectors == 5760 ) 
    {
	default_desc.boot_media[0] = EL_TORITO_MEDIA_288FLOP;
#ifdef APPLE_HYB
	/* NON-HFS change */
	if (verbose > 0 )
#endif /* APPLE_HYB */
	    fprintf(stderr,"Emulating a 2.88 meg floppy\n");
    }
    else if (nsectors == 2400 ) 
    {
	default_desc.boot_media[0] = EL_TORITO_MEDIA_12FLOP;
#ifdef APPLE_HYB
	/* NON-HFS change */
	if (verbose > 0 )
#endif /* APPLE_HYB */
	    fprintf(stderr,"Emulating a 1.2 meg floppy\n");
    }
    else if (nsectors == 4 ) 
    {
	default_desc.boot_media[0] = EL_TORITO_MEDIA_NOEMUL;
#ifdef APPLE_HYB
	/* NON-HFS change */
	if (verbose > 0 )
#endif /* APPLE_HYB */
	    fprintf(stderr,"No-emulation CD boot sector\n");
    }
    else 
    {
	fprintf(stderr,"\nError - boot image is not the an allowable size.\n");
	exit(1);
    }
    
    
    /* 
     * FOR NOW LOAD 1 SECTOR, JUST LIKE FLOPPY BOOT, unless it's no-emulation
     * boot.
     */
    if (default_desc.boot_media[0] != EL_TORITO_MEDIA_NOEMUL)
        nsectors = 1;
    set_721(default_desc.nsect, (unsigned int) nsectors );
#ifdef DEBUG_TORITO
    fprintf(stderr,"Extent of boot images is %d\n",get_733(de->isorec.extent));
#endif
    set_731(default_desc.bootoff, 
	    (unsigned int) get_733(de->isorec.extent));
 skip_x86:
    /*
     * add the EFI boot image, if specified
     */
    if (efi_de != NULL) {
	if (de != NULL) {
	    memset(&shdr_desc, 0, sizeof(shdr_desc));
	    shdr_desc.header_id[0] = EL_TORITO_SHDR_ID_LAST_SHDR;
	    shdr_desc.platform_id[0] = EL_TORITO_ARCH_EFI;
	    set_721(shdr_desc.entry_count, 1);
	}

	memset(&efi_desc, 0, sizeof(efi_desc));
	efi_desc.boot_id[0] = EL_TORITO_BOOTABLE;
	set_721(efi_desc.loadseg, 0);
	efi_desc.arch[0] = EL_TORITO_ARCH_EFI;

	nsectors = ((efi_de->size + 511) & ~(511))/512;
	set_721(efi_desc.nsect, nsectors);
	set_731(efi_desc.bootoff, (unsigned int)get_733(efi_de->isorec.extent));
    }

    /*
     * now write it to disk 
     */
    bootcat = open(de2->whole_name, O_RDWR | O_BINARY);
    if (bootcat == -1) 
    {
	fprintf(stderr,"Error opening boot catalog for update.\n");
	perror("");
	exit(1);
    }
    
    /* 
     * write out 
     */
    write(bootcat, &valid_desc, 32);
    if (de != NULL)
    {
	write(bootcat, &default_desc, 32);
	if (efi_de != NULL)
	    write(bootcat, &shdr_desc, sizeof(shdr_desc));
    }
    if (efi_de != NULL)
	write(bootcat, &efi_desc, sizeof(efi_desc));
    close(bootcat);
} /* get_torito_desc(... */

/*
 * Function to write the EVD for the disc.
 */
static int FDECL1(tvd_write, FILE *, outfile)
{
  /*
   * Next we write out the boot volume descriptor for the disc 
   */
  get_torito_desc(&gboot_desc);
  xfwrite(&gboot_desc, 1, 2048, outfile);
  last_extent_written ++;
  return 0;
}

struct output_fragment torito_desc    = {NULL, oneblock_size, NULL,     tvd_write};
