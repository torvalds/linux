/*
 * lms283gf05.h - Platform glue for Samsung LMS283GF05 LCD
 *
 * Copyright (C) 2009 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#ifndef _INCLUDE_LINUX_SPI_LMS283GF05_H_
#define _INCLUDE_LINUX_SPI_LMS283GF05_H_

struct lms283gf05_pdata {
	unsigned long	reset_gpio;
	bool		reset_inverted;
};

#endif /* _INCLUDE_LINUX_SPI_LMS283GF05_H_ */
