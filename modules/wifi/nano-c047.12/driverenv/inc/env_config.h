/*
***************************************************************************
* @(#) File: env_config.h
***************************************************************************
nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn
nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn
nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn
nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn
nnnnnnnnn        nnnn   nnnn    nnnn    nnnn   nnnn    nnnn       nnnnnnnnn
nnnnnnnnn        nnnn   nnnn    nnnn    nnnn   nnnn    nnnn       nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                     nnnnnnnnnnn         nnnnnnnnn
nnnnnnnnn                                   nnnnnnnnnnnnnnn       nnnnnnnnn
nnnnnnnnn                                 nnnnnnnnnnnnnnnnnnn     nnnnnnnnn
nnnnnnnnn                                 nnnnnnnnnnnnnnnnnnn     nnnnnnnnn
nnnnnnnnn                                 nnnnn         nnnnn     nnnnnnnnn
nnnnnnnnn                                 nnnnn         nnnnn     nnnnnnnnn
nnnnnnnnn                                 nnnnn         nnnnn     nnnnnnnnn
nnnnnnnnn                                 nnnnn         nnnnn     nnnnnnnnn
nnnnnnnnn                                 nnnnn         nnnnn     nnnnnnnnn
nnnnnnnnn                                 nnnnn         nnnnn     nnnnnnnnn
nnnnnnnnn                                 nnnnn         nnnnn     nnnnnnnnn
nnnnnnnnn                                 nnnnn         nnnnn     nnnnnnnnn
nnnnnnnnn                                 nnnnn         nnnnn     nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn                                                         nnnnnnnnn
nnnnnnnnn        nnnn   nnnn    nnnn    nnnn   nnnn    nnnn       nnnnnnnnn
nnnnnnnnn        nnnn   nnnn    nnnn    nnnn   nnnn    nnnn       nnnnnnnnn
nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn
nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn
nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn
nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn

            Copyright (c) 2008 by Nanoradio AB

*/

#ifndef ENV_CONFIG_H
#define ENV_CONFIG_H

#define DE_TRACE_MODE                  ( CFG_ON )
#define DE_REGISTRY_TYPE               ( CFG_TEXT)


#define DE_ENABLE_PCAPLOG              ( CFG_OFF )

#define DE_ENABLE_HASH_FUNCTIONS CFG_ON

/*
 * xx.yy DE_CCX (must not be left empty)
 *
 * Configures if WiFi-Engine should include support for CCX
 *
 * Syntax:
 *     DE_CCX  ( CFG_NOT_INCLUDED | CFG_INCLUDED )
 */
#ifndef DE_CCX
#ifdef CFG_NRX_600
#define DE_CCX            CFG_INCLUDED
#else
#define DE_CCX            CFG_NOT_INCLUDED
#endif //CHIP VERSION
#endif //DE_CCX

/*
 * xx.yy DE_CCX_ROAMING (must not be left empty)
 *
 * Configures if WiFi-Engine should include support for CCX roaming
 *
 * Syntax:
 *     DE_CCX_ROAMING  ( CFG_NOT_INCLUDED | CFG_INCLUDED )
 */
#ifndef DE_CCX_ROAMING
#ifdef  CFG_NRX_600
#define DE_CCX_ROAMING    CFG_INCLUDED
#else
#define DE_CCX_ROAMING    CFG_NOT_INCLUDED
#endif //CHIP VERSION
#endif //DE_CCX_ROAMING


#define DE_AGGREGATE_HI_DATA           CFG_AGGR_ALL

#endif   /* ENV_CONFIG_H */

