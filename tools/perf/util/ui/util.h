#ifndef _PERF_UI_UTIL_H_
#define _PERF_UI_UTIL_H_ 1

#include <stdbool.h>

int ui__popup_menu(int argc, char * const argv[]);
int ui__help_window(const char *text);
bool ui__dialog_yesno(const char *msg);

#endif /* _PERF_UI_UTIL_H_ */
