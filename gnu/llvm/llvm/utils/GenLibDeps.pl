#!/usr/bin/env perl
#
# Program:  GenLibDeps.pl
#
# Synopsis: Generate HTML output that shows the dependencies between a set of
#           libraries. The output of this script should periodically replace 
#           the similar content in the UsingLibraries.html document.
#
# Syntax:   GenLibDeps.pl [-flat] <directory_with_libraries_in_it> [path_to_nm_binary]
#
use strict;
use warnings;
# Parse arguments... 
my $FLAT = 0;
my $WHY = 0;
my $PEROBJ = 0;
my $PEROBJINCL = 0;
while (scalar(@ARGV) and ($_ = $ARGV[0], /^[-+]/)) {
  shift;
  last if /^--$/;  # Stop processing arguments on --

  # List command line options here...
  if (/^-flat$/)     { $FLAT = 1; next; }
  if (/^-why/)       { $WHY = 1; $FLAT = 1; next; }
  if (/^-perobj$/)    { $PEROBJ = 1; next; }
  if (/^-perobjincl/) { $PEROBJINCL = 1; next;}
  print "Unknown option: $_ : ignoring!\n";
}

# Give first option a name.
my $Directory = $ARGV[0];
if (!defined($Directory) || ! -d "$Directory") {
  die "First argument must specify the directory containing LLVM libs\n";
}

my $nmPath = $ARGV[1];

# Find the "dot" program
my $DotPath="";
if (!$FLAT) {
  chomp($DotPath = `which dot`);
  die "Can't find 'dot'" if (! -x "$DotPath");
}

if (defined($ENV{NM})) {
  chomp($nmPath=$ENV{NM});
}

if (!defined($nmPath) || $nmPath eq "") {
  chomp($nmPath=`which nm`);
  die "Can't find 'nm'" if (! -x "$nmPath");
}

my $ranlibPath;
if ($PEROBJ) {
  $ranlibPath = $ARGV[2];
  if (defined($ENV{RANLIB})) {
    chomp($ranlibPath=$ENV{RANLIB});
  }

  if (!defined($ranlibPath) || $ranlibPath eq "") {
    chomp($ranlibPath=`which ranlib`);
    die "Can't find 'ranlib'" if (! -x "$ranlibPath");
  }
}

# Open the directory and read its contents, sorting by name and differentiating
# by whether its a library (.a) or an object file (.o)
opendir DIR,$Directory;
my @files = readdir DIR;
closedir DIR;
my @libs = grep(/libLLVM.*\.(dylib|so|a)$/,sort(@files));
# Omit the all-of-llvm shared library.
@libs = grep(!/libLLVM-\d\.\d(svn)?\.(dylib|so)/, @libs);
my @objs = grep(/LLVM.*\.o$/,sort(@files));

# Declare the hashes we will use to keep track of the library and object file
# symbol definitions.
my %libdefs;
my %objdefs;

my %libobjs;
my %objdeps=();
# Gather library definitions at object file granularity (optional)
if ($PEROBJ) {
  foreach my $lib (@libs ) {
    `$ranlibPath $Directory/$lib`;
    my $libpath = $lib;
    $libpath =~ s/^libLLVM(.*)\.a/$1/;
    $libpath =~ s/(.+)CodeGen$/Target\/$1/;
    $libpath =~ s/(.+)AsmPrinter$/Target\/$1\/AsmPrinter/;
    $libpath =~ s/(.+)AsmParser$/Target\/$1\/AsmParser/;
    $libpath =~ s/(.+)Info$/Target\/$1\/TargetInfo/;
    $libpath =~ s/(.+)Disassembler$/Target\/$1\/Disassembler/;
    $libpath =~ s/SelectionDAG/CodeGen\/SelectionDAG/;
    $libpath =~ s/^AsmPrinter/CodeGen\/AsmPrinter/;
    $libpath =~ s/^BitReader/Bitcode\/Reader/;
    $libpath =~ s/^BitWriter/Bitcode\/Writer/;
    $libpath =~ s/^BitstreamReader/Bitstream\/Reader/;
    $libpath =~ s/^BitstreamWriter/Bitstream\/Writer/;
    $libpath =~ s/^MSIL/Target\/MSIL/;
    $libpath =~ s/^Core/IR/;
    $libpath =~ s/^Instrumentation/Transforms\/Instrumentation/;
    $libpath =~ s/^Interpreter/ExecutionEngine\/Interpreter/;
    $libpath =~ s/^JIT/ExecutionEngine\/JIT/;
    $libpath =~ s/^ScalarOpts/Transforms\/Scalar/;
    $libpath =~ s/^TransformUtils/Transforms\/Utils/;
    $libpath =~ s/^ipa/Analysis\/IPA/;
    $libpath =~ s/^ipo/Transforms\/IPO/;
    $libpath = "lib/".$libpath."/";
    open DEFS, "$nmPath -sg $Directory/$lib|";
    while (<DEFS>) {
      chomp;
      if (/^([^ ]*) in ([^ ]*)/) {
        my $objfile = $libpath.$2;
        $objdefs{$1} = $objfile;
        $objdeps{$objfile} = {};
        $libobjs{$lib}{$objfile}=1;
#        my $p = "../llvm/".$objfile;
#        $p =~ s/Support\/reg(.*).o/Support\/reg$1.c/;
#        $p =~ s/.o$/.cpp/;
#        unless (-e $p) {
#          die "$p\n"
#        }
      }
    }
    close DEFS or die "nm failed";
  }
  foreach my $lib (@libs ) {
    my $libpath = $lib;
    $libpath =~ s/^libLLVM(.*)\.a/$1/;
    $libpath =~ s/(.+)CodeGen$/Target\/$1/;
    $libpath =~ s/(.+)AsmPrinter$/Target\/$1\/AsmPrinter/;
    $libpath =~ s/(.+)AsmParser$/Target\/$1\/AsmParser/;
    $libpath =~ s/(.+)Info$/Target\/$1\/TargetInfo/;
    $libpath =~ s/(.+)Disassembler$/Target\/$1\/Disassembler/;
    $libpath =~ s/SelectionDAG/CodeGen\/SelectionDAG/;
    $libpath =~ s/^AsmPrinter/CodeGen\/AsmPrinter/;
    $libpath =~ s/^BitReader/Bitcode\/Reader/;
    $libpath =~ s/^BitWriter/Bitcode\/Writer/;
    $libpath =~ s/^BitstreamReader/Bitstream\/Reader/;
    $libpath =~ s/^BitstreamWriter/Bitstream\/Writer/;
    $libpath =~ s/^MSIL/Target\/MSIL/;
    $libpath =~ s/^Core/VMCore/;
    $libpath =~ s/^Instrumentation/Transforms\/Instrumentation/;
    $libpath =~ s/^Interpreter/ExecutionEngine\/Interpreter/;
    $libpath =~ s/^JIT/ExecutionEngine\/JIT/;
    $libpath =~ s/^ScalarOpts/Transforms\/Scalar/;
    $libpath =~ s/^TransformUtils/Transforms\/Utils/;
    $libpath =~ s/^ipa/Analysis\/IPA/;
    $libpath =~ s/^ipo/Transforms\/IPO/;
    $libpath = "lib/".$libpath."/";
    open UDEFS, "$nmPath -Aup $Directory/$lib|";
    while (<UDEFS>) {
      chomp;
      if (/:([^:]+):/) {
        my $obj = $libpath.$1;
        s/[^ ]+: *U //;
        if (defined($objdefs{$_})) {
          $objdeps{$obj}{$objdefs{$_}}=1;
        }
      }
    }
    close UDEFS or die "nm failed"
  }
} else {
# Gather definitions from the libraries
foreach my $lib (@libs ) {
  open DEFS, "$nmPath -g $Directory/$lib|";
  while (<DEFS>) {
    next if (! / [ABCDGRST] /);
    s/^[^ ]* [ABCDGRST] //;    
    s/\015?\012//; # not sure if <DEFS> is in binmode and uses LF or CRLF.
                   # this strips both LF and CRLF.
    $libdefs{$_} = $lib;
  }
  close DEFS or die "nm failed";
}
}

# Gather definitions from the object files.
foreach my $obj (@objs ) {
  open DEFS, "$nmPath -g $Directory/$obj |";
  while (<DEFS>) {
    next if (! / [ABCDGRST] /);
    s/^[^ ]* [ABCDGRST] //;
    s/\015?\012//; # not sure if <DEFS> is in binmode and uses LF or CRLF.
                   # this strips both LF and CRLF.    
    $objdefs{$_} = $obj;
  }
  close DEFS or die "nm failed";
}

# Generate one entry in the <dl> list. This generates the <dt> and <dd> elements
# for one library or object file. The <dt> provides the name of the library or
# object. The <dd> provides a list of the libraries/objects it depends on.
sub gen_one_entry {
  my $lib = $_[0];
  my $lib_ns = $lib;
  $lib_ns =~ s/(.*)\.[oa]/$1/;
  if ($FLAT) {
    print "$lib:";
    if ($WHY) { print "\n"; }
  } else {
    print "  <dt><b>$lib</b></dt><dd><ul>\n";
  }
  open UNDEFS, 
    "$nmPath -u $Directory/$lib | sed -e 's/^[ 0]* U //' | sort | uniq |";
  my %DepLibs;
  while (<UNDEFS>) {
    chomp;
    my $lib_printed = 0;
    if (defined($libdefs{$_}) && $libdefs{$_} ne $lib) {
      $DepLibs{$libdefs{$_}} = [] unless exists $DepLibs{$libdefs{$_}};
      push(@{$DepLibs{$libdefs{$_}}}, $_);
    } elsif (defined($objdefs{$_}) && $objdefs{$_} ne $lib) {
      if ($PEROBJ && !$PEROBJINCL) {
        # -perobjincl makes .a files depend on .o files they contain themselves
        # default is don't depend on these.
        next if defined $libobjs{$lib}{$objdefs{$_}};
      }
      my $libroot = $lib;
      $libroot =~ s/lib(.*).a/$1/;
      if ($objdefs{$_} ne "$libroot.o") {
        $DepLibs{$objdefs{$_}} = [] unless exists $DepLibs{$objdefs{$_}};
        push(@{$DepLibs{$objdefs{$_}}}, $_);
      }
    }
  }
  close UNDEFS or die "nm failed";
  unless(keys %DepLibs) {
    # above failed
    open UNDEFS, "$nmPath -u $Directory/$lib |";
    while (<UNDEFS>) {
      # to bypass non-working sed
      if ('  ' eq substr($_,0,2) and index($_,'U ')) {
        $_ = substr($_,index($_,'U ')+2)
      };
      $_ = substr($_,index($_,'  *U ')+5) if -1!=index($_,'  *U ');

      chomp;
      my $lib_printed = 0;
      if (defined($libdefs{$_}) && $libdefs{$_} ne $lib) {
        $DepLibs{$libdefs{$_}} = [] unless exists $DepLibs{$libdefs{$_}};
        push(@{$DepLibs{$libdefs{$_}}}, $_);
      } elsif (defined($objdefs{$_}) && $objdefs{$_} ne $lib) {
        my $libroot = $lib;
        $libroot =~ s/lib(.*).a/$1/;
        if ($objdefs{$_} ne "$libroot.o") {
          $DepLibs{$objdefs{$_}} = [] unless exists $DepLibs{$objdefs{$_}};
          push(@{$DepLibs{$objdefs{$_}}}, $_);
        }
      }
    }
    close UNDEFS or die "nm failed";
  }
  if ($PEROBJINCL) {
     # include the .a's objects
     for my $obj (keys %{$libobjs{$lib}}) {
        $DepLibs{$obj} = ["<.a object>"] unless exists $DepLibs{$obj};
     }
     my $madechange = 1;
     while($madechange) {
      $madechange = 0;
      my %temp = %DepLibs;
      foreach my $obj (keys %DepLibs) {
        foreach my $objdeps (keys %{$objdeps{$obj}}) {
          next if defined $temp{$objdeps};
          push(@{$temp{$objdeps}}, $obj);
          $madechange = 1;
        }
      }
      %DepLibs = %temp;
     }
  }

  for my $key (sort keys %DepLibs) {
    if ($FLAT) {
      print " $key";
      if ($WHY) {
        print "\n";
        my @syms = @{$DepLibs{$key}};
        foreach my $sym (@syms) {
          print "  $sym\n";
        }
      }
    } else {
      print "    <li>$key</li>\n";
    }
    my $suffix = substr($key,length($key)-1,1);
    $key =~ s/(.*)\.[oa]/$1/;
    if ($suffix eq "a") {
      if (!$FLAT) { print DOT "$lib_ns -> $key [ weight=0 ];\n" };
    } else {
      if (!$FLAT) { print DOT "$lib_ns -> $key [ weight=10];\n" };
    }
  }
  if ($FLAT) {
    if (!$WHY) {
      print "\n";
    }
  } else {
    print "  </ul></dd>\n";
  }
}

# Make sure we flush on write. This is slower but correct based on the way we
# write I/O in gen_one_entry.
$| = 1;

# Print the definition list tag
if (!$FLAT) {
    print "<dl>\n";

  open DOT, "| $DotPath -Tgif > libdeps.gif";

  print DOT "digraph LibDeps {\n";
  print DOT "  size=\"40,15\"; \n";
  print DOT "  ratio=\"1.33333\"; \n";
  print DOT "  margin=\"0.25\"; \n";
  print DOT "  rankdir=\"LR\"; \n";
  print DOT "  mclimit=\"50.0\"; \n";
  print DOT "  ordering=\"out\"; \n";
  print DOT "  center=\"1\";\n";
  print DOT "node [shape=\"box\",\n";
  print DOT "      color=\"#000088\",\n";
  print DOT "      fillcolor=\"#FFFACD\",\n";
  print DOT "      fontcolor=\"#3355BB\",\n";
  print DOT "      style=\"filled\",\n";
  print DOT "      fontname=\"sans\",\n";
  print DOT "      fontsize=\"24\"\n";
  print DOT "];\n";
  print DOT "edge [dir=\"forward\",style=\"solid\",color=\"#000088\"];\n";
}

# Print libraries first
foreach my $lib (@libs) {
  gen_one_entry($lib);
}

if ($PEROBJ) {
  foreach my $obj (keys %objdeps) {
     print "$obj:";
     if (!$PEROBJINCL) {
      foreach my $dep (keys %{$objdeps{$obj}}) {
          print " $dep";
      }
    }
     print "\n";
  }
}

if (!$FLAT) {
  print DOT "}\n";
  close DOT;
  open DOT, "| $DotPath -Tgif > objdeps.gif";
  print DOT "digraph ObjDeps {\n";
  print DOT "  size=\"8,10\";\n";
  print DOT "  margin=\"0.25\";\n";
  print DOT "  rankdir=\"LR\";\n";
  print DOT "  mclimit=\"50.0\";\n";
  print DOT "  ordering=\"out\";\n";
  print DOT "  center=\"1\";\n";
  print DOT "node [shape=\"box\",\n";
  print DOT "      color=\"#000088\",\n";
  print DOT "      fillcolor=\"#FFFACD\",\n";
  print DOT "      fontcolor=\"#3355BB\",\n";
  print DOT "      fontname=\"sans\",\n";
  print DOT "      style=\"filled\",\n";
  print DOT "      fontsize=\"24\"\n";
  print DOT "];\n";
  print DOT "edge [dir=\"forward\",style=\"solid\",color=\"#000088\"];\n";
}

# Print objects second
foreach my $obj (@objs) {
  gen_one_entry($obj);
}

if (!$FLAT) {
  print DOT "}\n";
  close DOT;

# Print end tag of definition list element
  print "</dl>\n";
}
