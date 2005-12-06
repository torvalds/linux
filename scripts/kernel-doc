#!/usr/bin/perl -w

use strict;

## Copyright (c) 1998 Michael Zucchi, All Rights Reserved        ##
## Copyright (C) 2000, 1  Tim Waugh <twaugh@redhat.com>          ##
## Copyright (C) 2001  Simon Huggins                             ##
## 								 ##
## #define enhancements by Armin Kuster <akuster@mvista.com>	 ##
## Copyright (c) 2000 MontaVista Software, Inc.			 ##
## 								 ##
## This software falls under the GNU General Public License.     ##
## Please read the COPYING file for more information             ##

# w.o. 03-11-2000: added the '-filelist' option.

# 18/01/2001 - 	Cleanups
# 		Functions prototyped as foo(void) same as foo()
# 		Stop eval'ing where we don't need to.
# -- huggie@earth.li

# 27/06/2001 -  Allowed whitespace after initial "/**" and
#               allowed comments before function declarations.
# -- Christian Kreibich <ck@whoop.org>

# Still to do:
# 	- add perldoc documentation
# 	- Look more closely at some of the scarier bits :)

# 26/05/2001 - 	Support for separate source and object trees.
#		Return error code.
# 		Keith Owens <kaos@ocs.com.au>

# 23/09/2001 - Added support for typedefs, structs, enums and unions
#              Support for Context section; can be terminated using empty line
#              Small fixes (like spaces vs. \s in regex)
# -- Tim Jansen <tim@tjansen.de>


#
# This will read a 'c' file and scan for embedded comments in the
# style of gnome comments (+minor extensions - see below).
#

# Note: This only supports 'c'.

# usage:
# kerneldoc [ -docbook | -html | -text | -man ]
#           [ -function funcname [ -function funcname ...] ] c file(s)s > outputfile
# or
#           [ -nofunction funcname [ -function funcname ...] ] c file(s)s > outputfile
#
#  Set output format using one of -docbook -html -text or -man.  Default is man.
#
#  -function funcname
#	If set, then only generate documentation for the given function(s).  All
#	other functions are ignored.
#
#  -nofunction funcname
#	If set, then only generate documentation for the other function(s).  All
#	other functions are ignored. Cannot be used with -function together
#	(yes thats a bug - perl hackers can fix it 8))
#
#  c files - list of 'c' files to process
#
#  All output goes to stdout, with errors to stderr.

#
# format of comments.
# In the following table, (...)? signifies optional structure.
#                         (...)* signifies 0 or more structure elements
# /**
#  * function_name(:)? (- short description)?
# (* @parameterx: (description of parameter x)?)*
# (* a blank line)?
#  * (Description:)? (Description of function)?
#  * (section header: (section description)? )*
#  (*)?*/
#
# So .. the trivial example would be:
#
# /**
#  * my_function
#  **/
#
# If the Description: header tag is ommitted, then there must be a blank line
# after the last parameter specification.
# e.g.
# /**
#  * my_function - does my stuff
#  * @my_arg: its mine damnit
#  *
#  * Does my stuff explained. 
#  */
#
#  or, could also use:
# /**
#  * my_function - does my stuff
#  * @my_arg: its mine damnit
#  * Description: Does my stuff explained. 
#  */
# etc.
#
# Beside functions you can also write documentation for structs, unions, 
# enums and typedefs. Instead of the function name you must write the name 
# of the declaration;  the struct/union/enum/typedef must always precede 
# the name. Nesting of declarations is not supported. 
# Use the argument mechanism to document members or constants.
# e.g.
# /**
#  * struct my_struct - short description
#  * @a: first member
#  * @b: second member
#  * 
#  * Longer description
#  */
# struct my_struct {
#     int a;
#     int b;
# /* private: */
#     int c;
# };
#
# All descriptions can be multiline, except the short function description.
# 
# You can also add additional sections. When documenting kernel functions you 
# should document the "Context:" of the function, e.g. whether the functions 
# can be called form interrupts. Unlike other sections you can end it with an
# empty line. 
# Example-sections should contain the string EXAMPLE so that they are marked 
# appropriately in DocBook.
#
# Example:
# /**
#  * user_function - function that can only be called in user context
#  * @a: some argument
#  * Context: !in_interrupt()
#  * 
#  * Some description
#  * Example:
#  *    user_function(22);
#  */
# ...
#
#
# All descriptive text is further processed, scanning for the following special
# patterns, which are highlighted appropriately.
#
# 'funcname()' - function
# '$ENVVAR' - environmental variable
# '&struct_name' - name of a structure (up to two words including 'struct')
# '@parameter' - name of a parameter
# '%CONST' - name of a constant.

my $errors = 0;
my $warnings = 0;

# match expressions used to find embedded type information
my $type_constant = '\%([-_\w]+)';
my $type_func = '(\w+)\(\)';
my $type_param = '\@(\w+)';
my $type_struct = '\&((struct\s*)?[_\w]+)';
my $type_env = '(\$\w+)';

# Output conversion substitutions.
#  One for each output format

# these work fairly well
my %highlights_html = ( $type_constant, "<i>\$1</i>",
			$type_func, "<b>\$1</b>",
			$type_struct, "<i>\$1</i>",
			$type_param, "<tt><b>\$1</b></tt>" );
my $blankline_html = "<p>";

# XML, docbook format
my %highlights_xml = ( "([^=])\\\"([^\\\"<]+)\\\"", "\$1<quote>\$2</quote>",
			$type_constant, "<constant>\$1</constant>",
			$type_func, "<function>\$1</function>",
			$type_struct, "<structname>\$1</structname>",
			$type_env, "<envar>\$1</envar>",
			$type_param, "<parameter>\$1</parameter>" );
my $blankline_xml = "</para><para>\n";

# gnome, docbook format
my %highlights_gnome = ( $type_constant, "<replaceable class=\"option\">\$1</replaceable>",
			 $type_func, "<function>\$1</function>",
			 $type_struct, "<structname>\$1</structname>",
			 $type_env, "<envar>\$1</envar>",
			 $type_param, "<parameter>\$1</parameter>" );
my $blankline_gnome = "</para><para>\n";

# these are pretty rough
my %highlights_man = ( $type_constant, "\$1",
		       $type_func, "\\\\fB\$1\\\\fP",
		       $type_struct, "\\\\fI\$1\\\\fP",
		       $type_param, "\\\\fI\$1\\\\fP" );
my $blankline_man = "";

# text-mode
my %highlights_text = ( $type_constant, "\$1",
			$type_func, "\$1",
			$type_struct, "\$1",
			$type_param, "\$1" );
my $blankline_text = "";


sub usage {
    print "Usage: $0 [ -v ] [ -docbook | -html | -text | -man ]\n";
    print "         [ -function funcname [ -function funcname ...] ]\n";
    print "         [ -nofunction funcname [ -nofunction funcname ...] ]\n";
    print "         c source file(s) > outputfile\n";
    exit 1;
}

# read arguments
if ($#ARGV==-1) {
    usage();
}

my $verbose = 0;
my $output_mode = "man";
my %highlights = %highlights_man;
my $blankline = $blankline_man;
my $modulename = "Kernel API";
my $function_only = 0;
my $man_date = ('January', 'February', 'March', 'April', 'May', 'June', 
		'July', 'August', 'September', 'October', 
		'November', 'December')[(localtime)[4]] . 
  " " . ((localtime)[5]+1900);

# Essentially these are globals
# They probably want to be tidied up made more localised or summat.
# CAVEAT EMPTOR!  Some of the others I localised may not want to be which
# could cause "use of undefined value" or other bugs.
my ($function, %function_table,%parametertypes,$declaration_purpose);
my ($type,$declaration_name,$return_type);
my ($newsection,$newcontents,$prototype,$filelist, $brcount, %source_map);

# Generated docbook code is inserted in a template at a point where 
# docbook v3.1 requires a non-zero sequence of RefEntry's; see:
# http://www.oasis-open.org/docbook/documentation/reference/html/refentry.html
# We keep track of number of generated entries and generate a dummy
# if needs be to ensure the expanded template can be postprocessed
# into html.
my $section_counter = 0;

my $lineprefix="";

# states
# 0 - normal code
# 1 - looking for function name
# 2 - scanning field start.
# 3 - scanning prototype.
# 4 - documentation block
my $state;

#declaration types: can be
# 'function', 'struct', 'union', 'enum', 'typedef'
my $decl_type;

my $doc_special = "\@\%\$\&";

my $doc_start = '^/\*\*\s*$'; # Allow whitespace at end of comment start.
my $doc_end = '\*/';
my $doc_com = '\s*\*\s*';
my $doc_decl = $doc_com.'(\w+)';
my $doc_sect = $doc_com.'(['.$doc_special.']?[\w ]+):(.*)';
my $doc_content = $doc_com.'(.*)';
my $doc_block = $doc_com.'DOC:\s*(.*)?';

my %constants;
my %parameterdescs;
my @parameterlist;
my %sections;
my @sectionlist;

my $contents = "";
my $section_default = "Description";	# default section
my $section_intro = "Introduction";
my $section = $section_default;
my $section_context = "Context";

my $undescribed = "-- undescribed --";

reset_state();

while ($ARGV[0] =~ m/^-(.*)/) {
    my $cmd = shift @ARGV;
    if ($cmd eq "-html") {
	$output_mode = "html";
	%highlights = %highlights_html;
	$blankline = $blankline_html;
    } elsif ($cmd eq "-man") {
	$output_mode = "man";
	%highlights = %highlights_man;
	$blankline = $blankline_man;
    } elsif ($cmd eq "-text") {
	$output_mode = "text";
	%highlights = %highlights_text;
	$blankline = $blankline_text;
    } elsif ($cmd eq "-docbook") {
	$output_mode = "xml";
	%highlights = %highlights_xml;
	$blankline = $blankline_xml;
    } elsif ($cmd eq "-gnome") {
	$output_mode = "gnome";
	%highlights = %highlights_gnome;
	$blankline = $blankline_gnome;
    } elsif ($cmd eq "-module") { # not needed for XML, inherits from calling document
	$modulename = shift @ARGV;
    } elsif ($cmd eq "-function") { # to only output specific functions
	$function_only = 1;
	$function = shift @ARGV;
	$function_table{$function} = 1;
    } elsif ($cmd eq "-nofunction") { # to only output specific functions
	$function_only = 2;
	$function = shift @ARGV;
	$function_table{$function} = 1;
    } elsif ($cmd eq "-v") {
	$verbose = 1;
    } elsif (($cmd eq "-h") || ($cmd eq "--help")) {
	usage();
    } elsif ($cmd eq '-filelist') {
	    $filelist = shift @ARGV;
    }
}


# generate a sequence of code that will splice in highlighting information
# using the s// operator.
my $dohighlight = "";
foreach my $pattern (keys %highlights) {
#    print "scanning pattern $pattern ($highlights{$pattern})\n";
    $dohighlight .=  "\$contents =~ s:$pattern:$highlights{$pattern}:gs;\n";
}

##
# dumps section contents to arrays/hashes intended for that purpose.
#
sub dump_section {
    my $name = shift;
    my $contents = join "\n", @_;

    if ($name =~ m/$type_constant/) {
	$name = $1;
#	print STDERR "constant section '$1' = '$contents'\n";
	$constants{$name} = $contents;
    } elsif ($name =~ m/$type_param/) {
#	print STDERR "parameter def '$1' = '$contents'\n";
	$name = $1;
	$parameterdescs{$name} = $contents;
    } else {
#	print STDERR "other section '$name' = '$contents'\n";
	$sections{$name} = $contents;
	push @sectionlist, $name;
    }
}

##
# output function
#
# parameterdescs, a hash.
#  function => "function name"
#  parameterlist => @list of parameters
#  parameterdescs => %parameter descriptions
#  sectionlist => @list of sections
#  sections => %descriont descriptions
#  

sub output_highlight {
    my $contents = join "\n",@_;
    my $line;

#   DEBUG
#   if (!defined $contents) {
#	use Carp;
#	confess "output_highlight got called with no args?\n";
#   }

    eval $dohighlight;
    die $@ if $@;
    foreach $line (split "\n", $contents) {
      if ($line eq ""){
	    print $lineprefix, $blankline;
	} else {
            $line =~ s/\\\\\\/\&/g;
	    print $lineprefix, $line;
	}
	print "\n";
    }
}

#output sections in html
sub output_section_html(%) {
    my %args = %{$_[0]};
    my $section;

    foreach $section (@{$args{'sectionlist'}}) {
	print "<h3>$section</h3>\n";
	print "<blockquote>\n";
	output_highlight($args{'sections'}{$section});
	print "</blockquote>\n";
    }  
}

# output enum in html
sub output_enum_html(%) {
    my %args = %{$_[0]};
    my ($parameter);
    my $count;
    print "<h2>enum ".$args{'enum'}."</h2>\n";

    print "<b>enum ".$args{'enum'}."</b> {<br>\n";
    $count = 0;
    foreach $parameter (@{$args{'parameterlist'}}) {
        print " <b>".$parameter."</b>";
	if ($count != $#{$args{'parameterlist'}}) {
	    $count++;
	    print ",\n";
	}
	print "<br>";
    }
    print "};<br>\n";

    print "<h3>Constants</h3>\n";
    print "<dl>\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	print "<dt><b>".$parameter."</b>\n";
	print "<dd>";
	output_highlight($args{'parameterdescs'}{$parameter});
    }
    print "</dl>\n";
    output_section_html(@_);
    print "<hr>\n";
}

# output tyepdef in html
sub output_typedef_html(%) {
    my %args = %{$_[0]};
    my ($parameter);
    my $count;
    print "<h2>typedef ".$args{'typedef'}."</h2>\n";

    print "<b>typedef ".$args{'typedef'}."</b>\n";
    output_section_html(@_);
    print "<hr>\n";
}

# output struct in html
sub output_struct_html(%) {
    my %args = %{$_[0]};
    my ($parameter);

    print "<h2>".$args{'type'}." ".$args{'struct'}."</h2>\n";
    print "<b>".$args{'type'}." ".$args{'struct'}."</b> {<br>\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	if ($parameter =~ /^#/) {
		print "$parameter<br>\n";
		next;
	}
	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

        ($args{'parameterdescs'}{$parameter_name} ne $undescribed) || next;
	$type = $args{'parametertypes'}{$parameter};
	if ($type =~ m/([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)/) {
	    # pointer-to-function
	    print " <i>$1</i><b>$parameter</b>) <i>($2)</i>;<br>\n";
	} elsif ($type =~ m/^(.*?)\s*(:.*)/) {
	    print " <i>$1</i> <b>$parameter</b>$2;<br>\n";
	} else {
	    print " <i>$type</i> <b>$parameter</b>;<br>\n";
	}
    }
    print "};<br>\n";

    print "<h3>Members</h3>\n";
    print "<dl>\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	($parameter =~ /^#/) && next;

	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

        ($args{'parameterdescs'}{$parameter_name} ne $undescribed) || next;
	print "<dt><b>".$parameter."</b>\n";
	print "<dd>";
	output_highlight($args{'parameterdescs'}{$parameter_name});
    }
    print "</dl>\n";
    output_section_html(@_);
    print "<hr>\n";
}

# output function in html
sub output_function_html(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $count;
    print "<h2>Function</h2>\n";

    print "<i>".$args{'functiontype'}."</i>\n";
    print "<b>".$args{'function'}."</b>\n";
    print "(";
    $count = 0;
    foreach $parameter (@{$args{'parameterlist'}}) {
	$type = $args{'parametertypes'}{$parameter};
	if ($type =~ m/([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)/) {
	    # pointer-to-function
	    print "<i>$1</i><b>$parameter</b>) <i>($2)</i>";
	} else {
	    print "<i>".$type."</i> <b>".$parameter."</b>";
	}
	if ($count != $#{$args{'parameterlist'}}) {
	    $count++;
	    print ",\n";
	}
    }
    print ")\n";

    print "<h3>Arguments</h3>\n";
    print "<dl>\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

        ($args{'parameterdescs'}{$parameter_name} ne $undescribed) || next;
	print "<dt><b>".$parameter."</b>\n";
	print "<dd>";
	output_highlight($args{'parameterdescs'}{$parameter_name});
    }
    print "</dl>\n";
    output_section_html(@_);
    print "<hr>\n";
}

# output intro in html
sub output_intro_html(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $count;

    foreach $section (@{$args{'sectionlist'}}) {
	print "<h3>$section</h3>\n";
	print "<ul>\n";
	output_highlight($args{'sections'}{$section});
	print "</ul>\n";
    }
    print "<hr>\n";
}

sub output_section_xml(%) {
    my %args = %{$_[0]};
    my $section;    
    # print out each section
    $lineprefix="   ";
    foreach $section (@{$args{'sectionlist'}}) {
	print "<refsect1>\n";
	print "<title>$section</title>\n";
	if ($section =~ m/EXAMPLE/i) {
	    print "<informalexample><programlisting>\n";
	} else {
	    print "<para>\n";
	}
	output_highlight($args{'sections'}{$section});
	if ($section =~ m/EXAMPLE/i) {
	    print "</programlisting></informalexample>\n";
	} else {
	    print "</para>\n";
	}
	print "</refsect1>\n";
    }
}

# output function in XML DocBook
sub output_function_xml(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $count;
    my $id;

    $id = "API-".$args{'function'};
    $id =~ s/[^A-Za-z0-9]/-/g;

    print "<refentry>\n";
    print "<refentryinfo>\n";
    print " <title>LINUX</title>\n";
    print " <productname>Kernel Hackers Manual</productname>\n";
    print " <date>$man_date</date>\n";
    print "</refentryinfo>\n";
    print "<refmeta>\n";
    print " <refentrytitle><phrase id=\"$id\">".$args{'function'}."</phrase></refentrytitle>\n";
    print " <manvolnum>9</manvolnum>\n";
    print "</refmeta>\n";
    print "<refnamediv>\n";
    print " <refname>".$args{'function'}."</refname>\n";
    print " <refpurpose>\n";
    print "  ";
    output_highlight ($args{'purpose'});
    print " </refpurpose>\n";
    print "</refnamediv>\n";

    print "<refsynopsisdiv>\n";
    print " <title>Synopsis</title>\n";
    print "  <funcsynopsis><funcprototype>\n";
    print "   <funcdef>".$args{'functiontype'}." ";
    print "<function>".$args{'function'}." </function></funcdef>\n";

    $count = 0;
    if ($#{$args{'parameterlist'}} >= 0) {
	foreach $parameter (@{$args{'parameterlist'}}) {
	    $type = $args{'parametertypes'}{$parameter};
	    if ($type =~ m/([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)/) {
		# pointer-to-function
		print "   <paramdef>$1<parameter>$parameter</parameter>)\n";
		print "     <funcparams>$2</funcparams></paramdef>\n";
	    } else {
		print "   <paramdef>".$type;
		print " <parameter>$parameter</parameter></paramdef>\n";
	    }
	}
    } else {
	print "  <void/>\n";
    }
    print "  </funcprototype></funcsynopsis>\n";
    print "</refsynopsisdiv>\n";

    # print parameters
    print "<refsect1>\n <title>Arguments</title>\n";
    if ($#{$args{'parameterlist'}} >= 0) {
	print " <variablelist>\n";
	foreach $parameter (@{$args{'parameterlist'}}) {
	    my $parameter_name = $parameter;
	    $parameter_name =~ s/\[.*//;

	    print "  <varlistentry>\n   <term><parameter>$parameter</parameter></term>\n";
	    print "   <listitem>\n    <para>\n";
	    $lineprefix="     ";
	    output_highlight($args{'parameterdescs'}{$parameter_name});
	    print "    </para>\n   </listitem>\n  </varlistentry>\n";
	}
	print " </variablelist>\n";
    } else {
	print " <para>\n  None\n </para>\n";
    }
    print "</refsect1>\n";

    output_section_xml(@_);
    print "</refentry>\n\n";
}

# output struct in XML DocBook
sub output_struct_xml(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $id;

    $id = "API-struct-".$args{'struct'};
    $id =~ s/[^A-Za-z0-9]/-/g;

    print "<refentry>\n";
    print "<refentryinfo>\n";
    print " <title>LINUX</title>\n";
    print " <productname>Kernel Hackers Manual</productname>\n";
    print " <date>$man_date</date>\n";
    print "</refentryinfo>\n";
    print "<refmeta>\n";
    print " <refentrytitle><phrase id=\"$id\">".$args{'type'}." ".$args{'struct'}."</phrase></refentrytitle>\n";
    print " <manvolnum>9</manvolnum>\n";
    print "</refmeta>\n";
    print "<refnamediv>\n";
    print " <refname>".$args{'type'}." ".$args{'struct'}."</refname>\n";
    print " <refpurpose>\n";
    print "  ";
    output_highlight ($args{'purpose'});
    print " </refpurpose>\n";
    print "</refnamediv>\n";

    print "<refsynopsisdiv>\n";
    print " <title>Synopsis</title>\n";
    print "  <programlisting>\n";
    print $args{'type'}." ".$args{'struct'}." {\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	if ($parameter =~ /^#/) {
	    print "$parameter\n";
	    next;
	}

	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

	defined($args{'parameterdescs'}{$parameter_name}) || next;
        ($args{'parameterdescs'}{$parameter_name} ne $undescribed) || next;
	$type = $args{'parametertypes'}{$parameter};
	if ($type =~ m/([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)/) {
	    # pointer-to-function
	    print "  $1 $parameter) ($2);\n";
	} elsif ($type =~ m/^(.*?)\s*(:.*)/) {
	    print "  $1 $parameter$2;\n";
	} else {
	    print "  ".$type." ".$parameter.";\n";
	}
    }
    print "};";
    print "  </programlisting>\n";
    print "</refsynopsisdiv>\n";

    print " <refsect1>\n";
    print "  <title>Members</title>\n";

    print "  <variablelist>\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
      ($parameter =~ /^#/) && next;

      my $parameter_name = $parameter;
      $parameter_name =~ s/\[.*//;

      defined($args{'parameterdescs'}{$parameter_name}) || next;
      ($args{'parameterdescs'}{$parameter_name} ne $undescribed) || next;
      print "    <varlistentry>";
      print "      <term>$parameter</term>\n";
      print "      <listitem><para>\n";
      output_highlight($args{'parameterdescs'}{$parameter_name});
      print "      </para></listitem>\n";
      print "    </varlistentry>\n";
    }
    print "  </variablelist>\n";
    print " </refsect1>\n";

    output_section_xml(@_);

    print "</refentry>\n\n";
}

# output enum in XML DocBook
sub output_enum_xml(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $count;
    my $id;

    $id = "API-enum-".$args{'enum'};
    $id =~ s/[^A-Za-z0-9]/-/g;

    print "<refentry>\n";
    print "<refentryinfo>\n";
    print " <title>LINUX</title>\n";
    print " <productname>Kernel Hackers Manual</productname>\n";
    print " <date>$man_date</date>\n";
    print "</refentryinfo>\n";
    print "<refmeta>\n";
    print " <refentrytitle><phrase id=\"$id\">enum ".$args{'enum'}."</phrase></refentrytitle>\n";
    print " <manvolnum>9</manvolnum>\n";
    print "</refmeta>\n";
    print "<refnamediv>\n";
    print " <refname>enum ".$args{'enum'}."</refname>\n";
    print " <refpurpose>\n";
    print "  ";
    output_highlight ($args{'purpose'});
    print " </refpurpose>\n";
    print "</refnamediv>\n";

    print "<refsynopsisdiv>\n";
    print " <title>Synopsis</title>\n";
    print "  <programlisting>\n";
    print "enum ".$args{'enum'}." {\n";
    $count = 0;
    foreach $parameter (@{$args{'parameterlist'}}) {
        print "  $parameter";
        if ($count != $#{$args{'parameterlist'}}) {
	    $count++;
	    print ",";
        }
	print "\n";
    }
    print "};";
    print "  </programlisting>\n";
    print "</refsynopsisdiv>\n";

    print "<refsect1>\n";
    print " <title>Constants</title>\n";    
    print "  <variablelist>\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
      my $parameter_name = $parameter;
      $parameter_name =~ s/\[.*//;

      print "    <varlistentry>";
      print "      <term>$parameter</term>\n";
      print "      <listitem><para>\n";
      output_highlight($args{'parameterdescs'}{$parameter_name});
      print "      </para></listitem>\n";
      print "    </varlistentry>\n";
    }
    print "  </variablelist>\n";
    print "</refsect1>\n";

    output_section_xml(@_);

    print "</refentry>\n\n";
}

# output typedef in XML DocBook
sub output_typedef_xml(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $id;

    $id = "API-typedef-".$args{'typedef'};
    $id =~ s/[^A-Za-z0-9]/-/g;

    print "<refentry>\n";
    print "<refentryinfo>\n";
    print " <title>LINUX</title>\n";
    print " <productname>Kernel Hackers Manual</productname>\n";
    print " <date>$man_date</date>\n";
    print "</refentryinfo>\n";
    print "<refmeta>\n";
    print " <refentrytitle><phrase id=\"$id\">typedef ".$args{'typedef'}."</phrase></refentrytitle>\n";
    print " <manvolnum>9</manvolnum>\n";
    print "</refmeta>\n";
    print "<refnamediv>\n";
    print " <refname>typedef ".$args{'typedef'}."</refname>\n";
    print " <refpurpose>\n";
    print "  ";
    output_highlight ($args{'purpose'});
    print " </refpurpose>\n";
    print "</refnamediv>\n";

    print "<refsynopsisdiv>\n";
    print " <title>Synopsis</title>\n";
    print "  <synopsis>typedef ".$args{'typedef'}.";</synopsis>\n";
    print "</refsynopsisdiv>\n";

    output_section_xml(@_);

    print "</refentry>\n\n";
}

# output in XML DocBook
sub output_intro_xml(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $count;

    my $id = $args{'module'};
    $id =~ s/[^A-Za-z0-9]/-/g;

    # print out each section
    $lineprefix="   ";
    foreach $section (@{$args{'sectionlist'}}) {
	print "<refsect1>\n <title>$section</title>\n <para>\n";
	if ($section =~ m/EXAMPLE/i) {
	    print "<example><para>\n";
	}
	output_highlight($args{'sections'}{$section});
	if ($section =~ m/EXAMPLE/i) {
	    print "</para></example>\n";
	}
	print " </para>\n</refsect1>\n";
    }

    print "\n\n";
}

# output in XML DocBook
sub output_function_gnome {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $count;
    my $id;

    $id = $args{'module'}."-".$args{'function'};
    $id =~ s/[^A-Za-z0-9]/-/g;

    print "<sect2>\n";
    print " <title id=\"$id\">".$args{'function'}."</title>\n";

    print "  <funcsynopsis>\n";
    print "   <funcdef>".$args{'functiontype'}." ";
    print "<function>".$args{'function'}." ";
    print "</function></funcdef>\n";

    $count = 0;
    if ($#{$args{'parameterlist'}} >= 0) {
	foreach $parameter (@{$args{'parameterlist'}}) {
	    $type = $args{'parametertypes'}{$parameter};
	    if ($type =~ m/([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)/) {
		# pointer-to-function
		print "   <paramdef>$1 <parameter>$parameter</parameter>)\n";
		print "     <funcparams>$2</funcparams></paramdef>\n";
	    } else {
		print "   <paramdef>".$type;
		print " <parameter>$parameter</parameter></paramdef>\n";
	    }
	}
    } else {
	print "  <void>\n";
    }
    print "  </funcsynopsis>\n";
    if ($#{$args{'parameterlist'}} >= 0) {
	print " <informaltable pgwide=\"1\" frame=\"none\" role=\"params\">\n";
	print "<tgroup cols=\"2\">\n";
	print "<colspec colwidth=\"2*\">\n";
	print "<colspec colwidth=\"8*\">\n";
	print "<tbody>\n";
	foreach $parameter (@{$args{'parameterlist'}}) {
	    my $parameter_name = $parameter;
	    $parameter_name =~ s/\[.*//;

	    print "  <row><entry align=\"right\"><parameter>$parameter</parameter></entry>\n";
	    print "   <entry>\n";
	    $lineprefix="     ";
	    output_highlight($args{'parameterdescs'}{$parameter_name});
	    print "    </entry></row>\n";
	}
	print " </tbody></tgroup></informaltable>\n";
    } else {
	print " <para>\n  None\n </para>\n";
    }

    # print out each section
    $lineprefix="   ";
    foreach $section (@{$args{'sectionlist'}}) {
	print "<simplesect>\n <title>$section</title>\n";
	if ($section =~ m/EXAMPLE/i) {
	    print "<example><programlisting>\n";
	} else {
	}
	print "<para>\n";
	output_highlight($args{'sections'}{$section});
	print "</para>\n";
	if ($section =~ m/EXAMPLE/i) {
	    print "</programlisting></example>\n";
	} else {
	}
	print " </simplesect>\n";
    }

    print "</sect2>\n\n";
}

##
# output function in man
sub output_function_man(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $count;

    print ".TH \"$args{'function'}\" 9 \"$args{'function'}\" \"$man_date\" \"Kernel Hacker's Manual\" LINUX\n";

    print ".SH NAME\n";
    print $args{'function'}." \\- ".$args{'purpose'}."\n";

    print ".SH SYNOPSIS\n";
    print ".B \"".$args{'functiontype'}."\" ".$args{'function'}."\n";
    $count = 0;
    my $parenth = "(";
    my $post = ",";
    foreach my $parameter (@{$args{'parameterlist'}}) {
	if ($count == $#{$args{'parameterlist'}}) {
	    $post = ");";
	}
	$type = $args{'parametertypes'}{$parameter};
	if ($type =~ m/([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)/) {
	    # pointer-to-function
	    print ".BI \"".$parenth.$1."\" ".$parameter." \") (".$2.")".$post."\"\n";
	} else {
	    $type =~ s/([^\*])$/$1 /;
	    print ".BI \"".$parenth.$type."\" ".$parameter." \"".$post."\"\n";
	}
	$count++;
	$parenth = "";
    }

    print ".SH ARGUMENTS\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

	print ".IP \"".$parameter."\" 12\n";
	output_highlight($args{'parameterdescs'}{$parameter_name});
    }
    foreach $section (@{$args{'sectionlist'}}) {
	print ".SH \"", uc $section, "\"\n";
	output_highlight($args{'sections'}{$section});
    }
}

##
# output enum in man
sub output_enum_man(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $count;

    print ".TH \"$args{'module'}\" 9 \"enum $args{'enum'}\" \"$man_date\" \"API Manual\" LINUX\n";

    print ".SH NAME\n";
    print "enum ".$args{'enum'}." \\- ".$args{'purpose'}."\n";

    print ".SH SYNOPSIS\n";
    print "enum ".$args{'enum'}." {\n";
    $count = 0;
    foreach my $parameter (@{$args{'parameterlist'}}) {
        print ".br\n.BI \"    $parameter\"\n";
	if ($count == $#{$args{'parameterlist'}}) {
	    print "\n};\n";
	    last;
	}
	else {
	    print ", \n.br\n";
	}
	$count++;
    }

    print ".SH Constants\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

	print ".IP \"".$parameter."\" 12\n";
	output_highlight($args{'parameterdescs'}{$parameter_name});
    }
    foreach $section (@{$args{'sectionlist'}}) {
	print ".SH \"$section\"\n";
	output_highlight($args{'sections'}{$section});
    }
}

##
# output struct in man
sub output_struct_man(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);

    print ".TH \"$args{'module'}\" 9 \"".$args{'type'}." ".$args{'struct'}."\" \"$man_date\" \"API Manual\" LINUX\n";

    print ".SH NAME\n";
    print $args{'type'}." ".$args{'struct'}." \\- ".$args{'purpose'}."\n";

    print ".SH SYNOPSIS\n";
    print $args{'type'}." ".$args{'struct'}." {\n.br\n";

    foreach my $parameter (@{$args{'parameterlist'}}) {
	if ($parameter =~ /^#/) {
	    print ".BI \"$parameter\"\n.br\n";
	    next;
	}
	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

        ($args{'parameterdescs'}{$parameter_name} ne $undescribed) || next;
	$type = $args{'parametertypes'}{$parameter};
	if ($type =~ m/([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)/) {
	    # pointer-to-function
	    print ".BI \"    ".$1."\" ".$parameter." \") (".$2.")"."\"\n;\n";
	} elsif ($type =~ m/^(.*?)\s*(:.*)/) {
	    print ".BI \"    ".$1."\" ".$parameter.$2." \""."\"\n;\n";
	} else {
	    $type =~ s/([^\*])$/$1 /;
	    print ".BI \"    ".$type."\" ".$parameter." \""."\"\n;\n";
	}
	print "\n.br\n";
    }
    print "};\n.br\n";

    print ".SH Arguments\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	($parameter =~ /^#/) && next;

	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

        ($args{'parameterdescs'}{$parameter_name} ne $undescribed) || next;
	print ".IP \"".$parameter."\" 12\n";
	output_highlight($args{'parameterdescs'}{$parameter_name});
    }
    foreach $section (@{$args{'sectionlist'}}) {
	print ".SH \"$section\"\n";
	output_highlight($args{'sections'}{$section});
    }
}

##
# output typedef in man
sub output_typedef_man(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);

    print ".TH \"$args{'module'}\" 9 \"$args{'typedef'}\" \"$man_date\" \"API Manual\" LINUX\n";

    print ".SH NAME\n";
    print "typedef ".$args{'typedef'}." \\- ".$args{'purpose'}."\n";

    foreach $section (@{$args{'sectionlist'}}) {
	print ".SH \"$section\"\n";
	output_highlight($args{'sections'}{$section});
    }
}

sub output_intro_man(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);
    my $count;

    print ".TH \"$args{'module'}\" 9 \"$args{'module'}\" \"$man_date\" \"API Manual\" LINUX\n";

    foreach $section (@{$args{'sectionlist'}}) {
	print ".SH \"$section\"\n";
	output_highlight($args{'sections'}{$section});
    }
}

##
# output in text
sub output_function_text(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);

    print "Function:\n\n";
    my $start=$args{'functiontype'}." ".$args{'function'}." (";
    print $start;
    my $count = 0;
    foreach my $parameter (@{$args{'parameterlist'}}) {
	$type = $args{'parametertypes'}{$parameter};
	if ($type =~ m/([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)/) {
	    # pointer-to-function
	    print $1.$parameter.") (".$2;
	} else {
	    print $type." ".$parameter;
	}
	if ($count != $#{$args{'parameterlist'}}) {
	    $count++;
	    print ",\n";
	    print " " x length($start);
	} else {
	    print ");\n\n";
	}
    }

    print "Arguments:\n\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

	print $parameter."\n\t".$args{'parameterdescs'}{$parameter_name}."\n";
    }
    output_section_text(@_);
}

#output sections in text
sub output_section_text(%) {
    my %args = %{$_[0]};
    my $section;

    print "\n";
    foreach $section (@{$args{'sectionlist'}}) {
	print "$section:\n\n";
	output_highlight($args{'sections'}{$section});
    }  
    print "\n\n";
}

# output enum in text
sub output_enum_text(%) {
    my %args = %{$_[0]};
    my ($parameter);
    my $count;
    print "Enum:\n\n";

    print "enum ".$args{'enum'}." {\n";
    $count = 0;
    foreach $parameter (@{$args{'parameterlist'}}) {
        print "\t$parameter";
	if ($count != $#{$args{'parameterlist'}}) {
	    $count++;
	    print ",";
	}
	print "\n";
    }
    print "};\n\n";

    print "Constants:\n\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	print "$parameter\n\t";
	print $args{'parameterdescs'}{$parameter}."\n";
    }

    output_section_text(@_);
}

# output typedef in text
sub output_typedef_text(%) {
    my %args = %{$_[0]};
    my ($parameter);
    my $count;
    print "Typedef:\n\n";

    print "typedef ".$args{'typedef'}."\n";
    output_section_text(@_);
}

# output struct as text
sub output_struct_text(%) {
    my %args = %{$_[0]};
    my ($parameter);

    print $args{'type'}." ".$args{'struct'}.":\n\n";
    print $args{'type'}." ".$args{'struct'}." {\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	if ($parameter =~ /^#/) {
	    print "$parameter\n";
	    next;
	}

	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

        ($args{'parameterdescs'}{$parameter_name} ne $undescribed) || next;
	$type = $args{'parametertypes'}{$parameter};
	if ($type =~ m/([^\(]*\(\*)\s*\)\s*\(([^\)]*)\)/) {
	    # pointer-to-function
	    print "\t$1 $parameter) ($2);\n";
	} elsif ($type =~ m/^(.*?)\s*(:.*)/) {
	    print "\t$1 $parameter$2;\n";
	} else {
	    print "\t".$type." ".$parameter.";\n";
	}
    }
    print "};\n\n";

    print "Members:\n\n";
    foreach $parameter (@{$args{'parameterlist'}}) {
	($parameter =~ /^#/) && next;

	my $parameter_name = $parameter;
	$parameter_name =~ s/\[.*//;

        ($args{'parameterdescs'}{$parameter_name} ne $undescribed) || next;
	print "$parameter\n\t";
	print $args{'parameterdescs'}{$parameter_name}."\n";
    }
    print "\n";
    output_section_text(@_);
}

sub output_intro_text(%) {
    my %args = %{$_[0]};
    my ($parameter, $section);

    foreach $section (@{$args{'sectionlist'}}) {
	print " $section:\n";
	print "    -> ";
	output_highlight($args{'sections'}{$section});
    }
}

##
# generic output function for typedefs
sub output_declaration {
    no strict 'refs';
    my $name = shift;
    my $functype = shift;
    my $func = "output_${functype}_$output_mode";
    if (($function_only==0) || 
	( $function_only == 1 && defined($function_table{$name})) || 
	( $function_only == 2 && !defined($function_table{$name})))
    {
        &$func(@_);
	$section_counter++;
    }
}

##
# generic output function - calls the right one based
# on current output mode.
sub output_intro {
    no strict 'refs';
    my $func = "output_intro_".$output_mode;
    &$func(@_);
    $section_counter++;
}

##
# takes a declaration (struct, union, enum, typedef) and 
# invokes the right handler. NOT called for functions.
sub dump_declaration($$) {
    no strict 'refs';
    my ($prototype, $file) = @_;
    my $func = "dump_".$decl_type;
    &$func(@_);
}

sub dump_union($$) {
    dump_struct(@_);
}

sub dump_struct($$) {
    my $x = shift;
    my $file = shift;

    if ($x =~/(struct|union)\s+(\w+)\s*{(.*)}/) {
        $declaration_name = $2;
        my $members = $3;

	# ignore embedded structs or unions
	$members =~ s/{.*?}//g;

	# ignore members marked private:
	$members =~ s/\/\*.*?private:.*?public:.*?\*\///gos;
	$members =~ s/\/\*.*?private:.*//gos;
	# strip comments:
	$members =~ s/\/\*.*?\*\///gos;

	create_parameterlist($members, ';', $file);

	output_declaration($declaration_name,
			   'struct',
			   {'struct' => $declaration_name,
			    'module' => $modulename,
			    'parameterlist' => \@parameterlist,
			    'parameterdescs' => \%parameterdescs,
			    'parametertypes' => \%parametertypes,
			    'sectionlist' => \@sectionlist,
			    'sections' => \%sections,
			    'purpose' => $declaration_purpose,
			    'type' => $decl_type
			   });
    }
    else {
        print STDERR "Error(${file}:$.): Cannot parse struct or union!\n";
	++$errors;
    }
}

sub dump_enum($$) {
    my $x = shift;
    my $file = shift;

    $x =~ s@/\*.*?\*/@@gos;	# strip comments.
    if ($x =~ /enum\s+(\w+)\s*{(.*)}/) {
        $declaration_name = $1;
        my $members = $2;

	foreach my $arg (split ',', $members) {
	    $arg =~ s/^\s*(\w+).*/$1/;
	    push @parameterlist, $arg;
	    if (!$parameterdescs{$arg}) {
	        $parameterdescs{$arg} = $undescribed;
	        print STDERR "Warning(${file}:$.): Enum value '$arg' ".
		    "not described in enum '$declaration_name'\n";
	    }

	}
	
	output_declaration($declaration_name,
			   'enum',
			   {'enum' => $declaration_name,
			    'module' => $modulename,
			    'parameterlist' => \@parameterlist,
			    'parameterdescs' => \%parameterdescs,
			    'sectionlist' => \@sectionlist,
			    'sections' => \%sections,
			    'purpose' => $declaration_purpose
			   });
    }
    else {
        print STDERR "Error(${file}:$.): Cannot parse enum!\n";
	++$errors;
    }
}

sub dump_typedef($$) {
    my $x = shift;
    my $file = shift;

    $x =~ s@/\*.*?\*/@@gos;	# strip comments.
    while (($x =~ /\(*.\)\s*;$/) || ($x =~ /\[*.\]\s*;$/)) {
        $x =~ s/\(*.\)\s*;$/;/;
	$x =~ s/\[*.\]\s*;$/;/;
    }

    if ($x =~ /typedef.*\s+(\w+)\s*;/) {
        $declaration_name = $1;

	output_declaration($declaration_name,
			   'typedef',
			   {'typedef' => $declaration_name,
			    'module' => $modulename,
			    'sectionlist' => \@sectionlist,
			    'sections' => \%sections,
			    'purpose' => $declaration_purpose
			   });
    }
    else {
        print STDERR "Error(${file}:$.): Cannot parse typedef!\n";
	++$errors;
    }
}

sub create_parameterlist($$$) {
    my $args = shift;
    my $splitter = shift;
    my $file = shift;
    my $type;
    my $param;

    while ($args =~ /(\([^\),]+),/) {
        $args =~ s/(\([^\),]+),/$1#/g;
    }
    
    foreach my $arg (split($splitter, $args)) {
	# strip comments
	$arg =~ s/\/\*.*\*\///;
        # strip leading/trailing spaces
        $arg =~ s/^\s*//;
	$arg =~ s/\s*$//;
	$arg =~ s/\s+/ /;

	if ($arg =~ /^#/) {
	    # Treat preprocessor directive as a typeless variable just to fill
	    # corresponding data structures "correctly". Catch it later in
	    # output_* subs.
	    push_parameter($arg, "", $file);
	} elsif ($arg =~ m/\(/) {
	    # pointer-to-function
	    $arg =~ tr/#/,/;
	    $arg =~ m/[^\(]+\(\*([^\)]+)\)/;
	    $param = $1;
	    $type = $arg;
	    $type =~ s/([^\(]+\(\*)$param/$1/;
	    push_parameter($param, $type, $file);
	} elsif ($arg) {
	    $arg =~ s/\s*:\s*/:/g;
	    $arg =~ s/\s*\[/\[/g;

	    my @args = split('\s*,\s*', $arg);
	    if ($args[0] =~ m/\*/) {
		$args[0] =~ s/(\*+)\s*/ $1/;
	    }
	    my @first_arg = split('\s+', shift @args);
	    unshift(@args, pop @first_arg);
	    $type = join " ", @first_arg;

	    foreach $param (@args) {
		if ($param =~ m/^(\*+)\s*(.*)/) {
		    push_parameter($2, "$type $1", $file);
		}
		elsif ($param =~ m/(.*?):(\d+)/) {
		    push_parameter($1, "$type:$2", $file)
		}
		else {
		    push_parameter($param, $type, $file);
		}
	    }
	}
    }
}

sub push_parameter($$$) {
	my $param = shift;
	my $type = shift;
	my $file = shift;

	my $param_name = $param;
	$param_name =~ s/\[.*//;

	if ($type eq "" && $param eq "...")
	{
	    $type="";
	    $param="...";
	    $parameterdescs{"..."} = "variable arguments";
	}
	elsif ($type eq "" && ($param eq "" or $param eq "void"))
	{
	    $type="";
	    $param="void";
	    $parameterdescs{void} = "no arguments";
	}
	if (defined $type && $type && !defined $parameterdescs{$param_name}) {
	    $parameterdescs{$param_name} = $undescribed;

	    if (($type eq 'function') || ($type eq 'enum')) {
	        print STDERR "Warning(${file}:$.): Function parameter ".
		    "or member '$param' not " .
		    "described in '$declaration_name'\n";
	    }
	    print STDERR "Warning(${file}:$.):".
	                 " No description found for parameter '$param'\n";
	    ++$warnings;
        }

	push @parameterlist, $param;
	$parametertypes{$param} = $type;
}

##
# takes a function prototype and the name of the current file being
# processed and spits out all the details stored in the global
# arrays/hashes.
sub dump_function($$) {
    my $prototype = shift;
    my $file = shift;

    $prototype =~ s/^static +//;
    $prototype =~ s/^extern +//;
    $prototype =~ s/^fastcall +//;
    $prototype =~ s/^asmlinkage +//;
    $prototype =~ s/^inline +//;
    $prototype =~ s/^__inline__ +//;
    $prototype =~ s/^#define +//; #ak added
    $prototype =~ s/__attribute__ \(\([a-z,]*\)\)//;

    # Yes, this truly is vile.  We are looking for:
    # 1. Return type (may be nothing if we're looking at a macro)
    # 2. Function name
    # 3. Function parameters.
    #
    # All the while we have to watch out for function pointer parameters
    # (which IIRC is what the two sections are for), C types (these
    # regexps don't even start to express all the possibilities), and
    # so on.
    #
    # If you mess with these regexps, it's a good idea to check that
    # the following functions' documentation still comes out right:
    # - parport_register_device (function pointer parameters)
    # - atomic_set (macro)
    # - pci_match_device (long return type)

    if ($prototype =~ m/^()([a-zA-Z0-9_~:]+)\s*\(([^\(]*)\)/ ||
	$prototype =~ m/^(\w+)\s+([a-zA-Z0-9_~:]+)\s*\(([^\(]*)\)/ ||
	$prototype =~ m/^(\w+\s*\*)\s*([a-zA-Z0-9_~:]+)\s*\(([^\(]*)\)/ ||
	$prototype =~ m/^(\w+\s+\w+)\s+([a-zA-Z0-9_~:]+)\s*\(([^\(]*)\)/ ||
	$prototype =~ m/^(\w+\s+\w+\s*\*)\s*([a-zA-Z0-9_~:]+)\s*\(([^\(]*)\)/ ||
	$prototype =~ m/^(\w+\s+\w+\s+\w+)\s+([a-zA-Z0-9_~:]+)\s*\(([^\(]*)\)/ ||
	$prototype =~ m/^(\w+\s+\w+\s+\w+\s*\*)\s*([a-zA-Z0-9_~:]+)\s*\(([^\(]*)\)/ ||
	$prototype =~ m/^()([a-zA-Z0-9_~:]+)\s*\(([^\{]*)\)/ ||
	$prototype =~ m/^(\w+)\s+([a-zA-Z0-9_~:]+)\s*\(([^\{]*)\)/ ||
	$prototype =~ m/^(\w+\s*\*)\s*([a-zA-Z0-9_~:]+)\s*\(([^\{]*)\)/ ||
	$prototype =~ m/^(\w+\s+\w+)\s+([a-zA-Z0-9_~:]+)\s*\(([^\{]*)\)/ ||
	$prototype =~ m/^(\w+\s+\w+\s*\*)\s*([a-zA-Z0-9_~:]+)\s*\(([^\{]*)\)/ ||
	$prototype =~ m/^(\w+\s+\w+\s+\w+)\s+([a-zA-Z0-9_~:]+)\s*\(([^\{]*)\)/ ||
	$prototype =~ m/^(\w+\s+\w+\s+\w+\s*\*)\s*([a-zA-Z0-9_~:]+)\s*\(([^\{]*)\)/)  {
	$return_type = $1;
	$declaration_name = $2;
	my $args = $3;

	create_parameterlist($args, ',', $file);
    } else {
	print STDERR "Error(${file}:$.): cannot understand prototype: '$prototype'\n";
	++$errors;
	return;
    }

    output_declaration($declaration_name, 
		       'function',
		       {'function' => $declaration_name,
			'module' => $modulename,
			'functiontype' => $return_type,
			'parameterlist' => \@parameterlist,
			'parameterdescs' => \%parameterdescs,
			'parametertypes' => \%parametertypes,
			'sectionlist' => \@sectionlist,
			'sections' => \%sections,
			'purpose' => $declaration_purpose
		       });
}

sub process_file($);

# Read the file that maps relative names to absolute names for
# separate source and object directories and for shadow trees.
if (open(SOURCE_MAP, "<.tmp_filelist.txt")) {
	my ($relname, $absname);
	while(<SOURCE_MAP>) {
		chop();
		($relname, $absname) = (split())[0..1];
		$relname =~ s:^/+::;
		$source_map{$relname} = $absname;
	}
	close(SOURCE_MAP);
}

if ($filelist) {
	open(FLIST,"<$filelist") or die "Can't open file list $filelist";
	while(<FLIST>) {
		chop;
		process_file($_);
	}
}

foreach (@ARGV) {
    chomp;
    process_file($_);
}
if ($verbose && $errors) {
  print STDERR "$errors errors\n";
}
if ($verbose && $warnings) {
  print STDERR "$warnings warnings\n";
}

exit($errors);

sub reset_state {
    $function = "";
    %constants = ();
    %parameterdescs = ();
    %parametertypes = ();
    @parameterlist = ();
    %sections = ();
    @sectionlist = ();
    $prototype = "";
    
    $state = 0;
}

sub process_state3_function($$) { 
    my $x = shift;
    my $file = shift;

    if ($x =~ m#\s*/\*\s+MACDOC\s*#io || ($x =~ /^#/ && $x !~ /^#define/)) {
	# do nothing
    }
    elsif ($x =~ /([^\{]*)/) {
        $prototype .= $1;
    }
    if (($x =~ /\{/) || ($x =~ /\#define/) || ($x =~ /;/)) {
        $prototype =~ s@/\*.*?\*/@@gos;	# strip comments.
	$prototype =~ s@[\r\n]+@ @gos; # strip newlines/cr's.
	$prototype =~ s@^\s+@@gos; # strip leading spaces
	dump_function($prototype,$file);
	reset_state();
    }
}

sub process_state3_type($$) { 
    my $x = shift;
    my $file = shift;

    $x =~ s@[\r\n]+@ @gos; # strip newlines/cr's.
    $x =~ s@^\s+@@gos; # strip leading spaces
    $x =~ s@\s+$@@gos; # strip trailing spaces
    if ($x =~ /^#/) {
	# To distinguish preprocessor directive from regular declaration later.
	$x .= ";";
    }

    while (1) {
        if ( $x =~ /([^{};]*)([{};])(.*)/ ) {
	    $prototype .= $1 . $2;
	    ($2 eq '{') && $brcount++;
	    ($2 eq '}') && $brcount--;
	    if (($2 eq ';') && ($brcount == 0)) {
	        dump_declaration($prototype,$file);
		reset_state();
	        last;
	    }
	    $x = $3;
        } else {
	    $prototype .= $x;
	    last;
	}
    }
}

# replace <, >, and &
sub xml_escape($) {
	my $text = shift;
	$text =~ s/\&/\\\\\\amp;/g;
	$text =~ s/\</\\\\\\lt;/g;
	$text =~ s/\>/\\\\\\gt;/g;
	return $text;
}

sub process_file($) {
    my $file;
    my $identifier;
    my $func;
    my $initial_section_counter = $section_counter;

    if (defined($ENV{'SRCTREE'})) {
	$file = "$ENV{'SRCTREE'}" . "/" . "@_";
    }
    else {
	$file = "@_";
    }
    if (defined($source_map{$file})) {
	$file = $source_map{$file};
    }

    if (!open(IN,"<$file")) {
	print STDERR "Error: Cannot open file $file\n";
	++$errors;
	return;
    }

    $section_counter = 0;
    while (<IN>) {
	if ($state == 0) {
	    if (/$doc_start/o) {
		$state = 1;		# next line is always the function name
	    }
	} elsif ($state == 1) {	# this line is the function name (always)
	    if (/$doc_block/o) {
		$state = 4;
		$contents = "";
		if ( $1 eq "" ) {
			$section = $section_intro;
		} else {
			$section = $1;
		}
            }
	    elsif (/$doc_decl/o) {
		$identifier = $1;
		if (/\s*([\w\s]+?)\s*-/) {
		    $identifier = $1;
		}

		$state = 2;
		if (/-(.*)/) {
		    $declaration_purpose = xml_escape($1);
		} else {
		    $declaration_purpose = "";
		}
		if ($identifier =~ m/^struct/) {
		    $decl_type = 'struct';
		} elsif ($identifier =~ m/^union/) {
		    $decl_type = 'union';
		} elsif ($identifier =~ m/^enum/) {
		    $decl_type = 'enum';
		} elsif ($identifier =~ m/^typedef/) {
		    $decl_type = 'typedef';
		} else {
		    $decl_type = 'function';
		}

		if ($verbose) {
		    print STDERR "Info(${file}:$.): Scanning doc for $identifier\n";
		}
	    } else {
		print STDERR "Warning(${file}:$.): Cannot understand $_ on line $.",
		" - I thought it was a doc line\n";
		++$warnings;
		$state = 0;
	    }
	} elsif ($state == 2) {	# look for head: lines, and include content
	    if (/$doc_sect/o) {
		$newsection = $1;
		$newcontents = $2;

		if ($contents ne "") {
		    dump_section($section, xml_escape($contents));
		    $section = $section_default;
		}

		$contents = $newcontents;
		if ($contents ne "") {
		    $contents .= "\n";
		}
		$section = $newsection;
	    } elsif (/$doc_end/) {

		if ($contents ne "") {
		    dump_section($section, xml_escape($contents));
		    $section = $section_default;
		    $contents = "";
		}

		$prototype = "";
		$state = 3;
		$brcount = 0;
#	    print STDERR "end of doc comment, looking for prototype\n";
	    } elsif (/$doc_content/) {
		# miguel-style comment kludge, look for blank lines after
		# @parameter line to signify start of description
		if ($1 eq "" && 
			($section =~ m/^@/ || $section eq $section_context)) {
		    dump_section($section, xml_escape($contents));
		    $section = $section_default;
		    $contents = "";
		} else {
		    $contents .= $1."\n";
		}
	    } else {
		# i dont know - bad line?  ignore.
		print STDERR "Warning(${file}:$.): bad line: $_"; 
		++$warnings;
	    }
	} elsif ($state == 3) {	# scanning for function { (end of prototype)
	    if ($decl_type eq 'function') {
	        process_state3_function($_, $file);
	    } else {
	        process_state3_type($_, $file);
	    }
	} elsif ($state == 4) {
		# Documentation block
	        if (/$doc_block/) {
			dump_section($section, $contents);
			output_intro({'sectionlist' => \@sectionlist,
				      'sections' => \%sections });
			$contents = "";
			$function = "";
			%constants = ();
			%parameterdescs = ();
			%parametertypes = ();
			@parameterlist = ();
			%sections = ();
			@sectionlist = ();
			$prototype = "";
			if ( $1 eq "" ) {
				$section = $section_intro;
			} else {
				$section = $1;
			}
                }
		elsif (/$doc_end/)
		{
			dump_section($section, $contents);
			output_intro({'sectionlist' => \@sectionlist,
				      'sections' => \%sections });
			$contents = "";
			$function = "";
			%constants = ();
			%parameterdescs = ();
			%parametertypes = ();
			@parameterlist = ();
			%sections = ();
			@sectionlist = ();
			$prototype = "";
			$state = 0;
		}
		elsif (/$doc_content/)
		{
			if ( $1 eq "" )
			{
				$contents .= $blankline;
			}
			else
			{
				$contents .= $1 . "\n";
			}	
        	}
          }
    }
    if ($initial_section_counter == $section_counter) {
	print STDERR "Warning(${file}): no structured comments found\n";
	if ($output_mode eq "xml") {
	    # The template wants at least one RefEntry here; make one.
	    print "<refentry>\n";
	    print " <refnamediv>\n";
	    print "  <refname>\n";
	    print "   ${file}\n";
	    print "  </refname>\n";
	    print "  <refpurpose>\n";
	    print "   Document generation inconsistency\n";
	    print "  </refpurpose>\n";
	    print " </refnamediv>\n";
	    print " <refsect1>\n";
	    print "  <title>\n";
	    print "   Oops\n";
	    print "  </title>\n";
	    print "  <warning>\n";
	    print "   <para>\n";
	    print "    The template for this document tried to insert\n";
	    print "    the structured comment from the file\n";
	    print "    <filename>${file}</filename> at this point,\n";
	    print "    but none was found.\n";
	    print "    This dummy section is inserted to allow\n";
	    print "    generation to continue.\n";
	    print "   </para>\n";
	    print "  </warning>\n";
	    print " </refsect1>\n";
	    print "</refentry>\n";
	}
    }
}
