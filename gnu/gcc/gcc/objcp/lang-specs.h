/* Definitions for specs for Objective-C++.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Ziemowit Laski  <zlaski@apple.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* This is the contribution to the `default_compilers' array in gcc.c for
   obj-c++.  It is based on -- and should be kept in sync with -- the g++
   spec (found in cp/lang-specs.h).  */

#ifndef CPLUSPLUS_CPP_SPEC
#define CPLUSPLUS_CPP_SPEC 0
#endif

  {".mm",  "@objective-c++", 0, 0, 0},
  {".M",   "@objective-c++", 0, 0, 0},
  {"@objective-c++-header",
    "%{E|M|MM:cc1objplus -E %(cpp_options) %2 %(cpp_debug_options)}\
     %{!E:%{!M:%{!MM:\
       %{save-temps|no-integrated-cpp:cc1objplus -E\
		%(cpp_options) %2 -o %{save-temps:%b.mii} %{!save-temps:%g.mii} \n}\
      cc1objplus %{save-temps|no-integrated-cpp:-fpreprocessed %{save-temps:%b.mii} %{!save-temps:%g.mii}}\
	      %{!save-temps:%{!no-integrated-cpp:%(cpp_unique_options)}}\
	%(cc1_options) %2 %{+e1*}\
        -o %g.s %{!o*:--output-pch=%i.gch} %W{o*:--output-pch=%*}%V}}}",
     CPLUSPLUS_CPP_SPEC, 0, 0},
  {"@objective-c++",
    "%{E|M|MM:cc1objplus -E %(cpp_options) %2 %(cpp_debug_options)}\
     %{!E:%{!M:%{!MM:\
       %{save-temps|no-integrated-cpp:cc1objplus -E\
		%(cpp_options) %2 -o %{save-temps:%b.mii} %{!save-temps:%g.mii} \n}\
      cc1objplus %{save-temps|no-integrated-cpp:-fpreprocessed %{save-temps:%b.mii} %{!save-temps:%g.mii}}\
	      %{!save-temps:%{!no-integrated-cpp:%(cpp_unique_options)}}\
	%(cc1_options) %2 %{+e1*}\
       %{!fsyntax-only:%(invoke_as)}}}}",
     CPLUSPLUS_CPP_SPEC, 0, 0},
  {".mii", "@objective-c++-cpp-output", 0, 0, 0},
  {"@objective-c++-cpp-output",
   "%{!M:%{!MM:%{!E:\
    cc1objplus -fpreprocessed %i %(cc1_options) %2 %{+e*}\
    %{!fsyntax-only:%(invoke_as)}}}}", 0, 0, 0},
  {"@objc++-cpp-output",
   "%{!M:%{!MM:%{!E:\
    cc1objplus -fpreprocessed %i %(cc1_options) %2 %{+e*}\
    %{!fsyntax-only:%(invoke_as)}}}}", 0, 0, 0},
