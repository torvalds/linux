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
my $install_mods;
my $grub_number;
my $target;
my $make;
my $noclean;

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

sub doprint {
    print @_;

    if (defined($opt{"LOG_FILE"})) {
	open(OUT, ">> $opt{LOG_FILE}") or die "Can't write to $opt{LOG_FILE}";
	print OUT @_;
	close(OUT);
    }
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

    return $failed;
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
	dodie "failed - never got a boot prompt.\n";
    }

    if ($bug) {
	dodie "failed - got a bug report\n";
    }
}

sub install {

    if (run_command "scp $opt{OUTPUT_DIR}/$opt{BUILD_TARGET} $target:$opt{TARGET_IMAGE}") {
	dodie "failed to copy image";
    }

    if ($install_mods) {
	my $modlib = "/lib/modules/$version";
	my $modtar = "autotest-mods.tar.bz2";

	if (run_command "ssh $target rm -rf $modlib") {
	    dodie "failed to remove old mods: $modlib";
	}

	# would be nice if scp -r did not follow symbolic links
	if (run_command "cd $opt{TMP_DIR} && tar -cjf $modtar lib/modules/$version") {
	    dodie "making tarball";
	}

	if (run_command "scp $opt{TMP_DIR}/$modtar $target:/tmp") {
	    dodie "failed to copy modules";
	}

	unlink "$opt{TMP_DIR}/$modtar";

	if (run_command "ssh $target '(cd / && tar xf /tmp/$modtar)'") {
	    dodie "failed to tar modules";
	}

	run_command "ssh $target rm -f /tmp/$modtar";
    }

}

sub build {
    my ($type) = @_;
    my $defconfig = "";
    my $append = "";

    if ($type =~ /^useconfig:(.*)/) {
	if (run_command "cp $1 $opt{OUTPUT_DIR}/.config") {
	    dodie "could not copy $1 to .config";
	}
	$type = "oldconfig";
    }

    # old config can ask questions
    if ($type eq "oldconfig") {
	$append = "yes ''|";

	# allow for empty configs
	run_command "touch $opt{OUTPUT_DIR}/.config";

	if (run_command "mv $opt{OUTPUT_DIR}/.config $opt{OUTPUT_DIR}/config_temp") {
	    dodie "moving .config";
	}

	if (!$noclean && run_command "$make mrproper") {
	    dodie "make mrproper";
	}

	if (run_command "mv $opt{OUTPUT_DIR}/config_temp $opt{OUTPUT_DIR}/.config") {
	    dodie "moving config_temp";
	}

    } elsif (!$noclean) {
	unlink "$opt{OUTPUT_DIR}/.config";
	if (run_command "$make mrproper") {
	    dodie "make mrproper";
	}
    }

    # add something to distinguish this build
    open(OUT, "> $opt{OUTPUT_DIR}/localversion") or dodie("Can't make localversion file");
    print OUT "$opt{LOCALVERSION}\n";
    close(OUT);

    if (defined($opt{"MIN_CONFIG"})) {
	$defconfig = "KCONFIG_ALLCONFIG=$opt{MIN_CONFIG}";
    }

    if (run_command "$defconfig $append $make $type") {
	dodie "failed make config";
    }

    if (run_command "$make $opt{BUILD_OPTIONS}") {
	dodie "failed build";
    }
}

sub reboot {
    # try to reboot normally
    if (run_command "ssh $target reboot") {
	# nope? power cycle it.
	run_command "$opt{POWER_CYCLE}";
    }
}

sub halt {
    if ((run_command "ssh $target halt") or defined($opt{"POWER_OFF"})) {
	# nope? the zap it!
	run_command "$opt{POWER_OFF}";
    }
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

    if (!defined($opt{$type})) {
	$opt{$type} = $opt{"DEFAULT_BUILD_TYPE"};
    }

    doprint "\n\n";
    doprint "RUNNING TEST $i of $opt{NUM_BUILDS} with option $opt{$type}\n\n";

    if ($opt{$type} ne "nobuild") {
	build $opt{$type};
    }

    # get the release name
    doprint "$make kernelrelease ... ";
    $version = `$make kernelrelease | tail -1`;
    chomp($version);
    doprint "$version\n";

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

    if ($install_mods) {
	if (run_command "$make INSTALL_MOD_PATH=$opt{TMP_DIR} modules_install") {
	    dodie "Failed to install modules";
	}
    } else {
	doprint "No modules needed\n";
    }

    install;

    monitor;

    doprint "\n\n*******************************************\n";
    doprint     "*******************************************\n";
    doprint     "**            SUCCESS!!!!                **\n";
    doprint     "*******************************************\n";
    doprint     "*******************************************\n";

    if ($i != $opt{"NUM_BUILDS"}) {
	reboot;
	sleep "$opt{SLEEP_TIME}";
    }
}

if ($opt{"POWEROFF_ON_SUCCESS"}) {
    halt;
} else {
    reboot;
}

exit 0;
