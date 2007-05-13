/*
 * include/asm-arm/arch-ks8695/regs-switch.h
 *
 * Copyright (C) 2006 Andrew Victor
 *
 * KS8695 - Switch Registers and bit definitions.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef KS8695_SWITCH_H
#define KS8695_SWITCH_H

#define KS8695_SWITCH_OFFSET	(0xF0000 + 0xe800)
#define KS8695_SWITCH_VA	(KS8695_IO_VA + KS8695_SWITCH_OFFSET)
#define KS8695_SWITCH_PA	(KS8695_IO_PA + KS8695_SWITCH_OFFSET)


/*
 * Switch registers
 */
#define KS8695_SEC0		(0x00)		/* Switch Engine Control 0 */
#define KS8695_SEC1		(0x04)		/* Switch Engine Control 1 */
#define KS8695_SEC2		(0x08)		/* Switch Engine Control 2 */

#define KS8695_P(x)_C(z)	(0xc0 + (((x)-1)*3 + ((z)-1))*4)	/* Port Configuration Registers */

#define KS8695_SEP12AN		(0x48)		/* Port 1 & 2 Auto-Negotiation */
#define KS8695_SEP34AN		(0x4c)		/* Port 3 & 4 Auto-Negotiation */
#define KS8695_SEIAC		(0x50)		/* Indirect Access Control */
#define KS8695_SEIADH2		(0x54)		/* Indirect Access Data High 2 */
#define KS8695_SEIADH1		(0x58)		/* Indirect Access Data High 1 */
#define KS8695_SEIADL		(0x5c)		/* Indirect Access Data Low */
#define KS8695_SEAFC		(0x60)		/* Advance Feature Control */
#define KS8695_SEDSCPH		(0x64)		/* TOS Priority High */
#define KS8695_SEDSCPL		(0x68)		/* TOS Priority Low */
#define KS8695_SEMAH		(0x6c)		/* Switch Engine MAC Address High */
#define KS8695_SEMAL		(0x70)		/* Switch Engine MAC Address Low */
#define KS8695_LPPM12		(0x74)		/* Port 1 & 2 PHY Power Management */
#define KS8695_LPPM34		(0x78)		/* Port 3 & 4 PHY Power Management */


/* Switch Engine Control 0 */
#define SEC0_LLED1S		(7 << 25)	/* LED1 Select */
#define		LLED1S_SPEED		(0 << 25)
#define		LLED1S_LINK		(1 << 25)
#define		LLED1S_DUPLEX		(2 << 25)
#define		LLED1S_COLLISION	(3 << 25)
#define		LLED1S_ACTIVITY		(4 << 25)
#define		LLED1S_FDX_COLLISION	(5 << 25)
#define		LLED1S_LINK_ACTIVITY	(6 << 25)
#define SEC0_LLED0S		(7 << 22)	/* LED0 Select */
#define		LLED0S_SPEED		(0 << 22)
#define		LLED0S_LINK		(1 << 22)
#define		LLED0S_DUPLEX		(2 << 22)
#define		LLED0S_COLLISION	(3 << 22)
#define		LLED0S_ACTIVITY		(4 << 22)
#define		LLED0S_FDX_COLLISION	(5 << 22)
#define		LLED0S_LINK_ACTIVITY	(6 << 22)
#define SEC0_ENABLE		(1 << 0)	/* Enable Switch */



#endif
