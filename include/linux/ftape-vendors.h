#ifndef _FTAPE_VENDORS_H
#define _FTAPE_VENDORS_H

/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                (C) 1996-1997 Claus-Justus Heine.

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
 * $Source: /homes/cvs/ftape-stacked/include/linux/ftape-vendors.h,v $
 * $Revision: 1.6 $
 * $Date: 1997/10/09 15:38:11 $
 *
 *      This file contains the supported drive types with their
 *      QIC-117 spec. vendor code and drive dependent configuration
 *      information.
 */

typedef enum {
	unknown_wake_up = 0,
	no_wake_up,
	wake_up_colorado,
	wake_up_mountain,
	wake_up_insight,
} wake_up_types;

typedef struct {
	wake_up_types wake_up;	/* see wake_up_types */
	char *name;		/* Text describing the drive */
} wakeup_method;

/*  Note: order of entries in WAKEUP_METHODS must be so that a variable
 *        of type wake_up_types can be used as an index in the array.
 */
#define WAKEUP_METHODS { \
  { unknown_wake_up,    "Unknown" }, \
  { no_wake_up,         "None" }, \
  { wake_up_colorado,   "Colorado" }, \
  { wake_up_mountain,   "Mountain" }, \
  { wake_up_insight,    "Motor-on" }, \
}

typedef struct {
	unsigned int vendor_id;	/* vendor id from drive */
	int speed;		/* maximum tape transport speed (ips) */
	wake_up_types wake_up;	/* see wake_up_types */
	char *name;		/* Text describing the drive */
} vendor_struct;

#define UNKNOWN_VENDOR (-1)

#define QIC117_VENDORS {						    \
/* see _vendor_struct */						    \
  { 0x00000,  82, wake_up_colorado,  "Colorado DJ-10 (old)" },		    \
  { 0x00047,  90, wake_up_colorado,  "Colorado DJ-10/DJ-20" },		    \
  { 0x011c2,  84, wake_up_colorado,  "Colorado 700" },			    \
  { 0x011c3,  90, wake_up_colorado,  "Colorado 1400" },			    \
  { 0x011c4,  84, wake_up_colorado,  "Colorado DJ-10/DJ-20 (new)" },	    \
  { 0x011c5,  84, wake_up_colorado,  "HP Colorado T1000" },		    \
  { 0x011c6,  90, wake_up_colorado,  "HP Colorado T3000" },		    \
  { 0x00005,  45, wake_up_mountain,  "Archive 5580i" },			    \
  { 0x10005,  50, wake_up_insight,   "Insight 80Mb, Irwin 80SX" },	    \
  { 0x00140,  74, wake_up_mountain,  "Archive S.Hornet [Identity/Escom]" }, \
  { 0x00146,  72, wake_up_mountain,  "Archive 31250Q [Escom]" },	    \
  { 0x0014a, 100, wake_up_mountain,  "Archive XL9250i [Conner/Escom]" },    \
  { 0x0014c,  98, wake_up_mountain,  "Conner C250MQT" },		    \
  { 0x0014e,  80, wake_up_mountain,  "Conner C250MQ" },			    \
  { 0x00150,  80, wake_up_mountain,  "Conner TSM420R/TST800R" },	    \
  { 0x00152,  80, wake_up_mountain,  "Conner TSM850R" },		    \
  { 0x00156,  80, wake_up_mountain,  "Conner TSM850R/1700R/TST3200R" },	    \
  { 0x00180,   0, wake_up_mountain,  "Summit SE 150" },			    \
  { 0x00181,  85, wake_up_mountain,  "Summit SE 250, Mountain FS8000" },    \
  { 0x001c1,  82, no_wake_up,        "Wangtek 3040F" },			    \
  { 0x001c8,  64, no_wake_up,        "Wangtek 3080F" },			    \
  { 0x001c8,  64, wake_up_colorado,  "Wangtek 3080F" },			    \
  { 0x001ca,  67, no_wake_up,        "Wangtek 3080F (new)" },		    \
  { 0x001cc,  77, wake_up_colorado,  "Wangtek 3200 / Teac 700" },	    \
  { 0x001cd,  75, wake_up_colorado,  "Reveal TB1400" },			    \
  { 0x00380,  85, wake_up_colorado,  "Exabyte Eagle-96" },		    \
  { 0x00381,  85, wake_up_colorado,  "Exabyte Eagle TR-3" },		    \
  { 0x00382,  85, wake_up_colorado,  "Exabyte Eagle TR-3" },		    \
  { 0x003ce,  77, wake_up_colorado,  "Teac 800" },			    \
  { 0x003cf,   0, wake_up_colorado,  "Teac FT3010TR" },			    \
  { 0x08880,  64, no_wake_up,        "Iomega 250, Ditto 800" },		    \
  { 0x08880,  64, wake_up_colorado,  "Iomega 250, Ditto 800" },		    \
  { 0x08880,  64, wake_up_insight,   "Iomega 250, Ditto 800" },		    \
  { 0x08881,  80, wake_up_colorado,  "Iomega 700" },			    \
  { 0x08882,  80, wake_up_colorado,  "Iomega 3200" },			    \
  { 0x08883,  80, wake_up_colorado,  "Iomega DITTO 2GB" },		    \
  { 0x00021,  70, no_wake_up,        "AIWA CT-803" },			    \
  { 0x004c0,  80, no_wake_up,        "AIWA TD-S1600" },			    \
  { 0x00021,   0, wake_up_mountain,  "COREtape QIC80" },		    \
  { 0x00441,   0, wake_up_mountain,  "ComByte DoublePlay" },		    \
  { 0x00481, 127, wake_up_mountain,  "PERTEC MyTape 800" },		    \
  { 0x00483, 130, wake_up_mountain,  "PERTEC MyTape 3200" },		    \
  { UNKNOWN_VENDOR, 0, no_wake_up, "unknown" }				    \
}

#define QIC117_MAKE_CODES {			\
  { 0, "Unassigned" },				\
  { 1, "Alloy Computer Products" },		\
  { 2, "3M" },					\
  { 3, "Tandberg Data" },			\
  { 4, "Colorado" },				\
  { 5, "Archive/Conner" },			\
  { 6, "Mountain/Summit Memory Systems" },	\
  { 7, "Wangtek/Rexon/Tecmar" },		\
  { 8, "Sony" },				\
  { 9, "Cipher Data Products" },		\
  { 10, "Irwin Magnetic Systems" },		\
  { 11, "Braemar" },				\
  { 12, "Verbatim" },				\
  { 13, "Core International" },			\
  { 14, "Exabyte" },				\
  { 15, "Teac" },				\
  { 16, "Gigatek" },				\
  { 17, "ComByte" },				\
  { 18, "PERTEC Memories" },			\
  { 19, "Aiwa" },				\
  { 71, "Colorado" },				\
  { 546, "Iomega Inc" },			\
}

#endif /* _FTAPE_VENDORS_H */
