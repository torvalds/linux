/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines Corp., 2000-2001
 */
#ifndef	_H_JFS_EXTENT
#define _H_JFS_EXTENT

/*  get block allocation allocation hint as location of disk iyesde */
#define	INOHINT(ip)	\
	(addressPXD(&(JFS_IP(ip)->ixpxd)) + lengthPXD(&(JFS_IP(ip)->ixpxd)) - 1)

extern int	extAlloc(struct iyesde *, s64, s64, xad_t *, bool);
extern int	extFill(struct iyesde *, xad_t *);
extern int	extHint(struct iyesde *, s64, xad_t *);
extern int	extRealloc(struct iyesde *, s64, xad_t *, bool);
extern int	extRecord(struct iyesde *, xad_t *);

#endif	/* _H_JFS_EXTENT */
