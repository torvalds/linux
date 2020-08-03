/*
 * Copyright (C) 2012 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License (not later!)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#ifndef _KBUFFER_H
#define _KBUFFER_H

#ifndef TS_SHIFT
#define TS_SHIFT		27
#endif

enum kbuffer_endian {
	KBUFFER_ENDIAN_BIG,
	KBUFFER_ENDIAN_LITTLE,
};

enum kbuffer_long_size {
	KBUFFER_LSIZE_4,
	KBUFFER_LSIZE_8,
};

enum {
	KBUFFER_TYPE_PADDING		= 29,
	KBUFFER_TYPE_TIME_EXTEND	= 30,
	KBUFFER_TYPE_TIME_STAMP		= 31,
};

struct kbuffer;

struct kbuffer *kbuffer_alloc(enum kbuffer_long_size size, enum kbuffer_endian endian);
void kbuffer_free(struct kbuffer *kbuf);
int kbuffer_load_subbuffer(struct kbuffer *kbuf, void *subbuffer);
void *kbuffer_read_event(struct kbuffer *kbuf, unsigned long long *ts);
void *kbuffer_next_event(struct kbuffer *kbuf, unsigned long long *ts);
unsigned long long kbuffer_timestamp(struct kbuffer *kbuf);
unsigned long long kbuffer_subbuf_timestamp(struct kbuffer *kbuf, void *subbuf);
unsigned int kbuffer_ptr_delta(struct kbuffer *kbuf, void *ptr);

void *kbuffer_translate_data(int swap, void *data, unsigned int *size);

void *kbuffer_read_at_offset(struct kbuffer *kbuf, int offset, unsigned long long *ts);

int kbuffer_curr_index(struct kbuffer *kbuf);

int kbuffer_curr_offset(struct kbuffer *kbuf);
int kbuffer_curr_size(struct kbuffer *kbuf);
int kbuffer_event_size(struct kbuffer *kbuf);
int kbuffer_missed_events(struct kbuffer *kbuf);
int kbuffer_subbuffer_size(struct kbuffer *kbuf);

void kbuffer_set_old_format(struct kbuffer *kbuf);
int kbuffer_start_of_data(struct kbuffer *kbuf);

/* Debugging */

struct kbuffer_raw_info {
	int			type;
	int			length;
	unsigned long long	delta;
	void			*next;
};

/* Read raw data */
struct kbuffer_raw_info *kbuffer_raw_get(struct kbuffer *kbuf, void *subbuf,
					 struct kbuffer_raw_info *info);

#endif /* _K_BUFFER_H */
