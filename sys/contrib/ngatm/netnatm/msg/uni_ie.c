/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/netnatm/msg/uni_ie.c,v 1.16 2005/05/23 12:06:30 brandt_h Exp $
 *
 * Private definitions for the IE code file.
 *
 * This file includes the table generated automatically.
 */

#include <sys/types.h>
#include <sys/param.h>

#ifdef _KERNEL
#include <sys/libkern.h>
#else
#include <string.h>
#endif
#include <netnatm/unimsg.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/msg/uniprint.h>
#include <netnatm/msg/priv.h>

#define UNUSED(_p) do { (void)(_p); } while (0)

/*
 * Define internal functions.
 */
#define DEF_IE_PRINT(Coding, IE) \
	void uni_ie_print_##Coding##_##IE(struct uni_ie_##IE *ie, struct unicx *cx)

#define DEF_IE_CHECK(Coding, IE) \
	int uni_ie_check_##Coding##_##IE(struct uni_ie_##IE *ie, struct unicx *cx)

#define DEF_IE_ENCODE(Coding, IE) \
	int uni_ie_encode_##Coding##_##IE(struct uni_msg *msg, struct uni_ie_##IE *ie, struct unicx *cx)

#define DEF_IE_DECODE(Coding, IE) \
	int uni_ie_decode_##Coding##_##IE(struct uni_ie_##IE *ie, struct uni_msg *msg, u_int ielen, struct unicx *cx)

/*
 * This structure is used to define value->string mappings. MKT() is used
 * to generate a table entry. EOT() to end the table.
 */
#define MKT(V,N)	{ #N, V }
#define EOT()		{ NULL, 0 }

/* library internal functions */
static void uni_entry(const char *, struct unicx *);
static int  uni_print_iehdr(const char *, struct uni_iehdr *h, struct unicx *);
static void uni_print_ieend(struct unicx *);
static void uni_putc(int, struct unicx *);


/*
 * Encoding
 */
#define APP_BYTE(M, B) do {					\
	*(M)->b_wptr++ = (B);					\
    } while (0)
#define APP_16BIT(M, B) do {					\
	u_int _v = (B);						\
	*(M)->b_wptr++ = _v >> 8; 				\
	*(M)->b_wptr++ = _v;					\
    } while (0)
#define APP_24BIT(M, B) do {					\
	u_int _v = (B);						\
	*(M)->b_wptr++ = _v >> 16;				\
	*(M)->b_wptr++ = _v >> 8;				\
	*(M)->b_wptr++ = _v;					\
    } while (0)
#define APP_32BIT(M, B) do {					\
	u_int _v = (B);						\
	*(M)->b_wptr++ = _v >> 24;				\
	*(M)->b_wptr++ = _v >> 16;				\
	*(M)->b_wptr++ = _v >> 8;				\
	*(M)->b_wptr++ = _v;					\
    } while (0)
#define APP_BUF(M, B, L) do {					\
	(void)memcpy((M)->b_wptr, (B), (L));			\
	(M)->b_wptr += (L);					\
    } while (0)

#define APP_SUB_BYTE(M, T, B)  do { APP_BYTE(M, T); APP_BYTE(M, B); } while (0)
#define APP_SUB_16BIT(M, T, B) do { APP_BYTE(M, T); APP_16BIT(M, B); } while (0)
#define APP_SUB_24BIT(M, T, B) do { APP_BYTE(M, T); APP_24BIT(M, B); } while (0)
#define APP_SUB_32BIT(M, T, B) do { APP_BYTE(M, T); APP_32BIT(M, B); } while (0)

#define APP_OPT(M, F, P, T) do {				\
	if ((F) & (P))						\
		APP_BYTE((M), (T));				\
    } while (0)
#define APP_OPT_BYTE(M, F, P, T, B) do {			\
	if ((F) & (P))						\
		APP_SUB_BYTE((M), (T), (B));			\
    } while (0)
#define APP_OPT_16BIT(M, F, P, T, B) do {			\
	if ((F) & (P))						\
		APP_SUB_16BIT((M), (T), (B));			\
    } while (0)
#define APP_OPT_24BIT(M, F, P, T, B) do {			\
	if ((F) & (P))						\
		APP_SUB_24BIT((M), (T), (B));			\
    } while (0)

#define START_IE(TYPE,CODE,LEN) 				\
	u_int ielen; 						\
								\
	if (uni_check_ie(CODE, (union uni_ieall *)ie, cx)) 	\
		return (-1); 					\
	if (uni_encode_ie_hdr(msg, CODE, &ie->h, (LEN), cx))	\
		return (0);					\
								\
	ielen = msg->b_wptr - msg->b_rptr - 2;

#define START_IE2(TYPE,CODE,LEN,REALCODE) 			\
	u_int ielen; 						\
								\
	if (uni_check_ie(CODE, (union uni_ieall *)ie, cx)) 	\
		return (-1); 					\
	if (uni_encode_ie_hdr(msg, REALCODE, &ie->h, (LEN), cx)) \
		return (0);					\
								\
	ielen = msg->b_wptr - msg->b_rptr - 2;

#define SET_IE_LEN(M) do {					\
	(M)->b_buf[ielen + 0] =					\
	    (((M)->b_wptr - (M)->b_rptr) - ielen - 2) >> 8;	\
	(M)->b_buf[ielen + 1] =					\
	    (((M)->b_wptr - (M)->b_rptr) - ielen - 2) >> 0;	\
    } while (0)


/***********************************************************************/
/*
 * Decoding
 */
#define IE_START(ERR) 							\
	if (IE_ISPRESENT(*ie))						\
		return (0);						\
	if (ielen == 0) {						\
		IE_SETEMPTY(*ie);					\
		return (0);						\
	}

#define IE_END(IE)							\
	IE_SETPRESENT(*ie);						\
	if (uni_check_ie(UNI_IE_##IE, (union uni_ieall *)ie, cx) == 0) 	\
		return (0);						\
  rej:									\
	ie->h.present = UNI_IE_ERROR | UNI_IE_PRESENT;			\
	return (1);

#define DEC_GETF3(ID, F, P) 						\
		  case UNI_##ID##_ID:					\
			if (ielen < 3)					\
				goto rej;				\
			ielen -= 3;					\
		  	if (!(P & UNI_##ID##_P)) {			\
		  		P |= UNI_##ID##_P;			\
				ie->F  = *msg->b_rptr++ << 16;		\
				ie->F |= *msg->b_rptr++ << 8;		\
				ie->F |= *msg->b_rptr++;		\
			} else						\
				msg->b_rptr += 3;			\
			break;

#define DEC_GETF1(ID, F, P) 						\
		  case UNI_##ID##_ID:					\
			if (ielen < 1)					\
				goto rej;				\
			ielen--;					\
		  	if (!(P & UNI_##ID##_P)) {			\
		  		P |= UNI_##ID##_P;			\
				ie->F = *msg->b_rptr++;			\
			} else						\
				msg->b_rptr++;				\
			break;


#define PRINT_NPREFIX (sizeof(((struct unicx *)0)->prefix) /		\
	    sizeof(((struct unicx *)0)->prefix[0]))

/*
 * This is rather here than in privmsg.c because we need the APP macros.
 */
int
uni_encode_msg_hdr(struct uni_msg *msg, struct uni_msghdr *h,
    enum uni_msgtype type, struct unicx *cx, int *mlen)
{
	u_char byte;

	(void)uni_msg_ensure(msg, 9);

	APP_BYTE(msg, cx->pnni ? PNNI_PROTO : UNI_PROTO); 
	APP_BYTE(msg, 3); 
	if(h->cref.cref >= 1<<23) 
		return -1; 
	APP_24BIT(msg, h->cref.cref | (h->cref.flag ? 0x800000 : 0));
	APP_BYTE(msg, type); 

	byte = 0x80;
	if(h->act != UNI_MSGACT_DEFAULT)
		byte |= 0x10 | (h->act & 3);
	if(cx->pnni && h->pass)
		byte |= 0x08;
	APP_BYTE(msg, byte); 

	*mlen = msg->b_wptr - msg->b_rptr; 
	APP_16BIT(msg, 0);

	return 0;
}

/*
 * Initialize printing. This must be called by all printing routines
 * that are exported to the user.
 */
void
uni_print_init(char *buf, size_t bufsiz, struct unicx *cx)
{
	if (cx->dont_init)
		return;

	cx->indent = 0;
	cx->nprefix = 0;
	cx->doindent = 0;
	if (cx->tabsiz == 0)
		cx->tabsiz = 4;
	cx->buf = buf;
	cx->bufsiz = bufsiz;
}

/*
 * Append a character to the buffer if there is still space
 */
static void
uni_putc(int c, struct unicx *cx)
{
	if(cx->bufsiz > 1) {
		*cx->buf++ = c;
		cx->bufsiz--;
		*cx->buf = '\0';
	}
}

void
uni_printf(struct unicx *cx, const char *fmt, ...)
{
	u_int n;
	va_list ap;

	if(cx->bufsiz > 1) {
		va_start(ap, fmt);
		n = vsnprintf(cx->buf, cx->bufsiz, fmt, ap);
		va_end(ap);
		if(n > 0) {
			if(n < cx->bufsiz) {
				cx->bufsiz -= n;
				cx->buf += n;
			} else {
				cx->buf += cx->bufsiz - 1;
				cx->bufsiz = 1;
			}
		}
		*cx->buf = '\0';
	}
}

/*
 * Print mode:
 *	0 - print all into one line, fully prefixed
 *	1 - print on multiple lines, full prefixed, but equal level
 *	    entries on one line
 *	2 - like 2, but only partial prefixed
 *	3 - like 1, but each entry onto a new line
 *	4 - like 2 + 3
 */

/*
 * If we are in multiline mode, end the current line and set the
 * flag, that we need indentation. But prevent double new lines.
 */
void
uni_print_eol(struct unicx *cx)
{
	if (cx->multiline) {
		if (!cx->doindent) {
			uni_putc('\n', cx);
			cx->doindent = 1;
		}
	} 
}

/*
 * New entry. Do the prefixing, indentation and spacing.
 */
static void
doprefix(struct unicx *cx, const char *s)
{
	u_int i;

	if(cx->multiline == 0) {
		uni_putc(' ', cx);
		for(i = 0; i < cx->nprefix; i++)
			if(cx->prefix[i])
				uni_printf(cx, "%s.", cx->prefix[i]);
	} else if(cx->multiline == 1) {
		if(cx->doindent) {
			uni_printf(cx, "%*s", cx->indent * cx->tabsiz, "");
			cx->doindent = 0;
		} else
			uni_putc(' ', cx);
		for(i = 0; i < cx->nprefix; i++)
			if(cx->prefix[i])
				uni_printf(cx, "%s.", cx->prefix[i]);
	} else if(cx->multiline == 2) {
		if(cx->doindent) {
			uni_printf(cx, "%*s", cx->indent * cx->tabsiz, "");
			cx->doindent = 0;
		} else
			uni_putc(' ', cx);
	} else if(cx->multiline == 3) {
		if(cx->doindent)
			cx->doindent = 0;
		else
			uni_putc('\n', cx);
		uni_printf(cx, "%*s", cx->indent * cx->tabsiz, "");
		for(i = 0; i < cx->nprefix; i++)
			if(cx->prefix[i])
				uni_printf(cx, "%s.", cx->prefix[i]);
	} else if(cx->multiline == 4) {
		if(cx->doindent)
			cx->doindent = 0;
		else
			uni_putc('\n', cx);
		uni_printf(cx, "%*s", cx->indent * cx->tabsiz, "");
	}
	uni_printf(cx, "%s", s);
}
static void
uni_entry(const char *s, struct unicx *cx)
{
	doprefix(cx, s);
	uni_putc('=', cx);
}
void
uni_print_flag(const char *s, struct unicx *cx)
{
	doprefix(cx, s);
}


/*
 * Start a deeper level of indendation. If multiline is in effect,
 * we end the current line.
 */
void
uni_print_push_prefix(const char *prefix, struct unicx *cx)
{
	if (cx->nprefix < PRINT_NPREFIX)
		cx->prefix[cx->nprefix++] = prefix;
}
void
uni_print_pop_prefix(struct unicx *cx)
{
	if (cx->nprefix > 0)
		cx->nprefix--;
}

void
uni_print_tbl(const char *entry, u_int val, const struct uni_print_tbl *tbl,
    struct unicx *cx)
{
	if (entry)
		uni_entry(entry, cx);
	while (tbl->name) {
		if (tbl->val == val) {
			uni_printf(cx, "%s", tbl->name);
			return;
		}
		tbl++;
	}
	uni_printf(cx, "ERROR(0x%x)", val);
}

void
uni_print_entry(struct unicx *cx, const char *e, const char *fmt, ...)
{
	u_int n;
	va_list ap;

	uni_entry(e, cx);

	if (cx->bufsiz > 1) {
		va_start(ap, fmt);
		n = vsnprintf(cx->buf, cx->bufsiz, fmt, ap);
		va_end(ap);
		if (n > 0) {
			if (n < cx->bufsiz) {
				cx->bufsiz -= n;
				cx->buf += n;
			} else {
				cx->buf += cx->bufsiz - 1;
				cx->bufsiz = 1;
			}
		}
		*cx->buf = '\0';
	}
}

/**********************************************************************/
/*
 * Printing information elements.
 */
static int
uni_print_iehdr(const char *name, struct uni_iehdr *h, struct unicx *cx)
{
	static const struct uni_print_tbl act_tab[] = {
		MKT(UNI_IEACT_CLEAR,	clear),
		MKT(UNI_IEACT_IGNORE,	ignore),
		MKT(UNI_IEACT_REPORT,	report),
		MKT(UNI_IEACT_MSG_IGNORE, ignore-msg),
		MKT(UNI_IEACT_MSG_REPORT, report-msg),
		MKT(UNI_IEACT_DEFAULT,	default),
		EOT()
	};
	static const struct uni_print_tbl cod_tab[] = {
		MKT(UNI_CODING_ITU, itut),
		MKT(UNI_CODING_NET, atmf),
		EOT()
	};

	uni_print_entry(cx, name, "(");
	uni_print_tbl(NULL, h->act, act_tab, cx);
	uni_putc(',', cx);
	uni_print_tbl(NULL, h->coding, cod_tab, cx);
	if(cx->pnni && h->pass)
		uni_printf(cx, ",pass");
	if(IE_ISEMPTY(*(struct uni_ie_aal *)h)) {
		uni_printf(cx, ",empty)");
		uni_print_eol(cx);
		return 1;
	}
	if(IE_ISERROR(*(struct uni_ie_aal *)h)) {
		uni_printf(cx, ",error)");
		uni_print_eol(cx);
		return 1;
	}

	uni_putc(')', cx);

	uni_print_push_prefix(name, cx);
	uni_print_eol(cx);
	cx->indent++;

	return 0;
}

static void
uni_print_ieend(struct unicx *cx)
{
	uni_print_pop_prefix(cx);
	uni_print_eol(cx);
	cx->indent--;
}

void
uni_print_ie_internal(enum uni_ietype code, const union uni_ieall *ie,
    struct unicx *cx)
{
	const struct iedecl *iedecl;

	if((iedecl = GET_IEDECL(code, ie->h.coding)) != NULL)
		(*iedecl->print)(ie, cx);
}

void
uni_print_ie(char *buf, size_t size, enum uni_ietype code,
    const union uni_ieall *ie, struct unicx *cx)
{
	uni_print_init(buf, size, cx);
	uni_print_ie_internal(code, ie, cx);
}

int
uni_check_ie(enum uni_ietype code, union uni_ieall *ie, struct unicx *cx)
{
	const struct iedecl *iedecl = GET_IEDECL(code, ie->h.coding);

	if (iedecl != NULL)
		return (iedecl->check(ie, cx));
	else
		return (-1);
}

/*
 * Decode a information element header.
 * Returns -1 if the message is too short.
 * Strip the header from the message.
 * The header is stripped, even if it is too short.
 */
int
uni_decode_ie_hdr(enum uni_ietype *ietype, struct uni_iehdr *hdr,
    struct uni_msg *msg, struct unicx *cx, u_int *ielen)
{
	u_int len;

	*ietype = (enum uni_ietype)0;
	*ielen = 0;
	hdr->present = 0;
	hdr->coding = UNI_CODING_ITU;
	hdr->act = UNI_IEACT_DEFAULT;

	if ((len = uni_msg_len(msg)) == 0)
		return (-1);

	*ietype = *msg->b_rptr++;

	if (--len == 0)
		return (-1);

	hdr->coding = (*msg->b_rptr >> 5) & 3;
	hdr->present = 0;

	switch (*msg->b_rptr & 0x17) {

	  case 0x10: case 0x11: case 0x12:
	  case 0x15: case 0x16:
		hdr->act = *msg->b_rptr & 0x7;
		break;

	  case 0x00: case 0x01: case 0x02: case 0x03:
	  case 0x04: case 0x05: case 0x06: case 0x07:
		hdr->act = UNI_IEACT_DEFAULT;
		break;

	  default:
		/* Q.2931 5.7.2 last sentence */
		hdr->act = UNI_IEACT_REPORT;
		break;
	}
	if (cx->pnni && (*msg->b_rptr & 0x08))
		hdr->pass = 1;
	else
		hdr->pass = 0;
	msg->b_rptr++;

	if (--len == 0) {
		hdr->present = UNI_IE_ERROR | UNI_IE_PRESENT;
		return (-1);
	}

	if (len < 2) {
		msg->b_rptr += len;
		hdr->present = UNI_IE_ERROR | UNI_IE_PRESENT;
		return (-1);
	}

	*ielen = *msg->b_rptr++ << 8;
	*ielen |= *msg->b_rptr++;

	return (0);
}

/*
 * Decode the body of an information element.
 */
int
uni_decode_ie_body(enum uni_ietype ietype, union uni_ieall *ie,
    struct uni_msg *msg, u_int ielen, struct unicx *cx)
{
	const struct iedecl *iedecl;
	u_char *end;
	int ret;

	if (ielen > uni_msg_len(msg)) {
		/*
		 * Information element too long -> content error.
		 * Q.2931 5.6.8.2
		 */
		msg->b_rptr = msg->b_wptr;
		ie->h.present = UNI_IE_ERROR | UNI_IE_PRESENT;
		return (-1);
	}

	if ((iedecl = GET_IEDECL(ietype, ie->h.coding)) == NULL) {
		/*
		 * entirly unknown IE.
		 * Q.2931 5.6.8.1
		 */
		msg->b_rptr += ielen;
		ie->h.present = UNI_IE_ERROR | UNI_IE_PRESENT;
		return (-1);
	}

	if (ielen > iedecl->maxlen) {
		/*
		 * Information element too long -> content error.
		 * Q.2931 5.6.8.2
		 */
		msg->b_rptr += iedecl->maxlen;
		ie->h.present = UNI_IE_ERROR | UNI_IE_PRESENT;
		return (-1);
	}

	end = msg->b_rptr + ielen;
	ret = (*iedecl->decode)(ie, msg, ielen, cx);
	msg->b_rptr = end;

	return (ret);
}

int
uni_encode_ie(enum uni_ietype code, struct uni_msg *msg, union uni_ieall *ie,
    struct unicx *cx)
{
	const struct iedecl *iedecl = GET_IEDECL(code, ie->h.coding);

	if (iedecl == NULL)
		return (-1);
	return (iedecl->encode(msg, ie, cx));
}

int
uni_encode_ie_hdr(struct uni_msg *msg, enum uni_ietype type,
    struct uni_iehdr *h, u_int len, struct unicx *cx)
{
	u_char byte;

	(void)uni_msg_ensure(msg, 4 + len);
	*msg->b_wptr++ = type;

	byte = 0x80 | (h->coding << 5);
	if(h->act != UNI_IEACT_DEFAULT)
		byte |= 0x10 | (h->act & 7);
	if(cx->pnni)
		byte |= h->pass << 3;
	*msg->b_wptr++ = byte;

	if(h->present & UNI_IE_EMPTY) {
		*msg->b_wptr++ = 0;
		*msg->b_wptr++ = 4;
		return -1;
	}
	*msg->b_wptr++ = 0;
	*msg->b_wptr++ = 0;

	return 0;
}

/*
 * Printing messages.
 */
static void
uni_print_cref_internal(const struct uni_cref *cref, struct unicx *cx)
{
	uni_print_entry(cx, "cref", "%d.", cref->flag);
	if (cref->cref == CREF_GLOBAL)
		uni_printf(cx, "GLOBAL");
	else if (cref->cref == CREF_DUMMY)
		uni_printf(cx, "DUMMY");
	else
		uni_printf(cx, "%d", cref->cref);
}
void
uni_print_cref(char *str, size_t len, const struct uni_cref *cref,
    struct unicx *cx)
{
	uni_print_init(str, len, cx);
	uni_print_cref_internal(cref, cx);
}

static void
uni_print_msghdr_internal(const struct uni_msghdr *hdr, struct unicx *cx)
{
	static const struct uni_print_tbl tab[] = {
		MKT(UNI_MSGACT_CLEAR,	clear),
		MKT(UNI_MSGACT_IGNORE,	ignore),
		MKT(UNI_MSGACT_REPORT,	report),
		MKT(UNI_MSGACT_DEFAULT,	default),
		EOT()
	};

	uni_print_cref_internal(&hdr->cref, cx);
	uni_print_tbl("act", hdr->act, tab, cx);
	if (cx->pnni)
		uni_print_entry(cx, "pass", "%s", hdr->pass ? "yes" : "no");
}

void
uni_print_msghdr(char *str, size_t len, const struct uni_msghdr *hdr,
    struct unicx *cx)
{
	uni_print_init(str, len, cx);
	uni_print_msghdr_internal(hdr, cx);
}


static void
uni_print_internal(const struct uni_all *msg, struct unicx *cx)
{
	uni_entry("mtype", cx);
	if(msg->mtype >= 256 || uni_msgtable[msg->mtype] == NULL) {
		uni_printf(cx, "0x%02x(ERROR)", msg->mtype);
	} else {
		uni_printf(cx, "%s", uni_msgtable[msg->mtype]->name);
		uni_print_msghdr_internal(&msg->u.hdr, cx);
		cx->indent++;
		uni_print_eol(cx);
		(*uni_msgtable[msg->mtype]->print)(&msg->u, cx);
		cx->indent--;
	}

	if(cx->multiline == 0)
		uni_printf(cx, "\n");
}

void
uni_print(char *buf, size_t size, const struct uni_all *all, struct unicx *cx)
{
	uni_print_init(buf, size, cx);
	uni_print_internal(all, cx);
}

static void
uni_print_msg_internal(u_int mtype, const union uni_msgall *msg,
    struct unicx *cx)
{

	uni_entry("mtype", cx);
	if (mtype >= 256 || uni_msgtable[mtype] == NULL) {
		uni_printf(cx, "0x%02x(ERROR)", mtype);
	} else {
		uni_printf(cx, "%s", uni_msgtable[mtype]->name);
		uni_print_msghdr_internal(&msg->hdr, cx);
		cx->indent++;
		uni_print_eol(cx);
		(*uni_msgtable[mtype]->print)(msg, cx);
		cx->indent--;
	}

	if(cx->multiline == 0)
		uni_printf(cx, "\n");
}

void
uni_print_msg(char *buf, size_t size, u_int mtype, const union uni_msgall *all,
    struct unicx *cx)
{
	uni_print_init(buf, size, cx);
	uni_print_msg_internal(mtype, all, cx);
}

void
uni_print_cx(char *buf, size_t size, struct unicx *cx)
{
	static const char *acttab[] = {
		"clr",	/* 0x00 */
		"ign",	/* 0x01 */
		"rep",	/* 0x02 */
		"x03",	/* 0x03 */
		"x04",	/* 0x04 */
		"mig",	/* 0x05 */
		"mrp",	/* 0x06 */
		"x07",	/* 0x07 */
		"def",	/* 0x08 */
	};

	static const char *errtab[] = {
		[UNI_IERR_UNK] = "unk",	/* unknown IE */
		[UNI_IERR_LEN] = "len",	/* length error */
		[UNI_IERR_BAD] = "bad",	/* content error */
		[UNI_IERR_ACC] = "acc",	/* access element discarded */
		[UNI_IERR_MIS] = "mis",	/* missing IE */
	};

	u_int i;

	uni_print_init(buf, size, cx);

	uni_printf(cx, "q2932		%d\n", cx->q2932);
	uni_printf(cx, "pnni		%d\n", cx->pnni);
	uni_printf(cx, "git_hard	%d\n", cx->git_hard);
	uni_printf(cx, "bearer_hard	%d\n", cx->bearer_hard);
	uni_printf(cx, "cause_hard	%d\n", cx->cause_hard);

	uni_printf(cx, "multiline	%d\n", cx->multiline);
	uni_printf(cx, "tabsiz		%d\n", cx->tabsiz);

	uni_printf(cx, "errcnt		%d (", cx->errcnt);
	for(i = 0; i < cx->errcnt; i++) {
		uni_printf(cx, "%02x[%s,%s%s]", cx->err[i].ie,
		    errtab[cx->err[i].err], acttab[cx->err[i].act],
		    cx->err[i].man ? ",M" : "");
		if(i != cx->errcnt - 1)
			uni_putc(' ', cx);
	}
	uni_printf(cx, ")\n");
}

#include <netnatm/msg/uni_ietab.h>

/*********************************************************************
 *
 * Cause
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 69 (just a pointer to Q.2610)
 *  Q.2610        (this is a small diff to Q.850)
 *  Q.850         !!
 *  UNI4.0 pp. 15
 *  PNNI1.0 p. 198
 *
 * ITU-T and NET coding for different values.
 */
static const struct causetab {
	const char	*str;
	enum uni_diag	diag;
} itu_causes[128] = {

#define D(NAME,VAL,DIAG,STD,STR) [UNI_CAUSE_##NAME] = { STR, UNI_DIAG_##DIAG },
#define N(NAME,VAL,DIAG,STD,STR)

UNI_DECLARE_CAUSE_VALUES

#undef D
#undef N

}, net_causes[128] = {

#define D(NAME,VAL,DIAG,STD,STR)
#define N(NAME,VAL,DIAG,STD,STR) [UNI_CAUSE_##NAME] = { STR, UNI_DIAG_##DIAG },

UNI_DECLARE_CAUSE_VALUES

#undef D
#undef N

};

enum uni_diag
uni_diag(enum uni_cause cause, enum uni_coding code)
{
	if ((int)cause >= 128)
		return (UNI_DIAG_NONE);

	if (code == UNI_CODING_NET)
		if (net_causes[cause].str != NULL)
			return (net_causes[cause].diag);
	if (itu_causes[cause].str != NULL)
		return (itu_causes[cause].diag);
	return (UNI_DIAG_NONE);
}

/**********************************************************************/

static void
print_cause(struct unicx *cx, struct uni_ie_cause *ie,
    const struct causetab *tab1, const struct causetab *tab2)
{
	static const struct uni_print_tbl loc_tbl[] = {
		MKT(UNI_CAUSE_LOC_USER,		user),
		MKT(UNI_CAUSE_LOC_PRIVLOC,	priv-net:loc-user),
		MKT(UNI_CAUSE_LOC_PUBLOC,	pub-net:loc-user),
		MKT(UNI_CAUSE_LOC_TRANSIT,	transit-net),
		MKT(UNI_CAUSE_LOC_PUBREM,	pub-net:rem-user),
		MKT(UNI_CAUSE_LOC_PRIVREM,	priv-net:rem-user),
		MKT(UNI_CAUSE_LOC_INTERNAT,	int-net),
		MKT(UNI_CAUSE_LOC_BEYOND,	beyond),
		EOT()
	};
	static const struct uni_print_tbl pu_tbl[] = {
		MKT(UNI_CAUSE_PU_PROVIDER,	provider),
		MKT(UNI_CAUSE_PU_USER,		user),
		EOT()
	};
	static const struct uni_print_tbl na_tbl[] = {
		MKT(UNI_CAUSE_NA_NORMAL,	normal),
		MKT(UNI_CAUSE_NA_ABNORMAL,	abnormal),
		EOT()
	};
	static const struct uni_print_tbl cond_tbl[] = {
		MKT(UNI_CAUSE_COND_UNKNOWN,	unknown),
		MKT(UNI_CAUSE_COND_PERM,	permanent),
		MKT(UNI_CAUSE_COND_TRANS,	transient),
		EOT()
	};
	static const struct uni_print_tbl rej_tbl[] = {
		MKT(UNI_CAUSE_REASON_USER,	user),
		MKT(UNI_CAUSE_REASON_IEMISS,	ie-missing),
		MKT(UNI_CAUSE_REASON_IESUFF,	ie-not-suff),
		EOT()
	};
	char buf[100], *s;
	u_int i;

	if (uni_print_iehdr("cause", &ie->h, cx))
		return;

	if ((int)ie->cause < 128 && tab1[ie->cause].str)
		strcpy(buf, tab1[ie->cause].str);
	else if ((int)ie->cause < 128 && tab2 != NULL && tab2[ie->cause].str != NULL)
		strcpy(buf, tab2[ie->cause].str);
	else {
		sprintf(buf, "UNKNOWN-%u", ie->cause);
	}

	for (s = buf; *s != '\0'; s++)
		if (*s == ' ')
			*s = '_';
	uni_print_entry(cx, "cause", "%s", buf);

	uni_print_tbl("loc", ie->loc, loc_tbl, cx);

	if (ie->h.present & UNI_CAUSE_COND_P) {
		uni_print_tbl("pu", ie->u.cond.pu, pu_tbl, cx);
		uni_print_tbl("na", ie->u.cond.na, na_tbl, cx);
		uni_print_tbl("condition", ie->u.cond.cond, cond_tbl, cx);
	}
	if (ie->h.present & UNI_CAUSE_REJ_P) {
		uni_print_tbl("reject", ie->u.rej.reason, rej_tbl, cx);
	}
	if (ie->h.present & UNI_CAUSE_REJ_USER_P) {
		uni_print_entry(cx, "user", "%u", ie->u.rej.user);
	}
	if (ie->h.present & UNI_CAUSE_REJ_IE_P) {
		uni_print_entry(cx, "ie", "%u", ie->u.rej.ie);
	}
	if (ie->h.present & UNI_CAUSE_IE_P) {
		uni_print_entry(cx, "ie", "(");
		for (i = 0; i < ie->u.ie.len; i++) {
			if (i)
				uni_putc(',', cx);
			uni_printf(cx, "0x%02x", ie->u.ie.ie[i]);
		}
		uni_putc(')', cx);
	}
	if (ie->h.present & UNI_CAUSE_TRAFFIC_P) {
		uni_print_entry(cx, "traffic", "(");
		for (i = 0; i < ie->u.traffic.len; i++) {
			if (i)
				uni_putc(',', cx);
			uni_printf(cx, "0x%02x", ie->u.traffic.traffic[i]);
		}
		uni_putc(')', cx);
	}
	if (ie->h.present & UNI_CAUSE_VPCI_P) {
		uni_print_entry(cx, "vpci", "(%u,%u)", ie->u.vpci.vpci, ie->u.vpci.vci);
	}
	if (ie->h.present & UNI_CAUSE_MTYPE_P) {
		uni_print_entry(cx, "mtype", "%u", ie->u.mtype);
	}
	if (ie->h.present & UNI_CAUSE_TIMER_P) {
		for (i = 0, s = buf; i < 3; i++) {
			if (ie->u.timer[i] < ' ') {
				*s++ = '^';
				*s++ = ie->u.timer[i] + '@';
			} else if (ie->u.timer[i] <= '~')
				*s++ = ie->u.timer[i];
			else {
				*s++ = '\\';
				*s++ = ie->u.timer[i] / 0100 + '0';
				*s++ = (ie->u.timer[i] % 0100) / 010 + '0';
				*s++ = ie->u.timer[i] % 010 + '0';
			}
		}
		*s++ = '\0';
		uni_print_entry(cx, "timer", "\"%s\"", buf);
	}
	if (ie->h.present & UNI_CAUSE_TNS_P) {
		uni_print_eol(cx);
		uni_print_ie_internal(UNI_IE_TNS, (union uni_ieall *)&ie->u.tns, cx);
	}
	if (ie->h.present & UNI_CAUSE_NUMBER_P) {
		uni_print_eol(cx);
		uni_print_ie_internal(UNI_IE_CALLED, (union uni_ieall *)&ie->u.number, cx);
	}
	if (ie->h.present & UNI_CAUSE_ATTR_P) {
		uni_print_entry(cx, "attr", "(");
		for (i = 0; i < ie->u.attr.nattr; i++) {
			uni_printf(cx, "(%u", ie->u.attr.attr[i][0]);
			if (!(ie->u.attr.attr[i][0] & 0x80)) {
				uni_printf(cx, ",%u", ie->u.attr.attr[i][1]);
				if (!(ie->u.attr.attr[i][1] & 0x80))
					uni_printf(cx, ",%u",
					    ie->u.attr.attr[i][2]);
			}
			uni_putc(')', cx);
		}
	}

	uni_print_ieend(cx);
}

DEF_IE_PRINT(itu, cause)
{
	print_cause(cx, ie, itu_causes, NULL);
}
DEF_IE_PRINT(net, cause)
{
	print_cause(cx, ie, net_causes, itu_causes);
}

const char *
uni_ie_cause2str(enum uni_coding coding, u_int cause)
{
	if (cause < 128) {
		if (coding == UNI_CODING_ITU)
			return (itu_causes[cause].str);
		if (coding == UNI_CODING_NET) {
			if (net_causes[cause].str != NULL)
				return (net_causes[cause].str);
			return (itu_causes[cause].str);
		}
	}
	return (NULL);
}

/**********************************************************************/

static int
check_cause(struct uni_ie_cause *ie, struct unicx *cx,
    const struct causetab *tab1, const struct causetab *tab2)
{
	static const u_int mask =
		UNI_CAUSE_COND_P | UNI_CAUSE_REJ_P | UNI_CAUSE_REJ_USER_P |
		UNI_CAUSE_REJ_IE_P | UNI_CAUSE_IE_P | UNI_CAUSE_TRAFFIC_P |
		UNI_CAUSE_VPCI_P | UNI_CAUSE_MTYPE_P | UNI_CAUSE_TIMER_P |
		UNI_CAUSE_TNS_P | UNI_CAUSE_NUMBER_P | UNI_CAUSE_ATTR_P |
		UNI_CAUSE_PARAM_P;

	const struct causetab *ptr;

	if ((int)ie->cause >= 128)
		return (-1);

	switch (ie->loc) {
	  default:
		return (-1);

	  case UNI_CAUSE_LOC_USER:
	  case UNI_CAUSE_LOC_PRIVLOC:
	  case UNI_CAUSE_LOC_PUBLOC:
	  case UNI_CAUSE_LOC_TRANSIT:
	  case UNI_CAUSE_LOC_PUBREM:
	  case UNI_CAUSE_LOC_PRIVREM:
	  case UNI_CAUSE_LOC_INTERNAT:
	  case UNI_CAUSE_LOC_BEYOND:
		break;
	}

	if (tab1[ie->cause].str != NULL)
		ptr = &tab1[ie->cause];
	else if (tab2 != NULL && tab2[ie->cause].str != NULL)
		ptr = &tab2[ie->cause];
	else
		return (cx->cause_hard ? -1 : 0);

	switch (ptr->diag) {

	  case UNI_DIAG_NONE:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
			break;
		}
		break;

	  case UNI_DIAG_COND:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_COND_P:
			break;
		}
		break;

	  case UNI_DIAG_REJ:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_REJ_P:
		  case UNI_CAUSE_REJ_P | UNI_CAUSE_REJ_USER_P:
		  case UNI_CAUSE_REJ_P | UNI_CAUSE_REJ_IE_P:
			break;
		}
		break;

	  case UNI_DIAG_CRATE:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_TRAFFIC_P:
			break;
		}
		break;

	  case UNI_DIAG_IE:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_IE_P:
			break;
		}
		break;

	  case UNI_DIAG_CHANID:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_VPCI_P:
			break;
		}
		break;

	  case UNI_DIAG_MTYPE:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_MTYPE_P:
			break;
		}
		break;

	  case UNI_DIAG_TIMER:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_TIMER_P:
			break;
		}
		break;

	  case UNI_DIAG_TNS:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_TNS_P:
			break;
		}
		break;

	  case UNI_DIAG_NUMBER:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_NUMBER_P:
			break;
		}
		break;

	  case UNI_DIAG_ATTR:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_ATTR_P:
			break;
		}
		break;

	  case UNI_DIAG_PARAM:
		switch (ie->h.present & mask) {
		  default:
			if (cx->cause_hard)
				return (-1);
			break;

		  case 0:
		  case UNI_CAUSE_PARAM_P:
			break;
		}
		break;
	}

	if (ie->h.present & UNI_CAUSE_COND_P) {
		switch (ie->u.cond.pu) {
		  default:
			return (-1);

		  case UNI_CAUSE_PU_PROVIDER:
		  case UNI_CAUSE_PU_USER:
			break;
		}
		switch (ie->u.cond.na) {
		  default:
			return (-1);

		  case UNI_CAUSE_NA_NORMAL:
		  case UNI_CAUSE_NA_ABNORMAL:
			break;
		}
		switch (ie->u.cond.cond) {
		  default:
			return (-1);

		  case UNI_CAUSE_COND_UNKNOWN:
		  case UNI_CAUSE_COND_PERM:
		  case UNI_CAUSE_COND_TRANS:
			break;
		}
	}
	if (ie->h.present & UNI_CAUSE_REJ_P) {
		switch (ie->u.rej.reason) {
		  default:
			return (-1);

		  case UNI_CAUSE_REASON_USER:
			switch (ie->h.present & mask) {
			  default:
				return (-1);

			  case UNI_CAUSE_REJ_P:
			  case UNI_CAUSE_REJ_P | UNI_CAUSE_REJ_USER_P:
				break;
			}
			break;

		  case UNI_CAUSE_REASON_IEMISS:
		  case UNI_CAUSE_REASON_IESUFF:
			switch (ie->h.present & mask) {
			  default:
				return (-1);

			  case UNI_CAUSE_REJ_P:
			  case UNI_CAUSE_REJ_P | UNI_CAUSE_REJ_IE_P:
				break;
			}
			break;
		}
	}
	if (ie->h.present & UNI_CAUSE_IE_P) {
		if (ie->u.ie.len == 0 || ie->u.ie.len > UNI_CAUSE_IE_N)
			return (-1);
	}
	if (ie->h.present & UNI_CAUSE_TRAFFIC_P) {
		if (ie->u.traffic.len == 0 ||
		    ie->u.traffic.len > UNI_CAUSE_TRAFFIC_N)
			return (-1);
	}

	if (ie->h.present & UNI_CAUSE_TNS_P) {
		if (uni_check_ie(UNI_IE_TNS, (union uni_ieall *)&ie->u.tns, cx))
			return (-1);
	}
	if (ie->h.present & UNI_CAUSE_NUMBER_P) {
		if(uni_check_ie(UNI_IE_CALLED, (union uni_ieall *)&ie->u.number, cx))
			return (-1);
	}
	if (ie->h.present & UNI_CAUSE_ATTR_P) {
		if(ie->u.attr.nattr > UNI_CAUSE_ATTR_N || ie->u.attr.nattr == 0)
			return (-1);
	}
	if (ie->h.present & UNI_CAUSE_PARAM_P) {
		UNUSED(cx);
	}

	return (0);
}

DEF_IE_CHECK(itu, cause)
{
	return (check_cause(ie, cx, itu_causes, NULL));
}
DEF_IE_CHECK(net, cause)
{
	return (check_cause(ie, cx, net_causes, itu_causes));
}
/**********************************************************************/

static int
encode_cause(struct uni_msg *msg, struct uni_ie_cause *ie, struct unicx *cx)
{
	u_int i;

	START_IE(cause, UNI_IE_CAUSE, 30);

	if (IE_ISERROR(*ie)) {
		APP_BYTE(msg, 0x00 | ie->loc);
	} else {
		APP_BYTE(msg, 0x80 | ie->loc);
	}
	APP_BYTE(msg, 0x80 | ie->cause);

	if (ie->h.present & UNI_CAUSE_COND_P)
		APP_BYTE(msg, 0x80 | (ie->u.cond.pu << 3) |
		    (ie->u.cond.na << 2) | ie->u.cond.cond);

	else if (ie->h.present & UNI_CAUSE_REJ_P) {
		APP_BYTE(msg, 0x80 | (ie->u.rej.reason << 2) | ie->u.rej.cond);
		if (ie->h.present & UNI_CAUSE_REJ_USER_P)
			APP_BYTE(msg, ie->u.rej.user);
		else if (ie->h.present & UNI_CAUSE_REJ_IE_P)
			APP_BYTE(msg, ie->u.rej.ie);

	} else if(ie->h.present & UNI_CAUSE_IE_P)
		APP_BUF(msg, ie->u.ie.ie, ie->u.ie.len);

	else if (ie->h.present & UNI_CAUSE_TRAFFIC_P)
		APP_BUF(msg, ie->u.traffic.traffic, ie->u.traffic.len);

	else if (ie->h.present & UNI_CAUSE_VPCI_P) {
		APP_BYTE(msg, (ie->u.vpci.vpci >> 8));
		APP_BYTE(msg, (ie->u.vpci.vpci >> 0));
		APP_BYTE(msg, (ie->u.vpci.vci >> 8));
		APP_BYTE(msg, (ie->u.vpci.vci >> 0));

	} else if (ie->h.present & UNI_CAUSE_MTYPE_P)
		APP_BYTE(msg, ie->u.mtype);

	else if (ie->h.present & UNI_CAUSE_TIMER_P) {
		APP_BYTE(msg, ie->u.timer[0]);
		APP_BYTE(msg, ie->u.timer[1]);
		APP_BYTE(msg, ie->u.timer[2]);

	} else if (ie->h.present & UNI_CAUSE_TNS_P)
		uni_encode_ie(UNI_IE_TNS, msg,
		    (union uni_ieall *)&ie->u.tns, cx);

	else if (ie->h.present & UNI_CAUSE_NUMBER_P)
		uni_encode_ie(UNI_IE_CALLED, msg,
		    (union uni_ieall *)&ie->u.number, cx);

	else if (ie->h.present & UNI_CAUSE_ATTR_P) {
		for (i = 0; i < ie->u.attr.nattr; i++) {
			APP_BYTE(msg, ie->u.attr.attr[i][0]);
			if (!ie->u.attr.attr[i][0]) {
				APP_BYTE(msg, ie->u.attr.attr[i][1]);
				if (!ie->u.attr.attr[i][1])
					APP_BYTE(msg, ie->u.attr.attr[i][2]);
			}
		}
	} else if (ie->h.present & UNI_CAUSE_PARAM_P)
		APP_BYTE(msg, ie->u.param);

	SET_IE_LEN(msg);

	return (0);
}

DEF_IE_ENCODE(itu, cause)
{
	return encode_cause(msg, ie, cx);
}
DEF_IE_ENCODE(net, cause)
{
	return encode_cause(msg, ie, cx);
}

/**********************************************************************/

static int
decode_cause(struct uni_ie_cause *ie, struct uni_msg *msg, u_int ielen,
    struct unicx *cx, const struct causetab *tab1, const struct causetab *tab2)
{
	u_char c;
	const struct causetab *ptr;
	enum uni_ietype ietype;
	u_int xielen;

	IE_START(;);

	if(ielen < 2 || ielen > 30)
		goto rej;

	c = *msg->b_rptr++;
	ielen--;
	if(!(c & 0x80))
		goto rej;
	ie->loc = c & 0xf;

	c = *msg->b_rptr++;
	ielen--;
	if(!(c & 0x80))
		goto rej;
	ie->cause = c & 0x7f;

	if(tab1[ie->cause].str != NULL)
		ptr = &tab1[ie->cause];
	else if(tab2 != NULL && tab2[ie->cause].str != NULL)
		ptr = &tab2[ie->cause];
	else {
		ptr = NULL;
		ielen = 0;	/* ignore diags */
	}

	if(ielen) {
		switch(ptr->diag) {

		  case UNI_DIAG_NONE:
			break;

		  case UNI_DIAG_COND:
			if(ielen < 1)
				goto rej;
			c = *msg->b_rptr++;
			ielen--;

			ie->h.present |= UNI_CAUSE_COND_P;
			ie->u.cond.pu = (c >> 3) & 1;
			ie->u.cond.na = (c >> 2) & 1;
			ie->u.cond.cond = c & 3;

			if(!(c & 0x80))
				goto rej;
			break;

		  case UNI_DIAG_REJ:
			if(ielen < 1)
				goto rej;
			c = *msg->b_rptr++;
			ielen--;

			ie->h.present |= UNI_CAUSE_REJ_P;
			ie->u.rej.reason = (c >> 2) & 0x1f;
			ie->u.rej.cond = c & 3;

			if(!(c & 0x80))
				goto rej;

			if(ielen > 0) {
				c = *msg->b_rptr++;
				ielen--;

				switch(ie->u.rej.reason) {

				  case UNI_CAUSE_REASON_USER:
					ie->h.present |= UNI_CAUSE_REJ_USER_P;
					ie->u.rej.user = c;
					break;

				  case UNI_CAUSE_REASON_IEMISS:
				  case UNI_CAUSE_REASON_IESUFF:
					ie->h.present |= UNI_CAUSE_REJ_IE_P;
					ie->u.rej.ie = c;
					break;
				}
			}
			break;

		  case UNI_DIAG_CRATE:
			ie->h.present |= UNI_CAUSE_TRAFFIC_P;
			while(ielen && ie->u.traffic.len < UNI_CAUSE_TRAFFIC_N) {
				ie->u.traffic.traffic[ie->u.traffic.len++] =
					*msg->b_rptr++;
				ielen--;
			}
			break;

		  case UNI_DIAG_IE:
			ie->h.present |= UNI_CAUSE_IE_P;
			while(ielen && ie->u.ie.len < UNI_CAUSE_IE_N) {
				ie->u.ie.ie[ie->u.ie.len++] = *msg->b_rptr++;
				ielen--;
			}
			break;

		  case UNI_DIAG_CHANID:
			if(ielen < 4)
				break;
			ie->h.present |= UNI_CAUSE_VPCI_P;
			ie->u.vpci.vpci  = *msg->b_rptr++ << 8;
			ie->u.vpci.vpci |= *msg->b_rptr++;
			ie->u.vpci.vci  = *msg->b_rptr++ << 8;
			ie->u.vpci.vci |= *msg->b_rptr++;
			ielen -= 4;
			break;

		  case UNI_DIAG_MTYPE:
			ie->h.present |= UNI_CAUSE_MTYPE_P;
			ie->u.mtype = *msg->b_rptr++;
			ielen--;
			break;

		  case UNI_DIAG_TIMER:
			if(ielen < 3)
				break;
			ie->h.present |= UNI_CAUSE_TIMER_P;
			ie->u.timer[0] = *msg->b_rptr++;
			ie->u.timer[1] = *msg->b_rptr++;
			ie->u.timer[2] = *msg->b_rptr++;
			ielen -= 3;
			break;

		  case UNI_DIAG_TNS:
			if(ielen < 4)
				break;
			if(uni_decode_ie_hdr(&ietype, &ie->u.tns.h, msg, cx, &xielen))
				break;
			if(ietype != UNI_IE_TNS)
				break;
			if(uni_decode_ie_body(ietype,
			    (union uni_ieall *)&ie->u.tns, msg, xielen, cx))
				break;
			ie->h.present |= UNI_CAUSE_TNS_P;
			break;

		  case UNI_DIAG_NUMBER:
			if(ielen < 4)
				break;
			if(uni_decode_ie_hdr(&ietype, &ie->u.number.h, msg, cx, &xielen))
				break;
			if(ietype != UNI_IE_CALLED)
				break;
			if(uni_decode_ie_body(ietype,
			    (union uni_ieall *)&ie->u.number, msg, xielen, cx))
				break;
			ie->h.present |= UNI_CAUSE_NUMBER_P;
			break;

		  case UNI_DIAG_ATTR:
			ie->h.present |= UNI_CAUSE_ATTR_P;
			while(ielen > 0 && ie->u.attr.nattr < UNI_CAUSE_ATTR_N) {
				c = *msg->b_rptr++;
				ie->u.attr.attr[ie->u.attr.nattr][0] = c;
				ielen--;
				if(ielen > 0 && !(c & 0x80)) {
					c = *msg->b_rptr++;
					ie->u.attr.attr[ie->u.attr.nattr][1] = c;
					ielen--;
					if(ielen > 0 && !(c & 0x80)) {
						c = *msg->b_rptr++;
						ie->u.attr.attr[ie->u.attr.nattr][2] = c;
						ielen--;
					}
				}
			}
			break;

		  case UNI_DIAG_PARAM:
			ie->h.present |= UNI_CAUSE_PARAM_P;
			ie->u.param = *msg->b_rptr++;
			ielen--;
			break;
		}
	}

	IE_END(CAUSE);
}

DEF_IE_DECODE(itu, cause)
{
	return decode_cause(ie, msg, ielen, cx, itu_causes, NULL);
}
DEF_IE_DECODE(net, cause)
{
	return decode_cause(ie, msg, ielen, cx, net_causes, itu_causes);
}

/*********************************************************************
 *
 * Callstate
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 59...60
 *  UNI4.0 pp. 14
 *
 * Only ITU-T coding allowed.
 */
DEF_IE_PRINT(itu, callstate)
{
	static const struct uni_print_tbl tbl[] = {
		MKT(UNI_CALLSTATE_U0,	U0/N0/REST0),
		MKT(UNI_CALLSTATE_U1,	U1/N1),
		MKT(UNI_CALLSTATE_U3,	U3/N3),
		MKT(UNI_CALLSTATE_U4,	U4/N4),
		MKT(UNI_CALLSTATE_U6,	U6/N6),
		MKT(UNI_CALLSTATE_U7,	U7/N7),
		MKT(UNI_CALLSTATE_U8,	U8/N8),
		MKT(UNI_CALLSTATE_U9,	U9/N9),
		MKT(UNI_CALLSTATE_U10,	U10/N10),
		MKT(UNI_CALLSTATE_U11,	U11/N11),
		MKT(UNI_CALLSTATE_U12,	U12/N12),
		MKT(UNI_CALLSTATE_REST1,REST1),
		MKT(UNI_CALLSTATE_REST2,REST2),
		MKT(UNI_CALLSTATE_U13,	U13/N13),
		MKT(UNI_CALLSTATE_U14,	U14/N14),
		EOT()
	};

	if(uni_print_iehdr("callstate", &ie->h, cx))
		return;
	uni_print_tbl("state", ie->state, tbl, cx);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, callstate)
{
	UNUSED(cx);

	switch(ie->state) {
	  default:
		return -1;

	  case UNI_CALLSTATE_U0:
	  case UNI_CALLSTATE_U1:
	  case UNI_CALLSTATE_U3:
	  case UNI_CALLSTATE_U4:
	  case UNI_CALLSTATE_U6:
	  case UNI_CALLSTATE_U7:
	  case UNI_CALLSTATE_U8:
	  case UNI_CALLSTATE_U9:
	  case UNI_CALLSTATE_U10:
	  case UNI_CALLSTATE_U11:
	  case UNI_CALLSTATE_U12:
	  case UNI_CALLSTATE_REST1:
	  case UNI_CALLSTATE_REST2:
	  case UNI_CALLSTATE_U13:
	  case UNI_CALLSTATE_U14:
		break;
	}

	return 0;
}

DEF_IE_ENCODE(itu, callstate)
{
	START_IE(callstate, UNI_IE_CALLSTATE, 1);

	APP_BYTE(msg, ie->state);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, callstate)
{
	IE_START(;);

	if(ielen != 1)
		goto rej;

	ie->state = *msg->b_rptr++ & 0x3f;
	ielen--;

	IE_END(CALLSTATE);
}

/*********************************************************************
 *
 * Facility Information.
 *
 * References for this IE are:
 *
 *  Q.2932.1
 *
 * The standard allows only ROSE as protocol. We allow everything up to the
 * maximum size.
 *
 * Only ITU-T coding allowed.
 */
DEF_IE_PRINT(itu, facility)
{
	u_int i;

	if(uni_print_iehdr("facility", &ie->h, cx))
		return;

	if(ie->proto == UNI_FACILITY_ROSE)
		uni_print_entry(cx, "proto", "rose");
	else
		uni_print_entry(cx, "proto", "0x%02x", ie->proto);

	uni_print_entry(cx, "len", "%u", ie->len);
	uni_print_entry(cx, "info", "(");
	for(i = 0; i < ie->len; i++)
		uni_printf(cx, "%s0x%02x", i == 0 ? "" : " ", ie->apdu[i]);
	uni_printf(cx, ")");

	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, facility)
{
	UNUSED(cx);

	if(ie->len > UNI_FACILITY_MAXAPDU)
		return -1;

	return 0;
}

DEF_IE_ENCODE(itu, facility)
{
	START_IE(facility, UNI_IE_FACILITY, 1 + ie->len);

	APP_BYTE(msg, ie->proto | 0x80);
	APP_BUF(msg, ie->apdu, ie->len);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, facility)
{
	u_char c;

	IE_START(;);

	if(ielen > UNI_FACILITY_MAXAPDU + 1 || ielen < 1)
		goto rej;

	ie->proto = (c = *msg->b_rptr++) & 0x1f;
	ielen--;
	if((c & 0xe0) != 0x80)
		goto rej;

	ie->len = ielen;
	ielen = 0;
	(void)memcpy(ie->apdu, msg->b_rptr, ie->len);
	msg->b_rptr += ie->len;

	IE_END(FACILITY);
}

/*********************************************************************
 *
 * Notification Indicator
 *
 * References for this IE are:
 *
 *  Q.2931 p.  76
 *  UNI4.0 p.  17
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, notify)
{
	u_int i;

	if(uni_print_iehdr("notify", &ie->h, cx))
		return;
	uni_print_entry(cx, "len", "%u", ie->len);
	uni_print_entry(cx, "info", "(");
	for(i = 0; i < ie->len; i++)
		uni_printf(cx, "%s0x%02x", i == 0 ? "" : " ", ie->notify[i]);
	uni_printf(cx, ")");
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, notify)
{
	UNUSED(cx);

	if(ie->len > UNI_NOTIFY_MAXLEN)
		return -1;

	return 0;
}

DEF_IE_ENCODE(itu, notify)
{
	START_IE(notify, UNI_IE_NOTIFY, ie->len);

	APP_BUF(msg, ie->notify, ie->len);
	if (IE_ISERROR(*ie)) {
		/* make it too long */
		u_int i = ie->len;

		while (i < UNI_NOTIFY_MAXLEN + 1) {
			APP_BYTE(msg, 0x00);
			i++;
		}
	}

	SET_IE_LEN(msg);
	return (0);
}

DEF_IE_DECODE(itu, notify)
{
	IE_START(;);

	if (ielen > UNI_NOTIFY_MAXLEN || ielen < 1)
		goto rej;

	ie->len = ielen;
	ielen = 0;
	(void)memcpy(ie->notify, msg->b_rptr, ie->len);
	msg->b_rptr += ie->len;

	IE_END(NOTIFY);
}

/*********************************************************************
 *
 * End-to-end transit delay.
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 70...71
 *  UNI4.0 pp. 69...70
 *  PNNI1.0 pp. 198...200
 *
 * Not clear, whether the new indicator should be used with NET coding or
 * not.
 *
 * Only ITU-T coding allowed.
 */

static void
print_eetd(struct uni_ie_eetd *ie, struct unicx *cx)
{
	if (uni_print_iehdr("eetd", &ie->h, cx))
		return;

	if (ie->h.present & UNI_EETD_CUM_P)
		uni_print_entry(cx, "cum", "%u", ie->cumulative);
	if (ie->h.present & UNI_EETD_MAX_P) {
		if (ie->maximum == UNI_EETD_ANYMAX)
			uni_print_entry(cx, "max", "any");
		else
			uni_print_entry(cx, "max", "%u", ie->maximum);
	}
	if (ie->h.present & UNI_EETD_PCTD_P)
		uni_print_entry(cx, "pnni_cum", "%u", ie->pctd);
	if (ie->h.present & UNI_EETD_PMTD_P)
		uni_print_entry(cx, "pnni_max", "%u", ie->pmtd);
	if (ie->h.present & UNI_EETD_NET_P)
		uni_print_flag("netgen", cx);

	uni_print_ieend(cx);
}
DEF_IE_PRINT(itu, eetd)
{
	print_eetd(ie, cx);
}
DEF_IE_PRINT(net, eetd)
{
	print_eetd(ie, cx);
}

DEF_IE_CHECK(itu, eetd)
{

	UNUSED(cx);

	if (!(ie->h.present & UNI_EETD_CUM_P))
		return (-1);
	if (ie->h.present & (UNI_EETD_PMTD_P | UNI_EETD_PCTD_P))
		return (-1);
	return (0);
}

DEF_IE_CHECK(net, eetd)
{

	if (!cx->pnni) {
		if (!(ie->h.present & UNI_EETD_CUM_P))
			return (-1);
		if (ie->h.present & (UNI_EETD_PMTD_P | UNI_EETD_PCTD_P))
			return (-1);
	} else {
		if (ie->h.present & UNI_EETD_MAX_P)
			return (-1);
		if ((ie->h.present & UNI_EETD_CUM_P) &&
		    (ie->h.present & UNI_EETD_PCTD_P))
			return (-1);
	}
	return (0);
}

DEF_IE_ENCODE(itu, eetd)
{
	START_IE(eetd, UNI_IE_EETD, 9);

	if (ie->h.present & UNI_EETD_CUM_P) {
		APP_BYTE(msg, UNI_EETD_CTD_ID);
		APP_16BIT(msg, ie->cumulative);
	}
	if (ie->h.present & UNI_EETD_MAX_P) {
		APP_BYTE(msg, UNI_EETD_MTD_ID);
		APP_16BIT(msg, ie->maximum);
	}
	if (ie->h.present & UNI_EETD_PMTD_P) {
		APP_BYTE(msg, UNI_EETD_PMTD_ID);
		APP_24BIT(msg, ie->pmtd);
	}
	if (ie->h.present & UNI_EETD_PCTD_P) {
		APP_BYTE(msg, UNI_EETD_PCTD_ID);
		APP_24BIT(msg, ie->pctd);
	}
	if (ie->h.present & UNI_EETD_NET_P) {
		APP_BYTE(msg, UNI_EETD_NET_ID);
	}

	SET_IE_LEN(msg);
	return (0);
}

DEF_IE_ENCODE(net, eetd)
{
	return (uni_ie_encode_itu_eetd(msg, ie, cx));
}

DEF_IE_DECODE(itu, eetd)
{
	IE_START(;);

	while (ielen > 0) {
		switch (ielen--, *msg->b_rptr++) {

		  case UNI_EETD_CTD_ID:
			if (ielen < 2)
				goto rej;
			ie->h.present |= UNI_EETD_CUM_P;
			ie->cumulative = *msg->b_rptr++ << 8;
			ie->cumulative |= *msg->b_rptr++;
			ielen -= 2;
			break;

		  case UNI_EETD_MTD_ID:
			if (ielen < 2)
				goto rej;
			ie->h.present |= UNI_EETD_MAX_P;
			ie->maximum = *msg->b_rptr++ << 8;
			ie->maximum |= *msg->b_rptr++;
			ielen -= 2;
			break;

		  case UNI_EETD_PCTD_ID:
			if (ielen < 3)
				goto rej;
			ie->h.present |= UNI_EETD_PCTD_P;
			ie->pctd = *msg->b_rptr++ << 16;
			ie->pctd |= *msg->b_rptr++ << 8;
			ie->pctd |= *msg->b_rptr++;
			ielen -= 3;
			break;

		  case UNI_EETD_PMTD_ID:
			if (ielen < 3)
				goto rej;
			ie->h.present |= UNI_EETD_PMTD_P;
			ie->pmtd = *msg->b_rptr++ << 16;
			ie->pmtd |= *msg->b_rptr++ << 8;
			ie->pmtd |= *msg->b_rptr++;
			ielen -= 3;
			break;

		  case UNI_EETD_NET_ID:
			ie->h.present |= UNI_EETD_NET_P;
			break;

		  default:
			goto rej;
		}
	}

	IE_END(EETD);
}
DEF_IE_DECODE(net, eetd)
{
	return (uni_ie_decode_itu_eetd(ie, msg, ielen, cx));
}

/*********************************************************************
 *
 * Called address
 * Called subaddress
 * Calling address
 * Calling subaddress
 * Connected address
 * Connected subaddress
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 60...68
 *  ...A4  pp. 27...36
 *  UNI4.0 pp. 14...15
 *  Q.2951 pp. 28...40
 *
 * It is assumed, that the coding of the addr arrays is ok.
 *
 * Only ITU-T coding allowed.
 */

static const struct uni_print_tbl screen_tbl[] = {
	MKT(UNI_ADDR_SCREEN_NOT,	no),
	MKT(UNI_ADDR_SCREEN_PASSED,	passed),
	MKT(UNI_ADDR_SCREEN_FAILED,	failed),
	MKT(UNI_ADDR_SCREEN_NET,	network),
	EOT()
};
static const struct uni_print_tbl pres_tbl[] = {
	MKT(UNI_ADDR_PRES,		allowed),
	MKT(UNI_ADDR_RESTRICT,		restricted),
	MKT(UNI_ADDR_NONUMBER,		no-number),
	EOT()
};


static void
print_addr(struct unicx *cx, struct uni_addr *addr)
{
	static const struct uni_print_tbl plan_tbl[] = {
		MKT(UNI_ADDR_UNKNOWN,	unknown),
		MKT(UNI_ADDR_E164,	E164),
		MKT(UNI_ADDR_ATME,	ATME),
		MKT(UNI_ADDR_DATA,	data),
		MKT(UNI_ADDR_PRIVATE,	private),
		EOT()
	};
	static const struct uni_print_tbl type_tbl[] = {
		MKT(UNI_ADDR_UNKNOWN,		unknown),
		MKT(UNI_ADDR_INTERNATIONAL,	international),
		MKT(UNI_ADDR_NATIONAL,		national),
		MKT(UNI_ADDR_NETWORK,		network),
		MKT(UNI_ADDR_SUBSCR,		subscriber),
		MKT(UNI_ADDR_ABBR,		abbreviated),
		EOT()
	};
	u_int i;

	uni_print_entry(cx, "addr", "(");
	uni_print_tbl(NULL, addr->type, type_tbl, cx);
	uni_putc(',', cx);
	uni_print_tbl(NULL, addr->plan, plan_tbl, cx);
	uni_putc(',', cx);
	if(addr->plan == UNI_ADDR_E164) {
		uni_putc('"', cx);
		for(i = 0; i < addr->len; i++) {
			if(addr->addr[i] < ' ')
				uni_printf(cx, "^%c", addr->addr[i] + '@');
			else if(addr->addr[i] <= '~')
				uni_putc(addr->addr[i], cx);
			else
				uni_printf(cx, "\\%03o", addr->addr[i]);
		}
		uni_putc('"', cx);

	} else if(addr->plan == UNI_ADDR_ATME) {
		for(i = 0; i < addr->len; i++)
			uni_printf(cx, "%02x", addr->addr[i]);
	}
	uni_putc(')', cx);
}

static void
print_addrsub(struct unicx *cx, struct uni_subaddr *addr)
{
	static const struct uni_print_tbl type_tbl[] = {
		MKT(UNI_SUBADDR_NSAP,	NSAP),
		MKT(UNI_SUBADDR_ATME,	ATME),
		MKT(UNI_SUBADDR_USER,	USER),
		EOT()
	};
	u_int i;

	uni_print_entry(cx, "addr", "(");
	uni_print_tbl(NULL, addr->type, type_tbl, cx);
	uni_putc(',', cx);

	for(i = 0; i < addr->len; i++)
		uni_printf(cx, "%02x", addr->addr[i]);

	uni_putc(')', cx);
}

static int
check_addr(struct uni_addr *addr)
{
	u_int i;

	switch(addr->plan) {
	  default:
		return -1;

	  case UNI_ADDR_E164:
		if(addr->type != UNI_ADDR_INTERNATIONAL)
			return -1;
		if(addr->len > 15 || addr->len == 0)
			return -1;
		for(i = 0; i < addr->len; i++)
			if(addr->addr[i] == 0 || (addr->addr[i] & 0x80))
				return -1;
		break;

	  case UNI_ADDR_ATME:
		if(addr->type != UNI_ADDR_UNKNOWN)
			return -1;
		if(addr->len != 20)
			return -1;
		break;
	}

	return 0;
}

static int
check_subaddr(struct uni_subaddr *addr)
{
	switch(addr->type) {
	  default:
		return -1;

	  case UNI_SUBADDR_NSAP:
		if(addr->len != 20)
			return -1;
		break;

	  case UNI_SUBADDR_ATME:
		if(addr->len > 20)
			return -1;
		break;
	}
	return 0;
}

static int
check_screen(enum uni_addr_screen screen, enum uni_addr_pres pres)
{
	switch(pres) {
	  default:
		return -1;

	  case UNI_ADDR_PRES:
	  case UNI_ADDR_RESTRICT:
	  case UNI_ADDR_NONUMBER:
		break;
	}
	switch(screen) {
	  default:
		return -1;

	  case UNI_ADDR_SCREEN_NOT:
	  case UNI_ADDR_SCREEN_PASSED:
	  case UNI_ADDR_SCREEN_FAILED:
	  case UNI_ADDR_SCREEN_NET:
		break;
	}

	return 0;
}

static void
encode_addr(struct uni_msg *msg, struct uni_addr *addr, u_int flag,
    enum uni_addr_screen screen, enum uni_addr_pres pres, int err)
{
	u_char ext = err ? 0x00 : 0x80;

	if (flag) {
		APP_BYTE(msg, (addr->type << 4) | addr->plan);
		APP_BYTE(msg, ext | (pres << 5) | (screen));
	} else {
		APP_BYTE(msg, ext | (addr->type << 4) | addr->plan);
	}
	APP_BUF(msg, addr->addr, addr->len);
}

static void
encode_subaddr(struct uni_msg *msg, struct uni_subaddr *addr)
{
	APP_BYTE(msg, 0x80|(addr->type<<4));
	APP_BUF(msg, addr->addr, addr->len);
}

static int
decode_addr(struct uni_addr *addr, u_int ielen, struct uni_msg *msg, u_int plan)
{
	addr->plan = plan & 0xf;
	addr->type = (plan >> 4) & 0x7;

	switch(addr->plan) {

	  case UNI_ADDR_E164:
		if(ielen > 15 || ielen == 0)
			return -1;
		addr->addr[ielen] = 0;
		break;

	  case UNI_ADDR_ATME:
		if(ielen != 20)
			return -1;
		break;

	  default:
		return -1;
	}
	(void)memcpy(addr->addr, msg->b_rptr, ielen);
	addr->len = ielen;
	msg->b_rptr += ielen;

	return 0;
}

static int
decode_subaddr(struct uni_subaddr *addr, u_int ielen, struct uni_msg *msg,
    u_int type)
{
	switch(addr->type = (type >> 4) & 0x7) {

	  case UNI_SUBADDR_NSAP:
		if(ielen == 0 || ielen > 20)
			return -1;
		break;

	  case UNI_SUBADDR_ATME:
		if(ielen != 20)
			return -1;
		break;

	  default:
		return -1;
	}
	if(!(type & 0x80))
		return -1;
	if((type & 0x7) != 0)
		return -1;

	addr->len = ielen;
	(void)memcpy(addr->addr, msg->b_rptr, ielen);
	msg->b_rptr += ielen;

	return 0;
}

/**********************************************************************/

DEF_IE_PRINT(itu, called)
{
	if (uni_print_iehdr("called", &ie->h, cx))
		return;
	print_addr(cx, &ie->addr);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, called)
{
	UNUSED(cx);

	if (check_addr(&ie->addr))
		return (-1);
	return (0);
}

DEF_IE_ENCODE(itu, called)
{
	START_IE(called, UNI_IE_CALLED, 21);
	encode_addr(msg, &ie->addr, 0, 0, 0, IE_ISERROR(*ie));
	SET_IE_LEN(msg);
	return (0);
}

DEF_IE_DECODE(itu, called)
{
	u_char c;
	IE_START(;);

	if (ielen > 21 || ielen < 1)
		goto rej;

	c = *msg->b_rptr++;
	ielen--;

	if (!(c & 0x80))
		goto rej;

	if (decode_addr(&ie->addr, ielen, msg, c))
		goto rej;

	IE_END(CALLED);
}

/**********************************************************************/

DEF_IE_PRINT(itu, calledsub)
{
	if(uni_print_iehdr("calledsub", &ie->h, cx))
		return;
	print_addrsub(cx, &ie->addr);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, calledsub)
{
	UNUSED(cx);

	if(check_subaddr(&ie->addr))
		return -1;
	return 0;
}

DEF_IE_ENCODE(itu, calledsub)
{
	START_IE(calledsub, UNI_IE_CALLEDSUB, 21);
	encode_subaddr(msg, &ie->addr);
	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, calledsub)
{
	u_char c;

	IE_START(;);

	if(ielen > 21)
		goto rej;

	c = *msg->b_rptr++;
	ielen--;

	if(decode_subaddr(&ie->addr, ielen, msg, c))
		goto rej;

	IE_END(CALLEDSUB);
}

/**********************************************************************/

DEF_IE_PRINT(itu, calling)
{
	if(uni_print_iehdr("calling", &ie->h, cx))
		return;
	print_addr(cx, &ie->addr);

	if(ie->h.present & UNI_CALLING_SCREEN_P) {
		uni_print_tbl("screening", ie->screen, screen_tbl, cx);
		uni_print_tbl("presentation", ie->pres, pres_tbl, cx);
	}

	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, calling)
{
	UNUSED(cx);

	if(check_addr(&ie->addr))
		return -1;

	if(ie->h.present & UNI_CALLING_SCREEN_P)
		if(check_screen(ie->screen, ie->pres))
			return -1;
	return 0;
}

DEF_IE_ENCODE(itu, calling)
{
	START_IE(calling, UNI_IE_CALLING, 22);
	encode_addr(msg, &ie->addr, ie->h.present & UNI_CALLING_SCREEN_P, ie->screen, ie->pres, IE_ISERROR(*ie));
	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, calling)
{
	u_char c, plan;

	IE_START(;);

	if(ielen > 22 || ielen < 1)
		goto rej;

	plan = *msg->b_rptr++;
	ielen--;

	if(!(plan & 0x80)) {
		if(ielen == 0)
			goto rej;
		ielen--;
		c = *msg->b_rptr++;

		ie->h.present |= UNI_CALLING_SCREEN_P;
		ie->pres = (c >> 5) & 0x3;
		ie->screen = c & 0x3;

		if(!(c & 0x80))
			goto rej;
	}

	if(decode_addr(&ie->addr, ielen, msg, plan))
		goto rej;

	IE_END(CALLING);
}

/**********************************************************************/

DEF_IE_PRINT(itu, callingsub)
{
	if(uni_print_iehdr("callingsub", &ie->h, cx))
		return;
	print_addrsub(cx, &ie->addr);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, callingsub)
{
	UNUSED(cx);

	if(check_subaddr(&ie->addr))
		return -1;
	return 0;
}

DEF_IE_ENCODE(itu, callingsub)
{
	START_IE(callingsub, UNI_IE_CALLINGSUB, 21);
	encode_subaddr(msg, &ie->addr);
	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, callingsub)
{
	u_char c;

	IE_START(;);

	if(ielen > 21)
		goto rej;

	c = *msg->b_rptr++;
	ielen--;

	if(decode_subaddr(&ie->addr, ielen, msg, c))
		goto rej;

	IE_END(CALLINGSUB);
}

/**********************************************************************/

DEF_IE_PRINT(itu, conned)
{
	if(uni_print_iehdr("conned", &ie->h, cx))
		return;
	print_addr(cx, &ie->addr);

	if(ie->h.present & UNI_CONNED_SCREEN_P) {
		uni_print_tbl("screening", ie->screen, screen_tbl, cx);
		uni_print_tbl("presentation", ie->pres, pres_tbl, cx);
	}

	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, conned)
{
	UNUSED(cx);

	if(check_addr(&ie->addr))
		return -1;

	if(ie->h.present & UNI_CONNED_SCREEN_P)
		if(check_screen(ie->screen, ie->pres))
			return -1;
	return 0;
}

DEF_IE_ENCODE(itu, conned)
{
	START_IE(conned, UNI_IE_CONNED, 22);
	encode_addr(msg, &ie->addr, ie->h.present & UNI_CONNED_SCREEN_P, ie->screen, ie->pres, IE_ISERROR(*ie));
	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, conned)
{
	u_char c, plan;

	IE_START(;);

	if(ielen > 22 || ielen < 1)
		goto rej;

	plan = *msg->b_rptr++;
	ielen--;

	if(!(plan & 0x80)) {
		if(ielen == 0)
			goto rej;
		ielen--;
		c = *msg->b_rptr++;

		ie->h.present |= UNI_CONNED_SCREEN_P;
		ie->pres = (c >> 5) & 0x3;
		ie->screen = c & 0x3;

		if(!(c & 0x80))
			goto rej;
	}

	if(decode_addr(&ie->addr, ielen, msg, plan))
		goto rej;

	IE_END(CONNED);
}

/**********************************************************************/

DEF_IE_PRINT(itu, connedsub)
{
	if(uni_print_iehdr("connedsub", &ie->h, cx))
		return;
	print_addrsub(cx, &ie->addr);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, connedsub)
{
	UNUSED(cx);

	if(check_subaddr(&ie->addr))
		return -1;
	return 0;
}

DEF_IE_ENCODE(itu, connedsub)
{
	START_IE(connedsub, UNI_IE_CONNEDSUB, 21);
	encode_subaddr(msg, &ie->addr);
	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, connedsub)
{
	u_char c;

	IE_START(;);

	if(ielen > 21)
		goto rej;

	c = *msg->b_rptr++;
	ielen--;

	if(decode_subaddr(&ie->addr, ielen, msg, c))
		goto rej;

	IE_END(CONNEDSUB);
}

/*********************************************************************
 *
 * Endpoint reference.
 *
 * References for this IE are:
 *
 *  Q.2971 p.  14
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, epref)
{
	if(uni_print_iehdr("epref", &ie->h, cx))
		return;
	uni_print_entry(cx, "epref", "(%u,%u)", ie->flag, ie->epref);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, epref)
{
	UNUSED(cx);

	if(ie->epref >= (2<<15))
		return -1;

	return 0;
}

DEF_IE_ENCODE(itu, epref)
{
	START_IE(epref, UNI_IE_EPREF, 3);

	if (IE_ISERROR(*ie))
		APP_BYTE(msg, 0xff);
	else
		APP_BYTE(msg, 0);
	APP_BYTE(msg, (ie->flag << 7) | ((ie->epref >> 8) & 0x7f));
	APP_BYTE(msg, (ie->epref & 0xff));

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, epref)
{
	u_char c;

	IE_START(;);

	if(ielen != 3)
		goto rej;
	if(*msg->b_rptr++ != 0)
		goto rej;

	c = *msg->b_rptr++;
	ie->flag = (c & 0x80) ? 1 : 0;
	ie->epref = (c & 0x7f) << 8;
	ie->epref |= *msg->b_rptr++;

	IE_END(EPREF);
}

/*********************************************************************
 *
 * Endpoint state.
 *
 * References for this IE are:
 *
 *  Q.2971 pp. 14...15
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, epstate)
{
	static const struct uni_print_tbl tbl[] = {
		MKT(UNI_EPSTATE_NULL,		null),
		MKT(UNI_EPSTATE_ADD_INIT,	add-initiated),
		MKT(UNI_EPSTATE_ALERT_DLVD,	alerting-delivered),
		MKT(UNI_EPSTATE_ADD_RCVD,	add-received),
		MKT(UNI_EPSTATE_ALERT_RCVD,	alerting-received),
		MKT(UNI_EPSTATE_ACTIVE,		active),
		MKT(UNI_EPSTATE_DROP_INIT,	drop-initiated),
		MKT(UNI_EPSTATE_DROP_RCVD,	drop-received),
		EOT()
	};

	if(uni_print_iehdr("epstate", &ie->h, cx))
		return;
	uni_print_tbl("state", ie->state, tbl, cx);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, epstate)
{
	UNUSED(cx);

	switch(ie->state) {
	  default:
		return -1;

	  case UNI_EPSTATE_NULL:
	  case UNI_EPSTATE_ADD_INIT:
	  case UNI_EPSTATE_ALERT_DLVD:
	  case UNI_EPSTATE_ADD_RCVD:
	  case UNI_EPSTATE_ALERT_RCVD:
	  case UNI_EPSTATE_DROP_INIT:
	  case UNI_EPSTATE_DROP_RCVD:
	  case UNI_EPSTATE_ACTIVE:
		break;
	}

	return 0;
}

DEF_IE_ENCODE(itu, epstate)
{
	START_IE(epstate, UNI_IE_EPSTATE, 1);

	APP_BYTE(msg, ie->state);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, epstate)
{
	IE_START(;);

	if(ielen != 1)
		goto rej;

	ie->state = *msg->b_rptr++ & 0x3f;

	IE_END(EPSTATE);
}

/*********************************************************************
 *
 * ATM adaptation layer parameters
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 43...49
 *  Q.2931 Amd 2
 *  UNI4.0 p.  9
 *
 * UNI4.0 states, that AAL2 is not supported. However we keep it. No
 * parameters are associated with AAL2.
 *
 * Amd2 not checked. XXX
 *
 * Only ITU-T coding allowed.
 */
DEF_IE_PRINT(itu, aal)
{
	static const struct uni_print_tbl aal_tbl[] = {
		MKT(UNI_AAL_0,			VOICE),
		MKT(UNI_AAL_1,			1),
		MKT(UNI_AAL_2,			2),
		MKT(UNI_AAL_4,			3/4),
		MKT(UNI_AAL_5,			5),
		MKT(UNI_AAL_USER,		USER),
		EOT()
	};
	static const struct uni_print_tbl subtype_tbl[] = {
		MKT(UNI_AAL1_SUB_NULL,		null),
		MKT(UNI_AAL1_SUB_VOICE,		voice),
		MKT(UNI_AAL1_SUB_CIRCUIT,	circuit),
		MKT(UNI_AAL1_SUB_HQAUDIO,	hqaudio),
		MKT(UNI_AAL1_SUB_VIDEO,		video),
		EOT()
	};
	static const struct uni_print_tbl cbr_rate_tbl[] = {
		MKT(UNI_AAL1_CBR_64,		64),
		MKT(UNI_AAL1_CBR_1544,		1544(DS1)),
		MKT(UNI_AAL1_CBR_6312,		6312(DS2)),
		MKT(UNI_AAL1_CBR_32064,		32064),
		MKT(UNI_AAL1_CBR_44736,		44736(DS3)),
		MKT(UNI_AAL1_CBR_97728,		97728),
		MKT(UNI_AAL1_CBR_2048,		2048(E1)),
		MKT(UNI_AAL1_CBR_8448,		8448(E2)),
		MKT(UNI_AAL1_CBR_34368,		34368(E3)),
		MKT(UNI_AAL1_CBR_139264,	139264),
		MKT(UNI_AAL1_CBR_N64,		Nx64),
		MKT(UNI_AAL1_CBR_N8,		Nx8),
		EOT()
	};
	static const struct uni_print_tbl screc_tbl[] = {
		MKT(UNI_AAL1_SCREC_NULL,	null),
		MKT(UNI_AAL1_SCREC_SRTS,	srts),
		MKT(UNI_AAL1_SCREC_ACLK,	aclk),
		EOT()
	};
	static const struct uni_print_tbl ecm_tbl[] = {
		MKT(UNI_AAL1_ECM_NULL,		null),
		MKT(UNI_AAL1_ECM_LOSS,		loss),
		MKT(UNI_AAL1_ECM_DELAY,		delay),
		EOT()
	};
	static const struct uni_print_tbl sscs_tbl[] = {
		MKT(UNI_AAL_SSCS_NULL,		null),
		MKT(UNI_AAL_SSCS_SSCOPA,	sscopa),
		MKT(UNI_AAL_SSCS_SSCOPU,	sscopu),
		MKT(UNI_AAL_SSCS_FRAME,		frame),
		EOT()
	};

	if(uni_print_iehdr("aal", &ie->h, cx))
		return;
	uni_print_tbl("type", ie->type, aal_tbl, cx);

	switch(ie->type) {

	  case UNI_AAL_0:
		uni_print_push_prefix("0", cx);
		cx->indent++;
		break;

	  case UNI_AAL_2:
		uni_print_push_prefix("2", cx);
		cx->indent++;
		break;

	  case UNI_AAL_1:
		uni_print_push_prefix("1", cx);
		cx->indent++;
		uni_print_tbl("subtype", ie->u.aal1.subtype, subtype_tbl, cx);
		uni_print_tbl("cbr_rate", ie->u.aal1.cbr_rate, cbr_rate_tbl, cx);
		if(ie->h.present & UNI_AAL1_MULT_P)
			uni_print_entry(cx, "mult", "%u", ie->u.aal1.mult);
		if(ie->h.present & UNI_AAL1_SCREC_P)
			uni_print_tbl("screc", ie->u.aal1.screc, screc_tbl, cx);
		if(ie->h.present & UNI_AAL1_ECM_P)
			uni_print_tbl("ecm", ie->u.aal1.ecm, ecm_tbl, cx);
		if(ie->h.present & UNI_AAL1_BSIZE_P)
			uni_print_entry(cx, "bsize", "%u", ie->u.aal1.bsize);
		if(ie->h.present & UNI_AAL1_PART_P)
			uni_print_entry(cx, "part", "%u", ie->u.aal1.part);
		break;

	  case UNI_AAL_4:
		uni_print_push_prefix("4", cx);
		cx->indent++;
		if(ie->h.present & UNI_AAL4_CPCS_P)
			uni_print_entry(cx, "cpcs", "(%u,%u)", ie->u.aal4.fwd_cpcs,
				ie->u.aal4.bwd_cpcs);
		if(ie->h.present & UNI_AAL4_MID_P)
			uni_print_entry(cx, "mid", "(%u,%u)", ie->u.aal4.mid_low,
				ie->u.aal4.mid_high);
		if(ie->h.present & UNI_AAL4_SSCS_P)
			uni_print_tbl("sscs", ie->u.aal4.sscs, sscs_tbl, cx);
		break;

	  case UNI_AAL_5:
		uni_print_push_prefix("5", cx);
		cx->indent++;
		if(ie->h.present & UNI_AAL5_CPCS_P)
			uni_print_entry(cx, "cpcs", "(%u,%u)", ie->u.aal5.fwd_cpcs,
				ie->u.aal5.bwd_cpcs);
		if(ie->h.present & UNI_AAL5_SSCS_P)
			uni_print_tbl("sscs", ie->u.aal5.sscs, sscs_tbl, cx);
		break;

	  case UNI_AAL_USER:
		uni_print_push_prefix("user", cx);
		cx->indent++;
		if(ie->u.aalu.len > 4) {
			uni_print_entry(cx, "info", "ERROR(len=%u)", ie->u.aalu.len);
		} else {
			u_int i;

			uni_print_entry(cx, "info", "(");
			for(i = 0; i < ie->u.aalu.len; i++)
				uni_printf(cx, "%s%u", !i?"":",", ie->u.aalu.user[i]);
			uni_printf(cx, ")");
		}
		break;
	}
	cx->indent--;
	uni_print_pop_prefix(cx);
	uni_print_eol(cx);

	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, aal)
{
	UNUSED(cx);

	if(ie->type == UNI_AAL_0) {
		;
	} else if(ie->type == UNI_AAL_1) {
		switch(ie->u.aal1.subtype) {

		  default:
			return -1;

		  case UNI_AAL1_SUB_NULL:
		  case UNI_AAL1_SUB_VOICE:
		  case UNI_AAL1_SUB_CIRCUIT:
		  case UNI_AAL1_SUB_HQAUDIO:
		  case UNI_AAL1_SUB_VIDEO:
			break;
		}
		switch(ie->u.aal1.cbr_rate) {

		  default:
			return -1;

		  case UNI_AAL1_CBR_64:
		  case UNI_AAL1_CBR_1544:
		  case UNI_AAL1_CBR_6312:
		  case UNI_AAL1_CBR_32064:
		  case UNI_AAL1_CBR_44736:
		  case UNI_AAL1_CBR_97728:
		  case UNI_AAL1_CBR_2048:
		  case UNI_AAL1_CBR_8448:
		  case UNI_AAL1_CBR_34368:
		  case UNI_AAL1_CBR_139264:
			if((ie->h.present & UNI_AAL1_MULT_P))
				return -1;
			break;

		  case UNI_AAL1_CBR_N64:
			if(!(ie->h.present & UNI_AAL1_MULT_P))
				return -1;
			if(ie->u.aal1.mult < 2)
				return -1;
			break;

		  case UNI_AAL1_CBR_N8:
			if(!(ie->h.present & UNI_AAL1_MULT_P))
				return -1;
			if(ie->u.aal1.mult == 0 || ie->u.aal1.mult > 7)
				return -1;
			break;
		}
		if(ie->h.present & UNI_AAL1_SCREC_P) {
			switch(ie->u.aal1.screc) {

			  default:
				return -1;

			  case UNI_AAL1_SCREC_NULL:
			  case UNI_AAL1_SCREC_SRTS:
			  case UNI_AAL1_SCREC_ACLK:
				break;
			}
		}
		if(ie->h.present & UNI_AAL1_ECM_P) {
			switch(ie->u.aal1.ecm) {

			  default:
				return -1;

			  case UNI_AAL1_ECM_NULL:
			  case UNI_AAL1_ECM_LOSS:
			  case UNI_AAL1_ECM_DELAY:
				break;
			}
		}
		if(ie->h.present & UNI_AAL1_BSIZE_P) {
			if(ie->u.aal1.bsize == 0)
				return -1;
		}
		if(ie->h.present & UNI_AAL1_PART_P) {
			if(ie->u.aal1.part == 0 || ie->u.aal1.part > 47)
				return -1;
		}

	} else if(ie->type == UNI_AAL_2) {
		;

	} else if(ie->type == UNI_AAL_4) {
		if(ie->h.present & UNI_AAL4_MID_P) {
			if(ie->u.aal4.mid_low >= 1024)
				return -1;
			if(ie->u.aal4.mid_high >= 1024)
				return -1;
			if(ie->u.aal4.mid_low > ie->u.aal4.mid_high)
				return -1;
		}
		if(ie->h.present & UNI_AAL4_SSCS_P) {
			switch(ie->u.aal4.sscs) {

			  default:
				return -1;

			  case UNI_AAL_SSCS_NULL:
			  case UNI_AAL_SSCS_SSCOPA:
			  case UNI_AAL_SSCS_SSCOPU:
			  case UNI_AAL_SSCS_FRAME:
				break;
			}
		}

	} else if(ie->type == UNI_AAL_5) {
		if(ie->h.present & UNI_AAL5_SSCS_P) {
			switch(ie->u.aal5.sscs) {

			  default:
				return -1;

			  case UNI_AAL_SSCS_NULL:
			  case UNI_AAL_SSCS_SSCOPA:
			  case UNI_AAL_SSCS_SSCOPU:
			  case UNI_AAL_SSCS_FRAME:
				break;
			}
		}

	} else if(ie->type == UNI_AAL_USER) {
		if(ie->u.aalu.len > 4)
			return -1;

	} else
		return -1;

	return 0;
}

DEF_IE_ENCODE(itu, aal)
{
	START_IE(aal, UNI_IE_AAL, 16);

	APP_BYTE(msg, ie->type);
	switch(ie->type) {

	  case UNI_AAL_0:
		break;

	  case UNI_AAL_1:
		APP_SUB_BYTE(msg,
			UNI_AAL_SUB_ID, ie->u.aal1.subtype);
		APP_SUB_BYTE(msg,
			UNI_AAL_CBR_ID, ie->u.aal1.cbr_rate);
		APP_OPT_16BIT(msg, ie->h.present, UNI_AAL1_MULT_P,
			UNI_AAL_MULT_ID, ie->u.aal1.mult);
		APP_OPT_BYTE(msg, ie->h.present, UNI_AAL1_SCREC_P,
			UNI_AAL_SCREC_ID, ie->u.aal1.screc);
		APP_OPT_BYTE(msg, ie->h.present, UNI_AAL1_ECM_P,
			UNI_AAL_ECM_ID, ie->u.aal1.ecm);
		APP_OPT_16BIT(msg, ie->h.present, UNI_AAL1_BSIZE_P,
			UNI_AAL_BSIZE_ID, ie->u.aal1.bsize);
		APP_OPT_BYTE(msg, ie->h.present, UNI_AAL1_PART_P,
			UNI_AAL_PART_ID, ie->u.aal1.part);
		break;

	  case UNI_AAL_2:
		break;

	  case UNI_AAL_4:
		if(ie->h.present & UNI_AAL4_CPCS_P) {
			APP_SUB_16BIT(msg,
				UNI_AAL_FWDCPCS_ID, ie->u.aal4.fwd_cpcs);
			APP_SUB_16BIT(msg,
				UNI_AAL_BWDCPCS_ID, ie->u.aal4.bwd_cpcs);
		}
		if(ie->h.present & UNI_AAL4_MID_P) {
			APP_BYTE(msg, UNI_AAL_MID_ID);
			APP_16BIT(msg, ie->u.aal4.mid_low);
			APP_16BIT(msg, ie->u.aal4.mid_high);
		}
		APP_OPT_BYTE(msg, ie->h.present, UNI_AAL4_SSCS_P,
			UNI_AAL_SSCS_ID, ie->u.aal4.sscs);
		break;

	  case UNI_AAL_5:
		if(ie->h.present & UNI_AAL5_CPCS_P) {
			APP_SUB_16BIT(msg,
				UNI_AAL_FWDCPCS_ID, ie->u.aal5.fwd_cpcs);
			APP_SUB_16BIT(msg,
				UNI_AAL_BWDCPCS_ID, ie->u.aal5.bwd_cpcs);
		}
		APP_OPT_BYTE(msg, ie->h.present, UNI_AAL5_SSCS_P,
			UNI_AAL_SSCS_ID, ie->u.aal5.sscs);
		break;

	  case UNI_AAL_USER:
		APP_BUF(msg, ie->u.aalu.user, ie->u.aalu.len);
		break;

	  default:
		return -1;
	}

	SET_IE_LEN(msg);
	return 0;
}

/*
 * XXX What should we do with multiple subtype occurences? Ignore
 * or reject. Currently we reject.
 */
static int
decode_aal_1(struct uni_ie_aal *ie, struct uni_msg *msg, u_int ielen)
{
	int subtype_p, cbr_p;

	subtype_p = cbr_p = 0;

	while(ielen-- > 0) {
		switch(*msg->b_rptr++) {

		  case UNI_AAL_SUB_ID:
			if(ielen == 0 || subtype_p)
				return -1;
			ielen--;
			subtype_p = 1;
			ie->u.aal1.subtype = *msg->b_rptr++;
			break;

		  case UNI_AAL_CBR_ID:
			if(ielen == 0 || cbr_p)
				return -1;
			ielen--;
			cbr_p = 1;
			ie->u.aal1.cbr_rate = *msg->b_rptr++;
			break;

		  case UNI_AAL_MULT_ID:
			if(ielen < 2 || (ie->h.present & UNI_AAL1_MULT_P))
				return -1;
			ielen -= 2;
			ie->h.present |= UNI_AAL1_MULT_P;
			ie->u.aal1.mult  = *msg->b_rptr++ << 8;
			ie->u.aal1.mult |= *msg->b_rptr++;
			break;
			
		  case UNI_AAL_SCREC_ID:
			if(ielen == 0 || (ie->h.present & UNI_AAL1_SCREC_P))
				return -1;
			ielen--;
			ie->h.present |= UNI_AAL1_SCREC_P;
			ie->u.aal1.screc = *msg->b_rptr++;
			break;

		  case UNI_AAL_ECM_ID:
			if(ielen == 0 || (ie->h.present & UNI_AAL1_ECM_P))
				return -1;
			ielen--;
			ie->h.present |= UNI_AAL1_ECM_P;
			ie->u.aal1.ecm = *msg->b_rptr++;
			break;

		  case UNI_AAL_BSIZE_ID:
			if(ielen < 2 || (ie->h.present & UNI_AAL1_BSIZE_P))
				return -1;
			ielen -= 2;
			ie->h.present |= UNI_AAL1_BSIZE_P;
			ie->u.aal1.bsize  = *msg->b_rptr++ << 8;
			ie->u.aal1.bsize |= *msg->b_rptr++;
			break;

		  case UNI_AAL_PART_ID:
			if(ielen == 0 || (ie->h.present & UNI_AAL1_PART_P))
				return -1;
			ielen--;
			ie->h.present |= UNI_AAL1_PART_P;
			ie->u.aal1.part = *msg->b_rptr++;
			break;

		  default:
			return -1;
		}
	}
	if(!subtype_p || !cbr_p)
		return -1;

	return 0;
}

static int
decode_aal_4(struct uni_ie_aal *ie, struct uni_msg *msg, u_int ielen)
{
	int fcpcs_p, bcpcs_p;

	fcpcs_p = bcpcs_p = 0;

	while(ielen-- > 0) {
		switch(*msg->b_rptr++) {

		  case UNI_AAL_FWDCPCS_ID:
			if(ielen < 2 || fcpcs_p)
				return -1;
			ielen -= 2;
			fcpcs_p = 1;
			ie->u.aal4.fwd_cpcs  = *msg->b_rptr++ << 8;
			ie->u.aal4.fwd_cpcs |= *msg->b_rptr++;
			break;

		  case UNI_AAL_BWDCPCS_ID:
			if(ielen < 2 || bcpcs_p)
				return -1;
			ielen -= 2;
			bcpcs_p = 1;
			ie->u.aal4.bwd_cpcs  = *msg->b_rptr++ << 8;
			ie->u.aal4.bwd_cpcs |= *msg->b_rptr++;
			break;

		  case UNI_AAL_MID_ID:
			if(ielen < 4 || (ie->h.present & UNI_AAL4_MID_P))
				return -1;
			ielen -= 4;
			ie->h.present |= UNI_AAL4_MID_P;
			ie->u.aal4.mid_low  = *msg->b_rptr++ << 8;
			ie->u.aal4.mid_low |= *msg->b_rptr++;
			ie->u.aal4.mid_high  = *msg->b_rptr++ << 8;
			ie->u.aal4.mid_high |= *msg->b_rptr++;
			break;

		  case UNI_AAL_SSCS_ID:
			if(ielen == 0 || (ie->h.present & UNI_AAL4_SSCS_P))
				return -1;
			ielen--;
			ie->h.present |= UNI_AAL4_SSCS_P;
			ie->u.aal4.sscs = *msg->b_rptr++;
			break;

		  default:
			return -1;
		}
	}

	if(fcpcs_p ^ bcpcs_p)
		return -1;
	if(fcpcs_p)
		ie->h.present |= UNI_AAL4_CPCS_P;

	return 0;
}

static int
decode_aal_5(struct uni_ie_aal *ie, struct uni_msg *msg, u_int ielen)
{
	int fcpcs_p, bcpcs_p;

	fcpcs_p = bcpcs_p = 0;

	while(ielen-- > 0) {
		switch(*msg->b_rptr++) {

		  case UNI_AAL_FWDCPCS_ID:
			if(ielen < 2 || fcpcs_p)
				return -1;
			ielen -= 2;
			fcpcs_p = 1;
			ie->u.aal5.fwd_cpcs  = *msg->b_rptr++ << 8;
			ie->u.aal5.fwd_cpcs |= *msg->b_rptr++;
			break;

		  case UNI_AAL_BWDCPCS_ID:
			if(ielen < 2 || bcpcs_p)
				return -1;
			ielen -= 2;
			bcpcs_p = 1;
			ie->u.aal5.bwd_cpcs  = *msg->b_rptr++ << 8;
			ie->u.aal5.bwd_cpcs |= *msg->b_rptr++;
			break;

		  case UNI_AAL_SSCS_ID:
			if(ielen == 0 || (ie->h.present & UNI_AAL5_SSCS_P))
				return -1;
			ielen--;
			ie->h.present |= UNI_AAL5_SSCS_P;
			ie->u.aal5.sscs = *msg->b_rptr++;
			break;

		  default:
			return -1;
		}
	}

	if(fcpcs_p ^ bcpcs_p)
		return -1;
	if(fcpcs_p)
		ie->h.present |= UNI_AAL5_CPCS_P;

	return 0;
}

static int
decode_aal_user(struct uni_ie_aal *ie, struct uni_msg *msg, u_int ielen)
{
	if(ielen > 4)
		return -1;

	ie->u.aalu.len = 0;
	while(ielen--)
		ie->u.aalu.user[ie->u.aalu.len++] = *msg->b_rptr++;

	return 0;
}

DEF_IE_DECODE(itu, aal)
{
	u_char c;

	IE_START(DISC_ACC_ERR(AAL));

	if(ielen < 1 || ielen > 21)
		goto rej;

	c = *msg->b_rptr++;
	ielen--;

	switch(c) {

	  case UNI_AAL_0:
		ie->type = c;
		break;

	  case UNI_AAL_1:
		ie->type = c;
		if(decode_aal_1(ie, msg, ielen))
			goto rej;
		break;

	  case UNI_AAL_2:
		ie->type = c;
		break;

	  case UNI_AAL_4:
		ie->type = c;
		if(decode_aal_4(ie, msg, ielen))
			goto rej;
		break;

	  case UNI_AAL_5:
		ie->type = c;
		if(decode_aal_5(ie, msg, ielen))
			goto rej;
		break;

	  case UNI_AAL_USER:
		ie->type = c;
		if(decode_aal_user(ie, msg, ielen))
			goto rej;
		break;

	  default:
		goto rej;
	}

	IE_END(AAL);
}

/*********************************************************************
 *
 * Traffic descriptor.
 * Alternate traffic descriptor.
 * Minimum traffic descriptor.
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 49...51
 *  Q.2961
 *  Q.2962
 *  UNI4.0 pp. 9...10, 106...109
 *
 * The Q.s specify the coding. UNI4.0 adds frame discard and best-effort.
 * Appendix in UNI4.0 lists the allowed combinations.
 *
 *		  PCR0 PCR1 SCR/MBS0 SCR/MBS1 BE TAG FDISC ABR
 *  1	CBR.1 	  -    Y    -        -        -  N   Y/N   -
 *  2	CBR.2 	  -    Y    -        -        -  N   Y/N   -    (*)
 *  3	CBR.3 	  Y    Y    -        -        -  Y   Y/N   -    (*)
 *  4	rt-VBR.1  -    Y    -        Y        -  N   Y/N   -
 *  5	rt-VBR.2  -    Y    Y        -        -  N   Y/N   -
 *  6	rt-VBR.3  -    Y    Y        -        -  Y   Y/N   -
 *  7	rt-VBR.4  Y    Y    -        -        -  Y/N Y/N   -    (*)
 *  8	rt-VBR.5  -    Y    -        -        -  N   Y/N   -    (*)
 *  9	rt-VBR.6  -    Y    -        Y        -  N   Y/N   -    (*)
 * 10	nrt-VBR.1 -    Y    -        Y        -  N   Y/N   -
 * 11	nrt-VBR.2 -    Y    Y        -        -  N   Y/N   -
 * 12	nrt-VBR.3 -    Y    Y        -        -  Y   Y/N   -
 * 13	nrt-VBR.4 Y    Y    -        -        -  Y/N Y/N   -	(*)
 * 14	nrt-VBR.5 -    Y    -        -        -  N   Y/N   -	(*)
 * 15	nrt-VBR.6 -    Y    -        Y        -  N   Y/N   -	(*)
 * 16	ABR	  -    Y    -        -        -  N   Y/N   O	(*)
 * 17	UBR.1	  -    Y    -        -        Y  N   Y/N   -
 * 18	UBR.2	  -    Y    -        -        Y  Y   Y/N   -
 *
 * Allow ITU-T and NET coding, because its not clear, whether the
 * new fields in UNI4.0 should be used with NET coding or not.
 * Does not allow for experimental codings yet.
 */

static void
print_ie_traffic_common(struct unicx *cx, u_int present, struct uni_xtraffic *ie)
{
	uni_print_entry(cx, "fwd", "(");
	if(present & UNI_TRAFFIC_FPCR0_P)
		uni_printf(cx, "%u", ie->fpcr0);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_FPCR1_P)
		uni_printf(cx, "%u", ie->fpcr1);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_FSCR0_P)
		uni_printf(cx, "%u", ie->fscr0);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_FSCR1_P)
		uni_printf(cx, "%u", ie->fscr1);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_FMBS0_P)
		uni_printf(cx, "%u", ie->fmbs0);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_FMBS1_P)
		uni_printf(cx, "%u", ie->fmbs1);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_FABR1_P)
		uni_printf(cx, "%u", ie->fabr1);
	uni_printf(cx, ")");

	uni_print_entry(cx, "bwd", "(");
	if(present & UNI_TRAFFIC_BPCR0_P)
		uni_printf(cx, "%u", ie->bpcr0);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_BPCR1_P)
		uni_printf(cx, "%u", ie->bpcr1);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_BSCR0_P)
		uni_printf(cx, "%u", ie->bscr0);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_BSCR1_P)
		uni_printf(cx, "%u", ie->bscr1);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_BMBS0_P)
		uni_printf(cx, "%u", ie->bmbs0);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_BMBS1_P)
		uni_printf(cx, "%u", ie->bmbs1);
	uni_putc(',', cx);
	if(present & UNI_TRAFFIC_BABR1_P)
		uni_printf(cx, "%u", ie->babr1);
	uni_printf(cx, ")");

	if(present & UNI_TRAFFIC_BEST_P)
		uni_print_flag("best_effort", cx);
	if(present & UNI_TRAFFIC_MOPT_P) {
		uni_print_entry(cx, "tag", "(");
		if(ie->ftag)
			uni_printf(cx, "fwd");
		uni_putc(',', cx);
		if(ie->btag)
			uni_printf(cx, "bwd");
		uni_putc(')', cx);

		uni_print_entry(cx, "disc", "(");
		if(ie->fdisc)
			uni_printf(cx, "fwd");
		uni_putc(',', cx);
		if(ie->bdisc)
			uni_printf(cx, "bwd");
		uni_putc(')', cx);
	}
}

struct tallow {
	u_int	mask;
	int	mopt_flag;
	u_char	mopt_mask, mopt_val;
};

static int
check_traffic(u_int mask, u_int mopt, struct tallow *a)
{
	if(mask != a->mask)
		return 0;

	if(a->mopt_flag == 0) {
		/* not allowed */
		if(mopt == 0xffff)
			return 1;
		return 0;
	}

	if(a->mopt_flag < 0) {
		/* optional */
		if(mopt == 0xffff)
			return 1;
		if((mopt & a->mopt_mask) == a->mopt_val)
			return 1;
		return 0;
	}
	
	/* required */
	if(mopt == 0xffff)
		return 0;
	if((mopt & a->mopt_mask) == a->mopt_val)
		return 1;
	return 0;
}

static int
check_ie_traffic_common(struct uni_xtraffic *ie, u_int present,
    struct unicx *cx __unused)
{
	static u_int fmask =
		UNI_TRAFFIC_FPCR0_P | UNI_TRAFFIC_FPCR1_P |
		UNI_TRAFFIC_FSCR0_P | UNI_TRAFFIC_FSCR1_P |
		UNI_TRAFFIC_FMBS0_P | UNI_TRAFFIC_FMBS1_P |
		UNI_TRAFFIC_FABR1_P;
	static u_int bmask =
		UNI_TRAFFIC_BPCR0_P | UNI_TRAFFIC_BPCR1_P |
		UNI_TRAFFIC_BSCR0_P | UNI_TRAFFIC_BSCR1_P |
		UNI_TRAFFIC_BMBS0_P | UNI_TRAFFIC_BMBS1_P |
		UNI_TRAFFIC_BABR1_P;
#define DTAB(U,X)							\
	{ U##X##PCR1_P,							\
	  -1, U##X##TAG,	0 },		/* 1, 2, 8, 14 */	\
	{ U##X##PCR0_P | U##X##PCR1_P,					\
	  +1, U##X##TAG,	U##X##TAG },	/* 3 */			\
	{ U##X##PCR1_P | U##X##SCR1_P | U##X##MBS1_P,			\
	  -1, U##X##TAG,	0 },		/* 4, 9, 10, 15 */	\
	{ U##X##PCR1_P | U##X##SCR0_P | U##X##MBS0_P,			\
	  -1, 0,		0 },		/* 5, 6, 11, 12 */	\
	{ U##X##PCR0_P | U##X##PCR1_P,					\
	  -1, 0,		0 },		/* 7, 13 */		\
	{ U##X##PCR1_P | U##X##ABR1_P,					\
	  -1, U##X##TAG,	0 },		/* 16a */
#define DTABSIZE 6

	static struct tallow allow[2][DTABSIZE] = {
		{ DTAB(UNI_TRAFFIC_, F) },
	  	{ DTAB(UNI_TRAFFIC_, B) },
	};
#undef DTAB

	u_int f, b, p, m;
	int i;

	f = present & fmask;
	b = present & bmask;
	p = present & (fmask | bmask);
	m = (present & UNI_TRAFFIC_MOPT_P)
		? (  (ie->ftag ? UNI_TRAFFIC_FTAG : 0)
		   | (ie->btag ? UNI_TRAFFIC_BTAG : 0)
		   | (ie->fdisc ? UNI_TRAFFIC_FDISC : 0)
		   | (ie->bdisc ? UNI_TRAFFIC_BDISC : 0))
		: 0xffff;
	

	if(present & UNI_TRAFFIC_BEST_P) {
		/*
		 * Lines 17 and 18
		 */
		if(p != (UNI_TRAFFIC_FPCR1_P | UNI_TRAFFIC_BPCR1_P))
			return -1;
		return 0;
	}

	/*
	 * Check forward and backward independent. There must be a higher
	 * level checking in the CAC
	 */
	for(i = 0; i < DTABSIZE; i++)
		if(check_traffic(f, m, &allow[0][i]))
			break;
	if(i == DTABSIZE)
		return -1;

	for(i = 0; i < DTABSIZE; i++)
		if(check_traffic(b, m, &allow[1][i]))
			break;
	if(i == DTABSIZE)
		return -1;

	return 0;
}

static int
encode_traffic_common(struct uni_msg *msg, struct uni_xtraffic *ie,
    u_int present, struct unicx *cx __unused)
{
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_FPCR0_P,
		UNI_TRAFFIC_FPCR0_ID, ie->fpcr0);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_BPCR0_P,
		UNI_TRAFFIC_BPCR0_ID, ie->bpcr0);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_FPCR1_P,
		UNI_TRAFFIC_FPCR1_ID, ie->fpcr1);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_BPCR1_P,
		UNI_TRAFFIC_BPCR1_ID, ie->bpcr1);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_FSCR0_P,
		UNI_TRAFFIC_FSCR0_ID, ie->fscr0);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_BSCR0_P,
		UNI_TRAFFIC_BSCR0_ID, ie->bscr0);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_FSCR1_P,
		UNI_TRAFFIC_FSCR1_ID, ie->fscr1);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_BSCR1_P,
		UNI_TRAFFIC_BSCR1_ID, ie->bscr1);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_FMBS0_P,
		UNI_TRAFFIC_FMBS0_ID, ie->fmbs0);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_BMBS0_P,
		UNI_TRAFFIC_BMBS0_ID, ie->bmbs0);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_FMBS1_P,
		UNI_TRAFFIC_FMBS1_ID, ie->fmbs1);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_BMBS1_P,
		UNI_TRAFFIC_BMBS1_ID, ie->bmbs1);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_FABR1_P,
		UNI_TRAFFIC_FABR1_ID, ie->fabr1);
	APP_OPT_24BIT(msg, present, UNI_TRAFFIC_BABR1_P,
		UNI_TRAFFIC_BABR1_ID, ie->babr1);

	APP_OPT(msg, present, UNI_TRAFFIC_BEST_P,
		UNI_TRAFFIC_BEST_ID);
	APP_OPT_BYTE(msg, present, UNI_TRAFFIC_MOPT_P,
		UNI_TRAFFIC_MOPT_ID,
		(ie->ftag ? UNI_TRAFFIC_FTAG : 0) |
		(ie->btag ? UNI_TRAFFIC_BTAG : 0) |
		(ie->fdisc ? UNI_TRAFFIC_FDISC : 0) |
		(ie->fdisc ? UNI_TRAFFIC_BDISC : 0));

	return 0;
}

static int
decode_traffic_common(struct uni_xtraffic *ie, struct uni_msg *msg,
    u_int ielen, u_int *present)
{
	u_char c;

	while(ielen--) {
		switch(c = *msg->b_rptr++) {

		  default:
		  rej:
			return -1;

		  DEC_GETF3(TRAFFIC_FPCR0, fpcr0, *present);
		  DEC_GETF3(TRAFFIC_BPCR0, bpcr0, *present);
		  DEC_GETF3(TRAFFIC_FPCR1, fpcr1, *present);
		  DEC_GETF3(TRAFFIC_BPCR1, bpcr1, *present);
		  DEC_GETF3(TRAFFIC_FSCR0, fscr0, *present);
		  DEC_GETF3(TRAFFIC_BSCR0, bscr0, *present);
		  DEC_GETF3(TRAFFIC_FSCR1, fscr1, *present);
		  DEC_GETF3(TRAFFIC_BSCR1, bscr1, *present);
		  DEC_GETF3(TRAFFIC_FMBS0, fmbs0, *present);
		  DEC_GETF3(TRAFFIC_BMBS0, bmbs0, *present);
		  DEC_GETF3(TRAFFIC_BMBS1, bmbs1, *present);
		  DEC_GETF3(TRAFFIC_FABR1, fabr1, *present);
		  DEC_GETF3(TRAFFIC_BABR1, babr1, *present);

		  case UNI_TRAFFIC_BEST_ID:
			*present |= UNI_TRAFFIC_BEST_P;
			break;

		  case UNI_TRAFFIC_MOPT_ID:
			if(ielen == 0)
				return -1;
			ielen--;
			if(!(*present & UNI_TRAFFIC_MOPT_P)) {
				*present |= UNI_TRAFFIC_MOPT_P;
				ie->ftag = (*msg->b_rptr&UNI_TRAFFIC_FTAG)?1:0;
				ie->btag = (*msg->b_rptr&UNI_TRAFFIC_BTAG)?1:0;
				ie->fdisc = (*msg->b_rptr&UNI_TRAFFIC_FDISC)?1:0;
				ie->bdisc = (*msg->b_rptr&UNI_TRAFFIC_BDISC)?1:0;
			} 
			msg->b_rptr++;
			break;
		}
	}
	return 0;
}


/*****************************************************************/

DEF_IE_PRINT(itu, traffic)
{
	if(uni_print_iehdr("traffic", &ie->h, cx))
		return;
	print_ie_traffic_common(cx, ie->h.present, &ie->t);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, traffic)
{
	return check_ie_traffic_common(&ie->t, ie->h.present, cx);
}

DEF_IE_ENCODE(itu, traffic)
{
	START_IE(traffic, UNI_IE_TRAFFIC, 26);
	encode_traffic_common(msg, &ie->t, ie->h.present, cx);
	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, traffic)
{
	IE_START(;);

	if(ielen > 30)
		goto rej;

	if(decode_traffic_common(&ie->t, msg, ielen, &ie->h.present))
		goto rej;

	IE_END(TRAFFIC);
}

/*****************************************************************/

DEF_IE_PRINT(itu, atraffic)
{
	if(uni_print_iehdr("atraffic", &ie->h, cx))
		return;
	print_ie_traffic_common(cx, ie->h.present, &ie->t);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, atraffic)
{
	return check_ie_traffic_common(&ie->t, ie->h.present, cx);
}

DEF_IE_ENCODE(itu, atraffic)
{
	START_IE(traffic, UNI_IE_ATRAFFIC, 26);
	encode_traffic_common(msg, &ie->t, ie->h.present, cx);
	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, atraffic)
{
	IE_START(;);

	if(ielen > 30)
		goto rej;

	if(decode_traffic_common(&ie->t, msg, ielen, &ie->h.present))
		goto rej;

	IE_END(ATRAFFIC);
}

/*****************************************************************/

DEF_IE_PRINT(itu, mintraffic)
{
	if(uni_print_iehdr("mintraffic", &ie->h, cx))
		return;

	uni_print_entry(cx, "pcr0", "(");
	if(ie->h.present & UNI_MINTRAFFIC_FPCR0_P)
		uni_printf(cx, "%u", ie->fpcr0);
	uni_putc(',', cx);
	if(ie->h.present & UNI_MINTRAFFIC_BPCR0_P)
		uni_printf(cx, "%u", ie->bpcr0);
	uni_putc(')', cx);

	uni_print_entry(cx, "pcr1", "(");
	if(ie->h.present & UNI_MINTRAFFIC_FPCR1_P)
		uni_printf(cx, "%u", ie->fpcr1);
	uni_putc(',', cx);
	if(ie->h.present & UNI_MINTRAFFIC_BPCR1_P)
		uni_printf(cx, "%u", ie->bpcr1);
	uni_putc(')', cx);

	uni_print_entry(cx, "abr1", "(");
	if(ie->h.present & UNI_MINTRAFFIC_FABR1_P)
		uni_printf(cx, "%u", ie->fabr1);
	uni_putc(',', cx);
	if(ie->h.present & UNI_MINTRAFFIC_BABR1_P)
		uni_printf(cx, "%u", ie->babr1);
	uni_printf(cx, ")");

	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, mintraffic)
{
	u_int abr;
	u_int xbr;
	UNUSED(cx);

	abr = ie->h.present & (UNI_MINTRAFFIC_FABR1_P|UNI_MINTRAFFIC_BABR1_P);
	xbr = ie->h.present & (UNI_MINTRAFFIC_FPCR0_P|UNI_MINTRAFFIC_BPCR0_P|
			       UNI_MINTRAFFIC_FPCR1_P|UNI_MINTRAFFIC_BPCR1_P);

	if(abr && xbr)
		return -1;

	return 0;
}

DEF_IE_ENCODE(itu, mintraffic)
{
	START_IE(mintraffic, UNI_IE_MINTRAFFIC, 16);

	APP_OPT_24BIT(msg, ie->h.present, UNI_MINTRAFFIC_FPCR0_P,
		UNI_TRAFFIC_FPCR0_ID, ie->fpcr0);
	APP_OPT_24BIT(msg, ie->h.present, UNI_MINTRAFFIC_BPCR0_P,
		UNI_TRAFFIC_BPCR0_ID, ie->bpcr0);
	APP_OPT_24BIT(msg, ie->h.present, UNI_MINTRAFFIC_FPCR1_P,
		UNI_TRAFFIC_FPCR1_ID, ie->fpcr1);
	APP_OPT_24BIT(msg, ie->h.present, UNI_MINTRAFFIC_BPCR1_P,
		UNI_TRAFFIC_BPCR1_ID, ie->bpcr1);
	APP_OPT_24BIT(msg, ie->h.present, UNI_MINTRAFFIC_FABR1_P,
		UNI_TRAFFIC_FABR1_ID, ie->fabr1);
	APP_OPT_24BIT(msg, ie->h.present, UNI_MINTRAFFIC_BABR1_P,
		UNI_TRAFFIC_BABR1_ID, ie->babr1);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, mintraffic)
{
	u_char c;

	IE_START(;);

	if(ielen > 20)
		goto rej;

	while(ielen--) {
		switch(c = *msg->b_rptr++) {

		  default:
			goto rej;

		  DEC_GETF3(MINTRAFFIC_FPCR0, fpcr0, ie->h.present);
		  DEC_GETF3(MINTRAFFIC_BPCR0, bpcr0, ie->h.present);
		  DEC_GETF3(MINTRAFFIC_FPCR1, fpcr1, ie->h.present);
		  DEC_GETF3(MINTRAFFIC_BPCR1, bpcr1, ie->h.present);
		  DEC_GETF3(MINTRAFFIC_FABR1, fabr1, ie->h.present);
		  DEC_GETF3(MINTRAFFIC_BABR1, babr1, ie->h.present);
		}
	}

	IE_END(MINTRAFFIC);
}

/*****************************************************************/

DEF_IE_PRINT(net, mdcr)
{
	static const struct uni_print_tbl origin_tbl[] = {
		MKT(UNI_MDCR_ORIGIN_USER,	user),
		MKT(UNI_MDCR_ORIGIN_NET,	net),
		EOT()
	};

	if(uni_print_iehdr("mdcr", &ie->h, cx))
		return;

	uni_print_tbl("origin", ie->origin, origin_tbl, cx);
	uni_print_entry(cx, "mdcr", "(");
	uni_printf(cx, "%u", ie->fmdcr);
	uni_putc(',', cx);
	uni_printf(cx, "%u", ie->bmdcr);
	uni_putc(')', cx);

	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, mdcr)
{
	UNUSED(cx);

	if ((ie->origin != UNI_MDCR_ORIGIN_USER &&
	    ie->origin != UNI_MDCR_ORIGIN_NET) ||
	    ie->fmdcr >= (1 << 24) || ie->bmdcr >= (1 << 24))
		return (-1);

	return (0);
}

DEF_IE_ENCODE(net, mdcr)
{
	START_IE(mdcr, UNI_IE_MDCR, 9);

	APP_BYTE(msg, ie->origin);
	APP_SUB_24BIT(msg, UNI_TRAFFIC_FMDCR_ID, ie->fmdcr);
	APP_SUB_24BIT(msg, UNI_TRAFFIC_BMDCR_ID, ie->bmdcr);

	SET_IE_LEN(msg);
	return (0);
}

DEF_IE_DECODE(net, mdcr)
{
	u_char c;
#define UNI_TRAFFIC_FMDCR_P 0x01
#define UNI_TRAFFIC_BMDCR_P 0x02
	u_int p = 0;

	IE_START(;);

	if(ielen != 9)
		goto rej;

	ie->origin = *msg->b_rptr++;
	ielen--;

	while(ielen--) {
		switch(c = *msg->b_rptr++) {

		  default:
			goto rej;

		  DEC_GETF3(TRAFFIC_FMDCR, fmdcr, p);
		  DEC_GETF3(TRAFFIC_BMDCR, bmdcr, p);
		}
	}
	if (p != (UNI_TRAFFIC_FMDCR_P | UNI_TRAFFIC_BMDCR_P))
		goto rej;

	IE_END(MDCR);
}

/*********************************************************************
 *
 * Connection identifier
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 69...70
 *  UNI4.0 pp. 15...16
 *  PNNI1.0 p. 198
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, connid)
{
	static const struct uni_print_tbl tbl[] = {
		MKT(UNI_CONNID_VCI,	exclusive),
		MKT(UNI_CONNID_ANYVCI,	any),
		MKT(UNI_CONNID_NOVCI,	no),
		EOT()
	};
	static const struct uni_print_tbl assoc_tbl[] = {
		MKT(UNI_CONNID_ASSOC,	associated),
		MKT(UNI_CONNID_NONASSOC,non-associated),
		EOT()
	};

	if(uni_print_iehdr("connid", &ie->h, cx))
		return;

	uni_print_tbl("mode", ie->assoc, assoc_tbl, cx);
	uni_print_entry(cx, "connid", "(%u,", ie->vpci);
	if(ie->type == UNI_CONNID_VCI)
		uni_printf(cx, "%u", ie->vci);
	else
		uni_print_tbl(NULL, ie->type, tbl, cx);
	uni_printf(cx, ")");

	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, connid)
{
	UNUSED(cx);
	switch(ie->type) {
	  default:
		return -1;
	  case UNI_CONNID_VCI:
	  case UNI_CONNID_ANYVCI:
	  case UNI_CONNID_NOVCI:
		break;
	}

#if 0
	/*
	 * This field must be checked by the application to fulfil
	 * Q.2931Amd4 27) 5.2.3 last sentence
	 */
	switch(ie->assoc) {

	  case UNI_CONNID_ASSOC:
		if(!cx->cx.pnni)
			return -1;
		break;

	  case UNI_CONNID_NONASSOC:
		break;

	  default:
		return -1;
	}
#endif
	return 0;
}

DEF_IE_ENCODE(itu, connid)
{
	START_IE(connid, UNI_IE_CONNID, 5);

	APP_BYTE(msg, 0x80 | (ie->assoc << 3) | ie->type);
	APP_BYTE(msg, ie->vpci >> 8);
	APP_BYTE(msg, ie->vpci >> 0);
	APP_BYTE(msg, ie->vci >> 8);
	APP_BYTE(msg, ie->vci >> 0);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, connid)
{
	u_char c;

	IE_START(;);

	if(ielen != 5)
		goto rej;

	c = *msg->b_rptr++;
	if((c & 0x80) == 0)
		goto rej;
	ie->assoc = (c >> 3) & 3;
	ie->type = c & 7;
	ie->vpci  = *msg->b_rptr++ << 8;
	ie->vpci |= *msg->b_rptr++;
	ie->vci  = *msg->b_rptr++ << 8;
	ie->vci |= *msg->b_rptr++;

	IE_END(CONNID);
}

/*********************************************************************
 *
 * Quality of Service
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 72
 *  UNI4.0 pp. 16...17
 */

static void
print_qos(struct unicx *cx, struct uni_ie_qos *ie)
{
	static const struct uni_print_tbl class_tbl[] = {
		MKT(UNI_QOS_CLASS0,	Class0),
		MKT(UNI_QOS_CLASS1,	Class1),
		MKT(UNI_QOS_CLASS2,	Class2),
		MKT(UNI_QOS_CLASS3,	Class3),
		MKT(UNI_QOS_CLASS4,	Class4),
		EOT()
	};

	if(uni_print_iehdr("qos", &ie->h, cx))
		return;

	uni_print_tbl("fwd", ie->fwd, class_tbl, cx);
	uni_print_tbl("bwd", ie->bwd, class_tbl, cx);

	uni_print_ieend(cx);
}

DEF_IE_PRINT(itu, qos)
{
	print_qos(cx, ie);
}
DEF_IE_PRINT(net, qos)
{
	print_qos(cx, ie);
}

DEF_IE_CHECK(itu, qos)
{
	UNUSED(cx);

	switch(ie->fwd) {
	  default:
		return -1;

	  case UNI_QOS_CLASS0:
		break;
	}
	switch(ie->bwd) {
	  default:
		return -1;

	  case UNI_QOS_CLASS0:
		break;
	}
	return 0;
}

DEF_IE_CHECK(net, qos)
{
	UNUSED(cx);

	switch(ie->fwd) {
	  default:
		return -1;

	  case UNI_QOS_CLASS1:
	  case UNI_QOS_CLASS2:
	  case UNI_QOS_CLASS3:
	  case UNI_QOS_CLASS4:
		break;
	}
	switch(ie->bwd) {
	  default:
		return -1;

	  case UNI_QOS_CLASS1:
	  case UNI_QOS_CLASS2:
	  case UNI_QOS_CLASS3:
	  case UNI_QOS_CLASS4:
		break;
	}
	return 0;
}

DEF_IE_ENCODE(itu, qos)
{
	START_IE(qos, UNI_IE_QOS, 2);

	APP_BYTE(msg, ie->fwd);
	APP_BYTE(msg, ie->bwd);

	SET_IE_LEN(msg);
	return 0;
}
DEF_IE_ENCODE(net, qos)
{
	START_IE(qos, UNI_IE_QOS, 2);

	APP_BYTE(msg, ie->fwd);
	APP_BYTE(msg, ie->bwd);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, qos)
{
	IE_START(;);

	if(ielen != 2)
		goto rej;

	ie->fwd = *msg->b_rptr++;
	ie->bwd = *msg->b_rptr++;

	IE_END(QOS);
}

DEF_IE_DECODE(net, qos)
{
	IE_START(;);

	if(ielen != 2)
		goto rej;

	ie->fwd = *msg->b_rptr++;
	ie->bwd = *msg->b_rptr++;

	IE_END(QOS);
}

/*********************************************************************
 *
 * Broadband Lower Layer Information
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 53...54
 *  UNI4.0 p.  12
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, bhli)
{
	static const struct uni_print_tbl type_tbl[] = {
		MKT(UNI_BHLI_ISO,	iso),
		MKT(UNI_BHLI_USER,	user),
		MKT(UNI_BHLI_VENDOR,	vendor),
		EOT()
	};
	u_int i;

	if(uni_print_iehdr("bhli", &ie->h, cx))
		return;

	uni_print_tbl("type", ie->type, type_tbl, cx);
	uni_print_entry(cx, "len", "%d", ie->len);
	uni_print_entry(cx, "info", "(");
	for(i = 0; i < ie->len; i++)
		uni_printf(cx, ",0x%02x", ie->info[i]);
	uni_printf(cx, ")");

	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, bhli)
{
	UNUSED(cx);

	switch(ie->type) {
	  default:
		return -1;

	  case UNI_BHLI_ISO:
	  case UNI_BHLI_USER:
	  case UNI_BHLI_VENDOR:
		break;
	}
	if(ie->len > 8)
		return -1;

	return 0;
}

DEF_IE_ENCODE(itu, bhli)
{
	START_IE(bhli, UNI_IE_BHLI, 9);

	APP_BYTE(msg, 0x80 | ie->type);
	APP_BUF(msg, ie->info, ie->len);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, bhli)
{
	u_char c;

	IE_START(;);

	if(ielen > 9)
		goto rej;

	c = *msg->b_rptr++;
	ielen--;

	if(!(c & 0x80))
		goto rej;
	ie->type = c & 0x7f;
	ie->len = ielen;
	(void)memcpy(ie->info, msg->b_rptr, ielen);
	msg->b_rptr += ielen;

	IE_END(BHLI);
}

/*********************************************************************
 *
 * Broadband bearer capabilities
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 51...52
 *  Q.2931 Amd 1
 *  UNI4.0 pp. 10...12, 106...109
 *
 * UNI4.0 changed the meaning of byte 5a. Instead of 3 bit traffic type and
 * 2 bit timing requirements there are now 7 bits ATM transfer capabilities.
 * However the old format is still supported: it should be recognized on
 * input, but never be generated on output. Mapping is left to the user of
 * UNI.
 *
 * Amd 1 not checked XXX.
 *
 * The Appendix in UNI4.0 lists all the supported combinations of various
 * traffic IE's. The check function implements part of it.
 *
 *			A		C		X		VP
 * 1	CBR.1		7		.		7		7
 * 2	CBR.2		-		.		4,5,6		5   (*)
 * 3	CBR.3		-		.		4,5,6		5   (*)
 * 4	rt-VBR.1	.		19		19		19
 * 5	rt-VBR.2	.		9		1,9		9
 * 6	rt-VBR.3	.		9		1,9		9
 * 7	rt-VBR.4	.		.		1,9		.   (*)
 * 8	rt-VBR.5	.		.		1,9		.   (*)
 * 9	rt-VBR.6	.		9		1,9		9   (*)
 * 10	nrt-VBR.1	.		11		11		11
 * 11	nrt-VBR.2	.		-		-,0,2,8,10	-,10
 * 12	nrt-VBR.3	.		-		-,0,2,8,10	-,10
 * 13	nrt-VBR.4	.		-		-,0,2,8,10	.   (*)
 * 14	nrt-VBR.5	.		-		-,0,2,8,10	.   (*)
 * 15	nrt-VBR.6	.		-		-,0,2,8,10	-,10(*)
 * 16	ABR		.		12		12		12
 * 17	UBR.1		.		-		-,0,2,8,10	-,10
 * 18	UBR.2		.		-		-,0,2,8,10	-,10
 *
 * (*) compatibility
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, bearer)
{
	static const struct uni_print_tbl bclass_tbl[] = {
		MKT(UNI_BEARER_A,	bcob-A),
		MKT(UNI_BEARER_C,	bcob-C),
		MKT(UNI_BEARER_X,	bcob-X),
		MKT(UNI_BEARER_TVP,	transparent-VP),
		EOT()
	};
	static const struct uni_print_tbl atc_tbl[] = {
		MKT(UNI_BEARER_ATC_CBR,		cbr),
		MKT(UNI_BEARER_ATC_CBR1,	cbr1),
		MKT(UNI_BEARER_ATC_VBR,		vbr),
		MKT(UNI_BEARER_ATC_VBR1,	vbr1),
		MKT(UNI_BEARER_ATC_NVBR,	nvbr),
		MKT(UNI_BEARER_ATC_NVBR1,	nvbr1),
		MKT(UNI_BEARER_ATC_ABR,		abr),

		MKT(UNI_BEARER_ATCX_0,		x0),
		MKT(UNI_BEARER_ATCX_1,		x1),
		MKT(UNI_BEARER_ATCX_2,		x2),
		MKT(UNI_BEARER_ATCX_4,		x4),
		MKT(UNI_BEARER_ATCX_6,		x6),
		MKT(UNI_BEARER_ATCX_8,		x8),
		EOT()
	};
	static const struct uni_print_tbl cfg_tbl[] = {
		MKT(UNI_BEARER_P2P,	p2p),
		MKT(UNI_BEARER_MP,	mp),
		EOT()
	};
	static const struct uni_print_tbl clip_tbl[] = {
		MKT(UNI_BEARER_NOCLIP,	no),
		MKT(UNI_BEARER_CLIP,	yes),
		EOT()
	};

	if(uni_print_iehdr("bearer", &ie->h, cx))
		return;

	uni_print_tbl("class", ie->bclass, bclass_tbl, cx);

	if(ie->h.present & UNI_BEARER_ATC_P) {
		uni_print_tbl("atc", ie->atc, atc_tbl, cx);
	}
	uni_print_tbl("clip", ie->clip, clip_tbl, cx);
	uni_print_tbl("cfg", ie->cfg, cfg_tbl, cx);

	uni_print_ieend(cx);
}

#define QTYPE(C,A)	((UNI_BEARER_##C << 8) | UNI_BEARER_ATC_##A)
#define QTYPEX(C,A)	((UNI_BEARER_##C << 8) | UNI_BEARER_ATCX_##A)
#define QTYPE0(C)	((UNI_BEARER_##C << 8) | (1 << 16))
DEF_IE_CHECK(itu, bearer)
{
	UNUSED(cx);

	switch((ie->bclass << 8) |
	       ((ie->h.present & UNI_BEARER_ATC_P) == 0
			? (1 << 16)
			: ie->atc)) {

	  default:
		return -1;

	  case QTYPE (A,   CBR1):	/* 1 */
	  case QTYPE (X,   CBR1):	/* 1 */
	  case QTYPE (TVP, CBR1):	/* 1 */

	  case QTYPE0(A):		/* 2,3 */
	  case QTYPEX(X,   4):		/* 2,3 */
	  case QTYPE (X,   CBR):	/* 2,3 */
	  case QTYPEX(X,   6):		/* 2,3 */
	  case QTYPE (TVP, CBR):	/* 2,3 */

	  case QTYPE (C,   VBR1):	/* 4 */
	  case QTYPE (X,   VBR1):	/* 4 */
	  case QTYPE (TVP, VBR1):	/* 4 */

	  case QTYPE (C,   VBR):	/* 5,6,9 */
	  case QTYPEX(X,   1):		/* 5,6,7,8,9 */
	  case QTYPE (X,   VBR):	/* 5,6,7,8,9 */
	  case QTYPE (TVP, VBR):	/* 5,6,9 */

	  case QTYPE (C,   NVBR1):	/* 10 */
	  case QTYPE (X,   NVBR1):	/* 10 */
	  case QTYPE (TVP, NVBR1):	/* 10 */

	  case QTYPE0(C):		/* 11,12,13,14,15,17,18 */
	  case QTYPE0(X):		/* 11,12,13,14,15,17,18 */
	  case QTYPEX(X,   0):		/* 11,12,13,14,15,17,18 */
	  case QTYPEX(X,   2):		/* 11,12,13,14,15,17,18 */
	  case QTYPEX(X,   8):		/* 11,12,13,14,15,17,18 */
	  case QTYPE (X,   NVBR):	/* 11,12,13,14,15,17,18 */
	  case QTYPE0(TVP):		/* 11,12,15,17,18 */
	  case QTYPE (TVP, NVBR):	/* 11,12,15,17,18 */

	  case QTYPE (C,   ABR):	/* 16 */
	  case QTYPE (X,   ABR):	/* 16 */
	  case QTYPE (TVP, ABR):	/* 16 */
		break;
	}

	switch(ie->clip) {
	  default:
		return -1;

	  case UNI_BEARER_NOCLIP:
	  case UNI_BEARER_CLIP:
		break;
	}
	switch(ie->cfg) {
	  default:
		return -1;

	  case UNI_BEARER_P2P:
	  case UNI_BEARER_MP:
		break;
	}

	return 0;
}
#undef QTYPE
#undef QTYPEX
#undef QTYPE0

DEF_IE_ENCODE(itu, bearer)
{
	START_IE(bearer, UNI_IE_BEARER, 3);

	APP_BYTE(msg, ie->bclass |
		((ie->h.present & UNI_BEARER_ATC_P) ? 0:0x80));
	APP_OPT(msg, ie->h.present, UNI_BEARER_ATC_P,
		0x80 | ie->atc);
	APP_BYTE(msg, 0x80 | (ie->clip << 5) | ie->cfg);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, bearer)
{
	u_char c;

	IE_START(;);

	if(ielen != 2 && ielen != 3)
		goto rej;

	c = *msg->b_rptr++;
	ielen--;
	ie->bclass = c & 0x1f;
	if(!(c & 0x80)) {
		c = *msg->b_rptr++;
		ielen--;
		ie->h.present |= UNI_BEARER_ATC_P;

		switch(c & 0x7f) {
		  /*
		   * Real legal values
		   */
		  case UNI_BEARER_ATC_CBR:
		  case UNI_BEARER_ATC_CBR1:
		  case UNI_BEARER_ATC_VBR:
		  case UNI_BEARER_ATC_VBR1:
		  case UNI_BEARER_ATC_NVBR:
		  case UNI_BEARER_ATC_NVBR1:
		  case UNI_BEARER_ATC_ABR:
			break;

		  /*
		   * Compat values
		   */
		  case UNI_BEARER_ATCX_0:
		  case UNI_BEARER_ATCX_1:
		  case UNI_BEARER_ATCX_2:
		  case UNI_BEARER_ATCX_4:
		  case UNI_BEARER_ATCX_6:
		  case UNI_BEARER_ATCX_8:
			break;

		  default:
			goto rej;
		}

		if(!(c & 0x80))
			goto rej;

		ie->atc = c & 0x7f;
	}
	if(ielen == 0)
		goto rej;
	c = *msg->b_rptr++;
	ielen--;
	if(!(c & 0x80))
		goto rej;
	ie->clip = (c >> 5) & 0x3;
	ie->cfg = c & 0x3;

	IE_END(BEARER);
}

/*********************************************************************
 *
 * Broadband Lower Layer Information
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 54...59
 *  UNI4.0 pp. 12...14
 *
 * UNI4.0 states, that layer 1 info is not supported.
 * We allow a layer 1 protocol identifier.
 *
 * UNI4.0 states, that the layer information subelements are NOT position
 * dependent. We allow them in any order on input, but generate always the
 * definit order on output.
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, blli)
{
	static const struct uni_print_tbl l2_tbl[] = {
		MKT(UNI_BLLI_L2_BASIC,		basic),
		MKT(UNI_BLLI_L2_Q921,		Q921),
		MKT(UNI_BLLI_L2_X25LL,		X25-LL),
		MKT(UNI_BLLI_L2_X25ML,		X25-ML),
		MKT(UNI_BLLI_L2_LABP,		LAPB),
		MKT(UNI_BLLI_L2_HDLC_ARM,	HDLC-ARM),
		MKT(UNI_BLLI_L2_HDLC_NRM,	HDLC-NRM),
		MKT(UNI_BLLI_L2_HDLC_ABM,	HDLC-ABM),
		MKT(UNI_BLLI_L2_LAN,		LAN),
		MKT(UNI_BLLI_L2_X75,		X75),
		MKT(UNI_BLLI_L2_Q922,		Q922),
		MKT(UNI_BLLI_L2_USER,		user),
		MKT(UNI_BLLI_L2_ISO7776,	ISO7776),
		EOT()
	};
	static const struct uni_print_tbl l2mode_tbl[] = {
		MKT(UNI_BLLI_L2NORM,		normal),
		MKT(UNI_BLLI_L2EXT,		extended),
		EOT()
	};
	static const struct uni_print_tbl l3_tbl[] = {
		MKT(UNI_BLLI_L3_X25,		X25),
		MKT(UNI_BLLI_L3_ISO8208,	ISO8208),
		MKT(UNI_BLLI_L3_X223,		X223),
		MKT(UNI_BLLI_L3_CLMP,		CLMP),
		MKT(UNI_BLLI_L3_T70,		T70),
		MKT(UNI_BLLI_L3_TR9577,		TR9577),
		MKT(UNI_BLLI_L3_USER,		user),
		MKT(UNI_BLLI_L3_H310,		H310),
		MKT(UNI_BLLI_L3_H321,		H321),
		EOT()
	};
	static const struct uni_print_tbl l3mode_tbl[] = {
		MKT(UNI_BLLI_L3NSEQ,		normal-seq),
		MKT(UNI_BLLI_L3ESEQ,		extended-seq),
		EOT()
	};
	static const struct uni_print_tbl l3psiz_tbl[] = {
		MKT(UNI_BLLI_L3_16,	16),
		MKT(UNI_BLLI_L3_32,	32),
		MKT(UNI_BLLI_L3_64,	64),
		MKT(UNI_BLLI_L3_128,	128),
		MKT(UNI_BLLI_L3_256,	256),
		MKT(UNI_BLLI_L3_512,	512),
		MKT(UNI_BLLI_L3_1024,	1024),
		MKT(UNI_BLLI_L3_2048,	2048),
		MKT(UNI_BLLI_L3_4096,	4096),
		EOT()
	};
	static const struct uni_print_tbl l3ttype_tbl[] = {
		MKT(UNI_BLLI_L3_TTYPE_RECV,	receive_only),
		MKT(UNI_BLLI_L3_TTYPE_SEND,	send_only),
		MKT(UNI_BLLI_L3_TTYPE_BOTH,	both),
		EOT()
	};
	static const struct uni_print_tbl l3mux_tbl[] = {
		MKT(UNI_BLLI_L3_MUX_NOMUX,	NOMUX),
		MKT(UNI_BLLI_L3_MUX_TS,		TS),
		MKT(UNI_BLLI_L3_MUX_TSFEC,	TSFEC),
		MKT(UNI_BLLI_L3_MUX_PS,		PS),
		MKT(UNI_BLLI_L3_MUX_PSFEC,	PSFEC),
		MKT(UNI_BLLI_L3_MUX_H221,	H221),
		EOT()
	};
	static const struct uni_print_tbl l3tcap_tbl[] = {
		MKT(UNI_BLLI_L3_TCAP_NOIND,	noind),
		MKT(UNI_BLLI_L3_TCAP_AAL1,	aal1),
		MKT(UNI_BLLI_L3_TCAP_AAL5,	aal5),
		MKT(UNI_BLLI_L3_TCAP_AAL15,	aal1&5),
		EOT()
	};

	if(uni_print_iehdr("blli", &ie->h, cx))
		return;

	if(ie->h.present & UNI_BLLI_L1_P) {
		uni_print_entry(cx, "l1", "%u", ie->l1);
		uni_print_eol(cx);
	}
	if(ie->h.present & UNI_BLLI_L2_P) {
		uni_print_tbl("l2", ie->l2, l2_tbl, cx);
		uni_print_push_prefix("l2", cx);
		cx->indent++;
		if(ie->h.present & UNI_BLLI_L2_USER_P)
			uni_print_entry(cx, "proto", "%u", ie->l2_user);
		if(ie->h.present & UNI_BLLI_L2_Q933_P) {
			uni_print_entry(cx, "q933", "%u", ie->l2_q933);
			uni_print_tbl("mode", ie->l2_mode, l2mode_tbl, cx);
		}
		if(ie->h.present & UNI_BLLI_L2_WSIZ_P)
			uni_print_entry(cx, "wsize", "%u", ie->l2_wsiz);
		uni_print_pop_prefix(cx);
		cx->indent--;
		uni_print_eol(cx);

	}
	if(ie->h.present & UNI_BLLI_L3_P) {
		uni_print_tbl("l3", ie->l3, l3_tbl, cx);
		uni_print_push_prefix("l3", cx);
		cx->indent++;
		if(ie->h.present & UNI_BLLI_L3_USER_P)
			uni_print_entry(cx, "proto", "%u", ie->l3_user);
		if(ie->h.present & UNI_BLLI_L3_MODE_P)
			uni_print_tbl("mode", ie->l3_mode, l3mode_tbl, cx);
		if(ie->h.present & UNI_BLLI_L3_PSIZ_P)
			uni_print_tbl("packet-size", ie->l3_psiz, l3psiz_tbl, cx);
		if(ie->h.present & UNI_BLLI_L3_WSIZ_P)
			uni_print_entry(cx, "window-size", "%u", ie->l3_wsiz);
		if(ie->h.present & UNI_BLLI_L3_TTYPE_P) {
			uni_print_tbl("ttype", ie->l3_ttype, l3ttype_tbl, cx);
			uni_print_tbl("tcap", ie->l3_tcap, l3tcap_tbl, cx);
		}
		if(ie->h.present & UNI_BLLI_L3_MUX_P) {
			uni_print_tbl("fmux", ie->l3_fmux, l3mux_tbl, cx);
			uni_print_tbl("bmux", ie->l3_bmux, l3mux_tbl, cx);
		}
		if(ie->h.present & UNI_BLLI_L3_IPI_P)
			uni_print_entry(cx, "ipi", "0x%02x", ie->l3_ipi);
		if(ie->h.present & UNI_BLLI_L3_SNAP_P)
			uni_print_entry(cx, "snap", "%06x.%04x", ie->oui, ie->pid);
		uni_print_pop_prefix(cx);
		cx->indent--;
		uni_print_eol(cx);
	}

	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, blli)
{
	UNUSED(cx);
/*
	if(ie->h.present & UNI_BLLI_L1_P)
		;
*/

	if(ie->h.present & UNI_BLLI_L2_P) {
		static u_int mask =
			UNI_BLLI_L2_Q933_P | UNI_BLLI_L2_WSIZ_P |
			UNI_BLLI_L2_USER_P;

		switch(ie->l2) {
		  default:
			return -1;

		  case UNI_BLLI_L2_BASIC:
		  case UNI_BLLI_L2_Q921:
		  case UNI_BLLI_L2_LABP:
		  case UNI_BLLI_L2_LAN:
		  case UNI_BLLI_L2_X75:
			if(ie->h.present & mask)
				return -1;
			break;

		  case UNI_BLLI_L2_X25LL:
		  case UNI_BLLI_L2_X25ML:
		  case UNI_BLLI_L2_HDLC_ARM:
		  case UNI_BLLI_L2_HDLC_NRM:
		  case UNI_BLLI_L2_HDLC_ABM:
		  case UNI_BLLI_L2_Q922:
		  case UNI_BLLI_L2_ISO7776:
			switch(ie->h.present & mask) {
			  default:
				return -1;

			  case 0:
			  case UNI_BLLI_L2_Q933_P:
			  case UNI_BLLI_L2_Q933_P | UNI_BLLI_L2_WSIZ_P:
				break;
			}
			break;

		  case UNI_BLLI_L2_USER:
			switch(ie->h.present & mask) {
			  default:
				return -1;

			  case 0:	/* XXX ? */
			  case UNI_BLLI_L2_USER_P:
				break;
			}
			break;
		}
		if(ie->h.present & UNI_BLLI_L2_Q933_P) {
			if(ie->l2_q933 != 0)
				return -1;

			switch(ie->l2_mode) {
			  default:
				return -1;

			  case UNI_BLLI_L2NORM:
			  case UNI_BLLI_L2EXT:
				break;
			}
		}
		if(ie->h.present & UNI_BLLI_L2_WSIZ_P) {
			if(ie->l2_wsiz == 0 || ie->l2_wsiz > 127)
				return -1;
		}
		if(ie->h.present & UNI_BLLI_L2_USER_P) {
			if(ie->l2_user > 127)
				return -1;
		}
	}
	if(ie->h.present & UNI_BLLI_L3_P) {
		static u_int mask =
			UNI_BLLI_L3_MODE_P | UNI_BLLI_L3_PSIZ_P |
			UNI_BLLI_L3_WSIZ_P | UNI_BLLI_L3_USER_P |
			UNI_BLLI_L3_IPI_P | UNI_BLLI_L3_SNAP_P |
			UNI_BLLI_L3_TTYPE_P | UNI_BLLI_L3_MUX_P;

		switch(ie->l3) {
		  default:
			return -1;

		  case UNI_BLLI_L3_X25:
		  case UNI_BLLI_L3_ISO8208:
		  case UNI_BLLI_L3_X223:
			switch(ie->h.present & mask) {
			  default:
				return -1;

			  case 0:
			  case UNI_BLLI_L3_MODE_P:
			  case UNI_BLLI_L3_MODE_P |
			       UNI_BLLI_L3_PSIZ_P:
			  case UNI_BLLI_L3_MODE_P |
			       UNI_BLLI_L3_PSIZ_P |
			       UNI_BLLI_L3_WSIZ_P:
				break;
			}
			break;

		  case UNI_BLLI_L3_CLMP:
		  case UNI_BLLI_L3_T70:
			if(ie->h.present & mask)
				return -1;
			break;

		  case UNI_BLLI_L3_TR9577:
			switch(ie->h.present & mask) {
			  default:
				return -1;

			  case 0:
			  case UNI_BLLI_L3_IPI_P:
			  case UNI_BLLI_L3_IPI_P | UNI_BLLI_L3_SNAP_P:
				break;
			}
			break;

		  case UNI_BLLI_L3_H310:
			switch(ie->h.present & mask) {
			  default:
				return -1;

			  case 0:
			  case UNI_BLLI_L3_TTYPE_P:
			  case UNI_BLLI_L3_TTYPE_P|UNI_BLLI_L3_MUX_P:
				break;
			}
			break;

		  case UNI_BLLI_L3_USER:
			switch(ie->h.present & mask) {
			  default:
				return -1;

			  case 0:	/* XXX ? */
			  case UNI_BLLI_L3_USER_P:
				break;
			}
			break;
		}
		if(ie->h.present & UNI_BLLI_L3_MODE_P) {
			switch(ie->l3_mode) {
			  default:
				return -1;

			  case UNI_BLLI_L3NSEQ:
			  case UNI_BLLI_L3ESEQ:
				break;
			}
		}
		if(ie->h.present & UNI_BLLI_L3_PSIZ_P) {
			switch(ie->l3_psiz) {
			  default:
				return -1;

			  case UNI_BLLI_L3_16:
			  case UNI_BLLI_L3_32:
			  case UNI_BLLI_L3_64:
			  case UNI_BLLI_L3_128:
			  case UNI_BLLI_L3_256:
			  case UNI_BLLI_L3_512:
			  case UNI_BLLI_L3_1024:
			  case UNI_BLLI_L3_2048:
			  case UNI_BLLI_L3_4096:
				break;
			}
		}
		if(ie->h.present & UNI_BLLI_L3_WSIZ_P) {
			if(ie->l3_wsiz == 0 || ie->l3_wsiz > 127)
				return -1;
		}
		if(ie->h.present & UNI_BLLI_L3_IPI_P) {
			if(ie->l3_ipi == UNI_BLLI_L3_SNAP) {
				if(!(ie->h.present & UNI_BLLI_L3_SNAP_P))
					return -1;
			} else {
				if(ie->h.present & UNI_BLLI_L3_SNAP_P)
					return -1;
			}
		}
		if(ie->h.present & UNI_BLLI_L3_USER_P) {
			if(ie->l3_user > 127)
				return -1;
		}
		if(ie->h.present & UNI_BLLI_L3_SNAP_P) {
			if(ie->oui >= (1<<24))
				return -1;
			if(ie->pid >= (1<<16))
				return -1;
		}
		if(ie->h.present & UNI_BLLI_L3_TTYPE_P) {
			switch(ie->l3_ttype) {
			  default:
				return -1;

			  case UNI_BLLI_L3_TTYPE_RECV:
			  case UNI_BLLI_L3_TTYPE_SEND:
			  case UNI_BLLI_L3_TTYPE_BOTH:
				break;
			}
			switch(ie->l3_tcap) {
			  default:
				return -1;

			  case UNI_BLLI_L3_TCAP_NOIND:
			  case UNI_BLLI_L3_TCAP_AAL1:
			  case UNI_BLLI_L3_TCAP_AAL5:
			  case UNI_BLLI_L3_TCAP_AAL15:
				break;
			}
		}
		if(ie->h.present & UNI_BLLI_L3_MUX_P) {
			switch(ie->l3_fmux) {
			  default:
				return -1;

			  case UNI_BLLI_L3_MUX_NOMUX:
			  case UNI_BLLI_L3_MUX_TS:
			  case UNI_BLLI_L3_MUX_TSFEC:
			  case UNI_BLLI_L3_MUX_PS:
			  case UNI_BLLI_L3_MUX_PSFEC:
			  case UNI_BLLI_L3_MUX_H221:
				break;
			}
			switch(ie->l3_bmux) {
			  default:
				return -1;

			  case UNI_BLLI_L3_MUX_NOMUX:
			  case UNI_BLLI_L3_MUX_TS:
			  case UNI_BLLI_L3_MUX_TSFEC:
			  case UNI_BLLI_L3_MUX_PS:
			  case UNI_BLLI_L3_MUX_PSFEC:
			  case UNI_BLLI_L3_MUX_H221:
				break;
			}
		}
	}

	return 0;
}

DEF_IE_ENCODE(itu, blli)
{
	START_IE(blli, UNI_IE_BLLI, 13);

	if (IE_ISERROR(*ie)) {
		APP_BYTE(msg, 0xff);
		APP_BYTE(msg, 0xff);
		goto out;
	}

	if(ie->h.present & UNI_BLLI_L1_P)
		APP_BYTE(msg, (UNI_BLLI_L1_ID<<5)|ie->l1|0x80);

	if(ie->h.present & UNI_BLLI_L2_P) {
		if(ie->h.present & UNI_BLLI_L2_Q933_P) {
			APP_BYTE(msg, (UNI_BLLI_L2_ID<<5)|ie->l2);
			if(ie->h.present & UNI_BLLI_L2_WSIZ_P) {
				APP_BYTE(msg, (ie->l2_mode<<5)|ie->l2_q933);
				APP_BYTE(msg, ie->l2_wsiz | 0x80);
			} else {
				APP_BYTE(msg, (ie->l2_mode<<5)|ie->l2_q933|0x80);
			}
		} else if(ie->h.present & UNI_BLLI_L2_USER_P) {
			APP_BYTE(msg, (UNI_BLLI_L2_ID<<5)|ie->l2);
			APP_BYTE(msg, ie->l2_user | 0x80);
		} else {
			APP_BYTE(msg, (UNI_BLLI_L2_ID << 5) | ie->l2 | 0x80);
		}
	}

	if(ie->h.present & UNI_BLLI_L3_P) {
		if(ie->h.present & UNI_BLLI_L3_MODE_P) {
			if(ie->h.present & UNI_BLLI_L3_PSIZ_P) {
				if(ie->h.present & UNI_BLLI_L3_WSIZ_P) {
					APP_BYTE(msg,(UNI_BLLI_L3_ID<<5)|ie->l3);
					APP_BYTE(msg,(ie->l3_mode<<5));
					APP_BYTE(msg,ie->l3_psiz);
					APP_BYTE(msg,ie->l3_wsiz|0x80);
				} else {
					APP_BYTE(msg,(UNI_BLLI_L3_ID<<5)|ie->l3);
					APP_BYTE(msg,(ie->l3_mode<<5));
					APP_BYTE(msg,(ie->l3_psiz|0x80));
				}
			} else {
				APP_BYTE(msg, (UNI_BLLI_L3_ID<<5)|ie->l3);
				APP_BYTE(msg, (ie->l3_mode<<5)|0x80);
			}
		} else if(ie->h.present & UNI_BLLI_L3_USER_P) {
			APP_BYTE(msg, (UNI_BLLI_L3_ID<<5)|ie->l3);
			APP_BYTE(msg,(ie->l3_user|0x80));
		} else if(ie->h.present & UNI_BLLI_L3_IPI_P) {
			APP_BYTE(msg, (UNI_BLLI_L3_ID<<5)|ie->l3);
			APP_BYTE(msg,((ie->l3_ipi>>1) & 0x7f));
			APP_BYTE(msg,(((ie->l3_ipi&1)<<6)|0x80));
			if(ie->h.present & UNI_BLLI_L3_SNAP_P) {
				APP_BYTE(msg, 0x80);
				APP_BYTE(msg, (ie->oui >> 16));
				APP_BYTE(msg, (ie->oui >>  8));
				APP_BYTE(msg, (ie->oui >>  0));
				APP_BYTE(msg, (ie->pid >>  8));
				APP_BYTE(msg, (ie->pid >>  0));
			}
		} else if(ie->h.present & UNI_BLLI_L3_TTYPE_P) {
			if(ie->h.present & UNI_BLLI_L3_MUX_P) {
				APP_BYTE(msg, ie->l3_ttype | (ie->l3_tcap << 4));
				APP_BYTE(msg, 0x80 | (ie->l3_fmux << 3) | ie->l3_bmux);
			} else {
				APP_BYTE(msg, 0x80 | ie->l3_ttype | (ie->l3_tcap << 4));
			}
		} else {
			APP_BYTE(msg, (UNI_BLLI_L3_ID<<5)|ie->l3|0x80);
		}
	}

  out:
	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, blli)
{
	u_char c;

	IE_START(;);

	if(ielen > 17)
		goto rej;

	while(ielen--) {
		switch(((c = *msg->b_rptr++) >> 5) & 0x3) {
		  default:
			goto rej;

		  case UNI_BLLI_L1_ID:
			ie->h.present |= UNI_BLLI_L1_P;
			ie->l1 = c & 0x1f;
			if(!(c & 0x80))
				goto rej;
			break;

		  case UNI_BLLI_L2_ID:
			ie->h.present |= UNI_BLLI_L2_P;
			ie->l2 = c & 0x1f;
			if(!(c & 0x80)) {
				if(ielen == 0)
					goto rej;
				ielen--;
				c = *msg->b_rptr++;
				if(ie->l2 == UNI_BLLI_L2_USER) {
					ie->h.present |= UNI_BLLI_L2_USER_P;
					ie->l2_user = c & 0x7f;
					if(!(c & 0x80))
						goto rej;
				} else {
					ie->h.present |= UNI_BLLI_L2_Q933_P;
					ie->l2_q933 = c & 0x3;
					ie->l2_mode = (c >> 5) & 0x3;
					if(!(c & 0x80)) {
						if(ielen == 0)
							goto rej;
						ielen--;
						c = *msg->b_rptr++;
						ie->h.present |= UNI_BLLI_L2_WSIZ_P;
						ie->l2_wsiz = c & 0x7f;
						if(!(c & 0x80))
							goto rej;
					}
				}
			}
			break;

		  case UNI_BLLI_L3_ID:
			ie->h.present |= UNI_BLLI_L3_P;
			ie->l3 = c & 0x1f;
			if(!(c & 0x80)) {
				switch(ie->l3) {
				  default:
				  case UNI_BLLI_L3_CLMP:
				  case UNI_BLLI_L3_T70:
					goto rej;

				  case UNI_BLLI_L3_X25:
				  case UNI_BLLI_L3_ISO8208:
				  case UNI_BLLI_L3_X223:
					if(ielen == 0)
						goto rej;
					ielen--;
					c = *msg->b_rptr++;
					ie->l3_mode = (c >> 5) & 0x3;
					ie->h.present |= UNI_BLLI_L3_MODE_P;

					if(c & 0x80)
						break;

					if(ielen == 0)
						goto rej;
					ielen--;
					c = *msg->b_rptr++;
					ie->l3_psiz = c & 0xf;
					ie->h.present |= UNI_BLLI_L3_PSIZ_P;

					if(c & 0x80)
						break;

					if(ielen == 0)
						goto rej;
					ielen--;
					c = *msg->b_rptr++;
					ie->l3_wsiz = c & 0x7f;
					ie->h.present |= UNI_BLLI_L3_WSIZ_P;

					if(!(c & 0x80))
						goto rej;
					break;

				  case UNI_BLLI_L3_TR9577:
					if(ielen < 2)
						goto rej;
					ielen -= 2;
					c = *msg->b_rptr++;
					ie->l3_ipi = (c << 1) & 0xfe;
					if(c & 0x80)
						goto rej;
					c = *msg->b_rptr++;
					ie->l3_ipi |= c & 1;
					if(!(c & 0x80))
						goto rej;
					ie->h.present |= UNI_BLLI_L3_IPI_P;

					if(ie->l3_ipi != UNI_BLLI_L3_SNAP)
						break;
					if(ielen < 6)
						goto rej;
					ielen -= 6;
					if(*msg->b_rptr++ != 0x80)
						goto rej;
					ie->h.present |= UNI_BLLI_L3_SNAP_P;
					ie->oui  = *msg->b_rptr++ << 16;
					ie->oui |= *msg->b_rptr++ << 8;
					ie->oui |= *msg->b_rptr++;
					ie->pid  = *msg->b_rptr++ << 8;
					ie->pid |= *msg->b_rptr++;
					break;

				  case UNI_BLLI_L3_H310:
					if(ielen == 0)
						goto rej;
					ielen--;
					c = *msg->b_rptr++;
					ie->l3_ttype = c & 0xf;
					ie->l3_tcap = (c >> 4) & 0x7;
					ie->h.present |= UNI_BLLI_L3_TTYPE_P;
					if(c & 0x80)
						break;
					if(ielen == 0)
						goto rej;
					ielen--;
					c = *msg->b_rptr++;
					ie->l3_fmux = (c >> 3) & 7;
					ie->l3_bmux = c & 7;
					ie->h.present |= UNI_BLLI_L3_MUX_P;
					if(!(c & 0x80))
						goto rej;
					break;

				  case UNI_BLLI_L3_USER:
					if(ielen == 0)
						goto rej;
					ielen--;
					c = *msg->b_rptr++;
					ie->l3_user = c & 0x7f;
					ie->h.present |= UNI_BLLI_L3_USER_P;
					if(!(c & 0x80))
						goto rej;
					break;
				}
			}
			break;
		}
	}

	IE_END(BLLI);
}

/*********************************************************************
 *
 * Broadband locking shift
 * Broadband non-locking shift.
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 41...42
 *  UNI4.0 pp. 9
 *
 * Procedure not supported in UNI4.0, but IE's must be recognized.
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, lshift)
{
	if(uni_print_iehdr("locking_shift", &ie->h, cx))
		return;
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, lshift)
{
	UNUSED(cx); UNUSED(ie);
	return -1;
}

DEF_IE_ENCODE(itu, lshift)
{
	START_IE(lshift, UNI_IE_LSHIFT, 1);
	APP_BYTE(msg, 0x80 | ie->set);
	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, lshift)
{
	u_char c;

	IE_START(;);

	if(ielen != 1)
		goto rej;

	c = *msg->b_rptr++;

	if(!(c & 0x80))
		goto rej;
	ie->set = c & 7;

	IE_END(LSHIFT);
}

/***********************************************************************/

DEF_IE_PRINT(itu, nlshift)
{
	if(uni_print_iehdr("nonlocking_shift", &ie->h, cx))
		return;
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, nlshift)
{
	UNUSED(cx); UNUSED(ie);
	return -1;
}

DEF_IE_ENCODE(itu, nlshift)
{
	START_IE(nlshift, UNI_IE_NLSHIFT, 1);
	APP_BYTE(msg, 0x80 | ie->set);
	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, nlshift)
{
	u_char c;

	IE_START(;);

	if(ielen != 1)
		goto rej;

	c = *msg->b_rptr++;

	if(!(c & 0x80))
		goto rej;
	ie->set = c & 7;

	IE_END(NLSHIFT);
}

/*********************************************************************
 *
 * Broadband Sending Complete Indicator
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 74-75
 *
 * Only ITU-T coding allowed.
 */
DEF_IE_PRINT(itu, scompl)
{
	if(uni_print_iehdr("sending_complete", &ie->h, cx))
		return;
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, scompl)
{
	UNUSED(ie); UNUSED(cx);
	return 0;
}

DEF_IE_ENCODE(itu, scompl)
{
	START_IE(scompl, UNI_IE_SCOMPL, 1);

	APP_BYTE(msg, 0x80 | 0x21);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, scompl)
{
	IE_START(;);

	if(ielen != 1)
  		goto rej;

	if(*msg->b_rptr++ != (0x80 | 0x21))
  		goto rej;

	IE_END(SCOMPL);
}

/*********************************************************************
 *
 * Broadband Repeat Indicator
 *
 * References for this IE are:
 *
 *  Q.2931 p.  73
 *  PNNI1.0 p. 196
 *
 * Q.2931 has table 4-19. Only codepoints 0x2 and 0xa (for PNNI) supported.
 *
 * Only ITU-T coding allowed.
 */
DEF_IE_PRINT(itu, repeat)
{
	static const struct uni_print_tbl tbl[] = {
		MKT(UNI_REPEAT_PRIDESC,	desc),
		MKT(UNI_REPEAT_STACK,   stack),
		EOT()
	};

	if(uni_print_iehdr("repeat", &ie->h, cx))
		return;
	uni_print_tbl("type", ie->type, tbl, cx);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, repeat)
{
	switch(ie->type) {

	  case UNI_REPEAT_PRIDESC:
		break;

	  case UNI_REPEAT_STACK:
		if(!cx->pnni)
			return -1;
		break;

	  default:
		return -1;
	}
	return 0;
}

DEF_IE_ENCODE(itu, repeat)
{
	START_IE(repeat, UNI_IE_REPEAT, 1);

	APP_BYTE(msg, 0x80 | ie->type);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, repeat)
{
	u_char c;

	IE_START(;);

	if(ielen != 1)
  		goto rej;

	c = *msg->b_rptr++;
	if(!(c & 0x80))
  		goto rej;
	ie->type = c & 0xf;

	IE_END(REPEAT);
}

/*********************************************************************
 *
 * Transit Network Selection
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 75...76
 *  UNI4.0 pp. 17
 *
 * According to UNI4.0 this is always National Network Id/Carried Id.
 *
 * ITU-T/Net coding allowed.
 */

DEF_IE_PRINT(itu, tns)
{
	u_int i;

	if(uni_print_iehdr("tns", &ie->h, cx))
		return;
	uni_print_entry(cx, "net", "%u,\"", ie->len);
	uni_putc('"', cx);
	for(i = 0; i < ie->len; i++) {
		if(ie->net[i] < ' ')
			uni_printf(cx, "^%c", ie->net[i] + '@');
		else if(ie->net[i] < '~')
			uni_putc(ie->net[i], cx);
		else
			uni_printf(cx, "\\%03o", ie->net[i]);
	}
	uni_putc('"', cx);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, tns)
{
	u_int i;

	UNUSED(cx);

	if(ie->len == 0 || ie->len > UNI_TNS_MAXLEN)
		return -1;
	for(i = 0; i < ie->len; i++)
		if(ie->net[i] < ' ' || ie->net[i] > '~')
			return -1;
	return 0;
}

DEF_IE_ENCODE(itu, tns)
{
	START_IE(tns, UNI_IE_TNS, ie->len + 1);

	APP_BYTE(msg, 0x80 | (0x2 << 4) | 0x1);
	APP_BUF(msg, ie->net, ie->len);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, tns)
{
	IE_START(;);

	if(ielen < 2 || ielen > 5)
		goto rej;

	if(*msg->b_rptr++ != (0x80 | (0x2 << 4) | 0x1))
		goto rej;
	ielen--;

	ie->len = 0;
	while(ielen--)
		ie->net[ie->len++] = *msg->b_rptr++;

	IE_END(TNS);
}

/*********************************************************************
 *
 * Restart indicator
 *
 * References for this IE are:
 *
 *  Q.2931 pp. 73...74
 *  UNI4.0 p.  17
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, restart)
{
	static const struct uni_print_tbl tbl[] = {
		MKT(UNI_RESTART_CHANNEL,	channel),
		MKT(UNI_RESTART_PATH,		path),
		MKT(UNI_RESTART_ALL,		all),
		EOT()
	};

	if(uni_print_iehdr("restart", &ie->h, cx))
		return;
	uni_print_tbl("class", ie->rclass, tbl, cx);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, restart)
{
	UNUSED(cx);

	switch(ie->rclass) {
	  default:
		return -1;

	  case UNI_RESTART_CHANNEL:
	  case UNI_RESTART_PATH:
	  case UNI_RESTART_ALL:
		break;
	}

	return 0;
}

DEF_IE_ENCODE(itu, restart)
{
	START_IE(restart, UNI_IE_RESTART, 1);

	APP_BYTE(msg, 0x80 | ie->rclass);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, restart)
{
	u_char c;

	IE_START(;);

	if(ielen != 1)
		goto rej;

	ie->rclass = (c = *msg->b_rptr++) & 0x7;

	if(!(c & 0x80))
		goto rej;

	IE_END(RESTART);
}

/*********************************************************************
 *
 * User-to-user info.
 *
 * References for this IE are:
 *
 *  Q.2957
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, uu)
{
	u_int i;

	if(uni_print_iehdr("uu", &ie->h, cx))
		return;
	uni_print_entry(cx, "len", "%u", ie->len);
	uni_print_entry(cx, "info", "(");
	for(i = 0; i < ie->len; i++)
		uni_printf(cx, "%s0x%02x", i == 0 ? "" : " ", ie->uu[i]);
	uni_printf(cx, ")");
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, uu)
{
	UNUSED(cx);

	if(ie->len > UNI_UU_MAXLEN)
		return -1;

	return 0;
}

DEF_IE_ENCODE(itu, uu)
{
	START_IE(uu, UNI_IE_UU, ie->len);

	APP_BUF(msg, ie->uu, ie->len);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, uu)
{
	IE_START(;);

	if(ielen > UNI_UU_MAXLEN || ielen < 1)
		goto rej;

	ie->len = ielen;
	ielen = 0;
	(void)memcpy(ie->uu, msg->b_rptr, ie->len);
	msg->b_rptr += ie->len;

	IE_END(UU);
}

/*********************************************************************
 *
 * Generic Identifier Transport
 *
 * References for this IE are:
 *
 *  UNI4.0 pp. 26...28
 *
 * UNI4.0 prescribes a fixed format for this IE. We have a flag in the
 * context structur, which tells us whether the check of this IE should be
 * hard or soft. Probably it should be hard for end systems and soft for
 * network nodes.
 *
 * Only Net Coding allowed. (XXX)
 */

DEF_IE_PRINT(net, git)
{
	static const struct uni_print_tbl std_tbl[] = {
		MKT(UNI_GIT_STD_DSMCC,	dsmcc),
		MKT(UNI_GIT_STD_H245,	H.245),
		EOT()
	};
	static const struct uni_print_tbl type_tbl[] = {
		MKT(UNI_GIT_TYPE_SESS,	sess),
		MKT(UNI_GIT_TYPE_RES,	res),
		EOT()
	};
	u_int i, j;
	char buf[20];

	if(uni_print_iehdr("git", &ie->h, cx))
		return;

	uni_print_tbl("std", ie->std, std_tbl, cx);

	uni_print_eol(cx);
	uni_print_push_prefix("id", cx);
	cx->indent++;
	for(i = 0; i < ie->numsub; i++) {
		sprintf(buf, "%u", i);
		uni_print_entry(cx, buf, "(");
		uni_print_tbl(NULL, ie->sub[i].type, type_tbl, cx);
		for(j = 0; j < ie->sub[i].len; j++)
			uni_printf(cx, ",0x%02x", ie->sub[i].val[j]);
		uni_printf(cx, ")");
		uni_print_eol(cx);
	}
	cx->indent--;
	uni_print_pop_prefix(cx);

	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, git)
{
	u_int i;

	if(cx->git_hard) {
		switch(ie->std) {
		  case UNI_GIT_STD_DSMCC:
		  case UNI_GIT_STD_H245:
			break;
		  default:
			return -1;
		}
		if(ie->numsub != 2)
			return -1;
		if(ie->sub[0].type != UNI_GIT_TYPE_SESS)
			return -1;
		if(ie->sub[0].len > UNI_GIT_MAXSESS)
			return -1;
		if(ie->sub[1].type != UNI_GIT_TYPE_RES)
			return -1;
		if(ie->sub[1].len > UNI_GIT_MAXRES)
			return -1;
	} else {
		if(ie->numsub > UNI_GIT_MAXSUB)
			return -1;
		for(i = 0; i < ie->numsub; i++)
			if(ie->sub[i].len > UNI_GIT_MAXVAL)
				return -1;
	}
	return 0;
}

DEF_IE_ENCODE(net, git)
{
	u_int i;

	START_IE(git, UNI_IE_GIT, 1 + ie->numsub * (1 + UNI_GIT_MAXVAL));

	APP_BYTE(msg, ie->std);
	for(i = 0; i < ie->numsub; i++) {
		APP_BYTE(msg, ie->sub[i].type);
		APP_BYTE(msg, ie->sub[i].len);
		APP_BUF(msg, ie->sub[i].val, ie->sub[i].len);
	}

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(net, git)
{
	IE_START(;);

	if(ielen > 1 + UNI_GIT_MAXSUB * (1 + UNI_GIT_MAXVAL) || ielen < 1)
		goto rej;

	ie->std = *msg->b_rptr++;
	ielen--;

	ie->numsub = 0;
	while(ielen > 0) {
		if(ie->numsub >= UNI_GIT_MAXSUB)
			goto rej;

		ie->sub[ie->numsub].type = *msg->b_rptr++;
		ielen--;

		if(ielen == 0)
			goto rej;
		ie->sub[ie->numsub].len = *msg->b_rptr++;
		ielen--;

		if(ie->sub[ie->numsub].len > UNI_GIT_MAXVAL)
			goto rej;
		if(ie->sub[ie->numsub].len > (u_int)ielen)
			goto rej;

		(void)memcpy(ie->sub[ie->numsub].val, msg->b_rptr, ie->sub[ie->numsub].len);
		ielen -= ie->sub[ie->numsub].len;
		msg->b_rptr += ie->sub[ie->numsub].len;

		ie->numsub++;
	}

	IE_END(GIT);
}

/*********************************************************************
 *
 * Additional ABR Parameters
 * ABR Setup parameters
 *
 * References for this IE are:
 *
 *  	UNI4.0 pp. 78...82
 *	PNNI1.0 p. 195
 *
 * Notes:
 *	Only NET coding.
 */

static void
print_abr_rec(struct unicx *cx, struct uni_abr_rec *rec)
{
	if(rec->present & UNI_ABR_REC_NRM_P)
		uni_print_entry(cx, "nrm", "%d", rec->nrm);
	if(rec->present & UNI_ABR_REC_TRM_P)
		uni_print_entry(cx, "trm", "%d", rec->trm);
	if(rec->present & UNI_ABR_REC_CDF_P)
		uni_print_entry(cx, "cdf", "%d", rec->cdf);
	if(rec->present & UNI_ABR_REC_ADTF_P)
		uni_print_entry(cx, "adtf", "%d", rec->adtf);
}

DEF_IE_PRINT(net, abradd)
{
	if(uni_print_iehdr("abradd", &ie->h, cx))
		return;

	uni_print_push_prefix("fwd", cx);
	print_abr_rec(cx, &ie->fwd);
	uni_print_pop_prefix(cx);

	uni_print_push_prefix("bwd", cx);
	print_abr_rec(cx, &ie->bwd);
	uni_print_pop_prefix(cx);

	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, abradd)
{
	UNUSED(cx);
	UNUSED(ie);

	return 0;
}

static u_int
encode_abr_rec(struct uni_abr_rec *rec)
{
	u_int ret = rec->present & 0xf000;

	if(ret & UNI_ABR_REC_NRM_P)
		ret |= (rec->nrm & 0x7) << 25;
	if(ret & UNI_ABR_REC_TRM_P)
		ret |= (rec->trm & 0x7) << 22;
	if(ret & UNI_ABR_REC_CDF_P)
		ret |= (rec->cdf & 0x7) << 19;
	if(ret & UNI_ABR_REC_ADTF_P)
		ret |= (rec->adtf & 0x3ff) << 9;

	return ret;
}

DEF_IE_ENCODE(net, abradd)
{
	START_IE(abradd, UNI_IE_ABRADD, 10);

	APP_SUB_32BIT(msg, UNI_ABRADD_FADD_ID, encode_abr_rec(&ie->fwd));
	APP_SUB_32BIT(msg, UNI_ABRADD_BADD_ID, encode_abr_rec(&ie->bwd));

	SET_IE_LEN(msg);
	return 0;
}

static int
decode_abr_rec(struct uni_msg *msg, struct uni_abr_rec *rec)
{
	u_int val;

	val  = *msg->b_rptr++ << 24;
	val |= *msg->b_rptr++ << 16;
	val |= *msg->b_rptr++ <<  8;
	val |= *msg->b_rptr++ <<  0;

	rec->present = val & 0xf000;

	rec->nrm  = (val & UNI_ABR_REC_NRM_P) ? ((val >> 25) & 0x7) : 0;
	rec->trm  = (val & UNI_ABR_REC_TRM_P) ? ((val >> 22) & 0x7) : 0;
	rec->cdf  = (val & UNI_ABR_REC_CDF_P) ? ((val >> 19) & 0x7) : 0;
	rec->adtf = (val & UNI_ABR_REC_ADTF_P)? ((val >>  9) & 0x3ff) : 0;

	return 0;
}

DEF_IE_DECODE(net, abradd)
{
	IE_START(;);

	if(ielen != 10)
		goto rej;


	while(ielen--) {
		switch(*msg->b_rptr++) {

		  default:
			goto rej;

		  case UNI_ABRADD_FADD_ID:
			if(decode_abr_rec(msg, &ie->fwd))
				goto rej;
			ielen -= 4;
			break;

		  case UNI_ABRADD_BADD_ID:
			if(decode_abr_rec(msg, &ie->bwd))
				goto rej;
			ielen -= 4;
			break;
		}
	}
	IE_END(ABRADD);
}

/*********************************************************************/

DEF_IE_PRINT(net, abrsetup)
{
	if(uni_print_iehdr("abrsetup", &ie->h, cx))
		return;

	uni_print_entry(cx, "rm_frt", "%d", ie->rmfrt);

	uni_print_push_prefix("fwd", cx);
	if(ie->h.present & UNI_ABRSETUP_FICR_P)
		uni_print_entry(cx, "icr", "%d", ie->ficr);
	if(ie->h.present & UNI_ABRSETUP_FTBE_P)
		uni_print_entry(cx, "tbe", "%d", ie->ftbe);
	if(ie->h.present & UNI_ABRSETUP_FRIF_P)
		uni_print_entry(cx, "rif", "%d", ie->frif);
	if(ie->h.present & UNI_ABRSETUP_FRDF_P)
		uni_print_entry(cx, "rdf", "%d", ie->frdf);
	uni_print_pop_prefix(cx);

	uni_print_push_prefix("bwd", cx);
	if(ie->h.present & UNI_ABRSETUP_BICR_P)
		uni_print_entry(cx, "icr", "%d", ie->bicr);
	if(ie->h.present & UNI_ABRSETUP_BTBE_P)
		uni_print_entry(cx, "tbe", "%d", ie->btbe);
	if(ie->h.present & UNI_ABRSETUP_BRIF_P)
		uni_print_entry(cx, "rif", "%d", ie->brif);
	if(ie->h.present & UNI_ABRSETUP_BRDF_P)
		uni_print_entry(cx, "rdf", "%d", ie->brdf);
	uni_print_pop_prefix(cx);

	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, abrsetup)
{
	if(cx->pnni) {
		if(!(ie->h.present & UNI_ABRSETUP_FICR_P))
			return -1;
		if(!(ie->h.present & UNI_ABRSETUP_BICR_P))
			return -1;
		if(!(ie->h.present & UNI_ABRSETUP_FTBE_P))
			return -1;
		if(!(ie->h.present & UNI_ABRSETUP_BTBE_P))
			return -1;
		if(!(ie->h.present & UNI_ABRSETUP_FRIF_P))
			return -1;
		if(!(ie->h.present & UNI_ABRSETUP_BRIF_P))
			return -1;
		if(!(ie->h.present & UNI_ABRSETUP_FRDF_P))
			return -1;
		if(!(ie->h.present & UNI_ABRSETUP_BRDF_P))
			return -1;
		if(!(ie->h.present & UNI_ABRSETUP_RMFRT_P))
			return -1;
	}

	if(!(ie->h.present & UNI_ABRSETUP_RMFRT_P))
		return -1;

	if(ie->h.present & UNI_ABRSETUP_FICR_P)
		if(ie->ficr >= 1 << 24)
			return -1;
	if(ie->h.present & UNI_ABRSETUP_BICR_P)
		if(ie->bicr >= 1 << 24)
			return -1;

	if(ie->h.present & UNI_ABRSETUP_FTBE_P)
		if(ie->ftbe >= 1 << 24 || ie->ftbe == 0)
			return -1;
	if(ie->h.present & UNI_ABRSETUP_BTBE_P)
		if(ie->btbe >= 1 << 24 || ie->btbe == 0)
			return -1;

	if(ie->rmfrt >= 1 << 24)
		return -1;

	if(ie->h.present & UNI_ABRSETUP_FRIF_P)
		if(ie->frif > 15)
			return -1;
	if(ie->h.present & UNI_ABRSETUP_FRDF_P)
		if(ie->frdf > 15)
			return -1;
	if(ie->h.present & UNI_ABRSETUP_BRIF_P)
		if(ie->brif > 15)
			return -1;
	if(ie->h.present & UNI_ABRSETUP_BRDF_P)
		if(ie->brdf > 15)
			return -1;
	return 0;
}

DEF_IE_ENCODE(net, abrsetup)
{
	START_IE(abrsetup, UNI_IE_ABRSETUP, 32);

	APP_OPT_24BIT(msg, ie->h.present, UNI_ABRSETUP_FICR_P,
		UNI_ABRSETUP_FICR_ID, ie->ficr);
	APP_OPT_24BIT(msg, ie->h.present, UNI_ABRSETUP_BICR_P,
		UNI_ABRSETUP_BICR_ID, ie->bicr);
	APP_OPT_24BIT(msg, ie->h.present, UNI_ABRSETUP_FTBE_P,
		UNI_ABRSETUP_FTBE_ID, ie->ftbe);
	APP_OPT_24BIT(msg, ie->h.present, UNI_ABRSETUP_BTBE_P,
		UNI_ABRSETUP_BTBE_ID, ie->btbe);
	APP_SUB_24BIT(msg, UNI_ABRSETUP_RMFRT_ID, ie->rmfrt);
	APP_OPT_BYTE(msg, ie->h.present, UNI_ABRSETUP_FRIF_P,
		UNI_ABRSETUP_FRIF_ID, ie->frif);
	APP_OPT_BYTE(msg, ie->h.present, UNI_ABRSETUP_BRIF_P,
		UNI_ABRSETUP_BRIF_ID, ie->brif);
	APP_OPT_BYTE(msg, ie->h.present, UNI_ABRSETUP_FRDF_P,
		UNI_ABRSETUP_FRDF_ID, ie->frdf);
	APP_OPT_BYTE(msg, ie->h.present, UNI_ABRSETUP_BRDF_P,
		UNI_ABRSETUP_BRDF_ID, ie->brdf);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(net, abrsetup)
{
	IE_START(;);

	if(ielen < 4 || ielen > 32)
		goto rej;


	while(ielen--) {
		switch(*msg->b_rptr++) {

		  default:
			goto rej;


		  DEC_GETF3(ABRSETUP_FICR, ficr, ie->h.present);
		  DEC_GETF3(ABRSETUP_BICR, bicr, ie->h.present);
		  DEC_GETF3(ABRSETUP_FTBE, ftbe, ie->h.present);
		  DEC_GETF3(ABRSETUP_BTBE, btbe, ie->h.present);
		  DEC_GETF1(ABRSETUP_FRIF, frif, ie->h.present);
		  DEC_GETF1(ABRSETUP_BRIF, brif, ie->h.present);
		  DEC_GETF1(ABRSETUP_FRDF, frdf, ie->h.present);
		  DEC_GETF1(ABRSETUP_BRDF, brdf, ie->h.present);
		  DEC_GETF3(ABRSETUP_RMFRT, frif, ie->h.present);
		}
	}
	IE_END(ABRSETUP);
}

/*********************************************************************
 *
 * Broadband report type
 *
 * References for this IE are:
 *
 *  Q.2963.1  pp. 7...8
 *
 * Only ITU-T coding allowed.
 */

DEF_IE_PRINT(itu, report)
{
	static const struct uni_print_tbl tbl[] = {
		MKT(UNI_REPORT_MODCONF,	modconf),
		MKT(UNI_REPORT_CLOCK,	clock),
		MKT(UNI_REPORT_EEAVAIL,	eeavail),
		MKT(UNI_REPORT_EEREQ,	eereq),
		MKT(UNI_REPORT_EECOMPL,	eecompl),
		EOT()
	};

	if(uni_print_iehdr("report", &ie->h, cx))
		return;
	uni_print_tbl("type", ie->report, tbl, cx);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, report)
{
	UNUSED(cx);

	switch(ie->report) {

	  default:
		return -1;

	  case UNI_REPORT_MODCONF:
	  case UNI_REPORT_CLOCK:
	  case UNI_REPORT_EEAVAIL:
	  case UNI_REPORT_EEREQ:
	  case UNI_REPORT_EECOMPL:
		break;
	}
	return 0;
}

DEF_IE_ENCODE(itu, report)
{
	START_IE(report, UNI_IE_REPORT, 1);

	APP_BYTE(msg, ie->report);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(itu, report)
{
	IE_START(;);
	if(ielen != 1)
		goto rej;

	ie->report = *msg->b_rptr++;

	IE_END(REPORT);
}

/*********************************************************************
 *
 * Soft PVPC/PVCC
 *
 * References for this IE are:
 *
 *  PNNI1.0 pp. 201...203
 *
 * Only NET coding allowed.
 */
DEF_IE_PRINT(net, calling_soft)
{
	if(uni_print_iehdr("calling_soft", &ie->h, cx))
		return;

	uni_print_entry(cx, "vpi", "%d", ie->vpi);
	if(ie->h.present & UNI_CALLING_SOFT_VCI_P)
		uni_print_entry(cx, "vci", "%d", ie->vci);

	uni_print_ieend(cx);
}

DEF_IE_PRINT(net, called_soft)
{
	static const struct uni_print_tbl tab[] = {
		MKT(UNI_SOFT_SEL_ANY,	any),
		MKT(UNI_SOFT_SEL_REQ,	required),
		MKT(UNI_SOFT_SEL_ASS,	assigned),
		EOT()
	};

	if(uni_print_iehdr("called_soft", &ie->h, cx))
		return;

	uni_print_tbl("selection", ie->sel, tab, cx);
	if(ie->h.present & UNI_CALLED_SOFT_VPI_P)
		uni_print_entry(cx, "vpi", "%d", ie->vpi);
	if(ie->h.present & UNI_CALLED_SOFT_VCI_P)
		uni_print_entry(cx, "vci", "%d", ie->vci);

	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, calling_soft)
{
	UNUSED(cx);

	if(ie->vpi >= 1 << 12)
		return -1;
	return 0;
}

DEF_IE_CHECK(net, called_soft)
{
	UNUSED(cx);

	switch(ie->sel) {

	  case UNI_SOFT_SEL_ANY:
	  case UNI_SOFT_SEL_REQ:
	  case UNI_SOFT_SEL_ASS:
		break;

	  default:
		return -1;
	}
	if(ie->h.present & UNI_CALLED_SOFT_VPI_P) {
		if(ie->vpi >= 1 << 12)
			return -1;
	} else {
		if(ie->sel != UNI_SOFT_SEL_ANY)
			return -1;
	}

	if(ie->h.present & UNI_CALLED_SOFT_VCI_P)
		if(!(ie->h.present & UNI_CALLED_SOFT_VPI_P))
			return -1;


	return 0;
}

DEF_IE_ENCODE(net, calling_soft)
{
	START_IE(calling_soft, UNI_IE_CALLING_SOFT, 6);

	APP_BYTE(msg, 0x81);
	APP_16BIT(msg, ie->vpi);

	if(ie->h.present & UNI_CALLING_SOFT_VCI_P) {
		APP_BYTE(msg, 0x82);
		APP_16BIT(msg, ie->vci);
	}

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_ENCODE(net, called_soft)
{
	START_IE(called_soft, UNI_IE_CALLED_SOFT, 7);

	APP_BYTE(msg, ie->sel);

	if(ie->h.present & UNI_CALLED_SOFT_VPI_P) {
		APP_BYTE(msg, 0x81);
		APP_16BIT(msg, ie->vpi);
	}

	if(ie->h.present & UNI_CALLED_SOFT_VCI_P) {
		APP_BYTE(msg, 0x82);
		APP_16BIT(msg, ie->vci);
	}

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(net, calling_soft)
{
	int vci_seen, vpi_seen;

	IE_START(;);
	if(ielen < 3)
		goto rej;

	vci_seen = 0;
	vpi_seen = 0;

	while(ielen) {
		switch(*msg->b_rptr++) {

		  case 0x81:
			if(!vpi_seen) {
				ie->vpi = *msg->b_rptr++ << 8;
				ie->vpi |= *msg->b_rptr++;
			} else {
				msg->b_rptr += 2;
			}
			ielen -= 3;
			break;

		  case 0x82:
			if(!vci_seen) {
				ie->vci = *msg->b_rptr++ << 8;
				ie->vci |= *msg->b_rptr++;
			} else {
				msg->b_rptr += 2;
			}
			ie->h.present |= UNI_CALLING_SOFT_VCI_P;
			ielen -= 3;
			break;

		  default:
			goto rej;
		}
	}

	if(!vpi_seen)
		goto rej;

	IE_END(CALLING_SOFT);
}

DEF_IE_DECODE(net, called_soft)
{
	int vci_seen, vpi_seen;

	IE_START(;);
	if(ielen < 3)
		goto rej;

	vci_seen = 0;
	vpi_seen = 0;

	while(ielen) {
		switch(*msg->b_rptr++) {

		  case 0x81:
			if(!vpi_seen) {
				ie->vpi = *msg->b_rptr++ << 8;
				ie->vpi |= *msg->b_rptr++;
				vpi_seen = 1;
			} else {
				msg->b_rptr += 2;
			}
			ielen -= 3;
			ie->h.present |= UNI_CALLED_SOFT_VCI_P;
			break;

		  case 0x82:
			if(!vci_seen) {
				ie->vci = *msg->b_rptr++ << 8;
				ie->vci |= *msg->b_rptr++;
				vci_seen = 1;
			} else {
				msg->b_rptr += 2;
			}
			ie->h.present |= UNI_CALLED_SOFT_VCI_P;
			ielen -= 3;
			break;

		  default:
			goto rej;
		}
	}

	IE_END(CALLED_SOFT);
}

/*********************************************************************
 *
 * Crankback
 *
 * References for this IE are:
 *
 *  PNNI1.0 pp. 203...206
 *
 * Only NET coding allowed.
 */

DEF_IE_PRINT(net, crankback)
{
	u_int j;

	if(uni_print_iehdr("crankback", &ie->h, cx))
		return;

	uni_print_entry(cx, "level", "%d", ie->level);

	switch(ie->type) {

	  case UNI_CRANKBACK_IF:
		uni_print_entry(cx, "type", "interface");
		break;

	  case UNI_CRANKBACK_NODE:
		uni_print_entry(cx, "type", "node");
		uni_print_entry(cx, "node", "{%d/", ie->id.node.level);
		for(j = 0; j < 21; j++)
			uni_printf(cx, "%02x", ie->id.node.id[j]);
		uni_printf(cx, "}");
		uni_print_eol(cx);
		break;

	  case UNI_CRANKBACK_LINK:
		uni_print_entry(cx, "type", "link");
		uni_print_push_prefix("link", cx);
		cx->indent++;

		uni_print_entry(cx, "prec", "{%d/", ie->id.link.plevel);
		for(j = 0; j < 21; j++)
			uni_printf(cx, "%02x", ie->id.link.pid[j]);
		uni_printf(cx, "}");
		uni_print_eol(cx);

		uni_print_entry(cx, "port", "0x%04x", ie->id.link.port);
		uni_print_eol(cx);

		uni_print_entry(cx, "succ", "{%d/", ie->id.link.slevel);
		for(j = 0; j < 21; j++)
			uni_printf(cx, "%02x", ie->id.link.sid[j]);
		uni_printf(cx, "}");
		uni_print_eol(cx);

		cx->indent--;
		uni_print_pop_prefix(cx);
		break;

	  default:
		uni_print_entry(cx, "type", "0x%02x", ie->type);
		break;
	}

	uni_print_entry(cx, "cause", "0x%02x", ie->cause);

	if(ie->h.present & UNI_CRANKBACK_TOP_P) {
		uni_print_push_prefix("topol", cx);
		uni_print_entry(cx, "dir", "%d", ie->diag.top.dir);
		uni_print_entry(cx, "port", "0x%04x", ie->diag.top.port);
		uni_print_entry(cx, "avcr", "%u", ie->diag.top.avcr);
		if(ie->h.present & UNI_CRANKBACK_TOPX_P) {
			uni_print_entry(cx, "crm", "%u", ie->diag.top.crm);
			uni_print_entry(cx, "vf", "%u", ie->diag.top.vf);
		}
		uni_print_pop_prefix(cx);
		uni_print_eol(cx);
	}
	if(ie->h.present & UNI_CRANKBACK_QOS_P) {
		uni_print_push_prefix("qos", cx);
		uni_print_entry(cx, "ctd", "%savail", ie->diag.qos.ctd ? "" : "un");
		uni_print_entry(cx, "cdv", "%savail", ie->diag.qos.cdv ? "" : "un");
		uni_print_entry(cx, "clr", "%savail", ie->diag.qos.clr ? "" : "un");
		uni_print_entry(cx, "other", "%savail", ie->diag.qos.other ? "" : "un");
		uni_print_pop_prefix(cx);
		uni_print_eol(cx);
	}

	uni_print_eol(cx);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, crankback)
{
	UNUSED(cx);

	if(ie->level > 104)
		return -1;
	switch(ie->type) {
	  case UNI_CRANKBACK_IF:
		break;
	  case UNI_CRANKBACK_NODE:
		if(ie->id.node.level > 104)
			return -1;
		break;

	  case UNI_CRANKBACK_LINK:
		if(ie->id.link.plevel > 104)
			return -1;
		if(ie->id.link.slevel > 104)
			return -1;
		break;

	  default:
		return -1;
	}

	if(ie->h.present & UNI_CRANKBACK_TOP_P) {
		if(ie->h.present & UNI_CRANKBACK_QOS_P)
			return -1;

		if(ie->cause != UNI_CAUSE_CRATE_NAVL)
			return -1;
		switch(ie->diag.top.dir) {

		  case 0x00:
		  case 0x01:
			break;

		  default:
			return -1;
		}
	}
	if(ie->h.present & UNI_CRANKBACK_QOS_P) {
		if(ie->cause != UNI_CAUSE_QOS_NAVL)
			return -1;
	}
	return 0;
}

DEF_IE_ENCODE(net, crankback)
{
	START_IE(crankback, UNI_IE_CRANKBACK, 72);

	APP_BYTE(msg, ie->level);
	APP_BYTE(msg, ie->type);

	switch(ie->type) {

	  case UNI_CRANKBACK_IF:
		break;

	  case UNI_CRANKBACK_NODE:
		APP_BYTE(msg, ie->id.node.level);
		APP_BUF(msg, ie->id.node.id, 21);
		break;

	  case UNI_CRANKBACK_LINK:
		APP_BYTE(msg, ie->id.link.plevel);
		APP_BUF(msg, ie->id.link.pid, 21);
		APP_32BIT(msg, ie->id.link.port);
		APP_BYTE(msg, ie->id.link.slevel);
		APP_BUF(msg, ie->id.link.sid, 21);
		break;
	}

	APP_BYTE(msg, ie->cause);

	if(ie->h.present & UNI_CRANKBACK_TOP_P) {
		APP_BYTE(msg, ie->diag.top.dir);
		APP_32BIT(msg, ie->diag.top.port);
		APP_32BIT(msg, ie->diag.top.avcr);
		if(ie->h.present & UNI_CRANKBACK_TOPX_P) {
			APP_32BIT(msg, ie->diag.top.crm);
			APP_32BIT(msg, ie->diag.top.vf);
		}
	}

	if(ie->h.present & UNI_CRANKBACK_QOS_P) {
		APP_BYTE(msg, (ie->diag.qos.ctd << 3)
			     |(ie->diag.qos.cdv << 2)
			     |(ie->diag.qos.clr << 1)
			     |(ie->diag.qos.other));
	}
	SET_IE_LEN(msg);
	return 0;
}


DEF_IE_DECODE(net, crankback)
{
	IE_START(;);

	if(ielen < 3)
		goto rej;

	ie->level = *msg->b_rptr++;
	ielen--;

	ie->type = *msg->b_rptr++;
	ielen--;

	switch(ie->type) {

	  default:
		goto rej;

	  case UNI_CRANKBACK_IF:
		break;

	  case UNI_CRANKBACK_NODE:
		if(ielen < 22)
			goto rej;
		ie->id.node.level = *msg->b_rptr++;
		(void)memcpy(ie->id.node.id, msg->b_rptr, 21);
		msg->b_rptr += 21;
		ielen -= 22;
		break;

	  case UNI_CRANKBACK_LINK:
		if(ielen < 48)
			goto rej;
		ie->id.link.plevel = *msg->b_rptr++;
		(void)memcpy(ie->id.link.pid, msg->b_rptr, 21);
		msg->b_rptr += 21;
		ielen -= 22;

		ie->id.link.port  = *msg->b_rptr++ << 24;
		ie->id.link.port |= *msg->b_rptr++ << 16;
		ie->id.link.port |= *msg->b_rptr++ <<  8;
		ie->id.link.port |= *msg->b_rptr++ <<  0;
		ielen -= 4;

		ie->id.link.slevel = *msg->b_rptr++;
		(void)memcpy(ie->id.link.sid, msg->b_rptr, 21);
		msg->b_rptr += 21;
		ielen -= 22;

		break;
	}

	if(ielen < 1)
		goto rej;
	ie->cause = *msg->b_rptr++;
	ielen--;

	if(ie->cause == UNI_CAUSE_CRATE_NAVL) {
		if(ielen > 0) {
			if(ielen != 9 && ielen != 17)
				goto rej;
			ie->diag.top.dir = *msg->b_rptr++;
			ie->diag.top.port  = *msg->b_rptr++ << 24;
			ie->diag.top.port |= *msg->b_rptr++ << 16;
			ie->diag.top.port |= *msg->b_rptr++ <<  8;
			ie->diag.top.port |= *msg->b_rptr++ <<  0;
			ie->diag.top.avcr  = *msg->b_rptr++ << 24;
			ie->diag.top.avcr |= *msg->b_rptr++ << 16;
			ie->diag.top.avcr |= *msg->b_rptr++ <<  8;
			ie->diag.top.avcr |= *msg->b_rptr++ <<  0;
			ielen -= 9;
			ie->h.present |= UNI_CRANKBACK_TOP_P;
			if(ielen > 0) {
				ie->diag.top.crm  = *msg->b_rptr++ << 24;
				ie->diag.top.crm |= *msg->b_rptr++ << 16;
				ie->diag.top.crm |= *msg->b_rptr++ <<  8;
				ie->diag.top.crm |= *msg->b_rptr++ <<  0;
				ie->diag.top.vf  = *msg->b_rptr++ << 24;
				ie->diag.top.vf |= *msg->b_rptr++ << 16;
				ie->diag.top.vf |= *msg->b_rptr++ <<  8;
				ie->diag.top.vf |= *msg->b_rptr++ <<  0;
				ie->h.present |= UNI_CRANKBACK_TOPX_P;
				ielen -= 8;
			}
		}
	} else if(ie->cause == UNI_CAUSE_QOS_NAVL) {
		if(ielen > 0) {
			if(ielen != 1)
				goto rej;
			ie->diag.qos.ctd = *msg->b_rptr >> 3;
			ie->diag.qos.cdv = *msg->b_rptr >> 2;
			ie->diag.qos.clr = *msg->b_rptr >> 1;
			ie->diag.qos.other = *msg->b_rptr >> 0;
			ie->h.present |= UNI_CRANKBACK_QOS_P;
			ielen -= 1;
		}
	} else {
		if(ielen > 0)
			goto rej;
	}

	IE_END(CRANKBACK);
}

/*********************************************************************
 *
 * Designated transit list
 *
 * References for this IE are:
 *
 *  PNNI1.0 pp. 206...208
 *
 * Only NET coding allowed.
 */
DEF_IE_PRINT(net, dtl)
{
	u_int i, j;
	char buf[10];

	if(uni_print_iehdr("dtl", &ie->h, cx))
		return;

	uni_print_entry(cx, "ptr", "%d(%d)", ie->ptr, ie->ptr / UNI_DTL_LOGNP_SIZE);
	uni_print_push_prefix("dtl", cx);
	cx->indent++;
	uni_printf(cx, "{");
	for(i = 0; i < ie->num; i++) {
		sprintf(buf, "%d", i);
		uni_print_entry(cx, buf, "{%d/", ie->dtl[i].node_level);
		for(j = 0; j < 21; j++)
			uni_printf(cx, "%02x", ie->dtl[i].node_id[j]);
		uni_printf(cx, ",%04x}", ie->dtl[i].port_id);
		uni_print_eol(cx);
	}
	cx->indent--;
	uni_print_pop_prefix(cx);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, dtl)
{
	u_int i;

	UNUSED(cx);

	if(ie->ptr % UNI_DTL_LOGNP_SIZE != 0)
		return -1;
	if(ie->ptr / UNI_DTL_LOGNP_SIZE > UNI_DTL_MAXNUM)
		return -1;
	if(ie->num > UNI_DTL_MAXNUM)
		return -1;
	for(i = 0; i < ie->num; i++)
		if(ie->dtl[i].node_level > 104)
			return -1;
	return 0;
}

DEF_IE_ENCODE(net, dtl)
{
	u_int i;

	START_IE(dtl, UNI_IE_DTL, 2 + UNI_DTL_LOGNP_SIZE * ie->num);

	APP_16BIT(msg, ie->ptr);

	for(i = 0; i < ie->num; i++) {
		APP_BYTE(msg, UNI_DTL_LOGNP);
		APP_BYTE(msg, ie->dtl[i].node_level);
		APP_BUF(msg, ie->dtl[i].node_id, 21);
		APP_32BIT(msg, ie->dtl[i].port_id);
	}

	SET_IE_LEN(msg);
	return 0;
}


DEF_IE_DECODE(net, dtl)
{
	IE_START(;);

	if(ielen < 2)
		goto rej;

	ie->ptr = *msg->b_rptr++ << 8;
	ie->ptr |= *msg->b_rptr++;
	ielen -= 2;

	if(ielen % UNI_DTL_LOGNP_SIZE != 0)
		goto rej;
	if(ielen / UNI_DTL_LOGNP_SIZE > UNI_DTL_MAXNUM)
		goto rej;

	ie->num = 0;
	while(ielen) {
		if(*msg->b_rptr++ != UNI_DTL_LOGNP)
			goto rej;
		ielen--;

		ie->dtl[ie->num].node_level = *msg->b_rptr++;
		ielen--;

		(void)memcpy(ie->dtl[ie->num].node_id, msg->b_rptr, 21);
		msg->b_rptr += 21;
		ielen -= 21;

		ie->dtl[ie->num].port_id  = *msg->b_rptr++ << 24;
		ie->dtl[ie->num].port_id |= *msg->b_rptr++ << 16;
		ie->dtl[ie->num].port_id |= *msg->b_rptr++ <<  8;
		ie->dtl[ie->num].port_id |= *msg->b_rptr++ <<  0;
		ielen -= 4;

		ie->num++;
	}

	IE_END(DTL);
}

/*********************************************************************
 *
 * Leaf initiated join call identifier.
 * Leaf initiated join parameters.
 * Leaf initiated join sequence number.
 *
 * References for this IE are:
 *
 *  UNI4.0 pp. 46...48
 *
 * Only NET coding allowed.
 */

/**********************************************************************/

DEF_IE_PRINT(net, lij_callid)
{
	static const struct uni_print_tbl type_tbl[] = {
		MKT(UNI_LIJ_IDTYPE_ROOT, root),
		EOT()
	};

	if(uni_print_iehdr("lij_callid", &ie->h, cx))
		return;

	uni_print_tbl("type", ie->type, type_tbl, cx);
	uni_print_entry(cx, "id", "0x%x", ie->callid);

	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, lij_callid)
{
	UNUSED(cx);

	switch(ie->type) {

	  case UNI_LIJ_IDTYPE_ROOT:
		break;

	  default:
		return -1;
	}

	return 0;
}

DEF_IE_ENCODE(net, lij_callid)
{
	START_IE(lij_callid, UNI_IE_LIJ_CALLID, 5);

	APP_BYTE(msg, 0x80 | ie->type);
	APP_32BIT(msg, ie->callid);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(net, lij_callid)
{
	IE_START(;);

	if(ielen != 5)
		goto rej;

	ie->type = *msg->b_rptr++ & 0xf;
	ie->callid  = *msg->b_rptr++ << 24;
	ie->callid |= *msg->b_rptr++ << 16;
	ie->callid |= *msg->b_rptr++ <<  8;
	ie->callid |= *msg->b_rptr++ <<  0;

	IE_END(LIJ_CALLID);
}

/**********************************************************************/

DEF_IE_PRINT(net, lij_param)
{
	static const struct uni_print_tbl lscreen_tbl[] = {
		MKT(UNI_LIJ_SCREEN_NETJOIN, netjoin),
		EOT()
	};

	if(uni_print_iehdr("lij_param", &ie->h, cx))
		return;
	uni_print_tbl("screen", ie->screen, lscreen_tbl, cx);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, lij_param)
{
	UNUSED(cx);

	switch(ie->screen) {

	  case UNI_LIJ_SCREEN_NETJOIN:
		break;

	  default:
		return -1;
	}

	return 0;
}

DEF_IE_ENCODE(net, lij_param)
{
	START_IE(lij_param, UNI_IE_LIJ_PARAM, 1);

	APP_BYTE(msg, 0x80 | ie->screen);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(net, lij_param)
{
	IE_START(;);

	if(ielen != 1)
		goto rej;

	ie->screen = *msg->b_rptr++ & 0xf;

	IE_END(LIJ_PARAM);
}

/**********************************************************************/

DEF_IE_PRINT(net, lij_seqno)
{
	if(uni_print_iehdr("lij_seqno", &ie->h, cx))
		return;
	uni_print_entry(cx, "seqno", "0x%x", ie->seqno);
	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, lij_seqno)
{
	UNUSED(cx); UNUSED(ie);

	return 0;
}

DEF_IE_ENCODE(net, lij_seqno)
{
	START_IE(lij_seqno, UNI_IE_LIJ_SEQNO, 4);

	APP_32BIT(msg, ie->seqno);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(net, lij_seqno)
{
	IE_START(;);

	if(ielen != 4)
		goto rej;

	ie->seqno  = *msg->b_rptr++ << 24;
	ie->seqno |= *msg->b_rptr++ << 16;
	ie->seqno |= *msg->b_rptr++ <<  8;
	ie->seqno |= *msg->b_rptr++ <<  0;

	IE_END(LIJ_SEQNO);
}

/*********************************************************************
 *
 * Connection scope
 *
 * References for this IE are:
 *
 *  UNI4.0 pp. 57...58
 *
 * Only NET coding allowed.
 */
DEF_IE_PRINT(net, cscope)
{
	static const struct uni_print_tbl type_tbl[] = {
		MKT(UNI_CSCOPE_ORG,	org),
		EOT()
	};
	static const struct uni_print_tbl scope_tbl[] = {
		MKT(UNI_CSCOPE_ORG_LOC,		local_network),
		MKT(UNI_CSCOPE_ORG_LOC_P1,	local_network_plus_one),
		MKT(UNI_CSCOPE_ORG_LOC_P2,	local_network_plus_two),
		MKT(UNI_CSCOPE_ORG_SITE_M1,	site_minus_one),
		MKT(UNI_CSCOPE_ORG_SITE,	intra_site),
		MKT(UNI_CSCOPE_ORG_SITE_P1,	site_plus_one),
		MKT(UNI_CSCOPE_ORG_ORG_M1,	organisation_minus_one),
		MKT(UNI_CSCOPE_ORG_ORG,		intra_organisation),
		MKT(UNI_CSCOPE_ORG_ORG_P1,	organisation_plus_one),
		MKT(UNI_CSCOPE_ORG_COMM_M1,	community_minus_one),
		MKT(UNI_CSCOPE_ORG_COMM,	intra_community),
		MKT(UNI_CSCOPE_ORG_COMM_P1,	community_plus_one),
		MKT(UNI_CSCOPE_ORG_REG,		regional),
		MKT(UNI_CSCOPE_ORG_INTER,	inter_regional),
		MKT(UNI_CSCOPE_ORG_GLOBAL,	global),
		EOT()
	};

	if(uni_print_iehdr("cscope", &ie->h, cx))
		return;

	uni_print_tbl("type", ie->type, type_tbl, cx);
	if(ie->type == UNI_CSCOPE_ORG)
		uni_print_tbl("scope", (u_int)ie->scope, scope_tbl, cx);
	else
		uni_print_entry(cx, "scope", "0x%02x", ie->scope);

	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, cscope)
{
	UNUSED(cx);

	switch(ie->type) {

	  default:
		return -1;

	  case UNI_CSCOPE_ORG:
		switch(ie->scope) {

		  default:
			return -1;

		  case UNI_CSCOPE_ORG_LOC:
		  case UNI_CSCOPE_ORG_LOC_P1:
		  case UNI_CSCOPE_ORG_LOC_P2:
		  case UNI_CSCOPE_ORG_SITE_M1:
		  case UNI_CSCOPE_ORG_SITE:
		  case UNI_CSCOPE_ORG_SITE_P1:
		  case UNI_CSCOPE_ORG_ORG_M1:
		  case UNI_CSCOPE_ORG_ORG:
		  case UNI_CSCOPE_ORG_ORG_P1:
		  case UNI_CSCOPE_ORG_COMM_M1:
		  case UNI_CSCOPE_ORG_COMM:
		  case UNI_CSCOPE_ORG_COMM_P1:
		  case UNI_CSCOPE_ORG_REG:
		  case UNI_CSCOPE_ORG_INTER:
		  case UNI_CSCOPE_ORG_GLOBAL:
			break;
		}
		break;
	}
	return 0;
}

DEF_IE_ENCODE(net, cscope)
{
	START_IE(cscope, UNI_IE_CSCOPE, 2);

	APP_BYTE(msg, ie->type | 0x80);
	APP_BYTE(msg, ie->scope);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(net, cscope)
{
	IE_START(;);
	if(ielen != 2)
		goto rej;

	if((*msg->b_rptr & 0xf0) != 0x80)
		goto rej;

	ie->type = *msg->b_rptr++ & 0xf;
	ie->scope = *msg->b_rptr++;

	IE_END(CSCOPE);
}

/*********************************************************************
 *
 * Extended Quality of Service
 *
 * References for this IE are:
 *
 *  	UNI4.0 pp. 70...72
 *
 * Notes:
 *	Only NET coding.
 */
DEF_IE_PRINT(net, exqos)
{
	static const struct uni_print_tbl tab[] = {
		MKT(UNI_EXQOS_USER,	user),
		MKT(UNI_EXQOS_NET,	net),
		EOT()
	};

	if(uni_print_iehdr("exqos", &ie->h, cx))
		return;

	uni_print_tbl("origin", ie->origin, tab, cx);

	uni_print_entry(cx, "acceptable", "(");
	if(ie->h.present & UNI_EXQOS_FACC_P) {
		if(ie->facc == UNI_EXQOS_ANY_CDV)
			uni_printf(cx, "ANY");
		else
			uni_printf(cx, "%d", ie->facc);
	}
	uni_putc(',', cx);
	if(ie->h.present & UNI_EXQOS_BACC_P) {
		if(ie->bacc == UNI_EXQOS_ANY_CDV)
			uni_printf(cx, "ANY");
		else
			uni_printf(cx, "%d", ie->bacc);
	}
	uni_putc(')', cx);

	uni_print_entry(cx, "cumulative", "(");
	if(ie->h.present & UNI_EXQOS_FCUM_P)
		uni_printf(cx, "%d", ie->fcum);
	uni_putc(',', cx);
	if(ie->h.present & UNI_EXQOS_BCUM_P)
		uni_printf(cx, "%d", ie->bcum);
	uni_putc(')', cx);

	uni_print_entry(cx, "clrid", "(");
	if(ie->h.present & UNI_EXQOS_FCLR_P) {
		if(ie->fclr == UNI_EXQOS_ANY_CLR)
			uni_printf(cx, "ANY");
		else
			uni_printf(cx, "%d", ie->fclr);
	}
	uni_putc(',', cx);
	if(ie->h.present & UNI_EXQOS_BCLR_P) {
		if(ie->bclr == UNI_EXQOS_ANY_CLR)
			uni_printf(cx, "ANY");
		else
			uni_printf(cx, "%d", ie->bclr);
	}
	uni_putc(')', cx);

	uni_print_ieend(cx);
}

DEF_IE_CHECK(net, exqos)
{
	UNUSED(cx);

	switch(ie->origin) {
	  case UNI_EXQOS_USER:
	  case UNI_EXQOS_NET:
		break;

	  default:
		return -1;
	}
	if(ie->h.present & UNI_EXQOS_FACC_P)
		if(!(ie->h.present & UNI_EXQOS_FCUM_P))
			return -1;
	if(ie->h.present & UNI_EXQOS_BACC_P)
		if(!(ie->h.present & UNI_EXQOS_BCUM_P))
			return -1;

	if(ie->h.present & UNI_EXQOS_FACC_P)
		if(ie->facc >= 1 << 24)
			return -1;
	if(ie->h.present & UNI_EXQOS_BACC_P)
		if(ie->bacc >= 1 << 24)
			return -1;
	if(ie->h.present & UNI_EXQOS_FCUM_P)
		if(ie->fcum >= 1 << 24)
			return -1;
	if(ie->h.present & UNI_EXQOS_BCUM_P)
		if(ie->bcum >= 1 << 24)
			return -1;

	if(ie->h.present & UNI_EXQOS_FCLR_P)
		if(ie->fclr==0 || (ie->fclr>15 && ie->fclr!=UNI_EXQOS_ANY_CLR))
			return -1;
	if(ie->h.present & UNI_EXQOS_BCLR_P)
		if(ie->bclr==0 || (ie->bclr>15 && ie->bclr!=UNI_EXQOS_ANY_CLR))
			return -1;
	return 0;
}

DEF_IE_ENCODE(net, exqos)
{
	START_IE(exqos, UNI_IE_EXQOS, 21);

	APP_BYTE(msg, ie->origin);

	APP_OPT_24BIT(msg, ie->h.present, UNI_EXQOS_FACC_P,
		UNI_EXQOS_FACC_ID, ie->facc);
	APP_OPT_24BIT(msg, ie->h.present, UNI_EXQOS_BACC_P,
		UNI_EXQOS_BACC_ID, ie->bacc);
	APP_OPT_24BIT(msg, ie->h.present, UNI_EXQOS_FCUM_P,
		UNI_EXQOS_FCUM_ID, ie->fcum);
	APP_OPT_24BIT(msg, ie->h.present, UNI_EXQOS_BCUM_P,
		UNI_EXQOS_BCUM_ID, ie->bcum);

	APP_OPT_BYTE(msg, ie->h.present, UNI_EXQOS_FCLR_P,
		UNI_EXQOS_FCLR_ID, ie->fclr);
	APP_OPT_BYTE(msg, ie->h.present, UNI_EXQOS_BCLR_P,
		UNI_EXQOS_BCLR_ID, ie->bclr);

	SET_IE_LEN(msg);
	return 0;
}

DEF_IE_DECODE(net, exqos)
{
	IE_START(;);

	if(ielen < 1 || ielen > 21)
		goto rej;

	ie->origin = *msg->b_rptr++;
	ielen--;

	while(ielen--) {
		switch(*msg->b_rptr++) {

		  default:
			goto rej;

		  DEC_GETF3(EXQOS_FACC, facc, ie->h.present);
		  DEC_GETF3(EXQOS_BACC, bacc, ie->h.present);
		  DEC_GETF3(EXQOS_FCUM, fcum, ie->h.present);
		  DEC_GETF3(EXQOS_BCUM, bcum, ie->h.present);

		  DEC_GETF1(EXQOS_FCLR, fclr, ie->h.present);
		  DEC_GETF1(EXQOS_BCLR, bclr, ie->h.present);

		}
	}
	IE_END(EXQOS);
}

/**************************************************************
 *
 * Free form IE (for testing mainly)
 */
DEF_IE_PRINT(itu, unrec)
{
	u_int i;

	if (uni_print_iehdr("unrec", &ie->h, cx))
		return;
	uni_print_entry(cx, "len", "%u", ie->len);
	uni_print_entry(cx, "data", "(");
	for (i = 0; i < ie->len; i++)
		uni_printf(cx, "%s0x%02x", i == 0 ? "" : " ", ie->data[i]);
	uni_printf(cx, ")");
	uni_print_ieend(cx);
}

DEF_IE_CHECK(itu, unrec)
{
	UNUSED(cx);

	if (ie->len > sizeof(ie->data))
		return (-1);

	return (0);
}

DEF_IE_ENCODE(itu, unrec)
{
	START_IE2(unrec, UNI_IE_UNREC, ie->len, ie->id);

	APP_BUF(msg, ie->data, ie->len);

	SET_IE_LEN(msg);
	return (0);
}

DEF_IE_DECODE(itu, unrec)
{
	IE_START(;);

	if (ielen > sizeof(ie->data) / sizeof(ie->data[0]) || ielen < 1)
		goto rej;

	ie->len = ielen;
	ielen = 0;
	(void)memcpy(ie->data, msg->b_rptr, ie->len);
	msg->b_rptr += ie->len;

	IE_END(UNREC);
}
