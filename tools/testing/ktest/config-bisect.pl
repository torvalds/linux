#!/usr/bin/perl -w
#
# Copyright 2015 - Steven Rostedt, Red Hat Inc.
# Copyright 2017 - Steven Rostedt, VMware, Inc.
#
# Licensed under the terms of the GNU GPL License version 2
#

# usage:
#  config-bisect.pl [options] good-config bad-config [good|bad]
#

# Compares a good config to a bad config, then takes half of the diffs
# and produces a config that is somewhere between the good config and
# the bad config. That is, the resulting config will start with the
# good config and will try to make half of the differences of between
# the good and bad configs match the bad config. It tries because of
# dependencies between the two configs it may not be able to change
# exactly half of the configs that are different between the two config
# files.

# Here's a normal way to use it:
#
#  $ cd /path/to/linux/kernel
#  $ config-bisect.pl /path/to/good/config /path/to/bad/config

# This will now pull in good config (blowing away .config in that directory
# so do not make that be one of the good or bad configs), and then
# build the config with "make oldconfig" to make sure it matches the
# current kernel. It will then store the configs in that result for
# the good config. It does the same for the bad config as well.
# The algorithm will run, merging half of the differences between
# the two configs and building them with "make oldconfig" to make sure
# the result changes (dependencies may reset changes the tool had made).
# It then copies the result of its good config to /path/to/good/config.tmp
# and the bad config to /path/to/bad/config.tmp (just appends ".tmp" to the
# files passed in). And the ".config" that you should test will be in
# directory

# After the first run, determine if the result is good or bad then
# run the same command appending the result

# For good results:
#  $ config-bisect.pl /path/to/good/config /path/to/bad/config good

# For bad results:
#  $ config-bisect.pl /path/to/good/config /path/to/bad/config bad

# Do not change the good-config or bad-config, config-bisect.pl will
# copy the good-config to a temp file with the same name as good-config
# but with a ".tmp" after it. It will do the same with the bad-config.

# If "good" or "bad" is not stated at the end, it will copy the good and
# bad configs to the .tmp versions. If a .tmp version already exists, it will
# warn before writing over them (-r will not warn, and just write over them).
# If the last config is labeled "good", then it will copy it to the good .tmp
# version. If the last config is labeled "bad", it will copy it to the bad
# .tmp version. It will continue this until it can not merge the two any more
# without the result being equal to either the good or bad .tmp configs.

my $start = 0;
my $val = "";

my $pwd = `pwd`;
chomp $pwd;
my $tree = $pwd;
my $build;

my $output_config;
my $reset_bisect;

sub usage {
    print << "EOF"

usage: config-bisect.pl [-l linux-tree][-b build-dir] good-config bad-config [good|bad]
  -l [optional] define location of linux-tree (default is current directory)
  -b [optional] define location to build (O=build-dir) (default is linux-tree)
  good-config the config that is considered good
  bad-config the config that does not work
  "good" add this if the last run produced a good config
  "bad" add this if the last run produced a bad config
  If "good" or "bad" is not specified, then it is the start of a new bisect

  Note, each run will create copy of good and bad configs with ".tmp" appended.

EOF
;

    exit(-1);
}

sub doprint {
    print @_;
}

sub dodie {
    doprint "CRITICAL FAILURE... ", @_, "\n";

    die @_, "\n";
}

sub expand_path {
    my ($file) = @_;

    if ($file =~ m,^/,) {
	return $file;
    }
    return "$pwd/$file";
}

sub read_prompt {
    my ($cancel, $prompt) = @_;

    my $ans;

    for (;;) {
	if ($cancel) {
	    print "$prompt [y/n/C] ";
	} else {
	    print "$prompt [y/N] ";
	}
	$ans = <STDIN>;
	chomp $ans;
	if ($ans =~ /^\s*$/) {
	    if ($cancel) {
		$ans = "c";
	    } else {
		$ans = "n";
	    }
	}
	last if ($ans =~ /^y$/i || $ans =~ /^n$/i);
	if ($cancel) {
	    last if ($ans =~ /^c$/i);
	    print "Please answer either 'y', 'n' or 'c'.\n";
	} else {
	    print "Please answer either 'y' or 'n'.\n";
	}
    }
    if ($ans =~ /^c/i) {
	exit;
    }
    if ($ans !~ /^y$/i) {
	return 0;
    }
    return 1;
}

sub read_yn {
    my ($prompt) = @_;

    return read_prompt 0, $prompt;
}

sub read_ync {
    my ($prompt) = @_;

    return read_prompt 1, $prompt;
}

sub run_command {
    my ($command, $redirect) = @_;
    my $start_time;
    my $end_time;
    my $dord = 0;
    my $pid;

    $start_time = time;

    doprint("$command ... ");

    $pid = open(CMD, "$command 2>&1 |") or
	dodie "unable to exec $command";

    if (defined($redirect)) {
	open (RD, ">$redirect") or
	    dodie "failed to write to redirect $redirect";
	$dord = 1;
    }

    while (<CMD>) {
	print RD  if ($dord);
    }

    waitpid($pid, 0);
    my $failed = $?;

    close(CMD);
    close(RD)  if ($dord);

    $end_time = time;
    my $delta = $end_time - $start_time;

    if ($delta == 1) {
	doprint "[1 second] ";
    } else {
	doprint "[$delta seconds] ";
    }

    if ($failed) {
	doprint "FAILED!\n";
    } else {
	doprint "SUCCESS\n";
    }

    return !$failed;
}

###### CONFIG BISECT ######

# config_ignore holds the configs that were set (or unset) for
# a good config and we will ignore these configs for the rest
# of a config bisect. These configs stay as they were.
my %config_ignore;

# config_set holds what all configs were set as.
my %config_set;

# config_off holds the set of configs that the bad config had disabled.
# We need to record them and set them in the .config when running
# olddefconfig, because olddefconfig keeps the defaults.
my %config_off;

# config_off_tmp holds a set of configs to turn off for now
my @config_off_tmp;

# config_list is the set of configs that are being tested
my %config_list;
my %null_config;

my %dependency;

my $make;

sub make_oldconfig {

    if (!run_command "$make olddefconfig") {
	# Perhaps olddefconfig doesn't exist in this version of the kernel
	# try oldnoconfig
	doprint "olddefconfig failed, trying make oldnoconfig\n";
	if (!run_command "$make oldnoconfig") {
	    doprint "oldnoconfig failed, trying yes '' | make oldconfig\n";
	    # try a yes '' | oldconfig
	    run_command "yes '' | $make oldconfig" or
		dodie "failed make config oldconfig";
	}
    }
}

sub assign_configs {
    my ($hash, $config) = @_;

    doprint "Reading configs from $config\n";

    open (IN, $config)
	or dodie "Failed to read $config";

    while (<IN>) {
	chomp;
	if (/^((CONFIG\S*)=.*)/) {
	    ${$hash}{$2} = $1;
	} elsif (/^(# (CONFIG\S*) is not set)/) {
	    ${$hash}{$2} = $1;
	}
    }

    close(IN);
}

sub process_config_ignore {
    my ($config) = @_;

    assign_configs \%config_ignore, $config;
}

sub get_dependencies {
    my ($config) = @_;

    my $arr = $dependency{$config};
    if (!defined($arr)) {
	return ();
    }

    my @deps = @{$arr};

    foreach my $dep (@{$arr}) {
	print "ADD DEP $dep\n";
	@deps = (@deps, get_dependencies $dep);
    }

    return @deps;
}

sub save_config {
    my ($pc, $file) = @_;

    my %configs = %{$pc};

    doprint "Saving configs into $file\n";

    open(OUT, ">$file") or dodie "Can not write to $file";

    foreach my $config (keys %configs) {
	print OUT "$configs{$config}\n";
    }
    close(OUT);
}

sub create_config {
    my ($name, $pc) = @_;

    doprint "Creating old config from $name configs\n";

    save_config $pc, $output_config;

    make_oldconfig;
}

# compare two config hashes, and return configs with different vals.
# It returns B's config values, but you can use A to see what A was.
sub diff_config_vals {
    my ($pa, $pb) = @_;

    # crappy Perl way to pass in hashes.
    my %a = %{$pa};
    my %b = %{$pb};

    my %ret;

    foreach my $item (keys %a) {
	if (defined($b{$item}) && $b{$item} ne $a{$item}) {
	    $ret{$item} = $b{$item};
	}
    }

    return %ret;
}

# compare two config hashes and return the configs in B but not A
sub diff_configs {
    my ($pa, $pb) = @_;

    my %ret;

    # crappy Perl way to pass in hashes.
    my %a = %{$pa};
    my %b = %{$pb};

    foreach my $item (keys %b) {
	if (!defined($a{$item})) {
	    $ret{$item} = $b{$item};
	}
    }

    return %ret;
}

# return if two configs are equal or not
# 0 is equal +1 b has something a does not
# +1 if a and b have a different item.
# -1 if a has something b does not
sub compare_configs {
    my ($pa, $pb) = @_;

    my %ret;

    # crappy Perl way to pass in hashes.
    my %a = %{$pa};
    my %b = %{$pb};

    foreach my $item (keys %b) {
	if (!defined($a{$item})) {
	    return 1;
	}
	if ($a{$item} ne $b{$item}) {
	    return 1;
	}
    }

    foreach my $item (keys %a) {
	if (!defined($b{$item})) {
	    return -1;
	}
    }

    return 0;
}

sub process_failed {
    my ($config) = @_;

    doprint "\n\n***************************************\n";
    doprint "Found bad config: $config\n";
    doprint "***************************************\n\n";
}

sub process_new_config {
    my ($tc, $nc, $gc, $bc) = @_;

    my %tmp_config = %{$tc};
    my %good_configs = %{$gc};
    my %bad_configs = %{$bc};

    my %new_configs;

    my $runtest = 1;
    my $ret;

    create_config "tmp_configs", \%tmp_config;
    assign_configs \%new_configs, $output_config;

    $ret = compare_configs \%new_configs, \%bad_configs;
    if (!$ret) {
	doprint "New config equals bad config, try next test\n";
	$runtest = 0;
    }

    if ($runtest) {
	$ret = compare_configs \%new_configs, \%good_configs;
	if (!$ret) {
	    doprint "New config equals good config, try next test\n";
	    $runtest = 0;
	}
    }

    %{$nc} = %new_configs;

    return $runtest;
}

sub convert_config {
    my ($config) = @_;

    if ($config =~ /^# (.*) is not set/) {
	$config = "$1=n";
    }

    $config =~ s/^CONFIG_//;
    return $config;
}

sub print_config {
    my ($sym, $config) = @_;

    $config = convert_config $config;
    doprint "$sym$config\n";
}

sub print_config_compare {
    my ($good_config, $bad_config) = @_;

    $good_config = convert_config $good_config;
    $bad_config = convert_config $bad_config;

    my $good_value = $good_config;
    my $bad_value = $bad_config;
    $good_value =~ s/(.*)=//;
    my $config = $1;

    $bad_value =~ s/.*=//;

    doprint " $config $good_value -> $bad_value\n";
}

# Pass in:
# $phalf: half of the configs names you want to add
# $oconfigs: The orginial configs to start with
# $sconfigs: The source to update $oconfigs with (from $phalf)
# $which: The name of which half that is updating (top / bottom)
# $type: The name of the source type (good / bad)
sub make_half {
    my ($phalf, $oconfigs, $sconfigs, $which, $type) = @_;

    my @half = @{$phalf};
    my %orig_configs = %{$oconfigs};
    my %source_configs = %{$sconfigs};

    my %tmp_config = %orig_configs;

    doprint "Settings bisect with $which half of $type configs:\n";
    foreach my $item (@half) {
	doprint "Updating $item to $source_configs{$item}\n";
	$tmp_config{$item} = $source_configs{$item};
    }

    return %tmp_config;
}

sub run_config_bisect {
    my ($pgood, $pbad) = @_;

    my %good_configs = %{$pgood};
    my %bad_configs = %{$pbad};

    my %diff_configs = diff_config_vals \%good_configs, \%bad_configs;
    my %b_configs = diff_configs \%good_configs, \%bad_configs;
    my %g_configs = diff_configs \%bad_configs, \%good_configs;

    # diff_arr is what is in both good and bad but are different (y->n)
    my @diff_arr = keys %diff_configs;
    my $len_diff = $#diff_arr + 1;

    # b_arr is what is in bad but not in good (has depends)
    my @b_arr = keys %b_configs;
    my $len_b = $#b_arr + 1;

    # g_arr is what is in good but not in bad
    my @g_arr = keys %g_configs;
    my $len_g = $#g_arr + 1;

    my $runtest = 0;
    my %new_configs;
    my $ret;

    # Look at the configs that are different between good and bad.
    # This does not include those that depend on other configs
    #  (configs depending on other configs that are not set would
    #   not show up even as a "# CONFIG_FOO is not set"


    doprint "# of configs to check:             $len_diff\n";
    doprint "# of configs showing only in good: $len_g\n";
    doprint "# of configs showing only in bad:  $len_b\n";

    if ($len_diff > 0) {
	# Now test for different values

	doprint "Configs left to check:\n";
	doprint "  Good Config\t\t\tBad Config\n";
	doprint "  -----------\t\t\t----------\n";
	foreach my $item (@diff_arr) {
	    doprint "  $good_configs{$item}\t$bad_configs{$item}\n";
	}

	my $half = int($#diff_arr / 2);
	my @tophalf = @diff_arr[0 .. $half];

	doprint "Set tmp config to be good config with some bad config values\n";

	my %tmp_config = make_half \@tophalf, \%good_configs,
	    \%bad_configs, "top", "bad";

	$runtest = process_new_config \%tmp_config, \%new_configs,
			    \%good_configs, \%bad_configs;

	if (!$runtest) {
	    doprint "Set tmp config to be bad config with some good config values\n";

	    my %tmp_config = make_half \@tophalf, \%bad_configs,
		\%good_configs, "top", "good";

	    $runtest = process_new_config \%tmp_config, \%new_configs,
		\%good_configs, \%bad_configs;
	}
    }

    if (!$runtest && $len_diff > 0) {
	# do the same thing, but this time with bottom half

	my $half = int($#diff_arr / 2);
	my @bottomhalf = @diff_arr[$half+1 .. $#diff_arr];

	doprint "Set tmp config to be good config with some bad config values\n";

	my %tmp_config = make_half \@bottomhalf, \%good_configs,
	    \%bad_configs, "bottom", "bad";

	$runtest = process_new_config \%tmp_config, \%new_configs,
			    \%good_configs, \%bad_configs;

	if (!$runtest) {
	    doprint "Set tmp config to be bad config with some good config values\n";

	    my %tmp_config = make_half \@bottomhalf, \%bad_configs,
		\%good_configs, "bottom", "good";

	    $runtest = process_new_config \%tmp_config, \%new_configs,
		\%good_configs, \%bad_configs;
	}
    }

    if ($runtest) {
	make_oldconfig;
	doprint "READY TO TEST .config IN $build\n";
	return 0;
    }

    doprint "\n%%%%%%%% FAILED TO FIND SINGLE BAD CONFIG %%%%%%%%\n";
    doprint "Hmm, can't make any more changes without making good == bad?\n";
    doprint "Difference between good (+) and bad (-)\n";

    foreach my $item (keys %bad_configs) {
	if (!defined($good_configs{$item})) {
	    print_config "-", $bad_configs{$item};
	}
    }

    foreach my $item (keys %good_configs) {
	next if (!defined($bad_configs{$item}));
	if ($good_configs{$item} ne $bad_configs{$item}) {
	    print_config_compare $good_configs{$item}, $bad_configs{$item};
	}
    }

    foreach my $item (keys %good_configs) {
	if (!defined($bad_configs{$item})) {
	    print_config "+", $good_configs{$item};
	}
    }
    return -1;
}

sub config_bisect {
    my ($good_config, $bad_config) = @_;
    my $ret;

    my %good_configs;
    my %bad_configs;
    my %tmp_configs;

    doprint "Run good configs through make oldconfig\n";
    assign_configs \%tmp_configs, $good_config;
    create_config "$good_config", \%tmp_configs;
    assign_configs \%good_configs, $output_config;

    doprint "Run bad configs through make oldconfig\n";
    assign_configs \%tmp_configs, $bad_config;
    create_config "$bad_config", \%tmp_configs;
    assign_configs \%bad_configs, $output_config;

    save_config \%good_configs, $good_config;
    save_config \%bad_configs, $bad_config;

    return run_config_bisect \%good_configs, \%bad_configs;
}

while ($#ARGV >= 0) {
    if ($ARGV[0] !~ m/^-/) {
	last;
    }
    my $opt = shift @ARGV;

    if ($opt eq "-b") {
	$val = shift @ARGV;
	if (!defined($val)) {
	    die "-b requires value\n";
	}
	$build = $val;
    }

    elsif ($opt eq "-l") {
	$val = shift @ARGV;
	if (!defined($val)) {
	    die "-l requires value\n";
	}
	$tree = $val;
    }

    elsif ($opt eq "-r") {
	$reset_bisect = 1;
    }

    elsif ($opt eq "-h") {
	usage;
    }

    else {
	die "Unknow option $opt\n";
    }
}

$build = $tree if (!defined($build));

$tree = expand_path $tree;
$build = expand_path $build;

if ( ! -d $tree ) {
    die "$tree not a directory\n";
}

if ( ! -d $build ) {
    die "$build not a directory\n";
}

usage if $#ARGV < 1;

if ($#ARGV == 1) {
    $start = 1;
} elsif ($#ARGV == 2) {
    $val = $ARGV[2];
    if ($val ne "good" && $val ne "bad") {
	die "Unknown command '$val', bust be either \"good\" or \"bad\"\n";
    }
} else {
    usage;
}

my $good_start = expand_path $ARGV[0];
my $bad_start = expand_path $ARGV[1];

my $good = "$good_start.tmp";
my $bad = "$bad_start.tmp";

$make = "make";

if ($build ne $tree) {
    $make = "make O=$build"
}

$output_config = "$build/.config";

if ($start) {
    if ( ! -f $good_start ) {
	die "$good_start not found\n";
    }
    if ( ! -f $bad_start ) {
	die "$bad_start not found\n";
    }
    if ( -f $good || -f $bad ) {
	my $p = "";

	if ( -f $good ) {
	    $p = "$good exists\n";
	}

	if ( -f $bad ) {
	    $p = "$p$bad exists\n";
	}

	if (!defined($reset_bisect)) {
	    if (!read_yn "${p}Overwrite and start new bisect anyway?") {
		exit (-1);
	    }
	}
    }
    run_command "cp $good_start $good" or die "failed to copy to $good\n";
    run_command "cp $bad_start $bad" or die "faield to copy to $bad\n";
} else {
    if ( ! -f $good ) {
	die "Can not find file $good\n";
    }
    if ( ! -f $bad ) {
	die "Can not find file $bad\n";
    }
    if ($val eq "good") {
	run_command "cp $output_config $good" or die "failed to copy $config to $good\n";
    } elsif ($val eq "bad") {
	run_command "cp $output_config $bad" or die "failed to copy $config to $bad\n";
    }
}

chdir $tree || die "can't change directory to $tree";

my $ret = config_bisect $good, $bad;

if (!$ret) {
    exit(0);
}

if ($ret > 0) {
    doprint "Cleaning temp files\n";
    run_command "rm $good";
    run_command "rm $bad";
    exit(1);
} else {
    doprint "See good and bad configs for details:\n";
    doprint "good: $good\n";
    doprint "bad:  $bad\n";
    doprint "%%%%%%%% FAILED TO FIND SINGLE BAD CONFIG %%%%%%%%\n";
}
exit(2);
