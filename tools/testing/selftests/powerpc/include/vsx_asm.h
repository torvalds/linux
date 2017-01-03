/*
 * Copyright 2015, Cyril Bur, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "basic_asm.h"

/*
 * Careful this will 'clobber' vsx (by design), VSX are always
 * volatile though so unlike vmx this isn't so much of an issue
 * Still should avoid calling from C
 */
FUNC_START(load_vsx)
	li	r5,0
	lxvx	vs20,r5,r3
	addi	r5,r5,16
	lxvx	vs21,r5,r3
	addi	r5,r5,16
	lxvx	vs22,r5,r3
	addi	r5,r5,16
	lxvx	vs23,r5,r3
	addi	r5,r5,16
	lxvx	vs24,r5,r3
	addi	r5,r5,16
	lxvx	vs25,r5,r3
	addi	r5,r5,16
	lxvx	vs26,r5,r3
	addi	r5,r5,16
	lxvx	vs27,r5,r3
	addi	r5,r5,16
	lxvx	vs28,r5,r3
	addi	r5,r5,16
	lxvx	vs29,r5,r3
	addi	r5,r5,16
	lxvx	vs30,r5,r3
	addi	r5,r5,16
	lxvx	vs31,r5,r3
	blr
FUNC_END(load_vsx)

FUNC_START(store_vsx)
	li	r5,0
	stxvx	vs20,r5,r3
	addi	r5,r5,16
	stxvx	vs21,r5,r3
	addi	r5,r5,16
	stxvx	vs22,r5,r3
	addi	r5,r5,16
	stxvx	vs23,r5,r3
	addi	r5,r5,16
	stxvx	vs24,r5,r3
	addi	r5,r5,16
	stxvx	vs25,r5,r3
	addi	r5,r5,16
	stxvx	vs26,r5,r3
	addi	r5,r5,16
	stxvx	vs27,r5,r3
	addi	r5,r5,16
	stxvx	vs28,r5,r3
	addi	r5,r5,16
	stxvx	vs29,r5,r3
	addi	r5,r5,16
	stxvx	vs30,r5,r3
	addi	r5,r5,16
	stxvx	vs31,r5,r3
	blr
FUNC_END(store_vsx)
