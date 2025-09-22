#!/usr/local/bin/perl -w

use strict;

my ($i, @arr);

# Set up an array with the type of ASCII characters
# Each set bit represents a character property.

# RFC2253 character properties
my $RFC2253_ESC = 1;	# Character escaped with \
my $ESC_CTRL	= 2;	# Escaped control character
# These are used with RFC1779 quoting using "
my $NOESC_QUOTE	= 8;	# Not escaped if quoted
my $PSTRING_CHAR = 0x10;	# Valid PrintableString character
my $RFC2253_FIRST_ESC = 0x20; # Escaped with \ if first character
my $RFC2253_LAST_ESC = 0x40;  # Escaped with \ if last character

for($i = 0; $i < 128; $i++) {
	# Set the RFC2253 escape characters (control)
	$arr[$i] = 0;
	if(($i < 32) || ($i > 126)) {
		$arr[$i] |= $ESC_CTRL;
	}

	# Some PrintableString characters
	if(		   ( ( $i >= ord("a")) && ( $i <= ord("z")) )
			|| (  ( $i >= ord("A")) && ( $i <= ord("Z")) )
			|| (  ( $i >= ord("0")) && ( $i <= ord("9")) )  ) {
		$arr[$i] |= $PSTRING_CHAR;
	}
}

# Now setup the rest

# Remaining RFC2253 escaped characters

$arr[ord(" ")] |= $NOESC_QUOTE | $RFC2253_FIRST_ESC | $RFC2253_LAST_ESC;
$arr[ord("#")] |= $NOESC_QUOTE | $RFC2253_FIRST_ESC;

$arr[ord(",")] |= $NOESC_QUOTE | $RFC2253_ESC;
$arr[ord("+")] |= $NOESC_QUOTE | $RFC2253_ESC;
$arr[ord("\"")] |= $RFC2253_ESC;
$arr[ord("\\")] |= $RFC2253_ESC;
$arr[ord("<")] |= $NOESC_QUOTE | $RFC2253_ESC;
$arr[ord(">")] |= $NOESC_QUOTE | $RFC2253_ESC;
$arr[ord(";")] |= $NOESC_QUOTE | $RFC2253_ESC;

# Remaining PrintableString characters

$arr[ord(" ")] |= $PSTRING_CHAR;
$arr[ord("'")] |= $PSTRING_CHAR;
$arr[ord("(")] |= $PSTRING_CHAR;
$arr[ord(")")] |= $PSTRING_CHAR;
$arr[ord("+")] |= $PSTRING_CHAR;
$arr[ord(",")] |= $PSTRING_CHAR;
$arr[ord("-")] |= $PSTRING_CHAR;
$arr[ord(".")] |= $PSTRING_CHAR;
$arr[ord("/")] |= $PSTRING_CHAR;
$arr[ord(":")] |= $PSTRING_CHAR;
$arr[ord("=")] |= $PSTRING_CHAR;
$arr[ord("?")] |= $PSTRING_CHAR;

# Now generate the C code

print <<EOF;
/* Auto generated with chartype.pl script.
 * Mask of various character properties
 */

static unsigned char char_type[] = {
EOF

for($i = 0; $i < 128; $i++) {
	print("\n") if($i && (($i % 16) == 0));
	printf("%2d", $arr[$i]);
	print(",") if ($i != 127);
}
print("\n};\n\n");

