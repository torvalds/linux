/* $FreeBSD$ */

	.text
	.globl	_start
_start:
	call	ofw_init
	 nop
	sir
