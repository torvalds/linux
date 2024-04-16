/*
 * Copyright (c) 2016 - Savoir-faire Linux
 * Author: Sebastien Bourdelin <sebastien.bourdelin@savoirfairelinux.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _TS_NBUS_H
#define _TS_NBUS_H

struct ts_nbus;

extern int ts_nbus_read(struct ts_nbus *ts_nbus, u8 adr, u16 *val);
extern int ts_nbus_write(struct ts_nbus *ts_nbus, u8 adr, u16 val);

#endif /* _TS_NBUS_H */
