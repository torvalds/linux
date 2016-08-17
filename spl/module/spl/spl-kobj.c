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
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Kobj Implementation.
\*****************************************************************************/

#include <sys/kobj.h>

struct _buf *
kobj_open_file(const char *name)
{
	struct _buf *file;
	vnode_t *vp;
	int rc;

	file = kmalloc(sizeof(_buf_t), kmem_flags_convert(KM_SLEEP));
	if (file == NULL)
		return ((_buf_t *)-1UL);

	if ((rc = vn_open(name, UIO_SYSSPACE, FREAD, 0644, &vp, 0, 0))) {
		kfree(file);
		return ((_buf_t *)-1UL);
	}

	file->vp = vp;

	return (file);
} /* kobj_open_file() */
EXPORT_SYMBOL(kobj_open_file);

void
kobj_close_file(struct _buf *file)
{
	VOP_CLOSE(file->vp, 0, 0, 0, 0, 0);
        kfree(file);
} /* kobj_close_file() */
EXPORT_SYMBOL(kobj_close_file);

int
kobj_read_file(struct _buf *file, char *buf, ssize_t size, offset_t off)
{
	return (vn_rdwr(UIO_READ, file->vp, buf, size, off,
	       UIO_SYSSPACE, 0, RLIM64_INFINITY, 0, NULL));
} /* kobj_read_file() */
EXPORT_SYMBOL(kobj_read_file);

int
kobj_get_filesize(struct _buf *file, uint64_t *size)
{
        vattr_t vap;
	int rc;

	rc = VOP_GETATTR(file->vp, &vap, 0, 0, NULL);
	if (rc)
		return (rc);

        *size = vap.va_size;

        return (rc);
} /* kobj_get_filesize() */
EXPORT_SYMBOL(kobj_get_filesize);
