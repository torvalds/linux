/*	$OpenBSD: wcstold.c,v 1.1 2009/01/13 18:18:31 kettenis Exp $	*/

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define FUNCNAME	wcstold
typedef long double	float_type;
#define STRTOD_FUNC	strtold

#include "_wcstod.h"
