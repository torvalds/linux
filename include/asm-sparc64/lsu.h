/* $Id: lsu.h,v 1.2 1997/04/04 00:50:22 davem Exp $ */
#ifndef _SPARC64_LSU_H
#define _SPARC64_LSU_H

#include <asm/const.h>

/* LSU Control Register */
#define LSU_CONTROL_PM _AC(0x000001fe00000000,UL) /* Phys-watchpoint byte mask*/
#define LSU_CONTROL_VM _AC(0x00000001fe000000,UL) /* Virt-watchpoint byte mask*/
#define LSU_CONTROL_PR _AC(0x0000000001000000,UL) /* Phys-rd watchpoint enable*/
#define LSU_CONTROL_PW _AC(0x0000000000800000,UL) /* Phys-wr watchpoint enable*/
#define LSU_CONTROL_VR _AC(0x0000000000400000,UL) /* Virt-rd watchpoint enable*/
#define LSU_CONTROL_VW _AC(0x0000000000200000,UL) /* Virt-wr watchpoint enable*/
#define LSU_CONTROL_FM _AC(0x00000000000ffff0,UL) /* Parity mask enables.     */
#define LSU_CONTROL_DM _AC(0x0000000000000008,UL) /* Data MMU enable.         */
#define LSU_CONTROL_IM _AC(0x0000000000000004,UL) /* Instruction MMU enable.  */
#define LSU_CONTROL_DC _AC(0x0000000000000002,UL) /* Data cache enable.       */
#define LSU_CONTROL_IC _AC(0x0000000000000001,UL) /* Instruction cache enable.*/

#endif /* !(_SPARC64_LSU_H) */
