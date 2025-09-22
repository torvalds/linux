//===----------------------Hexagon builtin routine ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Functions that implement common sequences in function prologues and epilogues
// used to save code size

	.macro FUNCTION_BEGIN name
	.p2align 2
        .section .text.\name,"ax",@progbits
	.globl \name
	.type  \name, @function
\name:
	.endm

	.macro FUNCTION_END name
	.size  \name, . - \name
	.endm

	.macro FALLTHROUGH_TAIL_CALL name0 name1
	.p2align 2
	.size \name0, . - \name0
	.globl \name1
	.type \name1, @function
\name1:
	.endm




// Save r17:16 at fp+#-8, r19:18 at fp+#-16, r21:20 at fp+#-24, r23:22 at
// fp+#-32, r25:24 at fp+#-40, and r27:26 at fp+#-48.
// The compiler knows that the __save_* functions clobber LR.  No other
// registers should be used without informing the compiler.

FUNCTION_BEGIN __save_r16_through_r27
        {
                memd(fp+#-48) = r27:26
                memd(fp+#-40) = r25:24
        }
        {
                memd(fp+#-32) = r23:22
                memd(fp+#-24) = r21:20
        }
        {
                memd(fp+#-16) = r19:18
                memd(fp+#-8) = r17:16
                jumpr lr
        }
FUNCTION_END __save_r16_through_r27

FUNCTION_BEGIN __save_r16_through_r25
        {
                memd(fp+#-40) = r25:24
                memd(fp+#-32) = r23:22
        }
        {
                memd(fp+#-24) = r21:20
                memd(fp+#-16) = r19:18
        }
        {
                memd(fp+#-8) = r17:16
                jumpr lr
        }
FUNCTION_END __save_r16_through_r25

FUNCTION_BEGIN __save_r16_through_r23
        {
                memd(fp+#-32) = r23:22
                memd(fp+#-24) = r21:20
        }
        {
                memd(fp+#-16) = r19:18
                memd(fp+#-8) = r17:16
                jumpr lr
        }
FUNCTION_END __save_r16_through_r23

FUNCTION_BEGIN __save_r16_through_r21
        {
                memd(fp+#-24) = r21:20
                memd(fp+#-16) = r19:18
        }
        {
                memd(fp+#-8) = r17:16
                jumpr lr
        }
FUNCTION_END __save_r16_through_r21

FUNCTION_BEGIN __save_r16_through_r19
        {
                memd(fp+#-16) = r19:18
                memd(fp+#-8) = r17:16
                jumpr lr
        }
FUNCTION_END __save_r16_through_r19

FUNCTION_BEGIN __save_r16_through_r17
        {
                memd(fp+#-8) = r17:16
                jumpr lr
        }
FUNCTION_END __save_r16_through_r17

// For each of the *_before_tailcall functions, jumpr lr is executed in parallel
// with deallocframe.  That way, the return gets the old value of lr, which is
// where these functions need to return, and at the same time, lr gets the value
// it needs going into the tail call.


FUNCTION_BEGIN __restore_r16_through_r27_and_deallocframe_before_tailcall
                r27:26 = memd(fp+#-48)
        {
                r25:24 = memd(fp+#-40)
                r23:22 = memd(fp+#-32)
        }
        {
                r21:20 = memd(fp+#-24)
                r19:18 = memd(fp+#-16)
        }
        {
                r17:16 = memd(fp+#-8)
                deallocframe
                jumpr lr
        }
FUNCTION_END __restore_r16_through_r27_and_deallocframe_before_tailcall

FUNCTION_BEGIN __restore_r16_through_r25_and_deallocframe_before_tailcall
        {
                r25:24 = memd(fp+#-40)
                r23:22 = memd(fp+#-32)
        }
        {
                r21:20 = memd(fp+#-24)
                r19:18 = memd(fp+#-16)
        }
        {
                r17:16 = memd(fp+#-8)
                deallocframe
                jumpr lr
        }
FUNCTION_END __restore_r16_through_r25_and_deallocframe_before_tailcall

FUNCTION_BEGIN __restore_r16_through_r23_and_deallocframe_before_tailcall
        {
                r23:22 = memd(fp+#-32)
                r21:20 = memd(fp+#-24)
        }
                r19:18 = memd(fp+#-16)
        {
                r17:16 = memd(fp+#-8)
                deallocframe
                jumpr lr
        }
FUNCTION_END __restore_r16_through_r23_and_deallocframe_before_tailcall


FUNCTION_BEGIN __restore_r16_through_r21_and_deallocframe_before_tailcall
        {
                r21:20 = memd(fp+#-24)
                r19:18 = memd(fp+#-16)
        }
        {
                r17:16 = memd(fp+#-8)
                deallocframe
                jumpr lr
        }
FUNCTION_END __restore_r16_through_r19_and_deallocframe_before_tailcall

FUNCTION_BEGIN __restore_r16_through_r19_and_deallocframe_before_tailcall
                r19:18 = memd(fp+#-16)
        {
                r17:16 = memd(fp+#-8)
                deallocframe
                jumpr lr
        }
FUNCTION_END __restore_r16_through_r19_and_deallocframe_before_tailcall

FUNCTION_BEGIN __restore_r16_through_r17_and_deallocframe_before_tailcall
        {
                r17:16 = memd(fp+#-8)
                deallocframe
                jumpr lr
        }
FUNCTION_END __restore_r16_through_r17_and_deallocframe_before_tailcall


FUNCTION_BEGIN __restore_r16_through_r27_and_deallocframe
                r27:26 = memd(fp+#-48)
        {
                r25:24 = memd(fp+#-40)
                r23:22 = memd(fp+#-32)
        }
        {
                r21:20 = memd(fp+#-24)
                r19:18 = memd(fp+#-16)
        }
	{
		r17:16 = memd(fp+#-8)
		dealloc_return
	}
FUNCTION_END __restore_r16_through_r27_and_deallocframe

FUNCTION_BEGIN __restore_r16_through_r25_and_deallocframe
        {
                r25:24 = memd(fp+#-40)
                r23:22 = memd(fp+#-32)
        }
        {
                r21:20 = memd(fp+#-24)
                r19:18 = memd(fp+#-16)
        }
	{
		r17:16 = memd(fp+#-8)
		dealloc_return
	}
FUNCTION_END __restore_r16_through_r25_and_deallocframe

FUNCTION_BEGIN __restore_r16_through_r23_and_deallocframe
        {
                r23:22 = memd(fp+#-32)
        }
        {
                r21:20 = memd(fp+#-24)
                r19:18 = memd(fp+#-16)
        }
	{
		r17:16 = memd(fp+#-8)
		dealloc_return
	}
FUNCTION_END __restore_r16_through_r23_and_deallocframe

FUNCTION_BEGIN __restore_r16_through_r21_and_deallocframe
        {
                r21:20 = memd(fp+#-24)
                r19:18 = memd(fp+#-16)
        }
	{
		r17:16 = memd(fp+#-8)
		dealloc_return
	}
FUNCTION_END __restore_r16_through_r21_and_deallocframe

FUNCTION_BEGIN __restore_r16_through_r19_and_deallocframe
	{
                r19:18 = memd(fp+#-16)
		r17:16 = memd(fp+#-8)
        }
        {
		dealloc_return
	}
FUNCTION_END __restore_r16_through_r19_and_deallocframe

FUNCTION_BEGIN __restore_r16_through_r17_and_deallocframe
	{
		r17:16 = memd(fp+#-8)
		dealloc_return
	}
FUNCTION_END __restore_r16_through_r17_and_deallocframe

FUNCTION_BEGIN __deallocframe
        dealloc_return
FUNCTION_END __deallocframe
