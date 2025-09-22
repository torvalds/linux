/*	$OpenBSD: wcstof.c,v 1.1 2009/01/13 18:18:31 kettenis Exp $	*/

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define FUNCNAME	wcstof
typedef float		float_type;
#define STRTOD_FUNC	strtof

#include "_wcstod.h"
