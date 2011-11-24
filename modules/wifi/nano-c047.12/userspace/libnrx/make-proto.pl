# Copyright (c) 1997 - 2004 Kungliga Tekniska Hogskolan
# (Royal Institute of Technology, Stockholm, Sweden). 
# All rights reserved. 
#
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions 
# are met: 
#
# 1. Redistributions of source code must retain the above copyright 
#    notice, this list of conditions and the following disclaimer. 
#
# 2. Neither the name of the Institute nor the names of its contributors 
#    may be used to endorse or promote products derived from this software 
#    without specific prior written permission. 
#
# THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
# ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
# SUCH DAMAGE. 

# Make prototypes from .c files
# $Heimdal: make-proto.pl,v 1.20 2004/09/03 08:50:57 lha Exp $
# $Id: make-proto.pl 15836 2010-07-20 12:13:57Z joda $

##use Getopt::Std;
require 'getopts.pl';

$common_header = "/* Copyright (C) 2007 Nanoradio AB */
/* \$" . "Id\$ */
/* This is a generated file. */
";

$brace = 0;
$line = "";
$debug = 0;
$private_func_re = "^_";

do Getopts('x:m:o:p:dqE:R:P:') || die "foo";

if($opt_d) {
    $debug++;
}

if($opt_R) {
    $private_func_re = $opt_R;
}
%flags = (
	  'multiline-proto' => 1,
	  'gnuc-attribute' => 1,
	  'doxygen' => 1,
	  'sort' => 0
	  );
if($opt_m) {
    foreach $i (split(/,/, $opt_m)) {
	if($i eq "roken") {
	    $flags{"multiline-proto"} = 0;
	    $flags{"gnuc-attribute"} = 0;
	} else {
	    if(substr($i, 0, 3) eq "no-") {
		$flags{substr($i, 3)} = 0;
	    } else {
		$flags{$i} = 1;
	    }
	}
    }
}

if($opt_x) {
    open(EXP, $opt_x);
    while(<EXP>) {
	chomp;
	s/\#.*//g;
	s/\s+/ /g;
	if(/^([a-zA-Z0-9_]+)\s?(.*)$/) {
	    $exported{$1} = $2;
	} else {
	    print $_, "\n";
	}
    }
    close EXP;
}

sub readfile {
    local ($filename) = @_;
    local $_;
    local $s = "";
    local $flag = 1;
    if(open(IN, $filename)) {
	while(<IN>) {
	    if(/END GENERATED PROTOS/) {
		$flag = 1;
	    }
	    if($flag) {
		$s .= $_;
	    }
	    if(/BEGIN GENERATED PROTOS/) {
		$flag = 0;
		$s .= "@@@";
	    }
	}
	close IN;
    }
    return $s;
}

$index = 0;

while(<>) {
    print $brace, " ", $_ if($debug);
##    print "%%", $line, " ", $_ if($debug);
    if(/^\#if 0/) {
	$if_0 = 1;
    }
    if($if_0 && /^\#endif/) {
	$if_0 = 0;
    }
    if($if_0) { next }
    if(/^\s*\#/) {
	next;
    }
    if(/^\s*$/) {
	$line = "";
	next;
    }
    if(/^\{/){
	if (!/\}/) {
	    $brace++;
	}
	$_ = $line;
	while(s,//.*,,) {} ## remove // comments
	while(s/\*\//\ca/){
	    if(/\/\*((.|\n)*)\ca/) {
		if(index($1, "@") >= 0) {
		    print ">>", $1, "<<\n" if($debug);
		    $doxycomment = $1;
		}
	    }
	    s/\/\*(.|\n)*\ca//;
	}
	s/^\s*//;
	s/\s*$//;
	s/\s+/ /g;
	if($_ =~ /\)$/){
	    if(!/^static/ && !/^PRIVATE/){
		if(/(.*)(__attribute__\s?\(.*\))/) {
		    $attr = $2;
		    $_ = $1;
		} else {
		    $attr = "";
		}
		# remove outer ()
		s/\s*\(/</;
		s/\)\s?$/>/;
		# remove , within ()
		while(s/\(([^()]*),(.*)\)/($1\$$2)/g){}
		s/\<\s*void\s*\>/<>/;
		# remove parameter names 
		if($opt_P eq "remove") {
		    s/(\s*)([a-zA-Z0-9_]+)([,>])/$3/g;
		    s/\s+\*/*/g;
		    s/\(\*(\s*)([a-zA-Z0-9_]+)\)/(*)/g;
		} elsif($opt_P eq "comment") {
		    s/([a-zA-Z0-9_]+)([,>])/\/\*$1\*\/$2/g;
		    s/\(\*([a-zA-Z0-9_]+)\)/(*\/\*$1\*\/)/g;
		}
		s/\<\>/<void>/;
		# insert newline before function name
		if($flags{"multiline-proto"}) {
		    s/(.*)\s([a-zA-Z0-9_]+\s*<)/$1\n$2/;
		    $indent = " " x (length($2) + 1);
		}
		# add newlines before parameters
		if($flags{"multiline-proto"}) {
		    s/,\s*/,\n$indent/g;
		} else {
		    s/,\s*/, /g;
		}
		# fix removed ,
		s/\$/,/g;
		# match function name
		/([a-zA-Z0-9_]+)\s*\</;
		$f = $1;
		# only add newline if more than one parameter
##              if($flags{"multiline-proto"} && /,/){ 
##		    s/\</ (\n\t/;
##		}else{
		    s/\</ (/;
##		}
		s/\>/)/;
		if($attr ne "") {
		    $_ .= "\n    $attr";
		}
		$_ = $_ . ";";
		$funcs{$f} = $_;
		$findex{$f} = $index++;
		if($doxycomment) {
		    $doxy{$f} = $doxycomment;
		}
	    }
	}
	$line = "";
	$doxycomment = "";
    }
    if(/^\}/){
	$brace--;
    }
    if(/^\}/){
	$brace = 0;
    }
    if($brace == 0) {
	$line = $line . " " . $_;
    }
}

sub foo {
    local ($arg) = @_;
    $_ = $arg;
    s/.*\/([^\/]*)/$1/;
    s/[^a-zA-Z0-9]/_/g;
    "__" . $_ . "__";
}

if($opt_o) {
    if(open(IN, "$opt_o")) {
	$proto = <IN>;
	close IN;
    }
    open(OUT, ">$opt_o" . ".new");
    $block = &foo($opt_o);
} else {
    $block = "__public_h__";
}

if($opt_p) {
    open(PRIV, ">$opt_p" . ".new");
    $private = &foo($opt_p);
} else {
    $private = "__private_h__";
}

$public_h = "";
$private_h = "";

sub make_default_file {
    local ($block) = @_;
    local $file = "";
    $file = $common_header;
    $file .= "#ifndef $block
#define $block

#include <stdarg.h>

/* BEGIN GENERATED PROTOS */
\@\@\@
/* END GENERATED PROTOS */

#endif /* $block */
";
    
    return $file;
}

if($opt_o) {
    $public_header = readfile($opt_o);
    if($public_header eq "") {
	$public_header = make_default_file($block);
    }
}
if($opt_o) {
    $private_header = readfile($opt_p);
    if($private_header eq "") {
	$private_header = make_default_file($private);
    }
}

@fkeys = keys %funcs;
if ($flags{"sort"}) {
    @fkeys = sort @fkeys;
} else {
    @fkeys = sort { $findex{$a} <=> $findex{$b} } @fkeys;
}
sub is_private {
    my ($func) = @_;
    print "==>1", $func, "\n" if ($debug);
    if(!defined($exported{$func}) &&
       $func =~ /$private_func_re/) {
	return 1;
    }
    print "==>2", $func, "\n" if ($debug);
    if($flags{"doxygen"} && !defined $doxy{$func}) {
	return 1;
    }
    print "==>3", $func, "\n" if ($debug);
    if($flags{"doxygen"} && index($doxy{$func}, "\@internal") >= 0) {
	return 1;
    }
    print "==>4", $func, "\n" if ($debug);
    return 0;
}

foreach(@fkeys){
    if(/^(main)$/) { next }
    if(is_private($_)) {
	if($flags{"doxygen"} && defined $doxy{$_}) {
	    $private_h .= "/*" . $doxy{$_} . "*/\n";
	}
	$private_h .= $funcs{$_} . "\n\n";
	if($funcs{$_} =~ /__attribute__/) {
	    $private_attribute_seen = 1;
	}
    } else {
	if($flags{"doxygen"} && defined $doxy{$_}) {
	    $public_h .= "/*" . $doxy{$_} . "*/\n";
	}
	$public_h .= $funcs{$_} . "\n";
	if($funcs{$_} =~ /__attribute__/) {
	    $public_attribute_seen = 1;
	}
	$public_h .= "\n";
    }
}

$gnu_attribute = "#if !defined(__GNUC__) && !defined(__attribute__)
#define __attribute__(x)
#endif

";

if($flags{"gnuc-attribute"}) {
    if ($public_attribute_seen) {
	$public_h = $gnu_attribute . $public_h;
    }

    if ($private_attribute_seen) {
	$public_h = $gnu_attribute . $private_h;
    }
}

sub try_rename {
    my ($file) = @_;
    local ($newfile) = $file . ".new";
    `cmp -s "$file" "$newfile"`;
    if($?) {
	rename $file, $file . '~' or die "failed to rename";
	rename $newfile, $file or die "failed to rename";
    } else {
	unlink $newfile or die "failed to remove";
    }
}

if($opt_o) {
    $public_header =~ s/\@\@\@\n?/\n$public_h/;
    print OUT $public_header;
    close OUT;
    try_rename($opt_o);
} 
if($opt_p) {
    $private_header =~ s/\@\@\@\n?/\n$private_h/;
    print PRIV $private_header;
    close PRIV;
    try_rename($opt_p);
} 
