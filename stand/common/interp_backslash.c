/*-
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * Jordan K. Hubbard
 * 29 August 1998
 *
 * Routine for doing backslash elimination.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <string.h>
#include "bootstrap.h"

#define	DIGIT(x) (isdigit(x) ? (x) - '0' : islower(x) ? (x) + 10 - 'a' : (x) + 10 - 'A')

/*
 * backslash: Return malloc'd copy of str with all standard "backslash
 * processing" done on it.  Original can be free'd if desired.
 */
char *
backslash(const char *str)
{
	/*
	 * Remove backslashes from the strings. Turn \040 etc. into a single
	 * character (we allow eight bit values). Currently NUL is not
	 * allowed.
	 *
	 * Turn "\n" and "\t" into '\n' and '\t' characters. Etc.
	 *
	 */
	char *new_str;
	int seenbs = 0;
	int i = 0;

	if ((new_str = strdup(str)) == NULL)
		return NULL;

	while (*str) {
		if (seenbs) {
			seenbs = 0;
			switch (*str) {
			case '\\':
				new_str[i++] = '\\';
				str++;
				break;

				/* preserve backslashed quotes, dollar signs */
			case '\'':
			case '"':
			case '$':
				new_str[i++] = '\\';
				new_str[i++] = *str++;
				break;

			case 'b':
				new_str[i++] = '\b';
				str++;
				break;

			case 'f':
				new_str[i++] = '\f';
				str++;
				break;

			case 'r':
				new_str[i++] = '\r';
				str++;
				break;

			case 'n':
				new_str[i++] = '\n';
				str++;
				break;

			case 's':
				new_str[i++] = ' ';
				str++;
				break;

			case 't':
				new_str[i++] = '\t';
				str++;
				break;

			case 'v':
				new_str[i++] = '\13';
				str++;
				break;

			case 'z':
				str++;
				break;

			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9': {
				char val;

				/* Three digit octal constant? */
				if (*str >= '0' && *str <= '3' && 
				    *(str + 1) >= '0' && *(str + 1) <= '7' &&
				    *(str + 2) >= '0' && *(str + 2) <= '7') {

					val = (DIGIT(*str) << 6) + (DIGIT(*(str + 1)) << 3) + 
					    DIGIT(*(str + 2));

					/* Allow null value if user really wants to shoot
					   at feet, but beware! */
					new_str[i++] = val;
					str += 3;
					break;
				}

				/* One or two digit hex constant?
				 * If two are there they will both be taken.
				 * Use \z to split them up if this is not wanted.
				 */
				if (*str == '0' &&
				    (*(str + 1) == 'x' || *(str + 1) == 'X') &&
				    isxdigit(*(str + 2))) {
					val = DIGIT(*(str + 2));
					if (isxdigit(*(str + 3))) {
						val = (val << 4) + DIGIT(*(str + 3));
						str += 4;
					}
					else
						str += 3;
					/* Yep, allow null value here too */
					new_str[i++] = val;
					break;
				}
			}
				break;

			default:
				new_str[i++] = *str++;
				break;
			}
		}
		else {
			if (*str == '\\') {
				seenbs = 1;
				str++;
			}
			else
				new_str[i++] = *str++;
		}
	}

	if (seenbs) {
		/*
		 * The final character was a '\'. Put it in as a single backslash.
		 */
		new_str[i++] = '\\';
	}
	new_str[i] = '\0';
	return new_str;
}
