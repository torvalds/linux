/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 ARM Limited */

.global pac_corruptor

.text
/*
 * Corrupting a single bit of the PAC ensures the authentication will fail.  It
 * also guarantees no possible collision. TCR_EL1.TBI0 is set by default so no
 * top byte PAC is tested
 */
 pac_corruptor:
	paciasp

	/* corrupt the top bit of the PAC */
	eor lr, lr, #1 << 53

	autiasp
	ret
