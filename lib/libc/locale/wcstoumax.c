/*	$OpenBSD: wcstoumax.c,v 1.1 2009/01/13 18:13:51 kettenis Exp $	*/
/*	$NetBSD: wcstoul.c,v 1.2 2003/03/11 09:21:24 tshiozak Exp $	*/

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#include "wctoint.h"

#define	FUNCNAME	wcstoumax
typedef uintmax_t	uint_type;
#define	MAX_VALUE	UINTMAX_MAX

#include "_wcstoul.h"
