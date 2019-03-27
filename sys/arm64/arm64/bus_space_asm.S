/*-
 * Copyright (c) 2014 Andrew Turner
 * All rights reserved.
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
 *
 */

#include <machine/asm.h>

__FBSDID("$FreeBSD$");

ENTRY(generic_bs_r_1)
	ldrb	w0, [x1, x2]
	ret
END(generic_bs_r_1)

ENTRY(generic_bs_r_2)
	ldrh	w0, [x1, x2]
	ret
END(generic_bs_r_2)

ENTRY(generic_bs_r_4)
	ldr	w0, [x1, x2]
	ret
END(generic_bs_r_4)

ENTRY(generic_bs_r_8)
	ldr	x0, [x1, x2]
	ret
END(generic_bs_r_8)

ENTRY(generic_bs_rm_1)
	/* If there is anything to read. */
	cbz	x4, 2f

	/* Calculate the device address. */
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Read the data. */
1:	ldrb	w1, [x0]
	strb	w1, [x3], #1
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_rm_1)

ENTRY(generic_bs_rm_2)
	/* If there is anything to read. */
	cbz	x4, 2f

	/* Calculate the device address. */
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Read the data. */
1:	ldrh	w1, [x0]
	strh	w1, [x3], #2
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_rm_2)

ENTRY(generic_bs_rm_4)
	/* If there is anything to read. */
	cbz	x4, 2f

	/* Calculate the device address. */
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Read the data. */
1:	ldr	w1, [x0]
	str	w1, [x3], #4
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_rm_4)

ENTRY(generic_bs_rm_8)
	/* If there is anything to read. */
	cbz	x4, 2f

	/* Calculate the device address. */
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Read the data. */
1:	ldr	x1, [x0]
	str	x1, [x3], #8
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_rm_8)

ENTRY(generic_bs_rr_1)
	/* Is there is anything to read. */
	cbz	x4, 2f

	/* Calculate the device address. */
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Read the data. */
1:	ldrb	w1, [x0], #1
	strb	w1, [x3], #1
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_rr_1)

ENTRY(generic_bs_rr_2)
	/* Is there is anything to read. */
	cbz	x4, 2f

	/* Calculate the device address. */
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Read the data. */
1:	ldrh	w1, [x0], #2
	strh	w1, [x3], #2
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_rr_2)

ENTRY(generic_bs_rr_4)
	/* Is there is anything to read. */
	cbz	x4, 2f

	/* Calculate the device address. */
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Read the data. */
1:	ldr	w1, [x0], #4
	str	w1, [x3], #4
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_rr_4)

ENTRY(generic_bs_rr_8)
	/* Is there is anything to read. */
	cbz	x4, 2f

	/* Calculate the device address. */
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Read the data. */
1:	ldr	x1, [x0], #8
	str	x1, [x3], #8
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_rr_8)


ENTRY(generic_bs_w_1)
	strb	w3, [x1, x2]
	ret
END(generic_bs_w_1)

ENTRY(generic_bs_w_2)
	strh	w3, [x1, x2]
	ret
END(generic_bs_w_2)

ENTRY(generic_bs_w_4)
	str	w3, [x1, x2]
	ret
END(generic_bs_w_4)

ENTRY(generic_bs_w_8)
	str	x3, [x1, x2]
	ret
END(generic_bs_w_8)

ENTRY(generic_bs_wm_1)
	/* If there is anything to write. */
	cbz	x4, 2f

	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Write the data */
1:	ldrb	w1, [x3], #1
	strb	w1, [x0]
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_wm_1)

ENTRY(generic_bs_wm_2)
	/* If there is anything to write. */
	cbz	x4, 2f

	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Write the data */
1:	ldrh	w1, [x3], #2
	strh	w1, [x0]
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_wm_2)

ENTRY(generic_bs_wm_4)
	/* If there is anything to write. */
	cbz	x4, 2f

	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Write the data */
1:	ldr	w1, [x3], #4
	str	w1, [x0]
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_wm_4)

ENTRY(generic_bs_wm_8)
	/* If there is anything to write. */
	cbz	x4, 2f

	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Write the data */
1:	ldr	x1, [x3], #8
	str	x1, [x0]
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_wm_8)

ENTRY(generic_bs_wr_1)
	/* Is there is anything to write. */
	cbz	x4, 2f
	
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Write the data */
1:	ldrb	w1, [x3], #1
	strb	w1, [x0], #1
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_wr_1)

ENTRY(generic_bs_wr_2)
	/* Is there is anything to write. */
	cbz	x4, 2f
	
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Write the data */
1:	ldrh	w1, [x3], #2
	strh	w1, [x0], #2
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_wr_2)

ENTRY(generic_bs_wr_4)
	/* Is there is anything to write. */
	cbz	x4, 2f
	
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Write the data */
1:	ldr	w1, [x3], #4
	str	w1, [x0], #4
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_wr_4)

ENTRY(generic_bs_wr_8)
	/* Is there is anything to write. */
	cbz	x4, 2f
	
	add	x0, x1, x2
	/*
	 * x0 = The device address.
	 * x3 = The kernel address.
	 * x4 = Count
	 */

	/* Write the data */
1:	ldr	x1, [x3], #8
	str	x1, [x0], #8
	subs	x4, x4, #1
	b.ne	1b

2:	ret
END(generic_bs_wr_8)
