/* $FreeBSD$ */
/*-
 * Copyright (c) 2013 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <bsd_kernel.h>

struct burst {
	uint32_t dw0;
	uint32_t dw1;
	uint32_t dw2;
	uint32_t dw3;
	uint32_t dw4;
	uint32_t dw5;
	uint32_t dw6;
	uint32_t dw7;
};

int
bus_space_subregion(bus_space_tag_t t, bus_space_handle_t bsh,
    bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp)
{

	*nbshp = bsh + offset;
	return (0);
}

void
bus_space_read_multi_1(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint8_t *datap, bus_size_t count)
{
	while (count--) {
		*datap++ = bus_space_read_1(t, h, offset);
	}
}

void
bus_space_read_multi_2(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint16_t *datap, bus_size_t count)
{
	while (count--) {
		*datap++ = bus_space_read_2(t, h, offset);
	}
}

void
bus_space_read_multi_4(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint32_t *datap, bus_size_t count)
{
	h += offset;

	while (count--) {
		*datap++ = *((volatile uint32_t *)h);
	}
}

void
bus_space_write_multi_1(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint8_t *datap, bus_size_t count)
{
	while (count--) {
		uint8_t temp = *datap++;

		bus_space_write_1(t, h, offset, temp);
	}
}

void
bus_space_write_multi_2(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint16_t *datap, bus_size_t count)
{
	while (count--) {
		uint16_t temp = *datap++;

		bus_space_write_2(t, h, offset, temp);
	}
}

void
bus_space_write_multi_4(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint32_t *datap, bus_size_t count)
{
	h += offset;

	while (count--) {
		*((volatile uint32_t *)h) = *datap++;
	}
}

void
bus_space_write_1(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint8_t data)
{
	*((volatile uint8_t *)(h + offset)) = data;
}

void
bus_space_write_2(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint16_t data)
{
	*((volatile uint16_t *)(h + offset)) = data;
}

void
bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint32_t data)
{
	*((volatile uint32_t *)(h + offset)) = data;
}

uint8_t
bus_space_read_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset)
{
	return (*((volatile uint8_t *)(h + offset)));
}

uint16_t
bus_space_read_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset)
{
	return (*((volatile uint16_t *)(h + offset)));
}

uint32_t
bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset)
{
	return (*((volatile uint32_t *)(h + offset)));
}

void
bus_space_read_region_1(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint8_t *datap, bus_size_t count)
{
	h += offset;

	while (count--) {
		*datap++ = *((volatile uint8_t *)h);
		h += 1;
	}
}

void
bus_space_write_region_1(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint8_t *datap, bus_size_t count)
{
	h += offset;

	while (count--) {
		*((volatile uint8_t *)h) = *datap++;
		h += 1;
	}
}

void
bus_space_read_region_4(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint32_t *datap, bus_size_t count)
{
	enum { BURST = sizeof(struct burst) / 4 };

	h += offset;

	while (count >= BURST) {
		*(struct burst *)datap = *((/* volatile */ struct burst *)h);

		h += BURST * 4;
		datap += BURST;
		count -= BURST;
	}

	while (count--) {
		*datap++ = *((volatile uint32_t *)h);
		h += 4;
	}
}

void
bus_space_write_region_4(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint32_t *datap, bus_size_t count)
{
	enum { BURST = sizeof(struct burst) / 4 };

	h += offset;

	while (count >= BURST) {
		*((/* volatile */ struct burst *)h) = *(struct burst *)datap;

		h += BURST * 4;
		datap += BURST;
		count -= BURST;
	}

	while (count--) {
		*((volatile uint32_t *)h) = *datap++;
		h += 4;
	}
}
