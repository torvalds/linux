/* $OpenBSD: unicode.h,v 1.2 2023/04/13 18:29:36 miod Exp $ */
/* $NetBSD: unicode.h,v 1.1 1999/02/20 18:20:02 drochner Exp $ */

/*
 * some private character definitions for stuff not found
 * in the Unicode database, for communication between
 * terminal emulation and graphics driver
 */

#define _e000U 0xe000 /* mirrored question mark? */
#define _e006U 0xe006 /* N/L control */
#define _e007U 0xe007 /* bracelefttp */
#define _e008U 0xe008 /* braceleftbt */
#define _e009U 0xe009 /* bracerighttp */
#define _e00aU 0xe00a /* bracerighrbt */
#define _e00bU 0xe00b /* braceleftmid */
#define _e00cU 0xe00c /* bracerightmid */
#define _e00dU 0xe00d /* inverted angle? */
#define _e00eU 0xe00e /* angle? */
#define _e00fU 0xe00f /* mirrored not sign? */
