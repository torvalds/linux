/* $FreeBSD$ */

#ifndef _MD5_H_
#define _MD5_H_

#ifndef _KERNEL

/* Ensure libmd symbols do not clash with libcrypto */

#ifndef MD5Init
#define MD5Init		_libmd_MD5Init
#endif
#ifndef MD5Update
#define MD5Update	_libmd_MD5Update
#endif
#ifndef MD5Pad
#define MD5Pad		_libmd_MD5Pad
#endif
#ifndef MD5Final
#define MD5Final	_libmd_MD5Final
#endif
#ifndef MD5Transform
#define MD5Transform	_libmd_MD5Transform
#endif
#ifndef MD5End
#define MD5End		_libmd_MD5End
#endif
#ifndef MD5Fd
#define MD5Fd		_libmd_MD5Fd
#endif
#ifndef MD5FdChunk
#define MD5FdChunk	_libmd_MD5FdChunk
#endif
#ifndef MD5File
#define MD5File		_libmd_MD5File
#endif
#ifndef MD5FileChunk
#define MD5FileChunk	_libmd_MD5FileChunk
#endif
#ifndef MD5Data
#define MD5Data		_libmd_MD5Data
#endif

#endif

#include <sys/md5.h>
#endif /* _MD5_H_ */
