/*-
 * Defines for Cronyx-Tau adapter driver.
 *
 * Copyright (C) 1999-2004 Cronyx Engineering.
 * Author: Kurakin Roman, <rik@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 * 
 * Cronyx Id: ng_ct.h,v 1.1.2.3 2004/01/27 14:39:11 rik Exp $
 * $FreeBSD$
 */

#ifdef NETGRAPH

#ifndef _CT_NETGRAPH_H_
#define _CT_NETGRAPH_H_

#define NG_CT_NODE_TYPE		"ct"
#define NGM_CT_COOKIE		942835777
#define NG_CT_HOOK_RAW		"rawdata"
#define NG_CT_HOOK_DEBUG	"debug"

#endif /* _CT_NETGRAPH_H_ */

#endif /* NETGRAPH */
