/* dk_ver.h - macros and definitions for memory management */

/*
 *  Copyright ?2000-2001 Atheros Communications, Inc.,  All Rights Reserved.
 */

#ident  "ACI $Id: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/dk_ver.h#12 $, $Header: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/dk_ver.h#12 $"

#ifndef __INCdk_verh
#define __INCdk_verh
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define ART_VERSION_MAJOR 0
#define ART_VERSION_MINOR 2
#define ART_BUILD_NUM     29
#define ART_BUILD_DATE    "03312011"

#define MAUIDK_VER1 ("\n   --- Atheros: MDK (multi-device version) ---\n")

#define MAUIDK_VER_SUB ("              - Revision 0.2.29_SH ")
#ifdef CUSTOMER_REL
#ifdef INDONESIA_BUILD
#define MAUIDK_VER2  ("    Revision 0.2 BUILD #29_IN ART_11n")
#else
#define MAUIDK_VER2  ("    Revision 0.2 BUILD #29_SH ART_11n")
#endif
#ifdef ANWI
#define MAUIDK_VER3 ("\n    Customer Version (ANWI BUILD)-\n")
#else
#define MAUIDK_VER3 ("\n    Customer Version -\n")
#endif //ANWI
#else
#define MAUIDK_VER2 ("    Revision 0.2 BUILD #29_SH ART_11n")
#define MAUIDK_VER3 ("\n    --- Atheros INTERNAL USE ONLY ---\n")
#endif
#define DEVLIB_VER1 ("\n    Devlib Revision 0.2 BUILD #29_SH ART_11n\n")

#ifdef ART
#define MDK_CLIENT_VER1 ("\n    --- Atheros: ART Client (multi-device version) ---\n")
#else
#define MDK_CLIENT_VER1 ("\n    --- Atheros: MDK Client (multi-device version) ---\n")
#endif
#define MDK_CLIENT_VER2 ("    Revision 0.2 BUILD #29_SH ART_11n -\n")

#define NART_VERSION_MAJOR 0
#define NART_VERSION_MINOR 2
#define NART_BUILD_NUM     31
#define NART_BUILD_DATE    "03312011_SH"
#define NART_VER1           ("\n--- Atheros: NART (New Atheros Radio Test) ---\n")

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __INCdk_verh */
