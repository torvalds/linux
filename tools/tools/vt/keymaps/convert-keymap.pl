#!/usr/bin/perl
# $FreeBSD$

use Text::Iconv;
use Encode;
use strict;
use utf8;

# command line parsing
die "Usage: $0 filename.kbd charset [EURO|YEN]\n"
    unless ($ARGV[1]);

my $inputfile = shift;					# first command argument
my $converter = Text::Iconv->new(shift, "UTF-8");	# second argument
my $use_euro;
my $use_yen;
my $current_char;
my $current_scancode;

while (my $arg = shift) {
    $use_euro = 1, next
	if $arg eq "EURO";
    $use_yen = 1, next
	if $arg eq "YEN";
    die "Unknown encoding option '$arg'\n";
}

# converter functions
sub local_to_UCS_string
{
    my ($string) = @_;

    return $converter->convert($string);
}

sub prettyprint_token
{
    my ($ucs_char) = @_;

    return "'" . chr($ucs_char) . "'"
        if 32 <= $ucs_char and $ucs_char <= 126; # print as ASCII if possible
#    return sprintf "%d", $ucs_char; # <---- temporary decimal
    return sprintf "0x%02x", $ucs_char
        if $ucs_char <= 255;        # print as hex number, else
    return sprintf "0x%04x", $ucs_char;
}

sub local_to_UCS_code
{
    my ($char) = @_;

    my $ucs_char = ord(Encode::decode("UTF-8", local_to_UCS_string($char)));

    $current_char = lc(chr($ucs_char))
	if $current_char eq "";

    $ucs_char = 0x20ac	# replace with Euro character
	if $ucs_char == 0xa4 and $use_euro and $current_char eq "e";

    $ucs_char = 0xa5	# replace with Jap. Yen character on PC kbd
	if $ucs_char == ord('\\') and $use_yen and $current_scancode == 125;

    return prettyprint_token($ucs_char);
}

sub malformed_to_UCS_code
{
    my ($char) = @_;

    return prettyprint_token(ord(Encode::decode("UTF-8", $char)));
}

sub convert_token
{
    my ($C) = @_;

    return $1
        if $C =~ m/^([a-z][a-z0-9]*)$/;		# key token
    return local_to_UCS_code(chr($1))
        if $C =~ m/^(\d+)$/;			# decimal number
    return local_to_UCS_code(chr(hex($1)))
        if $C =~ m/^0x([0-9a-f]+)$/i;		# hex number
    return local_to_UCS_code(chr(ord($1)))
        if $C =~ m/^'(.)'$/;			# character
    return malformed_to_UCS_code($1)
        if $C =~ m/^'(.+)'$/;			# character
    return "<?$C?>";				# uncovered case
}

sub tokenize { # split on white space and parentheses (but not within token)
    my ($line) = @_;

    $line =~ s/'\('/ _lpar_ /g; # prevent splitting of '('
    $line =~ s/'\)'/ _rpar_ /g; # prevent splitting of ')'
    $line =~ s/'''/'_squote_'/g; # remove quoted single quotes from matches below
    $line =~ s/([()])/ $1 /g; # insert blanks around remaining parentheses
    my $matches;
    do {
	$matches = ($line =~ s/^([^']*)'([^']+)'/$1_squoteL_$2_squoteR_/g);
    } while $matches;
    $line =~ s/_squoteL_ _squoteR_/ _spc_ /g; # prevent splitting of ' '
    my @KEYTOKEN = split (" ", $line);
    grep(s/_squote[LR]?_/'/g, @KEYTOKEN);
    grep(s/_spc_/' '/, @KEYTOKEN);
    grep(s/_lpar_/'('/, @KEYTOKEN);
    grep(s/_rpar_/')'/, @KEYTOKEN);
    return @KEYTOKEN;
}

# main program
open FH, "<$inputfile";
while (<FH>) {
    if (m/^#/) {
	print local_to_UCS_string($_);
    } elsif (m/^\s*$/) {
	print "\n";
    } else {
	my @KEYTOKEN = tokenize($_);
	my $at_bol = 1;
	my $C;
	foreach $C (@KEYTOKEN) {
	    if ($at_bol) {
		$current_char = "";
		$current_scancode = -1;
		if ($C =~ m/^\s*\d/) { # line begins with key code number
		    $current_scancode = $C;
		    printf "  %03d   ", $C;
		} elsif ($C =~ m/^[a-z]/) { # line begins with accent name or paren
		    printf "  %-4s ", $C; # accent name starts accent definition
		} elsif ($C eq "(") {
		    printf "%17s", "( "; # paren continues accent definition
		} else {
		    print "Unknown input line format: $_";
		}
		$at_bol = 0;
	    } else {
		if ($C =~ m/^([BCNO])$/) {
		    print " $1"; # special case: effect of Caps Lock/Num Lock
		} elsif ($C eq "(") {
		    $current_char = "";
		    print " ( ";
		} elsif ($C eq ")") {
		    print " )";
		} else {
		    printf "%-6s ", convert_token($C);
		}
	    }
	}
	print "\n";
    }
}
close FH;
