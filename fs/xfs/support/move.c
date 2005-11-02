/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <xfs.h>

/* Read from kernel buffer at src to user/kernel buffer defined
 * by the uio structure. Advance the pointer in the uio struct
 * as we go.
 */
int
uio_read(caddr_t src, size_t len, struct uio *uio)
{
	size_t	count;

	if (!len || !uio->uio_resid)
		return 0;

	count = uio->uio_iov->iov_len;
	if (!count)
		return 0;
	if (count > len)
		count = len;

	if (uio->uio_segflg == UIO_USERSPACE) {
		if (copy_to_user(uio->uio_iov->iov_base, src, count))
			return EFAULT;
	} else {
		ASSERT(uio->uio_segflg == UIO_SYSSPACE);
		memcpy(uio->uio_iov->iov_base, src, count);
	}

	uio->uio_iov->iov_base = (void*)((char*)uio->uio_iov->iov_base + count);
	uio->uio_iov->iov_len -= count;
	uio->uio_offset += count;
	uio->uio_resid -= count;
	return 0;
}
