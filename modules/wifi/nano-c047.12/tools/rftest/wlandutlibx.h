
/*
 *   This file contains Nanoradio specific extensions to wlandutlib
 */

#ifndef _WLANDUTLIBX_H
#define _WLANDUTLIBX_H

#include "wlandutlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *   Sets the interface name.
 *   Typicaly "eht0" or "wlan0" on android and "eth1" on a notebook
 *
 */
int set_interface(char* name);


/*
 *   Print data sent to and from target (good for debuging and verification)
 */
void enable_debug(void);



#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
