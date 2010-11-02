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

my $version;
my $install_mods;
my $grub_number;
my $target;
my $make;

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

sub reboot {
    run_command "ssh $target '(echo \"savedefault --default=$grub_number --once\" | grub --batch; reboot)'";
}

sub monitor {
    my $flags;
    my $booted = 0;
    my $bug = 0;
    my $pid;
    my $doopen2 = 0;

    if ($doopen2) {
	$pid = open2(\*IN, \*OUT, $opt{CONSOLE});
	if ($pid < 0) {
	    die "Failed to connect to the console";
	}
    } else {
	$pid = open(IN, "$opt{CONSOLE} |");
    }

    $flags = fcntl(IN, F_GETFL, 0) or
	die "Can't get flags for the socket: $!\n";

    $flags = fcntl(IN, F_SETFL, $flags | O_NONBLOCK) or
	die "Can't set flags for the socket: $!\n";

    my $line;
    my $full_line = "";

    doprint "Wait for monitor to settle down.\n";
    # read the monitor and wait for the system to calm down
    do {
	$line = wait_for_input(\*IN, 5);
    } while (defined($line));

    reboot;

    for (;;) {

	$line = wait_for_input(\*IN);

	last if (!defined($line));

	doprint $line;

	# we are not guaranteed to get a full line
	$full_line .= $line;

	if ($full_line =~ /login:/) {
	    $booted = 1;
	}

	if ($full_line =~ /call trace:/i) {
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
	die "failed - never got a boot prompt.\n";
    }

    if ($bug) {
	die "failed - got a bug report\n";
    }
}

sub install {

    if (run_command "scp $opt{OUTPUT_DIR}/$opt{BUILD_TARGET} $target:$opt{TARGET_IMAGE}") {
	die "failed to copy image";
    }

    if ($install_mods) {
	my $modlib = "/lib/modules/$version";

	if (run_command "ssh $target rm -rf $modlib") {
	    die "failed to remove old mods: $modlib";
	}

	if (run_command "scp -r $opt{TMP_DIR}/lib $target:/lib/modules/$version") {
	    die "failed to copy modules";
	}
    }

}

sub build {
    my ($type) = @_;

    unlink "$opt{OUTPUT_DIR}/.config";

    run_command "$make mrproper";

    # add something to distinguish this build
    open(OUT, "> $opt{OUTPUT_DIR}/localversion") or die("Can't make localversion file");
    print OUT "$opt{LOCALVERSION}\n";
    close(OUT);

    if (run_command "$make $opt{$type}") {
	die "failed make config";
    }

    if (defined($opt{"MIN_CONFIG"})) {
	run_command "cat $opt{MIN_CONFIG} >> $opt{OUTPUT_DIR}/.config";
	run_command "yes '' | $make oldconfig";
    }

    if (run_command "$make $opt{BUILD_OPTIONS}") {
	die "failed build";
    }
}

read_config $ARGV[0];

# mandatory configs
die "MACHINE not defined\n"		if (!defined($opt{"MACHINE"}));
die "SSH_USER not defined\n"		if (!defined($opt{"SSH_USER"}));
die "BUILD_DIR not defined\n"		if (!defined($opt{"BUILD_DIR"}));
die "OUTPUT_DIR not defined\n"		if (!defined($opt{"OUTPUT_DIR"}));
die "BUILD_TARGET not defined\n"	if (!defined($opt{"BUILD_TARGET"}));
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

    if (!defined($opt{$type})) {
	$opt{$type} = $opt{"DEFAULT_BUILD_TYPE"};
    }

    doprint "\n\n";
    doprint "RUNNING TEST $i of $opt{NUM_BUILDS} with option $opt{$type}\n\n";

    if ($opt{$type} ne "nobuild") {
	build $type;
    }

    # get the release name
    doprint "$make kernelrelease ... ";
    $version = `$make kernelrelease | tail -1`;
    chomp($version);
    doprint "$version\n";

    # should we process modules?
    $install_mods = 0;
    open(IN, "$opt{OUTPUT_DIR}/.config") or die("Can't read config file");
    while (<IN>) {
	if (/CONFIG_MODULES(=y)?/) {
	    $install_mods = 1 if (defined($1));
	    last;
	}
    }
    close(IN);

    if ($install_mods) {
	if (run_command "$make INSTALL_MOD_PATH=$opt{TMP_DIR} modules_install") {
	    die "Failed to install modules";
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

    # try to reboot normally

    if (run_command "ssh $target reboot") {
	# nope? power cycle it.
	run_command "$opt{POWER_CYCLE}";
    }

    sleep "$opt{SLEEP_TIME}";
}

exit 0;
