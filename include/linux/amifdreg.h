#ifndef _LINUX_AMIFDREG_H
#define _LINUX_AMIFDREG_H

/*
** CIAAPRA bits (read only)
*/

#define DSKRDY      (0x1<<5)        /* disk ready when low */
#define DSKTRACK0   (0x1<<4)        /* head at track zero when low */
#define DSKPROT     (0x1<<3)        /* disk protected when low */
#define DSKCHANGE   (0x1<<2)        /* low when disk removed */

/*
** CIAAPRB bits (read/write)
*/

#define DSKMOTOR    (0x1<<7)        /* motor on when low */
#define DSKSEL3     (0x1<<6)        /* select drive 3 when low */
#define DSKSEL2     (0x1<<5)        /* select drive 2 when low */
#define DSKSEL1     (0x1<<4)        /* select drive 1 when low */
#define DSKSEL0     (0x1<<3)        /* select drive 0 when low */
#define DSKSIDE     (0x1<<2)        /* side selection: 0 = upper, 1 = lower */
#define DSKDIREC    (0x1<<1)        /* step direction: 0=in, 1=out (to trk 0) */
#define DSKSTEP     (0x1)           /* pulse low to step head 1 track */

/*
** DSKBYTR bits (read only)
*/

#define DSKBYT      (1<<15)         /* register contains valid byte when set */
#define DMAON       (1<<14)         /* disk DMA enabled */
#define DISKWRITE   (1<<13)         /* disk write bit in DSKLEN enabled */
#define WORDEQUAL   (1<<12)         /* DSKSYNC register match when true */
/* bits 7-0 are data */

/*
** ADKCON/ADKCONR bits
*/

#ifndef SETCLR
#define ADK_SETCLR      (1<<15)     /* control bit */
#endif
#define ADK_PRECOMP1    (1<<14)     /* precompensation selection */
#define ADK_PRECOMP0    (1<<13)     /* 00=none, 01=140ns, 10=280ns, 11=500ns */
#define ADK_MFMPREC     (1<<12)     /* 0=GCR precomp., 1=MFM precomp. */
#define ADK_WORDSYNC    (1<<10)     /* enable DSKSYNC auto DMA */
#define ADK_MSBSYNC     (1<<9)      /* when 1, enable sync on MSbit (for GCR) */
#define ADK_FAST        (1<<8)      /* bit cell: 0=2us (GCR), 1=1us (MFM) */
 
/*
** DSKLEN bits
*/

#define DSKLEN_DMAEN    (1<<15)
#define DSKLEN_WRITE    (1<<14)

/*
** INTENA/INTREQ bits
*/

#define DSKINDEX    (0x1<<4)        /* DSKINDEX bit */

/*
** Misc
*/
 
#define MFM_SYNC    0x4489          /* standard MFM sync value */

/* Values for FD_COMMAND */
#define FD_RECALIBRATE		0x07	/* move to track 0 */
#define FD_SEEK			0x0F	/* seek track */
#define FD_READ			0xE6	/* read with MT, MFM, SKip deleted */
#define FD_WRITE		0xC5	/* write with MT, MFM */
#define FD_SENSEI		0x08	/* Sense Interrupt Status */
#define FD_SPECIFY		0x03	/* specify HUT etc */
#define FD_FORMAT		0x4D	/* format one track */
#define FD_VERSION		0x10	/* get version code */
#define FD_CONFIGURE		0x13	/* configure FIFO operation */
#define FD_PERPENDICULAR	0x12	/* perpendicular r/w mode */

#endif /* _LINUX_AMIFDREG_H */
