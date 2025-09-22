! crt1.s for Solaris 2, x86

!   Copyright (C) 1993, 1998 Free Software Foundation, Inc.
!   Written By Fred Fish, Nov 1992
! 
! This file is free software; you can redistribute it and/or modify it
! under the terms of the GNU General Public License as published by the
! Free Software Foundation; either version 2, or (at your option) any
! later version.
! 
! In addition to the permissions in the GNU General Public License, the
! Free Software Foundation gives you unlimited permission to link the
! compiled version of this file with other programs, and to distribute
! those programs without any restriction coming from the use of this
! file.  (The General Public License restrictions do apply in other
! respects; for example, they cover modification of the file, and
! distribution when not linked into another program.)
! 
! This file is distributed in the hope that it will be useful, but
! WITHOUT ANY WARRANTY; without even the implied warranty of
! MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
! General Public License for more details.
! 
! You should have received a copy of the GNU General Public License
! along with this program; see the file COPYING.  If not, write to
! the Free Software Foundation, 51 Franklin Street, Fifth Floor,
! Boston, MA 02110-1301, USA.
! 
!    As a special exception, if you link this library with files
!    compiled with GCC to produce an executable, this does not cause
!    the resulting executable to be covered by the GNU General Public License.
!    This exception does not however invalidate any other reasons why
!    the executable file might be covered by the GNU General Public License.
! 

! This file takes control of the process from the kernel, as specified
! in section 3 of the System V Application Binary Interface, Intel386
! Processor Supplement.  It has been constructed from information obtained
! from the ABI, information obtained from single stepping existing
! Solaris executables through their startup code with gdb, and from
! information obtained by single stepping executables on other i386 SVR4
! implementations.  This file is the first thing linked into any executable.

	.file	"crt1.s"
	.ident	"GNU C crt1.s"
	.weak	_cleanup
	.weak	_DYNAMIC
	.text

! Start creating the initial frame by pushing a NULL value for the return
! address of the initial frame, and mark the end of the stack frame chain
! (the innermost stack frame) with a NULL value, per page 3-32 of the ABI.
! Initialize the first stack frame pointer in %ebp (the contents of which
! are unspecified at process initialization).

	.globl	_start
_start:
	pushl	$0x0
	pushl	$0x0
	movl	%esp,%ebp

! As specified per page 3-32 of the ABI, %edx contains a function 
! pointer that should be registered with atexit(), for proper
! shared object termination.  Just push it onto the stack for now
! to preserve it.  We want to register _cleanup() first.

	pushl	%edx

! Check to see if there is an _cleanup() function linked in, and if
! so, register it with atexit() as the last thing to be run by
! atexit().

	movl	$_cleanup,%eax
	testl	%eax,%eax
	je	.L1
	pushl	$_cleanup
	call	atexit
	addl	$0x4,%esp
.L1:

! Now check to see if we have an _DYNAMIC table, and if so then
! we need to register the function pointer previously in %edx, but
! now conveniently saved on the stack as the argument to pass to
! atexit().

	movl	$_DYNAMIC,%eax
	testl	%eax,%eax
	je	.L2
	call	atexit
.L2:

! Register _fini() with atexit().  We will take care of calling _init()
! directly.

	pushl	$_fini
	call	atexit

! Compute the address of the environment vector on the stack and load
! it into the global variable _environ.  Currently argc is at 8 off
! the frame pointer.  Fetch the argument count into %eax, scale by the
! size of each arg (4 bytes) and compute the address of the environment
! vector which is 16 bytes (the two zero words we pushed, plus argc,
! plus the null word terminating the arg vector) further up the stack,
! off the frame pointer (whew!).

	movl	8(%ebp),%eax
	leal	16(%ebp,%eax,4),%edx
	movl	%edx,_environ

! Push the environment vector pointer, the argument vector pointer,
! and the argument count on to the stack to set up the arguments
! for _init(), _fpstart(), and main().  Note that the environment
! vector pointer and the arg count were previously loaded into
! %edx and %eax respectively.  The only new value we need to compute
! is the argument vector pointer, which is at a fixed address off
! the initial frame pointer.

!
! Make sure the stack is properly aligned.
!
	andl $0xfffffff0,%esp
	subl $4,%esp
	
	pushl	%edx
	leal	12(%ebp),%edx
	pushl	%edx
	pushl	%eax

! Call _init(argc, argv, environ), _fpstart(argc, argv, environ), and
! main(argc, argv, environ).

	call	_init
	call	__fpstart
	call	main

! Pop the argc, argv, and environ arguments off the stack, push the
! value returned from main(), and call exit().

	addl	$12,%esp
	pushl	%eax
	call	exit

! An inline equivalent of _exit, as specified in Figure 3-26 of the ABI.

	pushl	$0x0
	movl	$0x1,%eax
	lcall	$7,$0

! If all else fails, just try a halt!

	hlt
	.type	_start,@function
	.size	_start,.-_start

! A dummy profiling support routine for non-profiling executables,
! in case we link in some objects that have been compiled for profiling.

	.weak	_mcount
_mcount:
	ret
	.type	_mcount,@function
	.size	_mcount,.-_mcount
