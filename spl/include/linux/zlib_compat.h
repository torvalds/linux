/*****************************************************************************\
 *  Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_ZLIB_COMPAT_H
#define _SPL_ZLIB_COMPAT_H

#include <linux/zlib.h>

#ifdef HAVE_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE
#define spl_zlib_deflate_workspacesize(wb, ml) \
	zlib_deflate_workspacesize(wb, ml)
#else
#define spl_zlib_deflate_workspacesize(wb, ml) \
	zlib_deflate_workspacesize()
#endif /* HAVE_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE */

#endif /* SPL_ZLIB_COMPAT_H */
