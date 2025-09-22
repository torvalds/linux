// Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// 27.8.1.3 filebuf member functions
// @require@ %-*.tst %-*.txt
// @diff@ %-*.tst %-*.txt

// various tests for filebuf::open() and filebuf::close() including
// the non-portable functionality in the libstdc++-v3 IO library

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ext/stdio_filebuf.h>
#include <testsuite_hooks.h>

const char name_01[] = "filebuf_members-1.tst";
const char name_02[] = "filebuf_members-1.txt";

// Test member functions.
void test_01() 
{
  bool 			test = true;
  const char* name_03 = "filebuf_members-3"; // empty file, need to create

  std::filebuf fb_01; // in 
  std::filebuf fb_02; // out
  std::filebuf fb_03; // in | out

  // bool is_open()
  VERIFY( !fb_01.is_open() );
  VERIFY( !fb_02.is_open() );
  VERIFY( !fb_03.is_open() );

  // filebuf_type* open(const char* __s, ios_base::openmode __mode)
  fb_01.open(name_01, std::ios_base::in | std::ios_base::ate);
  fb_02.open(name_02, std::ios_base::in | std::ios_base::out 
	     | std::ios_base::trunc);
  // Try to open two different files without closing the first:
  // Should keep the old file attached, and disregard attempt to overthrow.
  fb_02.open(name_03, std::ios_base::in | std::ios_base::out);
  fb_03.open(name_03, std::ios_base::out | std::ios_base::trunc);
  VERIFY( fb_01.is_open() );
  VERIFY( fb_02.is_open() );
  VERIFY( fb_03.is_open() );

  // filebuf_type* close()
  fb_01.close();
  fb_02.close();
  fb_03.close();
  VERIFY( !fb_01.is_open() );
  VERIFY( !fb_02.is_open() );
  VERIFY( !fb_03.is_open() );
}

// Verify that std::filebuf doesn't close files that it didn't open
// when using the following std::filebuf ctor:
//
//      std::filebuf(__c_file_type*  __f,
//                   ios_base::openmode __mode,
//                   int_type  __s);
//
// Thanks to "George T. Talbot" <george@moberg.com> for uncovering
// this bug/situation. 
void test_02()
{
  bool test = true;
  int close_num;

  // read (ext)
  FILE* f2 = fopen(name_01, "r");
  VERIFY( f2 != NULL );
  if (f2)
  {
    __gnu_cxx::stdio_filebuf<char> fb(f2, std::ios_base::in, 512);
    close_num = fclose(f2);
  }
  VERIFY( close_num == 0 );

  // read (standard)
  FILE* f = fopen(name_01, "r");
  VERIFY( f != NULL );
  if (f)
  {
    std::ifstream ifstream1(name_01);
    VERIFY( ifstream1.is_open() );
    std::ios_base::iostate st01 = ifstream1.rdstate();
    VERIFY( st01 == std::ios_base::goodbit );
    close_num = fclose(f);
  }
  VERIFY( close_num == 0 );
}

void test_03()
{
  bool test = true;
  int first_fd = ::open(name_01, O_RDONLY);
  VERIFY( first_fd != -1 );
  FILE* first_file = ::fdopen(first_fd, "r");
  VERIFY( first_file != NULL );
  __gnu_cxx::stdio_filebuf<char> fb(first_file, std::ios_base::in);

  int second_fd = fb.fd();

  VERIFY( first_fd == second_fd );
}

// libstdc++/2913, libstdc++/4879
// John Fardo  <jfardo@laurelnetworks.com>, Brad Garcia <garsh@attbi.com>
void
test_04()
{
  bool test = true;
  const char* name = "tmp_fifo1";
  signal(SIGPIPE, SIG_IGN);

  unlink(name);
  if (0 != mkfifo(name, S_IRWXU))
    {
      std::cerr << "failed to create fifo" << std::endl;
      exit(-1);
    }
  
  int fval = fork();
  if (fval == -1)
    {
      std::cerr << "failed to fork" << std::endl;
      unlink(name);
      exit(-1);
    }
  else if (fval == 0)
    {
      std::ifstream ifs(name);
      sleep(1);
      ifs.close();
      exit(0);
    }

  std::ofstream ofs(name);
  sleep(2);
  ofs.put('t');

  /*
   * ISO/IED 14882:1998(E) 27.8.1.10.4
   *
   * void close();
   *
   * Effects:  Calls rdbuf()->close() and, if that function fails
   * (returns a null pointer), calls setstate(failbit)...
   */
  ofs.close();
  if (!(ofs.rdstate() & std::ios::failbit))
    {
      test = false;
      VERIFY( test );
      unlink(name);
      exit(-1);
    }

  unlink(name);
}

// Charles Leggett <CGLeggett@lbl.gov>
void test_05()
{
  bool test = true;
  const char* name = "tmp_file5";

  std::fstream scratch_file;

  scratch_file.open(name, std::ios::out);
  scratch_file.close();

  scratch_file.open(name, std::ios::in);
  if (!scratch_file)
    VERIFY( false );
  scratch_file.close();
}

// libstdc++/9507
void test_06()
{
  bool test = true;
  const char* name = "tmp_fifo2";

  signal(SIGPIPE, SIG_IGN);

  unlink(name);
  mkfifo(name, S_IRWXU);
	
  if (!fork())
    {
      std::filebuf fbuf;
      fbuf.open(name, std::ios_base::in);
      fbuf.sgetc();
      fbuf.close();
      exit(0);
    }

  std::filebuf fbuf;
  std::filebuf* r = fbuf.open(name,
			      std::ios_base::in
			      | std::ios_base::out
			      | std::ios_base::ate);
  VERIFY( !fbuf.is_open() );
  VERIFY( r == NULL );
}

// libstdc++/9964
void test_07()
{
  using namespace std;
  bool test = true;

  const char* name = "tmp_fifo3";

  signal(SIGPIPE, SIG_IGN);

  unlink(name);  
  mkfifo(name, S_IRWXU);
  
  int child = fork();
  VERIFY( child != -1 );

  if (child == 0)
    {
      filebuf fbin;
      fbin.open(name, ios_base::in);
      sleep(2);
      fbin.close();
      exit(0);
    }
  
  filebuf fb;
  sleep(1);
  filebuf* ret = fb.open(name, ios_base::in | ios_base::out);
  VERIFY( ret != NULL );
  VERIFY( fb.is_open() );

  sleep(3);
  fb.sputc('a');

  ret = fb.close();
  VERIFY( ret != NULL );
  VERIFY( !fb.is_open() );
}

int
main()
{
  test_01();
  test_02();
  test_03();
  test_04();
  test_05();
  test_06();
  test_07();
  return 0;
}


