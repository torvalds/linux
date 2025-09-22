/*	$OpenBSD: x86emu_util.c,v 1.6 2015/03/14 03:38:50 jsg Exp $	*/
/*	$NetBSD: x86emu_util.c,v 1.2 2007/12/04 17:32:22 joerg Exp $	*/

/*
 *
 *  Realmode X86 Emulator Library
 *
 *  Copyright (C) 1996-1999 SciTech Software, Inc.
 *  Copyright (C) David Mosberger-Tang
 *  Copyright (C) 1999 Egbert Eich
 *  Copyright (C) 2007 Joerg Sonnenberger
 *
 *  ========================================================================
 *
 *  Permission to use, copy, modify, distribute, and sell this software and
 *  its documentation for any purpose is hereby granted without fee,
 *  provided that the above copyright notice appear in all copies and that
 *  both that copyright notice and this permission notice appear in
 *  supporting documentation, and that the name of the authors not be used
 *  in advertising or publicity pertaining to distribution of the software
 *  without specific, written prior permission.  The authors makes no
 *  representations about the suitability of this software for any purpose.
 *  It is provided "as is" without express or implied warranty.
 *
 *  THE AUTHORS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 *  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 *  EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 *  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 *  USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 *  OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *  PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <sys/param.h>
#include <sys/endian.h>

#include <dev/x86emu/x86emu.h>



/*
 * PARAMETERS:
 * addr	- Emulator memory address to read
 * 
 * RETURNS:
 * Byte value read from emulator memory.
 * 
 * REMARKS:
 * Reads a byte value from the emulator memory.
 */
static uint8_t
rdb(struct x86emu *emu, uint32_t addr)
{
	if (addr > emu->mem_size - 1)
		x86emu_halt_sys(emu);
	return emu->mem_base[addr];
}

/*
 * PARAMETERS:
 * addr	- Emulator memory address to read
 * 
 * RETURNS:
 * Word value read from emulator memory.
 * 
 * REMARKS:
 * Reads a word value from the emulator memory.
 */
static uint16_t
rdw(struct x86emu *emu, uint32_t addr)
{
	if (addr > emu->mem_size - 2)
		x86emu_halt_sys(emu);
#ifdef __STRICT_ALIGNMENT
	if (addr & 1) {
		u_int8_t *a = emu->mem_base + addr;
		u_int16_t r;

		r = ((*(a + 0) << 0) & 0x00ff) |
		    ((*(a + 1) << 8) & 0xff00);
		return r;
	} else
		return letoh32(*(u_int32_t *)(emu->mem_base + addr));
#else
	return letoh16(*(u_int16_t *)(emu->mem_base + addr));
#endif
}

/*
 * PARAMETERS:
 * addr	- Emulator memory address to read
 * 
 * RETURNS:
 * Long value read from emulator memory.
 * REMARKS:
 * Reads a long value from the emulator memory.
 */
static uint32_t
rdl(struct x86emu *emu, uint32_t addr)
{
	if (addr > emu->mem_size - 4)
		x86emu_halt_sys(emu);
#ifdef __STRICT_ALIGNMENT
	if (addr & 3) {
		u_int8_t *a = emu->mem_base + addr;
		u_int32_t r;

		r = ((*(a + 0) <<  0) & 0x000000ff) |
		    ((*(a + 1) <<  8) & 0x0000ff00) |
		    ((*(a + 2) << 16) & 0x00ff0000) |
		    ((*(a + 3) << 24) & 0xff000000);
		return r;
	} else
		return letoh32(*(u_int32_t *)(emu->mem_base + addr));
#else
	return letoh32(*(u_int32_t *)(emu->mem_base + addr));
#endif
}

/*
 * PARAMETERS:
 * addr	- Emulator memory address to read
 * val		- Value to store
 * 
 * REMARKS:
 * Writes a byte value to emulator memory.
 */
static void
wrb(struct x86emu *emu, uint32_t addr, uint8_t val)
{
	if (addr > emu->mem_size - 1)
		x86emu_halt_sys(emu);
	emu->mem_base[addr] = val;
}

/*
 * PARAMETERS:
 * addr	- Emulator memory address to read
 * val		- Value to store
 * 
 * REMARKS:
 * Writes a word value to emulator memory.
 */
static void
wrw(struct x86emu *emu, uint32_t addr, uint16_t val)
{
	if (addr > emu->mem_size - 2)
		x86emu_halt_sys(emu);
#ifdef __STRICT_ALIGNMENT
	if (addr & 1) {
		u_int8_t *a = emu->mem_base + addr;

		*((a + 0)) = (val >> 0) & 0xff;
		*((a + 1)) = (val >> 8) & 0xff;
	} else
		*((u_int16_t *)(emu->mem_base + addr)) = htole16(val);
#else
	*((u_int16_t *)(emu->mem_base + addr)) = htole16(val);
#endif
}

/*
 * PARAMETERS:
 * addr	- Emulator memory address to read
 * val		- Value to store
 * 
 * REMARKS:
 * Writes a long value to emulator memory.
 */
static void
wrl(struct x86emu *emu, uint32_t addr, uint32_t val)
{
	if (addr > emu->mem_size - 4)
		x86emu_halt_sys(emu);
#ifdef __STRICT_ALIGNMENT
	if (addr & 3) {
		u_int8_t *a = emu->mem_base + addr;

		*((a + 0) = (val >>  0) & 0xff;
		*((a + 1) = (val >>  8) & 0xff;
		*((a + 2) = (val >> 16) & 0xff;
		*((a + 3) = (val >> 24) & 0xff;
	} else
		*((u_int32_t *)(emu->mem_base + addr)) = htole32(val);
#else
	*((u_int32_t *)(emu->mem_base + addr)) = htole32(val);
#endif
}

/* Setup */

void
x86emu_init_default(struct x86emu *emu)
{
	int i;

	emu->emu_rdb = rdb;
	emu->emu_rdw = rdw;
	emu->emu_rdl = rdl;
	emu->emu_wrb = wrb;
	emu->emu_wrw = wrw;
	emu->emu_wrl = wrl;

	for (i = 0; i < 256; i++)
		emu->_x86emu_intrTab[i] = NULL;
}
