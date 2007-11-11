/*
 * g_printer.h -- Header file for USB Printer gadget driver
 *
 * Copyright (C) 2007 Craig W. Nadler
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#define PRINTER_NOT_ERROR	0x08
#define PRINTER_SELECTED	0x10
#define PRINTER_PAPER_EMPTY	0x20

/* The 'g' code is also used by gadgetfs ioctl requests.
 * Don't add any colliding codes to either driver, and keep
 * them in unique ranges (size 0x20 for now).
 */
#define GADGET_GET_PRINTER_STATUS	_IOR('g', 0x21, unsigned char)
#define GADGET_SET_PRINTER_STATUS	_IOWR('g', 0x22, unsigned char)
