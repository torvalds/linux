#
# Copyright (c) 1999 Global Technology Associates, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
# OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
# BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
# EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#	From: btx.s 1.10 1999/02/25 16:27:41 rnordier
# $FreeBSD$
#

# Screen defaults and assumptions.

		.set SCR_MAT,0x7		# Mode/attribute
		.set SCR_COL,0x50		# Columns per row
		.set SCR_ROW,0x19		# Rows per screen

# BIOS Data Area locations.

		.set BDA_SCR,0x449		# Video mode
		.set BDA_POS,0x450		# Cursor position

		.globl crt_putchr

# void crt_putchr(int c)

crt_putchr: 	movb 0x4(%esp,1),%al		# Get character
		pusha				# Save
		xorl %ecx,%ecx			# Zero for loops
		movb $SCR_MAT,%ah		# Mode/attribute
		movl $BDA_POS,%ebx		# BDA pointer
		movw (%ebx),%dx 		# Cursor position
		movl $0xb8000,%edi		# Regen buffer (color)
		cmpb %ah,BDA_SCR-BDA_POS(%ebx)	# Mono mode?
		jne crt_putchr.1		# No
		xorw %di,%di			# Regen buffer (mono)
crt_putchr.1:	cmpb $0xa,%al			# New line?
		je crt_putchr.2			# Yes
		xchgl %eax,%ecx 		# Save char
		movb $SCR_COL,%al		# Columns per row
		mulb %dh			#  * row position
		addb %dl,%al			#  + column
		adcb $0x0,%ah			#  position
		shll %eax			#  * 2
		xchgl %eax,%ecx 		# Swap char, offset
		movw %ax,(%edi,%ecx,1)		# Write attr:char
		incl %edx			# Bump cursor
		cmpb $SCR_COL,%dl		# Beyond row?
		jb crt_putchr.3			# No
crt_putchr.2:	xorb %dl,%dl			# Zero column
		incb %dh			# Bump row
crt_putchr.3:	cmpb $SCR_ROW,%dh		# Beyond screen?
		jb crt_putchr.4			# No
		leal 2*SCR_COL(%edi),%esi	# New top line
		movw $(SCR_ROW-1)*SCR_COL/2,%cx # Words to move
		rep				# Scroll
		movsl				#  screen
		movb $' ',%al			# Space
		movb $SCR_COL,%cl		# Columns to clear
		rep				# Clear
		stosw				#  line
		movb $SCR_ROW-1,%dh		# Bottom line
crt_putchr.4:	movw %dx,(%ebx) 		# Update position
		popa				# Restore
		ret				# To caller
