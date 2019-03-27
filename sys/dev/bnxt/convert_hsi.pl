#!/usr/bin/env perl

# This script cleans up the "official" Broadcom hsi_struct_defs.h file as distributed
# to something somewhat more programmer friendly.
#
# $FreeBSD$

my $do_decode = 0;

if (! -f $ARGV[0]) {
	print "Input file not specified (should be path to hsi_struct_defs.h)\n";
	exit 1;
}

if (!open(IN, "<", $ARGV[0])) {
	print "Failure to open input file\n";
	exit 1;
}

if (!open(OUT, ">", "hsi_struct_def.h")) {
	print "Failure to open output file\n";
	exit 1;
}

$/=undef;
my $header = <IN>;
close IN;

print OUT <<END_OF_NOTICE;
/*-
 *   BSD LICENSE
 *
 *   Copyright (c) 2016 Broadcom, All Rights Reserved.
 *   The term Broadcom refers to Broadcom Limited and/or its subsidiaries
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("\$FreeBSD\$");

END_OF_NOTICE

# Convert line endings
$header =~ s/\r\n/\n/gs;

# Convert arrays of two u32_t to a single uint64_t
$header =~ s/\bu32_t(\s+[a-zA-Z0-9_]+)\[2\]/uint64_t$1/gs;

# Convert uint32_t *_lo/uint32_t *_hi to a single uint64_t
$header =~ s/\bu32_t(\s+[a-zA-Z0-9_]+)_lo;\s*\r?\n\s*u32_t(\s+[a-zA-Z0-9_]+)_hi/uint64_t$1/gs;

# Convert types
$header =~ s/\bu([0-9]+)_t\b/uint$1_t/gs;

# Convert literals
$header =~ s/\b((?:0x)?[0-9a-f]+)UL/UINT32_C($1)/gs;

# Strip comments
#$header =~ s/^(\s*[^\/\s][^\/]+?)\s*\/\*.*?\*\/\s*?$/$1/gm;
#$header =~ s/[ \t]*\/\*.*?\*\/\s*?\n?//gs;

# Pack structs
#$header =~ s/}(\s+)([^\s]+_t[,;])/} __attribute__((packed))$1$2/gs;

# Normalize indent
$header =~ s/(    ) +(#define)/$1$2/gs;
$header =~ s/^(}[^\n]*;)\n([^\n])/$1\n\n$2/gsm;
$header =~ s/([^\n])\n(typedef)/$1\n\n$2/gs;
$header =~ s/        /\t/g;
$header =~ s/    /\t/g;
$header =~ s/([^\s]\t+) +/$1/g;

# Remove typedefs and pack structs
$header =~ s/^typedef struct (.*?)\n{\n(.*?)}[^\n]*;/struct $1 {\n$2} __attribute__((packed));/gsm;

print OUT $header;
close OUT;

if ($do_decode) {
	if(!open(OUT, ">", "hsi_struct_decode.c")) {
		print "Failure to open decoder output file\n";
		exit 1;
	}

	print OUT <<END_OF_NOTICE;
	/*-
	 *   BSD LICENSE
	 *
	 *   Copyright (c) 2016 Broadcom, All Rights Reserved.
	 *   The term Broadcom refers to Broadcom Limited and/or its subsidiaries
	 *
	 *   Redistribution and use in source and binary forms, with or without
	 *   modification, are permitted provided that the following conditions
	 *   are met:
	 *     * Redistributions of source code must retain the above copyright
	 *       notice, this list of conditions and the following disclaimer.
	 *     * Redistributions in binary form must reproduce the above copyright
	 *       notice, this list of conditions and the following disclaimer in
	 *       the documentation and/or other materials provided with the
	 *       distribution.
	 *
	 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
	 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
	 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
	 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
	 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
	 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
	 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
	 */

	#include <sys/cdefs.h>
	__FBSDID("\$FreeBSD\$");

END_OF_NOTICE

	if(!open(HDR, ">", "hsi_struct_decode.h")) {
		print "Failure to open decoder output header file\n";
		exit 1;
	}

	print HDR <<END_OF_NOTICE;
	/*-
	 *   BSD LICENSE
	 *
	 *   Copyright(c) 2014-2015 Broadcom Corporation.
	 *   All rights reserved.
	 *
	 *   Redistribution and use in source and binary forms, with or without
	 *   modification, are permitted provided that the following conditions
	 *   are met:
	 *
	 *     * Redistributions of source code must retain the above copyright
	 *       notice, this list of conditions and the following disclaimer.
	 *     * Redistributions in binary form must reproduce the above copyright
	 *       notice, this list of conditions and the following disclaimer in
	 *       the documentation and/or other materials provided with the
	 *       distribution.
	 *     * Neither the name of Broadcom Corporation nor the names of its
	 *       contributors may be used to endorse or promote products derived
	 *       from this software without specific prior written permission.
	 *
	 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
	 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
	 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
	 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
	 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
	 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
	 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
	 */

END_OF_NOTICE

	print OUT "#ifdef HSI_DEBUG\n#include <inttypes.h>\n#include <rte_common.h>\n#include <rte_log.h>\n#include \"hsi_struct_def_dpdk.h\"\n#include \"hsi_struct_decode.h\"\n#include \"hsi_struct_decode.h\"\n\n";
	print HDR "#ifdef HSI_DEBUG\n#include \"hsi_struct_def_dpdk.h\"\n\n";

	my $hdr_defs = '';

	sub print_single_val
	{
		my $field=shift;
		my $type=shift;
		my $max_field_len=shift;
		my $name = shift;
		my $macroshash = shift;
		my %macros = %$macroshash;
		$macrosref = shift;
		my @macros = @$macrosref;
		$macrosref = shift;
		my @fields = @$macrosref;

		if ($type eq 'uint32_t') {
			printf OUT "\tRTE_LOG(DEBUG, PMD, \"  % *s = 0x%%08\"PRIX32\"\\n\", data->$field);\n",$max_field_len,$field;
		}
		elsif ($type eq 'uint16_t') {
			printf OUT "\tRTE_LOG(DEBUG, PMD, \"  % *s = 0x%%04\"PRIX16\"\\n\", data->$field);\n",$max_field_len,$field;
		}
		elsif ($type eq 'uint8_t') {
			printf OUT "\tRTE_LOG(DEBUG, PMD, \"  % *s = 0x%%02\"PRIX8\"\\n\", data->$field);\n",$max_field_len,$field;
		}
		elsif ($type eq 'uint64_t') {
			printf OUT "\tRTE_LOG(DEBUG, PMD, \"  % *s = 0x%%016\"PRIX64\"\\n\", data->$field);\n",$max_field_len,$field;
		}
		elsif ($type eq 'char') {
			if ($field =~ s/\[([0-9]+)\]//) {
				printf OUT "\tRTE_LOG(DEBUG, PMD, \"  % *s = \\\"%%.$1s\\\"\\n\", data->$field);\n",$max_field_len,$field;
			}
			else {
				printf OUT "\tRTE_LOG(DEBUG, PMD, \"  % *s = 0x%%02\"PRIX8\"\\n\", data->$field);\n",$max_field_len,$field;
			}
		}
		else {
			print "Unhandled type: '$type'\n";
		}

		my $macro_prefix = uc($name).'_'.uc($field).'_';
		# Special handling for the common flags_type field
		$macro_prefix =~ s/FLAGS_TYPE_$/FLAGS_/ if ($field eq 'flags_type');
		# Special handling for _hi types
		$macro_prefix =~ s/_HI_/_/ if ($name =~ /_hi$/);

		$macro_prefix =~ s/\[[0-9]+\]//;
		my %vmacros;
		my $vmacros_have_mask = 0;
		my @vmacros;
		my %subfields;
		my $all_single_bits=1;
	MACRO:
		foreach my $macro (@macros) {
			if ($macro =~ /^$macro_prefix(.*)_MASK$/) {
				my $macro = $&;
				my $maskdef = $macros{$macro};
				my $subfield = $1;
				my $subfield_value = "(data->$field & $macro)";
				if (defined $macros{"$macro_prefix$subfield\_SFT"}) {
					$subfield_value = "($subfield_value >> $macro_prefix$subfield\_SFT)";
				}
				$maskdef =~ s/[x0 ]//g;
				if ($type eq 'uint64_t') {
					printf OUT "\tRTE_LOG(DEBUG, PMD, \"  % *s   $subfield = %%0*\" PRIX64 \"\\n\", %u, $subfield_value);\n", $max_field_len, '', length($maskdef);
				}
				else {
					printf OUT "\tRTE_LOG(DEBUG, PMD, \"  % *s   $subfield = %%0*X\\n\", %u, $subfield_value);\n", $max_field_len, '', length($maskdef);
				}
				delete $$macroshash{$macro};
			}
			elsif ($macro =~ /^$macro_prefix(.*)_SFT$/) {
				delete $$macroshash{$macro};
			}
			elsif ($macro =~ /^$macro_prefix\MASK$/) {
				$vmacros_have_mask = 1;
				delete $$macroshash{$macro};
			}
			elsif ($macro =~ /^$macro_prefix(.*)$/) {
				my $macro = $&;
				my $subfield = $1;

				# Check for longer fields with the same base... ie: link and link_speed
				foreach my $extra_field (@fields) {
					next if ($extra_field eq $field);
					if ($extra_field =~ /^$field/) {
						my $extra_prefix = uc($name).'_'.uc($extra_field).'_';
						next MACRO if ($macro =~ /^$extra_prefix/);
					}
				}

				push @vmacros, $macro;
				my $macroeval = $macros{$macro};
				$macroeval =~ s/UINT32_C\((.*?)\)/$1/g;
				$vmacros{$macro} = eval("$macroeval");
				$subfields{$macro} = $subfield;

				$all_single_bits = 0 if ($vmacros{$macro} & ($vmacros{$macro}-1));
				$all_single_bits = 0 if ($vmacros{$macro} == 0);
			}
		}
		if ($all_single_bits) {
			foreach my $macro (@vmacros) {
				my $subfield_value = "(data->$field & $macro)";
				printf OUT "\tRTE_LOG(DEBUG, PMD, \"  % *s   $subfields{$macro} : %%s\\n\", $subfield_value?\"ON\":\"OFF\");\n", $max_field_len, '';
				delete $$macroshash{$macro};
			}
		}
		else {
			printf OUT "\tRTE_LOG(DEBUG, PMD, \"  % *s   Value : %%s\\n\",\n", $max_field_len, '';
			foreach my $macro (@vmacros) {
				my $subfield_value = "data->$field";
				$subfield_value = "($subfield_value & $macro_prefix\MASK)" if $vmacros_have_mask;
				print OUT "\t\t$subfield_value == $macro ? \"$subfields{$macro}\" :\n";
				delete $$macroshash{$macro};
			}
			print OUT "\t\t\"Unknown\");\n";
		}
	}

	while ($header =~ /^typedef\s+struct\s+(.*?)\s+{(.*?)^}/msg) {
		my ($name,$def) = ($1, $2);
		my @fields=();
		my %type=();
		my @macros=();
		my %macros=();
		my $max_field_len=0;

		# First, pull out all the fields in order...
		while($def =~ /^\s*([^\s#\/]+?)\s+([^;\/\s]+?)\s*;/mg) {
			my ($type, $name) = ($1, $2);
			push @fields, $name;
			$type{$name}=$type;
			$max_field_len = length($name) if length($name) > $max_field_len;
		}
		# Now, pull out the macros...
		while($def =~ /^\s*\#define\s+([^\s]+?)\s+(.*?)\s*$/mg) {
			push @macros, $1;
			$macros{$1}=$2;
		}

		# Now, generate code to print the struct...
		print OUT "void decode_$name(const char *string __rte_unused, struct $name *data) {\n\tRTE_LOG(DEBUG, PMD, \"$name\\n\");\n";
		print HDR "void decode_$name(const char *string __rte_unused, struct $name *data);\n";
		$hdr_defs .= "#define decode_$name(x, y) {}\n";
		foreach my $field (@fields) {
			if ($field =~ /\[([0-9]+)\]/) {
				if ($type{$field} eq 'char') {
					print_single_val($field, $type{$field}, $max_field_len, $name, \%macros, \@macros, \@fields);
				}
				else {
					foreach my $idx (0..$1-1) {
						my $item = $field;
						$item =~ s/\[[0-9]+\]/[$idx]/;
						print_single_val($item, $type{$field}, $max_field_len, $name, \%macros, \@macros, \@fields);
					}
				}
			}
			else {
				print_single_val($field, $type{$field}, $max_field_len, $name, \%macros, \@macros, \@fields);
			}
		}
	#	print "Unhandled macros:\n",join("\n", keys %macros),"\n" if (keys %macros > 0);
		print OUT "}\n\n";
	}
	print OUT "#endif\n";

	print HDR "#else\n";
	print HDR $hdr_defs;
	print HDR "#endif\n";
	close OUT;
	close HDR;
}
