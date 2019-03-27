/*
 * Defines for Cronyx Tau32-PCI adapter driver.
 *
 * Copyright (C) 2004 Cronyx Engineering.
 * Copyright (C) 2004 Kurakin Roman, <rik@FreeBSD.org>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Cronyx: ng_ce.h,v 1.2 2005/04/23 20:11:57 rik Exp $
 * $FreeBSD$
 */

#ifdef NETGRAPH

#ifndef _CE_NETGRAPH_H_
#define _CE_NETGRAPH_H_

#define NG_CE_NODE_TYPE		"ce"
#define NGM_CE_COOKIE		1083172653
#define NG_CE_HOOK_RAW		"rawdata"
#define NG_CE_HOOK_DEBUG	"debug"

#endif /* _CE_NETGRAPH_H_ */

#endif /* NETGRAPH */
