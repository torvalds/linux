/*
 * Copyright (c) 1987 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of California at Berkeley. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific written prior permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

/*
  
@deftypefn Supplemental int strcasecmp (const char *@var{s1}, const char *@var{s2})

A case-insensitive @code{strcmp}.

@end deftypefn

*/

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)strcasecmp.c	5.5 (Berkeley) 11/24/87";
#endif /* LIBC_SCCS and not lint */

#include <ansidecl.h>
#include <stddef.h>

/*
 * This array is designed for mapping upper and lower case letter
 * together for a case independent comparison.  The mappings are
 * based upon ascii character sequences.
 */
typedef unsigned char uc;
static const unsigned char charmap[] = {
	(uc)'\000',(uc)'\001',(uc)'\002',(uc)'\003',(uc)'\004',(uc)'\005',(uc)'\006',(uc)'\007',
	(uc)'\010',(uc)'\011',(uc)'\012',(uc)'\013',(uc)'\014',(uc)'\015',(uc)'\016',(uc)'\017',
	(uc)'\020',(uc)'\021',(uc)'\022',(uc)'\023',(uc)'\024',(uc)'\025',(uc)'\026',(uc)'\027',
	(uc)'\030',(uc)'\031',(uc)'\032',(uc)'\033',(uc)'\034',(uc)'\035',(uc)'\036',(uc)'\037',
	(uc)'\040',(uc)'\041',(uc)'\042',(uc)'\043',(uc)'\044',(uc)'\045',(uc)'\046',(uc)'\047',
	(uc)'\050',(uc)'\051',(uc)'\052',(uc)'\053',(uc)'\054',(uc)'\055',(uc)'\056',(uc)'\057',
	(uc)'\060',(uc)'\061',(uc)'\062',(uc)'\063',(uc)'\064',(uc)'\065',(uc)'\066',(uc)'\067',
	(uc)'\070',(uc)'\071',(uc)'\072',(uc)'\073',(uc)'\074',(uc)'\075',(uc)'\076',(uc)'\077',
	(uc)'\100',(uc)'\141',(uc)'\142',(uc)'\143',(uc)'\144',(uc)'\145',(uc)'\146',(uc)'\147',
	(uc)'\150',(uc)'\151',(uc)'\152',(uc)'\153',(uc)'\154',(uc)'\155',(uc)'\156',(uc)'\157',
	(uc)'\160',(uc)'\161',(uc)'\162',(uc)'\163',(uc)'\164',(uc)'\165',(uc)'\166',(uc)'\167',
	(uc)'\170',(uc)'\171',(uc)'\172',(uc)'\133',(uc)'\134',(uc)'\135',(uc)'\136',(uc)'\137',
	(uc)'\140',(uc)'\141',(uc)'\142',(uc)'\143',(uc)'\144',(uc)'\145',(uc)'\146',(uc)'\147',
	(uc)'\150',(uc)'\151',(uc)'\152',(uc)'\153',(uc)'\154',(uc)'\155',(uc)'\156',(uc)'\157',
	(uc)'\160',(uc)'\161',(uc)'\162',(uc)'\163',(uc)'\164',(uc)'\165',(uc)'\166',(uc)'\167',
	(uc)'\170',(uc)'\171',(uc)'\172',(uc)'\173',(uc)'\174',(uc)'\175',(uc)'\176',(uc)'\177',
	(uc)'\200',(uc)'\201',(uc)'\202',(uc)'\203',(uc)'\204',(uc)'\205',(uc)'\206',(uc)'\207',
	(uc)'\210',(uc)'\211',(uc)'\212',(uc)'\213',(uc)'\214',(uc)'\215',(uc)'\216',(uc)'\217',
	(uc)'\220',(uc)'\221',(uc)'\222',(uc)'\223',(uc)'\224',(uc)'\225',(uc)'\226',(uc)'\227',
	(uc)'\230',(uc)'\231',(uc)'\232',(uc)'\233',(uc)'\234',(uc)'\235',(uc)'\236',(uc)'\237',
	(uc)'\240',(uc)'\241',(uc)'\242',(uc)'\243',(uc)'\244',(uc)'\245',(uc)'\246',(uc)'\247',
	(uc)'\250',(uc)'\251',(uc)'\252',(uc)'\253',(uc)'\254',(uc)'\255',(uc)'\256',(uc)'\257',
	(uc)'\260',(uc)'\261',(uc)'\262',(uc)'\263',(uc)'\264',(uc)'\265',(uc)'\266',(uc)'\267',
	(uc)'\270',(uc)'\271',(uc)'\272',(uc)'\273',(uc)'\274',(uc)'\275',(uc)'\276',(uc)'\277',
	(uc)'\300',(uc)'\341',(uc)'\342',(uc)'\343',(uc)'\344',(uc)'\345',(uc)'\346',(uc)'\347',
	(uc)'\350',(uc)'\351',(uc)'\352',(uc)'\353',(uc)'\354',(uc)'\355',(uc)'\356',(uc)'\357',
	(uc)'\360',(uc)'\361',(uc)'\362',(uc)'\363',(uc)'\364',(uc)'\365',(uc)'\366',(uc)'\367',
	(uc)'\370',(uc)'\371',(uc)'\372',(uc)'\333',(uc)'\334',(uc)'\335',(uc)'\336',(uc)'\337',
	(uc)'\340',(uc)'\341',(uc)'\342',(uc)'\343',(uc)'\344',(uc)'\345',(uc)'\346',(uc)'\347',
	(uc)'\350',(uc)'\351',(uc)'\352',(uc)'\353',(uc)'\354',(uc)'\355',(uc)'\356',(uc)'\357',
	(uc)'\360',(uc)'\361',(uc)'\362',(uc)'\363',(uc)'\364',(uc)'\365',(uc)'\366',(uc)'\367',
	(uc)'\370',(uc)'\371',(uc)'\372',(uc)'\373',(uc)'\374',(uc)'\375',(uc)'\376',(uc)'\377',
};

int
strcasecmp(const char *s1, const char *s2)
{
    register unsigned char u1, u2;

    for (;;) {
	u1 = (unsigned char) *s1++;
	u2 = (unsigned char) *s2++;
	if (charmap[u1] != charmap[u2]) {
	    return charmap[u1] - charmap[u2];
	}
	if (u1 == '\0') {
	    return 0;
	}
    }
}

