/* $OpenBSD: wscons_features.h,v 1.5 2023/01/12 20:39:37 nicm Exp $ */
/* public domain */

/*
 * This file contains the logic used to enable several optional features
 * of the wscons framework:
 *
 * HAVE_WSMOUSED_SUPPORT
 *	defined to enable support for wsmoused(8)
 * HAVE_BURNER_SUPPORT
 *	defined to enable screen blanking functionality, controlled by
 *	wsconsctl(8)
 * HAVE_SCROLLBACK_SUPPORT
 *	defined to enable xterm-like shift-PgUp scrollback if the underlying
 *	wsdisplay supports this
 * HAVE_JUMP_SCROLL
 *	defined to enable jump scroll in the textmode emulation code
 * HAVE_UTF8_SUPPORT
 *	defined to enable UTF-8 mode and escape sequences in the textmode
 *	emulation code
 * HAVE_RESTARTABLE_EMULOPS
 *	defined to disable most of the restartable emulops code (to be used
 *	only if all wsdisplay drivers are compliant, i.e. no udl(4) in the
 *	kernel configuration)
 * HAVE_DOUBLE_WIDTH_HEIGHT
 *	defined to enable escape sequences for double width and height
 *	characters
 */

#ifdef _KERNEL

#ifndef	SMALL_KERNEL
#define	HAVE_WSMOUSED_SUPPORT
#define	HAVE_BURNER_SUPPORT
#define	HAVE_SCROLLBACK_SUPPORT
#define	HAVE_JUMP_SCROLL
#define	HAVE_UTF8_SUPPORT
#define	HAVE_RESTARTABLE_EMULOPS
#define	HAVE_DOUBLE_WIDTH_HEIGHT
#endif

#endif
