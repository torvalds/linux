/* A replacement for Digital Unix's <va_list.h>.  */

#ifndef __GNUC_VA_LIST
#define __GNUC_VA_LIST
typedef __builtin_va_list __gnuc_va_list;
#endif

#if !defined(_VA_LIST) && !defined(_HIDDEN_VA_LIST)
#define _VA_LIST
typedef __gnuc_va_list va_list;

#elif defined(_HIDDEN_VA_LIST) && !defined(_HIDDEN_VA_LIST_DONE)
#define _HIDDEN_VA_LIST_DONE
typedef __gnuc_va_list __va_list;

#elif defined(_HIDDEN_VA_LIST) && defined(_VA_LIST)
#undef _HIDDEN_VA_LIST

#endif
