/****************************************************************************/
/*
 *  linux/include/asm-arm/arch-l7200/pmpcon.h
 *
 *   Registers and  helper functions for the L7200 Link-Up Systems
 *   DC/DC converter register.
 *
 *   (C) Copyright 2000, S A McConnell  (samcconn@cotw.com)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

/****************************************************************************/

#define PMPCON_OFF 0x00006000  /* Offset from IO_START_2. */

/* IO_START_2 and IO_BASE_2 are defined in hardware.h */

#define PMPCON_START (IO_START_2 + PMPCON_OFF)  /* Physical address of reg. */
#define PMPCON_BASE  (IO_BASE_2  + PMPCON_OFF)  /* Virtual address of reg. */


#define PMPCON (*(volatile unsigned int *)(PMPCON_BASE))

#define PWM2_50CYCLE 0x800
#define CONTRAST     0x9

#define PWM1H (CONTRAST)
#define PWM1L (CONTRAST << 4)

#define PMPCON_VALUE  (PWM2_50CYCLE | PWM1L | PWM1H) 
	
/* PMPCON = 0x811;   // too light and fuzzy
 * PMPCON = 0x844;   
 * PMPCON = 0x866;   // better color poor depth
 * PMPCON = 0x888;   // Darker but better depth 
 * PMPCON = 0x899;   // Darker even better depth
 * PMPCON = 0x8aa;   // too dark even better depth
 * PMPCON = 0X8cc;   // Way too dark
 */

/* As CONTRAST value increases the greater the depth perception and
 * the darker the colors.
 */
