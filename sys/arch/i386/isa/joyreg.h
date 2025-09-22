/*	$OpenBSD: joyreg.h,v 1.5 2007/11/29 10:53:54 deraadt Exp $	*/
/*	$NetBSD: joy.c,v 1.3 1996/05/05 19:46:15 christos Exp $	*/

/*-
 * Copyright (c) 1995 Jean-Marc Zucconi
 * All rights reserved.
 *
 * Ported to NetBSD by Matthieu Herrb <matthieu@laas.fr>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * The game port can manage 4 buttons and 4 variable resistors (usually 2
 * joysticks, each with 2 buttons and 2 pots.) via the port at address 0x201.
 * Getting the state of the buttons is done by reading the game port;
 * buttons 1-4 correspond to bits 4-7 and resistors 1-4 (X1, Y1, X2, Y2)
 * to bits 0-3.  If button 1 (resp 2, 3, 4) is pressed, the bit 4 (resp 5,
 * 6, 7) is set to 0 to get the value of a resistor, write the value 0xff
 * at port and wait until the corresponding bit returns to 0.
 */

/*
 * The formulae below only work if u is ``not too large''.
 */
#define USEC2TICKS(u) 	(((u) * 19549) >> 14)
#define TICKS2USEC(u) 	(((u) * 3433) >> 12)


#define JOYPART(d) (minor(d) & 1)
#define JOYUNIT(d) minor(d) >> 1 & 3

#ifndef JOY_TIMEOUT
#define JOY_TIMEOUT   2000	/* 2 milliseconds */
#endif

#define JOY_NPORTS    1

struct joy_softc {
	struct	device sc_dev;
	int	port;
	int	x_off[2], y_off[2];
	int	timeout[2];
};

int		joyopen(dev_t, int, int, struct proc *);
int		joyclose(dev_t, int, int, struct proc *);
