/*
 * Copyright (C) 2011 Kuninori Morimoto
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef SH_HSPI_H
#define SH_HSPI_H

/*
 * flags
 *
 *
 */
#define SH_HSPI_CLK_DIVC(d)		(d & 0xFF)

#define SH_HSPI_FBS		(1 << 8)
#define SH_HSPI_CLKP_HIGH	(1 << 9)	/* default LOW */
#define SH_HSPI_IDIV_DIV128	(1 << 10)	/* default div16 */
struct sh_hspi_info {
	u32	flags;
};

#endif
