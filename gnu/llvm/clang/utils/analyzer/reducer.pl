#!/usr/bin/env perl
use strict;
use warnings;
use File::Temp qw/ tempdir /;
my $prog = "reducer";

die "$prog <code file> <error string> [optional command]\n" if ($#ARGV < 0);
my $file = shift @ARGV;
die "$prog: [error] cannot read file $file\n" if (! -r $file);

my $magic = shift @ARGV;
die "$prog: [error] no error string specified\n" if (! defined $magic);

# Create a backup of the file.
my $dir = tempdir( CLEANUP => 1 );
print "$prog: created temporary directory '$dir'\n";
my $srcFile = "$dir/$file";
`cp $file $srcFile`;

# Create the script.
my $scriptFile = "$dir/script";
open(OUT, ">$scriptFile") or die "$prog: cannot create '$scriptFile'\n";
my $reduceOut = "$dir/reduceOut";

my $command;
if (scalar(@ARGV) > 0) { $command = \@ARGV; }
else {
  my $compiler = "clang";
  $command = [$compiler, "-fsyntax-only", "-Wfatal-errors", "-Wno-deprecated-declarations", "-Wimplicit-function-declaration"];
}
push @$command, $srcFile;
my $commandStr = "@$command";

print OUT <<ENDTEXT;
#!/usr/bin/env perl
use strict;
use warnings;
my \$BAD = 1;
my \$GOOD = 0;
`rm -f $reduceOut`;
my \$command = "$commandStr > $reduceOut 2>&1";
system(\$command);
open(IN, "$reduceOut") or exit(\$BAD);
my \$found = 0;
while(<IN>) {
  if (/$magic/) { exit \$GOOD; }
}
exit \$BAD;
ENDTEXT
close(OUT);
`chmod +x $scriptFile`;

print "$prog: starting reduction\n";
sub multidelta($) {
    my ($level) = @_;
    system("multidelta -level=$level $scriptFile $srcFile");
}

for (my $i = 1 ; $i <= 5; $i++) {
  foreach my $level (0,0,1,1,2,2,10) {
    multidelta($level);
  }
}

# Copy the final file.
`cp $srcFile $file.reduced`;
print "$prog: generated '$file.reduced";
