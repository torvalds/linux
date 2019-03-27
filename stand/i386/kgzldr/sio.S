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
#	From: sio.s 1.3 1999/01/10 14:48:03 rnordier
# $FreeBSD$
#

		.globl sio_putchr

# void sio_putchr(int c)

sio_putchr:	movw $SIO_PRT+0x5,%dx		# Line status reg
		xor %ecx,%ecx			# Timeout
		movb $0x40,%ch			#  counter
sio_putchr.1:	inb %dx,%al			# Transmitter
		testb $0x20,%al 		#  buffer empty?
		loopz sio_putchr.1		# No
		jz sio_putchr.2			# If timeout
		movb 0x4(%esp,1),%al		# Get character
		subb $0x5,%dl			# Transmitter hold reg
		outb %al,%dx			# Write character
sio_putchr.2:	ret				# To caller
