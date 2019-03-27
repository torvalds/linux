/*	$OpenBSD: x86emu.c,v 1.9 2014/06/15 11:04:49 pirofti Exp $	*/
/*	$NetBSD: x86emu.c,v 1.7 2009/02/03 19:26:29 joerg Exp $	*/

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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <contrib/x86emu/x86emu.h>
#include <contrib/x86emu/x86emu_regs.h>

static void 	x86emu_intr_raise (struct x86emu *, uint8_t type);

static void	x86emu_exec_one_byte(struct x86emu *);
static void	x86emu_exec_two_byte(struct x86emu *);

static void	fetch_decode_modrm (struct x86emu *);
static uint8_t	fetch_byte_imm (struct x86emu *);
static uint16_t	fetch_word_imm (struct x86emu *);
static uint32_t	fetch_long_imm (struct x86emu *);
static uint8_t	fetch_data_byte (struct x86emu *, uint32_t offset);
static uint8_t	fetch_byte (struct x86emu *, u_int segment, uint32_t offset);
static uint16_t	fetch_data_word (struct x86emu *, uint32_t offset);
static uint16_t	fetch_word (struct x86emu *, uint32_t segment, uint32_t offset);
static uint32_t	fetch_data_long (struct x86emu *, uint32_t offset);
static uint32_t	fetch_long (struct x86emu *, uint32_t segment, uint32_t offset);
static void	store_data_byte (struct x86emu *, uint32_t offset, uint8_t val);
static void	store_byte (struct x86emu *, uint32_t segment, uint32_t offset, uint8_t val);
static void	store_data_word (struct x86emu *, uint32_t offset, uint16_t val);
static void	store_word (struct x86emu *, uint32_t segment, uint32_t offset, uint16_t val);
static void	store_data_long (struct x86emu *, uint32_t offset, uint32_t val);
static void	store_long (struct x86emu *, uint32_t segment, uint32_t offset, uint32_t val);
static uint8_t*	decode_rl_byte_register(struct x86emu *);
static uint16_t*	decode_rl_word_register(struct x86emu *);
static uint32_t* 	decode_rl_long_register(struct x86emu *);
static uint8_t* 	decode_rh_byte_register(struct x86emu *);
static uint16_t* 	decode_rh_word_register(struct x86emu *);
static uint32_t* 	decode_rh_long_register(struct x86emu *);
static uint16_t* 	decode_rh_seg_register(struct x86emu *);
static uint32_t	decode_rl_address(struct x86emu *);

static uint8_t 	decode_and_fetch_byte(struct x86emu *);
static uint16_t 	decode_and_fetch_word(struct x86emu *);
static uint32_t 	decode_and_fetch_long(struct x86emu *);

static uint8_t 	decode_and_fetch_byte_imm8(struct x86emu *, uint8_t *);
static uint16_t 	decode_and_fetch_word_imm8(struct x86emu *, uint8_t *);
static uint32_t 	decode_and_fetch_long_imm8(struct x86emu *, uint8_t *);

static uint16_t 	decode_and_fetch_word_disp(struct x86emu *, int16_t);
static uint32_t 	decode_and_fetch_long_disp(struct x86emu *, int16_t);

static void	write_back_byte(struct x86emu *, uint8_t);
static void	write_back_word(struct x86emu *, uint16_t);
static void	write_back_long(struct x86emu *, uint32_t);

static uint16_t	aaa_word (struct x86emu *, uint16_t d);
static uint16_t	aas_word (struct x86emu *, uint16_t d);
static uint16_t	aad_word (struct x86emu *, uint16_t d);
static uint16_t	aam_word (struct x86emu *, uint8_t d);
static uint8_t	adc_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	adc_word (struct x86emu *, uint16_t d, uint16_t s);
static uint32_t	adc_long (struct x86emu *, uint32_t d, uint32_t s);
static uint8_t	add_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	add_word (struct x86emu *, uint16_t d, uint16_t s);
static uint32_t	add_long (struct x86emu *, uint32_t d, uint32_t s);
static uint8_t	and_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	and_word (struct x86emu *, uint16_t d, uint16_t s);
static uint32_t	and_long (struct x86emu *, uint32_t d, uint32_t s);
static uint8_t	cmp_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	cmp_word (struct x86emu *, uint16_t d, uint16_t s);
static uint32_t	cmp_long (struct x86emu *, uint32_t d, uint32_t s);
static void	cmp_byte_no_return (struct x86emu *, uint8_t d, uint8_t s);
static void	cmp_word_no_return (struct x86emu *, uint16_t d, uint16_t s);
static void	cmp_long_no_return (struct x86emu *, uint32_t d, uint32_t s);
static uint8_t	daa_byte (struct x86emu *, uint8_t d);
static uint8_t	das_byte (struct x86emu *, uint8_t d);
static uint8_t	dec_byte (struct x86emu *, uint8_t d);
static uint16_t	dec_word (struct x86emu *, uint16_t d);
static uint32_t	dec_long (struct x86emu *, uint32_t d);
static uint8_t	inc_byte (struct x86emu *, uint8_t d);
static uint16_t	inc_word (struct x86emu *, uint16_t d);
static uint32_t	inc_long (struct x86emu *, uint32_t d);
static uint8_t	or_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	or_word (struct x86emu *, uint16_t d, uint16_t s);
static uint32_t	or_long (struct x86emu *, uint32_t d, uint32_t s);
static uint8_t	neg_byte (struct x86emu *, uint8_t s);
static uint16_t	neg_word (struct x86emu *, uint16_t s);
static uint32_t	neg_long (struct x86emu *, uint32_t s);
static uint8_t	rcl_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	rcl_word (struct x86emu *, uint16_t d, uint8_t s);
static uint32_t	rcl_long (struct x86emu *, uint32_t d, uint8_t s);
static uint8_t	rcr_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	rcr_word (struct x86emu *, uint16_t d, uint8_t s);
static uint32_t	rcr_long (struct x86emu *, uint32_t d, uint8_t s);
static uint8_t	rol_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	rol_word (struct x86emu *, uint16_t d, uint8_t s);
static uint32_t	rol_long (struct x86emu *, uint32_t d, uint8_t s);
static uint8_t	ror_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	ror_word (struct x86emu *, uint16_t d, uint8_t s);
static uint32_t	ror_long (struct x86emu *, uint32_t d, uint8_t s);
static uint8_t	shl_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	shl_word (struct x86emu *, uint16_t d, uint8_t s);
static uint32_t	shl_long (struct x86emu *, uint32_t d, uint8_t s);
static uint8_t	shr_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	shr_word (struct x86emu *, uint16_t d, uint8_t s);
static uint32_t	shr_long (struct x86emu *, uint32_t d, uint8_t s);
static uint8_t	sar_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	sar_word (struct x86emu *, uint16_t d, uint8_t s);
static uint32_t	sar_long (struct x86emu *, uint32_t d, uint8_t s);
static uint16_t	shld_word (struct x86emu *, uint16_t d, uint16_t fill, uint8_t s);
static uint32_t	shld_long (struct x86emu *, uint32_t d, uint32_t fill, uint8_t s);
static uint16_t	shrd_word (struct x86emu *, uint16_t d, uint16_t fill, uint8_t s);
static uint32_t	shrd_long (struct x86emu *, uint32_t d, uint32_t fill, uint8_t s);
static uint8_t	sbb_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	sbb_word (struct x86emu *, uint16_t d, uint16_t s);
static uint32_t	sbb_long (struct x86emu *, uint32_t d, uint32_t s);
static uint8_t	sub_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	sub_word (struct x86emu *, uint16_t d, uint16_t s);
static uint32_t	sub_long (struct x86emu *, uint32_t d, uint32_t s);
static void	test_byte (struct x86emu *, uint8_t d, uint8_t s);
static void	test_word (struct x86emu *, uint16_t d, uint16_t s);
static void	test_long (struct x86emu *, uint32_t d, uint32_t s);
static uint8_t	xor_byte (struct x86emu *, uint8_t d, uint8_t s);
static uint16_t	xor_word (struct x86emu *, uint16_t d, uint16_t s);
static uint32_t	xor_long (struct x86emu *, uint32_t d, uint32_t s);
static void	imul_byte (struct x86emu *, uint8_t s);
static void	imul_word (struct x86emu *, uint16_t s);
static void	imul_long (struct x86emu *, uint32_t s);
static void	mul_byte (struct x86emu *, uint8_t s);
static void	mul_word (struct x86emu *, uint16_t s);
static void	mul_long (struct x86emu *, uint32_t s);
static void	idiv_byte (struct x86emu *, uint8_t s);
static void	idiv_word (struct x86emu *, uint16_t s);
static void	idiv_long (struct x86emu *, uint32_t s);
static void	div_byte (struct x86emu *, uint8_t s);
static void	div_word (struct x86emu *, uint16_t s);
static void	div_long (struct x86emu *, uint32_t s);
static void	ins (struct x86emu *, int size);
static void	outs (struct x86emu *, int size);
static void	push_word (struct x86emu *, uint16_t w);
static void	push_long (struct x86emu *, uint32_t w);
static uint16_t	pop_word (struct x86emu *);
static uint32_t	pop_long (struct x86emu *);

/*
 * REMARKS:
 * Handles any pending asychronous interrupts.
 */
static void
x86emu_intr_dispatch(struct x86emu *emu, uint8_t intno)
{
	if (emu->_x86emu_intrTab[intno]) {
		(*emu->_x86emu_intrTab[intno]) (emu, intno);
	} else {
		push_word(emu, (uint16_t) emu->x86.R_FLG);
		CLEAR_FLAG(F_IF);
		CLEAR_FLAG(F_TF);
		push_word(emu, emu->x86.R_CS);
		emu->x86.R_CS = fetch_word(emu, 0, intno * 4 + 2);
		push_word(emu, emu->x86.R_IP);
		emu->x86.R_IP = fetch_word(emu, 0, intno * 4);
	}
}

static void 
x86emu_intr_handle(struct x86emu *emu)
{
	uint8_t intno;

	if (emu->x86.intr & INTR_SYNCH) {
		intno = emu->x86.intno;
		emu->x86.intr = 0;
		x86emu_intr_dispatch(emu, intno);
	}
}

/*
 * PARAMETERS:
 * intrnum - Interrupt number to raise
 * 
 * REMARKS:
 * Raise the specified interrupt to be handled before the execution of the
 * next instruction.
 */
void 
x86emu_intr_raise(struct x86emu *emu, uint8_t intrnum)
{
	emu->x86.intno = intrnum;
	emu->x86.intr |= INTR_SYNCH;
}

/*
 * REMARKS:
 * Main execution loop for the emulator. We return from here when the system
 * halts, which is normally caused by a stack fault when we return from the
 * original real mode call.
 */
void 
x86emu_exec(struct x86emu *emu)
{
	emu->x86.intr = 0;

	if (setjmp(emu->exec_state))
		return;

	for (;;) {
		if (emu->x86.intr) {
			if (((emu->x86.intr & INTR_SYNCH) &&
			    (emu->x86.intno == 0 || emu->x86.intno == 2)) ||
			    !ACCESS_FLAG(F_IF)) {
				x86emu_intr_handle(emu);
			}
		}
		if (emu->x86.R_CS == 0 && emu->x86.R_IP == 0)
			return;
		x86emu_exec_one_byte(emu);
		++emu->cur_cycles;
	}
}

void
x86emu_exec_call(struct x86emu *emu, uint16_t seg, uint16_t off)
{
	push_word(emu, 0);
	push_word(emu, 0);
	emu->x86.R_CS = seg;
	emu->x86.R_IP = off;

	x86emu_exec(emu);
}

void
x86emu_exec_intr(struct x86emu *emu, uint8_t intr)
{
	push_word(emu, emu->x86.R_FLG);
	CLEAR_FLAG(F_IF);
	CLEAR_FLAG(F_TF);
	push_word(emu, 0);
	push_word(emu, 0);
	emu->x86.R_CS = (*emu->emu_rdw)(emu, intr * 4 + 2);
	emu->x86.R_IP = (*emu->emu_rdw)(emu, intr * 4);
	emu->x86.intr = 0;

	x86emu_exec(emu);
}

/*
 * REMARKS:
 * Halts the system by setting the halted system flag.
 */
void 
x86emu_halt_sys(struct x86emu *emu)
{
	longjmp(emu->exec_state, 1);
}

/*
 * PARAMETERS:
 * mod		- Mod value from decoded byte
 * regh	- Reg h value from decoded byte
 * regl	- Reg l value from decoded byte
 * 
 * REMARKS:
 * Raise the specified interrupt to be handled before the execution of the
 * next instruction.
 * 
 * NOTE: Do not inline this function, as (*emu->emu_rdb) is already inline!
 */
static void 
fetch_decode_modrm(struct x86emu *emu)
{
	int fetched;

	fetched = fetch_byte_imm(emu);
	emu->cur_mod = (fetched >> 6) & 0x03;
	emu->cur_rh = (fetched >> 3) & 0x07;
	emu->cur_rl = (fetched >> 0) & 0x07;
}

/*
 * RETURNS:
 * Immediate byte value read from instruction queue
 * 
 * REMARKS:
 * This function returns the immediate byte from the instruction queue, and
 * moves the instruction pointer to the next value.
 * 
 * NOTE: Do not inline this function, as (*emu->emu_rdb) is already inline!
 */
static uint8_t 
fetch_byte_imm(struct x86emu *emu)
{
	uint8_t fetched;

	fetched = fetch_byte(emu, emu->x86.R_CS, emu->x86.R_IP);
	emu->x86.R_IP++;
	return fetched;
}

/*
 * RETURNS:
 * Immediate word value read from instruction queue
 * 
 * REMARKS:
 * This function returns the immediate byte from the instruction queue, and
 * moves the instruction pointer to the next value.
 * 
 * NOTE: Do not inline this function, as (*emu->emu_rdw) is already inline!
 */
static uint16_t 
fetch_word_imm(struct x86emu *emu)
{
	uint16_t fetched;

	fetched = fetch_word(emu, emu->x86.R_CS, emu->x86.R_IP);
	emu->x86.R_IP += 2;
	return fetched;
}

/*
 * RETURNS:
 * Immediate lone value read from instruction queue
 * 
 * REMARKS:
 * This function returns the immediate byte from the instruction queue, and
 * moves the instruction pointer to the next value.
 * 
 * NOTE: Do not inline this function, as (*emu->emu_rdw) is already inline!
 */
static uint32_t 
fetch_long_imm(struct x86emu *emu)
{
	uint32_t fetched;

	fetched = fetch_long(emu, emu->x86.R_CS, emu->x86.R_IP);
	emu->x86.R_IP += 4;
	return fetched;
}

/*
 * RETURNS:
 * Value of the default data segment
 * 
 * REMARKS:
 * Inline function that returns the default data segment for the current
 * instruction.
 * 
 * On the x86 processor, the default segment is not always DS if there is
 * no segment override. Address modes such as -3[BP] or 10[BP+SI] all refer to
 * addresses relative to SS (ie: on the stack). So, at the minimum, all
 * decodings of addressing modes would have to set/clear a bit describing
 * whether the access is relative to DS or SS.  That is the function of the
 * cpu-state-varible emu->x86.mode. There are several potential states:
 * 
 * 	repe prefix seen  (handled elsewhere)
 * 	repne prefix seen  (ditto)
 * 
 * 	cs segment override
 * 	ds segment override
 * 	es segment override
 * 	fs segment override
 * 	gs segment override
 * 	ss segment override
 * 
 * 	ds/ss select (in absense of override)
 * 
 * Each of the above 7 items are handled with a bit in the mode field.
 */
static uint32_t 
get_data_segment(struct x86emu *emu)
{
	switch (emu->x86.mode & SYSMODE_SEGMASK) {
	case 0:		/* default case: use ds register */
	case SYSMODE_SEGOVR_DS:
	case SYSMODE_SEGOVR_DS | SYSMODE_SEG_DS_SS:
		return emu->x86.R_DS;
	case SYSMODE_SEG_DS_SS:/* non-overridden, use ss register */
		return emu->x86.R_SS;
	case SYSMODE_SEGOVR_CS:
	case SYSMODE_SEGOVR_CS | SYSMODE_SEG_DS_SS:
		return emu->x86.R_CS;
	case SYSMODE_SEGOVR_ES:
	case SYSMODE_SEGOVR_ES | SYSMODE_SEG_DS_SS:
		return emu->x86.R_ES;
	case SYSMODE_SEGOVR_FS:
	case SYSMODE_SEGOVR_FS | SYSMODE_SEG_DS_SS:
		return emu->x86.R_FS;
	case SYSMODE_SEGOVR_GS:
	case SYSMODE_SEGOVR_GS | SYSMODE_SEG_DS_SS:
		return emu->x86.R_GS;
	case SYSMODE_SEGOVR_SS:
	case SYSMODE_SEGOVR_SS | SYSMODE_SEG_DS_SS:
		return emu->x86.R_SS;
	}
	x86emu_halt_sys(emu);
}

/*
 * PARAMETERS:
 * offset	- Offset to load data from
 * 
 * RETURNS:
 * Byte value read from the absolute memory location.
 * 
 * NOTE: Do not inline this function as (*emu->emu_rdX) is already inline!
 */
static uint8_t 
fetch_data_byte(struct x86emu *emu, uint32_t offset)
{
	return fetch_byte(emu, get_data_segment(emu), offset);
}

/*
 * PARAMETERS:
 * offset	- Offset to load data from
 * 
 * RETURNS:
 * Word value read from the absolute memory location.
 * 
 * NOTE: Do not inline this function as (*emu->emu_rdX) is already inline!
 */
static uint16_t 
fetch_data_word(struct x86emu *emu, uint32_t offset)
{
	return fetch_word(emu, get_data_segment(emu), offset);
}

/*
 * PARAMETERS:
 * offset	- Offset to load data from
 * 
 * RETURNS:
 * Long value read from the absolute memory location.
 * 
 * NOTE: Do not inline this function as (*emu->emu_rdX) is already inline!
 */
static uint32_t 
fetch_data_long(struct x86emu *emu, uint32_t offset)
{
	return fetch_long(emu, get_data_segment(emu), offset);
}

/*
 * PARAMETERS:
 * segment	- Segment to load data from
 * offset	- Offset to load data from
 * 
 * RETURNS:
 * Byte value read from the absolute memory location.
 * 
 * NOTE: Do not inline this function as (*emu->emu_rdX) is already inline!
 */
static uint8_t 
fetch_byte(struct x86emu *emu, uint32_t segment, uint32_t offset)
{
	return (*emu->emu_rdb) (emu, ((uint32_t) segment << 4) + offset);
}

/*
 * PARAMETERS:
 * segment	- Segment to load data from
 * offset	- Offset to load data from
 * 
 * RETURNS:
 * Word value read from the absolute memory location.
 * 
 * NOTE: Do not inline this function as (*emu->emu_rdX) is already inline!
 */
static uint16_t 
fetch_word(struct x86emu *emu, uint32_t segment, uint32_t offset)
{
	return (*emu->emu_rdw) (emu, ((uint32_t) segment << 4) + offset);
}

/*
 * PARAMETERS:
 * segment	- Segment to load data from
 * offset	- Offset to load data from
 * 
 * RETURNS:
 * Long value read from the absolute memory location.
 * 
 * NOTE: Do not inline this function as (*emu->emu_rdX) is already inline!
 */
static uint32_t 
fetch_long(struct x86emu *emu, uint32_t segment, uint32_t offset)
{
	return (*emu->emu_rdl) (emu, ((uint32_t) segment << 4) + offset);
}

/*
 * PARAMETERS:
 * offset	- Offset to store data at
 * val		- Value to store
 * 
 * REMARKS:
 * Writes a word value to an segmented memory location. The segment used is
 * the current 'default' segment, which may have been overridden.
 * 
 * NOTE: Do not inline this function as (*emu->emu_wrX) is already inline!
 */
static void 
store_data_byte(struct x86emu *emu, uint32_t offset, uint8_t val)
{
	store_byte(emu, get_data_segment(emu), offset, val);
}

/*
 * PARAMETERS:
 * offset	- Offset to store data at
 * val		- Value to store
 * 
 * REMARKS:
 * Writes a word value to an segmented memory location. The segment used is
 * the current 'default' segment, which may have been overridden.
 * 
 * NOTE: Do not inline this function as (*emu->emu_wrX) is already inline!
 */
static void 
store_data_word(struct x86emu *emu, uint32_t offset, uint16_t val)
{
	store_word(emu, get_data_segment(emu), offset, val);
}

/*
 * PARAMETERS:
 * offset	- Offset to store data at
 * val		- Value to store
 * 
 * REMARKS:
 * Writes a long value to an segmented memory location. The segment used is
 * the current 'default' segment, which may have been overridden.
 * 
 * NOTE: Do not inline this function as (*emu->emu_wrX) is already inline!
 */
static void 
store_data_long(struct x86emu *emu, uint32_t offset, uint32_t val)
{
	store_long(emu, get_data_segment(emu), offset, val);
}

/*
 * PARAMETERS:
 * segment	- Segment to store data at
 * offset	- Offset to store data at
 * val		- Value to store
 * 
 * REMARKS:
 * Writes a byte value to an absolute memory location.
 * 
 * NOTE: Do not inline this function as (*emu->emu_wrX) is already inline!
 */
static void 
store_byte(struct x86emu *emu, uint32_t segment, uint32_t offset, uint8_t val)
{
	(*emu->emu_wrb) (emu, ((uint32_t) segment << 4) + offset, val);
}

/*
 * PARAMETERS:
 * segment	- Segment to store data at
 * offset	- Offset to store data at
 * val		- Value to store
 * 
 * REMARKS:
 * Writes a word value to an absolute memory location.
 * 
 * NOTE: Do not inline this function as (*emu->emu_wrX) is already inline!
 */
static void 
store_word(struct x86emu *emu, uint32_t segment, uint32_t offset, uint16_t val)
{
	(*emu->emu_wrw) (emu, ((uint32_t) segment << 4) + offset, val);
}

/*
 * PARAMETERS:
 * segment	- Segment to store data at
 * offset	- Offset to store data at
 * val		- Value to store
 * 
 * REMARKS:
 * Writes a long value to an absolute memory location.
 * 
 * NOTE: Do not inline this function as (*emu->emu_wrX) is already inline!
 */
static void 
store_long(struct x86emu *emu, uint32_t segment, uint32_t offset, uint32_t val)
{
	(*emu->emu_wrl) (emu, ((uint32_t) segment << 4) + offset, val);
}

/*
 * PARAMETERS:
 * reg	- Register to decode
 * 
 * RETURNS:
 * Pointer to the appropriate register
 * 
 * REMARKS:
 * Return a pointer to the register given by the R/RM field of the
 * modrm byte, for byte operands. Also enables the decoding of instructions.
 */
static uint8_t *
decode_rm_byte_register(struct x86emu *emu, int reg)
{
	switch (reg) {
	case 0:
		return &emu->x86.R_AL;
	case 1:
		return &emu->x86.R_CL;
	case 2:
		return &emu->x86.R_DL;
	case 3:
		return &emu->x86.R_BL;
	case 4:
		return &emu->x86.R_AH;
	case 5:
		return &emu->x86.R_CH;
	case 6:
		return &emu->x86.R_DH;
	case 7:
		return &emu->x86.R_BH;
	default:
		x86emu_halt_sys(emu);
	}
}

static uint8_t *
decode_rl_byte_register(struct x86emu *emu)
{
	return decode_rm_byte_register(emu, emu->cur_rl);
}

static uint8_t *
decode_rh_byte_register(struct x86emu *emu)
{
	return decode_rm_byte_register(emu, emu->cur_rh);
}

/*
 * PARAMETERS:
 * reg	- Register to decode
 * 
 * RETURNS:
 * Pointer to the appropriate register
 * 
 * REMARKS:
 * Return a pointer to the register given by the R/RM field of the
 * modrm byte, for word operands.  Also enables the decoding of instructions.
 */
static uint16_t *
decode_rm_word_register(struct x86emu *emu, int reg)
{
	switch (reg) {
	case 0:
		return &emu->x86.R_AX;
	case 1:
		return &emu->x86.R_CX;
	case 2:
		return &emu->x86.R_DX;
	case 3:
		return &emu->x86.R_BX;
	case 4:
		return &emu->x86.R_SP;
	case 5:
		return &emu->x86.R_BP;
	case 6:
		return &emu->x86.R_SI;
	case 7:
		return &emu->x86.R_DI;
	default:
		x86emu_halt_sys(emu);
	}
}

static uint16_t *
decode_rl_word_register(struct x86emu *emu)
{
	return decode_rm_word_register(emu, emu->cur_rl);
}

static uint16_t *
decode_rh_word_register(struct x86emu *emu)
{
	return decode_rm_word_register(emu, emu->cur_rh);
}

/*
 * PARAMETERS:
 * reg	- Register to decode
 * 
 * RETURNS:
 * Pointer to the appropriate register
 * 
 * REMARKS:
 * Return a pointer to the register given by the R/RM field of the
 * modrm byte, for dword operands.  Also enables the decoding of instructions.
 */
static uint32_t *
decode_rm_long_register(struct x86emu *emu, int reg)
{
	switch (reg) {
	case 0:
		return &emu->x86.R_EAX;
	case 1:
		return &emu->x86.R_ECX;
	case 2:
		return &emu->x86.R_EDX;
	case 3:
		return &emu->x86.R_EBX;
	case 4:
		return &emu->x86.R_ESP;
	case 5:
		return &emu->x86.R_EBP;
	case 6:
		return &emu->x86.R_ESI;
	case 7:
		return &emu->x86.R_EDI;
	default:
		x86emu_halt_sys(emu);
	}
}

static uint32_t *
decode_rl_long_register(struct x86emu *emu)
{
	return decode_rm_long_register(emu, emu->cur_rl);
}

static uint32_t *
decode_rh_long_register(struct x86emu *emu)
{
	return decode_rm_long_register(emu, emu->cur_rh);
}


/*
 * PARAMETERS:
 * reg	- Register to decode
 * 
 * RETURNS:
 * Pointer to the appropriate register
 * 
 * REMARKS:
 * Return a pointer to the register given by the R/RM field of the
 * modrm byte, for word operands, modified from above for the weirdo
 * special case of segreg operands.  Also enables the decoding of instructions.
 */
static uint16_t *
decode_rh_seg_register(struct x86emu *emu)
{
	switch (emu->cur_rh) {
	case 0:
		return &emu->x86.R_ES;
	case 1:
		return &emu->x86.R_CS;
	case 2:
		return &emu->x86.R_SS;
	case 3:
		return &emu->x86.R_DS;
	case 4:
		return &emu->x86.R_FS;
	case 5:
		return &emu->x86.R_GS;
	default:
		x86emu_halt_sys(emu);
	}
}

/*
 * Return offset from the SIB Byte.
 */
static uint32_t 
decode_sib_address(struct x86emu *emu, int sib, int mod)
{
	uint32_t base = 0, i = 0, scale = 1;

	switch (sib & 0x07) {
	case 0:
		base = emu->x86.R_EAX;
		break;
	case 1:
		base = emu->x86.R_ECX;

		break;
	case 2:
		base = emu->x86.R_EDX;
		break;
	case 3:
		base = emu->x86.R_EBX;
		break;
	case 4:
		base = emu->x86.R_ESP;
		emu->x86.mode |= SYSMODE_SEG_DS_SS;
		break;
	case 5:
		if (mod == 0) {
			base = fetch_long_imm(emu);
		} else {
			base = emu->x86.R_EBP;
			emu->x86.mode |= SYSMODE_SEG_DS_SS;
		}
		break;
	case 6:
		base = emu->x86.R_ESI;
		break;
	case 7:
		base = emu->x86.R_EDI;
		break;
	}
	switch ((sib >> 3) & 0x07) {
	case 0:
		i = emu->x86.R_EAX;
		break;
	case 1:
		i = emu->x86.R_ECX;
		break;
	case 2:
		i = emu->x86.R_EDX;
		break;
	case 3:
		i = emu->x86.R_EBX;
		break;
	case 4:
		i = 0;
		break;
	case 5:
		i = emu->x86.R_EBP;
		break;
	case 6:
		i = emu->x86.R_ESI;
		break;
	case 7:
		i = emu->x86.R_EDI;
		break;
	}
	scale = 1 << ((sib >> 6) & 0x03);
	return base + (i * scale);
}

/*
 * PARAMETERS:
 * rm	- RM value to decode
 * 
 * RETURNS:
 * Offset in memory for the address decoding
 * 
 * REMARKS:
 * Return the offset given by mod=00, mod=01 or mod=10 addressing.
 * Also enables the decoding of instructions.
 */
static uint32_t
decode_rl_address(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_ADDR) {
		uint32_t offset, sib;
		/* 32-bit addressing */
		switch (emu->cur_rl) {
		case 0:
			offset = emu->x86.R_EAX;
			break;
		case 1:
			offset = emu->x86.R_ECX;
			break;
		case 2:
			offset = emu->x86.R_EDX;
			break;
		case 3:
			offset = emu->x86.R_EBX;
			break;
		case 4:
			sib = fetch_byte_imm(emu);
			offset = decode_sib_address(emu, sib, 0);
			break;
		case 5:
			if (emu->cur_mod == 0) {
				offset = fetch_long_imm(emu);
			} else {
				emu->x86.mode |= SYSMODE_SEG_DS_SS;
				offset = emu->x86.R_EBP;
			}
			break;
		case 6:
			offset = emu->x86.R_ESI;
			break;
		case 7:
			offset = emu->x86.R_EDI;
			break;
		default:
			x86emu_halt_sys(emu);
		}
		if (emu->cur_mod == 1)
			offset += (int8_t)fetch_byte_imm(emu);
		else if (emu->cur_mod == 2)
			offset += fetch_long_imm(emu);
		return offset;
	} else {
		uint16_t offset;

		/* 16-bit addressing */
		switch (emu->cur_rl) {
		case 0:
			offset = emu->x86.R_BX + emu->x86.R_SI;
			break;
		case 1:
			offset = emu->x86.R_BX + emu->x86.R_DI;
			break;
		case 2:
			emu->x86.mode |= SYSMODE_SEG_DS_SS;
			offset = emu->x86.R_BP + emu->x86.R_SI;
			break;
		case 3:
			emu->x86.mode |= SYSMODE_SEG_DS_SS;
			offset = emu->x86.R_BP + emu->x86.R_DI;
			break;
		case 4:
			offset = emu->x86.R_SI;
			break;
		case 5:
			offset = emu->x86.R_DI;
			break;
		case 6:
			if (emu->cur_mod == 0) {
				offset = fetch_word_imm(emu);
			} else {
				emu->x86.mode |= SYSMODE_SEG_DS_SS;
				offset = emu->x86.R_BP;
			}
			break;
		case 7:
			offset = emu->x86.R_BX;
			break;
		default:
			x86emu_halt_sys(emu);
		}
		if (emu->cur_mod == 1)
			offset += (int8_t)fetch_byte_imm(emu);
		else if (emu->cur_mod == 2)
			offset += fetch_word_imm(emu);
		return offset;
	}
}

static uint8_t
decode_and_fetch_byte(struct x86emu *emu)
{
	if (emu->cur_mod != 3) {
		emu->cur_offset = decode_rl_address(emu);
		return fetch_data_byte(emu, emu->cur_offset);
	} else {
		return *decode_rl_byte_register(emu);
	}
}

static uint16_t
decode_and_fetch_word_disp(struct x86emu *emu, int16_t disp)
{
	if (emu->cur_mod != 3) {
		/* TODO: A20 gate emulation */
		emu->cur_offset = decode_rl_address(emu) + disp;
		if ((emu->x86.mode & SYSMODE_PREFIX_ADDR) == 0)
			emu->cur_offset &= 0xffff;
		return fetch_data_word(emu, emu->cur_offset);
	} else {
		return *decode_rl_word_register(emu);
	}
}

static uint32_t
decode_and_fetch_long_disp(struct x86emu *emu, int16_t disp)
{
	if (emu->cur_mod != 3) {
		/* TODO: A20 gate emulation */
		emu->cur_offset = decode_rl_address(emu) + disp;
		if ((emu->x86.mode & SYSMODE_PREFIX_ADDR) == 0)
			emu->cur_offset &= 0xffff;
		return fetch_data_long(emu, emu->cur_offset);
	} else {
		return *decode_rl_long_register(emu);
	}
}

uint16_t
decode_and_fetch_word(struct x86emu *emu)
{
	return decode_and_fetch_word_disp(emu, 0);
}

uint32_t
decode_and_fetch_long(struct x86emu *emu)
{
	return decode_and_fetch_long_disp(emu, 0);
}

uint8_t
decode_and_fetch_byte_imm8(struct x86emu *emu, uint8_t *imm)
{
	if (emu->cur_mod != 3) {
		emu->cur_offset = decode_rl_address(emu);
		*imm = fetch_byte_imm(emu);
		return fetch_data_byte(emu, emu->cur_offset);
	} else {
		*imm = fetch_byte_imm(emu);
		return *decode_rl_byte_register(emu);
	}
}

static uint16_t
decode_and_fetch_word_imm8(struct x86emu *emu, uint8_t *imm)
{
	if (emu->cur_mod != 3) {
		emu->cur_offset = decode_rl_address(emu);
		*imm = fetch_byte_imm(emu);
		return fetch_data_word(emu, emu->cur_offset);
	} else {
		*imm = fetch_byte_imm(emu);
		return *decode_rl_word_register(emu);
	}
}

static uint32_t
decode_and_fetch_long_imm8(struct x86emu *emu, uint8_t *imm)
{
	if (emu->cur_mod != 3) {
		emu->cur_offset = decode_rl_address(emu);
		*imm = fetch_byte_imm(emu);
		return fetch_data_long(emu, emu->cur_offset);
	} else {
		*imm = fetch_byte_imm(emu);
		return *decode_rl_long_register(emu);
	}
}

static void
write_back_byte(struct x86emu *emu, uint8_t val)
{
	if (emu->cur_mod != 3)
		store_data_byte(emu, emu->cur_offset, val);
	else
		*decode_rl_byte_register(emu) = val;
}

static void
write_back_word(struct x86emu *emu, uint16_t val)
{
	if (emu->cur_mod != 3)
		store_data_word(emu, emu->cur_offset, val);
	else
		*decode_rl_word_register(emu) = val;
}

static void
write_back_long(struct x86emu *emu, uint32_t val)
{
	if (emu->cur_mod != 3)
		store_data_long(emu, emu->cur_offset, val);
	else
		*decode_rl_long_register(emu) = val;
}

static void
common_inc_word_long(struct x86emu *emu, union x86emu_register *reg)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		reg->I32_reg.e_reg = inc_long(emu, reg->I32_reg.e_reg);
	else
		reg->I16_reg.x_reg = inc_word(emu, reg->I16_reg.x_reg);
}

static void
common_dec_word_long(struct x86emu *emu, union x86emu_register *reg)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		reg->I32_reg.e_reg = dec_long(emu, reg->I32_reg.e_reg);
	else
		reg->I16_reg.x_reg = dec_word(emu, reg->I16_reg.x_reg);
}

static void
common_binop_byte_rm_r(struct x86emu *emu, 
    uint8_t (*binop)(struct x86emu *, uint8_t, uint8_t))
{
	uint32_t destoffset;
	uint8_t *destreg, srcval;
	uint8_t destval;

	fetch_decode_modrm(emu);
	srcval = *decode_rh_byte_register(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = fetch_data_byte(emu, destoffset);
		destval = (*binop)(emu, destval, srcval);
		store_data_byte(emu, destoffset, destval);
	} else {
		destreg = decode_rl_byte_register(emu);
		*destreg = (*binop)(emu, *destreg, srcval);
	}
}

static void
common_binop_ns_byte_rm_r(struct x86emu *emu, 
    void (*binop)(struct x86emu *, uint8_t, uint8_t))
{
	uint32_t destoffset;
	uint8_t destval, srcval;

	fetch_decode_modrm(emu);
	srcval = *decode_rh_byte_register(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = fetch_data_byte(emu, destoffset);
	} else {
		destval = *decode_rl_byte_register(emu);
	}
	(*binop)(emu, destval, srcval);
}

static void
common_binop_word_rm_r(struct x86emu *emu,
    uint16_t (*binop)(struct x86emu *, uint16_t, uint16_t))
{
	uint32_t destoffset;
	uint16_t destval, *destreg, srcval;

	fetch_decode_modrm(emu);
	srcval = *decode_rh_word_register(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = fetch_data_word(emu, destoffset);
		destval = (*binop)(emu, destval, srcval);
		store_data_word(emu, destoffset, destval);
	} else {
		destreg = decode_rl_word_register(emu);
		*destreg = (*binop)(emu, *destreg, srcval);
	}
}

static void
common_binop_byte_r_rm(struct x86emu *emu,
    uint8_t (*binop)(struct x86emu *, uint8_t, uint8_t))
{
	uint8_t *destreg, srcval;
	uint32_t srcoffset;

	fetch_decode_modrm(emu);
	destreg = decode_rh_byte_register(emu);
	if (emu->cur_mod != 3) {
		srcoffset = decode_rl_address(emu);
		srcval = fetch_data_byte(emu, srcoffset);
	} else {
		srcval = *decode_rl_byte_register(emu);
	}
	*destreg = (*binop)(emu, *destreg, srcval);
}

static void
common_binop_long_rm_r(struct x86emu *emu,
    uint32_t (*binop)(struct x86emu *, uint32_t, uint32_t))
{
	uint32_t destoffset;
	uint32_t destval, *destreg, srcval;

	fetch_decode_modrm(emu);
	srcval = *decode_rh_long_register(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = fetch_data_long(emu, destoffset);
		destval = (*binop)(emu, destval, srcval);
		store_data_long(emu, destoffset, destval);
	} else {
		destreg = decode_rl_long_register(emu);
		*destreg = (*binop)(emu, *destreg, srcval);
	}
}

static void
common_binop_word_long_rm_r(struct x86emu *emu,
    uint16_t (*binop16)(struct x86emu *, uint16_t, uint16_t),
    uint32_t (*binop32)(struct x86emu *, uint32_t, uint32_t))
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		common_binop_long_rm_r(emu, binop32);
	else
		common_binop_word_rm_r(emu, binop16);
}

static void
common_binop_ns_word_rm_r(struct x86emu *emu,
    void (*binop)(struct x86emu *, uint16_t, uint16_t))
{
	uint32_t destoffset;
	uint16_t destval, srcval;

	fetch_decode_modrm(emu);
	srcval = *decode_rh_word_register(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = fetch_data_word(emu, destoffset);
	} else {
		destval = *decode_rl_word_register(emu);
	}
	(*binop)(emu, destval, srcval);
}


static void
common_binop_ns_long_rm_r(struct x86emu *emu,
    void (*binop)(struct x86emu *, uint32_t, uint32_t))
{
	uint32_t destoffset;
	uint32_t destval, srcval;

	fetch_decode_modrm(emu);
	srcval = *decode_rh_long_register(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = fetch_data_long(emu, destoffset);
	} else {
		destval = *decode_rl_long_register(emu);
	}
	(*binop)(emu, destval, srcval);
}

static void
common_binop_ns_word_long_rm_r(struct x86emu *emu,
    void (*binop16)(struct x86emu *, uint16_t, uint16_t),
    void (*binop32)(struct x86emu *, uint32_t, uint32_t))
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		common_binop_ns_long_rm_r(emu, binop32);
	else
		common_binop_ns_word_rm_r(emu, binop16);
}

static void
common_binop_long_r_rm(struct x86emu *emu,
    uint32_t (*binop)(struct x86emu *, uint32_t, uint32_t))
{
	uint32_t srcoffset;
	uint32_t *destreg, srcval;

	fetch_decode_modrm(emu);
	destreg = decode_rh_long_register(emu);
	if (emu->cur_mod != 3) {
		srcoffset = decode_rl_address(emu);
		srcval = fetch_data_long(emu, srcoffset);
	} else {
		srcval = *decode_rl_long_register(emu);
	}
	*destreg = (*binop)(emu, *destreg, srcval);
}

static void
common_binop_word_r_rm(struct x86emu *emu,
    uint16_t (*binop)(struct x86emu *, uint16_t, uint16_t))
{
	uint32_t srcoffset;
	uint16_t *destreg, srcval;

	fetch_decode_modrm(emu);
	destreg = decode_rh_word_register(emu);
	if (emu->cur_mod != 3) {
		srcoffset = decode_rl_address(emu);
		srcval = fetch_data_word(emu, srcoffset);
	} else {
		srcval = *decode_rl_word_register(emu);
	}
	*destreg = (*binop)(emu, *destreg, srcval);
}

static void
common_binop_word_long_r_rm(struct x86emu *emu,
    uint16_t (*binop16)(struct x86emu *, uint16_t, uint16_t),
    uint32_t (*binop32)(struct x86emu *, uint32_t, uint32_t))
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		common_binop_long_r_rm(emu, binop32);
	else
		common_binop_word_r_rm(emu, binop16);
}

static void
common_binop_byte_imm(struct x86emu *emu,
    uint8_t (*binop)(struct x86emu *, uint8_t, uint8_t))
{
	uint8_t srcval;

	srcval = fetch_byte_imm(emu);
	emu->x86.R_AL = (*binop)(emu, emu->x86.R_AL, srcval);
}

static void
common_binop_word_long_imm(struct x86emu *emu,
    uint16_t (*binop16)(struct x86emu *, uint16_t, uint16_t),
    uint32_t (*binop32)(struct x86emu *, uint32_t, uint32_t))
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		uint32_t srcval;

		srcval = fetch_long_imm(emu);
		emu->x86.R_EAX = (*binop32)(emu, emu->x86.R_EAX, srcval);
	} else {
		uint16_t srcval;

		srcval = fetch_word_imm(emu);
		emu->x86.R_AX = (*binop16)(emu, emu->x86.R_AX, srcval);
	}
}

static void
common_push_word_long(struct x86emu *emu, union x86emu_register *reg)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		push_long(emu, reg->I32_reg.e_reg);
	else
		push_word(emu, reg->I16_reg.x_reg);
}

static void
common_pop_word_long(struct x86emu *emu, union x86emu_register *reg)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		reg->I32_reg.e_reg = pop_long(emu);
	else
		reg->I16_reg.x_reg = pop_word(emu);
}

static void
common_imul_long_IMM(struct x86emu *emu, int byte_imm)
{
	uint32_t srcoffset;
	uint32_t *destreg, srcval;
	int32_t imm;
	uint64_t res;

	fetch_decode_modrm(emu);
	destreg = decode_rh_long_register(emu);
	if (emu->cur_mod != 3) {
		srcoffset = decode_rl_address(emu);
		srcval = fetch_data_long(emu, srcoffset);
	} else {
		srcval = *decode_rl_long_register(emu);
	}

	if (byte_imm)
		imm = (int8_t)fetch_byte_imm(emu);
	else
		imm = fetch_long_imm(emu);
	res = (int32_t)srcval * imm;

	if (res > 0xffffffff) {
		SET_FLAG(F_CF);
		SET_FLAG(F_OF);
	} else {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
	}
	*destreg = (uint32_t)res;
}

static void
common_imul_word_IMM(struct x86emu *emu, int byte_imm)
{
	uint32_t srcoffset;
	uint16_t *destreg, srcval;
	int16_t imm;
	uint32_t res;

	fetch_decode_modrm(emu);
	destreg = decode_rh_word_register(emu);
	if (emu->cur_mod != 3) {
		srcoffset = decode_rl_address(emu);
		srcval = fetch_data_word(emu, srcoffset);
	} else {
		srcval = *decode_rl_word_register(emu);
	}

	if (byte_imm)
		imm = (int8_t)fetch_byte_imm(emu);
	else
		imm = fetch_word_imm(emu);
	res = (int16_t)srcval * imm;

	if (res > 0xffff) {
		SET_FLAG(F_CF);
		SET_FLAG(F_OF);
	} else {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
	}
	*destreg = (uint16_t) res;
}

static void
common_imul_imm(struct x86emu *emu, int byte_imm)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		common_imul_long_IMM(emu, byte_imm);
	else
		common_imul_word_IMM(emu, byte_imm);
}

static void
common_jmp_near(struct x86emu *emu, int cond)
{
	int8_t offset;
	uint16_t target;

	offset = (int8_t) fetch_byte_imm(emu);
	target = (uint16_t) (emu->x86.R_IP + (int16_t) offset);
	if (cond)
		emu->x86.R_IP = target;
}

static void
common_load_far_pointer(struct x86emu *emu, uint16_t *seg)
{
	uint16_t *dstreg;
	uint32_t srcoffset;

	fetch_decode_modrm(emu);
	if (emu->cur_mod == 3)
		x86emu_halt_sys(emu);

	dstreg = decode_rh_word_register(emu);
	srcoffset = decode_rl_address(emu);
	*dstreg = fetch_data_word(emu, srcoffset);
	*seg = fetch_data_word(emu, srcoffset + 2);
}

/* Implementation */

/*
 * REMARKS:
 * Handles opcode 0x3a
 */
static void
x86emuOp_cmp_byte_R_RM(struct x86emu *emu)
{
	uint8_t *destreg, srcval;

	fetch_decode_modrm(emu);
	destreg = decode_rh_byte_register(emu);
	srcval = decode_and_fetch_byte(emu);
	cmp_byte(emu, *destreg, srcval);
}

/*
 * REMARKS:
 * 
 * Handles opcode 0x3b
 */
static void
x86emuOp32_cmp_word_R_RM(struct x86emu *emu)
{
	uint32_t srcval, *destreg;

	fetch_decode_modrm(emu);
	destreg = decode_rh_long_register(emu);
	srcval = decode_and_fetch_long(emu);
	cmp_long(emu, *destreg, srcval);
}

static void
x86emuOp16_cmp_word_R_RM(struct x86emu *emu)
{
	uint16_t srcval, *destreg;

	fetch_decode_modrm(emu);
	destreg = decode_rh_word_register(emu);
	srcval = decode_and_fetch_word(emu);
	cmp_word(emu, *destreg, srcval);
}

static void
x86emuOp_cmp_word_R_RM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp32_cmp_word_R_RM(emu);
	else
		x86emuOp16_cmp_word_R_RM(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x3c
 */
static void
x86emuOp_cmp_byte_AL_IMM(struct x86emu *emu)
{
	uint8_t srcval;

	srcval = fetch_byte_imm(emu);
	cmp_byte(emu, emu->x86.R_AL, srcval);
}

/*
 * REMARKS:
 * Handles opcode 0x3d
 */
static void
x86emuOp32_cmp_word_AX_IMM(struct x86emu *emu)
{
	uint32_t srcval;

	srcval = fetch_long_imm(emu);
	cmp_long(emu, emu->x86.R_EAX, srcval);
}

static void
x86emuOp16_cmp_word_AX_IMM(struct x86emu *emu)
{
	uint16_t srcval;

	srcval = fetch_word_imm(emu);
	cmp_word(emu, emu->x86.R_AX, srcval);
}

static void
x86emuOp_cmp_word_AX_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp32_cmp_word_AX_IMM(emu);
	else
		x86emuOp16_cmp_word_AX_IMM(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x60
 */
static void
x86emuOp_push_all(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		uint32_t old_sp = emu->x86.R_ESP;

		push_long(emu, emu->x86.R_EAX);
		push_long(emu, emu->x86.R_ECX);
		push_long(emu, emu->x86.R_EDX);
		push_long(emu, emu->x86.R_EBX);
		push_long(emu, old_sp);
		push_long(emu, emu->x86.R_EBP);
		push_long(emu, emu->x86.R_ESI);
		push_long(emu, emu->x86.R_EDI);
	} else {
		uint16_t old_sp = emu->x86.R_SP;

		push_word(emu, emu->x86.R_AX);
		push_word(emu, emu->x86.R_CX);
		push_word(emu, emu->x86.R_DX);
		push_word(emu, emu->x86.R_BX);
		push_word(emu, old_sp);
		push_word(emu, emu->x86.R_BP);
		push_word(emu, emu->x86.R_SI);
		push_word(emu, emu->x86.R_DI);
	}
}

/*
 * REMARKS:
 * Handles opcode 0x61
 */
static void
x86emuOp_pop_all(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		emu->x86.R_EDI = pop_long(emu);
		emu->x86.R_ESI = pop_long(emu);
		emu->x86.R_EBP = pop_long(emu);
		emu->x86.R_ESP += 4;	/* skip ESP */
		emu->x86.R_EBX = pop_long(emu);
		emu->x86.R_EDX = pop_long(emu);
		emu->x86.R_ECX = pop_long(emu);
		emu->x86.R_EAX = pop_long(emu);
	} else {
		emu->x86.R_DI = pop_word(emu);
		emu->x86.R_SI = pop_word(emu);
		emu->x86.R_BP = pop_word(emu);
		emu->x86.R_SP += 2;/* skip SP */
		emu->x86.R_BX = pop_word(emu);
		emu->x86.R_DX = pop_word(emu);
		emu->x86.R_CX = pop_word(emu);
		emu->x86.R_AX = pop_word(emu);
	}
}
/*opcode 0x62   ILLEGAL OP, calls x86emuOp_illegal_op() */
/*opcode 0x63   ILLEGAL OP, calls x86emuOp_illegal_op() */


/*
 * REMARKS:
 * Handles opcode 0x68
 */
static void
x86emuOp_push_word_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		uint32_t imm;

		imm = fetch_long_imm(emu);
		push_long(emu, imm);
	} else {
		uint16_t imm;

		imm = fetch_word_imm(emu);
		push_word(emu, imm);
	}
}

/*
 * REMARKS:
 * Handles opcode 0x6a
 */
static void
x86emuOp_push_byte_IMM(struct x86emu *emu)
{
	int16_t imm;

	imm = (int8_t) fetch_byte_imm(emu);
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		push_long(emu, (int32_t) imm);
	} else {
		push_word(emu, imm);
	}
}

/*
 * REMARKS:
 * Handles opcode 0x6c and 0x6d
 */
static void
x86emuOp_ins_word(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		ins(emu, 4);
	} else {
		ins(emu, 2);
	}
}

/*
 * REMARKS:
 * Handles opcode 0x6f
 */
static void
x86emuOp_outs_word(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		outs(emu, 4);
	} else {
		outs(emu, 2);
	}
}

/*
 * REMARKS:
 * Handles opcode 0x7c
 */
static void
x86emuOp_jump_near_L(struct x86emu *emu)
{
	int sf, of;

	sf = ACCESS_FLAG(F_SF) != 0;
	of = ACCESS_FLAG(F_OF) != 0;

	common_jmp_near(emu, sf != of);
}

/*
 * REMARKS:
 * Handles opcode 0x7d
 */
static void
x86emuOp_jump_near_NL(struct x86emu *emu)
{
	int sf, of;

	sf = ACCESS_FLAG(F_SF) != 0;
	of = ACCESS_FLAG(F_OF) != 0;

	common_jmp_near(emu, sf == of);
}

/*
 * REMARKS:
 * Handles opcode 0x7e
 */
static void
x86emuOp_jump_near_LE(struct x86emu *emu)
{
	int sf, of;

	sf = ACCESS_FLAG(F_SF) != 0;
	of = ACCESS_FLAG(F_OF) != 0;

	common_jmp_near(emu, sf != of || ACCESS_FLAG(F_ZF));
}

/*
 * REMARKS:
 * Handles opcode 0x7f
 */
static void
x86emuOp_jump_near_NLE(struct x86emu *emu)
{
	int sf, of;

	sf = ACCESS_FLAG(F_SF) != 0;
	of = ACCESS_FLAG(F_OF) != 0;

	common_jmp_near(emu, sf == of && !ACCESS_FLAG(F_ZF));
}

static
uint8_t(*const opc80_byte_operation[]) (struct x86emu *, uint8_t d, uint8_t s) =
{
	add_byte,		/* 00 */
	or_byte,		/* 01 */
	adc_byte,		/* 02 */
	sbb_byte,		/* 03 */
	and_byte,		/* 04 */
	sub_byte,		/* 05 */
	xor_byte,		/* 06 */
	cmp_byte,		/* 07 */
};

/*
 * REMARKS:
 * Handles opcode 0x80
 */
static void
x86emuOp_opc80_byte_RM_IMM(struct x86emu *emu)
{
	uint8_t imm, destval;

	/*
         * Weirdo special case instruction format.  Part of the opcode
         * held below in "RH".  Doubly nested case would result, except
         * that the decoded instruction
         */
	fetch_decode_modrm(emu);
	destval = decode_and_fetch_byte(emu);
	imm = fetch_byte_imm(emu);
	destval = (*opc80_byte_operation[emu->cur_rh]) (emu, destval, imm);
	if (emu->cur_rh != 7)
		write_back_byte(emu, destval);
}

static
uint16_t(* const opc81_word_operation[])
    (struct x86emu *, uint16_t d, uint16_t s) =
{
	add_word,		/* 00 */
	or_word,		/* 01 */
	adc_word,		/* 02 */
	sbb_word,		/* 03 */
	and_word,		/* 04 */
	sub_word,		/* 05 */
	xor_word,		/* 06 */
	cmp_word,		/* 07 */
};

static
uint32_t(* const opc81_long_operation[])
    (struct x86emu *, uint32_t d, uint32_t s) =
{
	add_long,		/* 00 */
	or_long,		/* 01 */
	adc_long,		/* 02 */
	sbb_long,		/* 03 */
	and_long,		/* 04 */
	sub_long,		/* 05 */
	xor_long,		/* 06 */
	cmp_long,		/* 07 */
};

/*
 * REMARKS:
 * Handles opcode 0x81
 */
static void
x86emuOp32_opc81_word_RM_IMM(struct x86emu *emu)
{
	uint32_t destval, imm;

	/*
         * Weirdo special case instruction format.  Part of the opcode
         * held below in "RH".  Doubly nested case would result, except
         * that the decoded instruction
         */
	fetch_decode_modrm(emu);
	destval = decode_and_fetch_long(emu);
	imm = fetch_long_imm(emu);
	destval = (*opc81_long_operation[emu->cur_rh]) (emu, destval, imm);
	if (emu->cur_rh != 7)
		write_back_long(emu, destval);
}

static void
x86emuOp16_opc81_word_RM_IMM(struct x86emu *emu)
{
	uint16_t destval, imm;

	/*
         * Weirdo special case instruction format.  Part of the opcode
         * held below in "RH".  Doubly nested case would result, except
         * that the decoded instruction
         */
	fetch_decode_modrm(emu);
	destval = decode_and_fetch_word(emu);
	imm = fetch_word_imm(emu);
	destval = (*opc81_word_operation[emu->cur_rh]) (emu, destval, imm);
	if (emu->cur_rh != 7)
		write_back_word(emu, destval);
}

static void
x86emuOp_opc81_word_RM_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp32_opc81_word_RM_IMM(emu);
	else
		x86emuOp16_opc81_word_RM_IMM(emu);
}

static
uint8_t(* const opc82_byte_operation[])
    (struct x86emu *, uint8_t s, uint8_t d) =
{
	add_byte,		/* 00 */
	or_byte,		/* 01 *//* YYY UNUSED ???? */
	adc_byte,		/* 02 */
	sbb_byte,		/* 03 */
	and_byte,		/* 04 *//* YYY UNUSED ???? */
	sub_byte,		/* 05 */
	xor_byte,		/* 06 *//* YYY UNUSED ???? */
	cmp_byte,		/* 07 */
};

/*
 * REMARKS:
 * Handles opcode 0x82
 */
static void
x86emuOp_opc82_byte_RM_IMM(struct x86emu *emu)
{
	uint8_t imm, destval;

	/*
         * Weirdo special case instruction format.  Part of the opcode
         * held below in "RH".  Doubly nested case would result, except
         * that the decoded instruction Similar to opcode 81, except that
         * the immediate byte is sign extended to a word length.
         */
	fetch_decode_modrm(emu);
	destval = decode_and_fetch_byte(emu);
	imm = fetch_byte_imm(emu);
	destval = (*opc82_byte_operation[emu->cur_rh]) (emu, destval, imm);
	if (emu->cur_rh != 7)
		write_back_byte(emu, destval);
}

static
uint16_t(* const opc83_word_operation[])
    (struct x86emu *, uint16_t s, uint16_t d) =
{
	add_word,		/* 00 */
	or_word,		/* 01 *//* YYY UNUSED ???? */
	adc_word,		/* 02 */
	sbb_word,		/* 03 */
	and_word,		/* 04 *//* YYY UNUSED ???? */
	sub_word,		/* 05 */
	xor_word,		/* 06 *//* YYY UNUSED ???? */
	cmp_word,		/* 07 */
};

static
uint32_t(* const opc83_long_operation[])
    (struct x86emu *, uint32_t s, uint32_t d) =
{
	add_long,		/* 00 */
	or_long,		/* 01 *//* YYY UNUSED ???? */
	adc_long,		/* 02 */
	sbb_long,		/* 03 */
	and_long,		/* 04 *//* YYY UNUSED ???? */
	sub_long,		/* 05 */
	xor_long,		/* 06 *//* YYY UNUSED ???? */
	cmp_long,		/* 07 */
};

/*
 * REMARKS:
 * Handles opcode 0x83
 */
static void
x86emuOp32_opc83_word_RM_IMM(struct x86emu *emu)
{
	uint32_t destval, imm;

	fetch_decode_modrm(emu);
	destval = decode_and_fetch_long(emu);
	imm = (int8_t) fetch_byte_imm(emu);
	destval = (*opc83_long_operation[emu->cur_rh]) (emu, destval, imm);
	if (emu->cur_rh != 7)
		write_back_long(emu, destval);
}

static void
x86emuOp16_opc83_word_RM_IMM(struct x86emu *emu)
{
	uint16_t destval, imm;

	fetch_decode_modrm(emu);
	destval = decode_and_fetch_word(emu);
	imm = (int8_t) fetch_byte_imm(emu);
	destval = (*opc83_word_operation[emu->cur_rh]) (emu, destval, imm);
	if (emu->cur_rh != 7)
		write_back_word(emu, destval);
}

static void
x86emuOp_opc83_word_RM_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp32_opc83_word_RM_IMM(emu);
	else
		x86emuOp16_opc83_word_RM_IMM(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x86
 */
static void
x86emuOp_xchg_byte_RM_R(struct x86emu *emu)
{
	uint8_t *srcreg, destval, tmp;

	fetch_decode_modrm(emu);
	destval = decode_and_fetch_byte(emu);
	srcreg = decode_rh_byte_register(emu);
	tmp = destval;
	destval = *srcreg;
	*srcreg = tmp;
	write_back_byte(emu, destval);
}

/*
 * REMARKS:
 * Handles opcode 0x87
 */
static void
x86emuOp32_xchg_word_RM_R(struct x86emu *emu)
{
	uint32_t *srcreg, destval, tmp;

	fetch_decode_modrm(emu);
	destval = decode_and_fetch_long(emu);
	srcreg = decode_rh_long_register(emu);
	tmp = destval;
	destval = *srcreg;
	*srcreg = tmp;
	write_back_long(emu, destval);
}

static void
x86emuOp16_xchg_word_RM_R(struct x86emu *emu)
{
	uint16_t *srcreg, destval, tmp;

	fetch_decode_modrm(emu);
	destval = decode_and_fetch_word(emu);
	srcreg = decode_rh_word_register(emu);
	tmp = destval;
	destval = *srcreg;
	*srcreg = tmp;
	write_back_word(emu, destval);
}

static void
x86emuOp_xchg_word_RM_R(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp32_xchg_word_RM_R(emu);
	else
		x86emuOp16_xchg_word_RM_R(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x88
 */
static void
x86emuOp_mov_byte_RM_R(struct x86emu *emu)
{
	uint8_t *destreg, *srcreg;
	uint32_t destoffset;

	fetch_decode_modrm(emu);
	srcreg = decode_rh_byte_register(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		store_data_byte(emu, destoffset, *srcreg);
	} else {
		destreg = decode_rl_byte_register(emu);
		*destreg = *srcreg;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x89
 */
static void
x86emuOp32_mov_word_RM_R(struct x86emu *emu)
{
	uint32_t destoffset;
	uint32_t *destreg, srcval;

	fetch_decode_modrm(emu);
	srcval = *decode_rh_long_register(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		store_data_long(emu, destoffset, srcval);
	} else {
		destreg = decode_rl_long_register(emu);
		*destreg = srcval;
	}
}

static void
x86emuOp16_mov_word_RM_R(struct x86emu *emu)
{
	uint32_t destoffset;
	uint16_t *destreg, srcval;

	fetch_decode_modrm(emu);
	srcval = *decode_rh_word_register(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		store_data_word(emu, destoffset, srcval);
	} else {
		destreg = decode_rl_word_register(emu);
		*destreg = srcval;
	}
}

static void
x86emuOp_mov_word_RM_R(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp32_mov_word_RM_R(emu);
	else
		x86emuOp16_mov_word_RM_R(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x8a
 */
static void
x86emuOp_mov_byte_R_RM(struct x86emu *emu)
{
	uint8_t *destreg;

	fetch_decode_modrm(emu);
	destreg = decode_rh_byte_register(emu);
	*destreg = decode_and_fetch_byte(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x8b
 */
static void
x86emuOp_mov_word_R_RM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		uint32_t *destreg;

		fetch_decode_modrm(emu);
		destreg = decode_rh_long_register(emu);
		*destreg = decode_and_fetch_long(emu);
	} else {
		uint16_t *destreg;

		fetch_decode_modrm(emu);
		destreg = decode_rh_word_register(emu);
		*destreg = decode_and_fetch_word(emu);
	}
}

/*
 * REMARKS:
 * Handles opcode 0x8c
 */
static void
x86emuOp_mov_word_RM_SR(struct x86emu *emu)
{
	uint16_t *destreg, srcval;
	uint32_t destoffset;

	fetch_decode_modrm(emu);
	srcval = *decode_rh_seg_register(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		store_data_word(emu, destoffset, srcval);
	} else {
		destreg = decode_rl_word_register(emu);
		*destreg = srcval;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x8d
 */
static void
x86emuOp_lea_word_R_M(struct x86emu *emu)
{
	uint32_t destoffset;

	fetch_decode_modrm(emu);
	if (emu->cur_mod == 3)
		x86emu_halt_sys(emu);

	destoffset = decode_rl_address(emu);
	if (emu->x86.mode & SYSMODE_PREFIX_ADDR) {
		uint32_t *srcreg;

		srcreg = decode_rh_long_register(emu);
		*srcreg = (uint32_t) destoffset;
	} else {
		uint16_t *srcreg;

		srcreg = decode_rh_word_register(emu);
		*srcreg = (uint16_t) destoffset;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x8e
 */
static void
x86emuOp_mov_word_SR_RM(struct x86emu *emu)
{
	uint16_t *destreg;

	fetch_decode_modrm(emu);
	destreg = decode_rh_seg_register(emu);
	*destreg = decode_and_fetch_word(emu);
	/*
         * Clean up, and reset all the R_xSP pointers to the correct
         * locations.  This is about 3x too much overhead (doing all the
         * segreg ptrs when only one is needed, but this instruction
         * *cannot* be that common, and this isn't too much work anyway.
         */
}

/*
 * REMARKS:
 * Handles opcode 0x8f
 */
static void
x86emuOp32_pop_RM(struct x86emu *emu)
{
	uint32_t destoffset;
	uint32_t destval, *destreg;

	fetch_decode_modrm(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = pop_long(emu);
		store_data_long(emu, destoffset, destval);
	} else {
		destreg = decode_rl_long_register(emu);
		*destreg = pop_long(emu);
	}
}

static void
x86emuOp16_pop_RM(struct x86emu *emu)
{
	uint32_t destoffset;
	uint16_t destval, *destreg;

	fetch_decode_modrm(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = pop_word(emu);
		store_data_word(emu, destoffset, destval);
	} else {
		destreg = decode_rl_word_register(emu);
		*destreg = pop_word(emu);
	}
}

static void
x86emuOp_pop_RM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp32_pop_RM(emu);
	else
		x86emuOp16_pop_RM(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x91
 */
static void
x86emuOp_xchg_word_AX_CX(struct x86emu *emu)
{
	uint32_t tmp;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		tmp = emu->x86.R_EAX;
		emu->x86.R_EAX = emu->x86.R_ECX;
		emu->x86.R_ECX = tmp;
	} else {
		tmp = emu->x86.R_AX;
		emu->x86.R_AX = emu->x86.R_CX;
		emu->x86.R_CX = (uint16_t) tmp;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x92
 */
static void
x86emuOp_xchg_word_AX_DX(struct x86emu *emu)
{
	uint32_t tmp;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		tmp = emu->x86.R_EAX;
		emu->x86.R_EAX = emu->x86.R_EDX;
		emu->x86.R_EDX = tmp;
	} else {
		tmp = emu->x86.R_AX;
		emu->x86.R_AX = emu->x86.R_DX;
		emu->x86.R_DX = (uint16_t) tmp;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x93
 */
static void
x86emuOp_xchg_word_AX_BX(struct x86emu *emu)
{
	uint32_t tmp;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		tmp = emu->x86.R_EAX;
		emu->x86.R_EAX = emu->x86.R_EBX;
		emu->x86.R_EBX = tmp;
	} else {
		tmp = emu->x86.R_AX;
		emu->x86.R_AX = emu->x86.R_BX;
		emu->x86.R_BX = (uint16_t) tmp;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x94
 */
static void
x86emuOp_xchg_word_AX_SP(struct x86emu *emu)
{
	uint32_t tmp;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		tmp = emu->x86.R_EAX;
		emu->x86.R_EAX = emu->x86.R_ESP;
		emu->x86.R_ESP = tmp;
	} else {
		tmp = emu->x86.R_AX;
		emu->x86.R_AX = emu->x86.R_SP;
		emu->x86.R_SP = (uint16_t) tmp;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x95
 */
static void
x86emuOp_xchg_word_AX_BP(struct x86emu *emu)
{
	uint32_t tmp;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		tmp = emu->x86.R_EAX;
		emu->x86.R_EAX = emu->x86.R_EBP;
		emu->x86.R_EBP = tmp;
	} else {
		tmp = emu->x86.R_AX;
		emu->x86.R_AX = emu->x86.R_BP;
		emu->x86.R_BP = (uint16_t) tmp;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x96
 */
static void
x86emuOp_xchg_word_AX_SI(struct x86emu *emu)
{
	uint32_t tmp;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		tmp = emu->x86.R_EAX;
		emu->x86.R_EAX = emu->x86.R_ESI;
		emu->x86.R_ESI = tmp;
	} else {
		tmp = emu->x86.R_AX;
		emu->x86.R_AX = emu->x86.R_SI;
		emu->x86.R_SI = (uint16_t) tmp;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x97
 */
static void
x86emuOp_xchg_word_AX_DI(struct x86emu *emu)
{
	uint32_t tmp;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		tmp = emu->x86.R_EAX;
		emu->x86.R_EAX = emu->x86.R_EDI;
		emu->x86.R_EDI = tmp;
	} else {
		tmp = emu->x86.R_AX;
		emu->x86.R_AX = emu->x86.R_DI;
		emu->x86.R_DI = (uint16_t) tmp;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x98
 */
static void
x86emuOp_cbw(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		if (emu->x86.R_AX & 0x8000) {
			emu->x86.R_EAX |= 0xffff0000;
		} else {
			emu->x86.R_EAX &= 0x0000ffff;
		}
	} else {
		if (emu->x86.R_AL & 0x80) {
			emu->x86.R_AH = 0xff;
		} else {
			emu->x86.R_AH = 0x0;
		}
	}
}

/*
 * REMARKS:
 * Handles opcode 0x99
 */
static void
x86emuOp_cwd(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		if (emu->x86.R_EAX & 0x80000000) {
			emu->x86.R_EDX = 0xffffffff;
		} else {
			emu->x86.R_EDX = 0x0;
		}
	} else {
		if (emu->x86.R_AX & 0x8000) {
			emu->x86.R_DX = 0xffff;
		} else {
			emu->x86.R_DX = 0x0;
		}
	}
}

/*
 * REMARKS:
 * Handles opcode 0x9a
 */
static void
x86emuOp_call_far_IMM(struct x86emu *emu)
{
	uint16_t farseg, faroff;

	faroff = fetch_word_imm(emu);
	farseg = fetch_word_imm(emu);
	/* XXX
	 * 
	 * Hooked interrupt vectors calling into our "BIOS" will cause problems
	 * unless all intersegment stuff is checked for BIOS access.  Check
	 * needed here.  For moment, let it alone. */
	push_word(emu, emu->x86.R_CS);
	emu->x86.R_CS = farseg;
	push_word(emu, emu->x86.R_IP);
	emu->x86.R_IP = faroff;
}

/*
 * REMARKS:
 * Handles opcode 0x9c
 */
static void
x86emuOp_pushf_word(struct x86emu *emu)
{
	uint32_t flags;

	/* clear out *all* bits not representing flags, and turn on real bits */
	flags = (emu->x86.R_EFLG & F_MSK) | F_ALWAYS_ON;
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		push_long(emu, flags);
	} else {
		push_word(emu, (uint16_t) flags);
	}
}

/*
 * REMARKS:
 * Handles opcode 0x9d
 */
static void
x86emuOp_popf_word(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		emu->x86.R_EFLG = pop_long(emu);
	} else {
		emu->x86.R_FLG = pop_word(emu);
	}
}

/*
 * REMARKS:
 * Handles opcode 0x9e
 */
static void
x86emuOp_sahf(struct x86emu *emu)
{
	/* clear the lower bits of the flag register */
	emu->x86.R_FLG &= 0xffffff00;
	/* or in the AH register into the flags register */
	emu->x86.R_FLG |= emu->x86.R_AH;
}

/*
 * REMARKS:
 * Handles opcode 0x9f
 */
static void
x86emuOp_lahf(struct x86emu *emu)
{
	emu->x86.R_AH = (uint8_t) (emu->x86.R_FLG & 0xff);
	/* undocumented TC++ behavior??? Nope.  It's documented, but you have
	 * too look real hard to notice it. */
	emu->x86.R_AH |= 0x2;
}

/*
 * REMARKS:
 * Handles opcode 0xa0
 */
static void
x86emuOp_mov_AL_M_IMM(struct x86emu *emu)
{
	uint16_t offset;

	offset = fetch_word_imm(emu);
	emu->x86.R_AL = fetch_data_byte(emu, offset);
}

/*
 * REMARKS:
 * Handles opcode 0xa1
 */
static void
x86emuOp_mov_AX_M_IMM(struct x86emu *emu)
{
	uint16_t offset;

	offset = fetch_word_imm(emu);
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		emu->x86.R_EAX = fetch_data_long(emu, offset);
	} else {
		emu->x86.R_AX = fetch_data_word(emu, offset);
	}
}

/*
 * REMARKS:
 * Handles opcode 0xa2
 */
static void
x86emuOp_mov_M_AL_IMM(struct x86emu *emu)
{
	uint16_t offset;

	offset = fetch_word_imm(emu);
	store_data_byte(emu, offset, emu->x86.R_AL);
}

/*
 * REMARKS:
 * Handles opcode 0xa3
 */
static void
x86emuOp_mov_M_AX_IMM(struct x86emu *emu)
{
	uint16_t offset;

	offset = fetch_word_imm(emu);
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		store_data_long(emu, offset, emu->x86.R_EAX);
	} else {
		store_data_word(emu, offset, emu->x86.R_AX);
	}
}

/*
 * REMARKS:
 * Handles opcode 0xa4
 */
static void
x86emuOp_movs_byte(struct x86emu *emu)
{
	uint8_t val;
	uint32_t count;
	int inc;

	if (ACCESS_FLAG(F_DF))	/* down */
		inc = -1;
	else
		inc = 1;
	count = 1;
	if (emu->x86.mode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
		/* dont care whether REPE or REPNE */
		/* move them until CX is ZERO. */
		count = emu->x86.R_CX;
		emu->x86.R_CX = 0;
		emu->x86.mode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
	}
	while (count--) {
		val = fetch_data_byte(emu, emu->x86.R_SI);
		store_byte(emu, emu->x86.R_ES, emu->x86.R_DI, val);
		emu->x86.R_SI += inc;
		emu->x86.R_DI += inc;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xa5
 */
static void
x86emuOp_movs_word(struct x86emu *emu)
{
	uint32_t val;
	int inc;
	uint32_t count;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		inc = 4;
	else
		inc = 2;

	if (ACCESS_FLAG(F_DF))	/* down */
		inc = -inc;

	count = 1;
	if (emu->x86.mode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
		/* dont care whether REPE or REPNE */
		/* move them until CX is ZERO. */
		count = emu->x86.R_CX;
		emu->x86.R_CX = 0;
		emu->x86.mode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
	}
	while (count--) {
		if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
			val = fetch_data_long(emu, emu->x86.R_SI);
			store_long(emu, emu->x86.R_ES, emu->x86.R_DI, val);
		} else {
			val = fetch_data_word(emu, emu->x86.R_SI);
			store_word(emu, emu->x86.R_ES, emu->x86.R_DI,
			    (uint16_t) val);
		}
		emu->x86.R_SI += inc;
		emu->x86.R_DI += inc;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xa6
 */
static void
x86emuOp_cmps_byte(struct x86emu *emu)
{
	int8_t val1, val2;
	int inc;

	if (ACCESS_FLAG(F_DF))	/* down */
		inc = -1;
	else
		inc = 1;

	if (emu->x86.mode & SYSMODE_PREFIX_REPE) {
		/* REPE  */
		/* move them until CX is ZERO. */
		while (emu->x86.R_CX != 0) {
			val1 = fetch_data_byte(emu, emu->x86.R_SI);
			val2 = fetch_byte(emu, emu->x86.R_ES, emu->x86.R_DI);
			cmp_byte(emu, val1, val2);
			emu->x86.R_CX -= 1;
			emu->x86.R_SI += inc;
			emu->x86.R_DI += inc;
			if (ACCESS_FLAG(F_ZF) == 0)
				break;
		}
		emu->x86.mode &= ~SYSMODE_PREFIX_REPE;
	} else if (emu->x86.mode & SYSMODE_PREFIX_REPNE) {
		/* REPNE  */
		/* move them until CX is ZERO. */
		while (emu->x86.R_CX != 0) {
			val1 = fetch_data_byte(emu, emu->x86.R_SI);
			val2 = fetch_byte(emu, emu->x86.R_ES, emu->x86.R_DI);
			cmp_byte(emu, val1, val2);
			emu->x86.R_CX -= 1;
			emu->x86.R_SI += inc;
			emu->x86.R_DI += inc;
			if (ACCESS_FLAG(F_ZF))
				break;	/* zero flag set means equal */
		}
		emu->x86.mode &= ~SYSMODE_PREFIX_REPNE;
	} else {
		val1 = fetch_data_byte(emu, emu->x86.R_SI);
		val2 = fetch_byte(emu, emu->x86.R_ES, emu->x86.R_DI);
		cmp_byte(emu, val1, val2);
		emu->x86.R_SI += inc;
		emu->x86.R_DI += inc;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xa7
 */
static void
x86emuOp_cmps_word(struct x86emu *emu)
{
	uint32_t val1, val2;
	int inc;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		if (ACCESS_FLAG(F_DF))	/* down */
			inc = -4;
		else
			inc = 4;
	} else {
		if (ACCESS_FLAG(F_DF))	/* down */
			inc = -2;
		else
			inc = 2;
	}
	if (emu->x86.mode & SYSMODE_PREFIX_REPE) {
		/* REPE  */
		/* move them until CX is ZERO. */
		while (emu->x86.R_CX != 0) {
			if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
				val1 = fetch_data_long(emu, emu->x86.R_SI);
				val2 = fetch_long(emu, emu->x86.R_ES,
				    emu->x86.R_DI);
				cmp_long(emu, val1, val2);
			} else {
				val1 = fetch_data_word(emu, emu->x86.R_SI);
				val2 = fetch_word(emu, emu->x86.R_ES,
				    emu->x86.R_DI);
				cmp_word(emu, (uint16_t) val1, (uint16_t) val2);
			}
			emu->x86.R_CX -= 1;
			emu->x86.R_SI += inc;
			emu->x86.R_DI += inc;
			if (ACCESS_FLAG(F_ZF) == 0)
				break;
		}
		emu->x86.mode &= ~SYSMODE_PREFIX_REPE;
	} else if (emu->x86.mode & SYSMODE_PREFIX_REPNE) {
		/* REPNE  */
		/* move them until CX is ZERO. */
		while (emu->x86.R_CX != 0) {
			if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
				val1 = fetch_data_long(emu, emu->x86.R_SI);
				val2 = fetch_long(emu, emu->x86.R_ES,
				    emu->x86.R_DI);
				cmp_long(emu, val1, val2);
			} else {
				val1 = fetch_data_word(emu, emu->x86.R_SI);
				val2 = fetch_word(emu, emu->x86.R_ES,
				    emu->x86.R_DI);
				cmp_word(emu, (uint16_t) val1, (uint16_t) val2);
			}
			emu->x86.R_CX -= 1;
			emu->x86.R_SI += inc;
			emu->x86.R_DI += inc;
			if (ACCESS_FLAG(F_ZF))
				break;	/* zero flag set means equal */
		}
		emu->x86.mode &= ~SYSMODE_PREFIX_REPNE;
	} else {
		if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
			val1 = fetch_data_long(emu, emu->x86.R_SI);
			val2 = fetch_long(emu, emu->x86.R_ES, emu->x86.R_DI);
			cmp_long(emu, val1, val2);
		} else {
			val1 = fetch_data_word(emu, emu->x86.R_SI);
			val2 = fetch_word(emu, emu->x86.R_ES, emu->x86.R_DI);
			cmp_word(emu, (uint16_t) val1, (uint16_t) val2);
		}
		emu->x86.R_SI += inc;
		emu->x86.R_DI += inc;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xa9
 */
static void
x86emuOp_test_AX_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		test_long(emu, emu->x86.R_EAX, fetch_long_imm(emu));
	} else {
		test_word(emu, emu->x86.R_AX, fetch_word_imm(emu));
	}
}

/*
 * REMARKS:
 * Handles opcode 0xaa
 */
static void
x86emuOp_stos_byte(struct x86emu *emu)
{
	int inc;

	if (ACCESS_FLAG(F_DF))	/* down */
		inc = -1;
	else
		inc = 1;
	if (emu->x86.mode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
		/* dont care whether REPE or REPNE */
		/* move them until CX is ZERO. */
		while (emu->x86.R_CX != 0) {
			store_byte(emu, emu->x86.R_ES, emu->x86.R_DI,
			    emu->x86.R_AL);
			emu->x86.R_CX -= 1;
			emu->x86.R_DI += inc;
		}
		emu->x86.mode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
	} else {
		store_byte(emu, emu->x86.R_ES, emu->x86.R_DI, emu->x86.R_AL);
		emu->x86.R_DI += inc;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xab
 */
static void
x86emuOp_stos_word(struct x86emu *emu)
{
	int inc;
	uint32_t count;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		inc = 4;
	else
		inc = 2;
	
	if (ACCESS_FLAG(F_DF))	/* down */
		inc = -inc;

	count = 1;
	if (emu->x86.mode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
		/* dont care whether REPE or REPNE */
		/* move them until CX is ZERO. */
		count = emu->x86.R_CX;
		emu->x86.R_CX = 0;
		emu->x86.mode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
	}
	while (count--) {
		if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
			store_long(emu, emu->x86.R_ES, emu->x86.R_DI,
			    emu->x86.R_EAX);
		} else {
			store_word(emu, emu->x86.R_ES, emu->x86.R_DI,
			    emu->x86.R_AX);
		}
		emu->x86.R_DI += inc;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xac
 */
static void
x86emuOp_lods_byte(struct x86emu *emu)
{
	int inc;

	if (ACCESS_FLAG(F_DF))	/* down */
		inc = -1;
	else
		inc = 1;
	if (emu->x86.mode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
		/* dont care whether REPE or REPNE */
		/* move them until CX is ZERO. */
		while (emu->x86.R_CX != 0) {
			emu->x86.R_AL = fetch_data_byte(emu, emu->x86.R_SI);
			emu->x86.R_CX -= 1;
			emu->x86.R_SI += inc;
		}
		emu->x86.mode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
	} else {
		emu->x86.R_AL = fetch_data_byte(emu, emu->x86.R_SI);
		emu->x86.R_SI += inc;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xad
 */
static void
x86emuOp_lods_word(struct x86emu *emu)
{
	int inc;
	uint32_t count;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		inc = 4;
	else
		inc = 2;

	if (ACCESS_FLAG(F_DF))	/* down */
		inc = -inc;

	count = 1;
	if (emu->x86.mode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
		/* dont care whether REPE or REPNE */
		/* move them until CX is ZERO. */
		count = emu->x86.R_CX;
		emu->x86.R_CX = 0;
		emu->x86.mode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
	}
	while (count--) {
		if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
			emu->x86.R_EAX = fetch_data_long(emu, emu->x86.R_SI);
		} else {
			emu->x86.R_AX = fetch_data_word(emu, emu->x86.R_SI);
		}
		emu->x86.R_SI += inc;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xae
 */
static void
x86emuOp_scas_byte(struct x86emu *emu)
{
	int8_t val2;
	int inc;

	if (ACCESS_FLAG(F_DF))	/* down */
		inc = -1;
	else
		inc = 1;
	if (emu->x86.mode & SYSMODE_PREFIX_REPE) {
		/* REPE  */
		/* move them until CX is ZERO. */
		while (emu->x86.R_CX != 0) {
			val2 = fetch_byte(emu, emu->x86.R_ES, emu->x86.R_DI);
			cmp_byte(emu, emu->x86.R_AL, val2);
			emu->x86.R_CX -= 1;
			emu->x86.R_DI += inc;
			if (ACCESS_FLAG(F_ZF) == 0)
				break;
		}
		emu->x86.mode &= ~SYSMODE_PREFIX_REPE;
	} else if (emu->x86.mode & SYSMODE_PREFIX_REPNE) {
		/* REPNE  */
		/* move them until CX is ZERO. */
		while (emu->x86.R_CX != 0) {
			val2 = fetch_byte(emu, emu->x86.R_ES, emu->x86.R_DI);
			cmp_byte(emu, emu->x86.R_AL, val2);
			emu->x86.R_CX -= 1;
			emu->x86.R_DI += inc;
			if (ACCESS_FLAG(F_ZF))
				break;	/* zero flag set means equal */
		}
		emu->x86.mode &= ~SYSMODE_PREFIX_REPNE;
	} else {
		val2 = fetch_byte(emu, emu->x86.R_ES, emu->x86.R_DI);
		cmp_byte(emu, emu->x86.R_AL, val2);
		emu->x86.R_DI += inc;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xaf
 */
static void
x86emuOp_scas_word(struct x86emu *emu)
{
	int inc;
	uint32_t val;

	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		inc = 4;
	else
		inc = 2;

	if (ACCESS_FLAG(F_DF))	/* down */
		inc = -inc;

	if (emu->x86.mode & SYSMODE_PREFIX_REPE) {
		/* REPE  */
		/* move them until CX is ZERO. */
		while (emu->x86.R_CX != 0) {
			if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
				val = fetch_long(emu, emu->x86.R_ES,
				    emu->x86.R_DI);
				cmp_long(emu, emu->x86.R_EAX, val);
			} else {
				val = fetch_word(emu, emu->x86.R_ES,
				    emu->x86.R_DI);
				cmp_word(emu, emu->x86.R_AX, (uint16_t) val);
			}
			emu->x86.R_CX -= 1;
			emu->x86.R_DI += inc;
			if (ACCESS_FLAG(F_ZF) == 0)
				break;
		}
		emu->x86.mode &= ~SYSMODE_PREFIX_REPE;
	} else if (emu->x86.mode & SYSMODE_PREFIX_REPNE) {
		/* REPNE  */
		/* move them until CX is ZERO. */
		while (emu->x86.R_CX != 0) {
			if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
				val = fetch_long(emu, emu->x86.R_ES,
				    emu->x86.R_DI);
				cmp_long(emu, emu->x86.R_EAX, val);
			} else {
				val = fetch_word(emu, emu->x86.R_ES,
				    emu->x86.R_DI);
				cmp_word(emu, emu->x86.R_AX, (uint16_t) val);
			}
			emu->x86.R_CX -= 1;
			emu->x86.R_DI += inc;
			if (ACCESS_FLAG(F_ZF))
				break;	/* zero flag set means equal */
		}
		emu->x86.mode &= ~SYSMODE_PREFIX_REPNE;
	} else {
		if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
			val = fetch_long(emu, emu->x86.R_ES, emu->x86.R_DI);
			cmp_long(emu, emu->x86.R_EAX, val);
		} else {
			val = fetch_word(emu, emu->x86.R_ES, emu->x86.R_DI);
			cmp_word(emu, emu->x86.R_AX, (uint16_t) val);
		}
		emu->x86.R_DI += inc;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xb8
 */
static void
x86emuOp_mov_word_AX_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		emu->x86.R_EAX = fetch_long_imm(emu);
	else
		emu->x86.R_AX = fetch_word_imm(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xb9
 */
static void
x86emuOp_mov_word_CX_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		emu->x86.R_ECX = fetch_long_imm(emu);
	else
		emu->x86.R_CX = fetch_word_imm(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xba
 */
static void
x86emuOp_mov_word_DX_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		emu->x86.R_EDX = fetch_long_imm(emu);
	else
		emu->x86.R_DX = fetch_word_imm(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xbb
 */
static void
x86emuOp_mov_word_BX_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		emu->x86.R_EBX = fetch_long_imm(emu);
	else
		emu->x86.R_BX = fetch_word_imm(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xbc
 */
static void
x86emuOp_mov_word_SP_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		emu->x86.R_ESP = fetch_long_imm(emu);
	else
		emu->x86.R_SP = fetch_word_imm(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xbd
 */
static void
x86emuOp_mov_word_BP_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		emu->x86.R_EBP = fetch_long_imm(emu);
	else
		emu->x86.R_BP = fetch_word_imm(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xbe
 */
static void
x86emuOp_mov_word_SI_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		emu->x86.R_ESI = fetch_long_imm(emu);
	else
		emu->x86.R_SI = fetch_word_imm(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xbf
 */
static void
x86emuOp_mov_word_DI_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		emu->x86.R_EDI = fetch_long_imm(emu);
	else
		emu->x86.R_DI = fetch_word_imm(emu);
}
/* used by opcodes c0, d0, and d2. */
static
uint8_t(* const opcD0_byte_operation[])
    (struct x86emu *, uint8_t d, uint8_t s) =
{
	rol_byte,
	ror_byte,
	rcl_byte,
	rcr_byte,
	shl_byte,
	shr_byte,
	shl_byte,		/* sal_byte === shl_byte  by definition */
	sar_byte,
};

/*
 * REMARKS:
 * Handles opcode 0xc0
 */
static void
x86emuOp_opcC0_byte_RM_MEM(struct x86emu *emu)
{
	uint8_t destval, amt;

	/*
         * Yet another weirdo special case instruction format.  Part of
         * the opcode held below in "RH".  Doubly nested case would
         * result, except that the decoded instruction
         */
	fetch_decode_modrm(emu);
	/* know operation, decode the mod byte to find the addressing mode. */
	destval = decode_and_fetch_byte_imm8(emu, &amt);
	destval = (*opcD0_byte_operation[emu->cur_rh]) (emu, destval, amt);
	write_back_byte(emu, destval);
}
/* used by opcodes c1, d1, and d3. */
static
uint16_t(* const opcD1_word_operation[])
    (struct x86emu *, uint16_t s, uint8_t d) =
{
	rol_word,
	ror_word,
	rcl_word,
	rcr_word,
	shl_word,
	shr_word,
	shl_word,		/* sal_byte === shl_byte  by definition */
	sar_word,
};
/* used by opcodes c1, d1, and d3. */
static
uint32_t(* const opcD1_long_operation[])
    (struct x86emu *, uint32_t s, uint8_t d) =
{
	rol_long,
	ror_long,
	rcl_long,
	rcr_long,
	shl_long,
	shr_long,
	shl_long,		/* sal_byte === shl_byte  by definition */
	sar_long,
};

/*
 * REMARKS:
 * Handles opcode 0xc1
 */
static void
x86emuOp_opcC1_word_RM_MEM(struct x86emu *emu)
{
	uint8_t amt;

	/*
         * Yet another weirdo special case instruction format.  Part of
         * the opcode held below in "RH".  Doubly nested case would
         * result, except that the decoded instruction
         */
	fetch_decode_modrm(emu);
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		uint32_t destval;

		destval = decode_and_fetch_long_imm8(emu, &amt);
		destval = (*opcD1_long_operation[emu->cur_rh])
		    (emu, destval, amt);
		write_back_long(emu, destval);
	} else {
		uint16_t destval;

		destval = decode_and_fetch_word_imm8(emu, &amt);
		destval = (*opcD1_word_operation[emu->cur_rh])
		    (emu, destval, amt);
		write_back_word(emu, destval);
	}
}

/*
 * REMARKS:
 * Handles opcode 0xc2
 */
static void
x86emuOp_ret_near_IMM(struct x86emu *emu)
{
	uint16_t imm;

	imm = fetch_word_imm(emu);
	emu->x86.R_IP = pop_word(emu);
	emu->x86.R_SP += imm;
}

/*
 * REMARKS:
 * Handles opcode 0xc6
 */
static void
x86emuOp_mov_byte_RM_IMM(struct x86emu *emu)
{
	uint8_t *destreg;
	uint32_t destoffset;
	uint8_t imm;

	fetch_decode_modrm(emu);
	if (emu->cur_rh != 0)
		x86emu_halt_sys(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		imm = fetch_byte_imm(emu);
		store_data_byte(emu, destoffset, imm);
	} else {
		destreg = decode_rl_byte_register(emu);
		imm = fetch_byte_imm(emu);
		*destreg = imm;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xc7
 */
static void
x86emuOp32_mov_word_RM_IMM(struct x86emu *emu)
{
	uint32_t destoffset;
	uint32_t imm, *destreg;

	fetch_decode_modrm(emu);
	if (emu->cur_rh != 0)
		x86emu_halt_sys(emu);

	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		imm = fetch_long_imm(emu);
		store_data_long(emu, destoffset, imm);
	} else {
		destreg = decode_rl_long_register(emu);
		imm = fetch_long_imm(emu);
		*destreg = imm;
	}
}

static void
x86emuOp16_mov_word_RM_IMM(struct x86emu *emu)
{
	uint32_t destoffset;
	uint16_t imm, *destreg;

	fetch_decode_modrm(emu);
	if (emu->cur_rh != 0)
		x86emu_halt_sys(emu);

	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		imm = fetch_word_imm(emu);
		store_data_word(emu, destoffset, imm);
	} else {
		destreg = decode_rl_word_register(emu);
		imm = fetch_word_imm(emu);
		*destreg = imm;
	}
}

static void
x86emuOp_mov_word_RM_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp32_mov_word_RM_IMM(emu);
	else
		x86emuOp16_mov_word_RM_IMM(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xc8
 */
static void
x86emuOp_enter(struct x86emu *emu)
{
	uint16_t local, frame_pointer;
	uint8_t nesting;
	int i;

	local = fetch_word_imm(emu);
	nesting = fetch_byte_imm(emu);
	push_word(emu, emu->x86.R_BP);
	frame_pointer = emu->x86.R_SP;
	if (nesting > 0) {
		for (i = 1; i < nesting; i++) {
			emu->x86.R_BP -= 2;
			push_word(emu, fetch_word(emu, emu->x86.R_SS,
			    emu->x86.R_BP));
		}
		push_word(emu, frame_pointer);
	}
	emu->x86.R_BP = frame_pointer;
	emu->x86.R_SP = (uint16_t) (emu->x86.R_SP - local);
}

/*
 * REMARKS:
 * Handles opcode 0xc9
 */
static void
x86emuOp_leave(struct x86emu *emu)
{
	emu->x86.R_SP = emu->x86.R_BP;
	emu->x86.R_BP = pop_word(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xca
 */
static void
x86emuOp_ret_far_IMM(struct x86emu *emu)
{
	uint16_t imm;

	imm = fetch_word_imm(emu);
	emu->x86.R_IP = pop_word(emu);
	emu->x86.R_CS = pop_word(emu);
	emu->x86.R_SP += imm;
}

/*
 * REMARKS:
 * Handles opcode 0xcb
 */
static void
x86emuOp_ret_far(struct x86emu *emu)
{
	emu->x86.R_IP = pop_word(emu);
	emu->x86.R_CS = pop_word(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xcc
 */
static void
x86emuOp_int3(struct x86emu *emu)
{
	x86emu_intr_dispatch(emu, 3);
}

/*
 * REMARKS:
 * Handles opcode 0xcd
 */
static void
x86emuOp_int_IMM(struct x86emu *emu)
{
	uint8_t intnum;

	intnum = fetch_byte_imm(emu);
	x86emu_intr_dispatch(emu, intnum);
}

/*
 * REMARKS:
 * Handles opcode 0xce
 */
static void
x86emuOp_into(struct x86emu *emu)
{
	if (ACCESS_FLAG(F_OF))
		x86emu_intr_dispatch(emu, 4);
}

/*
 * REMARKS:
 * Handles opcode 0xcf
 */
static void
x86emuOp_iret(struct x86emu *emu)
{
	emu->x86.R_IP = pop_word(emu);
	emu->x86.R_CS = pop_word(emu);
	emu->x86.R_FLG = pop_word(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xd0
 */
static void
x86emuOp_opcD0_byte_RM_1(struct x86emu *emu)
{
	uint8_t destval;

	fetch_decode_modrm(emu);
	destval = decode_and_fetch_byte(emu);
	destval = (*opcD0_byte_operation[emu->cur_rh]) (emu, destval, 1);
	write_back_byte(emu, destval);
}

/*
 * REMARKS:
 * Handles opcode 0xd1
 */
static void
x86emuOp_opcD1_word_RM_1(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		uint32_t destval;

		fetch_decode_modrm(emu);
		destval = decode_and_fetch_long(emu);
		destval = (*opcD1_long_operation[emu->cur_rh])(emu, destval, 1);
		write_back_long(emu, destval);
	} else {
		uint16_t destval;

		fetch_decode_modrm(emu);
		destval = decode_and_fetch_word(emu);
		destval = (*opcD1_word_operation[emu->cur_rh])(emu, destval, 1);
		write_back_word(emu, destval);
	}
}

/*
 * REMARKS:
 * Handles opcode 0xd2
 */
static void
x86emuOp_opcD2_byte_RM_CL(struct x86emu *emu)
{
	uint8_t destval;

	fetch_decode_modrm(emu);
	destval = decode_and_fetch_byte(emu);
	destval = (*opcD0_byte_operation[emu->cur_rh])
	    (emu, destval, emu->x86.R_CL);
	write_back_byte(emu, destval);
}

/*
 * REMARKS:
 * Handles opcode 0xd3
 */
static void
x86emuOp_opcD3_word_RM_CL(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		uint32_t destval;

		fetch_decode_modrm(emu);
		destval = decode_and_fetch_long(emu);
		destval = (*opcD1_long_operation[emu->cur_rh])
		    (emu, destval, emu->x86.R_CL);
		write_back_long(emu, destval);
	} else {
		uint16_t destval;

		fetch_decode_modrm(emu);
		destval = decode_and_fetch_word(emu);
		destval = (*opcD1_word_operation[emu->cur_rh])
		    (emu, destval, emu->x86.R_CL);
		write_back_word(emu, destval);
	}
}

/*
 * REMARKS:
 * Handles opcode 0xd4
 */
static void
x86emuOp_aam(struct x86emu *emu)
{
	uint8_t a;

	a = fetch_byte_imm(emu);	/* this is a stupid encoding. */
	if (a != 10) {
		/* fix: add base decoding aam_word(uint8_t val, int base a) */
		x86emu_halt_sys(emu);
	}
	/* note the type change here --- returning AL and AH in AX. */
	emu->x86.R_AX = aam_word(emu, emu->x86.R_AL);
}

/*
 * REMARKS:
 * Handles opcode 0xd5
 */
static void
x86emuOp_aad(struct x86emu *emu)
{
	uint8_t a;

	a = fetch_byte_imm(emu);
	if (a != 10) {
		/* fix: add base decoding aad_word(uint16_t val, int base a) */
		x86emu_halt_sys(emu);
	}
	emu->x86.R_AX = aad_word(emu, emu->x86.R_AX);
}
/* opcode 0xd6 ILLEGAL OPCODE */


/*
 * REMARKS:
 * Handles opcode 0xd7
 */
static void
x86emuOp_xlat(struct x86emu *emu)
{
	uint16_t addr;

	addr = (uint16_t) (emu->x86.R_BX + (uint8_t) emu->x86.R_AL);
	emu->x86.R_AL = fetch_data_byte(emu, addr);
}

/* opcode=0xd8 */
static void 
x86emuOp_esc_coprocess_d8(struct x86emu *emu)
{
}
/* opcode=0xd9 */
static void 
x86emuOp_esc_coprocess_d9(struct x86emu *emu)
{
	fetch_decode_modrm(emu);
	if (emu->cur_mod != 3)
		decode_rl_address(emu);
}
/* opcode=0xda */
static void 
x86emuOp_esc_coprocess_da(struct x86emu *emu)
{
	fetch_decode_modrm(emu);
	if (emu->cur_mod != 3)
		decode_rl_address(emu);
}
/* opcode=0xdb */
static void 
x86emuOp_esc_coprocess_db(struct x86emu *emu)
{
	fetch_decode_modrm(emu);
	if (emu->cur_mod != 3)
		decode_rl_address(emu);
}
/* opcode=0xdc */
static void 
x86emuOp_esc_coprocess_dc(struct x86emu *emu)
{
	fetch_decode_modrm(emu);
	if (emu->cur_mod != 3)
		decode_rl_address(emu);
}
/* opcode=0xdd */
static void 
x86emuOp_esc_coprocess_dd(struct x86emu *emu)
{
	fetch_decode_modrm(emu);
	if (emu->cur_mod != 3)
		decode_rl_address(emu);
}
/* opcode=0xde */
static void 
x86emuOp_esc_coprocess_de(struct x86emu *emu)
{
	fetch_decode_modrm(emu);
	if (emu->cur_mod != 3)
		decode_rl_address(emu);
}
/* opcode=0xdf */
static void 
x86emuOp_esc_coprocess_df(struct x86emu *emu)
{
	fetch_decode_modrm(emu);
	if (emu->cur_mod != 3)
		decode_rl_address(emu);
}


/*
 * REMARKS:
 * Handles opcode 0xe0
 */
static void
x86emuOp_loopne(struct x86emu *emu)
{
	int16_t ip;

	ip = (int8_t) fetch_byte_imm(emu);
	ip += (int16_t) emu->x86.R_IP;
	emu->x86.R_CX -= 1;
	if (emu->x86.R_CX != 0 && !ACCESS_FLAG(F_ZF))	/* CX != 0 and !ZF */
		emu->x86.R_IP = ip;
}

/*
 * REMARKS:
 * Handles opcode 0xe1
 */
static void
x86emuOp_loope(struct x86emu *emu)
{
	int16_t ip;

	ip = (int8_t) fetch_byte_imm(emu);
	ip += (int16_t) emu->x86.R_IP;
	emu->x86.R_CX -= 1;
	if (emu->x86.R_CX != 0 && ACCESS_FLAG(F_ZF))	/* CX != 0 and ZF */
		emu->x86.R_IP = ip;
}

/*
 * REMARKS:
 * Handles opcode 0xe2
 */
static void
x86emuOp_loop(struct x86emu *emu)
{
	int16_t ip;

	ip = (int8_t) fetch_byte_imm(emu);
	ip += (int16_t) emu->x86.R_IP;
	emu->x86.R_CX -= 1;
	if (emu->x86.R_CX != 0)
		emu->x86.R_IP = ip;
}

/*
 * REMARKS:
 * Handles opcode 0xe3
 */
static void
x86emuOp_jcxz(struct x86emu *emu)
{
	uint16_t target;
	int8_t offset;

	/* jump to byte offset if overflow flag is set */
	offset = (int8_t) fetch_byte_imm(emu);
	target = (uint16_t) (emu->x86.R_IP + offset);
	if (emu->x86.R_CX == 0)
		emu->x86.R_IP = target;
}

/*
 * REMARKS:
 * Handles opcode 0xe4
 */
static void
x86emuOp_in_byte_AL_IMM(struct x86emu *emu)
{
	uint8_t port;

	port = (uint8_t) fetch_byte_imm(emu);
	emu->x86.R_AL = (*emu->emu_inb) (emu, port);
}

/*
 * REMARKS:
 * Handles opcode 0xe5
 */
static void
x86emuOp_in_word_AX_IMM(struct x86emu *emu)
{
	uint8_t port;

	port = (uint8_t) fetch_byte_imm(emu);
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		emu->x86.R_EAX = (*emu->emu_inl) (emu, port);
	} else {
		emu->x86.R_AX = (*emu->emu_inw) (emu, port);
	}
}

/*
 * REMARKS:
 * Handles opcode 0xe6
 */
static void
x86emuOp_out_byte_IMM_AL(struct x86emu *emu)
{
	uint8_t port;

	port = (uint8_t) fetch_byte_imm(emu);
	(*emu->emu_outb) (emu, port, emu->x86.R_AL);
}

/*
 * REMARKS:
 * Handles opcode 0xe7
 */
static void
x86emuOp_out_word_IMM_AX(struct x86emu *emu)
{
	uint8_t port;

	port = (uint8_t) fetch_byte_imm(emu);
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		(*emu->emu_outl) (emu, port, emu->x86.R_EAX);
	} else {
		(*emu->emu_outw) (emu, port, emu->x86.R_AX);
	}
}

/*
 * REMARKS:
 * Handles opcode 0xe8
 */
static void
x86emuOp_call_near_IMM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		int32_t ip;
		ip = (int32_t) fetch_long_imm(emu);
		ip += (int32_t) emu->x86.R_EIP;
		push_long(emu, emu->x86.R_EIP);
		emu->x86.R_EIP = ip;
	} else {
		int16_t ip;
		ip = (int16_t) fetch_word_imm(emu);
		ip += (int16_t) emu->x86.R_IP;	/* CHECK SIGN */
		push_word(emu, emu->x86.R_IP);
		emu->x86.R_IP = ip;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xe9
 */
static void
x86emuOp_jump_near_IMM(struct x86emu *emu)
{
	int ip;

	ip = (int16_t) fetch_word_imm(emu);
	ip += (int16_t) emu->x86.R_IP;
	emu->x86.R_IP = (uint16_t) ip;
}

/*
 * REMARKS:
 * Handles opcode 0xea
 */
static void
x86emuOp_jump_far_IMM(struct x86emu *emu)
{
	uint16_t cs, ip;

	ip = fetch_word_imm(emu);
	cs = fetch_word_imm(emu);
	emu->x86.R_IP = ip;
	emu->x86.R_CS = cs;
}

/*
 * REMARKS:
 * Handles opcode 0xeb
 */
static void
x86emuOp_jump_byte_IMM(struct x86emu *emu)
{
	uint16_t target;
	int8_t offset;

	offset = (int8_t) fetch_byte_imm(emu);
	target = (uint16_t) (emu->x86.R_IP + offset);
	emu->x86.R_IP = target;
}

/*
 * REMARKS:
 * Handles opcode 0xec
 */
static void
x86emuOp_in_byte_AL_DX(struct x86emu *emu)
{
	emu->x86.R_AL = (*emu->emu_inb) (emu, emu->x86.R_DX);
}

/*
 * REMARKS:
 * Handles opcode 0xed
 */
static void
x86emuOp_in_word_AX_DX(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		emu->x86.R_EAX = (*emu->emu_inl) (emu, emu->x86.R_DX);
	} else {
		emu->x86.R_AX = (*emu->emu_inw) (emu, emu->x86.R_DX);
	}
}

/*
 * REMARKS:
 * Handles opcode 0xee
 */
static void
x86emuOp_out_byte_DX_AL(struct x86emu *emu)
{
	(*emu->emu_outb) (emu, emu->x86.R_DX, emu->x86.R_AL);
}

/*
 * REMARKS:
 * Handles opcode 0xef
 */
static void
x86emuOp_out_word_DX_AX(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
		(*emu->emu_outl) (emu, emu->x86.R_DX, emu->x86.R_EAX);
	} else {
		(*emu->emu_outw) (emu, emu->x86.R_DX, emu->x86.R_AX);
	}
}

/*
 * REMARKS:
 * Handles opcode 0xf0
 */
static void
x86emuOp_lock(struct x86emu *emu)
{
}
/*opcode 0xf1 ILLEGAL OPERATION */


/*
 * REMARKS:
 * Handles opcode 0xf5
 */
static void
x86emuOp_cmc(struct x86emu *emu)
{
	if (ACCESS_FLAG(F_CF))
		CLEAR_FLAG(F_CF);
	else
		SET_FLAG(F_CF);
}

/*
 * REMARKS:
 * Handles opcode 0xf6
 */
static void
x86emuOp_opcF6_byte_RM(struct x86emu *emu)
{
	uint8_t destval, srcval;

	/* long, drawn out code follows.  Double switch for a total of 32
	 * cases.  */
	fetch_decode_modrm(emu);
	if (emu->cur_rh == 1)
		x86emu_halt_sys(emu);

	if (emu->cur_rh == 0) {
		destval = decode_and_fetch_byte_imm8(emu, &srcval);
		test_byte(emu, destval, srcval);
		return;
	}
	destval = decode_and_fetch_byte(emu);
	switch (emu->cur_rh) {
	case 2:
		destval = ~destval;
		write_back_byte(emu, destval);
		break;
	case 3:
		destval = neg_byte(emu, destval);
		write_back_byte(emu, destval);
		break;
	case 4:
		mul_byte(emu, destval);
		break;
	case 5:
		imul_byte(emu, destval);
		break;
	case 6:
		div_byte(emu, destval);
		break;
	case 7:
		idiv_byte(emu, destval);
		break;
	}
}

/*
 * REMARKS:
 * Handles opcode 0xf7
 */
static void
x86emuOp32_opcF7_word_RM(struct x86emu *emu)
{
	uint32_t destval, srcval;

	/* long, drawn out code follows.  Double switch for a total of 32
	 * cases.  */
	fetch_decode_modrm(emu);
	if (emu->cur_rh == 1)
		x86emu_halt_sys(emu);

	if (emu->cur_rh == 0) {
		if (emu->cur_mod != 3) {
			uint32_t destoffset;

			destoffset = decode_rl_address(emu);
			srcval = fetch_long_imm(emu);
			destval = fetch_data_long(emu, destoffset);
		} else {
			srcval = fetch_long_imm(emu);
			destval = *decode_rl_long_register(emu);
		}
		test_long(emu, destval, srcval);
		return;
	}
	destval = decode_and_fetch_long(emu);
	switch (emu->cur_rh) {
	case 2:
		destval = ~destval;
		write_back_long(emu, destval);
		break;
	case 3:
		destval = neg_long(emu, destval);
		write_back_long(emu, destval);
		break;
	case 4:
		mul_long(emu, destval);
		break;
	case 5:
		imul_long(emu, destval);
		break;
	case 6:
		div_long(emu, destval);
		break;
	case 7:
		idiv_long(emu, destval);
		break;
	}
}
static void
x86emuOp16_opcF7_word_RM(struct x86emu *emu)
{
	uint16_t destval, srcval;

	/* long, drawn out code follows.  Double switch for a total of 32
	 * cases.  */
	fetch_decode_modrm(emu);
	if (emu->cur_rh == 1)
		x86emu_halt_sys(emu);

	if (emu->cur_rh == 0) {
		if (emu->cur_mod != 3) {
			uint32_t destoffset;

			destoffset = decode_rl_address(emu);
			srcval = fetch_word_imm(emu);
			destval = fetch_data_word(emu, destoffset);
		} else {
			srcval = fetch_word_imm(emu);
			destval = *decode_rl_word_register(emu);
		}
		test_word(emu, destval, srcval);
		return;
	}
	destval = decode_and_fetch_word(emu);
	switch (emu->cur_rh) {
	case 2:
		destval = ~destval;
		write_back_word(emu, destval);
		break;
	case 3:
		destval = neg_word(emu, destval);
		write_back_word(emu, destval);
		break;
	case 4:
		mul_word(emu, destval);
		break;
	case 5:
		imul_word(emu, destval);
		break;
	case 6:
		div_word(emu, destval);
		break;
	case 7:
		idiv_word(emu, destval);
		break;
	}
}
static void
x86emuOp_opcF7_word_RM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp32_opcF7_word_RM(emu);
	else
		x86emuOp16_opcF7_word_RM(emu);
}

/*
 * REMARKS:
 * Handles opcode 0xfe
 */
static void
x86emuOp_opcFE_byte_RM(struct x86emu *emu)
{
	uint8_t destval;
	uint32_t destoffset;
	uint8_t *destreg;

	/* Yet another special case instruction. */
	fetch_decode_modrm(emu);
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		switch (emu->cur_rh) {
		case 0:	/* inc word ptr ... */
			destval = fetch_data_byte(emu, destoffset);
			destval = inc_byte(emu, destval);
			store_data_byte(emu, destoffset, destval);
			break;
		case 1:	/* dec word ptr ... */
			destval = fetch_data_byte(emu, destoffset);
			destval = dec_byte(emu, destval);
			store_data_byte(emu, destoffset, destval);
			break;
		}
	} else {
		destreg = decode_rl_byte_register(emu);
		switch (emu->cur_rh) {
		case 0:
			*destreg = inc_byte(emu, *destreg);
			break;
		case 1:
			*destreg = dec_byte(emu, *destreg);
			break;
		}
	}
}

/*
 * REMARKS:
 * Handles opcode 0xff
 */
static void
x86emuOp32_opcFF_word_RM(struct x86emu *emu)
{
	uint32_t destoffset = 0;
	uint32_t destval, *destreg;

	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = fetch_data_long(emu, destoffset);
		switch (emu->cur_rh) {
		case 0:	/* inc word ptr ... */
			destval = inc_long(emu, destval);
			store_data_long(emu, destoffset, destval);
			break;
		case 1:	/* dec word ptr ... */
			destval = dec_long(emu, destval);
			store_data_long(emu, destoffset, destval);
			break;
		case 6:	/* push word ptr ... */
			push_long(emu, destval);
			break;
		}
	} else {
		destreg = decode_rl_long_register(emu);
		switch (emu->cur_rh) {
		case 0:
			*destreg = inc_long(emu, *destreg);
			break;
		case 1:
			*destreg = dec_long(emu, *destreg);
			break;
		case 6:
			push_long(emu, *destreg);
			break;
		}
	}
}

static void
x86emuOp16_opcFF_word_RM(struct x86emu *emu)
{
	uint32_t destoffset = 0;
	uint16_t *destreg;
	uint16_t destval;

	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = fetch_data_word(emu, destoffset);
		switch (emu->cur_rh) {
		case 0:
			destval = inc_word(emu, destval);
			store_data_word(emu, destoffset, destval);
			break;
		case 1:	/* dec word ptr ... */
			destval = dec_word(emu, destval);
			store_data_word(emu, destoffset, destval);
			break;
		case 6:	/* push word ptr ... */
			push_word(emu, destval);
			break;
		}
	} else {
		destreg = decode_rl_word_register(emu);
		switch (emu->cur_rh) {
		case 0:
			*destreg = inc_word(emu, *destreg);
			break;
		case 1:
			*destreg = dec_word(emu, *destreg);
			break;
		case 6:
			push_word(emu, *destreg);
			break;
		}
	}
}

static void
x86emuOp_opcFF_word_RM(struct x86emu *emu)
{
	uint32_t destoffset = 0;
	uint16_t destval, destval2;

	/* Yet another special case instruction. */
	fetch_decode_modrm(emu);
	if ((emu->cur_mod == 3 && (emu->cur_rh == 3 || emu->cur_rh == 5)) ||
	    emu->cur_rh == 7)
		x86emu_halt_sys(emu);
	if (emu->cur_rh == 0 || emu->cur_rh == 1 || emu->cur_rh == 6) {
		if (emu->x86.mode & SYSMODE_PREFIX_DATA)
			x86emuOp32_opcFF_word_RM(emu);
		else
			x86emuOp16_opcFF_word_RM(emu);
		return;
	}

	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		destval = fetch_data_word(emu, destoffset);
		switch (emu->cur_rh) {
		case 3:	/* call far ptr ... */
			destval2 = fetch_data_word(emu, destoffset + 2);
			push_word(emu, emu->x86.R_CS);
			emu->x86.R_CS = destval2;
			push_word(emu, emu->x86.R_IP);
			emu->x86.R_IP = destval;
			break;
		case 5:	/* jmp far ptr ... */
			destval2 = fetch_data_word(emu, destoffset + 2);
			emu->x86.R_IP = destval;
			emu->x86.R_CS = destval2;
			break;
		}
	} else {
		destval = *decode_rl_word_register(emu);
	}

	switch (emu->cur_rh) {
	case 2: /* call word ptr */
		push_word(emu, emu->x86.R_IP);
		emu->x86.R_IP = destval;
		break;
	case 4: /* jmp */
		emu->x86.R_IP = destval;
		break;
	}
}

/*
 *  * Single byte operation code table:
 */
static void
x86emu_exec_one_byte(struct x86emu * emu)
{
	uint8_t op1;

	op1 = fetch_byte_imm(emu);

	switch (op1) {
	case 0x00:
		common_binop_byte_rm_r(emu, add_byte);
		break;
	case 0x01:
		common_binop_word_long_rm_r(emu, add_word, add_long);
		break;
	case 0x02:
		common_binop_byte_r_rm(emu, add_byte);
		break;
	case 0x03:
		common_binop_word_long_r_rm(emu, add_word, add_long);
		break;
	case 0x04:
		common_binop_byte_imm(emu, add_byte);
		break;
	case 0x05:
		common_binop_word_long_imm(emu, add_word, add_long);
		break;
	case 0x06:
		push_word(emu, emu->x86.R_ES);
		break;
	case 0x07:
		emu->x86.R_ES = pop_word(emu);
		break;

	case 0x08:
		common_binop_byte_rm_r(emu, or_byte);
		break;
	case 0x09:
		common_binop_word_long_rm_r(emu, or_word, or_long);
		break;
	case 0x0a:
		common_binop_byte_r_rm(emu, or_byte);
		break;
	case 0x0b:
		common_binop_word_long_r_rm(emu, or_word, or_long);
		break;
	case 0x0c:
		common_binop_byte_imm(emu, or_byte);
		break;
	case 0x0d:
		common_binop_word_long_imm(emu, or_word, or_long);
		break;
	case 0x0e:
		push_word(emu, emu->x86.R_CS);
		break;
	case 0x0f:
		x86emu_exec_two_byte(emu);
		break;

	case 0x10:
		common_binop_byte_rm_r(emu, adc_byte);
		break;
	case 0x11:
		common_binop_word_long_rm_r(emu, adc_word, adc_long);
		break;
	case 0x12:
		common_binop_byte_r_rm(emu, adc_byte);
		break;
	case 0x13:
		common_binop_word_long_r_rm(emu, adc_word, adc_long);
		break;
	case 0x14:
		common_binop_byte_imm(emu, adc_byte);
		break;
	case 0x15:
		common_binop_word_long_imm(emu, adc_word, adc_long);
		break;
	case 0x16:
		push_word(emu, emu->x86.R_SS);
		break;
	case 0x17:
		emu->x86.R_SS = pop_word(emu);
		break;

	case 0x18:
		common_binop_byte_rm_r(emu, sbb_byte);
		break;
	case 0x19:
		common_binop_word_long_rm_r(emu, sbb_word, sbb_long);
		break;
	case 0x1a:
		common_binop_byte_r_rm(emu, sbb_byte);
		break;
	case 0x1b:
		common_binop_word_long_r_rm(emu, sbb_word, sbb_long);
		break;
	case 0x1c:
		common_binop_byte_imm(emu, sbb_byte);
		break;
	case 0x1d:
		common_binop_word_long_imm(emu, sbb_word, sbb_long);
		break;
	case 0x1e:
		push_word(emu, emu->x86.R_DS);
		break;
	case 0x1f:
		emu->x86.R_DS = pop_word(emu);
		break;

	case 0x20:
		common_binop_byte_rm_r(emu, and_byte);
		break;
	case 0x21:
		common_binop_word_long_rm_r(emu, and_word, and_long);
		break;
	case 0x22:
		common_binop_byte_r_rm(emu, and_byte);
		break;
	case 0x23:
		common_binop_word_long_r_rm(emu, and_word, and_long);
		break;
	case 0x24:
		common_binop_byte_imm(emu, and_byte);
		break;
	case 0x25:
		common_binop_word_long_imm(emu, and_word, and_long);
		break;
	case 0x26:
		emu->x86.mode |= SYSMODE_SEGOVR_ES;
		break;
	case 0x27:
		emu->x86.R_AL = daa_byte(emu, emu->x86.R_AL);
		break;

	case 0x28:
		common_binop_byte_rm_r(emu, sub_byte);
		break;
	case 0x29:
		common_binop_word_long_rm_r(emu, sub_word, sub_long);
		break;
	case 0x2a:
		common_binop_byte_r_rm(emu, sub_byte);
		break;
	case 0x2b:
		common_binop_word_long_r_rm(emu, sub_word, sub_long);
		break;
	case 0x2c:
		common_binop_byte_imm(emu, sub_byte);
		break;
	case 0x2d:
		common_binop_word_long_imm(emu, sub_word, sub_long);
		break;
	case 0x2e:
		emu->x86.mode |= SYSMODE_SEGOVR_CS;
		break;
	case 0x2f:
		emu->x86.R_AL = das_byte(emu, emu->x86.R_AL);
		break;

	case 0x30:
		common_binop_byte_rm_r(emu, xor_byte);
		break;
	case 0x31:
		common_binop_word_long_rm_r(emu, xor_word, xor_long);
		break;
	case 0x32:
		common_binop_byte_r_rm(emu, xor_byte);
		break;
	case 0x33:
		common_binop_word_long_r_rm(emu, xor_word, xor_long);
		break;
	case 0x34:
		common_binop_byte_imm(emu, xor_byte);
		break;
	case 0x35:
		common_binop_word_long_imm(emu, xor_word, xor_long);
		break;
	case 0x36:
		emu->x86.mode |= SYSMODE_SEGOVR_SS;
		break;
	case 0x37:
		emu->x86.R_AX = aaa_word(emu, emu->x86.R_AX);
		break;

	case 0x38:
		common_binop_ns_byte_rm_r(emu, cmp_byte_no_return);
		break;
	case 0x39:
		common_binop_ns_word_long_rm_r(emu, cmp_word_no_return,
		    cmp_long_no_return);
		break;
	case 0x3a:
		x86emuOp_cmp_byte_R_RM(emu);
		break;
	case 0x3b:
		x86emuOp_cmp_word_R_RM(emu);
		break;
	case 0x3c:
		x86emuOp_cmp_byte_AL_IMM(emu);
		break;
	case 0x3d:
		x86emuOp_cmp_word_AX_IMM(emu);
		break;
	case 0x3e:
		emu->x86.mode |= SYSMODE_SEGOVR_DS;
		break;
	case 0x3f:
		emu->x86.R_AX = aas_word(emu, emu->x86.R_AX);
		break;

	case 0x40:
		common_inc_word_long(emu, &emu->x86.register_a);
		break;
	case 0x41:
		common_inc_word_long(emu, &emu->x86.register_c);
		break;
	case 0x42:
		common_inc_word_long(emu, &emu->x86.register_d);
		break;
	case 0x43:
		common_inc_word_long(emu, &emu->x86.register_b);
		break;
	case 0x44:
		common_inc_word_long(emu, &emu->x86.register_sp);
		break;
	case 0x45:
		common_inc_word_long(emu, &emu->x86.register_bp);
		break;
	case 0x46:
		common_inc_word_long(emu, &emu->x86.register_si);
		break;
	case 0x47:
		common_inc_word_long(emu, &emu->x86.register_di);
		break;

	case 0x48:
		common_dec_word_long(emu, &emu->x86.register_a);
		break;
	case 0x49:
		common_dec_word_long(emu, &emu->x86.register_c);
		break;
	case 0x4a:
		common_dec_word_long(emu, &emu->x86.register_d);
		break;
	case 0x4b:
		common_dec_word_long(emu, &emu->x86.register_b);
		break;
	case 0x4c:
		common_dec_word_long(emu, &emu->x86.register_sp);
		break;
	case 0x4d:
		common_dec_word_long(emu, &emu->x86.register_bp);
		break;
	case 0x4e:
		common_dec_word_long(emu, &emu->x86.register_si);
		break;
	case 0x4f:
		common_dec_word_long(emu, &emu->x86.register_di);
		break;

	case 0x50:
		common_push_word_long(emu, &emu->x86.register_a);
		break;
	case 0x51:
		common_push_word_long(emu, &emu->x86.register_c);
		break;
	case 0x52:
		common_push_word_long(emu, &emu->x86.register_d);
		break;
	case 0x53:
		common_push_word_long(emu, &emu->x86.register_b);
		break;
	case 0x54:
		common_push_word_long(emu, &emu->x86.register_sp);
		break;
	case 0x55:
		common_push_word_long(emu, &emu->x86.register_bp);
		break;
	case 0x56:
		common_push_word_long(emu, &emu->x86.register_si);
		break;
	case 0x57:
		common_push_word_long(emu, &emu->x86.register_di);
		break;

	case 0x58:
		common_pop_word_long(emu, &emu->x86.register_a);
		break;
	case 0x59:
		common_pop_word_long(emu, &emu->x86.register_c);
		break;
	case 0x5a:
		common_pop_word_long(emu, &emu->x86.register_d);
		break;
	case 0x5b:
		common_pop_word_long(emu, &emu->x86.register_b);
		break;
	case 0x5c:
		common_pop_word_long(emu, &emu->x86.register_sp);
		break;
	case 0x5d:
		common_pop_word_long(emu, &emu->x86.register_bp);
		break;
	case 0x5e:
		common_pop_word_long(emu, &emu->x86.register_si);
		break;
	case 0x5f:
		common_pop_word_long(emu, &emu->x86.register_di);
		break;

	case 0x60:
		x86emuOp_push_all(emu);
		break;
	case 0x61:
		x86emuOp_pop_all(emu);
		break;
	/* 0x62 bound */
	/* 0x63 arpl */
	case 0x64:
		emu->x86.mode |= SYSMODE_SEGOVR_FS;
		break;
	case 0x65:
		emu->x86.mode |= SYSMODE_SEGOVR_GS;
		break;
	case 0x66:
		emu->x86.mode |= SYSMODE_PREFIX_DATA;
		break;
	case 0x67:
		emu->x86.mode |= SYSMODE_PREFIX_ADDR;
		break;

	case 0x68:
		x86emuOp_push_word_IMM(emu);
		break;
	case 0x69:
		common_imul_imm(emu, 0);
		break;
	case 0x6a:
		x86emuOp_push_byte_IMM(emu);
		break;
	case 0x6b:
		common_imul_imm(emu, 1);
		break;
	case 0x6c:
		ins(emu, 1);
		break;
	case 0x6d:
		x86emuOp_ins_word(emu);
		break;
	case 0x6e:
		outs(emu, 1);
		break;
	case 0x6f:
		x86emuOp_outs_word(emu);
		break;

	case 0x70:
		common_jmp_near(emu, ACCESS_FLAG(F_OF));
		break;
	case 0x71:
		common_jmp_near(emu, !ACCESS_FLAG(F_OF));
		break;
	case 0x72:
		common_jmp_near(emu, ACCESS_FLAG(F_CF));
		break;
	case 0x73:
		common_jmp_near(emu, !ACCESS_FLAG(F_CF));
		break;
	case 0x74:
		common_jmp_near(emu, ACCESS_FLAG(F_ZF));
		break;
	case 0x75:
		common_jmp_near(emu, !ACCESS_FLAG(F_ZF));
		break;
	case 0x76:
		common_jmp_near(emu, ACCESS_FLAG(F_CF) || ACCESS_FLAG(F_ZF));
		break;
	case 0x77:
		common_jmp_near(emu, !ACCESS_FLAG(F_CF) && !ACCESS_FLAG(F_ZF));
		break;

	case 0x78:
		common_jmp_near(emu, ACCESS_FLAG(F_SF));
		break;
	case 0x79:
		common_jmp_near(emu, !ACCESS_FLAG(F_SF));
		break;
	case 0x7a:
		common_jmp_near(emu, ACCESS_FLAG(F_PF));
		break;
	case 0x7b:
		common_jmp_near(emu, !ACCESS_FLAG(F_PF));
		break;
	case 0x7c:
		x86emuOp_jump_near_L(emu);
		break;
	case 0x7d:
		x86emuOp_jump_near_NL(emu);
		break;
	case 0x7e:
		x86emuOp_jump_near_LE(emu);
		break;
	case 0x7f:
		x86emuOp_jump_near_NLE(emu);
		break;

	case 0x80:
		x86emuOp_opc80_byte_RM_IMM(emu);
		break;
	case 0x81:
		x86emuOp_opc81_word_RM_IMM(emu);
		break;
	case 0x82:
		x86emuOp_opc82_byte_RM_IMM(emu);
		break;
	case 0x83:
		x86emuOp_opc83_word_RM_IMM(emu);
		break;
	case 0x84:
		common_binop_ns_byte_rm_r(emu, test_byte);
		break;
	case 0x85:
		common_binop_ns_word_long_rm_r(emu, test_word, test_long);
		break;
	case 0x86:
		x86emuOp_xchg_byte_RM_R(emu);
		break;
	case 0x87:
		x86emuOp_xchg_word_RM_R(emu);
		break;

	case 0x88:
		x86emuOp_mov_byte_RM_R(emu);
		break;
	case 0x89:
		x86emuOp_mov_word_RM_R(emu);
		break;
	case 0x8a:
		x86emuOp_mov_byte_R_RM(emu);
		break;
	case 0x8b:
		x86emuOp_mov_word_R_RM(emu);
		break;
	case 0x8c:
		x86emuOp_mov_word_RM_SR(emu);
		break;
	case 0x8d:
		x86emuOp_lea_word_R_M(emu);
		break;
	case 0x8e:
		x86emuOp_mov_word_SR_RM(emu);
		break;
	case 0x8f:
		x86emuOp_pop_RM(emu);
		break;

	case 0x90:
		/* nop */
		break;
	case 0x91:
		x86emuOp_xchg_word_AX_CX(emu);
		break;
	case 0x92:
		x86emuOp_xchg_word_AX_DX(emu);
		break;
	case 0x93:
		x86emuOp_xchg_word_AX_BX(emu);
		break;
	case 0x94:
		x86emuOp_xchg_word_AX_SP(emu);
		break;
	case 0x95:
		x86emuOp_xchg_word_AX_BP(emu);
		break;
	case 0x96:
		x86emuOp_xchg_word_AX_SI(emu);
		break;
	case 0x97:
		x86emuOp_xchg_word_AX_DI(emu);
		break;

	case 0x98:
		x86emuOp_cbw(emu);
		break;
	case 0x99:
		x86emuOp_cwd(emu);
		break;
	case 0x9a:
		x86emuOp_call_far_IMM(emu);
		break;
	case 0x9b:
		/* wait */
		break;
	case 0x9c:
		x86emuOp_pushf_word(emu);
		break;
	case 0x9d:
		x86emuOp_popf_word(emu);
		break;
	case 0x9e:
		x86emuOp_sahf(emu);
		break;
	case 0x9f:
		x86emuOp_lahf(emu);
		break;

	case 0xa0:
		x86emuOp_mov_AL_M_IMM(emu);
		break;
	case 0xa1:
		x86emuOp_mov_AX_M_IMM(emu);
		break;
	case 0xa2:
		x86emuOp_mov_M_AL_IMM(emu);
		break;
	case 0xa3:
		x86emuOp_mov_M_AX_IMM(emu);
		break;
	case 0xa4:
		x86emuOp_movs_byte(emu);
		break;
	case 0xa5:
		x86emuOp_movs_word(emu);
		break;
	case 0xa6:
		x86emuOp_cmps_byte(emu);
		break;
	case 0xa7:
		x86emuOp_cmps_word(emu);
		break;

	case 0xa8:
		test_byte(emu, emu->x86.R_AL, fetch_byte_imm(emu));
		break;
	case 0xa9:
		x86emuOp_test_AX_IMM(emu);
		break;
	case 0xaa:
		x86emuOp_stos_byte(emu);
		break;
	case 0xab:
		x86emuOp_stos_word(emu);
		break;
	case 0xac:
		x86emuOp_lods_byte(emu);
		break;
	case 0xad:
		x86emuOp_lods_word(emu);
		break;
	case 0xae:
		x86emuOp_scas_byte(emu);
		break;
	case 0xaf:
		x86emuOp_scas_word(emu);
		break;

	case 0xb0:
		emu->x86.R_AL = fetch_byte_imm(emu);
		break;
	case 0xb1:
		emu->x86.R_CL = fetch_byte_imm(emu);
		break;
	case 0xb2:
		emu->x86.R_DL = fetch_byte_imm(emu);
		break;
	case 0xb3:
		emu->x86.R_BL = fetch_byte_imm(emu);
		break;
	case 0xb4:
		emu->x86.R_AH = fetch_byte_imm(emu);
		break;
	case 0xb5:
		emu->x86.R_CH = fetch_byte_imm(emu);
		break;
	case 0xb6:
		emu->x86.R_DH = fetch_byte_imm(emu);
		break;
	case 0xb7:
		emu->x86.R_BH = fetch_byte_imm(emu);
		break;

	case 0xb8:
		x86emuOp_mov_word_AX_IMM(emu);
		break;
	case 0xb9:
		x86emuOp_mov_word_CX_IMM(emu);
		break;
	case 0xba:
		x86emuOp_mov_word_DX_IMM(emu);
		break;
	case 0xbb:
		x86emuOp_mov_word_BX_IMM(emu);
		break;
	case 0xbc:

		x86emuOp_mov_word_SP_IMM(emu);
		break;
	case 0xbd:
		x86emuOp_mov_word_BP_IMM(emu);
		break;
	case 0xbe:
		x86emuOp_mov_word_SI_IMM(emu);
		break;
	case 0xbf:
		x86emuOp_mov_word_DI_IMM(emu);
		break;

	case 0xc0:
		x86emuOp_opcC0_byte_RM_MEM(emu);
		break;
	case 0xc1:
		x86emuOp_opcC1_word_RM_MEM(emu);
		break;
	case 0xc2:
		x86emuOp_ret_near_IMM(emu);
		break;
	case 0xc3:
		emu->x86.R_IP = pop_word(emu);
		break;
	case 0xc4:
		common_load_far_pointer(emu, &emu->x86.R_ES);
		break;
	case 0xc5:
		common_load_far_pointer(emu, &emu->x86.R_DS);
		break;
	case 0xc6:
		x86emuOp_mov_byte_RM_IMM(emu);
		break;
	case 0xc7:
		x86emuOp_mov_word_RM_IMM(emu);
		break;
	case 0xc8:
		x86emuOp_enter(emu);
		break;
	case 0xc9:
		x86emuOp_leave(emu);
		break;
	case 0xca:
		x86emuOp_ret_far_IMM(emu);
		break;
	case 0xcb:
		x86emuOp_ret_far(emu);
		break;
	case 0xcc:
		x86emuOp_int3(emu);
		break;
	case 0xcd:
		x86emuOp_int_IMM(emu);
		break;
	case 0xce:
		x86emuOp_into(emu);
		break;
	case 0xcf:
		x86emuOp_iret(emu);
		break;

	case 0xd0:
		x86emuOp_opcD0_byte_RM_1(emu);
		break;
	case 0xd1:
		x86emuOp_opcD1_word_RM_1(emu);
		break;
	case 0xd2:
		x86emuOp_opcD2_byte_RM_CL(emu);
		break;
	case 0xd3:
		x86emuOp_opcD3_word_RM_CL(emu);
		break;
	case 0xd4:
		x86emuOp_aam(emu);
		break;
	case 0xd5:
		x86emuOp_aad(emu);
		break;
	/* 0xd6 Undocumented SETALC instruction */
	case 0xd7:
		x86emuOp_xlat(emu);
		break;
	case 0xd8:
		x86emuOp_esc_coprocess_d8(emu);
		break;
	case 0xd9:
		x86emuOp_esc_coprocess_d9(emu);
		break;
	case 0xda:
		x86emuOp_esc_coprocess_da(emu);
		break;
	case 0xdb:
		x86emuOp_esc_coprocess_db(emu);
		break;
	case 0xdc:
		x86emuOp_esc_coprocess_dc(emu);
		break;
	case 0xdd:
		x86emuOp_esc_coprocess_dd(emu);
		break;
	case 0xde:
		x86emuOp_esc_coprocess_de(emu);
		break;
	case 0xdf:
		x86emuOp_esc_coprocess_df(emu);
		break;

	case 0xe0:
		x86emuOp_loopne(emu);
		break;
	case 0xe1:
		x86emuOp_loope(emu);
		break;
	case 0xe2:
		x86emuOp_loop(emu);
		break;
	case 0xe3:
		x86emuOp_jcxz(emu);
		break;
	case 0xe4:
		x86emuOp_in_byte_AL_IMM(emu);
		break;
	case 0xe5:
		x86emuOp_in_word_AX_IMM(emu);
		break;
	case 0xe6:
		x86emuOp_out_byte_IMM_AL(emu);
		break;
	case 0xe7:
		x86emuOp_out_word_IMM_AX(emu);
		break;

	case 0xe8:
		x86emuOp_call_near_IMM(emu);
		break;
	case 0xe9:
		x86emuOp_jump_near_IMM(emu);
		break;
	case 0xea:
		x86emuOp_jump_far_IMM(emu);
		break;
	case 0xeb:
		x86emuOp_jump_byte_IMM(emu);
		break;
	case 0xec:
		x86emuOp_in_byte_AL_DX(emu);
		break;
	case 0xed:
		x86emuOp_in_word_AX_DX(emu);
		break;
	case 0xee:
		x86emuOp_out_byte_DX_AL(emu);
		break;
	case 0xef:
		x86emuOp_out_word_DX_AX(emu);
		break;

	case 0xf0:
		x86emuOp_lock(emu);
		break;
	case 0xf2:
		emu->x86.mode |= SYSMODE_PREFIX_REPNE;
		break;
	case 0xf3:
		emu->x86.mode |= SYSMODE_PREFIX_REPE;
		break;
	case 0xf4:
		x86emu_halt_sys(emu);
		break;
	case 0xf5:
		x86emuOp_cmc(emu);
		break;
	case 0xf6:
		x86emuOp_opcF6_byte_RM(emu);
		break;
	case 0xf7:
		x86emuOp_opcF7_word_RM(emu);
		break;

	case 0xf8:
		CLEAR_FLAG(F_CF);
		break;
	case 0xf9:
		SET_FLAG(F_CF);
		break;
	case 0xfa:
		CLEAR_FLAG(F_IF);
		break;
	case 0xfb:
		SET_FLAG(F_IF);
		break;
	case 0xfc:
		CLEAR_FLAG(F_DF);
		break;
	case 0xfd:
		SET_FLAG(F_DF);
		break;
	case 0xfe:
		x86emuOp_opcFE_byte_RM(emu);
		break;
	case 0xff:
		x86emuOp_opcFF_word_RM(emu);
		break;
	default:
		x86emu_halt_sys(emu);
		break;
	}
	if (op1 != 0x26 && op1 != 0x2e && op1 != 0x36 && op1 != 0x3e &&
	    (op1 | 3) != 0x67)
		emu->x86.mode &= ~SYSMODE_CLRMASK;
}

static void
common_jmp_long(struct x86emu *emu, int cond)
{
	int16_t target;

	target = (int16_t) fetch_word_imm(emu);
	target += (int16_t) emu->x86.R_IP;
	if (cond)
		emu->x86.R_IP = (uint16_t) target;
}

static void
common_set_byte(struct x86emu *emu, int cond)
{
	uint32_t destoffset;
	uint8_t *destreg, destval;

	fetch_decode_modrm(emu);
	destval = cond ? 0x01 : 0x00;
	if (emu->cur_mod != 3) {
		destoffset = decode_rl_address(emu);
		store_data_byte(emu, destoffset, destval);
	} else {
		destreg = decode_rl_byte_register(emu);
		*destreg = destval;
	}
}

static void
common_bitstring32(struct x86emu *emu, int op)
{
	int bit;
	uint32_t srcval, *shiftreg, mask;

	fetch_decode_modrm(emu);
	shiftreg = decode_rh_long_register(emu);
	srcval = decode_and_fetch_long_disp(emu, (int16_t) *shiftreg >> 5);
	bit = *shiftreg & 0x1F;
	mask =  0x1 << bit;
	CONDITIONAL_SET_FLAG(srcval & mask, F_CF);

	switch (op) {
	case 0:
		break;
	case 1:
		write_back_long(emu, srcval | mask);
		break;
	case 2:
		write_back_long(emu, srcval & ~mask);
		break;
	case 3:
		write_back_long(emu, srcval ^ mask);
		break;
	}
}

static void
common_bitstring16(struct x86emu *emu, int op)
{
	int bit;
	uint16_t srcval, *shiftreg, mask;

	fetch_decode_modrm(emu);
	shiftreg = decode_rh_word_register(emu);
	srcval = decode_and_fetch_word_disp(emu, (int16_t) *shiftreg >> 4);
	bit = *shiftreg & 0xF;
	mask =  0x1 << bit;
	CONDITIONAL_SET_FLAG(srcval & mask, F_CF);

	switch (op) {
	case 0:
		break;
	case 1:
		write_back_word(emu, srcval | mask);
		break;
	case 2:
		write_back_word(emu, srcval & ~mask);
		break;
	case 3:
		write_back_word(emu, srcval ^ mask);
		break;
	}
}

static void
common_bitstring(struct x86emu *emu, int op)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		common_bitstring32(emu, op);
	else
		common_bitstring16(emu, op);
}

static void
common_bitsearch32(struct x86emu *emu, int diff)
{
	uint32_t srcval, *dstreg;

	fetch_decode_modrm(emu);
	dstreg = decode_rh_long_register(emu);
	srcval = decode_and_fetch_long(emu);
	CONDITIONAL_SET_FLAG(srcval == 0, F_ZF);
	for (*dstreg = 0; *dstreg < 32; *dstreg += diff) {
		if ((srcval >> *dstreg) & 1)
			break;
	}
}

static void
common_bitsearch16(struct x86emu *emu, int diff)
{
	uint16_t srcval, *dstreg;

	fetch_decode_modrm(emu);
	dstreg = decode_rh_word_register(emu);
	srcval = decode_and_fetch_word(emu);
	CONDITIONAL_SET_FLAG(srcval == 0, F_ZF);
	for (*dstreg = 0; *dstreg < 16; *dstreg += diff) {
		if ((srcval >> *dstreg) & 1)
			break;
	}
}

static void
common_bitsearch(struct x86emu *emu, int diff)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		common_bitsearch32(emu, diff);
	else
		common_bitsearch16(emu, diff);
}

static void
common_shift32(struct x86emu *emu, int shift_left, int use_cl)
{
	uint8_t shift;
	uint32_t destval, *shiftreg;

	fetch_decode_modrm(emu);
	shiftreg = decode_rh_long_register(emu);
	if (use_cl) {
		destval = decode_and_fetch_long(emu);
		shift = emu->x86.R_CL;
	} else {
		destval = decode_and_fetch_long_imm8(emu, &shift);
	}
	if (shift_left)
		destval = shld_long(emu, destval, *shiftreg, shift);
	else
		destval = shrd_long(emu, destval, *shiftreg, shift);
	write_back_long(emu, destval);
}

static void
common_shift16(struct x86emu *emu, int shift_left, int use_cl)
{
	uint8_t shift;
	uint16_t destval, *shiftreg;

	fetch_decode_modrm(emu);
	shiftreg = decode_rh_word_register(emu);
	if (use_cl) {
		destval = decode_and_fetch_word(emu);
		shift = emu->x86.R_CL;
	} else {
		destval = decode_and_fetch_word_imm8(emu, &shift);
	}
	if (shift_left)
		destval = shld_word(emu, destval, *shiftreg, shift);
	else
		destval = shrd_word(emu, destval, *shiftreg, shift);
	write_back_word(emu, destval);
}

static void
common_shift(struct x86emu *emu, int shift_left, int use_cl)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		common_shift32(emu, shift_left, use_cl);
	else
		common_shift16(emu, shift_left, use_cl);
}

/*
 * Implementation
 */
#define xorl(a,b)   ((a) && !(b)) || (!(a) && (b))


/*
 * REMARKS:
 * Handles opcode 0x0f,0x31
 */
static void
x86emuOp2_rdtsc(struct x86emu *emu)
{
	emu->x86.R_EAX = emu->cur_cycles & 0xffffffff;
	emu->x86.R_EDX = emu->cur_cycles >> 32;
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xa0
 */
static void
x86emuOp2_push_FS(struct x86emu *emu)
{
	push_word(emu, emu->x86.R_FS);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xa1
 */
static void
x86emuOp2_pop_FS(struct x86emu *emu)
{
	emu->x86.R_FS = pop_word(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xa1
 */
#if defined(__i386__) || defined(__amd64__)
static void
hw_cpuid(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
	__asm__ volatile("cpuid"
			     : "=a" (*a), "=b" (*b),
			       "=c" (*c), "=d" (*d)
			     : "a" (*a), "c" (*c)
			     : "cc");
}
#endif
static void
x86emuOp2_cpuid(struct x86emu *emu)
{
#if defined(__i386__) || defined(__amd64__)
	hw_cpuid(&emu->x86.R_EAX, &emu->x86.R_EBX, &emu->x86.R_ECX,
	    &emu->x86.R_EDX);
#endif
	switch (emu->x86.R_EAX) {
	case 0:
		emu->x86.R_EAX = 1;
#if !defined(__i386__) && !defined(__amd64__)
		/* "GenuineIntel" */
		emu->x86.R_EBX = 0x756e6547;
		emu->x86.R_EDX = 0x49656e69;
		emu->x86.R_ECX = 0x6c65746e;
#endif
		break;
	case 1:
#if !defined(__i386__) && !defined(__amd64__)
		emu->x86.R_EAX = 0x00000480;
		emu->x86.R_EBX = emu->x86.R_ECX = 0;
		emu->x86.R_EDX = 0x00000002;
#else
		emu->x86.R_EDX &= 0x00000012;
#endif
		break;
	default:
		emu->x86.R_EAX = emu->x86.R_EBX = emu->x86.R_ECX =
		    emu->x86.R_EDX = 0;
		break;
	}
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xa3
 */
static void
x86emuOp2_bt_R(struct x86emu *emu)
{
	common_bitstring(emu, 0);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xa4
 */
static void
x86emuOp2_shld_IMM(struct x86emu *emu)
{
	common_shift(emu, 1, 0);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xa5
 */
static void
x86emuOp2_shld_CL(struct x86emu *emu)
{
	common_shift(emu, 1, 1);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xa8
 */
static void
x86emuOp2_push_GS(struct x86emu *emu)
{
	push_word(emu, emu->x86.R_GS);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xa9
 */
static void
x86emuOp2_pop_GS(struct x86emu *emu)
{
	emu->x86.R_GS = pop_word(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xab
 */
static void
x86emuOp2_bts_R(struct x86emu *emu)
{
	common_bitstring(emu, 1);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xac
 */
static void
x86emuOp2_shrd_IMM(struct x86emu *emu)
{
	common_shift(emu, 0, 0);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xad
 */
static void
x86emuOp2_shrd_CL(struct x86emu *emu)
{
	common_shift(emu, 0, 1);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xaf
 */
static void
x86emuOp2_32_imul_R_RM(struct x86emu *emu)
{
	uint32_t *destreg, srcval;
	uint64_t res;

	fetch_decode_modrm(emu);
	destreg = decode_rh_long_register(emu);
	srcval = decode_and_fetch_long(emu);
	res = (int32_t) *destreg * (int32_t)srcval;
	if (res > 0xffffffff) {
		SET_FLAG(F_CF);
		SET_FLAG(F_OF);
	} else {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
	}
	*destreg = (uint32_t) res;
}

static void
x86emuOp2_16_imul_R_RM(struct x86emu *emu)
{
	uint16_t *destreg, srcval;
	uint32_t res;

	fetch_decode_modrm(emu);
	destreg = decode_rh_word_register(emu);
	srcval = decode_and_fetch_word(emu);
	res = (int16_t) * destreg * (int16_t)srcval;
	if (res > 0xFFFF) {
		SET_FLAG(F_CF);
		SET_FLAG(F_OF);
	} else {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
	}
	*destreg = (uint16_t) res;
}

static void
x86emuOp2_imul_R_RM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp2_32_imul_R_RM(emu);
	else
		x86emuOp2_16_imul_R_RM(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xb2
 */
static void
x86emuOp2_lss_R_IMM(struct x86emu *emu)
{
	common_load_far_pointer(emu, &emu->x86.R_SS);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xb3
 */
static void
x86emuOp2_btr_R(struct x86emu *emu)
{
	common_bitstring(emu, 2);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xb4
 */
static void
x86emuOp2_lfs_R_IMM(struct x86emu *emu)
{
	common_load_far_pointer(emu, &emu->x86.R_FS);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xb5
 */
static void
x86emuOp2_lgs_R_IMM(struct x86emu *emu)
{
	common_load_far_pointer(emu, &emu->x86.R_GS);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xb6
 */
static void
x86emuOp2_32_movzx_byte_R_RM(struct x86emu *emu)
{
	uint32_t *destreg;

	fetch_decode_modrm(emu);
	destreg = decode_rh_long_register(emu);
	*destreg = decode_and_fetch_byte(emu);
}

static void
x86emuOp2_16_movzx_byte_R_RM(struct x86emu *emu)
{
	uint16_t *destreg;

	fetch_decode_modrm(emu);
	destreg = decode_rh_word_register(emu);
	*destreg = decode_and_fetch_byte(emu);
}

static void
x86emuOp2_movzx_byte_R_RM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp2_32_movzx_byte_R_RM(emu);
	else
		x86emuOp2_16_movzx_byte_R_RM(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xb7
 */
static void
x86emuOp2_movzx_word_R_RM(struct x86emu *emu)
{
	uint32_t *destreg;

	fetch_decode_modrm(emu);
	destreg = decode_rh_long_register(emu);
	*destreg = decode_and_fetch_word(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xba
 */
static void
x86emuOp2_32_btX_I(struct x86emu *emu)
{
	int bit;
	uint32_t srcval, mask;
	uint8_t shift;

	fetch_decode_modrm(emu);
	if (emu->cur_rh < 4)
		x86emu_halt_sys(emu);

	srcval = decode_and_fetch_long_imm8(emu, &shift);
	bit = shift & 0x1F;
	mask = (0x1 << bit);

	switch (emu->cur_rh) {
	case 5:
		write_back_long(emu, srcval | mask);
		break;
	case 6:
		write_back_long(emu, srcval & ~mask);
		break;
	case 7:
		write_back_long(emu, srcval ^ mask);
		break;
	}
	CONDITIONAL_SET_FLAG(srcval & mask, F_CF);
}

static void
x86emuOp2_16_btX_I(struct x86emu *emu)
{
	int bit;

	uint16_t srcval, mask;
	uint8_t shift;

	fetch_decode_modrm(emu);
	if (emu->cur_rh < 4)
		x86emu_halt_sys(emu);

	srcval = decode_and_fetch_word_imm8(emu, &shift);
	bit = shift & 0xF;
	mask = (0x1 << bit);
	switch (emu->cur_rh) {
	case 5:
		write_back_word(emu, srcval | mask);
		break;
	case 6:
		write_back_word(emu, srcval & ~mask);
		break;
	case 7:
		write_back_word(emu, srcval ^ mask);
		break;
	}
	CONDITIONAL_SET_FLAG(srcval & mask, F_CF);
}

static void
x86emuOp2_btX_I(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp2_32_btX_I(emu);
	else
		x86emuOp2_16_btX_I(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xbb
 */
static void
x86emuOp2_btc_R(struct x86emu *emu)
{
	common_bitstring(emu, 3);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xbc
 */
static void
x86emuOp2_bsf(struct x86emu *emu)
{
	common_bitsearch(emu, +1);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xbd
 */
static void
x86emuOp2_bsr(struct x86emu *emu)
{
	common_bitsearch(emu, -1);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xbe
 */
static void
x86emuOp2_32_movsx_byte_R_RM(struct x86emu *emu)
{
	uint32_t *destreg;

	fetch_decode_modrm(emu);
	destreg = decode_rh_long_register(emu);
	*destreg = (int32_t)(int8_t)decode_and_fetch_byte(emu);
}

static void
x86emuOp2_16_movsx_byte_R_RM(struct x86emu *emu)
{
	uint16_t *destreg;

	fetch_decode_modrm(emu);
	destreg = decode_rh_word_register(emu);
	*destreg = (int16_t)(int8_t)decode_and_fetch_byte(emu);
}

static void
x86emuOp2_movsx_byte_R_RM(struct x86emu *emu)
{
	if (emu->x86.mode & SYSMODE_PREFIX_DATA)
		x86emuOp2_32_movsx_byte_R_RM(emu);
	else
		x86emuOp2_16_movsx_byte_R_RM(emu);
}

/*
 * REMARKS:
 * Handles opcode 0x0f,0xbf
 */
static void
x86emuOp2_movsx_word_R_RM(struct x86emu *emu)
{
	uint32_t *destreg;

	fetch_decode_modrm(emu);
	destreg = decode_rh_long_register(emu);
	*destreg = (int32_t)(int16_t)decode_and_fetch_word(emu);
}

static void
x86emu_exec_two_byte(struct x86emu * emu)
{
	uint8_t op2;

	op2 = fetch_byte_imm(emu);

	switch (op2) {
	/* 0x00 Group F (ring 0 PM)      */
	/* 0x01 Group G (ring 0 PM)      */
	/* 0x02 lar (ring 0 PM)          */
	/* 0x03 lsl (ring 0 PM)          */
	/* 0x05 loadall (undocumented)   */
	/* 0x06 clts (ring 0 PM)         */
	/* 0x07 loadall (undocumented)   */
	/* 0x08 invd (ring 0 PM)         */
	/* 0x09 wbinvd (ring 0 PM)       */

	/* 0x20 mov reg32(op2); break;creg (ring 0 PM) */
	/* 0x21 mov reg32(op2); break;dreg (ring 0 PM) */
	/* 0x22 mov creg(op2); break;reg32 (ring 0 PM) */
	/* 0x23 mov dreg(op2); break;reg32 (ring 0 PM) */
	/* 0x24 mov reg32(op2); break;treg (ring 0 PM) */
	/* 0x26 mov treg(op2); break;reg32 (ring 0 PM) */

	case 0x31:
		x86emuOp2_rdtsc(emu);
		break;

	case 0x80:
		common_jmp_long(emu, ACCESS_FLAG(F_OF));
		break;
	case 0x81:
		common_jmp_long(emu, !ACCESS_FLAG(F_OF));
		break;
	case 0x82:
		common_jmp_long(emu, ACCESS_FLAG(F_CF));
		break;
	case 0x83:
		common_jmp_long(emu, !ACCESS_FLAG(F_CF));
		break;
	case 0x84:
		common_jmp_long(emu, ACCESS_FLAG(F_ZF));
		break;
	case 0x85:
		common_jmp_long(emu, !ACCESS_FLAG(F_ZF));
		break;
	case 0x86:
		common_jmp_long(emu, ACCESS_FLAG(F_CF) || ACCESS_FLAG(F_ZF));
		break;
	case 0x87:
		common_jmp_long(emu, !(ACCESS_FLAG(F_CF) || ACCESS_FLAG(F_ZF)));
		break;
	case 0x88:
		common_jmp_long(emu, ACCESS_FLAG(F_SF));
		break;
	case 0x89:
		common_jmp_long(emu, !ACCESS_FLAG(F_SF));
		break;
	case 0x8a:
		common_jmp_long(emu, ACCESS_FLAG(F_PF));
		break;
	case 0x8b:
		common_jmp_long(emu, !ACCESS_FLAG(F_PF));
		break;
	case 0x8c:
		common_jmp_long(emu, xorl(ACCESS_FLAG(F_SF),
		    ACCESS_FLAG(F_OF)));
		break;
	case 0x8d:
		common_jmp_long(emu, !(xorl(ACCESS_FLAG(F_SF),
		    ACCESS_FLAG(F_OF))));
		break;
	case 0x8e:
		common_jmp_long(emu, (xorl(ACCESS_FLAG(F_SF), ACCESS_FLAG(F_OF))
		    || ACCESS_FLAG(F_ZF)));
		break;
	case 0x8f:
		common_jmp_long(emu, 
		    !(xorl(ACCESS_FLAG(F_SF), ACCESS_FLAG(F_OF)) ||
		    ACCESS_FLAG(F_ZF)));
		break;

	case 0x90:
		common_set_byte(emu, ACCESS_FLAG(F_OF));
		break;
	case 0x91:
		common_set_byte(emu, !ACCESS_FLAG(F_OF));
		break;
	case 0x92:
		common_set_byte(emu, ACCESS_FLAG(F_CF));
		break;
	case 0x93:
		common_set_byte(emu, !ACCESS_FLAG(F_CF));
		break;
	case 0x94:
		common_set_byte(emu, ACCESS_FLAG(F_ZF));
		break;
	case 0x95:
		common_set_byte(emu, !ACCESS_FLAG(F_ZF));
		break;
	case 0x96:
		common_set_byte(emu, ACCESS_FLAG(F_CF) || ACCESS_FLAG(F_ZF));
		break;
	case 0x97:
		common_set_byte(emu, !(ACCESS_FLAG(F_CF) || ACCESS_FLAG(F_ZF)));
		break;
	case 0x98:
		common_set_byte(emu, ACCESS_FLAG(F_SF));
		break;
	case 0x99:
		common_set_byte(emu, !ACCESS_FLAG(F_SF));
		break;
	case 0x9a:
		common_set_byte(emu, ACCESS_FLAG(F_PF));
		break;
	case 0x9b:
		common_set_byte(emu, !ACCESS_FLAG(F_PF));
		break;
	case 0x9c:
		common_set_byte(emu, xorl(ACCESS_FLAG(F_SF),
		    ACCESS_FLAG(F_OF)));
		break;
	case 0x9d:
		common_set_byte(emu, xorl(ACCESS_FLAG(F_SF),
		    ACCESS_FLAG(F_OF)));
		break;
	case 0x9e:
		common_set_byte(emu,
		    (xorl(ACCESS_FLAG(F_SF), ACCESS_FLAG(F_OF)) ||
		    ACCESS_FLAG(F_ZF)));
		break;
	case 0x9f:
		common_set_byte(emu,
		    !(xorl(ACCESS_FLAG(F_SF), ACCESS_FLAG(F_OF)) ||
		    ACCESS_FLAG(F_ZF)));
		break;

	case 0xa0:
		x86emuOp2_push_FS(emu);
		break;
	case 0xa1:
		x86emuOp2_pop_FS(emu);
		break;
	case 0xa2:
		x86emuOp2_cpuid(emu);
		break;
	case 0xa3:
		x86emuOp2_bt_R(emu);
		break;
	case 0xa4:
		x86emuOp2_shld_IMM(emu);
		break;
	case 0xa5:
		x86emuOp2_shld_CL(emu);
		break;
	case 0xa8:
		x86emuOp2_push_GS(emu);
		break;
	case 0xa9:
		x86emuOp2_pop_GS(emu);
		break;
	case 0xab:
		x86emuOp2_bts_R(emu);
		break;
	case 0xac:
		x86emuOp2_shrd_IMM(emu);
		break;
	case 0xad:
		x86emuOp2_shrd_CL(emu);
		break;
	case 0xaf:
		x86emuOp2_imul_R_RM(emu);
		break;

	/* 0xb0 TODO: cmpxchg */
	/* 0xb1 TODO: cmpxchg */
	case 0xb2:
		x86emuOp2_lss_R_IMM(emu);
		break;
	case 0xb3:
		x86emuOp2_btr_R(emu);
		break;
	case 0xb4:
		x86emuOp2_lfs_R_IMM(emu);
		break;
	case 0xb5:
		x86emuOp2_lgs_R_IMM(emu);
		break;
	case 0xb6:
		x86emuOp2_movzx_byte_R_RM(emu);
		break;
	case 0xb7:
		x86emuOp2_movzx_word_R_RM(emu);
		break;
	case 0xba:
		x86emuOp2_btX_I(emu);
		break;
	case 0xbb:
		x86emuOp2_btc_R(emu);
		break;
	case 0xbc:
		x86emuOp2_bsf(emu);
		break;
	case 0xbd:
		x86emuOp2_bsr(emu);
		break;
	case 0xbe:
		x86emuOp2_movsx_byte_R_RM(emu);
		break;
	case 0xbf:
		x86emuOp2_movsx_word_R_RM(emu);
		break;

	/* 0xc0 TODO: xadd */
	/* 0xc1 TODO: xadd */
	/* 0xc8 TODO: bswap */
	/* 0xc9 TODO: bswap */
	/* 0xca TODO: bswap */
	/* 0xcb TODO: bswap */
	/* 0xcc TODO: bswap */
	/* 0xcd TODO: bswap */
	/* 0xce TODO: bswap */
	/* 0xcf TODO: bswap */

	default:
		x86emu_halt_sys(emu);
		break;
	}
}

/*
 * Carry Chain Calculation
 *
 * This represents a somewhat expensive calculation which is
 * apparently required to emulate the setting of the OF and AF flag.
 * The latter is not so important, but the former is.  The overflow
 * flag is the XOR of the top two bits of the carry chain for an
 * addition (similar for subtraction).  Since we do not want to
 * simulate the addition in a bitwise manner, we try to calculate the
 * carry chain given the two operands and the result.
 *
 * So, given the following table, which represents the addition of two
 * bits, we can derive a formula for the carry chain.
 *
 * a   b   cin   r     cout
 * 0   0   0     0     0
 * 0   0   1     1     0
 * 0   1   0     1     0
 * 0   1   1     0     1
 * 1   0   0     1     0
 * 1   0   1     0     1
 * 1   1   0     0     1
 * 1   1   1     1     1
 *
 * Construction of table for cout:
 *
 * ab
 * r  \  00   01   11  10
 * |------------------
 * 0  |   0    1    1   1
 * 1  |   0    0    1   0
 *
 * By inspection, one gets:  cc = ab +  r'(a + b)
 *
 * That represents alot of operations, but NO CHOICE....
 *
 * Borrow Chain Calculation.
 *
 * The following table represents the subtraction of two bits, from
 * which we can derive a formula for the borrow chain.
 *
 * a   b   bin   r     bout
 * 0   0   0     0     0
 * 0   0   1     1     1
 * 0   1   0     1     1
 * 0   1   1     0     1
 * 1   0   0     1     0
 * 1   0   1     0     0
 * 1   1   0     0     0
 * 1   1   1     1     1
 *
 * Construction of table for cout:
 *
 * ab
 * r  \  00   01   11  10
 * |------------------
 * 0  |   0    1    0   0
 * 1  |   1    1    1   0
 *
 * By inspection, one gets:  bc = a'b +  r(a' + b)
 *
 */

/*
 * Global Variables
 */

static uint32_t x86emu_parity_tab[8] =
{
	0x96696996,
	0x69969669,
	0x69969669,
	0x96696996,
	0x69969669,
	0x96696996,
	0x96696996,
	0x69969669,
};
#define PARITY(x)   (((x86emu_parity_tab[(x) / 32] >> ((x) % 32)) & 1) == 0)
#define XOR2(x) 	(((x) ^ ((x)>>1)) & 0x1)


/*
 * REMARKS:
 * Implements the AAA instruction and side effects.
 */
static uint16_t 
aaa_word(struct x86emu *emu, uint16_t d)
{
	uint16_t res;
	if ((d & 0xf) > 0x9 || ACCESS_FLAG(F_AF)) {
		d += 0x6;
		d += 0x100;
		SET_FLAG(F_AF);
		SET_FLAG(F_CF);
	} else {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_AF);
	}
	res = (uint16_t) (d & 0xFF0F);
	CLEAR_FLAG(F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	return res;
}

/*
 * REMARKS:
 * Implements the AAA instruction and side effects.
 */
static uint16_t 
aas_word(struct x86emu *emu, uint16_t d)
{
	uint16_t res;
	if ((d & 0xf) > 0x9 || ACCESS_FLAG(F_AF)) {
		d -= 0x6;
		d -= 0x100;
		SET_FLAG(F_AF);
		SET_FLAG(F_CF);
	} else {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_AF);
	}
	res = (uint16_t) (d & 0xFF0F);
	CLEAR_FLAG(F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	return res;
}

/*
 * REMARKS:
 * Implements the AAD instruction and side effects.
 */
static uint16_t 
aad_word(struct x86emu *emu, uint16_t d)
{
	uint16_t l;
	uint8_t hb, lb;

	hb = (uint8_t) ((d >> 8) & 0xff);
	lb = (uint8_t) ((d & 0xff));
	l = (uint16_t) ((lb + 10 * hb) & 0xFF);

	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	CLEAR_FLAG(F_OF);
	CONDITIONAL_SET_FLAG(l & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(l == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(l & 0xff), F_PF);
	return l;
}

/*
 * REMARKS:
 * Implements the AAM instruction and side effects.
 */
static uint16_t 
aam_word(struct x86emu *emu, uint8_t d)
{
	uint16_t h, l;

	h = (uint16_t) (d / 10);
	l = (uint16_t) (d % 10);
	l |= (uint16_t) (h << 8);

	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	CLEAR_FLAG(F_OF);
	CONDITIONAL_SET_FLAG(l & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(l == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(l & 0xff), F_PF);
	return l;
}

/*
 * REMARKS:
 * Implements the ADC instruction and side effects.
 */
static uint8_t 
adc_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t cc;

	if (ACCESS_FLAG(F_CF))
		res = 1 + d + s;
	else
		res = d + s;

	CONDITIONAL_SET_FLAG(res & 0x100, F_CF);
	CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the carry chain  SEE NOTE AT TOP. */
	cc = (s & d) | ((~res) & (s | d));
	CONDITIONAL_SET_FLAG(XOR2(cc >> 6), F_OF);
	CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the ADC instruction and side effects.
 */
static uint16_t 
adc_word(struct x86emu *emu, uint16_t d, uint16_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t cc;

	if (ACCESS_FLAG(F_CF))
		res = 1 + d + s;
	else
		res = d + s;

	CONDITIONAL_SET_FLAG(res & 0x10000, F_CF);
	CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the carry chain  SEE NOTE AT TOP. */
	cc = (s & d) | ((~res) & (s | d));
	CONDITIONAL_SET_FLAG(XOR2(cc >> 14), F_OF);
	CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the ADC instruction and side effects.
 */
static uint32_t 
adc_long(struct x86emu *emu, uint32_t d, uint32_t s)
{
	uint32_t lo;	/* all operands in native machine order */
	uint32_t hi;
	uint32_t res;
	uint32_t cc;

	if (ACCESS_FLAG(F_CF)) {
		lo = 1 + (d & 0xFFFF) + (s & 0xFFFF);
		res = 1 + d + s;
	} else {
		lo = (d & 0xFFFF) + (s & 0xFFFF);
		res = d + s;
	}
	hi = (lo >> 16) + (d >> 16) + (s >> 16);

	CONDITIONAL_SET_FLAG(hi & 0x10000, F_CF);
	CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the carry chain  SEE NOTE AT TOP. */
	cc = (s & d) | ((~res) & (s | d));
	CONDITIONAL_SET_FLAG(XOR2(cc >> 30), F_OF);
	CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the ADD instruction and side effects.
 */
static uint8_t 
add_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t cc;

	res = d + s;
	CONDITIONAL_SET_FLAG(res & 0x100, F_CF);
	CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the carry chain  SEE NOTE AT TOP. */
	cc = (s & d) | ((~res) & (s | d));
	CONDITIONAL_SET_FLAG(XOR2(cc >> 6), F_OF);
	CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the ADD instruction and side effects.
 */
static uint16_t 
add_word(struct x86emu *emu, uint16_t d, uint16_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t cc;

	res = d + s;
	CONDITIONAL_SET_FLAG(res & 0x10000, F_CF);
	CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the carry chain  SEE NOTE AT TOP. */
	cc = (s & d) | ((~res) & (s | d));
	CONDITIONAL_SET_FLAG(XOR2(cc >> 14), F_OF);
	CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the ADD instruction and side effects.
 */
static uint32_t 
add_long(struct x86emu *emu, uint32_t d, uint32_t s)
{
	uint32_t lo;	/* all operands in native machine order */
	uint32_t hi;
	uint32_t res;
	uint32_t cc;

	lo = (d & 0xFFFF) + (s & 0xFFFF);
	res = d + s;
	hi = (lo >> 16) + (d >> 16) + (s >> 16);

	CONDITIONAL_SET_FLAG(hi & 0x10000, F_CF);
	CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the carry chain  SEE NOTE AT TOP. */
	cc = (s & d) | ((~res) & (s | d));
	CONDITIONAL_SET_FLAG(XOR2(cc >> 30), F_OF);
	CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);

	return res;
}

/*
 * REMARKS:
 * Implements the AND instruction and side effects.
 */
static uint8_t 
and_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	uint8_t res;	/* all operands in native machine order */

	res = d & s;

	/* set the flags  */
	CLEAR_FLAG(F_OF);
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res), F_PF);
	return res;
}

/*
 * REMARKS:
 * Implements the AND instruction and side effects.
 */
static uint16_t 
and_word(struct x86emu *emu, uint16_t d, uint16_t s)
{
	uint16_t res;	/* all operands in native machine order */

	res = d & s;

	/* set the flags  */
	CLEAR_FLAG(F_OF);
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	return res;
}

/*
 * REMARKS:
 * Implements the AND instruction and side effects.
 */
static uint32_t 
and_long(struct x86emu *emu, uint32_t d, uint32_t s)
{
	uint32_t res;	/* all operands in native machine order */

	res = d & s;

	/* set the flags  */
	CLEAR_FLAG(F_OF);
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	return res;
}

/*
 * REMARKS:
 * Implements the CMP instruction and side effects.
 */
static uint8_t 
cmp_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	res = d - s;
	CLEAR_FLAG(F_CF);
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	bc = (res & (~d | s)) | (~d & s);
	CONDITIONAL_SET_FLAG(bc & 0x80, F_CF);
	CONDITIONAL_SET_FLAG(XOR2(bc >> 6), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return d;
}

static void 
cmp_byte_no_return(struct x86emu *emu, uint8_t d, uint8_t s)
{
	cmp_byte(emu, d, s);
}

/*
 * REMARKS:
 * Implements the CMP instruction and side effects.
 */
static uint16_t 
cmp_word(struct x86emu *emu, uint16_t d, uint16_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	res = d - s;
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	bc = (res & (~d | s)) | (~d & s);
	CONDITIONAL_SET_FLAG(bc & 0x8000, F_CF);
	CONDITIONAL_SET_FLAG(XOR2(bc >> 14), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return d;
}

static void 
cmp_word_no_return(struct x86emu *emu, uint16_t d, uint16_t s)
{
	cmp_word(emu, d, s);
}

/*
 * REMARKS:
 * Implements the CMP instruction and side effects.
 */
static uint32_t 
cmp_long(struct x86emu *emu, uint32_t d, uint32_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	res = d - s;
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	bc = (res & (~d | s)) | (~d & s);
	CONDITIONAL_SET_FLAG(bc & 0x80000000, F_CF);
	CONDITIONAL_SET_FLAG(XOR2(bc >> 30), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return d;
}

static void 
cmp_long_no_return(struct x86emu *emu, uint32_t d, uint32_t s)
{
	cmp_long(emu, d, s);
}

/*
 * REMARKS:
 * Implements the DAA instruction and side effects.
 */
static uint8_t 
daa_byte(struct x86emu *emu, uint8_t d)
{
	uint32_t res = d;
	if ((d & 0xf) > 9 || ACCESS_FLAG(F_AF)) {
		res += 6;
		SET_FLAG(F_AF);
	}
	if (res > 0x9F || ACCESS_FLAG(F_CF)) {
		res += 0x60;
		SET_FLAG(F_CF);
	}
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xFF) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the DAS instruction and side effects.
 */
static uint8_t 
das_byte(struct x86emu *emu, uint8_t d)
{
	if ((d & 0xf) > 9 || ACCESS_FLAG(F_AF)) {
		d -= 6;
		SET_FLAG(F_AF);
	}
	if (d > 0x9F || ACCESS_FLAG(F_CF)) {
		d -= 0x60;
		SET_FLAG(F_CF);
	}
	CONDITIONAL_SET_FLAG(d & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(d == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(d & 0xff), F_PF);
	return d;
}

/*
 * REMARKS:
 * Implements the DEC instruction and side effects.
 */
static uint8_t 
dec_byte(struct x86emu *emu, uint8_t d)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	res = d - 1;
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	/* based on sub_byte, uses s==1.  */
	bc = (res & (~d | 1)) | (~d & 1);
	/* carry flag unchanged */
	CONDITIONAL_SET_FLAG(XOR2(bc >> 6), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the DEC instruction and side effects.
 */
static uint16_t 
dec_word(struct x86emu *emu, uint16_t d)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	res = d - 1;
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	/* based on the sub_byte routine, with s==1 */
	bc = (res & (~d | 1)) | (~d & 1);
	/* carry flag unchanged */
	CONDITIONAL_SET_FLAG(XOR2(bc >> 14), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the DEC instruction and side effects.
 */
static uint32_t 
dec_long(struct x86emu *emu, uint32_t d)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	res = d - 1;

	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	bc = (res & (~d | 1)) | (~d & 1);
	/* carry flag unchanged */
	CONDITIONAL_SET_FLAG(XOR2(bc >> 30), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the INC instruction and side effects.
 */
static uint8_t 
inc_byte(struct x86emu *emu, uint8_t d)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t cc;

	res = d + 1;
	CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the carry chain  SEE NOTE AT TOP. */
	cc = ((1 & d) | (~res)) & (1 | d);
	CONDITIONAL_SET_FLAG(XOR2(cc >> 6), F_OF);
	CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the INC instruction and side effects.
 */
static uint16_t 
inc_word(struct x86emu *emu, uint16_t d)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t cc;

	res = d + 1;
	CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the carry chain  SEE NOTE AT TOP. */
	cc = (1 & d) | ((~res) & (1 | d));
	CONDITIONAL_SET_FLAG(XOR2(cc >> 14), F_OF);
	CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the INC instruction and side effects.
 */
static uint32_t 
inc_long(struct x86emu *emu, uint32_t d)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t cc;

	res = d + 1;
	CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the carry chain  SEE NOTE AT TOP. */
	cc = (1 & d) | ((~res) & (1 | d));
	CONDITIONAL_SET_FLAG(XOR2(cc >> 30), F_OF);
	CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the OR instruction and side effects.
 */
static uint8_t 
or_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	uint8_t res;	/* all operands in native machine order */

	res = d | s;
	CLEAR_FLAG(F_OF);
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res), F_PF);
	return res;
}

/*
 * REMARKS:
 * Implements the OR instruction and side effects.
 */
static uint16_t 
or_word(struct x86emu *emu, uint16_t d, uint16_t s)
{
	uint16_t res;	/* all operands in native machine order */

	res = d | s;
	/* set the carry flag to be bit 8 */
	CLEAR_FLAG(F_OF);
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	return res;
}

/*
 * REMARKS:
 * Implements the OR instruction and side effects.
 */
static uint32_t 
or_long(struct x86emu *emu, uint32_t d, uint32_t s)
{
	uint32_t res;	/* all operands in native machine order */

	res = d | s;

	/* set the carry flag to be bit 8 */
	CLEAR_FLAG(F_OF);
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	return res;
}

/*
 * REMARKS:
 * Implements the OR instruction and side effects.
 */
static uint8_t 
neg_byte(struct x86emu *emu, uint8_t s)
{
	uint8_t res;
	uint8_t bc;

	CONDITIONAL_SET_FLAG(s != 0, F_CF);
	res = (uint8_t) - s;
	CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res), F_PF);
	/* calculate the borrow chain --- modified such that d=0.
	 * substitutiing d=0 into     bc= res&(~d|s)|(~d&s); (the one used for
	 * sub) and simplifying, since ~d=0xff..., ~d|s == 0xffff..., and
	 * res&0xfff... == res.  Similarly ~d&s == s.  So the simplified
	 * result is: */
	bc = res | s;
	CONDITIONAL_SET_FLAG(XOR2(bc >> 6), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the OR instruction and side effects.
 */
static uint16_t 
neg_word(struct x86emu *emu, uint16_t s)
{
	uint16_t res;
	uint16_t bc;

	CONDITIONAL_SET_FLAG(s != 0, F_CF);
	res = (uint16_t) - s;
	CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain --- modified such that d=0.
	 * substitutiing d=0 into     bc= res&(~d|s)|(~d&s); (the one used for
	 * sub) and simplifying, since ~d=0xff..., ~d|s == 0xffff..., and
	 * res&0xfff... == res.  Similarly ~d&s == s.  So the simplified
	 * result is: */
	bc = res | s;
	CONDITIONAL_SET_FLAG(XOR2(bc >> 14), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the OR instruction and side effects.
 */
static uint32_t 
neg_long(struct x86emu *emu, uint32_t s)
{
	uint32_t res;
	uint32_t bc;

	CONDITIONAL_SET_FLAG(s != 0, F_CF);
	res = (uint32_t) - s;
	CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain --- modified such that d=0.
	 * substitutiing d=0 into     bc= res&(~d|s)|(~d&s); (the one used for
	 * sub) and simplifying, since ~d=0xff..., ~d|s == 0xffff..., and
	 * res&0xfff... == res.  Similarly ~d&s == s.  So the simplified
	 * result is: */
	bc = res | s;
	CONDITIONAL_SET_FLAG(XOR2(bc >> 30), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the RCL instruction and side effects.
 */
static uint8_t 
rcl_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	unsigned int res, cnt, mask, cf;

	/* s is the rotate distance.  It varies from 0 - 8. */
	/* have
	 * 
	 * CF  B_7 B_6 B_5 B_4 B_3 B_2 B_1 B_0
	 * 
	 * want to rotate through the carry by "s" bits.  We could loop, but
	 * that's inefficient.  So the width is 9, and we split into three
	 * parts:
	 * 
	 * The new carry flag   (was B_n) the stuff in B_n-1 .. B_0 the stuff
	 * in B_7 .. B_n+1
	 * 
	 * The new rotate is done mod 9, and given this, for a rotation of n
	 * bits (mod 9) the new carry flag is then located n bits from the MSB.
	 * The low part is then shifted up cnt bits, and the high part is or'd
	 * in.  Using CAPS for new values, and lowercase for the original
	 * values, this can be expressed as:
	 * 
	 * IF n > 0 1) CF <-  b_(8-n) 2) B_(7) .. B_(n)  <-  b_(8-(n+1)) .. b_0
	 * 3) B_(n-1) <- cf 4) B_(n-2) .. B_0 <-  b_7 .. b_(8-(n-1))
	 */
	res = d;
	if ((cnt = s % 9) != 0) {
		/* extract the new CARRY FLAG. */
		/* CF <-  b_(8-n)             */
		cf = (d >> (8 - cnt)) & 0x1;

		/* 
		 * Get the low stuff which rotated into the range B_7 .. B_cnt
		 * B_(7) .. B_(n)  <-  b_(8-(n+1)) .. b_0
		 * note that the right hand side done by the mask.
		 */
		res = (d << cnt) & 0xff;

		/* 
		 * now the high stuff which rotated around into the positions
		 * B_cnt-2 .. B_0
		 * B_(n-2) .. B_0 <-  b_7 .. b_(8-(n-1))
		 * shift it downward, 7-(n-2) = 9-n positions. and mask off
		 * the result before or'ing in.
		 */
		mask = (1 << (cnt - 1)) - 1;
		res |= (d >> (9 - cnt)) & mask;

		/* if the carry flag was set, or it in.  */
		if (ACCESS_FLAG(F_CF)) {	/* carry flag is set */
			/* B_(n-1) <- cf */
			res |= 1 << (cnt - 1);
		}
		/* set the new carry flag, based on the variable "cf" */
		CONDITIONAL_SET_FLAG(cf, F_CF);
		/* OVERFLOW is set *IFF* cnt==1, then it is the xor of CF and
		 * the most significant bit.  Blecck. */
		/* parenthesized this expression since it appears to be
		 * causing OF to be misset */
		CONDITIONAL_SET_FLAG(cnt == 1 && XOR2(cf + ((res >> 6) & 0x2)),
		    F_OF);

	}
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the RCL instruction and side effects.
 */
static uint16_t 
rcl_word(struct x86emu *emu, uint16_t d, uint8_t s)
{
	unsigned int res, cnt, mask, cf;

	res = d;
	if ((cnt = s % 17) != 0) {
		cf = (d >> (16 - cnt)) & 0x1;
		res = (d << cnt) & 0xffff;
		mask = (1 << (cnt - 1)) - 1;
		res |= (d >> (17 - cnt)) & mask;
		if (ACCESS_FLAG(F_CF)) {
			res |= 1 << (cnt - 1);
		}
		CONDITIONAL_SET_FLAG(cf, F_CF);
		CONDITIONAL_SET_FLAG(cnt == 1 && XOR2(cf + ((res >> 14) & 0x2)),
		    F_OF);
	}
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the RCL instruction and side effects.
 */
static uint32_t 
rcl_long(struct x86emu *emu, uint32_t d, uint8_t s)
{
	uint32_t res, cnt, mask, cf;

	res = d;
	if ((cnt = s % 33) != 0) {
		cf = (d >> (32 - cnt)) & 0x1;
		res = (d << cnt) & 0xffffffff;
		mask = (1 << (cnt - 1)) - 1;
		res |= (d >> (33 - cnt)) & mask;
		if (ACCESS_FLAG(F_CF)) {	/* carry flag is set */
			res |= 1 << (cnt - 1);
		}
		CONDITIONAL_SET_FLAG(cf, F_CF);
		CONDITIONAL_SET_FLAG(cnt == 1 && XOR2(cf + ((res >> 30) & 0x2)),
		    F_OF);
	}
	return res;
}

/*
 * REMARKS:
 * Implements the RCR instruction and side effects.
 */
static uint8_t 
rcr_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	uint32_t res, cnt;
	uint32_t mask, cf, ocf = 0;

	/* rotate right through carry */
	/* s is the rotate distance.  It varies from 0 - 8. d is the byte
	 * object rotated.
	 * 
	 * have
	 * 
	 * CF  B_7 B_6 B_5 B_4 B_3 B_2 B_1 B_0
	 * 
	 * The new rotate is done mod 9, and given this, for a rotation of n
	 * bits (mod 9) the new carry flag is then located n bits from the LSB.
	 * The low part is then shifted up cnt bits, and the high part is or'd
	 * in.  Using CAPS for new values, and lowercase for the original
	 * values, this can be expressed as:
	 * 
	 * IF n > 0 
	 *	1) CF <-  b_(n-1) 
	 *	2) B_(8-(n+1)) .. B_(0)  <-  b_(7) .. b_(n)
	 * 	3) B_(8-n) <- cf 4) B_(7) .. B_(8-(n-1)) <-  b_(n-2) .. b_(0)
	 */
	res = d;
	if ((cnt = s % 9) != 0) {
		/* extract the new CARRY FLAG. */
		/* CF <-  b_(n-1)              */
		if (cnt == 1) {
			cf = d & 0x1;
			/* note hackery here.  Access_flag(..) evaluates to
			 * either 0 if flag not set non-zero if flag is set.
			 * doing access_flag(..) != 0 casts that into either
			 * 0..1 in any representation of the flags register
			 * (i.e. packed bit array or unpacked.) */
			ocf = ACCESS_FLAG(F_CF) != 0;
		} else
			cf = (d >> (cnt - 1)) & 0x1;

		/* B_(8-(n+1)) .. B_(0)  <-  b_(7) .. b_n  */
		/* note that the right hand side done by the mask This is
		 * effectively done by shifting the object to the right.  The
		 * result must be masked, in case the object came in and was
		 * treated as a negative number.  Needed??? */

		mask = (1 << (8 - cnt)) - 1;
		res = (d >> cnt) & mask;

		/* now the high stuff which rotated around into the positions
		 * B_cnt-2 .. B_0 */
		/* B_(7) .. B_(8-(n-1)) <-  b_(n-2) .. b_(0) */
		/* shift it downward, 7-(n-2) = 9-n positions. and mask off
		 * the result before or'ing in. */
		res |= (d << (9 - cnt));

		/* if the carry flag was set, or it in.  */
		if (ACCESS_FLAG(F_CF)) {	/* carry flag is set */
			/* B_(8-n) <- cf */
			res |= 1 << (8 - cnt);
		}
		/* set the new carry flag, based on the variable "cf" */
		CONDITIONAL_SET_FLAG(cf, F_CF);
		/* OVERFLOW is set *IFF* cnt==1, then it is the xor of CF and
		 * the most significant bit.  Blecck. */
		/* parenthesized... */
		if (cnt == 1) {
			CONDITIONAL_SET_FLAG(XOR2(ocf + ((d >> 6) & 0x2)),
			    F_OF);
		}
	}
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the RCR instruction and side effects.
 */
static uint16_t 
rcr_word(struct x86emu *emu, uint16_t d, uint8_t s)
{
	uint32_t res, cnt;
	uint32_t mask, cf, ocf = 0;

	/* rotate right through carry */
	res = d;
	if ((cnt = s % 17) != 0) {
		if (cnt == 1) {
			cf = d & 0x1;
			ocf = ACCESS_FLAG(F_CF) != 0;
		} else
			cf = (d >> (cnt - 1)) & 0x1;
		mask = (1 << (16 - cnt)) - 1;
		res = (d >> cnt) & mask;
		res |= (d << (17 - cnt));
		if (ACCESS_FLAG(F_CF)) {
			res |= 1 << (16 - cnt);
		}
		CONDITIONAL_SET_FLAG(cf, F_CF);
		if (cnt == 1) {
			CONDITIONAL_SET_FLAG(XOR2(ocf + ((d >> 14) & 0x2)),
			    F_OF);
		}
	}
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the RCR instruction and side effects.
 */
static uint32_t 
rcr_long(struct x86emu *emu, uint32_t d, uint8_t s)
{
	uint32_t res, cnt;
	uint32_t mask, cf, ocf = 0;

	/* rotate right through carry */
	res = d;
	if ((cnt = s % 33) != 0) {
		if (cnt == 1) {
			cf = d & 0x1;
			ocf = ACCESS_FLAG(F_CF) != 0;
		} else
			cf = (d >> (cnt - 1)) & 0x1;
		mask = (1 << (32 - cnt)) - 1;
		res = (d >> cnt) & mask;
		if (cnt != 1)
			res |= (d << (33 - cnt));
		if (ACCESS_FLAG(F_CF)) {	/* carry flag is set */
			res |= 1 << (32 - cnt);
		}
		CONDITIONAL_SET_FLAG(cf, F_CF);
		if (cnt == 1) {
			CONDITIONAL_SET_FLAG(XOR2(ocf + ((d >> 30) & 0x2)),
			    F_OF);
		}
	}
	return res;
}

/*
 * REMARKS:
 * Implements the ROL instruction and side effects.
 */
static uint8_t 
rol_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	unsigned int res, cnt, mask;

	/* rotate left */
	/* s is the rotate distance.  It varies from 0 - 8. d is the byte
	 * object rotated.
	 * 
	 * have
	 * 
	 * CF  B_7 ... B_0
	 * 
	 * The new rotate is done mod 8. Much simpler than the "rcl" or "rcr"
	 * operations.
	 * 
	 * IF n > 0 1) B_(7) .. B_(n)  <-  b_(8-(n+1)) .. b_(0) 2) B_(n-1) ..
	 * B_(0) <-  b_(7) .. b_(8-n) */
	res = d;
	if ((cnt = s % 8) != 0) {
		/* B_(7) .. B_(n)  <-  b_(8-(n+1)) .. b_(0) */
		res = (d << cnt);

		/* B_(n-1) .. B_(0) <-  b_(7) .. b_(8-n) */
		mask = (1 << cnt) - 1;
		res |= (d >> (8 - cnt)) & mask;

		/* OVERFLOW is set *IFF* s==1, then it is the xor of CF and
		 * the most significant bit.  Blecck. */
		CONDITIONAL_SET_FLAG(s == 1 &&
		    XOR2((res & 0x1) + ((res >> 6) & 0x2)),
		    F_OF);
	}
	if (s != 0) {
		/* set the new carry flag, Note that it is the low order bit
		 * of the result!!!                               */
		CONDITIONAL_SET_FLAG(res & 0x1, F_CF);
	}
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the ROL instruction and side effects.
 */
static uint16_t 
rol_word(struct x86emu *emu, uint16_t d, uint8_t s)
{
	unsigned int res, cnt, mask;

	res = d;
	if ((cnt = s % 16) != 0) {
		res = (d << cnt);
		mask = (1 << cnt) - 1;
		res |= (d >> (16 - cnt)) & mask;
		CONDITIONAL_SET_FLAG(s == 1 &&
		    XOR2((res & 0x1) + ((res >> 14) & 0x2)),
		    F_OF);
	}
	if (s != 0) {
		/* set the new carry flag, Note that it is the low order bit
		 * of the result!!!                               */
		CONDITIONAL_SET_FLAG(res & 0x1, F_CF);
	}
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the ROL instruction and side effects.
 */
static uint32_t 
rol_long(struct x86emu *emu, uint32_t d, uint8_t s)
{
	uint32_t res, cnt, mask;

	res = d;
	if ((cnt = s % 32) != 0) {
		res = (d << cnt);
		mask = (1 << cnt) - 1;
		res |= (d >> (32 - cnt)) & mask;
		CONDITIONAL_SET_FLAG(s == 1 &&
		    XOR2((res & 0x1) + ((res >> 30) & 0x2)),
		    F_OF);
	}
	if (s != 0) {
		/* set the new carry flag, Note that it is the low order bit
		 * of the result!!!                               */
		CONDITIONAL_SET_FLAG(res & 0x1, F_CF);
	}
	return res;
}

/*
 * REMARKS:
 * Implements the ROR instruction and side effects.
 */
static uint8_t 
ror_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	unsigned int res, cnt, mask;

	/* rotate right */
	/* s is the rotate distance.  It varies from 0 - 8. d is the byte
	 * object rotated.
	 * 
	 * have
	 * 
	 * B_7 ... B_0
	 * 
	 * The rotate is done mod 8.
	 * 
	 * IF n > 0 1) B_(8-(n+1)) .. B_(0)  <-  b_(7) .. b_(n) 2) B_(7) ..
	 * B_(8-n) <-  b_(n-1) .. b_(0) */
	res = d;
	if ((cnt = s % 8) != 0) {	/* not a typo, do nada if cnt==0 */
		/* B_(7) .. B_(8-n) <-  b_(n-1) .. b_(0) */
		res = (d << (8 - cnt));

		/* B_(8-(n+1)) .. B_(0)  <-  b_(7) .. b_(n) */
		mask = (1 << (8 - cnt)) - 1;
		res |= (d >> (cnt)) & mask;

		/* OVERFLOW is set *IFF* s==1, then it is the xor of the two
		 * most significant bits.  Blecck. */
		CONDITIONAL_SET_FLAG(s == 1 && XOR2(res >> 6), F_OF);
	}
	if (s != 0) {
		/* set the new carry flag, Note that it is the high order bit
		 * of the result!!!                               */
		CONDITIONAL_SET_FLAG(res & 0x80, F_CF);
	}
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the ROR instruction and side effects.
 */
static uint16_t 
ror_word(struct x86emu *emu, uint16_t d, uint8_t s)
{
	unsigned int res, cnt, mask;

	res = d;
	if ((cnt = s % 16) != 0) {
		res = (d << (16 - cnt));
		mask = (1 << (16 - cnt)) - 1;
		res |= (d >> (cnt)) & mask;
		CONDITIONAL_SET_FLAG(s == 1 && XOR2(res >> 14), F_OF);
	}
	if (s != 0) {
		/* set the new carry flag, Note that it is the high order bit
		 * of the result!!!                               */
		CONDITIONAL_SET_FLAG(res & 0x8000, F_CF);
	}
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the ROR instruction and side effects.
 */
static uint32_t 
ror_long(struct x86emu *emu, uint32_t d, uint8_t s)
{
	uint32_t res, cnt, mask;

	res = d;
	if ((cnt = s % 32) != 0) {
		res = (d << (32 - cnt));
		mask = (1 << (32 - cnt)) - 1;
		res |= (d >> (cnt)) & mask;
		CONDITIONAL_SET_FLAG(s == 1 && XOR2(res >> 30), F_OF);
	}
	if (s != 0) {
		/* set the new carry flag, Note that it is the high order bit
		 * of the result!!!                               */
		CONDITIONAL_SET_FLAG(res & 0x80000000, F_CF);
	}
	return res;
}

/*
 * REMARKS:
 * Implements the SHL instruction and side effects.
 */
static uint8_t 
shl_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	unsigned int cnt, res, cf;

	if (s < 8) {
		cnt = s % 8;

		/* last bit shifted out goes into carry flag */
		if (cnt > 0) {
			res = d << cnt;
			cf = d & (1 << (8 - cnt));
			CONDITIONAL_SET_FLAG(cf, F_CF);
			CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
			CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
			CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		} else {
			res = (uint8_t) d;
		}

		if (cnt == 1) {
			/* Needs simplification. */
			CONDITIONAL_SET_FLAG(
			    (((res & 0x80) == 0x80) ^
				(ACCESS_FLAG(F_CF) != 0)),
			/* was (emu->x86.R_FLG&F_CF)==F_CF)), */
			    F_OF);
		} else {
			CLEAR_FLAG(F_OF);
		}
	} else {
		res = 0;
		CONDITIONAL_SET_FLAG((d << (s - 1)) & 0x80, F_CF);
		CLEAR_FLAG(F_OF);
		CLEAR_FLAG(F_SF);
		SET_FLAG(F_PF);
		SET_FLAG(F_ZF);
	}
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the SHL instruction and side effects.
 */
static uint16_t 
shl_word(struct x86emu *emu, uint16_t d, uint8_t s)
{
	unsigned int cnt, res, cf;

	if (s < 16) {
		cnt = s % 16;
		if (cnt > 0) {
			res = d << cnt;
			cf = d & (1 << (16 - cnt));
			CONDITIONAL_SET_FLAG(cf, F_CF);
			CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
			CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
			CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		} else {
			res = (uint16_t) d;
		}

		if (cnt == 1) {
			CONDITIONAL_SET_FLAG(
			    (((res & 0x8000) == 0x8000) ^
				(ACCESS_FLAG(F_CF) != 0)),
			    F_OF);
		} else {
			CLEAR_FLAG(F_OF);
		}
	} else {
		res = 0;
		CONDITIONAL_SET_FLAG((d << (s - 1)) & 0x8000, F_CF);
		CLEAR_FLAG(F_OF);
		CLEAR_FLAG(F_SF);
		SET_FLAG(F_PF);
		SET_FLAG(F_ZF);
	}
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the SHL instruction and side effects.
 */
static uint32_t 
shl_long(struct x86emu *emu, uint32_t d, uint8_t s)
{
	unsigned int cnt, res, cf;

	if (s < 32) {
		cnt = s % 32;
		if (cnt > 0) {
			res = d << cnt;
			cf = d & (1 << (32 - cnt));
			CONDITIONAL_SET_FLAG(cf, F_CF);
			CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
			CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
			CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		} else {
			res = d;
		}
		if (cnt == 1) {
			CONDITIONAL_SET_FLAG((((res & 0x80000000) == 0x80000000)
			    ^ (ACCESS_FLAG(F_CF) != 0)), F_OF);
		} else {
			CLEAR_FLAG(F_OF);
		}
	} else {
		res = 0;
		CONDITIONAL_SET_FLAG((d << (s - 1)) & 0x80000000, F_CF);
		CLEAR_FLAG(F_OF);
		CLEAR_FLAG(F_SF);
		SET_FLAG(F_PF);
		SET_FLAG(F_ZF);
	}
	return res;
}

/*
 * REMARKS:
 * Implements the SHR instruction and side effects.
 */
static uint8_t 
shr_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	unsigned int cnt, res, cf;

	if (s < 8) {
		cnt = s % 8;
		if (cnt > 0) {
			cf = d & (1 << (cnt - 1));
			res = d >> cnt;
			CONDITIONAL_SET_FLAG(cf, F_CF);
			CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
			CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
			CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		} else {
			res = (uint8_t) d;
		}

		if (cnt == 1) {
			CONDITIONAL_SET_FLAG(XOR2(res >> 6), F_OF);
		} else {
			CLEAR_FLAG(F_OF);
		}
	} else {
		res = 0;
		CONDITIONAL_SET_FLAG((d >> (s - 1)) & 0x1, F_CF);
		CLEAR_FLAG(F_OF);
		CLEAR_FLAG(F_SF);
		SET_FLAG(F_PF);
		SET_FLAG(F_ZF);
	}
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the SHR instruction and side effects.
 */
static uint16_t 
shr_word(struct x86emu *emu, uint16_t d, uint8_t s)
{
	unsigned int cnt, res, cf;

	if (s < 16) {
		cnt = s % 16;
		if (cnt > 0) {
			cf = d & (1 << (cnt - 1));
			res = d >> cnt;
			CONDITIONAL_SET_FLAG(cf, F_CF);
			CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
			CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
			CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		} else {
			res = d;
		}

		if (cnt == 1) {
			CONDITIONAL_SET_FLAG(XOR2(res >> 14), F_OF);
		} else {
			CLEAR_FLAG(F_OF);
		}
	} else {
		res = 0;
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
		SET_FLAG(F_ZF);
		CLEAR_FLAG(F_SF);
		CLEAR_FLAG(F_PF);
	}
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the SHR instruction and side effects.
 */
static uint32_t 
shr_long(struct x86emu *emu, uint32_t d, uint8_t s)
{
	unsigned int cnt, res, cf;

	if (s < 32) {
		cnt = s % 32;
		if (cnt > 0) {
			cf = d & (1 << (cnt - 1));
			res = d >> cnt;
			CONDITIONAL_SET_FLAG(cf, F_CF);
			CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
			CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
			CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		} else {
			res = d;
		}
		if (cnt == 1) {
			CONDITIONAL_SET_FLAG(XOR2(res >> 30), F_OF);
		} else {
			CLEAR_FLAG(F_OF);
		}
	} else {
		res = 0;
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
		SET_FLAG(F_ZF);
		CLEAR_FLAG(F_SF);
		CLEAR_FLAG(F_PF);
	}
	return res;
}

/*
 * REMARKS:
 * Implements the SAR instruction and side effects.
 */
static uint8_t 
sar_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	unsigned int cnt, res, cf, mask, sf;

	res = d;
	sf = d & 0x80;
	cnt = s % 8;
	if (cnt > 0 && cnt < 8) {
		mask = (1 << (8 - cnt)) - 1;
		cf = d & (1 << (cnt - 1));
		res = (d >> cnt) & mask;
		CONDITIONAL_SET_FLAG(cf, F_CF);
		if (sf) {
			res |= ~mask;
		}
		CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
		CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	} else if (cnt >= 8) {
		if (sf) {
			res = 0xff;
			SET_FLAG(F_CF);
			CLEAR_FLAG(F_ZF);
			SET_FLAG(F_SF);
			SET_FLAG(F_PF);
		} else {
			res = 0;
			CLEAR_FLAG(F_CF);
			SET_FLAG(F_ZF);
			CLEAR_FLAG(F_SF);
			CLEAR_FLAG(F_PF);
		}
	}
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the SAR instruction and side effects.
 */
static uint16_t 
sar_word(struct x86emu *emu, uint16_t d, uint8_t s)
{
	unsigned int cnt, res, cf, mask, sf;

	sf = d & 0x8000;
	cnt = s % 16;
	res = d;
	if (cnt > 0 && cnt < 16) {
		mask = (1 << (16 - cnt)) - 1;
		cf = d & (1 << (cnt - 1));
		res = (d >> cnt) & mask;
		CONDITIONAL_SET_FLAG(cf, F_CF);
		if (sf) {
			res |= ~mask;
		}
		CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
		CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
		CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	} else if (cnt >= 16) {
		if (sf) {
			res = 0xffff;
			SET_FLAG(F_CF);
			CLEAR_FLAG(F_ZF);
			SET_FLAG(F_SF);
			SET_FLAG(F_PF);
		} else {
			res = 0;
			CLEAR_FLAG(F_CF);
			SET_FLAG(F_ZF);
			CLEAR_FLAG(F_SF);
			CLEAR_FLAG(F_PF);
		}
	}
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the SAR instruction and side effects.
 */
static uint32_t 
sar_long(struct x86emu *emu, uint32_t d, uint8_t s)
{
	uint32_t cnt, res, cf, mask, sf;

	sf = d & 0x80000000;
	cnt = s % 32;
	res = d;
	if (cnt > 0 && cnt < 32) {
		mask = (1 << (32 - cnt)) - 1;
		cf = d & (1 << (cnt - 1));
		res = (d >> cnt) & mask;
		CONDITIONAL_SET_FLAG(cf, F_CF);
		if (sf) {
			res |= ~mask;
		}
		CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
		CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
		CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	} else if (cnt >= 32) {
		if (sf) {
			res = 0xffffffff;
			SET_FLAG(F_CF);
			CLEAR_FLAG(F_ZF);
			SET_FLAG(F_SF);
			SET_FLAG(F_PF);
		} else {
			res = 0;
			CLEAR_FLAG(F_CF);
			SET_FLAG(F_ZF);
			CLEAR_FLAG(F_SF);
			CLEAR_FLAG(F_PF);
		}
	}
	return res;
}

/*
 * REMARKS:
 * Implements the SHLD instruction and side effects.
 */
static uint16_t 
shld_word(struct x86emu *emu, uint16_t d, uint16_t fill, uint8_t s)
{
	unsigned int cnt, res, cf;

	if (s < 16) {
		cnt = s % 16;
		if (cnt > 0) {
			res = (d << cnt) | (fill >> (16 - cnt));
			cf = d & (1 << (16 - cnt));
			CONDITIONAL_SET_FLAG(cf, F_CF);
			CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
			CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
			CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		} else {
			res = d;
		}
		if (cnt == 1) {
			CONDITIONAL_SET_FLAG((((res & 0x8000) == 0x8000) ^
				(ACCESS_FLAG(F_CF) != 0)), F_OF);
		} else {
			CLEAR_FLAG(F_OF);
		}
	} else {
		res = 0;
		CONDITIONAL_SET_FLAG((d << (s - 1)) & 0x8000, F_CF);
		CLEAR_FLAG(F_OF);
		CLEAR_FLAG(F_SF);
		SET_FLAG(F_PF);
		SET_FLAG(F_ZF);
	}
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the SHLD instruction and side effects.
 */
static uint32_t 
shld_long(struct x86emu *emu, uint32_t d, uint32_t fill, uint8_t s)
{
	unsigned int cnt, res, cf;

	if (s < 32) {
		cnt = s % 32;
		if (cnt > 0) {
			res = (d << cnt) | (fill >> (32 - cnt));
			cf = d & (1 << (32 - cnt));
			CONDITIONAL_SET_FLAG(cf, F_CF);
			CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
			CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
			CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		} else {
			res = d;
		}
		if (cnt == 1) {
			CONDITIONAL_SET_FLAG((((res & 0x80000000) == 0x80000000)
			    ^ (ACCESS_FLAG(F_CF) != 0)), F_OF);
		} else {
			CLEAR_FLAG(F_OF);
		}
	} else {
		res = 0;
		CONDITIONAL_SET_FLAG((d << (s - 1)) & 0x80000000, F_CF);
		CLEAR_FLAG(F_OF);
		CLEAR_FLAG(F_SF);
		SET_FLAG(F_PF);
		SET_FLAG(F_ZF);
	}
	return res;
}

/*
 * REMARKS:
 * Implements the SHRD instruction and side effects.
 */
static uint16_t 
shrd_word(struct x86emu *emu, uint16_t d, uint16_t fill, uint8_t s)
{
	unsigned int cnt, res, cf;

	if (s < 16) {
		cnt = s % 16;
		if (cnt > 0) {
			cf = d & (1 << (cnt - 1));
			res = (d >> cnt) | (fill << (16 - cnt));
			CONDITIONAL_SET_FLAG(cf, F_CF);
			CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
			CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
			CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		} else {
			res = d;
		}

		if (cnt == 1) {
			CONDITIONAL_SET_FLAG(XOR2(res >> 14), F_OF);
		} else {
			CLEAR_FLAG(F_OF);
		}
	} else {
		res = 0;
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
		SET_FLAG(F_ZF);
		CLEAR_FLAG(F_SF);
		CLEAR_FLAG(F_PF);
	}
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the SHRD instruction and side effects.
 */
static uint32_t 
shrd_long(struct x86emu *emu, uint32_t d, uint32_t fill, uint8_t s)
{
	unsigned int cnt, res, cf;

	if (s < 32) {
		cnt = s % 32;
		if (cnt > 0) {
			cf = d & (1 << (cnt - 1));
			res = (d >> cnt) | (fill << (32 - cnt));
			CONDITIONAL_SET_FLAG(cf, F_CF);
			CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
			CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
			CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
		} else {
			res = d;
		}
		if (cnt == 1) {
			CONDITIONAL_SET_FLAG(XOR2(res >> 30), F_OF);
		} else {
			CLEAR_FLAG(F_OF);
		}
	} else {
		res = 0;
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
		SET_FLAG(F_ZF);
		CLEAR_FLAG(F_SF);
		CLEAR_FLAG(F_PF);
	}
	return res;
}

/*
 * REMARKS:
 * Implements the SBB instruction and side effects.
 */
static uint8_t 
sbb_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	if (ACCESS_FLAG(F_CF))
		res = d - s - 1;
	else
		res = d - s;
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	bc = (res & (~d | s)) | (~d & s);
	CONDITIONAL_SET_FLAG(bc & 0x80, F_CF);
	CONDITIONAL_SET_FLAG(XOR2(bc >> 6), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the SBB instruction and side effects.
 */
static uint16_t 
sbb_word(struct x86emu *emu, uint16_t d, uint16_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	if (ACCESS_FLAG(F_CF))
		res = d - s - 1;
	else
		res = d - s;
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	bc = (res & (~d | s)) | (~d & s);
	CONDITIONAL_SET_FLAG(bc & 0x8000, F_CF);
	CONDITIONAL_SET_FLAG(XOR2(bc >> 14), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the SBB instruction and side effects.
 */
static uint32_t 
sbb_long(struct x86emu *emu, uint32_t d, uint32_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	if (ACCESS_FLAG(F_CF))
		res = d - s - 1;
	else
		res = d - s;
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	bc = (res & (~d | s)) | (~d & s);
	CONDITIONAL_SET_FLAG(bc & 0x80000000, F_CF);
	CONDITIONAL_SET_FLAG(XOR2(bc >> 30), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the SUB instruction and side effects.
 */
static uint8_t 
sub_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	res = d - s;
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	bc = (res & (~d | s)) | (~d & s);
	CONDITIONAL_SET_FLAG(bc & 0x80, F_CF);
	CONDITIONAL_SET_FLAG(XOR2(bc >> 6), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return (uint8_t) res;
}

/*
 * REMARKS:
 * Implements the SUB instruction and side effects.
 */
static uint16_t 
sub_word(struct x86emu *emu, uint16_t d, uint16_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	res = d - s;
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	bc = (res & (~d | s)) | (~d & s);
	CONDITIONAL_SET_FLAG(bc & 0x8000, F_CF);
	CONDITIONAL_SET_FLAG(XOR2(bc >> 14), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return (uint16_t) res;
}

/*
 * REMARKS:
 * Implements the SUB instruction and side effects.
 */
static uint32_t 
sub_long(struct x86emu *emu, uint32_t d, uint32_t s)
{
	uint32_t res;	/* all operands in native machine order */
	uint32_t bc;

	res = d - s;
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG((res & 0xffffffff) == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);

	/* calculate the borrow chain.  See note at top */
	bc = (res & (~d | s)) | (~d & s);
	CONDITIONAL_SET_FLAG(bc & 0x80000000, F_CF);
	CONDITIONAL_SET_FLAG(XOR2(bc >> 30), F_OF);
	CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the TEST instruction and side effects.
 */
static void 
test_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	uint32_t res;	/* all operands in native machine order */

	res = d & s;

	CLEAR_FLAG(F_OF);
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	/* AF == dont care */
	CLEAR_FLAG(F_CF);
}

/*
 * REMARKS:
 * Implements the TEST instruction and side effects.
 */
static void 
test_word(struct x86emu *emu, uint16_t d, uint16_t s)
{
	uint32_t res;	/* all operands in native machine order */

	res = d & s;

	CLEAR_FLAG(F_OF);
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	/* AF == dont care */
	CLEAR_FLAG(F_CF);
}

/*
 * REMARKS:
 * Implements the TEST instruction and side effects.
 */
static void 
test_long(struct x86emu *emu, uint32_t d, uint32_t s)
{
	uint32_t res;	/* all operands in native machine order */

	res = d & s;

	CLEAR_FLAG(F_OF);
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	/* AF == dont care */
	CLEAR_FLAG(F_CF);
}

/*
 * REMARKS:
 * Implements the XOR instruction and side effects.
 */
static uint8_t 
xor_byte(struct x86emu *emu, uint8_t d, uint8_t s)
{
	uint8_t res;	/* all operands in native machine order */

	res = d ^ s;
	CLEAR_FLAG(F_OF);
	CONDITIONAL_SET_FLAG(res & 0x80, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res), F_PF);
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the XOR instruction and side effects.
 */
static uint16_t 
xor_word(struct x86emu *emu, uint16_t d, uint16_t s)
{
	uint16_t res;	/* all operands in native machine order */

	res = d ^ s;
	CLEAR_FLAG(F_OF);
	CONDITIONAL_SET_FLAG(res & 0x8000, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the XOR instruction and side effects.
 */
static uint32_t 
xor_long(struct x86emu *emu, uint32_t d, uint32_t s)
{
	uint32_t res;	/* all operands in native machine order */

	res = d ^ s;
	CLEAR_FLAG(F_OF);
	CONDITIONAL_SET_FLAG(res & 0x80000000, F_SF);
	CONDITIONAL_SET_FLAG(res == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(res & 0xff), F_PF);
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	return res;
}

/*
 * REMARKS:
 * Implements the IMUL instruction and side effects.
 */
static void 
imul_byte(struct x86emu *emu, uint8_t s)
{
	int16_t res = (int16_t) ((int8_t) emu->x86.R_AL * (int8_t) s);

	emu->x86.R_AX = res;
	if (((emu->x86.R_AL & 0x80) == 0 && emu->x86.R_AH == 0x00) ||
	    ((emu->x86.R_AL & 0x80) != 0 && emu->x86.R_AH == 0xFF)) {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
	} else {
		SET_FLAG(F_CF);
		SET_FLAG(F_OF);
	}
}

/*
 * REMARKS:
 * Implements the IMUL instruction and side effects.
 */
static void 
imul_word(struct x86emu *emu, uint16_t s)
{
	int32_t res = (int16_t) emu->x86.R_AX * (int16_t) s;

	emu->x86.R_AX = (uint16_t) res;
	emu->x86.R_DX = (uint16_t) (res >> 16);
	if (((emu->x86.R_AX & 0x8000) == 0 && emu->x86.R_DX == 0x00) ||
	    ((emu->x86.R_AX & 0x8000) != 0 && emu->x86.R_DX == 0xFF)) {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
	} else {
		SET_FLAG(F_CF);
		SET_FLAG(F_OF);
	}
}

/*
 * REMARKS:
 * Implements the IMUL instruction and side effects.
 */
static void 
imul_long(struct x86emu *emu, uint32_t s)
{
	int64_t res;
	
	res = (int64_t)(int32_t)emu->x86.R_EAX * (int32_t)s;
	emu->x86.R_EAX = (uint32_t)res;
	emu->x86.R_EDX = ((uint64_t)res) >> 32;
	if (((emu->x86.R_EAX & 0x80000000) == 0 && emu->x86.R_EDX == 0x00) ||
	    ((emu->x86.R_EAX & 0x80000000) != 0 && emu->x86.R_EDX == 0xFF)) {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
	} else {
		SET_FLAG(F_CF);
		SET_FLAG(F_OF);
	}
}

/*
 * REMARKS:
 * Implements the MUL instruction and side effects.
 */
static void 
mul_byte(struct x86emu *emu, uint8_t s)
{
	uint16_t res = (uint16_t) (emu->x86.R_AL * s);

	emu->x86.R_AX = res;
	if (emu->x86.R_AH == 0) {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
	} else {
		SET_FLAG(F_CF);
		SET_FLAG(F_OF);
	}
}

/*
 * REMARKS:
 * Implements the MUL instruction and side effects.
 */
static void 
mul_word(struct x86emu *emu, uint16_t s)
{
	uint32_t res = emu->x86.R_AX * s;

	emu->x86.R_AX = (uint16_t) res;
	emu->x86.R_DX = (uint16_t) (res >> 16);
	if (emu->x86.R_DX == 0) {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
	} else {
		SET_FLAG(F_CF);
		SET_FLAG(F_OF);
	}
}

/*
 * REMARKS:
 * Implements the MUL instruction and side effects.
 */
static void 
mul_long(struct x86emu *emu, uint32_t s)
{
	uint64_t res = (uint64_t) emu->x86.R_EAX * s;

	emu->x86.R_EAX = (uint32_t) res;
	emu->x86.R_EDX = (uint32_t) (res >> 32);

	if (emu->x86.R_EDX == 0) {
		CLEAR_FLAG(F_CF);
		CLEAR_FLAG(F_OF);
	} else {
		SET_FLAG(F_CF);
		SET_FLAG(F_OF);
	}
}

/*
 * REMARKS:
 * Implements the IDIV instruction and side effects.
 */
static void 
idiv_byte(struct x86emu *emu, uint8_t s)
{
	int32_t dvd, div, mod;

	dvd = (int16_t) emu->x86.R_AX;
	if (s == 0) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	div = dvd / (int8_t) s;
	mod = dvd % (int8_t) s;
	if (div > 0x7f || div < -0x7f) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	emu->x86.R_AL = (int8_t) div;
	emu->x86.R_AH = (int8_t) mod;
}

/*
 * REMARKS:
 * Implements the IDIV instruction and side effects.
 */
static void 
idiv_word(struct x86emu *emu, uint16_t s)
{
	int32_t dvd, div, mod;

	dvd = (((int32_t) emu->x86.R_DX) << 16) | emu->x86.R_AX;
	if (s == 0) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	div = dvd / (int16_t) s;
	mod = dvd % (int16_t) s;
	if (div > 0x7fff || div < -0x7fff) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_SF);
	CONDITIONAL_SET_FLAG(div == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(mod & 0xff), F_PF);

	emu->x86.R_AX = (uint16_t) div;
	emu->x86.R_DX = (uint16_t) mod;
}

/*
 * REMARKS:
 * Implements the IDIV instruction and side effects.
 */
static void 
idiv_long(struct x86emu *emu, uint32_t s)
{
	int64_t dvd, div, mod;

	dvd = (((int64_t) emu->x86.R_EDX) << 32) | emu->x86.R_EAX;
	if (s == 0) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	div = dvd / (int32_t) s;
	mod = dvd % (int32_t) s;
	if (div > 0x7fffffff || div < -0x7fffffff) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	CLEAR_FLAG(F_SF);
	SET_FLAG(F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(mod & 0xff), F_PF);

	emu->x86.R_EAX = (uint32_t) div;
	emu->x86.R_EDX = (uint32_t) mod;
}

/*
 * REMARKS:
 * Implements the DIV instruction and side effects.
 */
static void 
div_byte(struct x86emu *emu, uint8_t s)
{
	uint32_t dvd, div, mod;

	dvd = emu->x86.R_AX;
	if (s == 0) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	div = dvd / (uint8_t) s;
	mod = dvd % (uint8_t) s;
	if (div > 0xff) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	emu->x86.R_AL = (uint8_t) div;
	emu->x86.R_AH = (uint8_t) mod;
}

/*
 * REMARKS:
 * Implements the DIV instruction and side effects.
 */
static void 
div_word(struct x86emu *emu, uint16_t s)
{
	uint32_t dvd, div, mod;

	dvd = (((uint32_t) emu->x86.R_DX) << 16) | emu->x86.R_AX;
	if (s == 0) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	div = dvd / (uint16_t) s;
	mod = dvd % (uint16_t) s;
	if (div > 0xffff) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_SF);
	CONDITIONAL_SET_FLAG(div == 0, F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(mod & 0xff), F_PF);

	emu->x86.R_AX = (uint16_t) div;
	emu->x86.R_DX = (uint16_t) mod;
}

/*
 * REMARKS:
 * Implements the DIV instruction and side effects.
 */
static void 
div_long(struct x86emu *emu, uint32_t s)
{
	uint64_t dvd, div, mod;

	dvd = (((uint64_t) emu->x86.R_EDX) << 32) | emu->x86.R_EAX;
	if (s == 0) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	div = dvd / (uint32_t) s;
	mod = dvd % (uint32_t) s;
	if (div > 0xffffffff) {
		x86emu_intr_raise(emu, 8);
		return;
	}
	CLEAR_FLAG(F_CF);
	CLEAR_FLAG(F_AF);
	CLEAR_FLAG(F_SF);
	SET_FLAG(F_ZF);
	CONDITIONAL_SET_FLAG(PARITY(mod & 0xff), F_PF);

	emu->x86.R_EAX = (uint32_t) div;
	emu->x86.R_EDX = (uint32_t) mod;
}

/*
 * REMARKS:
 * Implements the IN string instruction and side effects.
 */
static void 
ins(struct x86emu *emu, int size)
{
	int inc = size;

	if (ACCESS_FLAG(F_DF)) {
		inc = -size;
	}
	if (emu->x86.mode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
		/* dont care whether REPE or REPNE */
		/* in until CX is ZERO. */
		uint32_t count = ((emu->x86.mode & SYSMODE_PREFIX_DATA) ?
		    emu->x86.R_ECX : emu->x86.R_CX);
		switch (size) {
		case 1:
			while (count--) {
				store_byte(emu, emu->x86.R_ES, emu->x86.R_DI,
				    (*emu->emu_inb) (emu, emu->x86.R_DX));
				emu->x86.R_DI += inc;
			}
			break;

		case 2:
			while (count--) {
				store_word(emu, emu->x86.R_ES, emu->x86.R_DI,
				    (*emu->emu_inw) (emu, emu->x86.R_DX));
				emu->x86.R_DI += inc;
			}
			break;
		case 4:
			while (count--) {
				store_long(emu, emu->x86.R_ES, emu->x86.R_DI,
				    (*emu->emu_inl) (emu, emu->x86.R_DX));
				emu->x86.R_DI += inc;
				break;
			}
		}
		emu->x86.R_CX = 0;
		if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
			emu->x86.R_ECX = 0;
		}
		emu->x86.mode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
	} else {
		switch (size) {
		case 1:
			store_byte(emu, emu->x86.R_ES, emu->x86.R_DI,
			    (*emu->emu_inb) (emu, emu->x86.R_DX));
			break;
		case 2:
			store_word(emu, emu->x86.R_ES, emu->x86.R_DI,
			    (*emu->emu_inw) (emu, emu->x86.R_DX));
			break;
		case 4:
			store_long(emu, emu->x86.R_ES, emu->x86.R_DI,
			    (*emu->emu_inl) (emu, emu->x86.R_DX));
			break;
		}
		emu->x86.R_DI += inc;
	}
}

/*
 * REMARKS:
 * Implements the OUT string instruction and side effects.
 */
static void 
outs(struct x86emu *emu, int size)
{
	int inc = size;

	if (ACCESS_FLAG(F_DF)) {
		inc = -size;
	}
	if (emu->x86.mode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
		/* dont care whether REPE or REPNE */
		/* out until CX is ZERO. */
		uint32_t count = ((emu->x86.mode & SYSMODE_PREFIX_DATA) ?
		    emu->x86.R_ECX : emu->x86.R_CX);
		switch (size) {
		case 1:
			while (count--) {
				(*emu->emu_outb) (emu, emu->x86.R_DX,
				    fetch_byte(emu, emu->x86.R_ES,
				    emu->x86.R_SI));
				emu->x86.R_SI += inc;
			}
			break;

		case 2:
			while (count--) {
				(*emu->emu_outw) (emu, emu->x86.R_DX,
				    fetch_word(emu, emu->x86.R_ES,
				    emu->x86.R_SI));
				emu->x86.R_SI += inc;
			}
			break;
		case 4:
			while (count--) {
				(*emu->emu_outl) (emu, emu->x86.R_DX,
				    fetch_long(emu, emu->x86.R_ES,
				    emu->x86.R_SI));
				emu->x86.R_SI += inc;
				break;
			}
		}
		emu->x86.R_CX = 0;
		if (emu->x86.mode & SYSMODE_PREFIX_DATA) {
			emu->x86.R_ECX = 0;
		}
		emu->x86.mode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
	} else {
		switch (size) {
		case 1:
			(*emu->emu_outb) (emu, emu->x86.R_DX,
			    fetch_byte(emu, emu->x86.R_ES, emu->x86.R_SI));
			break;
		case 2:
			(*emu->emu_outw) (emu, emu->x86.R_DX,
			    fetch_word(emu, emu->x86.R_ES, emu->x86.R_SI));
			break;
		case 4:
			(*emu->emu_outl) (emu, emu->x86.R_DX,
			    fetch_long(emu, emu->x86.R_ES, emu->x86.R_SI));
			break;
		}
		emu->x86.R_SI += inc;
	}
}

/*
 * REMARKS:
 * Pushes a word onto the stack.
 * 
 * NOTE: Do not inline this, as (*emu->emu_wrX) is already inline!
 */
static void 
push_word(struct x86emu *emu, uint16_t w)
{
	emu->x86.R_SP -= 2;
	store_word(emu, emu->x86.R_SS, emu->x86.R_SP, w);
}

/*
 * REMARKS:
 * Pushes a long onto the stack.
 * 
 * NOTE: Do not inline this, as (*emu->emu_wrX) is already inline!
 */
static void 
push_long(struct x86emu *emu, uint32_t w)
{
	emu->x86.R_SP -= 4;
	store_long(emu, emu->x86.R_SS, emu->x86.R_SP, w);
}

/*
 * REMARKS:
 * Pops a word from the stack.
 * 
 * NOTE: Do not inline this, as (*emu->emu_rdX) is already inline!
 */
static uint16_t 
pop_word(struct x86emu *emu)
{
	uint16_t res;

	res = fetch_word(emu, emu->x86.R_SS, emu->x86.R_SP);
	emu->x86.R_SP += 2;
	return res;
}

/*
 * REMARKS:
 * Pops a long from the stack.
 * 
 * NOTE: Do not inline this, as (*emu->emu_rdX) is already inline!
 */
static uint32_t 
pop_long(struct x86emu *emu)
{
	uint32_t res;

	res = fetch_long(emu, emu->x86.R_SS, emu->x86.R_SP);
	emu->x86.R_SP += 4;
	return res;
}
