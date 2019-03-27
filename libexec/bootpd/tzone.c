/*
 * tzone.c - get the timezone
 *
 * This is shared by bootpd and bootpef
 *
 * $FreeBSD$
 */

#ifdef	SVR4
/* XXX - Is this really SunOS specific? -gwr */
/* This is in <time.h> but only visible if (__STDC__ == 1). */
extern long timezone;
#else /* SVR4 */
/* BSD or SunOS */
# include <time.h>
# include <syslog.h>
#endif /* SVR4 */

#include "bptypes.h"
#include "report.h"
#include "tzone.h"

/* This is what other modules use. */
int32 secondswest;

/*
 * Get our timezone offset so we can give it to clients if the
 * configuration file doesn't specify one.
 */
void
tzone_init()
{
#ifdef	SVR4
	/* XXX - Is this really SunOS specific? -gwr */
	secondswest = timezone;
#else /* SVR4 */
	struct tm *tm;
	time_t now;

	(void)time(&now);
	if ((tm = localtime(&now)) == NULL) {
		secondswest = 0;		/* Assume GMT for lack of anything better */
		report(LOG_ERR, "localtime() failed");
	} else {
		secondswest = -tm->tm_gmtoff;
	}
#endif /* SVR4 */
}
