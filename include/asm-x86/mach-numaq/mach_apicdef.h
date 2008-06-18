#ifndef ASM_X86__MACH_NUMAQ__MACH_APICDEF_H
#define ASM_X86__MACH_NUMAQ__MACH_APICDEF_H


#define APIC_ID_MASK (0xF<<24)

static inline unsigned get_apic_id(unsigned long x)
{
	        return (((x)>>24)&0x0F);
}

#define         GET_APIC_ID(x)  get_apic_id(x)

#endif /* ASM_X86__MACH_NUMAQ__MACH_APICDEF_H */
