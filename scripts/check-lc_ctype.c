/*
 * Check that a specified locale works as LC_CTYPE.  Used by the
 * DocBook build system to probe for C.UTF-8 support.
 */

#include <locale.h>

int main(void)
{
	return !setlocale(LC_CTYPE, "");
}
