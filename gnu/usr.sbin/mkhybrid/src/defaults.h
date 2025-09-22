/*
 * Header file defaults.h - assorted default values for character strings in
 * the volume descriptor.
 *
 * 	$Id: defaults.h,v 1.2 2023/11/21 08:46:06 jmatthew Exp $
 */

#define  PREPARER_DEFAULT 	NULL
#define  PUBLISHER_DEFAULT	NULL
#ifndef	APPID_DEFAULT
#define  APPID_DEFAULT 		"MKHYBRID ISO9660/HFS FILESYSTEM BUILDER"
#endif
#define  COPYRIGHT_DEFAULT 	NULL
#define  BIBLIO_DEFAULT 	NULL
#define  ABSTRACT_DEFAULT 	NULL
#define  VOLSET_ID_DEFAULT 	NULL
#define  VOLUME_ID_DEFAULT 	"CDROM"
#define  BOOT_CATALOG_DEFAULT   "boot.catalog"
#define  BOOT_IMAGE_DEFAULT     NULL
#define  EFI_BOOT_IMAGE_DEFAULT NULL
#ifdef APPLE_HYB
#define	 DEFTYPE		"TEXT"  /* default Apple TYPE */
#define  DEFCREATOR		"unix"  /* default Apple CREATOR */
#endif /* APPLE_HYB */

#ifdef __QNX__
#define  SYSTEM_ID_DEFAULT 	"QNX"
#endif

#ifdef __osf__
#define  SYSTEM_ID_DEFAULT 	"OSF"
#endif

#ifdef __sun
#ifdef __SVR4
#define  SYSTEM_ID_DEFAULT    "Solaris"
#else
#define  SYSTEM_ID_DEFAULT    "SunOS"
#endif
#endif

#ifdef __hpux
#define  SYSTEM_ID_DEFAULT 	"HP-UX"
#endif

#ifdef __sgi
#define  SYSTEM_ID_DEFAULT 	"SGI"
#endif

#ifdef _AIX
#define  SYSTEM_ID_DEFAULT 	"AIX"
#endif

#ifdef _WIN
#define	SYSTEM_ID_DEFAULT       "Win32"
#endif /* _WIN */

#ifdef __FreeBSD__
#define  SYSTEM_ID_DEFAULT     "FreeBSD"
#endif

#ifdef __OpenBSD__
#define  SYSTEM_ID_DEFAULT     "OpenBSD"
#endif

#ifdef __NetBSD__
#define  SYSTEM_ID_DEFAULT     "NetBSD"
#endif

#ifdef __linux__
#define  SYSTEM_ID_DEFAULT 	"LINUX"
#endif

#ifdef __FreeBSD__
#define  SYSTEM_ID_DEFAULT 	"FreeBSD"
#endif

#ifdef __OpenBSD__
#define  SYSTEM_ID_DEFAULT 	"OpenBSD"
#endif

#ifdef __NetBSD__
#define  SYSTEM_ID_DEFAULT 	"NetBSD"
#endif

#ifndef SYSTEM_ID_DEFAULT
#define  SYSTEM_ID_DEFAULT 	"Unknown"
#endif
