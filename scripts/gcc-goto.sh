#!/bin/sh
# Test for gcc 'asm goto' support
# Copyright (C) 2010, Jason Baron <jbaron@redhat.com>

cat << "END" | $@ -x c - -c -o /dev/null >/dev/null 2>&1 && echo "y"
int main(void)
{
#ifdef __arm__
	/*
	 * Not related to asm goto, but used by jump label
	 * and broken on some ARM GCC versions (see GCC Bug 48637).
	 */
	static struct { int dummy; int state; } tp;
	asm (".long %c0" :: "i" (&tp.state));
#endif

entry:
	asm goto ("" :::: entry);
	return 0;
}
END
