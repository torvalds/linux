#ifndef _SPARC64_DCU_H
#define _SPARC64_DCU_H

#include <linux/const.h>

/* UltraSparc-III Data Cache Unit Control Register */
#define DCU_CP	_AC(0x0002000000000000,UL) /* Phys Cache Enable w/o mmu	*/
#define DCU_CV	_AC(0x0001000000000000,UL) /* Virt Cache Enable w/o mmu	*/
#define DCU_ME	_AC(0x0000800000000000,UL) /* NC-store Merging Enable	*/
#define DCU_RE	_AC(0x0000400000000000,UL) /* RAW bypass Enable		*/
#define DCU_PE	_AC(0x0000200000000000,UL) /* PCache Enable		*/
#define DCU_HPE	_AC(0x0000100000000000,UL) /* HW prefetch Enable	*/
#define DCU_SPE	_AC(0x0000080000000000,UL) /* SW prefetch Enable	*/
#define DCU_SL	_AC(0x0000040000000000,UL) /* Secondary ld-steering Enab*/
#define DCU_WE	_AC(0x0000020000000000,UL) /* WCache enable		*/
#define DCU_PM	_AC(0x000001fe00000000,UL) /* PA Watchpoint Byte Mask	*/
#define DCU_VM	_AC(0x00000001fe000000,UL) /* VA Watchpoint Byte Mask	*/
#define DCU_PR	_AC(0x0000000001000000,UL) /* PA Watchpoint Read Enable	*/
#define DCU_PW	_AC(0x0000000000800000,UL) /* PA Watchpoint Write Enable*/
#define DCU_VR	_AC(0x0000000000400000,UL) /* VA Watchpoint Read Enable	*/
#define DCU_VW	_AC(0x0000000000200000,UL) /* VA Watchpoint Write Enable*/
#define DCU_DM	_AC(0x0000000000000008,UL) /* DMMU Enable		*/
#define DCU_IM	_AC(0x0000000000000004,UL) /* IMMU Enable		*/
#define DCU_DC	_AC(0x0000000000000002,UL) /* Data Cache Enable		*/
#define DCU_IC	_AC(0x0000000000000001,UL) /* Instruction Cache Enable	*/

#endif /* _SPARC64_DCU_H */
