/** HPI Version Definitions
Development releases have odd minor version.
Production releases have even minor version.

\file hpi_version.h
*/

#ifndef _HPI_VERSION_H
#define _HPI_VERSION_H

/* Use single digits for versions less that 10 to avoid octal. */
/* *** HPI_VER is the only edit required to update version *** */
/** HPI version */
#define HPI_VER HPI_VERSION_CONSTRUCTOR(4, 10, 1)

/** HPI version string in dotted decimal format */
#define HPI_VER_STRING "4.10.01"

/** Library version as documented in hpi-api-versions.txt */
#define HPI_LIB_VER  HPI_VERSION_CONSTRUCTOR(10, 2, 0)

/** Construct hpi version number from major, minor, release numbers */
#define HPI_VERSION_CONSTRUCTOR(maj, min, r) ((maj << 16) + (min << 8) + r)

/** Extract major version from hpi version number */
#define HPI_VER_MAJOR(v) ((int)(v >> 16))
/** Extract minor version from hpi version number */
#define HPI_VER_MINOR(v) ((int)((v >> 8) & 0xFF))
/** Extract release from hpi version number */
#define HPI_VER_RELEASE(v) ((int)(v & 0xFF))

#endif
