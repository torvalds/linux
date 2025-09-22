# crtn.s for sysv4

#   Copyright (C) 1996 Free Software Foundation, Inc.
#   Written By Michael Meissner
# 
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2, or (at your option) any
# later version.
# 
# In addition to the permissions in the GNU General Public License, the
# Free Software Foundation gives you unlimited permission to link the
# compiled version of this file with other programs, and to distribute
# those programs without any restriction coming from the use of this
# file.  (The General Public License restrictions do apply in other
# respects; for example, they cover modification of the file, and
# distribution when not linked into another program.)
# 
# This file is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING.  If not, write to
# the Free Software Foundation, 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301, USA.
# 
#    As a special exception, if you link this library with files
#    compiled with GCC to produce an executable, this does not cause
#    the resulting executable to be covered by the GNU General Public License.
#    This exception does not however invalidate any other reasons why
#    the executable file might be covered by the GNU General Public License.
# 

# This file just supplies labeled ending points for the .got* and other
# special sections.  It is linked in last after other modules.
 
	.file	"scrtn.s"
	.ident	"GNU C scrtn.s"

#ifndef __powerpc64__
# Default versions of exception handling register/deregister
	.weak	_ex_register
	.weak	_ex_deregister
	.set	_ex_register,0
	.set	_ex_deregister,0

# End list of C++ constructors
	.section ".ctors","aw"
	.globl	__CTOR_END__
	.type	__CTOR_END__,@object
__CTOR_END__:

# End list of C++ destructors
	.section ".dtors","aw"
	.globl	__DTOR_END__
	.type	__DTOR_END__,@object
__DTOR_END__:

	.section ".text"
	.globl	_ex_text1
_ex_text1:

	.section ".exception_ranges","aw"
	.globl	_ex_range1
_ex_range1:

# Tail of _init used for static constructors
	.section ".init","ax"
	lwz	%r0,16(%r1)
	lwz	%r31,12(%r1)
	mtlr	%r0
	addi	%r1,%r1,16
	blr

# Tail of _fini used for static destructors
	.section ".fini","ax"
	lwz	%r0,16(%r1)
	lwz	%r31,12(%r1)
	mtlr	%r0
	addi	%r1,%r1,16
	blr
#endif
