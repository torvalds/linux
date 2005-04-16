/*****************************************************************************/

/*
 *	comstats.h  -- Serial Port Stats.
 *
 *	Copyright (C) 1996-1998  Stallion Technologies
 *	Copyright (C) 1994-1996  Greg Ungerer.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*****************************************************************************/
#ifndef	_COMSTATS_H
#define	_COMSTATS_H
/*****************************************************************************/

/*
 *	Serial port stats structure. The structure itself is UART
 *	independent, but some fields may be UART/driver specific (for
 *	example state).
 */

typedef struct {
	unsigned long	brd;
	unsigned long	panel;
	unsigned long	port;
	unsigned long	hwid;
	unsigned long	type;
	unsigned long	txtotal;
	unsigned long	rxtotal;
	unsigned long	txbuffered;
	unsigned long	rxbuffered;
	unsigned long	rxoverrun;
	unsigned long	rxparity;
	unsigned long	rxframing;
	unsigned long	rxlost;
	unsigned long	txbreaks;
	unsigned long	rxbreaks;
	unsigned long	txxon;
	unsigned long	txxoff;
	unsigned long	rxxon;
	unsigned long	rxxoff;
	unsigned long	txctson;
	unsigned long	txctsoff;
	unsigned long	rxrtson;
	unsigned long	rxrtsoff;
	unsigned long	modem;
	unsigned long	state;
	unsigned long	flags;
	unsigned long	ttystate;
	unsigned long	cflags;
	unsigned long	iflags;
	unsigned long	oflags;
	unsigned long	lflags;
	unsigned long	signals;
} comstats_t;


/*
 *	Board stats structure. Returns useful info about the board.
 */

#define	COM_MAXPANELS	8

typedef struct {
	unsigned long	panel;
	unsigned long	type;
	unsigned long	hwid;
	unsigned long	nrports;
} companel_t;

typedef struct {
	unsigned long	brd;
	unsigned long	type;
	unsigned long	hwid;
	unsigned long	state;
	unsigned long	ioaddr;
	unsigned long	ioaddr2;
	unsigned long	memaddr;
	unsigned long	irq;
	unsigned long	nrpanels;
	unsigned long	nrports;
	companel_t	panels[COM_MAXPANELS];
} combrd_t;


/*
 *	Define the ioctl operations for stats stuff.
 */
#include <linux/ioctl.h>

#define	COM_GETPORTSTATS	_IO('c',30)
#define	COM_CLRPORTSTATS	_IO('c',31)
#define	COM_GETBRDSTATS		_IO('c',32)


/*
 *	Define the set of ioctls that give user level access to the
 *	private port, panel and board structures. The argument required
 *	will be driver dependent!  
 */
#define	COM_READPORT		_IO('c',40)
#define	COM_READBOARD		_IO('c',41)
#define	COM_READPANEL		_IO('c',42)

/*****************************************************************************/
#endif
