#!/usr/bin/awk -f
#
# $FreeBSD$

#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2004 Mark Santcroos <marks@ripe.net>
# All rights reserved.
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

BEGIN {
	OUTPUT="acpi_quirks.h"
}

# Print header and id
NR == 1 {
	VERSION = $0;
	gsub("\^# ", "", VERSION)
	gsub("\\$", "", VERSION)

	printf("/*\n") > OUTPUT;
	printf(" * THIS FILE IS AUTOMAGICALLY GENERATED.  DO NOT EDIT.\n") \
	    > OUTPUT;
	printf(" *\n") > OUTPUT;
	printf(" * Generated from:\n") > OUTPUT;
	printf(" * %s\n", VERSION) > OUTPUT;
	printf(" */\n\n") > OUTPUT;
}

# Ignore comments and empty lines
/^#/, NF == 0 {
}

#
# NAME field: this is the first line of every entry
#
$1 == "name:" {
	ENTRY_NAME = $2;
	printf("const struct acpi_q_rule %s[] = {\n", ENTRY_NAME) > OUTPUT;
}

#
# OEM field
#
$1 == "oem:" {
	LENGTH = length();

	# Parse table type to match
	TABLE = $2;

	# Parse OEM ID
	M = match ($0, /\"[^\"]*\"/);
	OEM_ID = substr($0, M, RLENGTH);

	# Parse OEM Table ID
	ANCHOR = LENGTH - (M + RLENGTH - 1);
	REMAINDER = substr($0, M + RLENGTH, ANCHOR);
	M = match (REMAINDER, /\"[^\"]*\"/);
	OEM_TABLE_ID = substr(REMAINDER, M, RLENGTH);

	printf("\t{ \"%s\", OEM, {%s}, {%s} },\n",
	    TABLE, OEM_ID, OEM_TABLE_ID) > OUTPUT;
}

#
# CREATOR field
#
$1 == "creator:" {
	# Parse table type to match
	TABLE = $2;

	M = match ($0, /\"[^\"]*\"/);
	CREATOR = substr($0, M, RLENGTH);

	printf("\t{ \"%s\", CREATOR, {%s} },\n",
	    TABLE, CREATOR) > OUTPUT;
}

#
# OEM REVISION field
#
$1 == "oem_rev:" {
	TABLE = $2;
	SIGN = $3;
	VALUE = $4;

	# Parse operand
	OPERAND = trans_sign(SIGN);

	printf("\t{ \"%s\", OEM_REV, {.op = %s}, {.rev = %s} },\n",
	    TABLE, OPERAND, VALUE) > OUTPUT;
}

#
# CREATOR REVISION field
#
$1 == "creator_rev:" {
	TABLE = $2;
	SIGN = $3;
	VALUE = $4;

	# Parse operand
	OPERAND = trans_sign(SIGN);

	printf("\t{ \"%s\", CREATOR_REV, {.op = %s}, {.rev = %s} },\n",
	    TABLE, OPERAND, VALUE) > OUTPUT;
}

#
# QUIRKS field: This is the last line of every entry
#
$1 == "quirks:" {
	printf("\t{ \"\" }\n};\n\n") > OUTPUT;

	QUIRKS = $0;
	sub(/^quirks:[ ]*/ , "", QUIRKS);

	QUIRK_COUNT++;
	QUIRK_LIST[QUIRK_COUNT] = QUIRKS;
	QUIRK_NAME[QUIRK_COUNT] = ENTRY_NAME;
}

#
# All information is gathered, now create acpi_quirks_table
#
END {
	# Header
	printf("const struct acpi_q_entry acpi_quirks_table[] = {\n") \
	    > OUTPUT;

	# Array of all quirks
	for (i = 1; i <= QUIRK_COUNT; i++) {
		printf("\t{ %s, %s },\n", QUIRK_NAME[i], QUIRK_LIST[i]) \
		    > OUTPUT;
	}

	# Footer
	printf("\t{ NULL, 0 }\n") > OUTPUT;
	printf("};\n") > OUTPUT;

	exit(0);
}

#
# Translate math SIGN into verbal OPERAND
#
function trans_sign(TMP_SIGN)
{
	if (TMP_SIGN == "=")
		TMP_OPERAND = "OP_EQL";
	else if (TMP_SIGN == "!=")
		TMP_OPERAND = "OP_NEQ";
	else if (TMP_SIGN == "<=")
		TMP_OPERAND = "OP_LEQ";
	else if (TMP_SIGN == ">=")
		TMP_OPERAND = "OP_GEQ";
	else if (TMP_SIGN == ">")
		TMP_OPERAND = "OP_GTR";
	else if (TMP_SIGN == "<")
		TMP_OPERAND = "OP_LES";
	else {
		printf("error: unknown sign: " TMP_SIGN "\n");
		exit(1);
	}

	return (TMP_OPERAND);
}
