/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

/*
 * sys/note.h:	interface for annotating source with info for tools
 *
 * This is the underlying interface; NOTE (/usr/include/note.h) is the
 * preferred interface, but all exported header files should include this
 * file directly and use _NOTE so as not to take "NOTE" from the user's
 * namespace.  For consistency, *all* kernel source should use _NOTE.
 *
 * By default, annotations expand to nothing.  This file implements
 * that.  Tools using annotations will interpose a different version
 * of this file that will expand annotations as needed.
 */

#ifndef	_SYS_NOTE_H
#define	_SYS_NOTE_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _NOTE
#define	_NOTE(s)
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_NOTE_H */
