/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/fbio.h>
#include <sys/consio.h>

#include <machine/md_var.h>
#include <machine/bus.h>

#include <dev/fb/fbreg.h>
#include <dev/syscons/syscons.h>

#define vtb_wrap(vtb, at, offset)				\
    (((at) + (offset) + (vtb)->vtb_size)%(vtb)->vtb_size)

void
sc_vtb_init(sc_vtb_t *vtb, int type, int cols, int rows, void *buf, int wait)
{
	vtb->vtb_flags = 0;
	vtb->vtb_type = type;
	vtb->vtb_cols = cols;
	vtb->vtb_rows = rows;
	vtb->vtb_size = cols*rows;
	vtb->vtb_buffer = 0;
	vtb->vtb_tail = 0;

	switch (type) {
	case VTB_MEMORY:
	case VTB_RINGBUFFER:
		if ((buf == NULL) && (cols*rows != 0)) {
			vtb->vtb_buffer =
				(vm_offset_t)malloc(cols*rows*sizeof(u_int16_t),
						    M_DEVBUF, 
						    (wait) ? M_WAITOK : M_NOWAIT);
			if (vtb->vtb_buffer != 0) {
				bzero((void *)sc_vtb_pointer(vtb, 0),
				      cols*rows*sizeof(u_int16_t));
				vtb->vtb_flags |= VTB_ALLOCED;
			}
		} else {
			vtb->vtb_buffer = (vm_offset_t)buf;
		}
		vtb->vtb_flags |= VTB_VALID;
		break;
#ifndef __sparc64__
	case VTB_FRAMEBUFFER:
		vtb->vtb_buffer = (vm_offset_t)buf;
		vtb->vtb_flags |= VTB_VALID;
		break;
#endif
	default:
		break;
	}
}

void
sc_vtb_destroy(sc_vtb_t *vtb)
{
	vm_offset_t p;

	vtb->vtb_cols = 0;
	vtb->vtb_rows = 0;
	vtb->vtb_size = 0;
	vtb->vtb_tail = 0;

	p = vtb->vtb_buffer;
	vtb->vtb_buffer = 0;
	switch (vtb->vtb_type) {
	case VTB_MEMORY:
	case VTB_RINGBUFFER:
		if ((vtb->vtb_flags & VTB_ALLOCED) && (p != 0))
			free((void *)p, M_DEVBUF);
		break;
	default:
		break;
	}
	vtb->vtb_flags = 0;
	vtb->vtb_type = VTB_INVALID;
}

size_t
sc_vtb_size(int cols, int rows)
{
	return (size_t)(cols*rows*sizeof(u_int16_t));
}

int
sc_vtb_getc(sc_vtb_t *vtb, int at)
{
#ifndef __sparc64__
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		return (readw(sc_vtb_pointer(vtb, at)) & 0x00ff);
	else
#endif
		return (*(u_int16_t *)sc_vtb_pointer(vtb, at) & 0x00ff);
}

int
sc_vtb_geta(sc_vtb_t *vtb, int at)
{
#ifndef __sparc64__
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		return (readw(sc_vtb_pointer(vtb, at)) & 0xff00);
	else
#endif
		return (*(u_int16_t *)sc_vtb_pointer(vtb, at) & 0xff00);
}

void
sc_vtb_putc(sc_vtb_t *vtb, int at, int c, int a)
{
#ifndef __sparc64__
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		writew(sc_vtb_pointer(vtb, at), a | c);
	else
#endif
		*(u_int16_t *)sc_vtb_pointer(vtb, at) = a | c;
}

vm_offset_t
sc_vtb_putchar(sc_vtb_t *vtb, vm_offset_t p, int c, int a)
{
#ifndef __sparc64__
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		writew(p, a | c);
	else
#endif
		*(u_int16_t *)p = a | c;
	return (p + sizeof(u_int16_t));
}

vm_offset_t
sc_vtb_pointer(sc_vtb_t *vtb, int at)
{
	return (vtb->vtb_buffer + sizeof(u_int16_t)*(at));
}

int
sc_vtb_pos(sc_vtb_t *vtb, int pos, int offset)
{
	return ((pos + offset + vtb->vtb_size)%vtb->vtb_size);
}

void
sc_vtb_clear(sc_vtb_t *vtb, int c, int attr)
{
#ifndef __sparc64__
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		fillw_io(attr | c, sc_vtb_pointer(vtb, 0), vtb->vtb_size);
	else
#endif
		fillw(attr | c, (void *)sc_vtb_pointer(vtb, 0), vtb->vtb_size);
}

void
sc_vtb_copy(sc_vtb_t *vtb1, int from, sc_vtb_t *vtb2, int to, int count)
{
#ifndef __sparc64__
	/* XXX if both are VTB_VRAMEBUFFER... */
	if (vtb2->vtb_type == VTB_FRAMEBUFFER)
		bcopy_toio(sc_vtb_pointer(vtb1, from),
			   sc_vtb_pointer(vtb2, to),
			   count*sizeof(u_int16_t));
	else if (vtb1->vtb_type == VTB_FRAMEBUFFER)
		bcopy_fromio(sc_vtb_pointer(vtb1, from),
			     sc_vtb_pointer(vtb2, to),
			     count*sizeof(u_int16_t));
	else
#endif
		bcopy((void *)sc_vtb_pointer(vtb1, from),
		      (void *)sc_vtb_pointer(vtb2, to),
		      count*sizeof(u_int16_t));
}

void
sc_vtb_append(sc_vtb_t *vtb1, int from, sc_vtb_t *vtb2, int count)
{
	int len;

	if (vtb2->vtb_type != VTB_RINGBUFFER)
		return;

	while (count > 0) {
		len = imin(count, vtb2->vtb_size - vtb2->vtb_tail);
#ifndef __sparc64__
		if (vtb1->vtb_type == VTB_FRAMEBUFFER)
			bcopy_fromio(sc_vtb_pointer(vtb1, from),
				     sc_vtb_pointer(vtb2, vtb2->vtb_tail),
				     len*sizeof(u_int16_t));
		else
#endif
			bcopy((void *)sc_vtb_pointer(vtb1, from),
			      (void *)sc_vtb_pointer(vtb2, vtb2->vtb_tail),
			      len*sizeof(u_int16_t));
		from += len;
		count -= len;
		vtb2->vtb_tail = vtb_wrap(vtb2, vtb2->vtb_tail, len);
	}
}

void
sc_vtb_seek(sc_vtb_t *vtb, int pos)
{
	vtb->vtb_tail = pos%vtb->vtb_size;
}

void
sc_vtb_erase(sc_vtb_t *vtb, int at, int count, int c, int attr)
{
	if (at + count > vtb->vtb_size)
		count = vtb->vtb_size - at;
#ifndef __sparc64__
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		fillw_io(attr | c, sc_vtb_pointer(vtb, at), count);
	else
#endif
		fillw(attr | c, (void *)sc_vtb_pointer(vtb, at), count);
}

void
sc_vtb_move(sc_vtb_t *vtb, int from, int to, int count)
{
	if (from + count > vtb->vtb_size)
		count = vtb->vtb_size - from;
	if (to + count > vtb->vtb_size)
		count = vtb->vtb_size - to;
	if (count <= 0)
		return;
#ifndef __sparc64__
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		bcopy_io(sc_vtb_pointer(vtb, from),
			 sc_vtb_pointer(vtb, to), count*sizeof(u_int16_t)); 
	else
#endif
		bcopy((void *)sc_vtb_pointer(vtb, from),
		      (void *)sc_vtb_pointer(vtb, to), count*sizeof(u_int16_t));
}

void
sc_vtb_delete(sc_vtb_t *vtb, int at, int count, int c, int attr)
{
	int len;

	if (at + count > vtb->vtb_size)
		count = vtb->vtb_size - at;
	len = vtb->vtb_size - at - count;
	if (len > 0) {
#ifndef __sparc64__
		if (vtb->vtb_type == VTB_FRAMEBUFFER)
			bcopy_io(sc_vtb_pointer(vtb, at + count),
				 sc_vtb_pointer(vtb, at),
				 len*sizeof(u_int16_t)); 
		else
#endif
			bcopy((void *)sc_vtb_pointer(vtb, at + count),
			      (void *)sc_vtb_pointer(vtb, at),
			      len*sizeof(u_int16_t)); 
	}
#ifndef __sparc64__
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		fillw_io(attr | c, sc_vtb_pointer(vtb, at + len),
			 vtb->vtb_size - at - len);
	else
#endif
		fillw(attr | c, (void *)sc_vtb_pointer(vtb, at + len),
		      vtb->vtb_size - at - len);
}

void
sc_vtb_ins(sc_vtb_t *vtb, int at, int count, int c, int attr)
{
	if (at + count > vtb->vtb_size)
		count = vtb->vtb_size - at;
	else {
#ifndef __sparc64__
		if (vtb->vtb_type == VTB_FRAMEBUFFER)
			bcopy_io(sc_vtb_pointer(vtb, at),
				 sc_vtb_pointer(vtb, at + count),
				 (vtb->vtb_size - at - count)*sizeof(u_int16_t)); 
		else
#endif
			bcopy((void *)sc_vtb_pointer(vtb, at),
			      (void *)sc_vtb_pointer(vtb, at + count),
			      (vtb->vtb_size - at - count)*sizeof(u_int16_t)); 
	}
#ifndef __sparc64__
	if (vtb->vtb_type == VTB_FRAMEBUFFER)
		fillw_io(attr | c, sc_vtb_pointer(vtb, at), count);
	else
#endif
		fillw(attr | c, (void *)sc_vtb_pointer(vtb, at), count);
}
