#!/usr/bin/perl -w

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
$opt{"TIMEOUT"}			= 50;
$opt{"TMP_DIR"}			= "/tmp/autotest";
$opt{"SLEEP_TIME"}		= 60;	# sleep time between tests
$opt{"BUILD_NOCLEAN"}		= 0;
$opt{"REBOOT_ON_ERROR"}		= 0;
$opt{"POWEROFF_ON_ERROR"}	= 0;
$opt{"POWEROFF_ON_SUCCESS"}	= 0;
$opt{"BUILD_OPTIONS"}		= "";

my $version;
my $grub_number;
my $target;
my $make;
my $noclean;
my $minconfig;
my $in_bisect = 0;
my $bisect_bad = "";

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
    doprint "CRITICAL FAILURE... ", @_;

    if ($opt{"REBOOT_ON_ERROR"}) {
	doprint "REBOOTING\n";
	`$opt{"POWER_CYCLE"}`;

    } elsif ($opt{"POWEROFF_ON_ERROR"} && defined($opt{"POWER_OFF"})) {
	doprint "POWERING OFF\n";
	`$opt{"POWER_OFF"}`;
    }

    die @_;
}

sub run_command {
    my ($command) = @_;
    my $redirect = "";

    if (defined($opt{"LOG_FILE"})) {
	$redirect = " >> $opt{LOG_FILE} 2>&1";
    }

    doprint "$command ... ";
    `$command $redirect`;

    my $failed = $?;

    if ($failed) {
	doprint "FAILED!\n";
    } else {
	doprint "SUCCESS\n";
    }

    return !$failed;
}

sub get_grub_index {

    return if ($grub_number >= 0);

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

sub monitor {
    my $flags;
    my $booted = 0;
    my $bug = 0;
    my $pid;
    my $doopen2 = 0;
    my $skip_call_trace = 0;

    if ($doopen2) {
	$pid = open2(\*IN, \*OUT, $opt{"CONSOLE"});
	if ($pid < 0) {
	    dodie "Failed to connect to the console";
	}
    } else {
	$pid = open(IN, "$opt{CONSOLE} |");
    }

    $flags = fcntl(IN, F_GETFL, 0) or
	dodie "Can't get flags for the socket: $!\n";

    $flags = fcntl(IN, F_SETFL, $flags | O_NONBLOCK) or
	dodie "Can't set flags for the socket: $!\n";

    my $line;
    my $full_line = "";

    doprint "Wait for monitor to settle down.\n";
    # read the monitor and wait for the system to calm down
    do {
	$line = wait_for_input(\*IN, 5);
    } while (defined($line));

    reboot_to;

    for (;;) {

	$line = wait_for_input(\*IN);

	last if (!defined($line));

	doprint $line;

	# we are not guaranteed to get a full line
	$full_line .= $line;

	if ($full_line =~ /login:/) {
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

    doprint "kill child process $pid\n";
    kill 2, $pid;

    print "closing!\n";
    close(IN);

    if (!$booted) {
	return 1 if (!$in_bisect);
	dodie "failed - never got a boot prompt.\n";
    }

    if ($bug) {
	return 1 if (!$in_bisect);
	dodie "failed - got a bug report\n";
    }

    return 0;
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

    if (!run_command "$make $opt{BUILD_OPTIONS}") {
	# bisect may need this to pass
	return 1 if ($in_bisect);
	dodie "failed build";
    }

    return 0;
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

sub run_bisect {
    my ($type) = @_;

    my $failed;
    my $result;
    my $output;
    my $ret;


    if (defined($minconfig)) {
	$failed = build "useconfig:$minconfig";
    } else {
	# ?? no config to use?
	$failed = build "oldconfig";
    }

    if ($type ne "build") {
	dodie "Failed on build" if $failed;

	# Now boot the box
	get_grub_index;
	get_version;
	install;
	$failed = monitor;

	if ($type ne "boot") {
	    dodie "Failed on boot" if $failed;
	}
    }

    if ($failed) {
	$result = "bad";
    } else {
	$result = "good";
    }

    doprint "git bisect $result ... ";
    $output = `git bisect $result 2>&1`;
    $ret = $?;

    logit $output;

    if ($ret) {
	doprint "FAILED\n";
	dodie "Failed to git bisect";
    }

    doprint "SUCCESS\n";
    if ($output =~ m/^(Bisecting: .*\(roughly \d+ steps?\)) \[([[:xdigit:]]+)\]/) {
	doprint "$1 [$2]\n";
    } elsif ($output =~ m/^([[:xdigit:]]+) is the first bad commit/) {
	$bisect_bad = $1;
	doprint "Found bad commit... $1\n";
	return 0;
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

    $in_bisect = 1;

    run_command "git bisect start" or
	dodie "could not start bisect";

    run_command "git bisect good $good" or
	dodie "could not set bisect good to $good";

    run_command "git bisect bad $bad" or
	dodie "could not set bisect good to $bad";

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

doprint "\n\nSTARTING AUTOMATED TESTS\n";


$make = "$opt{MAKE_CMD} O=$opt{OUTPUT_DIR}";

# First we need to do is the builds
for (my $i = 1; $i <= $opt{"NUM_BUILDS"}; $i++) {
    my $type = "BUILD_TYPE[$i]";

    if (defined($opt{"BUILD_NOCLEAN[$i]"}) &&
	$opt{"BUILD_NOCLEAN[$i]"} != 0) {
	$noclean = 1;
    } else {
	$noclean = $opt{"BUILD_NOCLEAN"};
    }

    if (defined($opt{"MIN_CONFIG[$i]"})) {
	$minconfig = $opt{"MIN_CONFIG[$i]"};
    } elsif (defined($opt{"MIN_CONFIG"})) {
	$minconfig = $opt{"MIN_CONFIG"};
    } else {
	undef $minconfig;
    }

    if (!defined($opt{$type})) {
	$opt{$type} = $opt{"DEFAULT_BUILD_TYPE"};
    }

    doprint "\n\n";
    doprint "RUNNING TEST $i of $opt{NUM_BUILDS} with option $opt{$type}\n\n";

    if ($opt{$type} eq "bisect") {
	bisect $i;
	next;
    }

    if ($opt{$type} ne "nobuild") {
	build $opt{$type};
    }

    get_grub_index;
    get_version;
    install;
    monitor;
    success $i;
}

if ($opt{"POWEROFF_ON_SUCCESS"}) {
    halt;
} else {
    reboot;
}

exit 0;
