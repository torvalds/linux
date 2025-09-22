/* $OpenBSD: lcd.h,v 1.2 2011/03/23 16:54:35 pirofti Exp $ */

/* 
 * Copyright (c) 2007 Kenji AOYAMA <aoyama@nk-home.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _MACHINE_LCD_H_
#define _MACHINE_LCD_H_

/*
 * OpenBSD/luna88k LCD driver
 */

/* The ioctl defines */

#define	LCDCLS		_IO('L', 1)		/* Clear LCD screen */
#define	LCDHOME		_IO('L', 2)		/* Move the cursor to left-upper */
#define	LCDMODE		_IOW('L', 3, int)	/* Set the data entry mode */
#define	LCDDISP		_IOW('L', 4, int)	/* Blink, cursor, and display on/off */
#define	LCDMOVE		_IOW('L', 5, int)	/* Move cursor / shift display area */
#define	LCDSEEK		_IOW('L', 6, int)	/* Move the cursor to specified position */
#define	LCDRESTORE	_IO('L', 7)		/* Restore boot-time LCD message */

/* argument value for each ioctl */

/* LCDMODE; when a character data is written, then ... */ 
#define	LCDMODE_C_LEFT	0x04	/* cursor moves left */	
#define	LCDMODE_C_RIGHT	0x06	/* cursor moves right */
#define	LCDMODE_D_LEFT	0x05	/* display area shifts to left */
#define	LCDMODE_D_RIGHT	0x07	/* display area shifts to right */

/* LCDDISP; you can use these values or'ed */
#define	LCD_DISPLAY	0x04	/* LCD display on */
#define	LCD_CURSOR	0x02 	/* Cursor on */
#define	LCD_BLINK	0x01	/* Blink on */

/* LCDMOVE; just move the cursor or shift the display area */
#define	LCDMOVE_C_LEFT	0x10	/* cursor moves left */
#define	LCDMOVE_C_RIGHT	0x14	/* cursor moves right */
#define	LCDMOVE_D_LEFT	0x18	/* display area shifts to left */
#define	LCDMOVE_D_RIGHT	0x1c	/* display area shifts to right */

#endif /* _MACHINE_LCD_H_ */
