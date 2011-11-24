#ifndef __WEI_DUMP_H
#define __WEI_DUMP_H

#define WE_DUMP_START_ADDRESS_RAM      0x0000
#define WE_DUMP_END_ADDRESS_RAM        0x20000 
#define WE_DUMP_CORE_RAM_SIZE          WE_DUMP_END_ADDRESS_RAM - WE_DUMP_START_ADDRESS_RAM
#define WE_DUMP_START_ADDRESS_STACK    0x40000
#define WE_DUMP_END_ADDRESS_STACK      0x48000
#define WE_DUMP_STACK_SIZE             WE_DUMP_END_ADDRESS_STACK - WE_DUMP_START_ADDRESS_STACK
#define WE_DUMP_BB_REG_SIZE            1024

extern uint32_t register_size;

/* Extra byte workaround (otherwise last part of message not transmitted) 
*  The is string is first in normal order, then 16-bit swapped */
#undef SCB_ERROR_KEY_STRING
#define SCB_ERROR_KEY_STRING "reqErrReason\0\0erEqrreRsano" 
#endif
