/*
 *   include/uapi/linux/cifs/cifs_mount.h
 *
 *   Author(s): Scott Lovenberg (scott.lovenberg@gmail.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 */
#ifndef _CIFS_MOUNT_H
#define _CIFS_MOUNT_H

/* Max string lengths for cifs mounting options. */
#define CIFS_MAX_DOMAINNAME_LEN 256 /* max fully qualified domain name */
#define CIFS_MAX_USERNAME_LEN   256 /* reasonable max for current servers */
#define CIFS_MAX_PASSWORD_LEN   512 /* Windows max seems to be 256 wide chars */
#define CIFS_MAX_SHARE_LEN       80

#endif /* _CIFS_MOUNT_H */
