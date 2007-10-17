#ifndef __ASM_MACH_APICDEF_H
#define __ASM_MACH_APICDEF_H

#include <asm/apic.h>

#define		APIC_ID_MASK		(0xF<<24)

static inline unsigned get_apic_id(unsigned long x) 
{ 
	unsigned int ver = GET_APIC_VERSION(apic_read(APIC_LVR));
	if (APIC_XAPIC(ver))
		return (((x)>>24)&0xFF);
	else
		return (((x)>>24)&0xF);
} 

#define		GET_APIC_ID(x)	get_apic_id(x)

#endif
