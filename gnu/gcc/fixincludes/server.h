
/*
 *  server.c  Set up and handle communications with a server process.
 *
 *  Server Handling copyright 1992-1999 The Free Software Foundation
 *
 *  Server Handling is free software.
 *  You may redistribute it and/or modify it under the terms of the
 *  GNU General Public License, as published by the Free Software
 *  Foundation; either version 2, or (at your option) any later version.
 *
 *  Server Handling is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Server Handling.  See the file "COPYING".  If not,
 *  write to:  The Free Software Foundation, Inc.,
 *             51 Franklin Street, Fifth Floor,
 *             Boston,  MA  02110-1301, USA.
 *
 * As a special exception, The Free Software Foundation gives
 * permission for additional uses of the text contained in his release
 * of ServerHandler.
 *
 * The exception is that, if you link the ServerHandler library with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the ServerHandler library code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public License.
 *
 * This exception applies only to the code released by The Free
 * Software Foundation under the name ServerHandler.  If you copy code
 * from other sources under the General Public License into a copy of
 * ServerHandler, as the General Public License permits, the exception
 * does not apply to the code that you add in this way.  To avoid
 * misleading anyone as to the status of such modified files, you must
 * delete this exception notice from them.
 *
 * If you write modifications of your own for ServerHandler, it is your
 * choice whether to permit this exception to apply to your modifications.
 * If you do not wish that, delete this exception notice.
 */

#ifndef GCC_SERVER_H
#define GCC_SERVER_H

/*
 *  Dual pipe opening of a child process
 */

typedef struct
{
  int read_fd;
  int write_fd;
} t_fd_pair;

typedef struct
{
  FILE *pf_read;		/* parent read fp  */
  FILE *pf_write;		/* parent write fp */
} t_pf_pair;

char* run_shell( const char* pzCmd );
pid_t proc2_fopen( t_pf_pair* p_pair, tCC** pp_args );
pid_t proc2_open( t_fd_pair* p_pair, tCC** pp_args );
int   chain_open( int in_fd, tCC** pp_args, pid_t* p_child );
void close_server( void );

#endif /* ! GCC_SERVER_H */
