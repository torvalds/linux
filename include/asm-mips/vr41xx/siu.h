/*
 *  Include file for NEC VR4100 series Serial Interface Unit.
 *
 *  Copyright (C) 2005  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __NEC_VR41XX_SIU_H
#define __NEC_VR41XX_SIU_H

#define SIU_PORTS_MAX 2

typedef enum {
	SIU_INTERFACE_RS232C,
	SIU_INTERFACE_IRDA,
} siu_interface_t;

extern void vr41xx_select_siu_interface(siu_interface_t interface);

typedef enum {
	SIU_USE_IRDA,
	FIR_USE_IRDA,
} irda_use_t;

extern void vr41xx_use_irda(irda_use_t use);

typedef enum {
	SHARP_IRDA,
	TEMIC_IRDA,
	HP_IRDA,
} irda_module_t;

typedef enum {
	IRDA_TX_1_5MBPS,
	IRDA_TX_4MBPS,
} irda_speed_t;

extern void vr41xx_select_irda_module(irda_module_t module, irda_speed_t speed);

#endif /* __NEC_VR41XX_SIU_H */
