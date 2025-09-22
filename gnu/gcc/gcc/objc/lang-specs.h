/* Definitions for specs for Objective-C.
   Copyright (C) 1998, 1999, 2002, 2002, 2003, 2005
   Free Software Foundation, Inc.

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
   objc.  */

  {".m", "@objective-c", 0, 0, 0},
  {"@objective-c",
     "%{E|M|MM:cc1obj -E %{traditional|ftraditional|traditional-cpp:-traditional-cpp}\
          %(cpp_options) %(cpp_debug_options)}\
      %{!E:%{!M:%{!MM:\
	%{traditional|ftraditional|traditional-cpp:\
%eGNU Objective C no longer supports traditional compilation}\
	%{save-temps|no-integrated-cpp:cc1obj -E %(cpp_options) -o %{save-temps:%b.mi} %{!save-temps:%g.mi} \n\
	    cc1obj -fpreprocessed %{save-temps:%b.mi} %{!save-temps:%g.mi} %(cc1_options) %{print-objc-runtime-info} %{gen-decls}}\
	%{!save-temps:%{!no-integrated-cpp:\
	    cc1obj %(cpp_unique_options) %(cc1_options) %{print-objc-runtime-info} %{gen-decls}}}\
        %{!fsyntax-only:%(invoke_as)}}}}", 0, 0, 0},
  {".mi", "@objc-cpp-output", 0, 0, 0},
  {"@objc-cpp-output",
     "%{!M:%{!MM:%{!E:cc1obj -fpreprocessed %i %(cc1_options) %{print-objc-runtime-info} %{gen-decls}\
			     %{!fsyntax-only:%(invoke_as)}}}}", 0, 0, 0},
  {"@objective-c-header",
     "%{E|M|MM:cc1obj -E %{traditional|ftraditional|traditional-cpp:-traditional-cpp}\
          %(cpp_options) %(cpp_debug_options)}\
      %{!E:%{!M:%{!MM:\
	%{traditional|ftraditional|traditional-cpp:\
%eGNU Objective C no longer supports traditional compilation}\
	%{save-temps|no-integrated-cpp:cc1obj -E %(cpp_options) -o %{save-temps:%b.mi} %{!save-temps:%g.mi} \n\
	    cc1obj -fpreprocessed %b.mi %(cc1_options) %{print-objc-runtime-info} %{gen-decls}\
                        -o %g.s %{!o*:--output-pch=%i.gch}\
                        %W{o*:--output-pch=%*}%V}\
	%{!save-temps:%{!no-integrated-cpp:\
	    cc1obj %(cpp_unique_options) %(cc1_options) %{print-objc-runtime-info} %{gen-decls}\
                        -o %g.s %{!o*:--output-pch=%i.gch}\
                        %W{o*:--output-pch=%*}%V}}}}}", 0, 0, 0},
