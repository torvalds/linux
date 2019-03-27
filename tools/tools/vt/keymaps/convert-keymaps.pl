#!/usr/local/bin/perl
# $FreeBSD$

use Text::Iconv;
use Encode;
use strict;
use utf8;

# directories and filenames
$0 =~ m:^(.*)/:;
my $dir_convtool = $1 || ".";

my $dir_keymaps_syscons = "/usr/src/share/syscons/keymaps";
my $dir_keymaps_config = "$dir_convtool";

my $dir_keymaps_vt = "/usr/src/share/vt/keymaps";
my $dir_keymaps_output = "$dir_keymaps_vt/OUTPUT";

my $keymap_index = "$dir_keymaps_syscons/INDEX.keymaps";

my $language_map = "$dir_keymaps_config/LANG.map";
my $keymapfile_map = "$dir_keymaps_config/KBDFILES.map";

# global variables
my %LANG_NEW;		# index: lang_old
my %ENCODING;		# index: lang_old, file_old
my %FILE_NEW;		# index: file_old

# subroutines
sub local_to_UCS_string
{
    my ($string, $old_enc) = @_;
    my $converter = Text::Iconv->new($old_enc, "UTF-8");
    my $result = $converter->convert($string);
    printf "!!! conversion failed for '$string' ($old_enc)\n"
	unless $result;
    return $result;
}

sub lang_fixup {
    my ($langlist) = @_;
    my $result;
    my $lang;
    for $lang (split(/,/, $langlist)) {
	$result .= ","
	    if $result;
	$result .= $LANG_NEW{$lang};
    }
    return $result;
}

# main program
open LANGMAP, "<$language_map"
    or die "$!";
while (<LANGMAP>) {
    next
	if m/^#/;
    my ($lang_old, $lang_new, $encoding) = split(" ");
#    print "$lang_old|$lang_new|$encoding\n";
    $LANG_NEW{$lang_old} = $lang_new;
    $ENCODING{$lang_old} = $encoding;
    $ENCODING{$lang_new} = $encoding;
}
close LANGMAP;

$FILE_NEW{"MENU"} = "MENU"; # dummy identity mapping
$FILE_NEW{"FONT"} = "FONT"; # dummy identity mapping
open FILEMAP, "<$keymapfile_map"
    or die "$!";
while (<FILEMAP>) {
    next
	if m/^#/;
    my ($encoding, $file_old, $file_new) = split(" ");
#    print "--> ", join("|", $encoding, $file_old, $file_new, $file_locale), "\n";
    if ($encoding and $file_old and $file_new) {
	$ENCODING{$file_old} = $encoding;
	$FILE_NEW{$file_old} = $file_new;
    }
}
close FILEMAP;

my $kbdfile;
foreach $kbdfile (glob("$dir_keymaps_syscons/*.kbd")) {
    my $basename;
    ($basename = $kbdfile) =~ s:.*/::;
    my ($encoding) = $ENCODING{$basename};
    $encoding =~ s/\+/ /g;		# e.g. "ISO8859-1+EURO" -> "ISO8859-1 EURO"
    my $outfile = $FILE_NEW{$basename};
    if ($encoding and $outfile) {
	if (-r $kbdfile) {
	    print "converting from '$basename' ($encoding) to '$outfile' (UCS)\n";
	    my $cmdline = "$dir_convtool/convert-keymap.pl $kbdfile $encoding > $dir_keymaps_output/$outfile";
	    system "$cmdline";
	} else {
	    print "$kbdfile not found\n";
	}
    } else {
	print "Ignore '$basename': No encoding defined in KBDFILES.map\n";
    }
}
