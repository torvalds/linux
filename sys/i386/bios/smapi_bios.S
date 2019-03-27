#include <machine/asm.h>
__FBSDID("$FreeBSD$");

/*
 * This is cribbed from the Linux thinkpad-4.1 driver by
 * Thomas Hood.
 */

smapi32_entry:					/* far pointer to SMAPI entry */
	.globl	smapi32_offset
smapi32_offset:		.long	0x00000000	/* set by caller */
smapi32_segment:	.word	0x0000		/* %cs stored here */

	.text
/*
 * smapi32(input_param, output_param)
 *	struct smapi_bios_parameter *input_parm;
 *	struct smapi_bios_parameter *output_parm;
 *
 *	stack frame:
 *		0x00 : saved ebp
 *		0x04 : return EIP
 *		0x08 : input_parm
 *		0x0c : output_parm
 */
ENTRY(smapi32)
	pushl	%ebp			/* Save frame */
	movl	%esp,%ebp

	pushl	%ds
	pushl	0x0c(%ebp)		/* Output Param */
	pushl	%ds
	pushl	0x08(%ebp)		/* Input Param */

	movl	$0,%eax			/* Clear EAX (return 0) */
	movw	%cs,smapi32_segment	/* Save CS */
	lcall	*(smapi32_offset)

	leave
	ret
