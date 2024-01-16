#include <asm/ppc_asm.h>

FUNC_START(enter_vmx_usercopy)
	li	r3,1
	blr

FUNC_START(exit_vmx_usercopy)
	li	r3,0
	blr

FUNC_START(enter_vmx_ops)
	li	r3,1
	blr

FUNC_START(exit_vmx_ops)
	blr

FUNC_START(__copy_tofrom_user_base)
	blr
