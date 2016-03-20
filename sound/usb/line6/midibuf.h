/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#ifndef MIDIBUF_H
#define MIDIBUF_H

struct midi_buffer {
	unsigned char *buf;
	int size;
	int split;
	int pos_read, pos_write;
	int full;
	int command_prev;
};

extern int line6_midibuf_bytes_used(struct midi_buffer *mb);
extern int line6_midibuf_bytes_free(struct midi_buffer *mb);
extern void line6_midibuf_destroy(struct midi_buffer *mb);
extern int line6_midibuf_ignore(struct midi_buffer *mb, int length);
extern int line6_midibuf_init(struct midi_buffer *mb, int size, int split);
extern int line6_midibuf_read(struct midi_buffer *mb, unsigned char *data,
			      int length);
extern void line6_midibuf_reset(struct midi_buffer *mb);
extern int line6_midibuf_write(struct midi_buffer *mb, unsigned char *data,
			       int length);

#endif
