#!/usr/bin/awk -f

#-
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 1992, 1993
#        The Regents of the University of California.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the University nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# From @(#)vnode_if.sh        8.1 (Berkeley) 6/10/93
# From @(#)makedevops.sh 1.1 1998/06/14 13:53:12 dfr Exp $
# From @(#)makedevops.sh ?.? 1998/10/05
# From src/sys/kern/makedevops.pl,v 1.12 1999/11/22 14:40:04 n_hibma Exp
# From src/sys/kern/makeobjops.pl,v 1.8 2001/11/16 02:02:42 joe Exp
#
# $FreeBSD$

#
#   Script to produce kobj front-end sugar.
#

function usage ()
{
	print "usage: makeobjops.awk <srcfile.m> [-d] [-p] [-l <nr>] [-c|-h]";
	print "where -c   produce only .c files";
	print "      -h   produce only .h files";
	print "      -p   use the path component in the source file for destination dir";
	print "      -l   set line width for output files [80]";
	print "      -d   switch on debugging";
	exit 1;
}

function warn (msg)
{
	print "makeobjops.awk:", msg > "/dev/stderr";
}

function warnsrc (msg)
{
	warn(src ":" lineno ": " msg);
}

function debug (msg)
{
	if (opt_d)
		warn(msg);
}

function die (msg)
{
	warn(msg);
	exit 1;
}

#   These are just for convenience ...
function printc(s) {if (opt_c) print s > ctmpfilename;}
function printh(s) {if (opt_h) print s > htmpfilename;}

#
#   If a line exceeds maxlength, split it into multiple
#   lines at commas.  Subsequent lines are indented by
#   the specified number of spaces.
#
#   In other words:  Lines are split by replacing ", "
#   by ",\n" plus indent spaces.
#

function format_line (line, maxlength, indent)
{
	rline = "";

	while (length(line) > maxlength) {
		#
		#   Find the rightmost ", " so that the part
		#   to the left of it is just within maxlength.
		#   If there is none, give up and leave it as-is.
		#
		if (!match(substr(line, 1, maxlength + 1), /^.*, /))
			break;
		rline = rline substr(line, 1, RLENGTH - 1) "\n";
		line = sprintf("%*s", indent, "") substr(line, RLENGTH + 1);
	}
	return rline line;
}

#
#   Join an array into a string.
#

function join (separator, array, num)
{
	_result = ""
	if (num) {
		while (num > 1)
			_result = separator array[num--] _result;
		_result = array[1] _result;
	}
	return _result;
}

#
#   Execute a system command and report if it failed.
#

function system_check (cmd)
{
	if ((rc = system(cmd)))
		warn(cmd " failed (" rc ")");
}

#
#   Handle "INTERFACE" line.
#

function handle_interface ()
{
	intname = $2;
	sub(/;$/, "", intname);
	if (intname !~ /^[a-z_][a-z0-9_]*$/) {
		debug($0);
		warnsrc("Invalid interface name '" intname "', use [a-z_][a-z0-9_]*");
		error = 1;
		return;
	}
	if (!/;[ 	]*$/)
		warnsrc("Semicolon missing at end of line, no problem");

	debug("Interface " intname);

	printh("#ifndef _" intname "_if_h_");
	printh("#define _" intname "_if_h_\n");
	printc("#include \"" intname "_if.h\"\n");
}

#
# Pass doc comments through to the C file
#
function handle_doc ()
{
	doc = ""
	while (!/\*\//) {
		doc = doc $0 "\n";
		getline < src;
		lineno++;
	}
	doc = doc $0 "\n";
	return doc;
}

#
#   Handle "CODE" and "HEADER" sections.
#   Returns the code as-is.
#

function handle_code ()
{
	code = "\n";
	getline < src;
	indent = $0;
	sub(/[^	 ].*$/, "", indent);	# find the indent used
	while (!/^}/) {
		sub("^" indent, "");	# remove the indent
		code = code $0 "\n";
		getline < src;
		lineno++;;
	}
	return code;
}

#
#   Handle "METHOD" and "STATICMETHOD" sections.
#

function handle_method (static, doc)
{
	#
	#   Get the return type and function name and delete that from
	#   the line. What is left is the possibly first function argument
	#   if it is on the same line.
	#
	if (!intname) {
		warnsrc("No interface name defined");
		error = 1;
		return;
	}
	sub(/^[^ 	]+[ 	]+/, "");
	ret = $0;
	sub(/[ 	]*\{.*$/, "", ret);
	name = ret;
	sub(/^.*[ 	]/, "", name);	# last element is name of method
	sub(/[ 	]+[^ 	]+$/, "", ret);	# return type
	debug("Method: name=" name " return type=" ret);

	sub(/^[^\{]*\{[	 ]*/, "");

	if (!name || !ret) {
		debug($0);
		warnsrc("Invalid method specification");
		error = 1;
		return;
	}

	if (name !~ /^[a-z_][a-z_0-9]*$/) {
		warnsrc("Invalid method name '" name "', use [a-z_][a-z0-9_]*");
		error = 1;
		return;
	}

	if (methods[name]) {
		warnsrc("Duplicate method name");
		error = 1;
		return;
	}
	methods[name] = name;

	line = $0;
	while (line !~ /\}/ && (getline < src) > 0) {
		line = line " " $0;
		lineno++
	}

	default_function = "";
	if (!match(line, /\};?/)) {
		warnsrc("Premature end of file");
		error = 1;
		return;
	}
	extra = substr(line, RSTART + RLENGTH);
	if (extra ~ /[	 ]*DEFAULT[ 	]*[a-zA-Z_][a-zA-Z_0-9]*[ 	]*;/) {
		default_function = extra;
		sub(/.*DEFAULT[	 ]*/, "", default_function);
		sub(/[; 	]+.*$/, "", default_function);
	}
	else if (extra && opt_d) {
		#   Warn about garbage at end of line.
		warnsrc("Ignored '" extra "'");
	}
	sub(/\};?.*$/, "", line);

	#
	#   Create a list of variables without the types prepended.
	#
	sub(/^[	 ]+/, "", line);	# remove leading ...
	sub(/[ 	]+$/, "", line);	# ... and trailing whitespace
	gsub(/[	 ]+/, " ", line);	# remove double spaces

	num_arguments = split(line, arguments, / *; */) - 1;
	delete varnames;		# list of varnames
	num_varnames = 0;
	for (i = 1; i <= num_arguments; i++) {
		if (!arguments[i])
			continue;	# skip argument if argument is empty
		num_ar = split(arguments[i], ar, /[* 	]+/);
		if (num_ar < 2) {	# only 1 word in argument?
			warnsrc("no type for '" arguments[i] "'");
			error = 1;
			return;
		}
		#   Last element is name of variable.
		varnames[++num_varnames] = ar[num_ar];
	}

	argument_list = join(", ", arguments, num_arguments);
	varname_list = join(", ", varnames, num_varnames);

	if (opt_d) {
		warn("Arguments: " argument_list);
		warn("Varnames: " varname_list);
	}

	mname = intname "_" name;	# method name
	umname = toupper(mname);	# uppercase method name

	firstvar = varnames[1];

	if (default_function == "")
		default_function = "kobj_error_method";

	# the method description 
	printh("/** @brief Unique descriptor for the " umname "() method */");
	printh("extern struct kobjop_desc " mname "_desc;");
	# the method typedef
	printh("/** @brief A function implementing the " umname "() method */");
	prototype = "typedef " ret " " mname "_t(";
	printh(format_line(prototype argument_list ");",
	    line_width, length(prototype)));

	# Print out the method desc
	printc("struct kobjop_desc " mname "_desc = {");
	printc("\t0, { &" mname "_desc, (kobjop_t)" default_function " }");
	printc("};\n");

	# Print out the method itself
	printh(doc);
	if (0) {		# haven't chosen the format yet
		printh("static __inline " ret " " umname "(" varname_list ")");
		printh("\t" join(";\n\t", arguments, num_arguments) ";");
	}
	else {
		prototype = "static __inline " ret " " umname "(";
		printh(format_line(prototype argument_list ")",
		    line_width, length(prototype)));
	}
	printh("{");
	printh("\tkobjop_t _m;");
	if (ret != "void")
		printh("\t" ret " rc;");
	if (!static)
		firstvar = "((kobj_t)" firstvar ")";
	if (prolog != "")
		printh(prolog);
	printh("\tKOBJOPLOOKUP(" firstvar "->ops," mname ");");
	rceq = (ret != "void") ? "rc = " : "";
	printh("\t" rceq "((" mname "_t *) _m)(" varname_list ");");
	if (epilog != "")
		printh(epilog);
	if (ret != "void")
		printh("\treturn (rc);");
	printh("}\n");
}

#
#   Begin of the main program.
#

BEGIN {

line_width = 80;
gerror = 0;

#
#   Process the command line.
#

num_files = 0;

for (i = 1; i < ARGC; i++) {
	if (ARGV[i] ~ /^-/) {
		#
		#   awk doesn't have getopt(), so we have to do it ourselves.
		#   This is a bit clumsy, but it works.
		#
		for (j = 2; j <= length(ARGV[i]); j++) {
			o = substr(ARGV[i], j, 1);
			if	(o == "c")	opt_c = 1;
			else if	(o == "h")	opt_h = 1;
			else if	(o == "p")	opt_p = 1;
			else if	(o == "d")	opt_d = 1;
			else if	(o == "l") {
				if (length(ARGV[i]) > j) {
					opt_l = substr(ARGV[i], j + 1);
					break;
				}
				else {
					if (++i < ARGC)
						opt_l = ARGV[i];
					else
						usage();
				}
			}
			else
				usage();
		}
	}
	else if (ARGV[i] ~ /\.m$/)
		filenames[num_files++] = ARGV[i];
	else
		usage();
}

if (!num_files || !(opt_c || opt_h))
	usage();

if (opt_p)
	debug("Will produce files in original not in current directory");

if (opt_l) {
	if (opt_l !~ /^[0-9]+$/ || opt_l < 1)
		die("Invalid line width '" opt_l "'");
	line_width = opt_l;
	debug("Line width set to " line_width);
}

for (i = 0; i < num_files; i++)
	debug("Filename: " filenames[i]);

for (file_i = 0; file_i < num_files; file_i++) {
	src = filenames[file_i];
	cfilename = hfilename = src;
	sub(/\.m$/, ".c", cfilename);
	sub(/\.m$/, ".h", hfilename);
	if (!opt_p) {
		sub(/^.*\//, "", cfilename);
		sub(/^.*\//, "", hfilename);
	}

	debug("Processing from " src " to " cfilename " / " hfilename);

	ctmpfilename = cfilename ".tmp";
	htmpfilename = hfilename ".tmp";

	common_head = \
	    "/*\n" \
	    " * This file is produced automatically.\n" \
	    " * Do not modify anything in here by hand.\n" \
	    " *\n" \
	    " * Created from source file\n" \
	    " *   " src "\n" \
	    " * with\n" \
	    " *   makeobjops.awk\n" \
	    " *\n" \
	    " * See the source file for legal information\n" \
	    " */\n";

	printc(common_head "\n" \
	    "#include <sys/param.h>\n" \
	    "#include <sys/queue.h>\n" \
	    "#include <sys/kernel.h>\n" \
	    "#include <sys/kobj.h>");

	printh(common_head);

	delete methods;		# clear list of methods
	intname = "";
	lineno = 0;
	error = 0;		# to signal clean up and gerror setting
	lastdoc = "";
	prolog = "";
	epilog = "";

	while (!error && (getline < src) > 0) {
		lineno++;

		#
		#   Take special notice of include directives.
		#
		if (/^#[ 	]*include[ 	]+["<][^">]+[">]/) {
			incld = $0;
			sub(/^#[ 	]*include[ 	]+/, "", incld);
			debug("Included file: " incld);
			printc("#include " incld);
		}

		sub(/#.*/, "");		# remove comments
		sub(/^[	 ]+/, "");	# remove leading ...
		sub(/[ 	]+$/, "");	# ... and trailing whitespace

		if (/^$/) {		# skip empty lines
		}
		else if (/^\/\*\*/)
			lastdoc = handle_doc();
		else if (/^INTERFACE[ 	]+[^ 	;]*[ 	]*;?[ 	]*$/) {
			printh(lastdoc);
			lastdoc = "";
			handle_interface();
		} else if (/^CODE[ 	]*{$/)
			printc(handle_code());
		else if (/^HEADER[	 ]*{$/)
			printh(handle_code());
		else if (/^METHOD/) {
			handle_method(0, lastdoc);
			lastdoc = "";
			prolog = "";
			epilog = "";
		} else if (/^STATICMETHOD/) {
			handle_method(1, lastdoc);
			lastdoc = "";
			prolog = "";
			epilog = "";
		} else if (/^PROLOG[ 	]*{$/)
			prolog = handle_code();
		else if (/^EPILOG[ 	]*{$/)
			epilog = handle_code();
		else {
			debug($0);
			warnsrc("Invalid line encountered");
			error = 1;
		}
	}

	#
	#   Print the final '#endif' in the header file.
	#
	printh("#endif /* _" intname "_if_h_ */");

	close (ctmpfilename);
	close (htmpfilename);

	if (error) {
		warn("Output skipped");
		system_check("rm -f " ctmpfilename " " htmpfilename);
		gerror = 1;
	}
	else {
		if (opt_c)
			system_check("mv -f " ctmpfilename " " cfilename);
		if (opt_h)
			system_check("mv -f " htmpfilename " " hfilename);
	}
}

exit gerror;

}
