#!/usr/bin/env perl
use strict;
use File::Find;
use File::Copy;
use Digest::MD5;

my @fileTypes = ("cpp", "c");
my %dirFiles;
my %dirCMake;

sub GetFiles {
  my $dir = shift;
  my $x = $dirFiles{$dir};  
  if (!defined $x) {
    $x = [];
    $dirFiles{$dir} = $x;
  }  
  return $x;
}

sub ProcessFile {
  my $file = $_;
  my $dir = $File::Find::dir;
  # Record if a CMake file was found.
  if ($file eq "CMakeLists.txt") {
    $dirCMake{$dir} = $File::Find::name;
    return 0;
  }
  # Grab the extension of the file.
  $file =~ /\.([^.]+)$/;
  my $ext = $1;
  my $files;
  foreach my $x (@fileTypes) {
    if ($ext eq $x) {
      if (!defined $files) {
        $files = GetFiles($dir);
      }
      push @$files, $file;
      return 0;
    }
  }
  return 0;
}

sub EmitCMakeList {
  my $dir = shift;
  my $files = $dirFiles{$dir};
  
  if (!defined $files) {
    return;
  }
  
  foreach my $file (sort @$files) {
    print OUT "  ";
    print OUT $file;
    print OUT "\n";
  }  
}

sub UpdateCMake {
  my $cmakeList = shift;
  my $dir = shift;
  my $cmakeListNew = $cmakeList . ".new";
  open(IN, $cmakeList);
  open(OUT, ">", $cmakeListNew);
  my $foundLibrary = 0;
  
  while(<IN>) {
    if (!$foundLibrary) {
      print OUT $_;
      if (/^add_[^_]+_library\(/ || /^add_llvm_target\(/ || /^add_[^_]+_executable\(/) {
        $foundLibrary = 1;
        EmitCMakeList($dir);
      }
    }
    else {
      if (/\)/) {
        print OUT $_;
        $foundLibrary = 0;
      }
    }
  }

  close(IN);
  close(OUT);

  open(FILE, $cmakeList) or
    die("Cannot open $cmakeList when computing digest\n");
  binmode FILE;
  my $digestA = Digest::MD5->new->addfile(*FILE)->hexdigest;
  close(FILE);
    
  open(FILE, $cmakeListNew) or
    die("Cannot open $cmakeListNew when computing digest\n");
  binmode FILE;
  my $digestB = Digest::MD5->new->addfile(*FILE)->hexdigest;
  close(FILE);
  
  if ($digestA ne $digestB) {
    move($cmakeListNew, $cmakeList);
    return 1;    
  }
  
  unlink($cmakeListNew);
  return 0;
}

sub UpdateCMakeFiles {
  foreach my $dir (sort keys %dirCMake) {
    if (UpdateCMake($dirCMake{$dir}, $dir)) {
      print "Updated: $dir\n";
    }
  }
}

find({ wanted => \&ProcessFile, follow => 1 }, '.');
UpdateCMakeFiles();

