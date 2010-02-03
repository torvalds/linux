/*  Syslog internals
 *
 *  Copyright 2010 Canonical, Ltd.
 *  Author: Kees Cook <kees.cook@canonical.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _LINUX_SYSLOG_H
#define _LINUX_SYSLOG_H

#define SYSLOG_FROM_CALL 0
#define SYSLOG_FROM_FILE 1

int do_syslog(int type, char __user *buf, int count, bool from_file);

#endif /* _LINUX_SYSLOG_H */
