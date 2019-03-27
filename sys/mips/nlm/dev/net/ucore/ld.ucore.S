/*-
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

ENTRY(_start)
OUTPUT_FORMAT(elf32-tradbigmips)
__DYNAMIC  =  0;

SECTIONS
{
	. = 0x0;
	_loadaddr = .	;

	/* ----------------------------------------- */

	.text : {
		_ftext = . ;
		PROVIDE (eprol = .);
		_shim_reg = . ;
		*(.text)
		*(.text.*)
		*(.gnu.linkonce.t*)
		*(.mips16.fn.*)
		*(.mips16.call.*)
	}
	.init : {
		KEEP(*(.init))
		*(.init)
	}
	.fini : {
		*(.fini)
	}
	.rel.sdata : {
		PROVIDE (__runtime_reloc_start = .);
		*(.rel.sdata)
		PROVIDE (__runtime_reloc_stop = .);
	}
	PROVIDE (etext = .);
	.ctors    :
	{
		KEEP (*crtbegin.o(.ctors))
		KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
		KEEP (*(SORT(.ctors.*)))
		KEEP (*(.ctors))
	}
	.dtors    :
	{
		KEEP (*crtbegin.o(.dtors))
		KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
		KEEP (*(SORT(.dtors.*)))
		KEEP (*(.dtors))
	}
	. = .;
	.rodata : {
		*(.rdata)
		*(.rodata)
		*(.rodata.*)
		*(.gnu.linkonce.r*)
	}
	. = . + (0x1000 - .) ;

	/* ----------------------------------------- */

	. = 0x8000 ;
	magicstart = . ;
	.magicregs : {
		*(.magicregs)
	}
	magicend = . ;

	/* ----------------------------------------- */

	. = 0x18000 ;
	shmemstart = . ;
	.sharedmem : {
		*(.sharedmem)
	}
	shmemend = . ;

	/* ----------------------------------------- */

	. = 0xFF800 ;
	.data : {
		*(.data)
		*(.data.*)
		*(.gnu.linkonce.d*)
	}
	. = ALIGN(8);
	.lit8 : {
		*(.lit8)
	}
	.lit4 : {
		*(.lit4)
	}
	.sdata : {
		*(.sdata)
		*(.sdata.*)
		*(.gnu.linkonce.s*)
	}
	. = ALIGN (8);
	PROVIDE (edata  =  .);
	_edata  =  .;
	_fbss = .;
	.sbss : {
		*(.sbss)
		*(.scommon)
	}
	.bss : {
		_bss_start = . ;
		*(.bss)
		*(COMMON)
	}
	_bss_end = . ;
	_end = .;

	_gp = . ;
	__global = _gp ;
	. = ALIGN (8);

	PROVIDE(__stackmarker = .) ;

	. = 0xFFA00 ;

	/* 32 + 4(argc) + 4(argv), aligned to 64 */
	PROVIDE(__stack =  . - 64);

	/* ----------------------------------------- */

	. = 0xFFE00 ;
	.pktbuf : {
		*(.pktbuf)
	}
	. = . + (0x100000 - .) ;

	PROVIDE(_endaddr = 0x0 + 0x100000);
}
