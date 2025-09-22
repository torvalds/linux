# crti.asm for Sunplus S+CORE
#
#   Copyright (C) 2005 Free Software Foundation, Inc.
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
# along with GCC; see the file COPYING.  If not, write to the Free
# Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#
#   As a special exception, if you link this library with files
#   compiled with GCC to produce an executable, this does not cause
#   the resulting executable to be covered by the GNU General Public License.
#   This exception does not however invalidate any other reasons why
#   the executable file might be covered by the GNU General Public License.
#

# This file makes a stack frame for the contents of the .init and
# .fini sections.

#ifndef __pic__
.section .init, "ax", @progbits
        .weak   _start
        .ent    _start
        .frame  r0, 0, r3, 0
        .mask   0x00000000, 0
_start:
        la      r28, _gp
        la      r8, __bss_start
        la      r9, __bss_end__
        sub!    r9, r8
        srli!   r9, 2
        addi    r9, -1
        mtsr    r9, sr0
        li      r9, 0
1:
        sw      r9, [r8]+, 4
        bcnz    1b
        la      r0, _stack
        jl      _init
        la      r4, _end
        jl      _init_argv
        jl      exit
        .end    _start

        .weak   _init_argv
        .ent
        .frame  r0, 0, r3, 0
        .mask   0x00000000, 0
_init_argv:
        ldiu!   r4, 0
        ldiu!   r5, 0
        j       main
        .end    _init_argv

        .globl  _init
        .type   _init, %function
_init:
        addi    r0, -32
        sw      r3, [r0, 20]

        .section .fini, "ax", @progbits
        .globl  _fini
        .type   _fini, %function
_fini:
        addi    r0, -32
        sw      r3, [r0, 20]
#else
.section .init, "ax", @progbits
        .set    pic
        .weak   _start
        .ent    _start
        .frame  r0, 0, r3, 0
        .mask   0x00000000,0
_start:
        la      r28, _gp
        la      r8, __bss_start
        la      r9, __bss_end__
        sub!    r9, r8
        srli!   r9, 2
        addi    r9, -1
        mtsr    r9, sr0
        li      r9, 0
1:
        sw      r9, [r8]+, 4
        bcnz    1b
        la      r0, _stack
        ldiu!   r4, 0
        ldiu!   r5, 0
        la      r29, main
        brl     r29
        la      r29, exit
        brl     r29
        .end    _start

        .weak   _init_argv
        .ent
        .frame  r0, 0, r3, 0
        .mask   0x00000000, 0
_init_argv:
        ldiu!   r4, 0
        ldiu!   r5, 0
        j       main
        .end    _init_argv

        .globl  _init
        .type   _init, %function
_init:
        addi    r0, -32
        sw      r3, [r0, 20]

.section .fini, "ax", @progbits
        .globl  _fini
        .type   _fini, %function
_fini:
        addi    r0, -32
        sw      r3, [r0, 20]

#endif


