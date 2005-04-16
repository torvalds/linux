#ifndef _AMIFD_H
#define _AMIFD_H

/* Definitions for the Amiga floppy driver */

#include <linux/fd.h>

#define FD_MAX_UNITS    4	/* Max. Number of drives */
#define FLOPPY_MAX_SECTORS	22	/* Max. Number of sectors per track */

#ifndef ASSEMBLER

struct fd_data_type {
    char *name;			/* description of data type */
    int sects;			/* sectors per track */
#ifdef __STDC__
    int (*read_fkt)(int);
    void (*write_fkt)(int);
#else
    int (*read_fkt)();		/* read whole track */
    void (*write_fkt)();		/* write whole track */
#endif
};

/*
** Floppy type descriptions
*/

struct fd_drive_type {
    unsigned long code;		/* code returned from drive */
    char *name;			/* description of drive */
    unsigned int tracks;	/* number of tracks */
    unsigned int heads;		/* number of heads */
    unsigned int read_size;	/* raw read size for one track */
    unsigned int write_size;	/* raw write size for one track */
    unsigned int sect_mult;	/* sectors and gap multiplier (HD = 2) */
    unsigned int precomp1;	/* start track for precomp 1 */
    unsigned int precomp2;	/* start track for precomp 2 */
    unsigned int step_delay;	/* time (in ms) for delay after step */
    unsigned int settle_time;	/* time to settle after dir change */
    unsigned int side_time;	/* time needed to change sides */
};

struct amiga_floppy_struct {
    struct fd_drive_type *type;	/* type of floppy for this unit */
    struct fd_data_type *dtype;	/* type of floppy for this unit */
    int track;			/* current track (-1 == unknown) */
    unsigned char *trackbuf;    /* current track (kmaloc()'d */

    int blocks;			/* total # blocks on disk */

    int changed;		/* true when not known */
    int disk;			/* disk in drive (-1 == unknown) */
    int motor;			/* true when motor is at speed */
    int busy;			/* true when drive is active */
    int dirty;			/* true when trackbuf is not on disk */
    int status;			/* current error code for unit */
    struct gendisk *gendisk;
};
#endif

#endif
