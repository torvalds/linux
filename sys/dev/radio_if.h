/* $OpenBSD: radio_if.h,v 1.4 2022/03/21 19:22:40 miod Exp $ */
/* $RuOBSD: radio_if.h,v 1.6 2001/10/18 16:51:36 pva Exp $ */

/*
 * Copyright (c) 2001 Maxim Tsyplakov <tm@oganer.net>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYS_DEV_RADIO_IF_H
#define _SYS_DEV_RADIO_IF_H

/*
 * Generic interface to hardware driver
 */

#define RADIOUNIT(x)	(minor(x))

struct radio_hw_if {
	/* open hardware */
	int	(*open)(void *, int, int, struct proc *);	

	/* close hardware */
	int	(*close)(void *, int, int, struct proc *);

	int     (*get_info)(void *, struct radio_info *);
	int     (*set_info)(void *, struct radio_info *);
	int     (*search)(void *, int);
};

struct device  *radio_attach_mi(const struct radio_hw_if *, void *,
	    struct device *);

#endif /* _SYS_DEV_RADIO_IF_H */
