/** @file
 * IPRT - Path Manipulation.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___iprt_path_h
#define ___iprt_path_h

#include <iprt/cdefs.h>
#include <iprt/types.h>
#ifdef IN_RING3
# include <iprt/fs.h>
#endif



RT_C_DECLS_BEGIN

/** @defgroup grp_rt_path   RTPath - Path Manipulation
 * @ingroup grp_rt
 * @{
 */

/**
 * Host max path (the reasonable value).
 * @remarks defined both by iprt/param.h and iprt/path.h.
 */
#if !defined(___iprt_param_h) || defined(DOXYGEN_RUNNING)
# define RTPATH_MAX         (4096 + 4)    /* (PATH_MAX + 1) on linux w/ some alignment */
#endif

/** @def RTPATH_TAG
 * The default allocation tag used by the RTPath allocation APIs.
 *
 * When not defined before the inclusion of iprt/string.h, this will default to
 * the pointer to the current file name.  The string API will make of use of
 * this as pointer to a volatile but read-only string.
 */
#ifndef RTPATH_TAG
# define RTPATH_TAG     (__FILE__)
#endif


/** @name RTPATH_F_XXX - Generic flags for APIs working on the file system.
 * @{ */
/** Last component: Work on the link. */
#define RTPATH_F_ON_LINK          RT_BIT_32(0)
/** Last component: Follow if link. */
#define RTPATH_F_FOLLOW_LINK      RT_BIT_32(1)
/** Don't allow symbolic links as part of the path.
 * @remarks this flag is currently not implemented and will be ignored. */
#define RTPATH_F_NO_SYMLINKS      RT_BIT_32(2)
/** @} */

/** Validates a flags parameter containing RTPATH_F_*.
 * @remarks The parameters will be referenced multiple times. */
#define RTPATH_F_IS_VALID(a_fFlags, a_fIgnore) \
    (    ((a_fFlags) & ~(uint32_t)((a_fIgnore) | RTPATH_F_NO_SYMLINKS)) == RTPATH_F_ON_LINK \
      || ((a_fFlags) & ~(uint32_t)((a_fIgnore) | RTPATH_F_NO_SYMLINKS)) == RTPATH_F_FOLLOW_LINK )


/** @name RTPATH_STR_F_XXX - Generic flags for APIs working with path strings.
 * @{
 */
/** Host OS path style (default 0 value). */
#define RTPATH_STR_F_STYLE_HOST         UINT32_C(0x00000000)
/** DOS, OS/2 and Windows path style. */
#define RTPATH_STR_F_STYLE_DOS          UINT32_C(0x00000001)
/** Unix path style. */
#define RTPATH_STR_F_STYLE_UNIX         UINT32_C(0x00000002)
/** Reserved path style. */
#define RTPATH_STR_F_STYLE_RESERVED     UINT32_C(0x00000003)
/** The path style mask. */
#define RTPATH_STR_F_STYLE_MASK         UINT32_C(0x00000003)
/** Partial path - no start.
 * This causes the API to skip the root specification parsing.  */
#define RTPATH_STR_F_NO_START           UINT32_C(0x00000010)
/** Partial path - no end.
 * This causes the API to skip the filename and dir-slash parsing.  */
#define RTPATH_STR_F_NO_END             UINT32_C(0x00000020)
/** Partial path - no start and no end. */
#define RTPATH_STR_F_MIDDLE             (RTPATH_STR_F_NO_START | RTPATH_STR_F_NO_END)

/** Reserved for future use. */
#define RTPATH_STR_F_RESERVED_MASK      UINT32_C(0x0000ffcc)
/** @} */

/** Validates a flags parameter containing RTPATH_FSTR_.
 * @remarks The parameters will be references multiple times.  */
#define RTPATH_STR_F_IS_VALID(a_fFlags, a_fIgnore) \
      (   ((a_fFlags) & ~((uint32_t)(a_fIgnore) | RTPATH_STR_F_STYLE_MASK | RTPATH_STR_F_MIDDLE)) == 0 \
       && ((a_fFlags) & RTPATH_STR_F_STYLE_MASK) != RTPATH_STR_F_STYLE_RESERVED \
       && ((a_fFlags) & RTPATH_STR_F_RESERVED_MASK) == 0 )


/** @def RTPATH_STYLE
 * The host path style. This is set to RTPATH_STR_F_STYLE_DOS,
 * RTPATH_STR_F_STYLE_UNIX, or other future styles. */
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
# define RTPATH_STYLE       RTPATH_STR_F_STYLE_DOS
#else
# define RTPATH_STYLE       RTPATH_STR_F_STYLE_UNIX
#endif


/** @def RTPATH_SLASH
 * The preferred slash character.
 *
 * @remark IPRT will always accept unix slashes. So, normally you would
 *         never have to use this define.
 */
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
# define RTPATH_SLASH       '\\'
#elif RTPATH_STYLE == RTPATH_STR_F_STYLE_UNIX
# define RTPATH_SLASH       '/'
#else
# error "Unsupported RTPATH_STYLE value."
#endif

/** @deprecated Use '/'! */
#define RTPATH_DELIMITER    RTPATH_SLASH


/** @def RTPATH_SLASH_STR
 * The preferred slash character as a string, handy for concatenations
 * with other strings.
 *
 * @remark IPRT will always accept unix slashes. So, normally you would
 *         never have to use this define.
 */
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
# define RTPATH_SLASH_STR   "\\"
#elif RTPATH_STYLE == RTPATH_STR_F_STYLE_UNIX
# define RTPATH_SLASH_STR   "/"
#else
# error "Unsupported RTPATH_STYLE value."
#endif


/** @def RTPATH_IS_SLASH
 * Checks if a character is a slash.
 *
 * @returns true if it's a slash and false if not.
 * @returns @param      a_ch    Char to check.
 */
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
# define RTPATH_IS_SLASH(a_ch)      ( (a_ch) == '\\' || (a_ch) == '/' )
#elif RTPATH_STYLE == RTPATH_STR_F_STYLE_UNIX
# define RTPATH_IS_SLASH(a_ch)      ( (a_ch) == '/' )
#else
# error "Unsupported RTPATH_STYLE value."
#endif


/** @def RTPATH_IS_VOLSEP
 * Checks if a character marks the end of the volume specification.
 *
 * @remark  This is sufficient for the drive letter concept on PC.
 *          However it might be insufficient on other platforms
 *          and even on PC a UNC volume spec won't be detected this way.
 *          Use the RTPath@<too be created@>() instead.
 *
 * @returns true if it is and false if it isn't.
 * @returns @param      a_ch    Char to check.
 */
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
# define RTPATH_IS_VOLSEP(a_ch)   ( (a_ch) == ':' )
#elif RTPATH_STYLE == RTPATH_STR_F_STYLE_UNIX
# define RTPATH_IS_VOLSEP(a_ch)   (false)
#else
# error "Unsupported RTPATH_STYLE value."
#endif


/** @def RTPATH_IS_SEP
 * Checks if a character is path component separator
 *
 * @returns true if it is and false if it isn't.
 * @returns @param      a_ch    Char to check.
 * @
 */
#define RTPATH_IS_SEP(a_ch)     ( RTPATH_IS_SLASH(a_ch) || RTPATH_IS_VOLSEP(a_ch) )


/**
 * Checks if the path exists.
 *
 * Symbolic links will all be attempted resolved and broken links means false.
 *
 * @returns true if it exists and false if it doesn't.
 * @param   pszPath     The path to check.
 */
RTDECL(bool) RTPathExists(const char *pszPath);

/**
 * Checks if the path exists.
 *
 * @returns true if it exists and false if it doesn't.
 * @param   pszPath     The path to check.
 * @param   fFlags      RTPATH_F_ON_LINK or RTPATH_F_FOLLOW_LINK.
 */
RTDECL(bool) RTPathExistsEx(const char *pszPath, uint32_t fFlags);

/**
 * Sets the current working directory of the process.
 *
 * @returns IPRT status code.
 * @param   pszPath         The path to the new working directory.
 */
RTDECL(int)  RTPathSetCurrent(const char *pszPath);

/**
 * Gets the current working directory of the process.
 *
 * @returns IPRT status code.
 * @param   pszPath         Where to store the path.
 * @param   cchPath         The size of the buffer pszPath points to.
 */
RTDECL(int)  RTPathGetCurrent(char *pszPath, size_t cchPath);

/**
 * Gets the current working directory on the specified drive.
 *
 * On systems without drive letters, the root slash will be returned.
 *
 * @returns IPRT status code.
 * @param   chDrive         The drive we're querying the driver letter on.
 * @param   pszPath         Where to store the working directroy path.
 * @param   cbPath          The size of the buffer pszPath points to.
 */
RTDECL(int) RTPathGetCurrentOnDrive(char chDrive, char *pszPath, size_t cbPath);

/**
 * Gets the current working drive of the process.
 *
 * Normally drive letter and colon will be returned, never trailing a root
 * slash.  If the current directory is on a UNC share, the root of the share
 * will be returned.  On systems without drive letters, an empty string is
 * returned for consistency.
 *
 * @returns IPRT status code.
 * @param   pszPath         Where to store the working drive or UNC root.
 * @param   cbPath          The size of the buffer pszPath points to.
 */
RTDECL(int) RTPathGetCurrentDrive(char *pszPath, size_t cbPath);

/**
 * Get the real path (no symlinks, no . or .. components), must exist.
 *
 * @returns iprt status code.
 * @param   pszPath         The path to resolve.
 * @param   pszRealPath     Where to store the real path.
 * @param   cchRealPath     Size of the buffer.
 */
RTDECL(int) RTPathReal(const char *pszPath, char *pszRealPath, size_t cchRealPath);

/**
 * Same as RTPathReal only the result is RTStrDup()'ed.
 *
 * @returns Pointer to real path. Use RTStrFree() to free this string.
 * @returns NULL if RTPathReal() or RTStrDup() fails.
 * @param   pszPath         The path to resolve.
 */
RTDECL(char *) RTPathRealDup(const char *pszPath);

/**
 * Get the absolute path (starts from root, no . or .. components), doesn't have
 * to exist. Note that this method is designed to never perform actual file
 * system access, therefore symlinks are not resolved.
 *
 * @returns iprt status code.
 * @param   pszPath         The path to resolve.
 * @param   pszAbsPath      Where to store the absolute path.
 * @param   cchAbsPath      Size of the buffer.
 */
RTDECL(int) RTPathAbs(const char *pszPath, char *pszAbsPath, size_t cchAbsPath);

/**
 * Same as RTPathAbs only the result is RTStrDup()'ed.
 *
 * @returns Pointer to the absolute path. Use RTStrFree() to free this string.
 * @returns NULL if RTPathAbs() or RTStrDup() fails.
 * @param   pszPath         The path to resolve.
 */
RTDECL(char *) RTPathAbsDup(const char *pszPath);

/**
 * Get the absolute path (no symlinks, no . or .. components), assuming the
 * given base path as the current directory. The resulting path doesn't have
 * to exist.
 *
 * @returns iprt status code.
 * @param   pszBase         The base path to act like a current directory.
 *                          When NULL, the actual cwd is used (i.e. the call
 *                          is equivalent to RTPathAbs(pszPath, ...).
 * @param   pszPath         The path to resolve.
 * @param   pszAbsPath      Where to store the absolute path.
 * @param   cchAbsPath      Size of the buffer.
 */
RTDECL(int) RTPathAbsEx(const char *pszBase, const char *pszPath, char *pszAbsPath, size_t cchAbsPath);

/**
 * Same as RTPathAbsEx only the result is RTStrDup()'ed.
 *
 * @returns Pointer to the absolute path. Use RTStrFree() to free this string.
 * @returns NULL if RTPathAbsEx() or RTStrDup() fails.
 * @param   pszBase         The base path to act like a current directory.
 *                          When NULL, the actual cwd is used (i.e. the call
 *                          is equivalent to RTPathAbs(pszPath, ...).
 * @param   pszPath         The path to resolve.
 */
RTDECL(char *) RTPathAbsExDup(const char *pszBase, const char *pszPath);

/**
 * Strips the filename from a path. Truncates the given string in-place by overwriting the
 * last path separator character with a null byte in a platform-neutral way.
 *
 * @param   pszPath     Path from which filename should be extracted, will be truncated.
 *                      If the string contains no path separator, it will be changed to a "." string.
 */
RTDECL(void) RTPathStripFilename(char *pszPath);

/**
 * Strips the last suffix from a path.
 *
 * @param   pszPath     Path which suffix should be stripped.
 */
RTDECL(void) RTPathStripSuffix(char *pszPath);

/**
 * Strips the trailing slashes of a path name.
 *
 * Won't strip root slashes.
 *
 * @returns The new length of pszPath.
 * @param   pszPath     Path to strip.
 */
RTDECL(size_t) RTPathStripTrailingSlash(char *pszPath);

/**
 * Ensures that the path has a trailing path separator such that file names can
 * be appended without further work.
 *
 * This can be helpful when preparing for efficiently combining a directory path
 * with the filenames returned by RTDirRead.  The return value gives you the
 * position at which you copy the RTDIRENTRY::szName to construct a valid path
 * to it.
 *
 * @returns The length of the path, 0 on buffer overflow.
 * @param   pszPath     The path.
 * @param   cbPath      The length of the path buffer @a pszPath points to.
 */
RTDECL(size_t) RTPathEnsureTrailingSeparator(char *pszPath, size_t cbPath);

/**
 * Changes all the slashes in the specified path to DOS style.
 *
 * Unless @a fForce is set, nothing will be done when on a UNIX flavored system
 * since paths wont work with DOS style slashes there.
 *
 * @returns @a pszPath.
 * @param   pszPath             The path to modify.
 * @param   fForce              Whether to force the conversion on non-DOS OSes.
 */
RTDECL(char *) RTPathChangeToDosSlashes(char *pszPath, bool fForce);

/**
 * Changes all the slashes in the specified path to unix style.
 *
 * Unless @a fForce is set, nothing will be done when on a UNIX flavored system
 * since paths wont work with DOS style slashes there.
 *
 * @returns @a pszPath.
 * @param   pszPath             The path to modify.
 * @param   fForce              Whether to force the conversion on non-DOS OSes.
 */
RTDECL(char *) RTPathChangeToUnixSlashes(char *pszPath, bool fForce);

/**
 * Simple parsing of the a path.
 *
 * It figures the length of the directory component, the offset of
 * the file name and the location of the suffix dot.
 *
 * @returns The path length.
 *
 * @param   pszPath     Path to find filename in.
 * @param   pcchDir     Where to put the length of the directory component. If
 *                      no directory, this will be 0. Optional.
 * @param   poffName    Where to store the filename offset.
 *                      If empty string or if it's ending with a slash this
 *                      will be set to -1. Optional.
 * @param   poffSuff    Where to store the suffix offset (the last dot).
 *                      If empty string or if it's ending with a slash this
 *                      will be set to -1. Optional.
 */
RTDECL(size_t) RTPathParseSimple(const char *pszPath, size_t *pcchDir, ssize_t *poffName, ssize_t *poffSuff);

/**
 * Finds the filename in a path.
 *
 * @returns Pointer to filename within pszPath.
 * @returns NULL if no filename (i.e. empty string or ends with a slash).
 * @param   pszPath     Path to find filename in.
 */
RTDECL(char *) RTPathFilename(const char *pszPath);

/**
 * Finds the filename in a path, extended version.
 *
 * @returns Pointer to filename within pszPath.
 * @returns NULL if no filename (i.e. empty string or ends with a slash).
 * @param   pszPath     Path to find filename in.
 * @param   fFlags      RTPATH_STR_F_STYLE_XXX. Other RTPATH_STR_F_XXX flags
 *                      will be ignored.
 */
RTDECL(char *) RTPathFilenameEx(const char *pszPath, uint32_t fFlags);

/**
 * Finds the suffix part of in a path (last dot and onwards).
 *
 * @returns Pointer to suffix within pszPath.
 * @returns NULL if no suffix
 * @param   pszPath     Path to find suffix in.
 *
 * @remarks IPRT terminology: A suffix includes the dot, the extension starts
 *          after the dot. For instance suffix '.txt' and extension 'txt'.
 */
RTDECL(char *) RTPathSuffix(const char *pszPath);

/**
 * Checks if a path has an extension / suffix.
 *
 * @returns true if extension / suffix present.
 * @returns false if no extension / suffix.
 * @param   pszPath     Path to check.
 */
RTDECL(bool) RTPathHasSuffix(const char *pszPath);
/** Same thing, different name.  */
#define RTPathHasExt RTPathHasSuffix

/**
 * Checks if a path includes more than a filename.
 *
 * @returns true if path present.
 * @returns false if no path.
 * @param   pszPath     Path to check.
 */
RTDECL(bool) RTPathHasPath(const char *pszPath);
/** Misspelled, don't use.  */
#define RTPathHavePath  RTPathHasPath

/**
 * Checks if the path starts with a root specifier or not.
 *
 * @returns @c true if it starts with root, @c false if not.
 *
 * @param   pszPath     Path to check.
 */
RTDECL(bool) RTPathStartsWithRoot(const char *pszPath);

/**
 * Counts the components in the specified path.
 *
 * An empty string has zero components.  A lone root slash is considered have
 * one.  The paths "/init" and "/bin/" are considered having two components.  An
 * UNC share specifier like "\\myserver\share" will be considered as one single
 * component.
 *
 * @returns The number of path components.
 * @param   pszPath     The path to parse.
 */
RTDECL(size_t) RTPathCountComponents(const char *pszPath);

/**
 * Copies the specified number of path components from @a pszSrc and into @a
 * pszDst.
 *
 * @returns VINF_SUCCESS or VERR_BUFFER_OVERFLOW.  In the latter case the buffer
 *          is not touched.
 *
 * @param   pszDst      The destination buffer.
 * @param   cbDst       The size of the destination buffer.
 * @param   pszSrc      The source path.
 * @param   cComponents The number of components to copy from @a pszSrc.
 */
RTDECL(int) RTPathCopyComponents(char *pszDst, size_t cbDst, const char *pszSrc, size_t cComponents);

/** @name Path properties returned by RTPathParse and RTPathSplit.
 * @{ */

/** Indicates that there is a filename.
 * If not set, either a lone root spec was given (RTPATH_PROP_UNC,
 * RTPATH_PROP_ROOT_SLASH, or RTPATH_PROP_VOLUME) or the final component had a
 * trailing slash (RTPATH_PROP_DIR_SLASH). */
#define RTPATH_PROP_FILENAME        UINT16_C(0x0001)
/** Indicates that a directory was specified using a trailing slash.
 * @note This is not set for lone root specifications (RTPATH_PROP_UNC,
 *       RTPATH_PROP_ROOT_SLASH, or RTPATH_PROP_VOLUME).
 * @note The slash is not counted into the last component. However, it is
 *       counted into cchPath. */
#define RTPATH_PROP_DIR_SLASH       UINT16_C(0x0002)

/** The filename has a suffix (extension). */
#define RTPATH_PROP_SUFFIX          UINT16_C(0x0004)
/** Indicates that this is an UNC path (Windows and OS/2 only).
 *
 * UNC = Universal Naming Convention.  It is on the form '//Computer/',
 * '//Namespace/', '//ComputerName/Resource' and '//Namespace/Resource'.
 * RTPathParse, RTPathSplit and friends does not consider the 'Resource'  as
 * part of the UNC root specifier.  Thus the root specs for the above examples
 * would be '//ComputerName/' or '//Namespace/'.
 *
 * Please note that  '//something' is not a UNC path, there must be a slash
 * following the computer or namespace.
 */
#define RTPATH_PROP_UNC             UINT16_C(0x0010)
/** A root slash was specified (unix style root).
 * (While the path must relative if not set, this being set doesn't make it
 * absolute.)
 *
 * This will be set in the following examples: '/', '/bin', 'C:/', 'C:/Windows',
 * '//./', '//./PhysicalDisk0', '//example.org/', and '//example.org/share'.
 *
 * It will not be set for the following examples: '.', 'bin/ls', 'C:', and
 * 'C:Windows'.
 */
#define RTPATH_PROP_ROOT_SLASH      UINT16_C(0x0020)
/** A volume is specified (Windows, DOS and OS/2).
 * For examples: 'C:', 'C:/', and 'A:/AutoExec.bat'. */
#define RTPATH_PROP_VOLUME          UINT16_C(0x0040)
/** The path is absolute, i.e. has a root specifier (root-slash,
 * volume or UNC) and contains no winding '..' bits, though it may contain
 * unnecessary slashes (RTPATH_PROP_EXTRA_SLASHES) and '.' components
 * (RTPATH_PROP_DOT_REFS).
 *
 * On systems without volumes and UNC (unix style) it will be set for '/',
 * '/bin/ls', and '/bin//./ls', but not for 'bin/ls', /bin/../usr/bin/env',
 * '/./bin/ls' or '/.'.
 *
 * On systems with volumes, it will be set for 'C:/', C:/Windows', and
 * 'C:/./Windows//', but not for 'C:', 'C:Windows', or 'C:/Windows/../boot.ini'.
 *
 * On systems with UNC paths, it will be set for '//localhost/',
 * '//localhost/C$', '//localhost/C$/Windows/System32', '//localhost/.', and
 * '//localhost/C$//./AutoExec.bat', but not for
 * '//localhost/C$/Windows/../AutoExec.bat'.
 *
 * @note For the RTPathAbs definition, this flag needs to be set while both
 *       RTPATH_PROP_EXTRA_SLASHES and RTPATH_PROP_DOT_REFS must be cleared.
 */
#define RTPATH_PROP_ABSOLUTE        UINT16_C(0x0100)
/** Relative path. Inverse of RTPATH_PROP_ABSOLUTE. */
#define RTPATH_PROP_RELATIVE        UINT16_C(0x0200)
/** The path contains unnecessary slashes. Meaning, that if  */
#define RTPATH_PROP_EXTRA_SLASHES   UINT16_C(0x0400)
/** The path contains references to the special '.' (dot) directory link. */
#define RTPATH_PROP_DOT_REFS        UINT16_C(0x0800)
/** The path contains references to the special '..' (dot) directory link.
 * RTPATH_PROP_RELATIVE will always be set together with this.  */
#define RTPATH_PROP_DOTDOT_REFS     UINT16_C(0x1000)


/** Macro to determin whether to insert a slash after the first component when
 * joining it with something else.
 * (All other components in a split or parsed path requies slashes added.) */
#define RTPATH_PROP_FIRST_NEEDS_NO_SLASH(a_fProps) \
    RT_BOOL( (a_fProps) & (RTPATH_PROP_ROOT_SLASH | RTPATH_PROP_VOLUME | RTPATH_PROP_UNC) )

/** Macro to determin whether there is a root specification of any kind
 * (unix, volumes, unc). */
#define RTPATH_PROP_HAS_ROOT_SPEC(a_fProps) \
    RT_BOOL( (a_fProps) & (RTPATH_PROP_ROOT_SLASH | RTPATH_PROP_VOLUME | RTPATH_PROP_UNC) )

/** @} */


/**
 * Parsed path.
 *
 * The first component is the root, volume or UNC specifier, if present.  Use
 * RTPATH_PROP_HAS_ROOT_SPEC() on RTPATHPARSED::fProps to determine its
 * presence.
 *
 * Other than the root component, no component will include directory separators
 * (slashes).
 */
typedef struct RTPATHPARSED
{
    /** Number of path components.
     * This will always be set on VERR_BUFFER_OVERFLOW returns from RTPathParsed
     * so the caller can calculate the required buffer size. */
    uint16_t    cComps;
    /** Path property flags, RTPATH_PROP_XXX */
    uint16_t    fProps;
    /** On success this is the length of the described path, i.e. sum of all
     * component lengths and necessary separators.
     * Do NOT use this to index in the source path in case it contains
     * unnecessary slashes that RTPathParsed has ignored here. */
    uint16_t    cchPath;
    /** Reserved for future use. */
    uint16_t    u16Reserved;
    /** The offset of the filename suffix, offset of the NUL char if none. */
    uint16_t    offSuffix;
    /** The lenght of the suffix. */
    uint16_t    cchSuffix;
    /** Array of component descriptors (variable size).
     * @note Don't try figure the end of the input path by adding up off and cch
     *       of the last component.  If RTPATH_PROP_DIR_SLASH is set, there may
     *       be one or more trailing slashes that are unaccounted for! */
    struct
    {
        /** The offset of the component. */
        uint16_t    off;
        /** The length of the component. */
        uint16_t    cch;
    } aComps[1];
} RTPATHPARSED;
/** Pointer to to a parsed path result. */
typedef RTPATHPARSED *PRTPATHPARSED;
/** Pointer to to a const parsed path result. */
typedef RTPATHPARSED *PCRTPATHPARSED;


/**
 * Parses the path.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_POINTER if pParsed or pszPath is an invalid pointer.
 * @retval  VERR_INVALID_PARAMETER if cbOutput is less than the RTPATHPARSED
 *          strucuture. No output. (asserted)
 * @retval  VERR_BUFFER_OVERFLOW there are more components in the path than
 *          there is space in aComps. The required amount of space can be
 *          determined from the pParsed->cComps:
 *          @code
 *              RT_OFFSETOF(RTPATHPARSED, aComps[pParsed->cComps])
 *          @endcode
 * @retval  VERR_PATH_ZERO_LENGTH if the path is empty.
 *
 * @param   pszPath             The path to parse.
 * @param   pParsed             Where to store the details of the parsed path.
 * @param   cbParsed            The size of the buffer. Must be at least the
 *                              size of RTPATHPARSED.
 * @param   fFlags              Combination of RTPATH_STR_F_XXX flags.
 *                              Most users will pass 0.
 * @sa      RTPathSplit, RTPathSplitA.
 */
RTDECL(int) RTPathParse(const char *pszPath, PRTPATHPARSED pParsed, size_t cbParsed, uint32_t fFlags);

/**
 * Reassembles a path parsed by RTPathParse.
 *
 * This will be more useful as more APIs manipulating the RTPATHPARSED output
 * are added.
 *
 * @returns IPRT status code.
 * @retval  VERR_BUFFER_OVERFLOW if @a cbDstPath is less than or equal to
 *          RTPATHPARSED::cchPath.
 *
 * @param   pszSrcPath          The source path.
 * @param   pParsed             The parser output for @a pszSrcPath.
 * @param   fFlags              Combination of RTPATH_STR_F_STYLE_XXX.
 *                              Most users will pass 0.
 * @param   pszDstPath          Pointer to the buffer where the path is to be
 *                              reassembled.
 * @param   cbDstPath           The size of the output buffer.
 */
RTDECL(int) RTPathParsedReassemble(const char *pszSrcPath, PRTPATHPARSED pParsed, uint32_t fFlags,
                                   char *pszDstPath, size_t cbDstPath);


/**
 * Output buffer for RTPathSplit and RTPathSplitA.
 */
typedef struct RTPATHSPLIT
{
    /** Number of path components.
     * This will always be set on VERR_BUFFER_OVERFLOW returns from RTPathParsed
     * so the caller can calculate the required buffer size. */
    uint16_t    cComps;
    /** Path property flags, RTPATH_PROP_XXX */
    uint16_t    fProps;
    /** On success this is the length of the described path, i.e. sum of all
     * component lengths and necessary separators.
     * Do NOT use this to index in the source path in case it contains
     * unnecessary slashes that RTPathSplit has ignored here. */
    uint16_t    cchPath;
    /** Reserved (internal use).  */
    uint16_t    u16Reserved;
    /** The amount of memory used (on success) or required (on
     *  VERR_BUFFER_OVERFLOW) of this structure and it's strings. */
    uint32_t    cbNeeded;
    /** Pointer to the filename suffix (the dot), if any. Points to the NUL
     * character of the last component if none or if RTPATH_PROP_DIR_SLASH is
     * present. */
    const char *pszSuffix;
    /** Array of component strings (variable size). */
    char       *apszComps[1];
} RTPATHSPLIT;
/** Pointer to a split path buffer. */
typedef RTPATHSPLIT *PRTPATHSPLIT;
/** Pointer to a const split path buffer. */
typedef RTPATHSPLIT const *PCRTPATHSPLIT;

/**
 * Splits the path into individual component strings, carved from user supplied
 * the given buffer block.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_POINTER if pParsed or pszPath is an invalid pointer.
 * @retval  VERR_INVALID_PARAMETER if cbOutput is less than the RTPATHSPLIT
 *          strucuture. No output. (asserted)
 * @retval  VERR_BUFFER_OVERFLOW there are more components in the path than
 *          there is space in aComps. The required amount of space can be
 *          determined from the pParsed->cComps:
 *          @code
 *              RT_OFFSETOF(RTPATHPARSED, aComps[pParsed->cComps])
 *          @endcode
 * @retval  VERR_PATH_ZERO_LENGTH if the path is empty.
 * @retval  VERR_FILENAME_TOO_LONG if the filename is too long (close to 64 KB).
 *
 * @param   pszPath             The path to parse.
 * @param   pSplit              Where to store the details of the parsed path.
 * @param   cbSplit             The size of the buffer pointed to by @a pSplit
 *                              (variable sized array at the end).  Must be at
 *                              least the size of RTPATHSPLIT.
 * @param   fFlags              Combination of RTPATH_STR_F_XXX flags.
 *                              Most users will pass 0.
 *
 * @sa      RTPathSplitA, RTPathParse.
 */
RTDECL(int) RTPathSplit(const char *pszPath, PRTPATHSPLIT pSplit, size_t cbSplit, uint32_t fFlags);

/**
 * Splits the path into individual component strings, allocating the buffer on
 * the default thread heap.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_POINTER if pParsed or pszPath is an invalid pointer.
 * @retval  VERR_PATH_ZERO_LENGTH if the path is empty.
 *
 * @param   pszPath             The path to parse.
 * @param   ppSplit             Where to return the pointer to the output on
 *                              success.  This must be freed by calling
 *                              RTPathSplitFree().
 * @param   fFlags              Combination of RTPATH_STR_F_XXX flags.
 *                              Most users will pass 0.
 * @sa      RTPathSplitFree, RTPathSplit, RTPathParse.
 */
#define RTPathSplitA(pszPath, ppSplit, fFlags)      RTPathSplitATag(pszPath, ppSplit, fFlags, RTPATH_TAG)

/**
 * Splits the path into individual component strings, allocating the buffer on
 * the default thread heap.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_POINTER if pParsed or pszPath is an invalid pointer.
 * @retval  VERR_PATH_ZERO_LENGTH if the path is empty.
 *
 * @param   pszPath             The path to parse.
 * @param   ppSplit             Where to return the pointer to the output on
 *                              success.  This must be freed by calling
 *                              RTPathSplitFree().
 * @param   fFlags              Combination of RTPATH_STR_F_XXX flags.
 *                              Most users will pass 0.
 * @param   pszTag              Allocation tag used for statistics and such.
 * @sa      RTPathSplitFree, RTPathSplit, RTPathParse.
 */
RTDECL(int) RTPathSplitATag(const char *pszPath, PRTPATHSPLIT *ppSplit, uint32_t fFlags, const char *pszTag);

/**
 * Frees buffer returned by RTPathSplitA.
 *
 * @param   pSplit              What RTPathSplitA returned.
 * @sa      RTPathSplitA
 */
RTDECL(void) RTPathSplitFree(PRTPATHSPLIT pSplit);

/**
 * Reassembles a path parsed by RTPathSplit.
 *
 * This will be more useful as more APIs manipulating the RTPATHSPLIT output are
 * added.
 *
 * @returns IPRT status code.
 * @retval  VERR_BUFFER_OVERFLOW if @a cbDstPath is less than or equal to
 *          RTPATHSPLIT::cchPath.
 *
 * @param   pSplit              A split path (see RTPathSplit, RTPathSplitA).
 * @param   fFlags              Combination of RTPATH_STR_F_STYLE_XXX.
 *                              Most users will pass 0.
 * @param   pszDstPath          Pointer to the buffer where the path is to be
 *                              reassembled.
 * @param   cbDstPath           The size of the output buffer.
 */
RTDECL(int) RTPathSplitReassemble(PRTPATHSPLIT pSplit, uint32_t fFlags, char *pszDstPath, size_t cbDstPath);

/**
 * Checks if the two paths leads to the file system object.
 *
 * If the objects exist, we'll query attributes for them.  If that's not
 * conclusive (some OSes) or one of them doesn't exist, we'll use a combination
 * of RTPathAbs and RTPathCompare to determine the result.
 *
 * @returns true, false, or VERR_FILENAME_TOO_LONG.
 * @param   pszPath1            The first path.
 * @param   pszPath2            The seoncd path.
 */
RTDECL(int) RTPathIsSame(const char *pszPath1, const char *pszPath2);


/**
 * Compares two paths.
 *
 * The comparison takes platform-dependent details into account,
 * such as:
 * <ul>
 * <li>On DOS-like platforms, both separator chars (|\| and |/|) are considered
 *     to be equal.
 * <li>On platforms with case-insensitive file systems, mismatching characters
 *     are uppercased and compared again.
 * </ul>
 *
 * @returns @< 0 if the first path less than the second path.
 * @returns 0 if the first path identical to the second path.
 * @returns @> 0 if the first path greater than the second path.
 *
 * @param   pszPath1    Path to compare (must be an absolute path).
 * @param   pszPath2    Path to compare (must be an absolute path).
 *
 * @remarks File system details are currently ignored. This means that you won't
 *          get case-insensitive compares on unix systems when a path goes into a
 *          case-insensitive filesystem like FAT, HPFS, HFS, NTFS, JFS, or
 *          similar. For NT, OS/2 and similar you'll won't get case-sensitive
 *          compares on a case-sensitive file system.
 */
RTDECL(int) RTPathCompare(const char *pszPath1, const char *pszPath2);

/**
 * Checks if a path starts with the given parent path.
 *
 * This means that either the path and the parent path matches completely, or
 * that the path is to some file or directory residing in the tree given by the
 * parent directory.
 *
 * The path comparison takes platform-dependent details into account,
 * see RTPathCompare() for details.
 *
 * @returns |true| when \a pszPath starts with \a pszParentPath (or when they
 *          are identical), or |false| otherwise.
 *
 * @param   pszPath         Path to check, must be an absolute path.
 * @param   pszParentPath   Parent path, must be an absolute path.
 *                          No trailing directory slash!
 *
 * @remarks This API doesn't currently handle root directory compares in a
 *          manner consistent with the other APIs. RTPathStartsWith(pszSomePath,
 *          "/") will not work if pszSomePath isn't "/".
 */
RTDECL(bool) RTPathStartsWith(const char *pszPath, const char *pszParentPath);

/**
 * Appends one partial path to another.
 *
 * The main purpose of this function is to deal correctly with the slashes when
 * concatenating the two partial paths.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the result is too big to fit within
 *          cbPathDst bytes. No changes has been made.
 * @retval  VERR_INVALID_PARAMETER if the string pointed to by pszPath is longer
 *          than cbPathDst-1 bytes (failed to find terminator). Asserted.
 *
 * @param   pszPath         The path to append pszAppend to. This serves as both
 *                          input and output. This can be empty, in which case
 *                          pszAppend is just copied over.
 * @param   cbPathDst       The size of the buffer pszPath points to, terminator
 *                          included. This should NOT be strlen(pszPath).
 * @param   pszAppend       The partial path to append to pszPath. This can be
 *                          NULL, in which case nothing is done.
 *
 * @remarks See the RTPathAppendEx remarks.
 */
RTDECL(int) RTPathAppend(char *pszPath, size_t cbPathDst, const char *pszAppend);

/**
 * Appends one partial path to another.
 *
 * The main purpose of this function is to deal correctly with the slashes when
 * concatenating the two partial paths.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the result is too big to fit within
 *          cbPathDst bytes. No changes has been made.
 * @retval  VERR_INVALID_PARAMETER if the string pointed to by pszPath is longer
 *          than cbPathDst-1 bytes (failed to find terminator). Asserted.
 *
 * @param   pszPath         The path to append pszAppend to. This serves as both
 *                          input and output. This can be empty, in which case
 *                          pszAppend is just copied over.
 * @param   cbPathDst       The size of the buffer pszPath points to, terminator
 *                          included. This should NOT be strlen(pszPath).
 * @param   pszAppend       The partial path to append to pszPath. This can be
 *                          NULL, in which case nothing is done.
 * @param   cchAppendMax    The maximum number or characters to take from @a
 *                          pszAppend.  RTSTR_MAX is fine.
 *
 * @remarks On OS/2, Window and similar systems, concatenating a drive letter
 *          specifier with a slash prefixed path will result in an absolute
 *          path. Meaning, RTPathAppend(strcpy(szBuf, "C:"), sizeof(szBuf),
 *          "/bar") will result in "C:/bar". (This follows directly from the
 *          behavior when pszPath is empty.)
 *
 *          On the other hand, when joining a drive letter specifier with a
 *          partial path that does not start with a slash, the result is not an
 *          absolute path. Meaning, RTPathAppend(strcpy(szBuf, "C:"),
 *          sizeof(szBuf), "bar") will result in "C:bar".
 */
RTDECL(int) RTPathAppendEx(char *pszPath, size_t cbPathDst, const char *pszAppend, size_t cchAppendMax);

/**
 * Like RTPathAppend, but with the base path as a separate argument instead of
 * in the path buffer.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the result is too big to fit within
 *          cbPathDst bytes.
 * @retval  VERR_INVALID_PARAMETER if the string pointed to by pszPath is longer
 *          than cbPathDst-1 bytes (failed to find terminator). Asserted.
 *
 * @param   pszPathDst      Where to store the resulting path.
 * @param   cbPathDst       The size of the buffer pszPathDst points to,
 *                          terminator included.
 * @param   pszPathSrc      The base path to copy into @a pszPathDst before
 *                          appending @a pszAppend.
 * @param   pszAppend       The partial path to append to pszPathSrc. This can
 *                          be NULL, in which case nothing is done.
 *
 */
RTDECL(int) RTPathJoin(char *pszPathDst, size_t cbPathDst, const char *pszPathSrc,
                       const char *pszAppend);

/**
 * Same as RTPathJoin, except that the output buffer is allocated.
 *
 * @returns Buffer containing the joined up path, call RTStrFree to free.  NULL
 *          on allocation failure.
 * @param   pszPathSrc      The base path to copy into @a pszPathDst before
 *                          appending @a pszAppend.
 * @param   pszAppend       The partial path to append to pszPathSrc. This can
 *                          be NULL, in which case nothing is done.
 *
 */
RTDECL(char *) RTPathJoinA(const char *pszPathSrc, const char *pszAppend);

/**
 * Extended version of RTPathJoin, both inputs can be specified as substrings.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the result is too big to fit within
 *          cbPathDst bytes.
 * @retval  VERR_INVALID_PARAMETER if the string pointed to by pszPath is longer
 *          than cbPathDst-1 bytes (failed to find terminator). Asserted.
 *
 * @param   pszPathDst      Where to store the resulting path.
 * @param   cbPathDst       The size of the buffer pszPathDst points to,
 *                          terminator included.
 * @param   pszPathSrc      The base path to copy into @a pszPathDst before
 *                          appending @a pszAppend.
 * @param   cchPathSrcMax   The maximum number of bytes to copy from @a
 *                          pszPathSrc.  RTSTR_MAX is find.
 * @param   pszAppend       The partial path to append to pszPathSrc. This can
 *                          be NULL, in which case nothing is done.
 * @param   cchAppendMax    The maximum number of bytes to copy from @a
 *                          pszAppend.  RTSTR_MAX is find.
 *
 */
RTDECL(int) RTPathJoinEx(char *pszPathDst, size_t cbPathDst,
                         const char *pszPathSrc, size_t cchPathSrcMax,
                         const char *pszAppend, size_t cchAppendMax);

/**
 * Callback for RTPathTraverseList that's called for each element.
 *
 * @returns IPRT style status code. Return VERR_TRY_AGAIN to continue, any other
 *          value will abort the traversing and be returned to the caller.
 *
 * @param   pchPath         Pointer to the start of the current path. This is
 *                          not null terminated.
 * @param   cchPath         The length of the path.
 * @param   pvUser1         The first user parameter.
 * @param   pvUser2         The second user parameter.
 */
typedef DECLCALLBACK(int) FNRTPATHTRAVERSER(char const *pchPath, size_t cchPath, void *pvUser1, void *pvUser2);
/** Pointer to a FNRTPATHTRAVERSER. */
typedef FNRTPATHTRAVERSER *PFNRTPATHTRAVERSER;

/**
 * Traverses a string that can contain multiple paths separated by a special
 * character.
 *
 * @returns IPRT style status code from the callback or VERR_END_OF_STRING if
 *          the callback returned VERR_TRY_AGAIN for all paths in the string.
 *
 * @param   pszPathList     The string to traverse.
 * @param   chSep           The separator character.  Using the null terminator
 *                          is fine, but the result will simply be that there
 *                          will only be one callback for the entire string
 *                          (save any leading white space).
 * @param   pfnCallback     The callback.
 * @param   pvUser1         First user argument for the callback.
 * @param   pvUser2         Second user argument for the callback.
 */
RTDECL(int) RTPathTraverseList(const char *pszPathList, char chSep, PFNRTPATHTRAVERSER pfnCallback, void *pvUser1, void *pvUser2);


/**
 * Calculate a relative path between the two given paths.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_BUFFER_OVERFLOW if the result is too big to fit within
 *          cbPathDst bytes.
 * @retval  VERR_NOT_SUPPORTED if both paths start with different volume specifiers.
 * @param   pszPathDst      Where to store the resulting path.
 * @param   cbPathDst       The size of the buffer pszPathDst points to,
 *                          terminator included.
 * @param   pszPathFrom     The path to start from creating the relative path.
 * @param   pszPathTo       The path to reach with the created relative path.
 */
RTDECL(int) RTPathCalcRelative(char *pszPathDst, size_t cbPathDst,
                               const char *pszPathFrom,
                               const char *pszPathTo);

#ifdef IN_RING3

/**
 * Gets the path to the directory containing the executable.
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathExecDir(char *pszPath, size_t cchPath);

/**
 * Gets the user home directory.
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathUserHome(char *pszPath, size_t cchPath);

/**
 * Gets the user documents directory.
 *
 * The returned path isn't guaranteed to exist.
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathUserDocuments(char *pszPath, size_t cchPath);

/**
 * Gets the directory of shared libraries.
 *
 * This is not the same as RTPathAppPrivateArch() as Linux depends all shared
 * libraries in a common global directory where ld.so can find them.
 *
 * Linux:    /usr/lib
 * Solaris:  /opt/@<application@>/@<arch>@ or something
 * Windows:  @<program files directory@>/@<application@>
 * Old path: same as RTPathExecDir()
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathSharedLibs(char *pszPath, size_t cchPath);

/**
 * Gets the directory for architecture-independent application data, for
 * example NLS files, module sources, ...
 *
 * Linux:    /usr/shared/@<application@>
 * Solaris:  /opt/@<application@>
 * Windows:  @<program files directory@>/@<application@>
 * Old path: same as RTPathExecDir()
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathAppPrivateNoArch(char *pszPath, size_t cchPath);

/**
 * Gets the directory for architecture-dependent application data, for
 * example modules which can be loaded at runtime.
 *
 * Linux:    /usr/lib/@<application@>
 * Solaris:  /opt/@<application@>/@<arch>@ or something
 * Windows:  @<program files directory@>/@<application@>
 * Old path: same as RTPathExecDir()
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathAppPrivateArch(char *pszPath, size_t cchPath);

/**
 * Gets the toplevel directory for architecture-dependent application data.
 *
 * This differs from RTPathAppPrivateArch on Solaris only where it will work
 * around the /opt/@<application@>/amd64 and /opt/@<application@>/i386 multi
 * architecture installation style.
 *
 * Linux:    /usr/lib/@<application@>
 * Solaris:  /opt/@<application@>
 * Windows:  @<program files directory@>/@<application@>
 * Old path: same as RTPathExecDir()
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathAppPrivateArchTop(char *pszPath, size_t cchPath);

/**
 * Gets the directory for documentation.
 *
 * Linux:    /usr/share/doc/@<application@>
 * Solaris:  /opt/@<application@>
 * Windows:  @<program files directory@>/@<application@>
 * Old path: same as RTPathExecDir()
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathAppDocs(char *pszPath, size_t cchPath);

/**
 * Gets the temporary directory path.
 *
 * @returns iprt status code.
 * @param   pszPath     Buffer where to store the path.
 * @param   cchPath     Buffer size in bytes.
 */
RTDECL(int) RTPathTemp(char *pszPath, size_t cchPath);


/**
 * RTPathGlobl result entry.
 */
typedef struct RTPATHGLOBENTRY
{
    /** List entry. */
    struct RTPATHGLOBENTRY *pNext;
    /** RTDIRENTRYTYPE value. */
    uint8_t                 uType;
    /** Unused explicit padding. */
    uint8_t                 bUnused;
    /** The length of the path. */
    uint16_t                cchPath;
    /** The path to the file (variable length). */
    char                    szPath[1];
} RTPATHGLOBENTRY;
/** Pointer to a GLOB result entry. */
typedef RTPATHGLOBENTRY *PRTPATHGLOBENTRY;
/** Pointer to a const GLOB result entry. */
typedef RTPATHGLOBENTRY const *PCRTPATHGLOBENTRY;
/** Pointer to a GLOB result entry pointer. */
typedef PCRTPATHGLOBENTRY *PPCRTPATHGLOBENTRY;

/**
 * Performs wildcard expansion on a path pattern.
 *
 * @returns IPRT status code.
 *
 * @param   pszPattern      The pattern to expand.
 * @param   fFlags          RTPATHGLOB_F_XXX.
 * @param   ppHead          Where to return the head of the result list.  This
 *                          is always set to NULL on failure.
 * @param   pcResults       Where to return the number of the result. Optional.
 */
RTDECL(int) RTPathGlob(const char *pszPattern, uint32_t fFlags, PPCRTPATHGLOBENTRY ppHead, uint32_t *pcResults);

/** @name RTPATHGLOB_F_XXX - RTPathGlob flags
 *  @{ */
/** Case insensitive. */
#define RTPATHGLOB_F_IGNORE_CASE        RT_BIT_32(0)
/** Do not expand \${EnvOrSpecialVariable} in the pattern. */
#define RTPATHGLOB_F_NO_VARIABLES       RT_BIT_32(1)
/** Do not interpret a leading tilde as a home directory reference. */
#define RTPATHGLOB_F_NO_TILDE           RT_BIT_32(2)
/** Only return the first match. */
#define RTPATHGLOB_F_FIRST_ONLY         RT_BIT_32(3)
/** Only match directories (implied if pattern ends with slash). */
#define RTPATHGLOB_F_ONLY_DIRS          RT_BIT_32(4)
/** Do not match directories.  (Can't be used with RTPATHGLOB_F_ONLY_DIRS or
 * patterns containing a trailing slash.) */
#define RTPATHGLOB_F_NO_DIRS            RT_BIT_32(5)
/** Disables the '**' wildcard pattern for matching zero or more subdirs. */
#define RTPATHGLOB_F_NO_STARSTAR        RT_BIT_32(6)
/** Mask of valid flags. */
#define RTPATHGLOB_F_MASK               UINT32_C(0x0000007f)
/** @} */

/**
 * Frees the results produced by RTPathGlob.
 *
 * @param   pHead           What RTPathGlob returned.  NULL ignored.
 */
RTDECL(void) RTPathGlobFree(PCRTPATHGLOBENTRY pHead);


/**
 * Query information about a file system object.
 *
 * This API will resolve NOT symbolic links in the last component (just like
 * unix lstat()).
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if the object exists, information returned.
 * @retval  VERR_PATH_NOT_FOUND if any but the last component in the specified
 *          path was not found or was not a directory.
 * @retval  VERR_FILE_NOT_FOUND if the object does not exist (but path to the
 *          parent directory exists).
 *
 * @param   pszPath     Path to the file system object.
 * @param   pObjInfo    Object information structure to be filled on successful
 *                      return.
 * @param   enmAdditionalAttribs
 *                      Which set of additional attributes to request.
 *                      Use RTFSOBJATTRADD_NOTHING if this doesn't matter.
 */
RTR3DECL(int) RTPathQueryInfo(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs);

/**
 * Query information about a file system object.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS if the object exists, information returned.
 * @retval  VERR_PATH_NOT_FOUND if any but the last component in the specified
 *          path was not found or was not a directory.
 * @retval  VERR_FILE_NOT_FOUND if the object does not exist (but path to the
 *          parent directory exists).
 *
 * @param   pszPath     Path to the file system object.
 * @param   pObjInfo    Object information structure to be filled on successful return.
 * @param   enmAdditionalAttribs
 *                      Which set of additional attributes to request.
 *                      Use RTFSOBJATTRADD_NOTHING if this doesn't matter.
 * @param   fFlags      RTPATH_F_ON_LINK or RTPATH_F_FOLLOW_LINK.
 */
RTR3DECL(int) RTPathQueryInfoEx(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags);

/**
 * Changes the mode flags of a file system object.
 *
 * The API requires at least one of the mode flag sets (Unix/Dos) to
 * be set. The type is ignored.
 *
 * This API will resolve symbolic links in the last component since
 * mode isn't important for symbolic links.
 *
 * @returns iprt status code.
 * @param   pszPath     Path to the file system object.
 * @param   fMode       The new file mode, see @ref grp_rt_fs for details.
 */
RTR3DECL(int) RTPathSetMode(const char *pszPath, RTFMODE fMode);

/**
 * Gets the mode flags of a file system object.
 *
 * @returns iprt status code.
 * @param   pszPath     Path to the file system object.
 * @param   pfMode      Where to store the file mode, see @ref grp_rt_fs for details.
 *
 * @remark  This is wrapper around RTPathQueryInfoEx(RTPATH_F_FOLLOW_LINK) and
 *          exists to complement RTPathSetMode().
 */
RTR3DECL(int) RTPathGetMode(const char *pszPath, PRTFMODE pfMode);

/**
 * Changes one or more of the timestamps associated of file system object.
 *
 * This API will not resolve symbolic links in the last component (just
 * like unix lutimes()).
 *
 * @returns iprt status code.
 * @param   pszPath             Path to the file system object.
 * @param   pAccessTime         Pointer to the new access time.
 * @param   pModificationTime   Pointer to the new modification time.
 * @param   pChangeTime         Pointer to the new change time. NULL if not to be changed.
 * @param   pBirthTime          Pointer to the new time of birth. NULL if not to be changed.
 *
 * @remark  The file system might not implement all these time attributes,
 *          the API will ignore the ones which aren't supported.
 *
 * @remark  The file system might not implement the time resolution
 *          employed by this interface, the time will be chopped to fit.
 *
 * @remark  The file system may update the change time even if it's
 *          not specified.
 *
 * @remark  POSIX can only set Access & Modification and will always set both.
 */
RTR3DECL(int) RTPathSetTimes(const char *pszPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                             PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime);

/**
 * Changes one or more of the timestamps associated of file system object.
 *
 * @returns iprt status code.
 * @param   pszPath             Path to the file system object.
 * @param   pAccessTime         Pointer to the new access time.
 * @param   pModificationTime   Pointer to the new modification time.
 * @param   pChangeTime         Pointer to the new change time. NULL if not to be changed.
 * @param   pBirthTime          Pointer to the new time of birth. NULL if not to be changed.
 * @param   fFlags              RTPATH_F_ON_LINK or RTPATH_F_FOLLOW_LINK.
 *
 * @remark  The file system might not implement all these time attributes,
 *          the API will ignore the ones which aren't supported.
 *
 * @remark  The file system might not implement the time resolution
 *          employed by this interface, the time will be chopped to fit.
 *
 * @remark  The file system may update the change time even if it's
 *          not specified.
 *
 * @remark  POSIX can only set Access & Modification and will always set both.
 */
RTR3DECL(int) RTPathSetTimesEx(const char *pszPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                               PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime, uint32_t fFlags);

/**
 * Gets one or more of the timestamps associated of file system object.
 *
 * @returns iprt status code.
 * @param   pszPath             Path to the file system object.
 * @param   pAccessTime         Where to store the access time. NULL is ok.
 * @param   pModificationTime   Where to store the modification time. NULL is ok.
 * @param   pChangeTime         Where to store the change time. NULL is ok.
 * @param   pBirthTime          Where to store the creation time. NULL is ok.
 *
 * @remark  This is wrapper around RTPathQueryInfo() and exists to complement
 *          RTPathSetTimes().  If the last component is a symbolic link, it will
 *          not be resolved.
 */
RTR3DECL(int) RTPathGetTimes(const char *pszPath, PRTTIMESPEC pAccessTime, PRTTIMESPEC pModificationTime,
                             PRTTIMESPEC pChangeTime, PRTTIMESPEC pBirthTime);

/**
 * Changes the owner and/or group of a file system object.
 *
 * This API will not resolve symbolic links in the last component (just
 * like unix lchown()).
 *
 * @returns iprt status code.
 * @param   pszPath     Path to the file system object.
 * @param   uid         The new file owner user id.  Pass NIL_RTUID to leave
 *                      this unchanged.
 * @param   gid         The new group id.  Pass NIL_RTGUID to leave this
 *                      unchanged.
 */
RTR3DECL(int) RTPathSetOwner(const char *pszPath, uint32_t uid, uint32_t gid);

/**
 * Changes the owner and/or group of a file system object.
 *
 * @returns iprt status code.
 * @param   pszPath     Path to the file system object.
 * @param   uid         The new file owner user id.  Pass NIL_RTUID to leave
 *                      this unchanged.
 * @param   gid         The new group id.  Pass NIL_RTGID to leave this
 *                      unchanged.
 * @param   fFlags      RTPATH_F_ON_LINK or RTPATH_F_FOLLOW_LINK.
 */
RTR3DECL(int) RTPathSetOwnerEx(const char *pszPath, uint32_t uid, uint32_t gid, uint32_t fFlags);

/**
 * Gets the owner and/or group of a file system object.
 *
 * @returns iprt status code.
 * @param   pszPath     Path to the file system object.
 * @param   pUid        Where to store the owner user id. NULL is ok.
 * @param   pGid        Where to store the group id. NULL is ok.
 *
 * @remark  This is wrapper around RTPathQueryInfo() and exists to complement
 *          RTPathGetOwner().  If the last component is a symbolic link, it will
 *          not be resolved.
 */
RTR3DECL(int) RTPathGetOwner(const char *pszPath, uint32_t *pUid, uint32_t *pGid);


/** @name RTPathRename, RTDirRename & RTFileRename flags.
 * @{ */
/** Do not replace anything. */
#define RTPATHRENAME_FLAGS_NO_REPLACE   UINT32_C(0)
/** This will replace attempt any target which isn't a directory. */
#define RTPATHRENAME_FLAGS_REPLACE      RT_BIT(0)
/** Don't allow symbolic links as part of the path.
 * @remarks this flag is currently not implemented and will be ignored. */
#define RTPATHRENAME_FLAGS_NO_SYMLINKS  RT_BIT(1)
/** @} */

/**
 * Renames a path within a filesystem.
 *
 * This will rename symbolic links.  If RTPATHRENAME_FLAGS_REPLACE is used and
 * pszDst is a symbolic link, it will be replaced and not its target.
 *
 * @returns IPRT status code.
 * @param   pszSrc      The source path.
 * @param   pszDst      The destination path.
 * @param   fRename     Rename flags, RTPATHRENAME_FLAGS_*.
 */
RTR3DECL(int) RTPathRename(const char *pszSrc,  const char *pszDst, unsigned fRename);

/** @name RTPathUnlink flags.
 * @{ */
/** Don't allow symbolic links as part of the path.
 * @remarks this flag is currently not implemented and will be ignored. */
#define RTPATHUNLINK_FLAGS_NO_SYMLINKS  RT_BIT(0)
/** @} */

/**
 * Removes the last component of the path.
 *
 * @returns IPRT status code.
 * @param   pszPath     The path.
 * @param   fUnlink     Unlink flags, RTPATHUNLINK_FLAGS_*.
 */
RTR3DECL(int) RTPathUnlink(const char *pszPath, uint32_t fUnlink);

/**
 * A /bin/rm tool.
 *
 * @returns Program exit code.
 *
 * @param   cArgs               The number of arguments.
 * @param   papszArgs           The argument vector.  (Note that this may be
 *                              reordered, so the memory must be writable.)
 */
RTDECL(RTEXITCODE) RTPathRmCmd(unsigned cArgs, char **papszArgs);

#endif /* IN_RING3 */

/** @} */

RT_C_DECLS_END

#endif

