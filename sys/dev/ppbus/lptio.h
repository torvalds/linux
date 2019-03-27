/*-
 *  Copyright (C) 1994 Geoffrey M. Rehmet
 *
 *  This program is free software; you may redistribute it and/or
 *  modify it, provided that it retain the above copyright notice
 *  and the following disclaimer.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	Geoff Rehmet, Rhodes University, South Africa <csgr@cs.ru.ac.za>
 *
 * $FreeBSD$
 */

#ifndef	_DEV_PPBUS_LPT_H_
#define	_DEV_PPBUS_LPT_H_

#include <sys/ioccom.h>

#define	LPT_IRQ		_IOW('p', 1, long)	/* set interrupt status */

#endif /* !_DEV_PPBUS_LPT_H_ */
