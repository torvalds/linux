#!/usr/bin/perl -w
#
# Copywrite 2010 - Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
# Licensed under the terms of the GNU GPL License version 2
#

use strict;
use IPC::Open2;
use Fcntl qw(F_GETFL F_SETFL O_NONBLOCK);
use File::Path qw(mkpath);
use File::Copy qw(cp);
use FileHandle;

$#ARGV >= 0 || die "usage: autotest.pl config-file\n";

$| = 1;

my %opt;
my %default;

#default opts
$default{"NUM_TESTS"}		= 5;
$default{"REBOOT_TYPE"}		= "grub";
$default{"TEST_TYPE"}		= "test";
$default{"BUILD_TYPE"}		= "randconfig";
$default{"MAKE_CMD"}		= "make";
$default{"TIMEOUT"}		= 120;
$default{"TMP_DIR"}		= "/tmp/autotest";
$default{"SLEEP_TIME"}		= 60;	# sleep time between tests
$default{"BUILD_NOCLEAN"}	= 0;
$default{"REBOOT_ON_ERROR"}	= 0;
$default{"POWEROFF_ON_ERROR"}	= 0;
$default{"REBOOT_ON_SUCCESS"}	= 1;
$default{"POWEROFF_ON_SUCCESS"}	= 0;
$default{"BUILD_OPTIONS"}	= "";
$default{"BISECT_SLEEP_TIME"}	= 60;   # sleep time between bisects
$default{"CLEAR_LOG"}		= 0;
$default{"SUCCESS_LINE"}	= "login:";
$default{"BOOTED_TIMEOUT"}	= 1;
$default{"DIE_ON_FAILURE"}	= 1;

my $version;
my $machine;
my $tmpdir;
my $builddir;
my $outputdir;
my $test_type;
my $build_type;
my $build_options;
my $reboot_type;
my $reboot_script;
my $power_cycle;
my $reboot_on_error;
my $poweroff_on_error;
my $die_on_failure;
my $powercycle_after_reboot;
my $poweroff_after_halt;
my $power_off;
my $grub_menu;
my $grub_number;
my $target;
my $make;
my $post_install;
my $noclean;
my $minconfig;
my $addconfig;
my $in_bisect = 0;
my $bisect_bad = "";
my $reverse_bisect;
my $in_patchcheck = 0;
my $run_test;
my $redirect;
my $buildlog;
my $dmesg;
my $monitor_fp;
my $monitor_pid;
my $monitor_cnt = 0;
my $sleep_time;
my $bisect_sleep_time;
my $store_failures;
my $timeout;
my $booted_timeout;
my $console;
my $success_line;
my $build_target;
my $target_image;
my $localversion;
my $iteration = 0;

sub read_config {
    my ($config) = @_;

    open(IN, $config) || die "can't read file $config";

    while (<IN>) {

	# ignore blank lines and comments
	next if (/^\s*$/ || /\s*\#/);

	if (/^\s*(\S+)\s*=\s*(.*?)\s*$/) {
	    my $lvalue = $1;
	    my $rvalue = $2;

	    if (defined($opt{$lvalue})) {
		die "Error: Option $lvalue defined more than once!\n";
	    }
	    $opt{$lvalue} = $rvalue;
	}
    }

    close(IN);

    # set any defaults

    foreach my $default (keys %default) {
	if (!defined($opt{$default})) {
	    $opt{$default} = $default{$default};
	}
    }
}

sub logit {
    if (defined($opt{"LOG_FILE"})) {
	open(OUT, ">> $opt{LOG_FILE}") or die "Can't write to $opt{LOG_FILE}";
	print OUT @_;
	close(OUT);
    }
}

sub doprint {
    print @_;
    logit @_;
}

sub run_command;

sub reboot {
    # try to reboot normally
    if (run_command "ssh $target reboot") {
	if (defined($powercycle_after_reboot)) {
	    sleep $powercycle_after_reboot;
	    run_command "$power_cycle";
	}
    } else {
	# nope? power cycle it.
	run_command "$power_cycle";
    }
}

sub do_not_reboot {
    my $i = $iteration;

    return $test_type eq "build" ||
	($test_type eq "patchcheck" && $opt{"PATCHCHECK_TYPE[$i]"} eq "build") ||
	($test_type eq "bisect" && $opt{"BISECT_TYPE[$i]"} eq "build");
}

sub dodie {
    doprint "CRITICAL FAILURE... ", @_, "\n";

    my $i = $iteration;

    if ($reboot_on_error && !do_not_reboot) {

	doprint "REBOOTING\n";
	reboot;

    } elsif ($poweroff_on_error && defined($power_off)) {
	doprint "POWERING OFF\n";
	`$power_off`;
    }

    die @_, "\n";
}

sub open_console {
    my ($fp) = @_;

    my $flags;

    my $pid = open($fp, "$console|") or
	dodie "Can't open console $console";

    $flags = fcntl($fp, F_GETFL, 0) or
	dodie "Can't get flags for the socket: $!";
    $flags = fcntl($fp, F_SETFL, $flags | O_NONBLOCK) or
	dodie "Can't set flags for the socket: $!";

    return $pid;
}

sub close_console {
    my ($fp, $pid) = @_;

    doprint "kill child process $pid\n";
    kill 2, $pid;

    print "closing!\n";
    close($fp);
}

sub start_monitor {
    if ($monitor_cnt++) {
	return;
    }
    $monitor_fp = \*MONFD;
    $monitor_pid = open_console $monitor_fp;

    return;

    open(MONFD, "Stop perl from warning about single use of MONFD");
}

sub end_monitor {
    if (--$monitor_cnt) {
	return;
    }
    close_console($monitor_fp, $monitor_pid);
}

sub wait_for_monitor {
    my ($time) = @_;
    my $line;

    doprint "** Wait for monitor to settle down **\n";

    # read the monitor and wait for the system to calm down
    do {
	$line = wait_for_input($monitor_fp, $time);
	print "$line" if (defined($line));
    } while (defined($line));
    print "** Monitor flushed **\n";
}

sub fail {

	if ($die_on_failure) {
		dodie @_;
	}

	doprint "FAILED\n";

	my $i = $iteration;

	# no need to reboot for just building.
	if (!do_not_reboot) {
	    doprint "REBOOTING\n";
	    reboot;
	    start_monitor;
	    wait_for_monitor $sleep_time;
	    end_monitor;
	}

	doprint "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";
	doprint "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";
	doprint "**** Failed: ", @_, " ****\n";
	doprint "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";
	doprint "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";

	return 1 if (!defined($store_failures));

	my @t = localtime;
	my $date = sprintf "%04d%02d%02d%02d%02d%02d",
		1900+$t[5],$t[4],$t[3],$t[2],$t[1],$t[0];

	my $dir = "$machine-$test_type-$build_type-fail-$date";
	my $faildir = "$store_failures/$dir";

	if (!-d $faildir) {
	    mkpath($faildir) or
		die "can't create $faildir";
	}
	if (-f "$outputdir/.config") {
	    cp "$outputdir/.config", "$faildir/config" or
		die "failed to copy .config";
	}
	if (-f $buildlog) {
	    cp $buildlog, "$faildir/buildlog" or
		die "failed to move $buildlog";
	}
	if (-f $dmesg) {
	    cp $dmesg, "$faildir/dmesg" or
		die "failed to move $dmesg";
	}

	doprint "*** Saved info to $faildir ***\n";

	return 1;
}

sub run_command {
    my ($command) = @_;
    my $dolog = 0;
    my $dord = 0;
    my $pid;

    doprint("$command ... ");

    $pid = open(CMD, "$command 2>&1 |") or
	(fail "unable to exec $command" and return 0);

    if (defined($opt{"LOG_FILE"})) {
	open(LOG, ">>$opt{LOG_FILE}") or
	    dodie "failed to write to log";
	$dolog = 1;
    }

    if (defined($redirect)) {
	open (RD, ">$redirect") or
	    dodie "failed to write to redirect $redirect";
	$dord = 1;
    }

    while (<CMD>) {
	print LOG if ($dolog);
	print RD  if ($dord);
    }

    waitpid($pid, 0);
    my $failed = $?;

    close(CMD);
    close(LOG) if ($dolog);
    close(RD)  if ($dord);

    if ($failed) {
	doprint "FAILED!\n";
    } else {
	doprint "SUCCESS\n";
    }

    return !$failed;
}

sub get_grub_index {

    if ($reboot_type ne "grub") {
	return;
    }
    return if (defined($grub_number));

    doprint "Find grub menu ... ";
    $grub_number = -1;
    open(IN, "ssh $target cat /boot/grub/menu.lst |")
	or die "unable to get menu.lst";
    while (<IN>) {
	if (/^\s*title\s+$grub_menu\s*$/) {
	    $grub_number++;
	    last;
	} elsif (/^\s*title\s/) {
	    $grub_number++;
	}
    }
    close(IN);

    die "Could not find '$grub_menu' in /boot/grub/menu on $machine"
	if ($grub_number < 0);
    doprint "$grub_number\n";
}

sub wait_for_input
{
    my ($fp, $time) = @_;
    my $rin;
    my $ready;
    my $line;
    my $ch;

    if (!defined($time)) {
	$time = $timeout;
    }

    $rin = '';
    vec($rin, fileno($fp), 1) = 1;
    $ready = select($rin, undef, undef, $time);

    $line = "";

    # try to read one char at a time
    while (sysread $fp, $ch, 1) {
	$line .= $ch;
	last if ($ch eq "\n");
    }

    if (!length($line)) {
	return undef;
    }

    return $line;
}

sub reboot_to {
    if ($reboot_type eq "grub") {
	run_command "ssh $target '(echo \"savedefault --default=$grub_number --once\" | grub --batch; reboot)'";
	return;
    }

    run_command "$reboot_script";
}

sub monitor {
    my $booted = 0;
    my $bug = 0;
    my $skip_call_trace = 0;
    my $loops;

    wait_for_monitor 5;

    my $line;
    my $full_line = "";

    open(DMESG, "> $dmesg") or
	die "unable to write to $dmesg";

    reboot_to;

    for (;;) {

	if ($booted) {
	    $line = wait_for_input($monitor_fp, $booted_timeout);
	} else {
	    $line = wait_for_input($monitor_fp);
	}

	last if (!defined($line));

	doprint $line;
	print DMESG $line;

	# we are not guaranteed to get a full line
	$full_line .= $line;

	if ($full_line =~ /$success_line/) {
	    $booted = 1;
	}

	if ($full_line =~ /\[ backtrace testing \]/) {
	    $skip_call_trace = 1;
	}

	if ($full_line =~ /call trace:/i) {
	    $bug = 1 if (!$skip_call_trace);
	}

	if ($full_line =~ /\[ end of backtrace testing \]/) {
	    $skip_call_trace = 0;
	}

	if ($full_line =~ /Kernel panic -/) {
	    $bug = 1;
	}

	if ($line =~ /\n/) {
	    $full_line = "";
	}
    }

    close(DMESG);

    if ($bug) {
	return 0 if ($in_bisect);
	fail "failed - got a bug report" and return 0;
    }

    if (!$booted) {
	return 0 if ($in_bisect);
	fail "failed - never got a boot prompt." and return 0;
    }

    return 1;
}

sub install {

    run_command "scp $outputdir/$build_target $target:$target_image" or
	dodie "failed to copy image";

    my $install_mods = 0;

    # should we process modules?
    $install_mods = 0;
    open(IN, "$outputdir/.config") or dodie("Can't read config file");
    while (<IN>) {
	if (/CONFIG_MODULES(=y)?/) {
	    $install_mods = 1 if (defined($1));
	    last;
	}
    }
    close(IN);

    if (!$install_mods) {
	doprint "No modules needed\n";
	return;
    }

    run_command "$make INSTALL_MOD_PATH=$tmpdir modules_install" or
	dodie "Failed to install modules";

    my $modlib = "/lib/modules/$version";
    my $modtar = "autotest-mods.tar.bz2";

    run_command "ssh $target rm -rf $modlib" or
	dodie "failed to remove old mods: $modlib";

    # would be nice if scp -r did not follow symbolic links
    run_command "cd $tmpdir && tar -cjf $modtar lib/modules/$version" or
	dodie "making tarball";

    run_command "scp $tmpdir/$modtar $target:/tmp" or
	dodie "failed to copy modules";

    unlink "$tmpdir/$modtar";

    run_command "ssh $target '(cd / && tar xf /tmp/$modtar)'" or
	dodie "failed to tar modules";

    run_command "ssh $target rm -f /tmp/$modtar";

    return if (!defined($post_install));

    my $save_env = $ENV{KERNEL_VERSION};

    $ENV{KERNEL_VERSION} = $version;
    run_command "$post_install" or
	dodie "Failed to run post install";

    $ENV{KERNEL_VERSION} = $save_env;
}

sub check_buildlog {
    my ($patch) = @_;

    my @files = `git show $patch | diffstat -l`;

    open(IN, "git show $patch |") or
	dodie "failed to show $patch";
    while (<IN>) {
	if (m,^--- a/(.*),) {
	    chomp $1;
	    $files[$#files] = $1;
	}
    }
    close(IN);

    open(IN, $buildlog) or dodie "Can't open $buildlog";
    while (<IN>) {
	if (/^\s*(.*?):.*(warning|error)/) {
	    my $err = $1;
	    foreach my $file (@files) {
		my $fullpath = "$builddir/$file";
		if ($file eq $err || $fullpath eq $err) {
		    fail "$file built with warnings" and return 0;
		}
	    }
	}
    }
    close(IN);

    return 1;
}

sub build {
    my ($type) = @_;
    my $defconfig = "";
    my $append = "";

    unlink $buildlog;

    if ($type =~ /^useconfig:(.*)/) {
	run_command "cp $1 $outputdir/.config" or
	    dodie "could not copy $1 to .config";

	$type = "oldconfig";
    }

    # old config can ask questions
    if ($type eq "oldconfig") {
	$append = "yes ''|";

	# allow for empty configs
	run_command "touch $outputdir/.config";

	run_command "mv $outputdir/.config $outputdir/config_temp" or
	    dodie "moving .config";

	if (!$noclean && !run_command "$make mrproper") {
	    dodie "make mrproper";
	}

	run_command "mv $outputdir/config_temp $outputdir/.config" or
	    dodie "moving config_temp";

    } elsif (!$noclean) {
	unlink "$outputdir/.config";
	run_command "$make mrproper" or
	    dodie "make mrproper";
    }

    # add something to distinguish this build
    open(OUT, "> $outputdir/localversion") or dodie("Can't make localversion file");
    print OUT "$localversion\n";
    close(OUT);

    if (defined($minconfig)) {
	$defconfig = "KCONFIG_ALLCONFIG=$minconfig";
    }

    run_command "$append $defconfig $make $type" or
	dodie "failed make config";

    $redirect = "$buildlog";
    if (!run_command "$make $build_options") {
	undef $redirect;
	# bisect may need this to pass
	return 0 if ($in_bisect);
	fail "failed build" and return 0;
    }
    undef $redirect;

    return 1;
}

sub halt {
    if (!run_command "ssh $target halt" or defined($power_off)) {
	if (defined($poweroff_after_halt)) {
	    sleep $poweroff_after_halt;
	    run_command "$power_off";
	}
    } else {
	# nope? the zap it!
	run_command "$power_off";
    }
}

sub success {
    my ($i) = @_;

    doprint "\n\n*******************************************\n";
    doprint     "*******************************************\n";
    doprint     "**           TEST $i SUCCESS!!!!         **\n";
    doprint     "*******************************************\n";
    doprint     "*******************************************\n";

    if ($i != $opt{"NUM_TESTS"} && !do_not_reboot) {
	doprint "Reboot and wait $sleep_time seconds\n";
	reboot;
	start_monitor;
	wait_for_monitor $sleep_time;
	end_monitor;
    }
}

sub get_version {
    # get the release name
    doprint "$make kernelrelease ... ";
    $version = `$make kernelrelease | tail -1`;
    chomp($version);
    doprint "$version\n";
}

sub child_run_test {
    my $failed = 0;

    # child should have no power
    $reboot_on_error = 0;
    $poweroff_on_error = 0;
    $die_on_failure = 1;

    run_command $run_test or $failed = 1;
    exit $failed;
}

my $child_done;

sub child_finished {
    $child_done = 1;
}

sub do_run_test {
    my $child_pid;
    my $child_exit;
    my $line;
    my $full_line;
    my $bug = 0;

    wait_for_monitor 1;

    doprint "run test $run_test\n";

    $child_done = 0;

    $SIG{CHLD} = qw(child_finished);

    $child_pid = fork;

    child_run_test if (!$child_pid);

    $full_line = "";

    do {
	$line = wait_for_input($monitor_fp, 1);
	if (defined($line)) {

	    # we are not guaranteed to get a full line
	    $full_line .= $line;

	    if ($full_line =~ /call trace:/i) {
		$bug = 1;
	    }

	    if ($full_line =~ /Kernel panic -/) {
		$bug = 1;
	    }

	    if ($line =~ /\n/) {
		$full_line = "";
	    }
	}
    } while (!$child_done && !$bug);

    if ($bug) {
	doprint "Detected kernel crash!\n";
	# kill the child with extreme prejudice
	kill 9, $child_pid;
    }

    waitpid $child_pid, 0;
    $child_exit = $?;

    if ($bug || $child_exit) {
	return 0 if $in_bisect;
	fail "test failed" and return 0;
    }
    return 1;
}

sub run_git_bisect {
    my ($command) = @_;

    doprint "$command ... ";

    my $output = `$command 2>&1`;
    my $ret = $?;

    logit $output;

    if ($ret) {
	doprint "FAILED\n";
	dodie "Failed to git bisect";
    }

    doprint "SUCCESS\n";
    if ($output =~ m/^(Bisecting: .*\(roughly \d+ steps?\))\s+\[([[:xdigit:]]+)\]/) {
	doprint "$1 [$2]\n";
    } elsif ($output =~ m/^([[:xdigit:]]+) is the first bad commit/) {
	$bisect_bad = $1;
	doprint "Found bad commit... $1\n";
	return 0;
    } else {
	# we already logged it, just print it now.
	print $output;
    }

    return 1;
}

sub run_bisect {
    my ($type) = @_;

    my $failed = 0;
    my $result;
    my $output;
    my $ret;

    if (defined($minconfig)) {
	build "useconfig:$minconfig" or $failed = 1;
    } else {
	# ?? no config to use?
	build "oldconfig" or $failed = 1;
    }

    if ($type ne "build") {
	dodie "Failed on build" if $failed;

	# Now boot the box
	get_grub_index;
	get_version;
	install;

	start_monitor;
	monitor or $failed = 1;

	if ($type ne "boot") {
	    dodie "Failed on boot" if $failed;

	    do_run_test or $failed = 1;
	}
	end_monitor;
    }

    if ($failed) {
	$result = "bad";

	# reboot the box to a good kernel
	if ($type ne "build") {
	    doprint "Reboot and sleep $bisect_sleep_time seconds\n";
	    reboot;
	    start_monitor;
	    wait_for_monitor $bisect_sleep_time;
	    end_monitor;
	}
    } else {
	$result = "good";
    }

    # Are we looking for where it worked, not failed?
    if ($reverse_bisect) {
	if ($failed) {
	    $result = "good";
	} else {
	    $result = "bad";
	}
    }

    return $result;
}

sub bisect {
    my ($i) = @_;

    my $result;

    die "BISECT_GOOD[$i] not defined\n"	if (!defined($opt{"BISECT_GOOD[$i]"}));
    die "BISECT_BAD[$i] not defined\n"	if (!defined($opt{"BISECT_BAD[$i]"}));
    die "BISECT_TYPE[$i] not defined\n"	if (!defined($opt{"BISECT_TYPE[$i]"}));

    my $good = $opt{"BISECT_GOOD[$i]"};
    my $bad = $opt{"BISECT_BAD[$i]"};
    my $type = $opt{"BISECT_TYPE[$i]"};
    my $start = $opt{"BISECT_START[$i]"};
    my $replay = $opt{"BISECT_REPLAY[$i]"};

    if (defined($opt{"BISECT_REVERSE[$i]"}) &&
	$opt{"BISECT_REVERSE[$i]"} == 1) {
	doprint "Performing a reverse bisect (bad is good, good is bad!)\n";
	$reverse_bisect = 1;
    } else {
	$reverse_bisect = 0;
    }

    $in_bisect = 1;

    # Can't have a test without having a test to run
    if ($type eq "test" && !defined($run_test)) {
	$type = "boot";
    }

    my $check = $opt{"BISECT_CHECK[$i]"};
    if (defined($check) && $check ne "0") {

	# get current HEAD
	doprint "git rev-list HEAD --max-count=1 ... ";
	my $head = `git rev-list HEAD --max-count=1`;
	my $ret = $?;

	logit $head;

	if ($ret) {
	    doprint "FAILED\n";
	    dodie "Failed to get git HEAD";
	}

	print "SUCCESS\n";

	chomp $head;

	if ($check ne "good") {
	    doprint "TESTING BISECT BAD [$bad]\n";
	    run_command "git checkout $bad" or
		die "Failed to checkout $bad";

	    $result = run_bisect $type;

	    if ($result ne "bad") {
		fail "Tested BISECT_BAD [$bad] and it succeeded" and return 0;
	    }
	}

	if ($check ne "bad") {
	    doprint "TESTING BISECT GOOD [$good]\n";
	    run_command "git checkout $good" or
		die "Failed to checkout $good";

	    $result = run_bisect $type;

	    if ($result ne "good") {
		fail "Tested BISECT_GOOD [$good] and it failed" and return 0;
	    }
	}

	# checkout where we started
	run_command "git checkout $head" or
	    die "Failed to checkout $head";
    }

    run_command "git bisect start" or
	dodie "could not start bisect";

    run_command "git bisect good $good" or
	dodie "could not set bisect good to $good";

    run_git_bisect "git bisect bad $bad" or
	dodie "could not set bisect bad to $bad";

    if (defined($replay)) {
	run_command "git bisect replay $replay" or
	    dodie "failed to run replay";
    }

    if (defined($start)) {
	run_command "git checkout $start" or
	    dodie "failed to checkout $start";
    }

    my $test;
    do {
	$result = run_bisect $type;
	$test = run_git_bisect "git bisect $result";
    } while ($test);

    run_command "git bisect log" or
	dodie "could not capture git bisect log";

    run_command "git bisect reset" or
	dodie "could not reset git bisect";

    doprint "Bad commit was [$bisect_bad]\n";

    $in_bisect = 0;

    success $i;
}

sub patchcheck {
    my ($i) = @_;

    die "PATCHCHECK_START[$i] not defined\n"
	if (!defined($opt{"PATCHCHECK_START[$i]"}));
    die "PATCHCHECK_TYPE[$i] not defined\n"
	if (!defined($opt{"PATCHCHECK_TYPE[$i]"}));

    my $start = $opt{"PATCHCHECK_START[$i]"};

    my $end = "HEAD";
    if (defined($opt{"PATCHCHECK_END[$i]"})) {
	$end = $opt{"PATCHCHECK_END[$i]"};
    }

    my $type = $opt{"PATCHCHECK_TYPE[$i]"};

    # Can't have a test without having a test to run
    if ($type eq "test" && !defined($run_test)) {
	$type = "boot";
    }

    open (IN, "git log --pretty=oneline $end|") or
	dodie "could not get git list";

    my @list;

    while (<IN>) {
	chomp;
	$list[$#list+1] = $_;
	last if (/^$start/);
    }
    close(IN);

    if ($list[$#list] !~ /^$start/) {
	fail "SHA1 $start not found";
    }

    # go backwards in the list
    @list = reverse @list;

    my $save_clean = $noclean;

    $in_patchcheck = 1;
    foreach my $item (@list) {
	my $sha1 = $item;
	$sha1 =~ s/^([[:xdigit:]]+).*/$1/;

	doprint "\nProcessing commit $item\n\n";

	run_command "git checkout $sha1" or
	    die "Failed to checkout $sha1";

	# only clean on the first and last patch
	if ($item eq $list[0] ||
	    $item eq $list[$#list]) {
	    $noclean = $save_clean;
	} else {
	    $noclean = 1;
	}

	if (defined($minconfig)) {
	    build "useconfig:$minconfig" or return 0;
	} else {
	    # ?? no config to use?
	    build "oldconfig" or return 0;
	}

	check_buildlog $sha1 or return 0;

	next if ($type eq "build");

	get_grub_index;
	get_version;
	install;

	my $failed = 0;

	start_monitor;
	monitor or $failed = 1;

	if (!$failed && $type ne "boot"){
	    do_run_test or $failed = 1;
	}
	end_monitor;
	return 0 if ($failed);

    }
    $in_patchcheck = 0;
    success $i;

    return 1;
}

read_config $ARGV[0];

# mandatory configs
die "MACHINE not defined\n"		if (!defined($opt{"MACHINE"}));
die "SSH_USER not defined\n"		if (!defined($opt{"SSH_USER"}));
die "BUILD_DIR not defined\n"		if (!defined($opt{"BUILD_DIR"}));
die "OUTPUT_DIR not defined\n"		if (!defined($opt{"OUTPUT_DIR"}));
die "BUILD_TARGET not defined\n"	if (!defined($opt{"BUILD_TARGET"}));
die "TARGET_IMAGE not defined\n"	if (!defined($opt{"TARGET_IMAGE"}));
die "POWER_CYCLE not defined\n"		if (!defined($opt{"POWER_CYCLE"}));
die "CONSOLE not defined\n"		if (!defined($opt{"CONSOLE"}));
die "LOCALVERSION not defined\n"	if (!defined($opt{"LOCALVERSION"}));

if ($opt{"CLEAR_LOG"} && defined($opt{"LOG_FILE"})) {
    unlink $opt{"LOG_FILE"};
}

doprint "\n\nSTARTING AUTOMATED TESTS\n\n";

foreach my $option (sort keys %opt) {
    doprint "$option = $opt{$option}\n";
}

sub set_test_option {
    my ($name, $i) = @_;

    my $option = "$name\[$i\]";

    if (defined($opt{$option})) {
	return $opt{$option};
    }

    if (defined($opt{$name})) {
	return $opt{$name};
    }

    return undef;
}

# First we need to do is the builds
for (my $i = 1; $i <= $opt{"NUM_TESTS"}; $i++) {

    $iteration = $i;

    my $ssh_user = set_test_option("SSH_USER", $i);
    my $makecmd = set_test_option("MAKE_CMD", $i);

    $machine = set_test_option("MACHINE", $i);
    $tmpdir = set_test_option("TMP_DIR", $i);
    $outputdir = set_test_option("OUTPUT_DIR", $i);
    $builddir = set_test_option("BUILD_DIR", $i);
    $test_type = set_test_option("TEST_TYPE", $i);
    $build_type = set_test_option("BUILD_TYPE", $i);
    $build_options = set_test_option("BUILD_OPTIONS", $i);
    $power_cycle = set_test_option("POWER_CYCLE", $i);
    $noclean = set_test_option("BUILD_NOCLEAN", $i);
    $minconfig = set_test_option("MIN_CONFIG", $i);
    $run_test = set_test_option("TEST", $i);
    $addconfig = set_test_option("ADD_CONFIG", $i);
    $reboot_type = set_test_option("REBOOT_TYPE", $i);
    $grub_menu = set_test_option("GRUB_MENU", $i);
    $post_install = set_test_option("POST_INSTALL", $i);
    $reboot_script = set_test_option("REBOOT_SCRIPT", $i);
    $reboot_on_error = set_test_option("REBOOT_ON_ERROR", $i);
    $poweroff_on_error = set_test_option("POWEROFF_ON_ERROR", $i);
    $die_on_failure = set_test_option("DIE_ON_FAILURE", $i);
    $power_off = set_test_option("POWER_OFF", $i);
    $powercycle_after_reboot = set_test_option("POWERCYCLE_AFTER_REBOOT", $i);
    $poweroff_after_halt = set_test_option("POWEROFF_AFTER_HALT", $i);
    $sleep_time = set_test_option("SLEEP_TIME", $i);
    $bisect_sleep_time = set_test_option("BISECT_SLEEP_TIME", $i);
    $store_failures = set_test_option("STORE_FAILURES", $i);
    $timeout = set_test_option("TIMEOUT", $i);
    $booted_timeout = set_test_option("BOOTED_TIMEOUT", $i);
    $console = set_test_option("CONSOLE", $i);
    $success_line = set_test_option("SUCCESS_LINE", $i);
    $build_target = set_test_option("BUILD_TARGET", $i);
    $target_image = set_test_option("TARGET_IMAGE", $i);
    $localversion = set_test_option("LOCALVERSION", $i);

    chdir $builddir || die "can't change directory to $builddir";

    if (!-d $tmpdir) {
	mkpath($tmpdir) or
	    die "can't create $tmpdir";
    }

    $target = "$ssh_user\@$machine";

    $buildlog = "$tmpdir/buildlog-$machine";
    $dmesg = "$tmpdir/dmesg-$machine";
    $make = "$makecmd O=$outputdir";

    if ($reboot_type eq "grub") {
	dodie "GRUB_MENU not defined" if (!defined($grub_menu));
    } elsif (!defined($reboot_script)) {
	dodie "REBOOT_SCRIPT not defined"
    }

    my $run_type = $build_type;
    if ($test_type eq "patchcheck") {
	$run_type = $opt{"PATCHCHECK_TYPE[$i]"};
    } elsif ($test_type eq "bisect") {
	$run_type = $opt{"BISECT_TYPE[$i]"};
    }

    # mistake in config file?
    if (!defined($run_type)) {
	$run_type = "ERROR";
    }

    doprint "\n\n";
    doprint "RUNNING TEST $i of $opt{NUM_TESTS} with option $test_type $run_type\n\n";

    unlink $dmesg;
    unlink $buildlog;

    if (!defined($minconfig)) {
	$minconfig = $addconfig;

    } elsif (defined($addconfig)) {
	run_command "cat $addconfig $minconfig > $tmpdir/use_config" or
	    dodie "Failed to create temp config";
	$minconfig = "$tmpdir/use_config";
    }

    my $checkout = $opt{"CHECKOUT[$i]"};
    if (defined($checkout)) {
	run_command "git checkout $checkout" or
	    die "failed to checkout $checkout";
    }

    if ($test_type eq "bisect") {
	bisect $i;
	next;
    } elsif ($test_type eq "patchcheck") {
	patchcheck $i;
	next;
    }

    if ($build_type ne "nobuild") {
	build $build_type or next;
    }

    if ($test_type ne "build") {
	get_grub_index;
	get_version;
	install;

	my $failed = 0;
	start_monitor;
	monitor or $failed = 1;;

	if (!$failed && $test_type ne "boot" && defined($run_test)) {
	    do_run_test or $failed = 1;
	}
	end_monitor;
	next if ($failed);
    }

    success $i;
}

if ($opt{"POWEROFF_ON_SUCCESS"}) {
    halt;
} elsif ($opt{"REBOOT_ON_SUCCESS"} && !do_not_reboot) {
    reboot;
}

exit 0;
