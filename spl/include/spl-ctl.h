/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
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

#ifndef _DEBUG_CTL_H
#define	_DEBUG_CTL_H

/*
 * Contains shared definitions which both the user space
 * and kernel space portions of splat must agree on.
 */
typedef struct spl_debug_header {
	int ph_len;
	int ph_flags;
	int ph_subsys;
	int ph_mask;
	int ph_cpu_id;
	int ph_sec;
	long ph_usec;
	int ph_stack;
	int ph_pid;
	int ph_line_num;
} spl_debug_header_t;

#endif /* _DEBUG_CTL_H */
