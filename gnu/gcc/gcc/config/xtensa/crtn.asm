# End of .init and .fini sections.
# Copyright (C) 2003 Free Software Foundation, Inc.
# 
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
# 
# In addition to the permissions in the GNU General Public License, the
# Free Software Foundation gives you unlimited permission to link the
# compiled version of this file into combinations with other programs,
# and to distribute those combinations without any restriction coming
# from the use of this file.  (The General Public License restrictions
# do apply in other respects; for example, they cover modification of
# the file, and distribution when not linked into a combine
# executable.)
# 
# GCC is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
# 
# You should have received a copy of the GNU General Public License
# along with GCC; see the file COPYING.  If not, write to the Free
# Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

# This file just makes sure that the .fini and .init sections do in
# fact return.  Users may put any desired instructions in those sections.
# This file is the last thing linked into any executable.

#include "xtensa-config.h"

	.section .init
#if XCHAL_HAVE_WINDOWED && !__XTENSA_CALL0_ABI__
	retw
#else
	l32i	a0, sp, 0
	addi	sp, sp, 32
	ret
#endif

	.section .fini
#if XCHAL_HAVE_WINDOWED && !__XTENSA_CALL0_ABI__
	retw
#else
	l32i	a0, sp, 0
	addi	sp, sp, 32
	ret
#endif
