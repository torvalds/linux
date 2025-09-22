/*******************************************************************************
 * $Revision: 1.1.1.1 $
 * $Date: 2004/05/10 18:48:43 $
 * $Author: kettenis $
 *
 * Contents: A streambuf which uses the GNU readline library for line I/O
 * (c) 2001 by Dimitris Vyzovitis [vyzo@media.mit.edu]
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 ******************************************************************************/

#ifndef _READLINEBUF_H_
#define _READLINEBUF_H_

#include <iostream>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <cstdio>

#include <readline/readline.h>
#include <readline/history.h>

#if (defined __GNUC__) && (__GNUC__ < 3)
#include <streambuf.h>
#else
#include <streambuf>
using std::streamsize;
using std::streambuf;
#endif

class readlinebuf : public streambuf {
public:
#if (defined __GNUC__) && (__GNUC__ < 3)
	typedef char char_type;
	typedef int int_type;
	typedef streampos pos_type;
	typedef streamoff off_type;
#endif
	static const int_type eof = EOF; // this is -1
	static const int_type not_eof = 0;

private:
	const char* prompt_;
	bool history_;
	char* line_;
	int low_;
	int high_;

protected:
		
	virtual int_type showmanyc() const { return high_ - low_; }
		
	virtual streamsize xsgetn( char_type* buf, streamsize n ) {
		int rd = n > (high_ - low_)? (high_ - low_) : n;
		memcpy( buf, line_, rd );
		low_ += rd;
			
		if ( rd < n ) {
			low_ = high_ = 0;
			free( line_ ); // free( NULL ) is a noop
			line_ = readline( prompt_ );
			if ( line_ ) {
				high_ = strlen( line_ );
				if ( history_ && high_ ) add_history( line_ );
				rd += xsgetn( buf + rd, n - rd );
			}
		}
			
		return rd; 
	}
		
	virtual int_type underflow() {
		if ( high_ == low_ ) {
			low_ = high_ = 0;
			free( line_ ); // free( NULL ) is a noop
			line_ = readline( prompt_ );
			if ( line_ ) {
				high_ = strlen( line_ );
				if ( history_ && high_ ) add_history( line_ );
			}
		}
			
		if ( low_ < high_ ) return line_[low_];
		else return eof;
	}
		
	virtual int_type uflow() {
		int_type c = underflow();
		if ( c != eof ) ++low_;
		return c;
	}
		
	virtual int_type pbackfail( int_type c = eof ) {
		if ( low_ > 0 )	--low_;
		else if ( c != eof ) {
			if ( high_ > 0 ) {
				char* nl = (char*)realloc( line_, high_ + 1 );
				if ( nl ) {
					line_ = (char*)memcpy( nl + 1, line_, high_ );
					high_ += 1;
					line_[0] = char( c );
				} else return eof;
			} else {
				assert( !line_ );
				line_ = (char*)malloc( sizeof( char ) );
				*line_ = char( c );
				high_ = 1;
			}
		} else return eof;

		return not_eof;
	}
 		
public:
	readlinebuf( const char* prompt = NULL, bool history = true ) 
		: prompt_( prompt ), history_( history ),
		  line_( NULL ), low_( 0 ), high_( 0 ) {
		setbuf( 0, 0 );
	}
		
		
};

#endif
