/*
 *    Filename: ks0108.h
 *     Version: 0.1.0
 * Description: ks0108 LCD Controller driver header
 *     License: GPLv2
 *
 *      Author: Copyright (C) Miguel Ojeda Sandonis
 *        Date: 2006-10-31
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _KS0108_H_
#define _KS0108_H_

/* Write a byte to the data port */
extern void ks0108_writedata(unsigned char byte);

/* Write a byte to the control port */
extern void ks0108_writecontrol(unsigned char byte);

/* Set the controller's current display state (0..1) */
extern void ks0108_displaystate(unsigned char state);

/* Set the controller's current startline (0..63) */
extern void ks0108_startline(unsigned char startline);

/* Set the controller's current address (0..63) */
extern void ks0108_address(unsigned char address);

/* Set the controller's current page (0..7) */
extern void ks0108_page(unsigned char page);

/* Is the module inited? */
extern unsigned char ks0108_isinited(void);

#endif /* _KS0108_H_ */
