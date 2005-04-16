#include <asm/voyager.h>
#define VOYAGER_BIOS_INFO ((struct voyager_bios_info *)(PARAM+0x40))

/* Hook to call BIOS initialisation function */

/* for voyager, pass the voyager BIOS/SUS info area to the detection 
 * routines */

#define ARCH_SETUP	voyager_detect(VOYAGER_BIOS_INFO);

