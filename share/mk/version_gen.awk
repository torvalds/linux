#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (C) 2006 Daniel M. Eischen.  All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#
# Make a list of all the library versions listed in the master file.
#
#   versions[] - array indexed by version name, contains number
#                of symbols (+ 1) found for each version.
#   successors[] - array index by version name, contains successor
#                  version name.
#   symbols[][] - array index by [version name, symbol index], contains
#                 names of symbols defined for each version.
#   names[] - array index is symbol name and value is its first version seen,
#	      used to check for duplicate symbols and warn about them.
#
BEGIN {
	brackets = 0;
	errors = warns = 0;
	version_count = 0;
	current_version = "";
	stderr = "/dev/stderr";
	while (getline < vfile) {
		# Strip comments.
		sub("#.*$", "", $0);

		# Strip leading and trailing whitespace.
		sub("^[ \t]+", "", $0);
		sub("[ \t]+$", "", $0);

		if (/^[a-zA-Z0-9._]+[ \t]*{$/) {
			# Strip brace.
			sub("{", "", $1);
			brackets++;
			symver = $1;
			versions[symver] = 1;
			successors[symver] = "";
			generated[symver] = 0;
			version_count++;
		}
		else if (/^}[ \t]*[a-zA-Z0-9._]+[ \t]*;$/) {
			v = $1 != "}" ? $1 : $2;
			# Strip brace.
			sub("}", "", v);
			# Strip semicolon.
			sub(";", "", v);
			if (symver == "") {
				printf("File %s: Unmatched bracket.\n",
				vfile) > stderr;
				errors++;
			}
			else if (versions[v] != 1) {
				printf("File %s: `%s' has unknown " \
				    "successor `%s'.\n",
				    vfile, symver, v) > stderr;
				errors++;
			}
			else
				successors[symver] = v;
			brackets--;
		}
		else if (/^}[ \t]*;$/) {
			if (symver == "") {
				printf("File %s: Unmatched bracket.\n",
				    vfile) > stderr;
				errors++;
			}
			# No successor
			brackets--;
		}
		else if (/^}$/) {
			printf("File %s: Missing final semicolon.\n",
			    vfile) > stderr;
			errors++;
		}
		else if (/^$/)
			;  # Ignore blank lines.
		else {
			printf("File %s: Unknown directive: `%s'.\n",
			    vfile, $0) > stderr;
			errors++;
		}
	}
	brackets = 0;
}

{
	# Set meaningful filename for diagnostics.
	filename = FILENAME != "" ? FILENAME : "<stdin>";

	# Delete comments, preceding and trailing whitespace, then
	# consume blank lines.
	sub("#.*$", "", $0);
	sub("^[ \t]+", "", $0);
	sub("[ \t]+$", "", $0);
	if ($0 == "")
		next;
}

/^[a-zA-Z0-9._]+[ \t]*{$/ {
	# Strip bracket from version name.
	sub("{", "", $1);
	if (current_version != "") {
		printf("File %s, line %d: Illegal nesting detected.\n",
		    filename, FNR) > stderr;
		errors++;
	}
	else if (versions[$1] == 0) {
		printf("File %s, line %d: Undefined " \
		    "library version `%s'.\n", filename, FNR, $1) > stderr;
		errors++;
		# Remove this entry from the versions.
		delete versions[$1];
	}
	else
		current_version = $1;
	brackets++;
	next;
}

/^[a-zA-Z0-9._]+[ \t]*;$/ {
	# Strip semicolon.
	sub(";", "", $1);
	if (current_version != "") {
		count = versions[current_version];
		versions[current_version]++;
		symbols[current_version, count] = $1;
		if ($1 in names && names[$1] != current_version) {
			#
			# A graver case when a dup symbol appears under
			# different versions in the map.  That can result
			# in subtle problems with the library later.
			#
			printf("File %s, line %d: Duplicated symbol `%s' " \
			    "in version `%s', first seen in `%s'. " \
			    "Did you forget to move it to ObsoleteVersions?\n",
			    filename, FNR, $1,
			    current_version, names[$1]) > stderr;
			errors++;
		}
		else if (names[$1] == current_version) {
			#
			# A harmless case: a dup symbol with the same version.
			#
			printf("File %s, line %d: warning: " \
			    "Duplicated symbol `%s' in version `%s'.\n",
			    filename, FNR, $1, current_version) > stderr;
			warns++;
		}
		else
			names[$1] = current_version;
	}
	else {
		printf("File %s, line %d: Symbol `%s' outside version scope.\n",
		    filename, FNR, $1) > stderr;
		errors++;
	}
	next;
}

/^}[ \t]*;$/ {
	brackets--;
	if (brackets < 0) {
		printf("File %s, line %d: Unmatched bracket.\n",
		    filename, FNR, $1) > stderr;
		errors++;
		brackets = 0;	# Reset
	}
	current_version = "";
	next;
}


{
	printf("File %s, line %d: Unknown directive: `%s'.\n",
	    filename, FNR, $0) > stderr;
	errors++;
}

function print_version(v)
{
	# This function is recursive, so return if this version
	# has already been printed.  Otherwise, if there is an
	# ancestral version, recursively print its symbols before
	# printing the symbols for this version.
	#
	if (generated[v] == 1)
		return;
	if (successors[v] != "")
		print_version(successors[v]);

	printf("%s {\n", v);

	# The version count is always one more that actual,
	# so the loop ranges from 1 to n-1.
	#
	for (i = 1; i < versions[v]; i++) {
		if (i == 1)
			printf("global:\n");
		printf("\t%s;\n", symbols[v, i]);
	}

	version_count--;
	if (version_count == 0) {
		printf("local:\n");
		printf("\t*;\n");
	}
	if (successors[v] == "")
		printf("};\n");
	else
		printf("} %s;\n", successors[v]);
	printf("\n");

	generated[v] = 1;
    }

END {
	if (errors) {
		printf("%d error(s) total.\n", errors) > stderr;
		exit(1);
	}
	# OK, no errors.
	for (v in versions) {
		print_version(v);
	}
}
