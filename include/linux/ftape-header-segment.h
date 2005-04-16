#ifndef _FTAPE_HEADER_SEGMENT_H
#define _FTAPE_HEADER_SEGMENT_H

/*
 * Copyright (C) 1996-1997 Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/include/linux/ftape-header-segment.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:19:28 $
 *
 *      This file defines some offsets into the header segment of a
 *      floppy tape cartridge.  For use with the QIC-40/80/3010/3020
 *      floppy-tape driver "ftape" for Linux.
 */

#define FT_SIGNATURE   0  /* must be 0xaa55aa55 */
#define FT_FMT_CODE    4
#define FT_REV_LEVEL   5  /* only for QIC-80 since. Rev. L (== 0x0c)         */
#define FT_HSEG_1      6  /* first header segment, except for format code  6 */
#define FT_HSEG_2      8  /* second header segment, except for format code 6 */
#define FT_FRST_SEG   10  /* first data segment, except for format code 6    */
#define FT_LAST_SEG   12  /* last data segment, except for format code 6     */
#define FT_FMT_DATE   14  /* date and time of most recent format, see below  */
#define FT_WR_DATE    18  /* date and time of most recent write or format    */
#define FT_SPT        24  /* segments per track                              */
#define FT_TPC        26  /* tracks per cartridge                            */
#define FT_FHM        27  /* floppy drive head (maximum of it)               */
#define FT_FTM        28  /* floppy track max.                               */
#define FT_FSM        29  /* floppy sector max. (128)                        */
#define FT_LABEL      30  /* floppy tape label                               */
#define FT_LABEL_DATE 74  /* date and time the tape label was written        */
#define FT_LABEL_SZ   (FT_LABEL_DATE - FT_LABEL)
#define FT_CMAP_START 78  /* starting segment of compression map             */
#define FT_FMT_ERROR 128  /* must be set to 0xff if remainder gets lost during
			   * tape format
			   */
#define FT_SEG_CNT   130  /* number of seg. written, formatted or verified
			   * through lifetime of tape (why not read?)
			   */
#define FT_INIT_DATE 138  /* date and time of initial tape format    */
#define FT_FMT_CNT   142  /* number of times tape has been formatted */
#define FT_FSL_CNT   144  /* number of segments in failed sector log */
#define FT_MK_CODE   146  /* id string of tape manufacturer          */
#define FT_LOT_CODE  190  /* tape manufacturer lot code              */
#define FT_6_HSEG_1  234  /* first header segment for format code  6 */
#define FT_6_HSEG_2  238  /* second header segment for format code 6 */
#define FT_6_FRST_SEG 242 /* first data segment for format code 6    */
#define FT_6_LAST_SEG 246 /* last data segment for format code 6     */

#define FT_FSL        256
#define FT_HEADER_END 256 /* space beyond this point:
			   * format codes 2, 3 and 5: 
			   * -  failed sector log until byte 2047
			   * -  bad sector map in the reamining part of segment
			   * format codes 4 and 6:
			   * -  bad sector map  starts hear
			   */


/*  value to be stored at the FT_SIGNATURE offset 
 */
#define FT_HSEG_MAGIC 0xaa55aa55
#define FT_D2G_MAGIC  0x82288228 /* Ditto 2GB */

/* data and time encoding: */
#define FT_YEAR_SHIFT 25
#define FT_YEAR_MASK  0xfe000000
#define FT_YEAR_0     1970
#define FT_YEAR_MAX   127
#define FT_YEAR(year) ((((year)-FT_YEAR_0)<<FT_YEAR_SHIFT)&FT_YEAR_MASK)

#define FT_TIME_SHIFT   0
#define FT_TIME_MASK    0x01FFFFFF
#define FT_TIME_MAX     0x01ea6dff /* last second of a year */
#define FT_TIME(mo,d,h,m,s) \
	((((s)+60*((m)+60*((h)+24*((d)+31*(mo))))) & FT_TIME_MASK))

#define FT_TIME_STAMP(y,mo,d,h,m,s) (FT_YEAR(y) | FT_TIME(mo,d,h,m,s))

/* values for the format code field */
typedef enum {
	fmt_normal = 2, /*  QIC-80 post Rev. B 205Ft or 307Ft tape    */
	fmt_1100ft = 3, /*  QIC-80 post Rev. B 1100Ft tape            */
	fmt_var    = 4, /*  QIC-80 post Rev. B variabel length format */
	fmt_425ft  = 5, /*  QIC-80 post Rev. B 425Ft tape             */
	fmt_big    = 6  /*  QIC-3010/3020 variable length tape with more 
			 *  than 2^16 segments per tape
			 */
} ft_format_type;

/* definitions for the failed sector log */
#define FT_FSL_SIZE        (2 * FT_SECTOR_SIZE - FT_HEADER_END)
#define FT_FSL_MAX_ENTRIES (FT_FSL_SIZE/sizeof(__u32))

typedef struct ft_fsl_entry {
	__u16 segment;
	__u16 date;
} __attribute__ ((packed)) ft_fsl_entry;


/*  date encoding for the failed sector log 
 *  month: 1..12, day: 1..31, year: 1970..2097
 */
#define FT_FSL_TIME_STAMP(y,m,d) \
	(((((y) - FT_YEAR_0)<<9)&0xfe00) | (((m)<<5)&0x01e0) | ((d)&0x001f))

#endif /* _FTAPE_HEADER_SEGMENT_H */
