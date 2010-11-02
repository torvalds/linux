#!/usr/bin/perl -w
#
# Copywrite 2010 - Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
# Licensed under the terms of the GNU GPL License version 2
#

use strict;
use IPC::Open2;
use Fcntl qw(F_GETFL F_SETFL O_NONBLOCK);
use FileHandle;

$#ARGV >= 0 || die "usage: autotest.pl config-file\n";

$| = 1;

my %opt;

#default opts
$opt{"NUM_BUILDS"}		= 5;
$opt{"DEFAULT_BUILD_TYPE"}	= "randconfig";
$opt{"MAKE_CMD"}		= "make";
$opt{"TIMEOUT"}			= 120;
$opt{"TMP_DIR"}			= "/tmp/autotest";
$opt{"SLEEP_TIME"}		= 60;	# sleep time between tests
$opt{"BUILD_NOCLEAN"}		= 0;
$opt{"REBOOT_ON_ERROR"}		= 0;
$opt{"POWEROFF_ON_ERROR"}	= 0;
$opt{"REBOOT_ON_SUCCESS"}	= 1;
$opt{"POWEROFF_ON_SUCCESS"}	= 0;
$opt{"BUILD_OPTIONS"}		= "";
$opt{"BISECT_SLEEP_TIME"}	= 10;   # sleep time between bisects
$opt{"CLEAR_LOG"}		= 0;
$opt{"SUCCESS_LINE"}		= "login:";
$opt{"BOOTED_TIMEOUT"}		= 1;
$opt{"DIE_ON_FAILURE"}		= 1;

my $version;
my $grub_number;
my $target;
my $make;
my $noclean;
my $minconfig;
my $addconfig;
my $in_bisect = 0;
my $bisect_bad = "";
my $reverse_bisect;
my $in_patchcheck = 0;
my $run_test;
my $redirect;

sub read_config {
    my ($config) = @_;

    open(IN, $config) || die "can't read file $config";

    while (<IN>) {

	# ignore blank lines and comments
	next if (/^\s*$/ || /\s*\#/);

	if (/^\s*(\S+)\s*=\s*(.*?)\s*$/) {
	    my $lvalue = $1;
	    my $rvalue = $2;

	    $opt{$lvalue} = $rvalue;
	}
    }

    close(IN);
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

sub dodie {
    doprint "CRITICAL FAILURE... ", @_, "\n";

    if ($opt{"REBOOT_ON_ERROR"}) {
	doprint "REBOOTING\n";
	`$opt{"POWER_CYCLE"}`;

    } elsif ($opt{"POWEROFF_ON_ERROR"} && defined($opt{"POWER_OFF"})) {
	doprint "POWERING OFF\n";
	`$opt{"POWER_OFF"}`;
    }

    die @_;
}

sub fail {

	if ($opt{"DIE_ON_FAILURE"}) {
		dodie @_;
	}

	doprint "Failed: ", @_, "\n";
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

    return if (defined($grub_number));

    doprint "Find grub menu ... ";
    $grub_number = -1;
    open(IN, "ssh $target cat /boot/grub/menu.lst |")
	or die "unable to get menu.lst";
    while (<IN>) {
	if (/^\s*title\s+$opt{GRUB_MENU}\s*$/) {
	    $grub_number++;
	    last;
	} elsif (/^\s*title\s/) {
	    $grub_number++;
	}
    }
    close(IN);

    die "Could not find '$opt{GRUB_MENU}' in /boot/grub/menu on $opt{MACHINE}"
	if ($grub_number < 0);
    doprint "$grub_number\n";
}

my $timeout = $opt{"TIMEOUT"};

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
    run_command "ssh $target '(echo \"savedefault --default=$grub_number --once\" | grub --batch; reboot)'";
}

sub open_console {
    my ($fp) = @_;

    my $flags;

    my $pid = open($fp, "$opt{CONSOLE}|") or
	dodie "Can't open console $opt{CONSOLE}";

    $flags = fcntl($fp, F_GETFL, 0) or
	dodie "Can't get flags for the socket: $!\n";
    $flags = fcntl($fp, F_SETFL, $flags | O_NONBLOCK) or
	dodie "Can't set flags for the socket: $!\n";

    return $pid;
}

sub close_console {
    my ($fp, $pid) = @_;

    doprint "kill child process $pid\n";
    kill 2, $pid;

    print "closing!\n";
    close($fp);
}

sub monitor {
    my $booted = 0;
    my $bug = 0;
    my $pid;
    my $skip_call_trace = 0;
    my $fp = \*IN;
    my $loops;

    $pid = open_console($fp);

    my $line;
    my $full_line = "";

    doprint "Wait for monitor to settle down.\n";
    # read the monitor and wait for the system to calm down
    do {
	$line = wait_for_input($fp, 5);
    } while (defined($line));

    reboot_to;

    for (;;) {

	if ($booted) {
	    $line = wait_for_input($fp, $opt{"BOOTED_TIMEOUT"});
	} else {
	    $line = wait_for_input($fp);
	}

	last if (!defined($line));

	doprint $line;

	# we are not guaranteed to get a full line
	$full_line .= $line;

	if ($full_line =~ /$opt{"SUCCESS_LINE"}/) {
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

    close_console($fp, $pid);

    if (!$booted) {
	return 0 if ($in_bisect);
	fail "failed - never got a boot prompt.\n" and return 0;
    }

    if ($bug) {
	return 0 if ($in_bisect);
	fail "failed - got a bug report\n" and return 0;
    }

    return 1;
}

sub install {

    run_command "scp $opt{OUTPUT_DIR}/$opt{BUILD_TARGET} $target:$opt{TARGET_IMAGE}" or
	dodie "failed to copy image";

    my $install_mods = 0;

    # should we process modules?
    $install_mods = 0;
    open(IN, "$opt{OUTPUT_DIR}/.config") or dodie("Can't read config file");
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

    run_command "$make INSTALL_MOD_PATH=$opt{TMP_DIR} modules_install" or
	dodie "Failed to install modules";

    my $modlib = "/lib/modules/$version";
    my $modtar = "autotest-mods.tar.bz2";

    run_command "ssh $target rm -rf $modlib" or
	dodie "failed to remove old mods: $modlib";

    # would be nice if scp -r did not follow symbolic links
    run_command "cd $opt{TMP_DIR} && tar -cjf $modtar lib/modules/$version" or
	dodie "making tarball";

    run_command "scp $opt{TMP_DIR}/$modtar $target:/tmp" or
	dodie "failed to copy modules";

    unlink "$opt{TMP_DIR}/$modtar";

    run_command "ssh $target '(cd / && tar xf /tmp/$modtar)'" or
	dodie "failed to tar modules";

    run_command "ssh $target rm -f /tmp/$modtar";
}

sub check_buildlog {
    my ($patch) = @_;

    my $buildlog = "$opt{TMP_DIR}/buildlog";
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
		my $fullpath = "$opt{BUILD_DIR}/$file";
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

    if ($type =~ /^useconfig:(.*)/) {
	run_command "cp $1 $opt{OUTPUT_DIR}/.config" or
	    dodie "could not copy $1 to .config";

	$type = "oldconfig";
    }

    # old config can ask questions
    if ($type eq "oldconfig") {
	$append = "yes ''|";

	# allow for empty configs
	run_command "touch $opt{OUTPUT_DIR}/.config";

	run_command "mv $opt{OUTPUT_DIR}/.config $opt{OUTPUT_DIR}/config_temp" or
	    dodie "moving .config";

	if (!$noclean && !run_command "$make mrproper") {
	    dodie "make mrproper";
	}

	run_command "mv $opt{OUTPUT_DIR}/config_temp $opt{OUTPUT_DIR}/.config" or
	    dodie "moving config_temp";

    } elsif (!$noclean) {
	unlink "$opt{OUTPUT_DIR}/.config";
	run_command "$make mrproper" or
	    dodie "make mrproper";
    }

    # add something to distinguish this build
    open(OUT, "> $opt{OUTPUT_DIR}/localversion") or dodie("Can't make localversion file");
    print OUT "$opt{LOCALVERSION}\n";
    close(OUT);

    if (defined($minconfig)) {
	$defconfig = "KCONFIG_ALLCONFIG=$minconfig";
    }

    run_command "$defconfig $append $make $type" or
	dodie "failed make config";

    # patch check will examine the log
    if ($in_patchcheck) {
	$redirect = "$opt{TMP_DIR}/buildlog";
    }

    if (!run_command "$make $opt{BUILD_OPTIONS}") {
	undef $redirect;
	# bisect may need this to pass
	return 0 if ($in_bisect);
	fail "failed build" and return 0;
    }
    undef $redirect;

    return 1;
}

sub reboot {
    # try to reboot normally
    if (!run_command "ssh $target reboot") {
	# nope? power cycle it.
	run_command "$opt{POWER_CYCLE}";
    }
}

sub halt {
    if (!run_command "ssh $target halt" or defined($opt{"POWER_OFF"})) {
	# nope? the zap it!
	run_command "$opt{POWER_OFF}";
    }
}

sub success {
    my ($i) = @_;

    doprint "\n\n*******************************************\n";
    doprint     "*******************************************\n";
    doprint     "**            SUCCESS!!!!                **\n";
    doprint     "*******************************************\n";
    doprint     "*******************************************\n";

    if ($i != $opt{"NUM_BUILDS"}) {
	reboot;
	doprint "Sleeping $opt{SLEEP_TIME} seconds\n";
	sleep "$opt{SLEEP_TIME}";
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
    my $failed;

    $failed = !run_command $run_test;
    exit $failed;
}

my $child_done;

sub child_finished {
    $child_done = 1;
}

sub do_run_test {
    my $child_pid;
    my $child_exit;
    my $pid;
    my $line;
    my $full_line;
    my $bug = 0;
    my $fp = \*IN;

    $pid = open_console($fp);

    # read the monitor and wait for the system to calm down
    do {
	$line = wait_for_input($fp, 1);
    } while (defined($line));

    $child_done = 0;

    $SIG{CHLD} = qw(child_finished);

    $child_pid = fork;

    child_run_test if (!$child_pid);

    $full_line = "";

    do {
	$line = wait_for_input($fp, 1);
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

    close_console($fp, $pid);

    if ($bug || $child_exit) {
	return 0 if $in_bisect;
	fail "test failed" and return 0;
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
	fail "Failed on build" if $failed;

	# Now boot the box
	get_grub_index;
	get_version;
	install;
	monitor or $failed = 1;

	if ($type ne "boot") {
	    fail "Failed on boot" if $failed;

	    do_run_test or $failed = 1;
	}
    }

    if ($failed) {
	$result = "bad";

	# reboot the box to a good kernel
	if ($type eq "boot") {
	    reboot;
	    doprint "sleep a little for reboot\n";
	    sleep $opt{"BISECT_SLEEP_TIME"};
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

    doprint "git bisect $result ... ";
    $output = `git bisect $result 2>&1`;
    $ret = $?;

    logit $output;

    if ($ret) {
	doprint "FAILED\n";
	fail "Failed to git bisect";
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

sub bisect {
    my ($i) = @_;

    my $result;

    die "BISECT_GOOD[$i] not defined\n"	if (!defined($opt{"BISECT_GOOD[$i]"}));
    die "BISECT_BAD[$i] not defined\n"	if (!defined($opt{"BISECT_BAD[$i]"}));
    die "BISECT_TYPE[$i] not defined\n"	if (!defined($opt{"BISECT_TYPE[$i]"}));

    my $good = $opt{"BISECT_GOOD[$i]"};
    my $bad = $opt{"BISECT_BAD[$i]"};
    my $type = $opt{"BISECT_TYPE[$i]"};

    if (defined($opt{"BISECT_REVERSE[$i]"}) &&
	$opt{"BISECT_REVERSE[$i]"} == 1) {
	doprint "Performing a reverse bisect (bad is good, good is bad!)\n";
	$reverse_bisect = 1;
    } else {
	$reverse_bisect = 0;
    }

    $in_bisect = 1;

    run_command "git bisect start" or
	fail "could not start bisect";

    run_command "git bisect good $good" or
	fail "could not set bisect good to $good";

    run_command "git bisect bad $bad" or
	fail "could not set bisect good to $bad";

    # Can't have a test without having a test to run
    if ($type eq "test" && !defined($run_test)) {
	$type = "boot";
    }

    do {
	$result = run_bisect $type;
    } while ($result);

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
	monitor or return 0;

	next if ($type eq "boot");
	do_run_test or next;
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
die "GRUB_MENU not defined\n"		if (!defined($opt{"GRUB_MENU"}));

chdir $opt{"BUILD_DIR"} || die "can't change directory to $opt{BUILD_DIR}";

$target = "$opt{SSH_USER}\@$opt{MACHINE}";

if ($opt{"CLEAR_LOG"} && defined($opt{"LOG_FILE"})) {
    unlink $opt{"LOG_FILE"};
}

doprint "\n\nSTARTING AUTOMATED TESTS\n\n";

foreach my $option (sort keys %opt) {
    doprint "$option = $opt{$option}\n";
}

$make = "$opt{MAKE_CMD} O=$opt{OUTPUT_DIR}";

sub set_build_option {
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
for (my $i = 1; $i <= $opt{"NUM_BUILDS"}; $i++) {
    my $type = "BUILD_TYPE[$i]";

    if (!defined($opt{$type})) {
	$opt{$type} = $opt{"DEFAULT_BUILD_TYPE"};
    }

    $noclean = set_build_option("BUILD_NOCLEAN", $i);
    $minconfig = set_build_option("MIN_CONFIG", $i);
    $run_test = set_build_option("TEST", $i);
    $addconfig = set_build_option("ADD_CONFIG", $i);

    doprint "\n\n";
    doprint "RUNNING TEST $i of $opt{NUM_BUILDS} with option $opt{$type}\n\n";

    if (!defined($minconfig)) {
	$minconfig = $addconfig;

    } elsif (defined($addconfig)) {
	run_command "cat $addconfig $minconfig > $opt{TMP_DIR}/use_config" or
	    dodie "Failed to create temp config";
	$minconfig = "$opt{TMP_DIR}/use_config";
    }

    my $checkout = $opt{"CHECKOUT[$i]"};
    if (defined($checkout)) {
	run_command "git checkout $checkout" or
	    die "failed to checkout $checkout";
    }

    if ($opt{$type} eq "bisect") {
	bisect $i;
	next;
    } elsif ($opt{$type} eq "patchcheck") {
	patchcheck $i;
	next;
    }

    if ($opt{$type} ne "nobuild") {
	build $opt{$type} or next;
    }

    get_grub_index;
    get_version;
    install;
    monitor or next;

    if (defined($run_test)) {
	do_run_test or next;
    }

    success $i;
}

if ($opt{"POWEROFF_ON_SUCCESS"}) {
    halt;
} elsif ($opt{"REBOOT_ON_SUCCESS"}) {
    reboot;
}

exit 0;
