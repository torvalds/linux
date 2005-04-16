#ifndef _VTX_H
#define _VTX_H

/*
 * Teletext (=Videotext) hardware decoders using interface /dev/vtx
 * Do not confuse with drivers using /dev/vbi which decode videotext by software
 *
 * Videotext IOCTLs changed in order to use _IO() macros defined in <linux/ioctl.h>,
 * unused tuner IOCTLs cleaned up by
 * Michael Geng <linux@MichaelGeng.de>
 *
 * Copyright (c) 1994-97 Martin Buck  <martin-2.buck@student.uni-ulm.de>
 * Read COPYING for more information
 *
 */


/*
 *	Videotext ioctls
 */
#define VTXIOCGETINFO	_IOR  (0x81,  1, vtx_info_t)
#define VTXIOCCLRPAGE	_IOW  (0x81,  2, vtx_pagereq_t)
#define VTXIOCCLRFOUND	_IOW  (0x81,  3, vtx_pagereq_t)
#define VTXIOCPAGEREQ	_IOW  (0x81,  4, vtx_pagereq_t)
#define VTXIOCGETSTAT	_IOW  (0x81,  5, vtx_pagereq_t)
#define VTXIOCGETPAGE	_IOW  (0x81,  6, vtx_pagereq_t)
#define VTXIOCSTOPDAU	_IOW  (0x81,  7, vtx_pagereq_t)
#define VTXIOCPUTPAGE	_IO   (0x81,  8)
#define VTXIOCSETDISP	_IO   (0x81,  9)
#define VTXIOCPUTSTAT	_IO   (0x81, 10)
#define VTXIOCCLRCACHE	_IO   (0x81, 11)
#define VTXIOCSETVIRT	_IOW  (0x81, 12, long)

/* for compatibility, will go away some day */
#define VTXIOCGETINFO_OLD  0x7101  /* get version of driver & capabilities of vtx-chipset */
#define VTXIOCCLRPAGE_OLD  0x7102  /* clear page-buffer */
#define VTXIOCCLRFOUND_OLD 0x7103  /* clear bits indicating that page was found */
#define VTXIOCPAGEREQ_OLD  0x7104  /* search for page */
#define VTXIOCGETSTAT_OLD  0x7105  /* get status of page-buffer */
#define VTXIOCGETPAGE_OLD  0x7106  /* get contents of page-buffer */
#define VTXIOCSTOPDAU_OLD  0x7107  /* stop data acquisition unit */
#define VTXIOCPUTPAGE_OLD  0x7108  /* display page on TV-screen */
#define VTXIOCSETDISP_OLD  0x7109  /* set TV-mode */
#define VTXIOCPUTSTAT_OLD  0x710a  /* set status of TV-output-buffer */
#define VTXIOCCLRCACHE_OLD 0x710b  /* clear cache on VTX-interface (if avail.) */
#define VTXIOCSETVIRT_OLD  0x710c  /* turn on virtual mode (this disables TV-display) */

/* 
 *	Definitions for VTXIOCGETINFO
 */
 
#define SAA5243 0
#define SAA5246 1
#define SAA5249 2
#define SAA5248 3
#define XSTV5346 4

typedef struct {
	int version_major, version_minor;	/* version of driver; if version_major changes, driver */
						/* is not backward compatible!!! CHECK THIS!!! */  
	int numpages;				/* number of page-buffers of vtx-chipset */
	int cct_type;				/* type of vtx-chipset (SAA5243, SAA5246, SAA5248 or
  						 * SAA5249) */
}
vtx_info_t;


/*
 *	Definitions for VTXIOC{CLRPAGE,CLRFOUND,PAGEREQ,GETSTAT,GETPAGE,STOPDAU,PUTPAGE,SETDISP}
 */

#define MIN_UNIT   (1<<0)
#define MIN_TEN    (1<<1)
#define HR_UNIT    (1<<2)
#define HR_TEN     (1<<3)
#define PG_UNIT    (1<<4)
#define PG_TEN     (1<<5)
#define PG_HUND    (1<<6)
#define PGMASK_MAX (1<<7)
#define PGMASK_PAGE (PG_HUND | PG_TEN | PG_UNIT)
#define PGMASK_HOUR (HR_TEN | HR_UNIT)
#define PGMASK_MINUTE (MIN_TEN | MIN_UNIT)

typedef struct 
{
	int page;	/* number of requested page (hexadecimal) */
	int hour;	/* requested hour (hexadecimal) */
	int minute;	/* requested minute (hexadecimal) */
	int pagemask;	/* mask defining which values of the above are set */
	int pgbuf;	/* buffer where page will be stored */
	int start;	/* start of requested part of page */
	int end;	/* end of requested part of page */
	void __user *buffer;	/* pointer to beginning of destination buffer */
}
vtx_pagereq_t;


/*
 *	Definitions for VTXIOC{GETSTAT,PUTSTAT}
 */
 
#define VTX_PAGESIZE (40 * 24)
#define VTX_VIRTUALSIZE (40 * 49)

typedef struct 
{
	int pagenum;			/* number of page (hexadecimal) */
	int hour;			/* hour (hexadecimal) */
	int minute;			/* minute (hexadecimal) */
	int charset;			/* national charset */
	unsigned delete : 1;		/* delete page (C4) */
	unsigned headline : 1;		/* insert headline (C5) */
	unsigned subtitle : 1;		/* insert subtitle (C6) */
	unsigned supp_header : 1;	/* suppress header (C7) */
	unsigned update : 1;		/* update page (C8) */
	unsigned inter_seq : 1;		/* interrupted sequence (C9) */
	unsigned dis_disp : 1;		/* disable/suppress display (C10) */
	unsigned serial : 1;		/* serial mode (C11) */
	unsigned notfound : 1;		/* /FOUND */
	unsigned pblf : 1;		/* PBLF */
	unsigned hamming : 1;		/* hamming-error occurred */
}
vtx_pageinfo_t;
 
#endif /* _VTX_H */
