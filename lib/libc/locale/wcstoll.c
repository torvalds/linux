/*	$OpenBSD: wcstoll.c,v 1.2 2005/08/08 08:05:35 espie Exp $	*/
/* $NetBSD: wcstoll.c,v 1.1 2003/03/11 09:21:23 tshiozak Exp $ */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#include "wctoint.h"

#define	FUNCNAME	wcstoll
typedef long long int int_type;
#define	MIN_VALUE	LLONG_MIN
#define	MAX_VALUE	LLONG_MAX

#include "_wcstol.h"
