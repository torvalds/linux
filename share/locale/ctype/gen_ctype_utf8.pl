#!/usr/bin/env perl
#	$OpenBSD: gen_ctype_utf8.pl,v 1.8 2023/02/16 01:06:01 afresh1 Exp $	#
use 5.022;
use warnings;

# Copyright (c) 2015 Andrew Fresh <afresh1@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

use Unicode::UCD v0.610 qw( charinfo charprop prop_invmap );

my @lists = qw(
    ALPHA
    CONTROL
    DIGIT
    GRAPH
    LOWER
    PUNCT
    SPACE
    UPPER
    XDIGIT
    BLANK
    PRINT
    SPECIAL
    PHONOGRAM

    SWIDTH0
    SWIDTH1
    SWIDTH2
);

my @maps = qw(
    MAPUPPER
    MAPLOWER
    TODIGIT
);

my ( $blocks_ranges_ref, $blocks_maps_ref ) = prop_invmap("Block");

print "/*\t\$" . 'OpenBSD' . "\$\t*/\n";
print <<'EOL';

/*
 * COPYRIGHT AND PERMISSION NOTICE
 *
 * Copyright (c) 1991-2021 Unicode, Inc. All rights reserved.
 * Distributed under the Terms of Use in
 * https://www.unicode.org/copyright.html.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of the Unicode data files and any associated documentation
 * (the "Data Files") or Unicode software and any associated documentation
 * (the "Software") to deal in the Data Files or Software
 * without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, and/or sell copies of
 * the Data Files or Software, and to permit persons to whom the Data Files
 * or Software are furnished to do so, provided that either
 * (a) this copyright and permission notice appear with all copies
 * of the Data Files or Software, or
 * (b) this copyright and permission notice appear in associated
 * Documentation.
 *
 * THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF
 * ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT OF THIRD PARTY RIGHTS.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS
 * NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL
 * DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THE DATA FILES OR SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder
 * shall not be used in advertising or otherwise to promote the sale,
 * use or other dealings in these Data Files or Software without prior
 * written authorization of the copyright holder.
 */

ENCODING        "UTF8"
VARIABLE        CODESET=UTF-8

EOL

print "/* Unicode Version " . Unicode::UCD::UnicodeVersion() . " */\n";

for my $i ( 0 .. $#{ $blocks_ranges_ref } ) {
	my $start = $blocks_ranges_ref->[ $i ];
	my $end   = ( $blocks_ranges_ref->[ $i + 1 ] || 0 ) - 1;

	my $descr = sprintf "U+%04X - U+%04X : %s",
	     $start, $end, $blocks_maps_ref->[$i];

	warn "$descr\n";
	print "\n/*\n * $descr\n */\n\n";

	last if $end == -1;
	next if $blocks_maps_ref->[$i] eq 'No_Block';

	my %info;
	categorize( $_, \%info ) for $start .. $end;
	print_info(%info);
}

# http://www.unicode.org/reports/tr44/tr44-16.html#General_Category_Values
# Table 12. General_Category Values
#
# Abbr         Long                             Description
# Lu   Uppercase_Letter      an uppercase letter
# Ll   Lowercase_Letter      a lowercase letter
# Lt   Titlecase_Letter      a digraphic character, with first part uppercase
# LC   Cased_Letter          Lu | Ll | Lt
# Lm   Modifier_Letter       a modifier letter
# Lo   Other_Letter          other letters, including syllables and ideographs
# L    Letter                Lu | Ll | Lt | Lm | Lo
# Mn   Nonspacing_Mark       a nonspacing combining mark (zero advance width)
# Mc   Spacing_Mark          a spacing combining mark (positive advance width)
# Me   Enclosing_Mark        an enclosing combining mark
# M    Mark                  Mn | Mc | Me
# Nd   Decimal_Number        a decimal digit
# Nl   Letter_Number         a letterlike numeric character
# No   Other_Number          a numeric character of other type
# N    Number                Nd | Nl | No
# Pc   Connector_Punctuation a connecting punctuation mark, like a tie
# Pd   Dash_Punctuation      a dash or hyphen punctuation mark
# Ps   Open_Punctuation      an opening punctuation mark (of a pair)
# Pe   Close_Punctuation     a closing punctuation mark (of a pair)
# Pi   Initial_Punctuation   an initial quotation mark
# Pf   Final_Punctuation     a final quotation mark
# Po   Other_Punctuation     a punctuation mark of other type
# P    Punctuation           Pc | Pd | Ps | Pe | Pi | Pf | Po
# Sm   Math_Symbol           a symbol of mathematical use
# Sc   Currency_Symbol       a currency sign
# Sk   Modifier_Symbol       a non-letterlike modifier symbol
# So   Other_Symbol          a symbol of other type
# S    Symbol                Sm | Sc | Sk | So
# Zs   Space_Separator       a space character (of various non-zero widths)
# Zl   Line_Separator        U+2028 LINE SEPARATOR only
# Zp   Paragraph_Separator   U+2029 PARAGRAPH SEPARATOR only
# Z    Separator             Zs | Zl | Zp
# Cc   Control               a C0 or C1 control code
# Cf   Format                a format control character
# Cs   Surrogate             a surrogate code point
# Co   Private_Use           a private-use character
# Cn   Unassigned            a reserved unassigned code point or a noncharacter
# C    Other                 Cc | Cf | Cs | Co | Cn

sub categorize
{
	my ( $code, $info ) = @_;

	# http://www.unicode.org/L2/L2003/03139-posix-classes.htm
	my $charinfo = charinfo($code);
	return unless $charinfo;
	my $general_category = $charinfo->{category};
	my $gc = substr $general_category, 0, 1;

	my $is_upper = $general_category eq 'Lu';
	my $is_lower = $general_category eq 'Ll';
	my $is_space = charprop( $code, 'Sentence_Break' ) eq 'Sp';

	my $is_print;
	my $matched;
	if ( $general_category eq 'Nd' ) {
		push @{ $info->{DIGIT} }, $code;
		$is_print = 1;
		$matched  = 1;
	} elsif ( $gc eq 'P' or $gc eq 'S' ) {
		push @{ $info->{PUNCT} }, $code;
		$is_print = 1;
		$matched  = 1;
	} elsif ( charprop( $code, 'White_Space' ) eq 'Yes' ) {
		push @{ $info->{SPACE} }, $code;
		$is_print = 1 if charprop( $code, 'Grapheme_Base' ) eq 'Yes';
		$matched = 1;
	} elsif ( charprop( $code, 'Alphabetic' ) eq 'Yes' ) {
		push @{ $info->{ALPHA} }, $code
		    if charprop( $code, 'Numeric_Type' ) eq 'None';
		push @{ $info->{LOWER} }, $code if $is_lower;
		push @{ $info->{UPPER} }, $code if $is_upper;
		push @{ $info->{PHONOGRAM} }, $code
		    if $charinfo->{name} =~ /SYLLABLE/
		    or $charinfo->{block} =~ /Syllable/i;
		$is_print = 1;
		$matched  = 1;
	}

	if ( $general_category eq 'Cc'
		or charprop( $code, 'Grapheme_Cluster_Break' ) eq 'Control' )
	{
		push @{ $info->{CONTROL} }, $code;
		$matched = 1;
	}

	push @{ $info->{BLANK} }, $code if $is_space;

	if (
		not(
			   $is_space or $general_category eq 'Cc',
			or $general_category eq 'Ss',
			or $general_category eq 'Cn',
		)
	    )
	{
		push @{ $info->{GRAPH} }, $code;
		push @{ $info->{SPECIAL} }, $code unless $matched;
		$is_print = 1;
	}
	push @{ $info->{PRINT} }, $code if $is_print;

	if ( charprop( $code, 'Hex_Digit' ) eq 'Yes' ) {
		push @{ $info->{XDIGIT} }, $code;
		$info->{TODIGIT}{$code} = hex chr $code
		    if charprop( $code, 'ASCII_Hex_Digit' ) eq 'Yes';
		$matched = 1;
	}

	if ($is_lower) {
		my $mapping = ord charprop( $code, 'Simple_Uppercase_Mapping' );
		$info->{MAPUPPER}{$code} = $mapping if $mapping != $code;
	}

	if ($is_upper) {
		my $mapping = ord charprop( $code, 'Simple_Lowercase_Mapping' );
		$info->{MAPLOWER}{$code} = $mapping if $mapping != $code;
	}

	{
		my $mapping = charprop( $code, 'Numeric_Value' );
		$info->{TODIGIT}{$code} = $mapping
		    if $mapping =~ /^[0-9]+$/ and chr($code) ne $mapping;
	}

	if ($is_print) {
		my $columns = codepoint_columns( $code, $charinfo );
		push @{ $info->{"SWIDTH$columns"} }, $code if defined $columns;
	}
}

sub print_info
{
	my (%info) = @_;

	my $printed = 0;

	foreach my $list (@lists) {
		next unless $info{$list};
		$printed = 1;
		print_list( $list => $info{$list} );
	}

	print "\n" if $printed;

	foreach my $map (@maps) {
		next unless $info{$map};
		print_map( $map => $info{$map} );
	}
}

sub print_list
{
	my ( $list, $points ) = @_;

	my @squished = reverse @{ squish_points($points) };
	my $line = sprintf "%-10s%s", $list, pop @squished;

	while (@squished) {
		my $item = pop @squished;

		if ( length("$line  $item") > 80 ) {
			say $line;
			$line = sprintf "%-10s%s", $list, $item;
		} else {
			$line .= "  $item";    # two leading spaces on purpose
		}
	}

	say $line;
}

sub print_map
{
	my ( $map, $points ) = @_;
	my $single = '< %s %s >';
	my $range  = '< %s : %s >';

	my %map;

	my $adjustment;
	my $last_diff = 0;
	my $first_point;
	my $prev_point;
	foreach my $point ( sort { $a <=> $b } keys %{$points} ) {
		my $diff = $point - $points->{$point};

		if ( $diff != $last_diff
			or
			( defined $prev_point and $point - 1 != $prev_point ) )
		{
			$first_point = undef;
			$adjustment  = undef;
			$last_diff   = undef;
		}

		$first_point //= $point;
		$adjustment  //= $points->{$point};
		$last_diff   //= $diff;

		$prev_point = $point;

		push @{ $map{$first_point}{$adjustment} }, $point;
	}

	my @ranges;

	foreach my $point ( keys %map ) {
		foreach my $adjustment ( keys %{ $map{$point} } ) {
			my $adj =
			    $map eq 'TODIGIT'
			    ? ( $adjustment || '0x0000' )
			    : format_point($adjustment);
			foreach (
				@{ squish_points( $map{$point}{$adjustment} ) }
			    )
			{
				my $format = / - / ? $range : $single;
				my $formatted = sprintf $format, $_, $adj;
				push @ranges, $formatted;
			}
		}
	}

	printf "%-10s%s\n", $map, $_ for sort @ranges;
}

sub squish_points
{
	my ($points) = @_;
	my @squished;

	my $start;
	my $last_point = 0;

	foreach my $i ( 0 .. $#{$points} + 1 ) {

		my $point = $points->[$i];

		if ( defined $point and $point - 1 == $last_point ) {
			$last_point = $point;
			next;
		}

		if ( defined $start ) {
			if ( $start == $i - 1 ) {
				push @squished,
				    format_point( $points->[$start] );
			}

			# TODO: This is nice, but breaks print_map
			#elsif ( $start == $i - 2 ) {
			#    push @squished, format_point( $points->[$start] ),
			#        format_point( $points->[ $i - 1 ] );
			#}
			else {
				push @squished, join ' - ',
				    format_point( $points->[$start] ),
				    format_point( $points->[ $i - 1 ] );
			}
		}

		$start      = $i;
		$last_point = $point;
	}

	return \@squished;
}

sub format_point
{
	my ($point) = @_;
	state %make_chr;
	%make_chr = map { $_ => 1 } ( 0 .. 9, 'a' .. 'z', 'A' .. 'Z' )
	    unless %make_chr;

	my $chr = chr $point;
	return "'$chr'" if $make_chr{$chr};
	return sprintf "0x%04x", $point;
}

sub codepoint_columns
{
	my ( $code, $charinfo ) = @_;
	return undef unless defined $code;

	# Private use areas are _most likely_ used by one column glyphs
	return 1 if $charinfo->{category} eq 'Co';

	return 0 if $charinfo->{category} eq 'Mn';
	return 0 if $charinfo->{category} eq 'Me';
	return 0 if index( $charinfo->{category}, 'C' ) == 0;

	return 2 if $charinfo->{block} eq 'Hangul Jamo';
	return 2 if $charinfo->{block} eq 'Hangul Jamo Extended-B';

	{
		my $eaw = charprop( $code, 'East_Asian_Width' );
		return 2 if $eaw eq 'Wide' or $eaw eq 'Fullwidth';
	}

	return 1;
}

__END__
=head1 NAME

gen_ctype_utf8.pl - rebuild  src/share/locale/ctype/en_US.UTF-8.src

=head1 SYNOPSIS

gen_ctype_utf8.pl > en_US.UTF-8.src

=head1 DESCRIPTION

The perl community does a good job of keeping their Unicode tables up to date
we can reuse their hard work to rebuild our tables.

We don't directly use the files from the Unicode Consortium instead we use
summary files that we generate.
See L<mklocale(1)> for more information about these files.

=head1 CAVEATS

Requires perl 5.22 or newer.

=head1 AUTHOR

Andrew Fresh <afresh1@openbsd.org>
