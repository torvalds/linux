/*
 * Copyright (C) 2007-2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#ifndef _NET_BATMAN_ADV_RING_BUFFER_H_
#define _NET_BATMAN_ADV_RING_BUFFER_H_

void ring_buffer_set(uint8_t lq_recv[], uint8_t *lq_index, uint8_t value);
uint8_t ring_buffer_avg(uint8_t lq_recv[]);

#endif /* _NET_BATMAN_ADV_RING_BUFFER_H_ */
