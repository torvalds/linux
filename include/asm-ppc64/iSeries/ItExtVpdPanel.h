/*
 * ItExtVpdPanel.h
 * Copyright (C) 2002  Dave Boutcher IBM Corporation
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#ifndef _ITEXTVPDPANEL_H
#define _ITEXTVPDPANEL_H

/*
 *
 *	This struct maps the panel information 
 *
 * Warning:
 *	This data must match the architecture for the panel information
 *
 */


/*-------------------------------------------------------------------
 * Standard Includes
 *------------------------------------------------------------------- 
*/
#include <asm/types.h>

struct ItExtVpdPanel
{
  // Definition of the Extended Vpd On Panel Data Area
  char                      systemSerial[8];
  char                      mfgID[4];
  char                      reserved1[24];
  char                      machineType[4];
  char                      systemID[6];
  char                      somUniqueCnt[4];
  char                      serialNumberCount;
  char                      reserved2[7];
  u16                       bbu3;
  u16                       bbu2;
  u16                       bbu1;
  char                      xLocationLabel[8];
  u8                        xRsvd1[6];
  u16                       xFrameId;
  u8                        xRsvd2[48];
};

#endif /* _ITEXTVPDPANEL_H  */
