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
 * Cronyx Id: ng_cx.h,v 1.1.2.3 2004/01/27 14:39:11 rik Exp $
 * $FreeBSD$
 */

#ifdef NETGRAPH

#ifndef _CX_NETGRAPH_H_
#define _CX_NETGRAPH_H_

#define NG_CX_NODE_TYPE		"cx"
#define NGM_CX_COOKIE		942763600
#define NG_CX_HOOK_RAW		"rawdata"
#define NG_CX_HOOK_DEBUG	"debug"

#endif /* _CX_NETGRAPH_H_ */

#endif /* NETGRAPH */
