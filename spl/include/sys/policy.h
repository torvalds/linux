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

#ifndef _SPL_POLICY_H
#define	_SPL_POLICY_H

#define	secpolicy_fs_unmount(c, vfs)			(0)
#define	secpolicy_nfs(c)				(0)
#define	secpolicy_sys_config(c, co)			(0)
#define	secpolicy_zfs(c)				(0)
#define	secpolicy_zinject(c)				(0)
#define	secpolicy_vnode_setids_setgids(c, id)		(0)
#define	secpolicy_vnode_setid_retain(c, sr)		(0)
#define	secpolicy_setid_clear(v, c)			(0)
#define	secpolicy_vnode_any_access(c, vp, o)		(0)
#define	secpolicy_vnode_access2(c, cp, o, m1, m2)	(0)
#define	secpolicy_vnode_chown(c, o)			(0)
#define	secpolicy_vnode_setdac(c, o)			(0)
#define	secpolicy_vnode_remove(c)			(0)
#define	secpolicy_vnode_setattr(c, v, a, o, f, func, n)	(0)
#define	secpolicy_xvattr(x, o, c, t)			(0)
#define	secpolicy_vnode_stky_modify(c)			(0)
#define	secpolicy_setid_setsticky_clear(v, a, o, c)	(0)
#define	secpolicy_basic_link(c)				(0)

#endif /* SPL_POLICY_H */
