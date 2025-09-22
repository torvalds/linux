/*
 * a generic (simple) parser. Use to parse rr's, private key
 * information and /etc/resolv.conf files
 *
 * a Net::DNS like library for C
 * LibDNS Team @ NLnet Labs
 * (c) NLnet Labs, 2005-2006
 * See the file LICENSE for the license
 */
#include "config.h"
#include "sldns/parse.h"
#include "sldns/parseutil.h"
#include "sldns/sbuffer.h"

#include <limits.h>
#include <strings.h>

sldns_lookup_table sldns_directive_types[] = {
        { LDNS_DIR_TTL, "$TTL" },
        { LDNS_DIR_ORIGIN, "$ORIGIN" },
        { LDNS_DIR_INCLUDE, "$INCLUDE" },
        { 0, NULL }
};

/* add max_limit here? */
ssize_t
sldns_fget_token(FILE *f, char *token, const char *delim, size_t limit)
{
	return sldns_fget_token_l(f, token, delim, limit, NULL);
}

ssize_t
sldns_fget_token_l(FILE *f, char *token, const char *delim, size_t limit, int *line_nr)
{
	int c, prev_c;
	int p; /* 0 -> no parentheses seen, >0 nr of ( seen */
	int com, quoted, only_blank;
	char *t;
	size_t i;
	const char *d;
	const char *del;

	/* standard delimiters */
	if (!delim) {
		/* from isspace(3) */
		del = LDNS_PARSE_NORMAL;
	} else {
		del = delim;
	}

	p = 0;
	i = 0;
	com = 0;
	quoted = 0;
	prev_c = 0;
	only_blank = 1;	/* Assume we got only <blank> until now */
	t = token;
	if (del[0] == '"') {
		quoted = 1;
	}
	while ((c = getc(f)) != EOF) {
		if (c == '\r') /* carriage return */
			c = ' ';
		if (c == '(' && prev_c != '\\' && !quoted) {
			/* this only counts for non-comments */
			if (com == 0) {
				p++;
			}
			prev_c = c;
			continue;
		}

		if (c == ')' && prev_c != '\\' && !quoted) {
			/* this only counts for non-comments */
			if (com == 0) {
				p--;
			}
			prev_c = c;
			continue;
		}

		if (p < 0) {
			/* more ) then ( - close off the string */
			*t = '\0';
			return 0;
		}

		/* do something with comments ; */
		if (c == ';' && quoted == 0) {
			if (prev_c != '\\') {
				com = 1;
			}
		}
		if (c == '\"' && com == 0 && prev_c != '\\') {
			quoted = 1 - quoted;
		}

		if (c == '\n' && com != 0) {
			/* comments */
			com = 0;
			*t = ' ';
			if (line_nr) {
				*line_nr = *line_nr + 1;
			}
			if (only_blank && i > 0) {
				/* Got only <blank> so far. Reset and try
				 * again with the next line.
				 */
				i = 0;
				t = token;
			}
			if (p == 0) {
				/* If p != 0 then the next line is a continuation. So
				 * we assume that the next line starts with a blank only
				 * if it is actually a new line.
				 */
				only_blank = 1;	/* Assume next line starts with
						 * <blank>.
						 */
			}
			if (p == 0 && i > 0) {
				goto tokenread;
			} else {
				prev_c = c;
				continue;
			}
		}

		if (com == 1) {
			*t = ' ';
			prev_c = c;
			continue;
		}

		if (c == '\n' && p != 0 && t > token) {
			/* in parentheses */
			if (line_nr) {
				*line_nr = *line_nr + 1;
			}
			if (limit > 0 && (i+1 >= limit || (size_t)(t-token)+1 >= limit)) {
				*t = '\0';
				return -1;
			}
			*t++ = ' ';
			prev_c = c;
			continue;
		}

		/* check if we hit the delim */
		for (d = del; *d; d++) {
			if (c == *d)
				break;
		}

		if (c == *d && i > 0 && prev_c != '\\' && p == 0) {
			if (c == '\n' && line_nr) {
				*line_nr = *line_nr + 1;
			}
			if (only_blank) {
				/* Got only <blank> so far. Reset and
				 * try again with the next line.
				 */
				i = 0;
				t = token;
				only_blank = 1;
				prev_c = c;
				continue;
			}
			goto tokenread;
		}
		if (c != ' ' && c != '\t') {
			/* Found something that is not <blank> */
			only_blank= 0;
		}
		if (c != '\0' && c != '\n') {
			i++;
		}
		/* is there space for the character and the zero after it */
		if (limit > 0 && (i+1 >= limit || (size_t)(t-token)+1 >= limit)) {
			*t = '\0';
			return -1;
		}
		if (c != '\0' && c != '\n') {
			*t++ = c;
		}
		if (c == '\n') {
			if (line_nr) {
				*line_nr = *line_nr + 1;
			}
			only_blank = 1;	/* Assume next line starts with
					 * <blank>.
					 */
		}
		if (c == '\\' && prev_c == '\\')
			prev_c = 0;
		else	prev_c = c;
	}
	*t = '\0';
	if (c == EOF) {
		return (ssize_t)i;
	}

	if (i == 0) {
		/* nothing read */
		return -1;
	}
	if (p != 0) {
		return -1;
	}
	return (ssize_t)i;

tokenread:
	if(*del == '"')
		/* do not skip over quotes after the string, they are part
		 * of the next string.  But skip over whitespace (if needed)*/
		sldns_fskipcs_l(f, del+1, line_nr);
	else	sldns_fskipcs_l(f, del, line_nr);
	*t = '\0';
	if (p != 0) {
		return -1;
	}

	return (ssize_t)i;
}

ssize_t
sldns_fget_keyword_data(FILE *f, const char *keyword, const char *k_del, char *data,
               const char *d_del, size_t data_limit)
{
       return sldns_fget_keyword_data_l(f, keyword, k_del, data, d_del,
		       data_limit, NULL);
}

ssize_t
sldns_fget_keyword_data_l(FILE *f, const char *keyword, const char *k_del, char *data,
               const char *d_del, size_t data_limit, int *line_nr)
{
       /* we assume: keyword|sep|data */
       char *fkeyword;
       ssize_t i;

       if(strlen(keyword) >= LDNS_MAX_KEYWORDLEN)
               return -1;
       fkeyword = (char*)malloc(LDNS_MAX_KEYWORDLEN);
       if(!fkeyword)
               return -1;

       i = sldns_fget_token(f, fkeyword, k_del, LDNS_MAX_KEYWORDLEN);
       if(i==0 || i==-1) {
               free(fkeyword);
               return -1;
       }

       /* case??? i instead of strlen? */
       if (strncmp(fkeyword, keyword, LDNS_MAX_KEYWORDLEN - 1) == 0) {
               /* whee! */
               /* printf("%s\n%s\n", "Matching keyword", fkeyword); */
               i = sldns_fget_token_l(f, data, d_del, data_limit, line_nr);
               free(fkeyword);
               return i;
       } else {
               /*printf("no match for %s (read: %s)\n", keyword, fkeyword);*/
               free(fkeyword);
               return -1;
       }
}

int
sldns_bgetc(sldns_buffer *buffer)
{
	if (!sldns_buffer_available_at(buffer, buffer->_position, sizeof(uint8_t))) {
		sldns_buffer_set_position(buffer, sldns_buffer_limit(buffer));
		/* sldns_buffer_rewind(buffer);*/
		return EOF;
	}
	return (int)sldns_buffer_read_u8(buffer);
}

ssize_t
sldns_bget_token(sldns_buffer *b, char *token, const char *delim, size_t limit)
{
	return sldns_bget_token_par(b, token, delim, limit, NULL, NULL);
}

ssize_t
sldns_bget_token_par(sldns_buffer *b, char *token, const char *delim,
	size_t limit, int* par, const char* skipw)
{
	int c, lc;
	int p; /* 0 -> no parentheses seen, >0 nr of ( seen */
	int com, quoted;
	char *t;
	size_t i;
	const char *d;
	const char *del;

	/* standard delimiters */
	if (!delim) {
		/* from isspace(3) */
		del = LDNS_PARSE_NORMAL;
	} else {
		del = delim;
	}

	p = (par?*par:0);
	i = 0;
	com = 0;
	quoted = 0;
	t = token;
	lc = 0;
	if (del[0] == '"') {
		quoted = 1;
	}

	while ((c = sldns_bgetc(b)) != EOF) {
		if (c == '\r') /* carriage return */
			c = ' ';
		if (c == '(' && lc != '\\' && !quoted) {
			/* this only counts for non-comments */
			if (com == 0) {
				if(par) (*par)++;
				p++;
			}
			lc = c;
			continue;
		}

		if (c == ')' && lc != '\\' && !quoted) {
			/* this only counts for non-comments */
			if (com == 0) {
				if(par) (*par)--;
				p--;
			}
			lc = c;
			continue;
		}

		if (p < 0) {
			/* more ) then ( */
			*t = '\0';
			return 0;
		}

		/* do something with comments ; */
		if (c == ';' && quoted == 0) {
			if (lc != '\\') {
				com = 1;
			}
		}
		if (c == '"' && com == 0 && lc != '\\') {
			quoted = 1 - quoted;
		}

		if (c == '\n' && com != 0) {
			/* comments */
			com = 0;
			*t = ' ';
			lc = c;
			continue;
		}

		if (com == 1) {
			*t = ' ';
			lc = c;
			continue;
		}

		if (c == '\n' && p != 0) {
			/* in parentheses */
			/* do not write ' ' if we want to skip spaces */
			if(!(skipw && (strchr(skipw, c)||strchr(skipw, ' ')))) {
				/* check for space for the space character and a zero delimiter after that. */
				if (limit > 0 && (i+1 >= limit || (size_t)(t-token)+1 >= limit)) {
					*t = '\0';
					return -1;
				}
				*t++ = ' ';
			}
			lc = c;
			continue;
		}

		/* check to skip whitespace at start, but also after ( */
		if(skipw && i==0 && !com && !quoted && lc != '\\') {
			if(strchr(skipw, c)) {
				lc = c;
				continue;
			}
		}

		/* check if we hit the delim */
		for (d = del; *d; d++) {
			/* we can only exit if no parens or user tracks them */
                        if (c == *d && lc != '\\' && (p == 0 || par)) {
				goto tokenread;
                        }
		}

		i++;
		if (limit > 0 && (i+1 >= limit || (size_t)(t-token)+1 >= limit)) {
			*t = '\0';
			return -1;
		}
		*t++ = c;

		if (c == '\\' && lc == '\\') {
			lc = 0;
		} else {
			lc = c;
		}
	}
	*t = '\0';
	if (i == 0) {
		/* nothing read */
		return -1;
	}
	if (!par && p != 0) {
		return -1;
	}
	return (ssize_t)i;

tokenread:
	if(*del == '"')
		/* do not skip over quotes after the string, they are part
		 * of the next string.  But skip over whitespace (if needed)*/
		sldns_bskipcs(b, del+1);
	else 	sldns_bskipcs(b, del);
	*t = '\0';

	if (!par && p != 0) {
		return -1;
	}
	return (ssize_t)i;
}


void
sldns_bskipcs(sldns_buffer *buffer, const char *s)
{
        int found;
        char c;
        const char *d;

        while(sldns_buffer_available_at(buffer, buffer->_position, sizeof(char))) {
                c = (char) sldns_buffer_read_u8_at(buffer, buffer->_position);
                found = 0;
                for (d = s; *d; d++) {
                        if (*d == c) {
                                found = 1;
                        }
                }
                if (found && buffer->_limit > buffer->_position) {
                        buffer->_position += sizeof(char);
                } else {
                        return;
                }
        }
}

void
sldns_fskipcs(FILE *fp, const char *s)
{
	sldns_fskipcs_l(fp, s, NULL);
}

void
sldns_fskipcs_l(FILE *fp, const char *s, int *line_nr)
{
        int found;
        int c;
        const char *d;

	while ((c = fgetc(fp)) != EOF) {
		if (line_nr && c == '\n') {
			*line_nr = *line_nr + 1;
		}
                found = 0;
                for (d = s; *d; d++) {
                        if (*d == c) {
                                found = 1;
                        }
                }
		if (!found) {
			/* with getc, we've read too far */
			ungetc(c, fp);
			return;
		}
	}
}

ssize_t
sldns_bget_keyword_data(sldns_buffer *b, const char *keyword, const char *k_del, char
*data, const char *d_del, size_t data_limit)
{
       /* we assume: keyword|sep|data */
       char *fkeyword;
       ssize_t i;

       if(strlen(keyword) >= LDNS_MAX_KEYWORDLEN)
               return -1;
       fkeyword = (char*)malloc(LDNS_MAX_KEYWORDLEN);
       if(!fkeyword)
               return -1; /* out of memory */

       i = sldns_bget_token(b, fkeyword, k_del, data_limit);
       if(i==0 || i==-1) {
               free(fkeyword);
               return -1; /* nothing read */
       }

       /* case??? */
       if (strncmp(fkeyword, keyword, strlen(keyword)) == 0) {
               free(fkeyword);
               /* whee, the match! */
               /* retrieve it's data */
               i = sldns_bget_token(b, data, d_del, 0);
               return i;
       } else {
               free(fkeyword);
               return -1;
       }
}

