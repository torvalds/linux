#ifndef _ISERIES_IO_H
#define _ISERIES_IO_H

#include <linux/config.h>

#ifdef CONFIG_PPC_ISERIES
#include <linux/types.h>
/*
 * File iSeries_io.h created by Allan Trautman on Thu Dec 28 2000.
 *
 * Remaps the io.h for the iSeries Io
 * Copyright (C) 2000  Allan H Trautman, IBM Corporation
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
 * along with this program; if not, write to the:
 * Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330,
 * Boston, MA  02111-1307  USA
 *
 * Change Activity:
 *   Created December 28, 2000
 * End Change Activity
 */

extern u8   iSeries_Read_Byte(const volatile void __iomem * IoAddress);
extern u16  iSeries_Read_Word(const volatile void __iomem * IoAddress);
extern u32  iSeries_Read_Long(const volatile void __iomem * IoAddress);
extern void iSeries_Write_Byte(u8  IoData, volatile void __iomem * IoAddress);
extern void iSeries_Write_Word(u16 IoData, volatile void __iomem * IoAddress);
extern void iSeries_Write_Long(u32 IoData, volatile void __iomem * IoAddress);

extern void iSeries_memset_io(volatile void __iomem *dest, char x, size_t n);
extern void iSeries_memcpy_toio(volatile void __iomem *dest, void *source,
		size_t n);
extern void iSeries_memcpy_fromio(void *dest,
		const volatile void __iomem *source, size_t n);

#endif /* CONFIG_PPC_ISERIES */
#endif /* _ISERIES_IO_H */
