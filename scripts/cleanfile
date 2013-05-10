#!/usr/bin/perl -w
#
# Clean a text file -- or directory of text files -- of stealth whitespace.
# WARNING: this can be a highly destructive operation.  Use with caution.
#

use bytes;
use File::Basename;

# Default options
$max_width = 79;

# Clean up space-tab sequences, either by removing spaces or
# replacing them with tabs.
sub clean_space_tabs($)
{
    no bytes;			# Tab alignment depends on characters

    my($li) = @_;
    my($lo) = '';
    my $pos = 0;
    my $nsp = 0;
    my($i, $c);

    for ($i = 0; $i < length($li); $i++) {
	$c = substr($li, $i, 1);
	if ($c eq "\t") {
	    my $npos = ($pos+$nsp+8) & ~7;
	    my $ntab = ($npos >> 3) - ($pos >> 3);
	    $lo .= "\t" x $ntab;
	    $pos = $npos;
	    $nsp = 0;
	} elsif ($c eq "\n" || $c eq "\r") {
	    $lo .= " " x $nsp;
	    $pos += $nsp;
	    $nsp = 0;
	    $lo .= $c;
	    $pos = 0;
	} elsif ($c eq " ") {
	    $nsp++;
	} else {
	    $lo .= " " x $nsp;
	    $pos += $nsp;
	    $nsp = 0;
	    $lo .= $c;
	    $pos++;
	}
    }
    $lo .= " " x $nsp;
    return $lo;
}

# Compute the visual width of a string
sub strwidth($) {
    no bytes;			# Tab alignment depends on characters

    my($li) = @_;
    my($c, $i);
    my $pos = 0;
    my $mlen = 0;

    for ($i = 0; $i < length($li); $i++) {
	$c = substr($li,$i,1);
	if ($c eq "\t") {
	    $pos = ($pos+8) & ~7;
	} elsif ($c eq "\n") {
	    $mlen = $pos if ($pos > $mlen);
	    $pos = 0;
	} else {
	    $pos++;
	}
    }

    $mlen = $pos if ($pos > $mlen);
    return $mlen;
}

$name = basename($0);

@files = ();

while (defined($a = shift(@ARGV))) {
    if ($a =~ /^-/) {
	if ($a eq '-width' || $a eq '-w') {
	    $max_width = shift(@ARGV)+0;
	} else {
	    print STDERR "Usage: $name [-width #] files...\n";
	    exit 1;
	}
    } else {
	push(@files, $a);
    }
}

foreach $f ( @files ) {
    print STDERR "$name: $f\n";

    if (! -f $f) {
	print STDERR "$f: not a file\n";
	next;
    }

    if (!open(FILE, '+<', $f)) {
	print STDERR "$name: Cannot open file: $f: $!\n";
	next;
    }

    binmode FILE;

    # First, verify that it is not a binary file; consider any file
    # with a zero byte to be a binary file.  Is there any better, or
    # additional, heuristic that should be applied?
    $is_binary = 0;

    while (read(FILE, $data, 65536) > 0) {
	if ($data =~ /\0/) {
	    $is_binary = 1;
	    last;
	}
    }

    if ($is_binary) {
	print STDERR "$name: $f: binary file\n";
	next;
    }

    seek(FILE, 0, 0);

    $in_bytes = 0;
    $out_bytes = 0;
    $blank_bytes = 0;

    @blanks = ();
    @lines  = ();
    $lineno = 0;

    while ( defined($line = <FILE>) ) {
	$lineno++;
	$in_bytes += length($line);
	$line =~ s/[ \t\r]*$//;		# Remove trailing spaces
	$line = clean_space_tabs($line);

	if ( $line eq "\n" ) {
	    push(@blanks, $line);
	    $blank_bytes += length($line);
	} else {
	    push(@lines, @blanks);
	    $out_bytes += $blank_bytes;
	    push(@lines, $line);
	    $out_bytes += length($line);
	    @blanks = ();
	    $blank_bytes = 0;
	}

	$l_width = strwidth($line);
	if ($max_width && $l_width > $max_width) {
	    print STDERR
		"$f:$lineno: line exceeds $max_width characters ($l_width)\n";
	}
    }

    # Any blanks at the end of the file are discarded

    if ($in_bytes != $out_bytes) {
	# Only write to the file if changed
	seek(FILE, 0, 0);
	print FILE @lines;

	if ( !defined($where = tell(FILE)) ||
	     !truncate(FILE, $where) ) {
	    die "$name: Failed to truncate modified file: $f: $!\n";
	}
    }

    close(FILE);
}
