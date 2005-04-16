/*
 * LparData.h
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
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

#ifndef _LPARDATA_H
#define _LPARDATA_H

#include <asm/types.h>
#include <asm/page.h>
#include <asm/abs_addr.h>

#include <asm/iSeries/ItLpNaca.h>
#include <asm/iSeries/ItLpRegSave.h>
#include <asm/iSeries/HvReleaseData.h>
#include <asm/iSeries/LparMap.h>
#include <asm/iSeries/ItVpdAreas.h>
#include <asm/iSeries/ItIplParmsReal.h>
#include <asm/iSeries/ItExtVpdPanel.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/iSeries/IoHriProcessorVpd.h>

extern struct LparMap	xLparMap;
extern struct HvReleaseData hvReleaseData;
extern struct ItLpNaca	itLpNaca;
extern struct ItIplParmsReal xItIplParmsReal;
extern struct ItExtVpdPanel xItExtVpdPanel;
extern struct IoHriProcessorVpd xIoHriProcessorVpd[];
extern struct ItLpQueue xItLpQueue;
extern struct ItVpdAreas itVpdAreas;
extern u64    xMsVpd[];
extern struct msChunks msChunks;


#endif /* _LPARDATA_H */
