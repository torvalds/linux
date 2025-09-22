/*	$OpenBSD: nl_langinfo.c,v 1.9 2017/09/05 03:16:13 schwarze Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <locale.h>
#include <nl_types.h>
#include <langinfo.h>

#include "localedef.h"
#include "rune.h"
#include "runetype.h"

char *
nl_langinfo(nl_item item)
{
	const char *s;

	switch (item) {
	case D_T_FMT:
		s = _CurrentTimeLocale->d_t_fmt;
		break;
	case D_FMT:
		s = _CurrentTimeLocale->d_fmt;
		break;
	case T_FMT:
		s = _CurrentTimeLocale->t_fmt;
		break;
	case T_FMT_AMPM:
		s = _CurrentTimeLocale->t_fmt_ampm;
		break;
	case AM_STR:
	case PM_STR:
		s = _CurrentTimeLocale->am_pm[item - AM_STR];
		break;
	case DAY_1:
	case DAY_2:
	case DAY_3:
	case DAY_4:
	case DAY_5:
	case DAY_6:
	case DAY_7:
		s = _CurrentTimeLocale->day[item - DAY_1];
		break;
	case ABDAY_1:
	case ABDAY_2:
	case ABDAY_3:
	case ABDAY_4:
	case ABDAY_5:
	case ABDAY_6:
	case ABDAY_7:
		s = _CurrentTimeLocale->abday[item - ABDAY_1];
		break;
	case MON_1:
	case MON_2:
	case MON_3:
	case MON_4:
	case MON_5:
	case MON_6:
	case MON_7:
	case MON_8:
	case MON_9:
	case MON_10:
	case MON_11:
	case MON_12:
		s = _CurrentTimeLocale->mon[item - MON_1];
		break;
	case ABMON_1:
	case ABMON_2:
	case ABMON_3:
	case ABMON_4:
	case ABMON_5:
	case ABMON_6:
	case ABMON_7:
	case ABMON_8:
	case ABMON_9:
	case ABMON_10:
	case ABMON_11:
	case ABMON_12:
		s = _CurrentTimeLocale->abmon[item - ABMON_1];
		break;
	case RADIXCHAR:
		s = _CurrentNumericLocale->decimal_point;
		break;
	case THOUSEP:
		s = _CurrentNumericLocale->thousands_sep;
		break;
	case YESSTR:
		s = _CurrentMessagesLocale->yesstr;
		break;
	case YESEXPR:
		s = _CurrentMessagesLocale->yesexpr;
		break;
	case NOSTR:
		s = _CurrentMessagesLocale->nostr;
		break;
	case NOEXPR:
		s = _CurrentMessagesLocale->noexpr;
		break;
	case CRNCYSTR:				/* XXX */
		s = "";
		break;
	case CODESET:
		s = _CurrentRuneLocale()->rl_codeset;
		if (!s)
			s = "";
		break;
	default:
		s = "";
		break;
	}

	return (char *) s;
}
DEF_WEAK(nl_langinfo);
