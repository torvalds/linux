#
# Copyright (c) 1998 Robert Nordier
# All rights reserved.
#
# Redistribution and use in source and binary forms are freely
# permitted provided that the above copyright notice and this
# paragraph and the following disclaimer are duplicated in all
# such forms.
#
# This software is provided "AS IS" and without any express or
# implied warranties, including, without limitation, the implied
# warranties of merchantability and fitness for a particular
# purpose.
#

# $FreeBSD$

#include <bootargs.h>

#
# BTX C startup code (ELF).
#

#
# Globals.
#
		.global _start
#
# Client entry point.
#
_start: 	cld
		pushl %eax
		movl $_edata,%edi 
		movl $_end,%ecx 
		subl %edi, %ecx
		xorb %al, %al
		rep
		stosb
		popl __base
		movl %esp,%eax			# Set
		addl $ARGADJ,%eax		#  argument
		movl %eax,__args		#  pointer
		call main			# Invoke client main()
		call exit			# Invoke client exit()
#
# Data.
#
		.comm __base,4			# Client base address
		.comm __args,4			# Client arguments
