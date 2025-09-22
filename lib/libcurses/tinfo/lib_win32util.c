/****************************************************************************
 * Copyright 2020-2021,2023 Thomas E. Dickey                                *
 * Copyright 1998-2009,2010 Free Software Foundation, Inc.                  *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Juergen Pfeifer                                                 *
 *     and: Thomas E. Dickey                                                *
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: lib_win32util.c,v 1.1 2023/10/17 09:52:09 nicm Exp $")

#ifdef _NC_WINDOWS
#include <io.h>

#ifdef _NC_CHECK_MINTTY
#define PSAPI_VERSION 2
#include <psapi.h>
#include <tchar.h>

#define array_length(a) (sizeof(a)/sizeof(a[0]))

/*   This function tests, whether or not the ncurses application
     is running as a descendant of MSYS2/cygwin mintty terminal
     application. mintty doesn't use Windows Console for its screen
     I/O, so the native Windows _isatty doesn't recognize it as
     character device. But we can discover we are at the end of an
     Pipe and can query the server side of the pipe, looking whether
     or not this is mintty.
     For now we terminate the program if we discover that situation.
     Although in theory it would be possible, to remotely manipulate
     the terminal state of mintty, this is out of scope for now and
     not worth the significant effort.
 */
NCURSES_EXPORT(int)
_nc_console_checkmintty(int fd, LPHANDLE pMinTTY)
{
    HANDLE handle = _nc_console_handle(fd);
    DWORD dw;
    int code = 0;

    T((T_CALLED("lib_winhelper::_nc_console_checkmintty(%d, %p)"), fd, pMinTTY));

    if (handle != INVALID_HANDLE_VALUE) {
	dw = GetFileType(handle);
	if (dw == FILE_TYPE_PIPE) {
	    if (GetNamedPipeInfo(handle, 0, 0, 0, 0)) {
		ULONG pPid;
		/* Requires NT6 */
		if (GetNamedPipeServerProcessId(handle, &pPid)) {
		    TCHAR buf[MAX_PATH];
		    DWORD len = 0;
		    /* These security attributes may allow us to
		       create a remote thread in mintty to manipulate
		       the terminal state remotely */
		    HANDLE pHandle = OpenProcess(PROCESS_CREATE_THREAD
						 | PROCESS_QUERY_INFORMATION
						 | PROCESS_VM_OPERATION
						 | PROCESS_VM_WRITE
						 | PROCESS_VM_READ,
						 FALSE,
						 pPid);
		    if (pMinTTY)
			*pMinTTY = INVALID_HANDLE_VALUE;
		    if (pHandle != INVALID_HANDLE_VALUE) {
			if ((len = GetProcessImageFileName(pHandle,
							   buf,
							   (DWORD)
							   array_length(buf)))) {
			    TCHAR *pos = _tcsrchr(buf, _T('\\'));
			    if (pos) {
				pos++;
				if (_tcsnicmp(pos, _TEXT("mintty.exe"), 10)
				    == 0) {
				    if (pMinTTY)
					*pMinTTY = pHandle;
				    code = 1;
				}
			    }
			}
		    }
		}
	    }
	}
    }
    returnCode(code);
}
#endif /* _NC_CHECK_MINTTY */

#if HAVE_GETTIMEOFDAY == 2
#define JAN1970 116444736000000000LL	/* the value for 01/01/1970 00:00 */

NCURSES_EXPORT(int)
_nc_gettimeofday(struct timeval *tv, void *tz GCC_UNUSED)
{
    union {
	FILETIME ft;
	long long since1601;	/* time since 1 Jan 1601 in 100ns units */
    } data;

    GetSystemTimeAsFileTime(&data.ft);
    tv->tv_usec = (long) ((data.since1601 / 10LL) % 1000000LL);
    tv->tv_sec = (long) ((data.since1601 - JAN1970) / 10000000LL);
    return (0);
}
#endif // HAVE_GETTIMEOFDAY == 2

#endif // _NC_WINDOWS
