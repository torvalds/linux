/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _SYS_ICONV_H_
#define _SYS_ICONV_H_

#define	ICONV_CSNMAXLEN		31	/* maximum length of charset name */
#define	ICONV_CNVNMAXLEN	31	/* maximum length of converter name */
/* maximum size of data associated with cs pair */
#define	ICONV_CSMAXDATALEN	(sizeof(caddr_t) * 0x200 + sizeof(uint32_t) * 0x200 * 0x80)

#define	XLAT16_ACCEPT_NULL_OUT		0x01000000
#define	XLAT16_ACCEPT_NULL_IN		0x02000000
#define	XLAT16_HAS_LOWER_CASE		0x04000000
#define	XLAT16_HAS_UPPER_CASE		0x08000000
#define	XLAT16_HAS_FROM_LOWER_CASE	0x10000000
#define	XLAT16_HAS_FROM_UPPER_CASE	0x20000000
#define	XLAT16_IS_3BYTE_CHR		0x40000000

#define	KICONV_LOWER		1	/* tolower converted character */
#define	KICONV_UPPER		2	/* toupper converted character */
#define	KICONV_FROM_LOWER	4	/* tolower source character, then convert */
#define	KICONV_FROM_UPPER	8	/* toupper source character, then convert */
#define	KICONV_WCTYPE		16	/* towlower/towupper characters */

#define	ENCODING_UNICODE	"UTF-16BE"
#define	KICONV_WCTYPE_NAME	"_wctype"

/*
 * Entry for cslist sysctl
 */
#define	ICONV_CSPAIR_INFO_VER	1

struct iconv_cspair_info {
	int	cs_version;
	int	cs_id;
	int	cs_base;
	int	cs_refcount;
	char	cs_to[ICONV_CSNMAXLEN];
	char	cs_from[ICONV_CSNMAXLEN];
};

/*
 * Parameters for 'add' sysctl
 */
#define	ICONV_ADD_VER	1

struct iconv_add_in {
	int	ia_version;
	char	ia_converter[ICONV_CNVNMAXLEN];
	char	ia_to[ICONV_CSNMAXLEN];
	char	ia_from[ICONV_CSNMAXLEN];
	int	ia_datalen;
	const void *ia_data;
};

struct iconv_add_out {
	int	ia_csid;
};

#ifndef _KERNEL

__BEGIN_DECLS

#define	KICONV_VENDOR_MICSFT	1	/* Microsoft Vendor Code for quirk */

int   kiconv_add_xlat_table(const char *, const char *, const u_char *);
int   kiconv_add_xlat16_cspair(const char *, const char *, int);
int   kiconv_add_xlat16_cspairs(const char *, const char *);
int   kiconv_add_xlat16_table(const char *, const char *, const void *, int);
int   kiconv_lookupconv(const char *drvname);
int   kiconv_lookupcs(const char *tocode, const char *fromcode);
const char *kiconv_quirkcs(const char *, int);

__END_DECLS

#else /* !_KERNEL */

#include <sys/kobj.h>
#include <sys/module.h>			/* can't avoid that */
#include <sys/queue.h>			/* can't avoid that */
#include <sys/sysctl.h>			/* can't avoid that */

struct iconv_cspair;
struct iconv_cspairdata;

/*
 * iconv converter class definition
 */
struct iconv_converter_class {
	KOBJ_CLASS_FIELDS;
	TAILQ_ENTRY(iconv_converter_class)	cc_link;
};

struct iconv_cspair {
	int		cp_id;		/* unique id of charset pair */
	int		cp_refcount;	/* number of references from other pairs */
	const char *	cp_from;
	const char *	cp_to;
	void *		cp_data;
	struct iconv_converter_class * cp_dcp;
	struct iconv_cspair *cp_base;
	TAILQ_ENTRY(iconv_cspair)	cp_link;
};

#define	KICONV_CONVERTER(name,size)			\
    static struct iconv_converter_class iconv_ ## name ## _class = { \
	"iconv_"#name, iconv_ ## name ## _methods, size, NULL \
    };							\
    static moduledata_t iconv_ ## name ## _mod = {	\
	"iconv_"#name, iconv_converter_handler,		\
	(void*)&iconv_ ## name ## _class		\
    };							\
    DECLARE_MODULE(iconv_ ## name, iconv_ ## name ## _mod, SI_SUB_DRIVERS, SI_ORDER_ANY);

#define	KICONV_CES(name,size)				\
    static DEFINE_CLASS(iconv_ces_ ## name, iconv_ces_ ## name ## _methods, (size)); \
    static moduledata_t iconv_ces_ ## name ## _mod = {	\
	"iconv_ces_"#name, iconv_cesmod_handler,	\
	(void*)&iconv_ces_ ## name ## _class		\
    };							\
    DECLARE_MODULE(iconv_ces_ ## name, iconv_ces_ ## name ## _mod, SI_SUB_DRIVERS, SI_ORDER_ANY);

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_ICONV);
#endif

/*
 * Basic conversion functions
 */
int iconv_open(const char *to, const char *from, void **handle);
int iconv_close(void *handle);
int iconv_conv(void *handle, const char **inbuf,
	size_t *inbytesleft, char **outbuf, size_t *outbytesleft);
int iconv_conv_case(void *handle, const char **inbuf,
	size_t *inbytesleft, char **outbuf, size_t *outbytesleft, int casetype);
int iconv_convchr(void *handle, const char **inbuf,
	size_t *inbytesleft, char **outbuf, size_t *outbytesleft);
int iconv_convchr_case(void *handle, const char **inbuf,
	size_t *inbytesleft, char **outbuf, size_t *outbytesleft, int casetype);
int iconv_add(const char *converter, const char *to, const char *from);
char* iconv_convstr(void *handle, char *dst, const char *src);
void* iconv_convmem(void *handle, void *dst, const void *src, int size);
int iconv_vfs_refcount(const char *fsname);

int towlower(int c, void *handle);
int towupper(int c, void *handle);

/*
 * Bridge struct of iconv functions
 */
struct iconv_functions {
	int (*open)(const char *to, const char *from, void **handle);
	int (*close)(void *handle);
	int (*conv)(void *handle, const char **inbuf, size_t *inbytesleft,
		char **outbuf, size_t *outbytesleft);
	int (*conv_case)(void *handle, const char **inbuf, size_t *inbytesleft,
		char **outbuf, size_t *outbytesleft, int casetype);
	int (*convchr)(void *handle, const char **inbuf, size_t *inbytesleft,
		char **outbuf, size_t *outbytesleft);
	int (*convchr_case)(void *handle, const char **inbuf, size_t *inbytesleft,
		char **outbuf, size_t *outbytesleft, int casetype);
};

#define VFS_DECLARE_ICONV(fsname)					\
	static struct iconv_functions fsname ## _iconv_core = {		\
		iconv_open,						\
		iconv_close,						\
		iconv_conv,						\
		iconv_conv_case,					\
		iconv_convchr,						\
		iconv_convchr_case					\
	};								\
	extern struct iconv_functions *fsname ## _iconv;		\
	static int fsname ## _iconv_mod_handler(module_t mod,		\
		int type, void *d);					\
	static int							\
	fsname ## _iconv_mod_handler(module_t mod, int type, void *d)	\
	{								\
		int error = 0;						\
		switch(type) {						\
		case MOD_LOAD:						\
			fsname ## _iconv = & fsname ## _iconv_core;	\
			break;						\
		case MOD_UNLOAD:					\
			error = iconv_vfs_refcount(#fsname);		\
			if (error)					\
				return (EBUSY);				\
			fsname ## _iconv = NULL;			\
			break;						\
		default:						\
			error = EINVAL;					\
			break;						\
		}							\
		return (error);						\
	}								\
	static moduledata_t fsname ## _iconv_mod = {			\
		#fsname"_iconv",					\
		fsname ## _iconv_mod_handler,				\
		NULL							\
	};								\
	DECLARE_MODULE(fsname ## _iconv, fsname ## _iconv_mod,		\
		       SI_SUB_DRIVERS, SI_ORDER_ANY);			\
	MODULE_DEPEND(fsname ## _iconv, fsname, 1, 1, 1);		\
	MODULE_DEPEND(fsname ## _iconv, libiconv, 2, 2, 2);		\
	MODULE_VERSION(fsname ## _iconv, 1)

/*
 * Internal functions
 */
int iconv_lookupcp(char **cpp, const char *s);

int iconv_converter_initstub(struct iconv_converter_class *dp);
int iconv_converter_donestub(struct iconv_converter_class *dp);
int iconv_converter_tolowerstub(int c, void *handle);
int iconv_converter_handler(module_t mod, int type, void *data);

#ifdef ICONV_DEBUG
#define ICDEBUG(format, ...) printf("%s: "format, __func__ , ## __VA_ARGS__)
#else
#define ICDEBUG(format, ...)
#endif

#endif /* !_KERNEL */

#endif /* !_SYS_ICONV_H_ */
