/*
 * ng_parse.h
 */

/*-
 * Copyright (c) 1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $Whistle: ng_parse.h,v 1.2 1999/11/29 01:43:48 archie Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_NG_PARSE_H_
#define _NETGRAPH_NG_PARSE_H_

/*

  This defines a library of routines for converting between various C
  language types in binary form and ASCII strings.  Types are user
  definable.  Several pre-defined types are supplied, for some common
  C types: structures, variable and fixed length arrays, integer types,
  variable and fixed length strings, IP addresses, etc.

  A netgraph node type may provide a list of types that correspond to
  the structures it expects to send and receive in the arguments field
  of a control message.  This allows these messages to be converted
  between their native binary form and the corresponding ASCII form.

  A future use of the ASCII form may be for inter-machine communication
  of control messages, because the ASCII form is machine independent
  whereas the native binary form is not.

  Syntax
  ------

    Structures:

      '{' [ <name>=<value> ... ] '}'

      Omitted fields have their default values by implication.
      The order in which the fields are specified does not matter.

    Arrays:

      '[' [ [index=]<value> ... ] ']'

      Element value may be specified with or without the "<index>=" prefix;
      If omitted, the index after the previous element is used.
      Omitted fields have their default values by implication.

    Strings:

      "foo bar blah\r\n"

      That is, strings are specified just like C strings. The usual
      backslash escapes are accepted.

    Other simple types (integers, IP addresses) have their obvious forms.

  Example
  -------

    Suppose we have a netgraph command that takes as an argument
    a 'struct foo' shown below.  Here is an example of a possible
    value for the structure, and the corresponding ASCII encoding
    of that value:

	Structure			Binary value
	---------			------------

	struct foo {
	    struct in_addr ip;  	01 02 03 04
	    int bar;			00 00 00 00
	    char label[8];		61 62 63 0a 00 00 00 00
	    u_char alen;		03 00
	    short ary[];	  	05 00 00 00 0a 00
	};

	ASCII value
	-----------

	{ ip=1.2.3.4 label="abc\n" alen=3 ary=[ 5 2=10 ] }

    Note that omitted fields and array elements get their default
    values ("bar" and ary[2]), and that the alignment is handled
    automatically (the extra 00 byte after "alen").  Also, since byte
    order and alignment are inherently machine dependent, so is this
    conversion process.  The above example shows an x86 (little
    endian) encoding.  Also the above example is tricky because the
    structure is variable length, depending on 'alen', the number of
    elements in the array 'ary'.

    Here is how one would define a parse type for the above structure,
    subclassing the pre-defined types below.  We construct the type in
    a 'bottom up' fashion, defining each field's type first, then the
    type for the whole structure ('//' comments used to avoid breakage).

    // Super-type info for 'label' field
    struct ng_parse_fixedstring_info foo_label_info = { 8 };

    // Parse type for 'label' field
    struct ng_parse_type foo_label_type = {
	    &ng_parse_fixedstring_type		// super-type
	    &foo_label_info			// super-type info
    };

    #define OFFSETOF(s, e) ((char *)&((s *)0)->e - (char *)((s *)0))

    // Function to compute the length of the array 'ary', which
    // is variable length, depending on the previous field 'alen'.
    // Upon entry 'buf' will be pointing at &ary[0].
    int
    foo_ary_getLength(const struct ng_parse_type *type,
	    const u_char *start, const u_char *buf)
    {
	    const struct foo *f;

	    f = (const struct foo *)(buf - OFFSETOF(struct foo, ary));
	    return f->alen;
    }

    // Super-type info for 'ary' field
    struct ng_parse_array_info foo_ary_info = {
	    &ng_parse_int16_type,		// element type
	    &foo_ary_getLength			// func to get array length
    }

    // Parse type for 'ary' field
    struct ng_parse_type foo_ary_type = {
	    &ng_parse_array_type,		// super-type
	    &foo_ary_info			// super-type info
    };

    // Super-type info for struct foo
    struct ng_parse_struct_field foo_fields[] = {
	    { "ip",	&ng_parse_ipaddr_type	},
	    { "bar",	&ng_parse_int32_type	},
	    { "label",	&foo_label_type		},
	    { "alen",	&ng_parse_uint8_type	},
	    { "ary",	&foo_ary_type		},
	    { NULL }
    };

    // Parse type for struct foo
    struct ng_parse_type foo_type = {
	    &ng_parse_struct_type,		// super-type
	    &foo_fields				// super-type info
    };

  To define a type, you can define it as a sub-type of a predefined
  type as shown above, possibly overriding some of the predefined
  type's methods, or define an entirely new syntax, with the restriction
  that the ASCII representation of your type's value must not contain
  any whitespace or any of these characters: { } [ ] = "

  See ng_ksocket.c for an example of how to do this for 'struct sockaddr'.
  See ng_parse.c to see implementations of the pre-defined types below.

*/

/************************************************************************
			METHODS REQUIRED BY A TYPE
 ************************************************************************/

/*
 * Three methods are required for a type. These may be given explicitly
 * or, if NULL, inherited from the super-type.  The 'getDefault' method
 * is always optional; the others are required if there is no super-type.
 */

struct ng_parse_type;

/*
 * Convert ASCII to binary according to the supplied type.
 *
 * The ASCII characters begin at offset *off in 'string'.  The binary
 * representation is put into 'buf', which has at least *buflen bytes.
 * 'start' points to the first byte output by ng_parse() (ie, start <= buf).
 *
 * Upon return, *buflen contains the length of the new binary data, and
 * *off is updated to point just past the end of the parsed range of
 * characters, or, in the case of an error, to the offending character(s).
 *
 * Return values:
 *	0		Success; *buflen holds the length of the data
 *			and *off points just past the last char parsed.
 *	EALREADY	Field specified twice
 *	ENOENT		Unknown field
 *	E2BIG		Array or character string overflow
 *	ERANGE		Output was longer than *buflen bytes
 *	EINVAL		Parse failure or other invalid content
 *	ENOMEM		Out of memory
 *	EOPNOTSUPP	Mandatory array/structure element missing
 */
typedef	int	ng_parse_t(const struct ng_parse_type *type, const char *string,
			int *off, const u_char *start,
			u_char *buf, int *buflen);

/*
 * Convert binary to ASCII according to the supplied type.
 *
 * The results are put into 'buf', which is at least buflen bytes long.
 * *off points to the current byte in 'data' and should be updated
 * before return to point just past the last byte unparsed.
 *
 * Returns:
 *	0		Success
 *	ERANGE		Output was longer than buflen bytes
 */
typedef	int	ng_unparse_t(const struct ng_parse_type *type,
			const u_char *data, int *off, char *buf, int buflen);

/*
 * Compute the default value according to the supplied type.
 *
 * Store the result in 'buf', which is at least *buflen bytes long.
 * Upon return *buflen contains the length of the output.
 *
 * Returns:
 *	0		Success
 *	ERANGE		Output was longer than *buflen bytes
 *	EOPNOTSUPP	Default value is not specified for this type
 */
typedef	int	ng_getDefault_t(const struct ng_parse_type *type,
			const u_char *start, u_char *buf, int *buflen);

/*
 * Return the alignment requirement of this type.  Zero is same as one.
 */
typedef	int	ng_getAlign_t(const struct ng_parse_type *type);

/************************************************************************
			TYPE DEFINITION
 ************************************************************************/

/*
 * This structure describes a type, which may be a sub-type of another
 * type by pointing to it with 'supertype' and possibly omitting methods.
 * Typically the super-type requires some type-specific info, which is
 * supplied by the 'info' field.
 *
 * The 'private' field is ignored by all of the pre-defined types.
 * Sub-types may use it as they see fit.
 *
 * The 'getDefault' method may always be omitted (even if there is no
 * super-type), which means the value for any item of this type must
 * always be explicitly given.
 */
struct ng_parse_type {
	const struct ng_parse_type *supertype;	/* super-type, if any */
	const void		*info;		/* type-specific info */
	void			*private;	/* client private info */
	ng_parse_t		*parse;		/* parse method */
	ng_unparse_t		*unparse;	/* unparse method */
	ng_getDefault_t		*getDefault;	/* get default value method */
	ng_getAlign_t		*getAlign;	/* get alignment */
};

/************************************************************************
			PRE-DEFINED TYPES
 ************************************************************************/

/*
 * STRUCTURE TYPE
 *
 * This type supports arbitrary C structures.  The normal field alignment
 * rules for the local machine are applied.  Fields are always parsed in
 * field order, no matter what order they are listed in the ASCII string.
 *
 *   Default value:		Determined on a per-field basis
 *   Additional info:		struct ng_parse_struct_field *
 */
extern const struct ng_parse_type ng_parse_struct_type;

/* Each field has a name, type, and optional alignment override. If the
   override is non-zero, the alignment is determined from the field type.
   Note: add an extra struct ng_parse_struct_field with name == NULL
   to indicate the end of the list. */
struct ng_parse_struct_field {
	const char			*name;		/* field name */
	const struct ng_parse_type	*type;		/* field type */
	int				alignment;	/* override alignment */
};

/*
 * FIXED LENGTH ARRAY TYPE
 *
 * This type supports fixed length arrays, having any element type.
 *
 *   Default value:		As returned by getDefault for each index
 *   Additional info:		struct ng_parse_fixedarray_info *
 */
extern const struct ng_parse_type ng_parse_fixedarray_type;

/*
 * Get the default value for the element at index 'index'.  This method
 * may be NULL, in which case the default value is computed from the
 * element type.  Otherwise, it should fill in the default value at *buf
 * (having size *buflen) and update *buflen to the length of the filled-in
 * value before return.  If there is not enough routine return ERANGE.
 */
typedef	int	ng_parse_array_getDefault_t(const struct ng_parse_type *type,
				int index, const u_char *start,
				u_char *buf, int *buflen);

struct ng_parse_fixedarray_info {
	const struct ng_parse_type	*elementType;
	int				length;
	ng_parse_array_getDefault_t	*getDefault;
};

/*
 * VARIABLE LENGTH ARRAY TYPE
 *
 * Same as fixed length arrays, except that the length is determined
 * by a function instead of a constant value.
 *
 *   Default value:		Same as with fixed length arrays
 *   Additional info:		struct ng_parse_array_info *
 */
extern const struct ng_parse_type ng_parse_array_type;

/*
 * Return the length of the array.  If the array is a field in a structure,
 * all prior fields are guaranteed to be filled in already.  Upon entry,
 * 'start' is equal to the first byte parsed in this run, while 'buf' points
 * to the first element of the array to be filled in.
 */
typedef int	ng_parse_array_getLength_t(const struct ng_parse_type *type,
				const u_char *start, const u_char *buf);

struct ng_parse_array_info {
	const struct ng_parse_type	*elementType;
	ng_parse_array_getLength_t	*getLength;
	ng_parse_array_getDefault_t	*getDefault;
};

/*
 * ARBITRARY LENGTH STRING TYPE
 *
 * For arbirary length, NUL-terminated strings.
 *
 *   Default value:		Empty string
 *   Additional info:		None required
 */
extern const struct ng_parse_type ng_parse_string_type;

/*
 * BOUNDED LENGTH STRING TYPE
 *
 * These are strings that have a fixed-size buffer, and always include
 * a terminating NUL character.
 *
 *   Default value:		Empty string
 *   Additional info:		struct ng_parse_fixedstring_info *
 */
extern const struct ng_parse_type ng_parse_fixedstring_type;

struct ng_parse_fixedstring_info {
	int	bufSize;	/* size of buffer (including NUL) */
};

/*
 * EXPLICITLY SIZED STRING TYPE
 *
 * These are strings that have a two byte length field preceding them.
 * Parsed strings are NOT NUL-terminated.
 *
 *   Default value:		Empty string
 *   Additional info:		None
 */
extern const struct ng_parse_type ng_parse_sizedstring_type;

/*
 * COMMONLY USED BOUNDED LENGTH STRING TYPES
 */
extern const struct ng_parse_type ng_parse_nodebuf_type;  /* NG_NODESIZ */
extern const struct ng_parse_type ng_parse_hookbuf_type;  /* NG_HOOKSIZ */
extern const struct ng_parse_type ng_parse_pathbuf_type;  /* NG_PATHSIZ */
extern const struct ng_parse_type ng_parse_typebuf_type;  /* NG_TYPESIZ */
extern const struct ng_parse_type ng_parse_cmdbuf_type;   /* NG_CMDSTRSIZ */

/*
 * INTEGER TYPES
 *
 *   Default value:		0
 *   Additional info:		None required
 */
extern const struct ng_parse_type ng_parse_int8_type;
extern const struct ng_parse_type ng_parse_int16_type;
extern const struct ng_parse_type ng_parse_int32_type;
extern const struct ng_parse_type ng_parse_int64_type;

/* Same thing but unparse as unsigned quantities */
extern const struct ng_parse_type ng_parse_uint8_type;
extern const struct ng_parse_type ng_parse_uint16_type;
extern const struct ng_parse_type ng_parse_uint32_type;
extern const struct ng_parse_type ng_parse_uint64_type;

/* Same thing but unparse as hex quantities, e.g., "0xe7" */
extern const struct ng_parse_type ng_parse_hint8_type;
extern const struct ng_parse_type ng_parse_hint16_type;
extern const struct ng_parse_type ng_parse_hint32_type;
extern const struct ng_parse_type ng_parse_hint64_type;

/*
 * IP ADDRESS TYPE
 *
 *   Default value:		0.0.0.0
 *   Additional info:		None required
 */
extern const struct ng_parse_type ng_parse_ipaddr_type;

/*
 * ETHERNET ADDRESS TYPE
 *
 *   Default value:		None
 *   Additional info:		None required
 */
extern const struct ng_parse_type ng_parse_enaddr_type;

/*
 * VARIABLE LENGTH BYTE ARRAY TYPE
 *
 * The bytes are displayed in hex.  The ASCII form may be either an
 * array of bytes or a string constant, in which case the array is
 * zero-filled after the string bytes.
 *
 *   Default value:		All bytes are zero
 *   Additional info:		ng_parse_array_getLength_t *
 */
extern const struct ng_parse_type ng_parse_bytearray_type;

/*
 * NETGRAPH CONTROL MESSAGE TYPE
 *
 * This is the parse type for a struct ng_mesg.
 *
 *   Default value:		All fields zero
 *   Additional info:		None required
 */
extern const struct ng_parse_type ng_parse_ng_mesg_type;

/************************************************************************
		CONVERSTION AND PARSING ROUTINES
 ************************************************************************/

/* Tokens for parsing structs and arrays */
enum ng_parse_token {
	T_LBRACE,		/* '{' */
	T_RBRACE,		/* '}' */
	T_LBRACKET,		/* '[' */
	T_RBRACKET,		/* ']' */
	T_EQUALS,		/* '=' */
	T_STRING,		/* string in double quotes */
	T_ERROR,		/* error parsing string in double quotes */
	T_WORD,			/* anything else containing no whitespace */
	T_EOF,			/* end of string reached */
};

/*
 * See typedef ng_parse_t for definition
 */
extern int	ng_parse(const struct ng_parse_type *type, const char *string,
			int *off, u_char *buf, int *buflen);

/*
 * See typedef ng_unparse_t for definition (*off assumed to be zero).
 */
extern int	ng_unparse(const struct ng_parse_type *type,
			const u_char *data, char *buf, int buflen);

/*
 * See typedef ng_getDefault_t for definition
 */
extern int	ng_parse_getDefault(const struct ng_parse_type *type,
			u_char *buf, int *buflen);

/*
 * Parse a token: '*startp' is the offset to start looking.  Upon
 * successful return, '*startp' equals the beginning of the token
 * and '*lenp' the length.  If error, '*startp' points at the
 * offending character(s).
 */
extern enum	ng_parse_token ng_parse_get_token(const char *s,
			int *startp, int *lenp);

/*
 * Like above, but specifically for getting a string token and returning
 * the string value.  The string token must be enclosed in double quotes
 * and the normal C backslash escapes are recognized.  The caller must
 * eventually free() the returned result.  Returns NULL if token is
 * not a string token, or parse or other error. Otherwise, *lenp contains
 * the number of characters parsed, and *slenp (if not NULL) contains
 * the actual number of characters in the parsed string.
 */
extern char	*ng_get_string_token(const char *s, int *startp,
			int *lenp, int *slenp);

/*
 * Convert a raw string into a doubly-quoted string including any
 * necessary backslash escapes.  Caller must free the result.
 * Returns NULL if ENOMEM. Normally "slen" should equal strlen(s)
 * unless you want to encode NUL bytes.
 */
extern char	*ng_encode_string(const char *s, int slen);

#endif /* _NETGRAPH_NG_PARSE_H_ */

