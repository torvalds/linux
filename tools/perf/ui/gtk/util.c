#include "../util.h"
#include "../../util/debug.h"
#include "gtk.h"


/*
 * FIXME: Functions below should be implemented properly.
 *        For now, just add stubs for NO_NEWT=1 build.
 */
#ifdef NO_NEWT_SUPPORT
int ui_helpline__show_help(const char *format __used, va_list ap __used)
{
	return 0;
}

void ui_progress__update(u64 curr __used, u64 total __used,
			 const char *title __used)
{
}
#endif
