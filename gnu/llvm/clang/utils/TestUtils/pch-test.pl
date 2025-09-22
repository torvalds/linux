#!/usr/bin/env perl

# This tiny little script, which should be run from the clang
# directory (with clang in your patch), tries to take each
# compilable Clang test and build a PCH file from that test, then read
# and dump the contents of the PCH file just created.
use POSIX;
use warnings;

$exitcode = 0;
sub testfiles($$) {
  my $suffix = shift;
  my $language = shift;
  my $passed = 0;
  my $failed = 0;
  my $skipped = 0;

  @files = `ls test/*/*.$suffix`;
  foreach $file (@files) {
    chomp($file);
    my $code = system("clang -fsyntax-only -x $language $file > /dev/null 2>&1");
    if ($code == 0) {
      print(".");
      $code = system("clang -cc1 -emit-pch -x $language -o $file.pch $file > /dev/null 2>&1");
      if ($code == 0) {
        $code = system("clang -cc1 -include-pch $file.pch -x $language -ast-dump /dev/null > /dev/null 2>&1");
        if ($code == 0) {
          $passed++;
        } elsif (($code & 0xFF) == SIGINT) {
          exit($exitcode);
        } else {
          print("\n---Failed to dump AST file for \"$file\"---\n");
          $exitcode = 1;
          $failed++;
        }
        unlink "$file.pch";
      } elsif (($code & 0xFF) == SIGINT) {
        exit($exitcode);
      } else {
        print("\n---Failed to build PCH file for \"$file\"---\n");
        $exitcode = 1;
          $failed++;
      }
    } elsif (($code & 0xFF) == SIGINT) {
      exit($exitcode);
    } else {
      print("x");
      $skipped++;
    }
  }

  print("\n\n$passed tests passed\n");
  print("$failed tests failed\n");
  print("$skipped tests skipped ('x')\n")
}

printf("-----Testing precompiled headers for C-----\n");
testfiles("c", "c");
printf("\n-----Testing precompiled headers for Objective-C-----\n");
testfiles("m", "objective-c");
print("\n");
exit($exitcode);
