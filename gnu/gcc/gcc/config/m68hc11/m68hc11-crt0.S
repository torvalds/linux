/* Startup code for M68HC11.
   Copyright (C) 1999, 2000, 2002 Free Software Foundation, Inc.

This file is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file with other programs, and to distribute
those programs without any restriction coming from the use of this
file.  (The General Public License restrictions do apply in other
respects; for example, they cover modification of the file, and
distribution when not linked into another program.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */
	
;-----------------------------------------
; startup code
;-----------------------------------------
	.file	"crt0.s"

#ifdef __HAVE_SHORT_INT__
	.mode mshort
#else
	.mode mlong
#endif

#if defined(__USE_RTC__) && defined(mc68hc12)
	.macro jsr name
	call	\name
	.endm
#endif
;; 
;; 
;; The linker concatenate the .install* sections in the following order:
;; 
;; .install0	Setup the stack pointer
;; .install1	Place holder for applications
;; .install2	Optional installation of data section in memory
;; .install3	Place holder for applications
;; .install4	Invokes the main
;; 
	.sect   .install0,"ax",@progbits
	.globl _start

_start:
;;
;; At this step, the stack is not initialized and interrupts are masked.
;; Applications only have 64 cycles to initialize some registers.
;;
;; To have a generic/configurable startup, initialize the stack to
;; the end of some memory region.  The _stack symbol is defined by
;; the linker.
;;
	lds	#_stack
	
	.sect	.install2,"ax",@progbits
;;
;; Call a specific initialization operation.  The default is empty.
;; It can be overridden by applications.  It is intended to initialize
;; the 68hc11 registers.  Function prototype is:
;; 
;;	int __premain(void);
;; 
	jsr	__premain
	
;;
;; 
;;
	.sect	.install4,"ax",@progbits
	jsr     main
fatal:
	jsr	exit
	bra fatal

;-----------------------------------------
; end startup code
;-----------------------------------------
;; Force loading of data section mapping and bss clear
	.2byte	__map_data_section
	.2byte  __init_bss_section
