/*	$OpenBSD: crtendS.c,v 1.10 2017/01/21 00:45:13 guenther Exp $	*/
/*	$NetBSD: crtend.c,v 1.1 1997/04/16 19:38:24 thorpej Exp $	*/

#include <sys/types.h>
#include "md_init.h"
#include "extern.h"

MD_DATA_SECTION_FLAGS_VALUE(".ctors", "aw", 0);
MD_DATA_SECTION_FLAGS_VALUE(".dtors", "aw", 0);
MD_DATA_SECTION_FLAGS_VALUE(".jcr", "aw", 0);

MD_SECTION_EPILOGUE(".init");
MD_SECTION_EPILOGUE(".fini");
