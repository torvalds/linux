/* $OpenBSD: read_entry.c,v 1.19 2024/04/12 14:10:28 millert Exp $ */

/****************************************************************************
 * Copyright 2018-2022,2023 Thomas E. Dickey                                *
 * Copyright 1998-2016,2017 Free Software Foundation, Inc.                  *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
 *	read_entry.c -- Routine for reading in a compiled terminfo file
 */

#include <curses.priv.h>
#include <hashed_db.h>

#include <tic.h>

MODULE_ID("$Id: read_entry.c,v 1.19 2024/04/12 14:10:28 millert Exp $")

#define MyNumber(n) (short) LOW_MSB(n)

#define SIZEOF_32BITS 4

#if NCURSES_USE_DATABASE
#if NCURSES_EXT_NUMBERS
static size_t
convert_16bits(char *buf, NCURSES_INT2 *Numbers, int count)
{
    int i;
    size_t j;
    size_t size = SIZEOF_SHORT;
    for (i = 0; i < count; i++) {
	unsigned mask = 0xff;
	unsigned char ch = 0;
	Numbers[i] = 0;
	for (j = 0; j < size; ++j) {
	    ch = UChar(*buf++);
	    Numbers[i] |= (ch << (8 * j));
	    mask <<= 8;
	}
	if (ch & 0x80) {
	    while (mask != 0) {
		Numbers[i] |= (int) mask;
		mask <<= 8;
	    }
	}
	TR(TRACE_DATABASE, ("get Numbers[%d]=%d", i, Numbers[i]));
    }
    return size;
}

static size_t
convert_32bits(char *buf, NCURSES_INT2 *Numbers, int count)
{
    int i;
    size_t j;
    size_t size = SIZEOF_INT2;
    unsigned char ch;

    assert(sizeof(NCURSES_INT2) == size);
    for (i = 0; i < count; i++) {
	Numbers[i] = 0;
	for (j = 0; j < size; ++j) {
	    ch = UChar(*buf++);
	    Numbers[i] |= (ch << (8 * j));
	}
	/* "unsigned" and NCURSES_INT2 are the same size - no sign-extension */
	TR(TRACE_DATABASE, ("get Numbers[%d]=%d", i, Numbers[i]));
    }
    return size;
}
#else
static size_t
convert_32bits(char *buf, NCURSES_INT2 *Numbers, int count)
{
    int i, j;
    unsigned char ch;
    for (i = 0; i < count; i++) {
	int value = 0;
	for (j = 0; j < SIZEOF_32BITS; ++j) {
	    ch = UChar(*buf++);
	    value |= (ch << (8 * j));
	}
	if (value == -1)
	    Numbers[i] = ABSENT_NUMERIC;
	else if (value == -2)
	    Numbers[i] = CANCELLED_NUMERIC;
	else if (value > MAX_OF_TYPE(NCURSES_INT2))
	    Numbers[i] = MAX_OF_TYPE(NCURSES_INT2);
	else
	    Numbers[i] = (short) value;
	TR(TRACE_DATABASE, ("get Numbers[%d]=%d", i, Numbers[i]));
    }
    return SIZEOF_SHORT;
}

static size_t
convert_16bits(char *buf, NCURSES_INT2 *Numbers, int count)
{
    int i;
    for (i = 0; i < count; i++) {
	if (IS_NEG1(buf + 2 * i))
	    Numbers[i] = ABSENT_NUMERIC;
	else if (IS_NEG2(buf + 2 * i))
	    Numbers[i] = CANCELLED_NUMERIC;
	else
	    Numbers[i] = MyNumber(buf + 2 * i);
	TR(TRACE_DATABASE, ("get Numbers[%d]=%d", i, Numbers[i]));
    }
    return SIZEOF_SHORT;
}
#endif

static bool
convert_strings(char *buf, char **Strings, int count, int size,
		char *table, bool always)
{
    int i;
    char *p;
    bool success = TRUE;

    for (i = 0; i < count; i++) {
	if (IS_NEG1(buf + 2 * i)) {
	    Strings[i] = ABSENT_STRING;
	} else if (IS_NEG2(buf + 2 * i)) {
	    Strings[i] = CANCELLED_STRING;
	} else if (MyNumber(buf + 2 * i) > size) {
	    Strings[i] = ABSENT_STRING;
	} else {
	    int nn = MyNumber(buf + 2 * i);
	    if (nn >= 0 && nn < size) {
		Strings[i] = (nn + table);
		TR(TRACE_DATABASE, ("Strings[%d] = %s", i,
				    _nc_visbuf(Strings[i])));
	    } else {
		TR(TRACE_DATABASE,
		   ("found out-of-range index %d to Strings[%d]", nn, i));
		success = FALSE;
		break;
	    }
	}

	/* make sure all strings are NUL terminated */
	if (VALID_STRING(Strings[i])) {
	    for (p = Strings[i]; p < table + size; p++)
		if (*p == '\0')
		    break;
	    /* if there is no NUL, ignore the string */
	    if (p >= table + size) {
		Strings[i] = ABSENT_STRING;
	    } else if (p == Strings[i] && always) {
		TR(TRACE_DATABASE,
		   ("found empty but required Strings[%d]", i));
		success = FALSE;
		break;
	    }
	} else if (always) {	/* names are always needed */
	    TR(TRACE_DATABASE,
	       ("found invalid but required Strings[%d]", i));
	    success = FALSE;
	    break;
	}
    }
    if (!success) {
	_nc_warning("corrupt data found in convert_strings");
    }
    return success;
}

static int
fake_read(char *src, int *offset, int limit, char *dst, unsigned want)
{
    int have = (limit - *offset);

    if (have > 0) {
	if ((int) want > have)
	    want = (unsigned) have;
	memcpy(dst, src + *offset, (size_t) want);
	*offset += (int) want;
    } else {
	want = 0;
    }
    return (int) want;
}

#define Read(buf, count) fake_read(buffer, &offset, limit, (char *) buf, (unsigned) count)

#define read_shorts(buf, count) \
	(Read(buf, (count)*SIZEOF_SHORT) == (int) (count)*SIZEOF_SHORT)

#define read_numbers(buf, count) \
	(Read(buf, (count)*(unsigned)size_of_numbers) == (int) (count)*size_of_numbers)

#define even_boundary(value) \
    if ((value) % 2 != 0) Read(buf, 1)
#endif

NCURSES_EXPORT(void)
_nc_init_termtype(TERMTYPE2 *const tp)
{
    unsigned i;

    DEBUG(2, (T_CALLED("_nc_init_termtype(tp=%p)"), (void *) tp));

#if NCURSES_XNAMES
    tp->num_Booleans = BOOLCOUNT;
    tp->num_Numbers = NUMCOUNT;
    tp->num_Strings = STRCOUNT;
    tp->ext_Booleans = 0;
    tp->ext_Numbers = 0;
    tp->ext_Strings = 0;
#endif
    if (tp->Booleans == 0)
	TYPE_MALLOC(NCURSES_SBOOL, BOOLCOUNT, tp->Booleans);
    if (tp->Numbers == 0)
	TYPE_MALLOC(NCURSES_INT2, NUMCOUNT, tp->Numbers);
    if (tp->Strings == 0)
	TYPE_MALLOC(char *, STRCOUNT, tp->Strings);

    for_each_boolean(i, tp)
	tp->Booleans[i] = FALSE;

    for_each_number(i, tp)
	tp->Numbers[i] = ABSENT_NUMERIC;

    for_each_string(i, tp)
	tp->Strings[i] = ABSENT_STRING;

    DEBUG(2, (T_RETURN("")));
}

#if NCURSES_USE_DATABASE
#if NCURSES_XNAMES
static bool
valid_shorts(char *buffer, int limit)
{
    bool result = FALSE;
    int n;
    for (n = 0; n < limit; ++n) {
	if (MyNumber(buffer + (n * 2)) > 0) {
	    result = TRUE;
	    break;
	}
    }
    return result;
}
#endif

/*
 * Return TGETENT_YES if read, TGETENT_NO if not found or garbled.
 */
NCURSES_EXPORT(int)
_nc_read_termtype(TERMTYPE2 *ptr, char *buffer, int limit)
{
    int offset = 0;
    int name_size, bool_count, num_count, str_count, str_size;
    int i;
    char buf[MAX_ENTRY_SIZE + 2];
    char *string_table;
    unsigned want, have;
    size_t (*convert_numbers) (char *, NCURSES_INT2 *, int);
    int size_of_numbers;
    int max_entry_size = MAX_ENTRY_SIZE;

    TR(TRACE_DATABASE,
       (T_CALLED("_nc_read_termtype(ptr=%p, buffer=%p, limit=%d)"),
	(void *) ptr, buffer, limit));

    TR(TRACE_DATABASE, ("READ termtype header @%d", offset));

    memset(ptr, 0, sizeof(*ptr));

    /* grab the header */
    if (!read_shorts(buf, 6)
	|| !IS_TIC_MAGIC(buf)) {
	returnDB(TGETENT_NO);
    }
#if NCURSES_EXT_NUMBERS
    if (LOW_MSB(buf) == MAGIC2) {
	convert_numbers = convert_32bits;
	size_of_numbers = SIZEOF_INT2;
    } else {
	max_entry_size = MAX_ENTRY_SIZE1;
	convert_numbers = convert_16bits;
	size_of_numbers = SIZEOF_SHORT;
    }
#else
    if (LOW_MSB(buf) == MAGIC2) {
	convert_numbers = convert_32bits;
	size_of_numbers = SIZEOF_32BITS;
    } else {
	convert_numbers = convert_16bits;
	size_of_numbers = SIZEOF_INT2;
    }
#endif

    /* *INDENT-EQLS* */
    name_size  = MyNumber(buf + 2);
    bool_count = MyNumber(buf + 4);
    num_count  = MyNumber(buf + 6);
    str_count  = MyNumber(buf + 8);
    str_size   = MyNumber(buf + 10);

    TR(TRACE_DATABASE,
       ("TERMTYPE name_size=%d, bool=%d/%d, num=%d/%d str=%d/%d(%d)",
	name_size, bool_count, BOOLCOUNT, num_count, NUMCOUNT,
	str_count, STRCOUNT, str_size));
    if (name_size < 0
	|| bool_count < 0
	|| num_count < 0
	|| str_count < 0
	|| bool_count > BOOLCOUNT
	|| num_count > NUMCOUNT
	|| str_count > STRCOUNT
	|| str_size < 0) {
	returnDB(TGETENT_NO);
    }

    want = (unsigned) (str_size + name_size + 1);
    /* try to allocate space for the string table */
    if (str_count * SIZEOF_SHORT >= max_entry_size
	|| (string_table = typeMalloc(char, want)) == 0) {
	returnDB(TGETENT_NO);
    }

    /* grab the name (a null-terminated string) */
    want = min(MAX_NAME_SIZE, (unsigned) name_size);
    ptr->str_table = string_table;
    ptr->term_names = string_table;
    if ((have = (unsigned) Read(ptr->term_names, want)) != want) {
	memset(ptr->term_names + have, 0, (size_t) (want - have));
    }
    ptr->term_names[want] = '\0';
    string_table += (want + 1);

    if (have > MAX_NAME_SIZE)
	offset = (int) (have - MAX_NAME_SIZE);

    /* grab the booleans */
    TYPE_CALLOC(NCURSES_SBOOL, max(BOOLCOUNT, bool_count), ptr->Booleans);
    if (Read(ptr->Booleans, (unsigned) bool_count) < bool_count) {
	returnDB(TGETENT_NO);
    }

    /*
     * If booleans end on an odd byte, skip it.  The machine they
     * originally wrote terminfo on must have been a 16-bit
     * word-oriented machine that would trap out if you tried a
     * word access off a 2-byte boundary.
     */
    even_boundary(name_size + bool_count);

    /* grab the numbers */
    TYPE_CALLOC(NCURSES_INT2, max(NUMCOUNT, num_count), ptr->Numbers);
    if (!read_numbers(buf, num_count)) {
	returnDB(TGETENT_NO);
    }
    convert_numbers(buf, ptr->Numbers, num_count);

    TYPE_CALLOC(char *, max(STRCOUNT, str_count), ptr->Strings);

    if (str_count) {
	/* grab the string offsets */
	if (!read_shorts(buf, str_count)) {
	    returnDB(TGETENT_NO);
	}
	/* finally, grab the string table itself */
	if (Read(string_table, (unsigned) str_size) != str_size) {
	    returnDB(TGETENT_NO);
	}
	if (!convert_strings(buf, ptr->Strings, str_count, str_size,
			     string_table, FALSE)) {
	    returnDB(TGETENT_NO);
	}
    }
#if NCURSES_XNAMES

    ptr->num_Booleans = BOOLCOUNT;
    ptr->num_Numbers = NUMCOUNT;
    ptr->num_Strings = STRCOUNT;

    /*
     * Read extended entries, if any, after the normal end of terminfo data.
     */
    even_boundary(str_size);
    TR(TRACE_DATABASE, ("READ extended_header @%d", offset));
    if (_nc_user_definable && read_shorts(buf, 5) && valid_shorts(buf, 5)) {
	int ext_bool_count = MyNumber(buf + 0);
	int ext_num_count = MyNumber(buf + 2);
	int ext_str_count = MyNumber(buf + 4);
	int ext_str_usage = MyNumber(buf + 6);
	int ext_str_limit = MyNumber(buf + 8);
	unsigned need = (unsigned) (ext_bool_count + ext_num_count + ext_str_count);
	int base = 0;

	if ((int) need >= (max_entry_size / 2)
	    || ext_str_usage >= max_entry_size
	    || ext_str_limit >= max_entry_size
	    || ext_bool_count < 0
	    || ext_num_count < 0
	    || ext_str_count < 0
	    || ext_str_usage < 0
	    || ext_str_limit < 0) {
	    returnDB(TGETENT_NO);
	}

	ptr->num_Booleans = UShort(BOOLCOUNT + ext_bool_count);
	ptr->num_Numbers = UShort(NUMCOUNT + ext_num_count);
	ptr->num_Strings = UShort(STRCOUNT + ext_str_count);

	TYPE_REALLOC(NCURSES_SBOOL, ptr->num_Booleans, ptr->Booleans);
	TYPE_REALLOC(NCURSES_INT2, ptr->num_Numbers, ptr->Numbers);
	TYPE_REALLOC(char *, ptr->num_Strings, ptr->Strings);

	TR(TRACE_DATABASE, ("extended header: "
			    "bool %d, "
			    "number %d, "
			    "string %d(%d:%d)",
			    ext_bool_count,
			    ext_num_count,
			    ext_str_count,
			    ext_str_usage,
			    ext_str_limit));

	TR(TRACE_DATABASE, ("READ %d extended-booleans @%d",
			    ext_bool_count, offset));
	if ((ptr->ext_Booleans = UShort(ext_bool_count)) != 0) {
	    if (Read(ptr->Booleans + BOOLCOUNT, (unsigned)
		     ext_bool_count) != ext_bool_count) {
		returnDB(TGETENT_NO);
	    }
	}
	even_boundary(ext_bool_count);

	TR(TRACE_DATABASE, ("READ %d extended-numbers @%d",
			    ext_num_count, offset));
	if ((ptr->ext_Numbers = UShort(ext_num_count)) != 0) {
	    if (!read_numbers(buf, ext_num_count)) {
		returnDB(TGETENT_NO);
	    }
	    TR(TRACE_DATABASE, ("Before converting extended-numbers"));
	    convert_numbers(buf, ptr->Numbers + NUMCOUNT, ext_num_count);
	}

	TR(TRACE_DATABASE, ("READ extended-offsets @%d", offset));
	if ((ext_str_count + (int) need) >= (max_entry_size / 2)) {
	    returnDB(TGETENT_NO);
	}
	if ((ext_str_count || need)
	    && !read_shorts(buf, ext_str_count + (int) need)) {
	    returnDB(TGETENT_NO);
	}

	TR(TRACE_DATABASE, ("READ %d bytes of extended-strings @%d",
			    ext_str_limit, offset));

	if (ext_str_limit) {
	    ptr->ext_str_table = typeMalloc(char, (size_t) ext_str_limit);
	    if (ptr->ext_str_table == 0) {
		returnDB(TGETENT_NO);
	    }
	    if (Read(ptr->ext_str_table, (unsigned) ext_str_limit) != ext_str_limit) {
		returnDB(TGETENT_NO);
	    }
	    TR(TRACE_DATABASE, ("first extended-string is %s", _nc_visbuf(ptr->ext_str_table)));
	}

	if ((ptr->ext_Strings = UShort(ext_str_count)) != 0) {
	    int check = (ext_bool_count + ext_num_count + ext_str_count);

	    TR(TRACE_DATABASE,
	       ("Before computing extended-string capabilities "
		"str_count=%d, ext_str_count=%d",
		str_count, ext_str_count));
	    if (!convert_strings(buf, ptr->Strings + str_count, ext_str_count,
				 ext_str_limit, ptr->ext_str_table, FALSE)) {
		returnDB(TGETENT_NO);
	    }
	    for (i = ext_str_count - 1; i >= 0; i--) {
		TR(TRACE_DATABASE, ("MOVE from [%d:%d] %s",
				    i, i + str_count,
				    _nc_visbuf(ptr->Strings[i + str_count])));
		ptr->Strings[i + STRCOUNT] = ptr->Strings[i + str_count];
		if (VALID_STRING(ptr->Strings[i + STRCOUNT])) {
		    base += (int) (strlen(ptr->Strings[i + STRCOUNT]) + 1);
		    ++check;
		}
		TR(TRACE_DATABASE, ("... to    [%d] %s",
				    i + STRCOUNT,
				    _nc_visbuf(ptr->Strings[i + STRCOUNT])));
	    }
	    TR(TRACE_DATABASE, ("Check table-size: %d/%d", check, ext_str_usage));
#if 0
	    /*
	     * Phasing in a proper check will be done "later".
	     */
	    if (check != ext_str_usage)
		returnDB(TGETENT_NO);
#endif
	}

	if (need) {
	    if (ext_str_count >= (max_entry_size / 2)) {
		returnDB(TGETENT_NO);
	    }
	    TYPE_CALLOC(char *, need, ptr->ext_Names);
	    TR(TRACE_DATABASE,
	       ("ext_NAMES starting @%d in extended_strings, first = %s",
		base, _nc_visbuf(ptr->ext_str_table + base)));
	    if (!convert_strings(buf + (2 * ext_str_count),
				 ptr->ext_Names,
				 (int) need,
				 ext_str_limit, ptr->ext_str_table + base,
				 TRUE)) {
		returnDB(TGETENT_NO);
	    }
	}

	TR(TRACE_DATABASE,
	   ("...done reading terminfo bool %d(%d) num %d(%d) str %d(%d)",
	    ptr->num_Booleans, ptr->ext_Booleans,
	    ptr->num_Numbers, ptr->ext_Numbers,
	    ptr->num_Strings, ptr->ext_Strings));

	TR(TRACE_DATABASE, ("extend: num_Booleans:%d", ptr->num_Booleans));
    } else
#endif /* NCURSES_XNAMES */
    {
	TR(TRACE_DATABASE, ("...done reading terminfo bool %d num %d str %d",
			    bool_count, num_count, str_count));
#if NCURSES_XNAMES
	TR(TRACE_DATABASE, ("normal: num_Booleans:%d", ptr->num_Booleans));
#endif
    }

    for (i = bool_count; i < BOOLCOUNT; i++)
	ptr->Booleans[i] = FALSE;
    for (i = num_count; i < NUMCOUNT; i++)
	ptr->Numbers[i] = ABSENT_NUMERIC;
    for (i = str_count; i < STRCOUNT; i++)
	ptr->Strings[i] = ABSENT_STRING;

    returnDB(TGETENT_YES);
}

/*
 *	int
 *	_nc_read_file_entry(filename, ptr)
 *
 *	Read the compiled terminfo entry in the given file into the
 *	structure pointed to by ptr, allocating space for the string
 *	table.
 */
NCURSES_EXPORT(int)
_nc_read_file_entry(const char *const filename, TERMTYPE2 *ptr)
/* return 1 if read, 0 if not found or garbled */
{
    FILE *fp = 0;
    int code;

    if (_nc_access(filename, R_OK) < 0
	|| (fp = safe_fopen(filename, BIN_R)) == 0) {
	TR(TRACE_DATABASE, ("cannot open terminfo %s (errno=%d)", filename, errno));
	code = TGETENT_NO;
    } else {
	int limit;
	char buffer[MAX_ENTRY_SIZE + 1];

	limit = (int) fread(buffer, sizeof(char), sizeof(buffer), fp);
	if (limit > 0) {
	    const char *old_source = _nc_get_source();

	    TR(TRACE_DATABASE, ("read terminfo %s", filename));
	    if (old_source == NULL)
		_nc_set_source(filename);
	    if ((code = _nc_read_termtype(ptr, buffer, limit)) == TGETENT_NO) {
		_nc_free_termtype2(ptr);
	    }
	    _nc_set_source(old_source);
	} else {
	    code = TGETENT_NO;
	}
	fclose(fp);
    }

    return (code);
}

#if USE_HASHED_DB
/*
 * Return if if we can build the filename of a ".db" file.
 */
static bool
make_db_filename(char *filename, unsigned limit, const char *const path)
{
    static const char suffix[] = DBM_SUFFIX;

    size_t lens = sizeof(suffix) - 1;
    size_t size = strlen(path);
    size_t test = lens + size;
    bool result = FALSE;

    if (test < limit) {
	if (size >= lens
	    && !strcmp(path + size - lens, suffix))
	    _nc_STRCPY(filename, path, limit);
	else
	    _nc_SPRINTF(filename, _nc_SLIMIT(limit) "%s%s", path, suffix);
	result = TRUE;
    }
    return result;
}
#endif

/*
 * Return true if we can build the name of a filesystem entry.
 */
static bool
make_dir_filename(char *filename,
		  unsigned limit,
		  const char *const path,
		  const char *name)
{
    bool result = FALSE;

#if NCURSES_USE_TERMCAP
    if (_nc_is_dir_path(path))
#endif
    {
	unsigned need = (unsigned) (LEAF_LEN + 3 + strlen(path) + strlen(name));

	if (need <= limit) {
	    _nc_SPRINTF(filename, _nc_SLIMIT(limit)
			"%s/" LEAF_FMT "/%s", path, *name, name);
	    result = TRUE;
	}
    }
    return result;
}

static int
lookup_b64(int *target, const char **source)
{
    int result = 3;
    int j;
    /*
     * ncurses' quickdump writes only RFC 4648 "url/filename-safe" encoding,
     * but accepts RFC-3548
     */
    for (j = 0; j < 4; ++j) {
	int ch = UChar(**source);
	*source += 1;
	if (ch >= 'A' && ch <= 'Z') {
	    target[j] = (ch - 'A');
	} else if (ch >= 'a' && ch <= 'z') {
	    target[j] = 26 + (ch - 'a');
	} else if (ch >= '0' && ch <= '9') {
	    target[j] = 52 + (ch - '0');
	} else if (ch == '-' || ch == '+') {
	    target[j] = 62;
	} else if (ch == '_' || ch == '/') {
	    target[j] = 63;
	} else if (ch == '=') {
	    target[j] = 64;
	    result--;
	} else {
	    result = -1;
	    break;
	}
    }
    return result;
}

static int
decode_hex(const char **source)
{
    int result = 0;
    int nibble;

    for (nibble = 0; nibble < 2; ++nibble) {
	int ch = UChar(**source);
	result <<= 4;
	*source += 1;
	if (ch >= '0' && ch <= '9') {
	    ch -= '0';
	} else if (ch >= 'A' && ch <= 'F') {
	    ch -= 'A';
	    ch += 10;
	} else if (ch >= 'a' && ch <= 'f') {
	    ch -= 'a';
	    ch += 10;
	} else {
	    result = -1;
	    break;
	}
	result |= ch;
    }
    return result;
}

static int
decode_quickdump(char *target, const char *source)
{
    char *base = target;
    int result = 0;

    if (!strncmp(source, "b64:", (size_t) 4)) {
	source += 4;
	while (*source != '\0') {
	    int bits[4];
	    int ch = lookup_b64(bits, &source);
	    if (ch < 0 || (ch + target - base) >= MAX_ENTRY_SIZE) {
		result = 0;
		break;
	    }
	    result += ch;
	    *target++ = (char) ((bits[0] << 2) | (bits[1] >> 4));
	    if (bits[2] < 64) {
		*target++ = (char) ((bits[1] << 4) | (bits[2] >> 2));
		if (bits[3] < 64) {
		    *target++ = (char) ((bits[2] << 6) | bits[3]);
		}
	    }
	}
    } else if (!strncmp(source, "hex:", (size_t) 4)) {
	source += 4;
	while (*source != '\0') {
	    int ch = decode_hex(&source);
	    if (ch < 0 || (target - base) >= MAX_ENTRY_SIZE) {
		result = 0;
		break;
	    }
	    *target++ = (char) ch;
	    ++result;
	}
    }
    return result;
}

/*
 * Build a terminfo pathname and try to read the data.  Returns TGETENT_YES on
 * success, TGETENT_NO on failure.
 */
static int
_nc_read_tic_entry(char *filename,
		   unsigned limit,
		   const char *const path,
		   const char *name,
		   TERMTYPE2 *const tp)
{
    int code = TGETENT_NO;
#if USE_HASHED_DB
    DB *capdbp;
#endif
    char buffer[MAX_ENTRY_SIZE + 1];
    int used;

    TR(TRACE_DATABASE,
       (T_CALLED("_nc_read_tic_entry(file=%p, path=%s, name=%s)"),
	filename, path, name));

    assert(TGETENT_YES == TRUE);	/* simplify call for _nc_name_match */

    if ((used = decode_quickdump(buffer, path)) != 0
	&& (code = _nc_read_termtype(tp, buffer, used)) == TGETENT_YES
	&& (code = _nc_name_match(tp->term_names, name, "|")) == TGETENT_YES) {
	TR(TRACE_DATABASE, ("loaded quick-dump for %s", name));
	/* shorten name shown by infocmp */
	_nc_STRCPY(filename, "$TERMINFO", limit);
    } else
#if USE_HASHED_DB
	if (make_db_filename(filename, limit, path)
	    && (capdbp = _nc_db_open(filename, FALSE)) != 0) {

	DBT key, data;
	int reccnt = 0;
	char *save = strdup(name);

	if (save == 0)
	    returnDB(code);

	memset(&key, 0, sizeof(key));
	key.data = save;
	key.size = strlen(save);

	/*
	 * This lookup could return termcap data, which we do not want.  We are
	 * looking for compiled (binary) terminfo data.
	 *
	 * cgetent uses a two-level lookup.  On the first it uses the given
	 * name to return a record containing only the aliases for an entry.
	 * On the second (using that list of aliases as a key), it returns the
	 * content of the terminal description.  We expect second lookup to
	 * return data beginning with the same set of aliases.
	 *
	 * For compiled terminfo, the list of aliases in the second case will
	 * be null-terminated.  A termcap entry will not be, and will run on
	 * into the description.  So we can easily distinguish between the two
	 * (source/binary) by checking the lengths.
	 */
	while (_nc_db_get(capdbp, &key, &data) == 0) {
	    char *have = (char *) data.data;
	    used = (int) data.size - 1;

	    if (*have++ == 0) {
		if (data.size > key.size
		    && IS_TIC_MAGIC(have)) {
		    code = _nc_read_termtype(tp, have, used);
		    if (code == TGETENT_NO) {
			_nc_free_termtype2(tp);
		    }
		}
		break;
	    }

	    /*
	     * Just in case we have a corrupt database, do not waste time with
	     * it.
	     */
	    if (++reccnt >= 3)
		break;

	    /*
	     * Prepare for the second level.
	     */
	    key.data = have;
	    key.size = used;
	}

	free(save);
    } else			/* may be either filesystem or flat file */
#endif
    if (make_dir_filename(filename, limit, path, name)) {
	code = _nc_read_file_entry(filename, tp);
    }
#if NCURSES_USE_TERMCAP
    if (code != TGETENT_YES) {
	const char *source = _nc_get_source();
	code = _nc_read_termcap_entry(name, tp);
	_nc_SPRINTF(filename, _nc_SLIMIT(PATH_MAX)
		    "%.*s", PATH_MAX - 1, source ? source : "");
    }
#endif
    returnDB(code);
}
#endif /* NCURSES_USE_DATABASE */

/*
 * Find and read the compiled entry for a given terminal type, if it exists.
 * We take pains here to make sure no combination of environment variables and
 * terminal type name can be used to overrun the file buffer.
 */
NCURSES_EXPORT(int)
_nc_read_entry2(const char *const name, char *const filename, TERMTYPE2 *const tp)
{
    int code = TGETENT_NO;

    if (name == 0)
	return _nc_read_entry2("", filename, tp);

    _nc_SPRINTF(filename, _nc_SLIMIT(PATH_MAX)
		"%.*s", PATH_MAX - 1, name);

    if (strlen(name) == 0
	|| strcmp(name, ".") == 0
	|| strcmp(name, "..") == 0
	|| _nc_pathlast(name) != 0
	|| strchr(name, NCURSES_PATHSEP) != 0) {
	TR(TRACE_DATABASE, ("illegal or missing entry name '%s'", name));
    } else {
#if NCURSES_USE_DATABASE
	DBDIRS state;
	int offset;
	const char *path;

	_nc_first_db(&state, &offset);
	code = TGETENT_ERR;
	while ((path = _nc_next_db(&state, &offset)) != 0) {
	    code = _nc_read_tic_entry(filename, PATH_MAX, path, name, tp);
	    if (code == TGETENT_YES) {
		_nc_last_db();
		break;
	    }
	}
#elif NCURSES_USE_TERMCAP
	if (code != TGETENT_YES) {
	    const char *source = _nc_get_source();
	    code = _nc_read_termcap_entry(name, tp);
	    _nc_SPRINTF(filename, _nc_SLIMIT(PATH_MAX)
			"%.*s", PATH_MAX - 1, source ? source : "");
	}
#endif
    }
    return code;
}

#if NCURSES_EXT_NUMBERS
NCURSES_EXPORT(int)
_nc_read_entry(const char *const name, char *const filename, TERMTYPE *const tp)
{
    TERMTYPE2 dummy;
    int rc;
    rc = _nc_read_entry2(name, filename, &dummy);
    if (rc == TGETENT_YES)
	_nc_export_termtype2(tp, &dummy);
    return rc;
}
#endif
