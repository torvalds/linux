/*	$OpenBSD: crtend.c,v 1.12 2017/01/21 00:45:13 guenther Exp $	*/
/*	$NetBSD: crtend.c,v 1.1 1996/09/12 16:59:04 cgd Exp $	*/

#include <sys/types.h>
#include "md_init.h"
#include "extern.h"

MD_DATA_SECTION_FLAGS_VALUE(".ctors", "aw", 0);
MD_DATA_SECTION_FLAGS_VALUE(".dtors", "aw", 0);
MD_DATA_SECTION_FLAGS_VALUE(".eh_frame", "a", 0);
MD_DATA_SECTION_FLAGS_VALUE(".jcr", "aw", 0);

MD_SECTION_EPILOGUE(".init");
MD_SECTION_EPILOGUE(".fini");
