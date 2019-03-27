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

#
# BTX V86 interface.
#

#
# Globals.
#
		.global __v86int
#
# Fields in V86 interface structure.
#
		.set V86_CTL,0x0		# Control flags
		.set V86_ADDR,0x4		# Int number/address
		.set V86_ES,0x8			# V86 ES
		.set V86_DS,0xc			# V86 DS
		.set V86_FS,0x10		# V86 FS
		.set V86_GS,0x14		# V86 GS
		.set V86_EAX,0x18		# V86 EAX
		.set V86_ECX,0x1c		# V86 ECX
		.set V86_EDX,0x20		# V86 EDX
		.set V86_EBX,0x24		# V86 EBX
		.set V86_EFL,0x28		# V86 eflags
		.set V86_EBP,0x2c		# V86 EBP
		.set V86_ESI,0x30		# V86 ESI
		.set V86_EDI,0x34		# V86 EDI
#
# Other constants.
#
		.set INT_V86,0x31		# Interrupt number
		.set SIZ_V86,0x38		# Size of V86 structure
#
# V86 interface function.
#
__v86int:	popl __v86ret			# Save return address
		pushl $__v86			# Push pointer
		call __v86_swap			# Load V86 registers
		int $INT_V86			# To BTX
		call __v86_swap			# Load user registers
		addl $0x4,%esp			# Discard pointer
		pushl __v86ret			# Restore return address
		ret 				# To user
#
# Swap V86 and user registers.
#
__v86_swap:	xchgl %ebp,0x4(%esp,1)		# Swap pointer, EBP
		xchgl %eax,V86_EAX(%ebp)	# Swap EAX
		xchgl %ecx,V86_ECX(%ebp)	# Swap ECX
		xchgl %edx,V86_EDX(%ebp)	# Swap EDX
		xchgl %ebx,V86_EBX(%ebp)	# Swap EBX
		pushl %eax			# Save
		pushf 				# Put eflags
		popl %eax			#  in EAX
		xchgl %eax,V86_EFL(%ebp)	# Swap
		pushl %eax			# Put EAX
		popf 				#  in eflags
		movl 0x8(%esp,1),%eax		# Load EBP
		xchgl %eax,V86_EBP(%ebp)	# Swap
		movl %eax,0x8(%esp,1)		# Save EBP
		popl %eax			# Restore
		xchgl %esi,V86_ESI(%ebp)	# Swap ESI
		xchgl %edi,V86_EDI(%ebp)	# Swap EDI
		xchgl %ebp,0x4(%esp,1)		# Swap pointer, EBP
		ret				# To caller
#
# V86 interface structure.
#
		.comm __v86,SIZ_V86
		.comm __v86ret,4
