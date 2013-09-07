/*
 *  thinkpad_ec.h - interface to ThinkPad embedded controller LPC3 functions
 *
 *  Copyright (C) 2005 Shem Multinymous <multinymous@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _THINKPAD_EC_H
#define _THINKPAD_EC_H

#ifdef __KERNEL__

#define TP_CONTROLLER_ROW_LEN 16

/* EC transactions input and output (possibly partial) vectors of 16 bytes. */
struct thinkpad_ec_row {
	u16 mask; /* bitmap of which entries of val[] are meaningful */
	u8 val[TP_CONTROLLER_ROW_LEN];
};

extern int __must_check thinkpad_ec_lock(void);
extern int __must_check thinkpad_ec_try_lock(void);
extern void thinkpad_ec_unlock(void);

extern int thinkpad_ec_read_row(const struct thinkpad_ec_row *args,
				struct thinkpad_ec_row *data);
extern int thinkpad_ec_try_read_row(const struct thinkpad_ec_row *args,
				    struct thinkpad_ec_row *mask);
extern int thinkpad_ec_prefetch_row(const struct thinkpad_ec_row *args);
extern void thinkpad_ec_invalidate(void);


#endif /* __KERNEL */
#endif /* _THINKPAD_EC_H */
