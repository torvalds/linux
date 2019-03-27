/*	$NetBSD: if_tun.h,v 1.5 1994/06/29 06:36:27 cgd Exp $	*/

/*-
 * Copyright (c) 1988, Julian Onions <jpo@cs.nott.ac.uk>
 * Nottingham University 1987.
 *
 * This source may be freely distributed, however I would be interested
 * in any changes that are made.
 *
 * This driver takes packets off the IP i/f and hands them up to a
 * user process to have its wicked way with. This driver has it's
 * roots in a similar driver written by Phil Cockcroft (formerly) at
 * UCL. This driver is based much more on read/write/select mode of
 * operation though.
 *
 * $FreeBSD$
 */

#ifndef _NET_IF_TUN_H_
#define _NET_IF_TUN_H_

/* Refer to if_tunvar.h for the softc stuff */

/* Maximum transmit packet size (default) */
#define	TUNMTU		1500

/* Maximum receive packet size (hard limit) */
#define	TUNMRU		65535

struct tuninfo {
	int	baudrate;		/* linespeed */
	unsigned short	mtu;		/* maximum transmission unit */
	u_char	type;			/* ethernet, tokenring, etc. */
	u_char	dummy;			/* place holder */
};

/* ioctl's for get/set debug */
#define	TUNSDEBUG	_IOW('t', 90, int)
#define	TUNGDEBUG	_IOR('t', 89, int)
#define	TUNSIFINFO	_IOW('t', 91, struct tuninfo)
#define	TUNGIFINFO	_IOR('t', 92, struct tuninfo)
#define	TUNSLMODE	_IOW('t', 93, int)
#define	TUNSIFMODE	_IOW('t', 94, int)
#define	TUNSIFPID	_IO('t', 95)
#define	TUNSIFHEAD	_IOW('t', 96, int)
#define	TUNGIFHEAD	_IOR('t', 97, int)

#endif /* !_NET_IF_TUN_H_ */
