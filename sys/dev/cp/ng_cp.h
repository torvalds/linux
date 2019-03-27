/*-
 * Defines for Cronyx-Tau-PCI adapter driver.
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
 * $Cronyx: ng_cp.h,v 1.1.2.4 2004/01/27 14:39:11 rik Exp $
 * $FreeBSD$
 */

#ifdef NETGRAPH

#ifndef _CP_NETGRAPH_H_
#define _CP_NETGRAPH_H_

#define NG_CP_NODE_TYPE		"cp"
#define NGM_CP_COOKIE		941049562
#define NG_CP_HOOK_RAW		"rawdata"
#define NG_CP_HOOK_DEBUG	"debug"

#endif /* _CP_NETGRAPH_H_ */

#endif /* NETGRAPH */
