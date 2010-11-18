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

my $VERSION = "0.2";

$| = 1;

my %opt;
my %repeat_tests;
my %repeats;
my %default;

#default opts
$default{"NUM_TESTS"}		= 1;
$default{"REBOOT_TYPE"}		= "grub";
$default{"TEST_TYPE"}		= "test";
$default{"BUILD_TYPE"}		= "randconfig";
$default{"MAKE_CMD"}		= "make";
$default{"TIMEOUT"}		= 120;
$default{"TMP_DIR"}		= "/tmp/ktest";
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
$default{"SSH_EXEC"}		= "ssh \$SSH_USER\@\$MACHINE \$SSH_COMMAND";
$default{"SCP_TO_TARGET"}	= "scp \$SRC_FILE \$SSH_USER\@\$MACHINE:\$DST_FILE";
$default{"REBOOT"}		= "ssh \$SSH_USER\@\$MACHINE reboot";
$default{"STOP_AFTER_SUCCESS"}	= 10;
$default{"STOP_AFTER_FAILURE"}	= 60;
$default{"LOCALVERSION"}	= "-test";

my $ktest_config;
my $version;
my $machine;
my $ssh_user;
my $tmpdir;
my $builddir;
my $outputdir;
my $output_config;
my $test_type;
my $build_type;
my $build_options;
my $reboot_type;
my $reboot_script;
my $power_cycle;
my $reboot;
my $reboot_on_error;
my $poweroff_on_error;
my $die_on_failure;
my $powercycle_after_reboot;
my $poweroff_after_halt;
my $ssh_exec;
my $scp_to_target;
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
my $stop_after_success;
my $stop_after_failure;
my $build_target;
my $target_image;
my $localversion;
my $iteration = 0;
my $successes = 0;

my %entered_configs;
my %config_help;

$config_help{"MACHINE"} = << "EOF"
 The machine hostname that you will test.
EOF
    ;
$config_help{"SSH_USER"} = << "EOF"
 The box is expected to have ssh on normal bootup, provide the user
  (most likely root, since you need privileged operations)
EOF
    ;
$config_help{"BUILD_DIR"} = << "EOF"
 The directory that contains the Linux source code (full path).
EOF
    ;
$config_help{"OUTPUT_DIR"} = << "EOF"
 The directory that the objects will be built (full path).
 (can not be same as BUILD_DIR)
EOF
    ;
$config_help{"BUILD_TARGET"} = << "EOF"
 The location of the compiled file to copy to the target.
 (relative to OUTPUT_DIR)
EOF
    ;
$config_help{"TARGET_IMAGE"} = << "EOF"
 The place to put your image on the test machine.
EOF
    ;
$config_help{"POWER_CYCLE"} = << "EOF"
 A script or command to reboot the box.

 Here is a digital loggers power switch example
 POWER_CYCLE = wget --no-proxy -O /dev/null -q  --auth-no-challenge 'http://admin:admin\@power/outlet?5=CCL'

 Here is an example to reboot a virtual box on the current host
 with the name "Guest".
 POWER_CYCLE = virsh destroy Guest; sleep 5; virsh start Guest
EOF
    ;
$config_help{"CONSOLE"} = << "EOF"
 The script or command that reads the console

  If you use ttywatch server, something like the following would work.
CONSOLE = nc -d localhost 3001

 For a virtual machine with guest name "Guest".
CONSOLE =  virsh console Guest
EOF
    ;
$config_help{"LOCALVERSION"} = << "EOF"
 Required version ending to differentiate the test
 from other linux builds on the system.
EOF
    ;
$config_help{"REBOOT_TYPE"} = << "EOF"
 Way to reboot the box to the test kernel.
 Only valid options so far are "grub" and "script".

 If you specify grub, it will assume grub version 1
 and will search in /boot/grub/menu.lst for the title \$GRUB_MENU
 and select that target to reboot to the kernel. If this is not
 your setup, then specify "script" and have a command or script
 specified in REBOOT_SCRIPT to boot to the target.

 The entry in /boot/grub/menu.lst must be entered in manually.
 The test will not modify that file.
EOF
    ;
$config_help{"GRUB_MENU"} = << "EOF"
 The grub title name for the test kernel to boot
 (Only mandatory if REBOOT_TYPE = grub)

 Note, ktest.pl will not update the grub menu.lst, you need to
 manually add an option for the test. ktest.pl will search
 the grub menu.lst for this option to find what kernel to
 reboot into.

 For example, if in the /boot/grub/menu.lst the test kernel title has:
 title Test Kernel
 kernel vmlinuz-test
 GRUB_MENU = Test Kernel
EOF
    ;
$config_help{"REBOOT_SCRIPT"} = << "EOF"
 A script to reboot the target into the test kernel
 (Only mandatory if REBOOT_TYPE = script)
EOF
    ;


sub get_ktest_config {
    my ($config) = @_;

    return if (defined($opt{$config}));

    if (defined($config_help{$config})) {
	print "\n";
	print $config_help{$config};
    }

    for (;;) {
	print "$config = ";
	if (defined($default{$config})) {
	    print "\[$default{$config}\] ";
	}
	$entered_configs{$config} = <STDIN>;
	$entered_configs{$config} =~ s/^\s*(.*\S)\s*$/$1/;
	if ($entered_configs{$config} =~ /^\s*$/) {
	    if ($default{$config}) {
		$entered_configs{$config} = $default{$config};
	    } else {
		print "Your answer can not be blank\n";
		next;
	    }
	}
	last;
    }
}

sub get_ktest_configs {
    get_ktest_config("MACHINE");
    get_ktest_config("SSH_USER");
    get_ktest_config("BUILD_DIR");
    get_ktest_config("OUTPUT_DIR");
    get_ktest_config("BUILD_TARGET");
    get_ktest_config("TARGET_IMAGE");
    get_ktest_config("POWER_CYCLE");
    get_ktest_config("CONSOLE");
    get_ktest_config("LOCALVERSION");

    my $rtype = $opt{"REBOOT_TYPE"};

    if (!defined($rtype)) {
	if (!defined($opt{"GRUB_MENU"})) {
	    get_ktest_config("REBOOT_TYPE");
	    $rtype = $entered_configs{"REBOOT_TYPE"};
	} else {
	    $rtype = "grub";
	}
    }

    if ($rtype eq "grub") {
	get_ktest_config("GRUB_MENU");
    } else {
	get_ktest_config("REBOOT_SCRIPT");
    }
}

sub set_value {
    my ($lvalue, $rvalue) = @_;

    if (defined($opt{$lvalue})) {
	die "Error: Option $lvalue defined more than once!\n";
    }
    if ($rvalue =~ /^\s*$/) {
	delete $opt{$lvalue};
    } else {
	$opt{$lvalue} = $rvalue;
    }
}

sub read_config {
    my ($config) = @_;

    open(IN, $config) || die "can't read file $config";

    my $name = $config;
    $name =~ s,.*/(.*),$1,;

    my $test_num = 0;
    my $default = 1;
    my $repeat = 1;
    my $num_tests_set = 0;
    my $skip = 0;
    my $rest;

    while (<IN>) {

	# ignore blank lines and comments
	next if (/^\s*$/ || /\s*\#/);

	if (/^\s*TEST_START(.*)/) {

	    $rest = $1;

	    if ($num_tests_set) {
		die "$name: $.: Can not specify both NUM_TESTS and TEST_START\n";
	    }

	    my $old_test_num = $test_num;
	    my $old_repeat = $repeat;

	    $test_num += $repeat;
	    $default = 0;
	    $repeat = 1;

	    if ($rest =~ /\s+SKIP(.*)/) {
		$rest = $1;
		$skip = 1;
	    } else {
		$skip = 0;
	    }

	    if ($rest =~ /\s+ITERATE\s+(\d+)(.*)$/) {
		$repeat = $1;
		$rest = $2;
		$repeat_tests{"$test_num"} = $repeat;
	    }

	    if ($rest =~ /\s+SKIP(.*)/) {
		$rest = $1;
		$skip = 1;
	    }

	    if ($rest !~ /^\s*$/) {
		die "$name: $.: Gargbage found after TEST_START\n$_";
	    }

	    if ($skip) {
		$test_num = $old_test_num;
		$repeat = $old_repeat;
	    }

	} elsif (/^\s*DEFAULTS(.*)$/) {
	    $default = 1;

	    $rest = $1;

	    if ($rest =~ /\s+SKIP(.*)/) {
		$rest = $1;
		$skip = 1;
	    } else {
		$skip = 0;
	    }

	    if ($rest !~ /^\s*$/) {
		die "$name: $.: Gargbage found after DEFAULTS\n$_";
	    }

	} elsif (/^\s*([A-Z_\[\]\d]+)\s*=\s*(.*?)\s*$/) {

	    next if ($skip);

	    my $lvalue = $1;
	    my $rvalue = $2;

	    if (!$default &&
		($lvalue eq "NUM_TESTS" ||
		 $lvalue eq "LOG_FILE" ||
		 $lvalue eq "CLEAR_LOG")) {
		die "$name: $.: $lvalue must be set in DEFAULTS section\n";
	    }

	    if ($lvalue eq "NUM_TESTS") {
		if ($test_num) {
		    die "$name: $.: Can not specify both NUM_TESTS and TEST_START\n";
		}
		if (!$default) {
		    die "$name: $.: NUM_TESTS must be set in default section\n";
		}
		$num_tests_set = 1;
	    }

	    if ($default || $lvalue =~ /\[\d+\]$/) {
		set_value($lvalue, $rvalue);
	    } else {
		my $val = "$lvalue\[$test_num\]";
		set_value($val, $rvalue);

		if ($repeat > 1) {
		    $repeats{$val} = $repeat;
		}
	    }
	} else {
	    die "$name: $.: Garbage found in config\n$_";
	}
    }

    close(IN);

    if ($test_num) {
	$test_num += $repeat - 1;
	$opt{"NUM_TESTS"} = $test_num;
    }

    # make sure we have all mandatory configs
    get_ktest_configs;

    # set any defaults

    foreach my $default (keys %default) {
	if (!defined($opt{$default})) {
	    $opt{$default} = $default{$default};
	}
    }
}

sub _logit {
    if (defined($opt{"LOG_FILE"})) {
	open(OUT, ">> $opt{LOG_FILE}") or die "Can't write to $opt{LOG_FILE}";
	print OUT @_;
	close(OUT);
    }
}

sub logit {
    if (defined($opt{"LOG_FILE"})) {
	_logit @_;
    } else {
	print @_;
    }
}

sub doprint {
    print @_;
    _logit @_;
}

sub run_command;

sub reboot {
    # try to reboot normally
    if (run_command $reboot) {
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
	doprint "KTEST RESULT: TEST $i Failed: ", @_, "\n";
	doprint "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";
	doprint "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";

	return 1 if (!defined($store_failures));

	my @t = localtime;
	my $date = sprintf "%04d%02d%02d%02d%02d%02d",
		1900+$t[5],$t[4],$t[3],$t[2],$t[1],$t[0];

	my $type = $build_type;
	if ($type =~ /useconfig/) {
	    $type = "useconfig";
	}

	my $dir = "$machine-$test_type-$type-fail-$date";
	my $faildir = "$store_failures/$dir";

	if (!-d $faildir) {
	    mkpath($faildir) or
		die "can't create $faildir";
	}
	if (-f "$output_config") {
	    cp "$output_config", "$faildir/config" or
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

    $command =~ s/\$SSH_USER/$ssh_user/g;
    $command =~ s/\$MACHINE/$machine/g;

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

sub run_ssh {
    my ($cmd) = @_;
    my $cp_exec = $ssh_exec;

    $cp_exec =~ s/\$SSH_COMMAND/$cmd/g;
    return run_command "$cp_exec";
}

sub run_scp {
    my ($src, $dst) = @_;
    my $cp_scp = $scp_to_target;

    $cp_scp =~ s/\$SRC_FILE/$src/g;
    $cp_scp =~ s/\$DST_FILE/$dst/g;

    return run_command "$cp_scp";
}

sub get_grub_index {

    if ($reboot_type ne "grub") {
	return;
    }
    return if (defined($grub_number));

    doprint "Find grub menu ... ";
    $grub_number = -1;

    my $ssh_grub = $ssh_exec;
    $ssh_grub =~ s,\$SSH_COMMAND,cat /boot/grub/menu.lst,g;

    open(IN, "$ssh_grub |")
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
	run_ssh "'(echo \"savedefault --default=$grub_number --once\" | grub --batch; reboot)'";
	return;
    }

    run_command "$reboot_script";
}

sub get_sha1 {
    my ($commit) = @_;

    doprint "git rev-list --max-count=1 $commit ... ";
    my $sha1 = `git rev-list --max-count=1 $commit`;
    my $ret = $?;

    logit $sha1;

    if ($ret) {
	doprint "FAILED\n";
	dodie "Failed to get git $commit";
    }

    print "SUCCESS\n";

    chomp $sha1;

    return $sha1;
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

    my $success_start;
    my $failure_start;

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
	    $success_start = time;
	}

	if ($booted && defined($stop_after_success) &&
	    $stop_after_success >= 0) {
	    my $now = time;
	    if ($now - $success_start >= $stop_after_success) {
		doprint "Test forced to stop after $stop_after_success seconds after success\n";
		last;
	    }
	}

	if ($full_line =~ /\[ backtrace testing \]/) {
	    $skip_call_trace = 1;
	}

	if ($full_line =~ /call trace:/i) {
	    if (!$skip_call_trace) {
		$bug = 1;
		$failure_start = time;
	    }
	}

	if ($bug && defined($stop_after_failure) &&
	    $stop_after_failure >= 0) {
	    my $now = time;
	    if ($now - $failure_start >= $stop_after_failure) {
		doprint "Test forced to stop after $stop_after_failure seconds after failure\n";
		last;
	    }
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

    run_scp "$outputdir/$build_target", "$target_image" or
	dodie "failed to copy image";

    my $install_mods = 0;

    # should we process modules?
    $install_mods = 0;
    open(IN, "$output_config") or dodie("Can't read config file");
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
    my $modtar = "ktest-mods.tar.bz2";

    run_ssh "rm -rf $modlib" or
	dodie "failed to remove old mods: $modlib";

    # would be nice if scp -r did not follow symbolic links
    run_command "cd $tmpdir && tar -cjf $modtar lib/modules/$version" or
	dodie "making tarball";

    run_scp "$tmpdir/$modtar", "/tmp" or
	dodie "failed to copy modules";

    unlink "$tmpdir/$modtar";

    run_ssh "'(cd / && tar xf /tmp/$modtar)'" or
	dodie "failed to tar modules";

    run_ssh "rm -f /tmp/$modtar";

    return if (!defined($post_install));

    my $cp_post_install = $post_install;
    $cp_post_install = s/\$KERNEL_VERSION/$version/g;
    run_command "$cp_post_install" or
	dodie "Failed to run post install";
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

    unlink $buildlog;

    if ($type =~ /^useconfig:(.*)/) {
	run_command "cp $1 $output_config" or
	    dodie "could not copy $1 to .config";

	$type = "oldconfig";
    }

    # old config can ask questions
    if ($type eq "oldconfig") {
	$type = "oldnoconfig";

	# allow for empty configs
	run_command "touch $output_config";

	run_command "mv $output_config $outputdir/config_temp" or
	    dodie "moving .config";

	if (!$noclean && !run_command "$make mrproper") {
	    dodie "make mrproper";
	}

	run_command "mv $outputdir/config_temp $output_config" or
	    dodie "moving config_temp";

    } elsif (!$noclean) {
	unlink "$output_config";
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

    run_command "$defconfig $make $type" or
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
    if (!run_ssh "halt" or defined($power_off)) {
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

    $successes++;

    doprint "\n\n*******************************************\n";
    doprint     "*******************************************\n";
    doprint     "KTEST RESULT: TEST $i SUCCESS!!!!         **\n";
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

# returns 1 on success, 0 on failure
sub run_bisect_test {
    my ($type, $buildtype) = @_;

    my $failed = 0;
    my $result;
    my $output;
    my $ret;

    $in_bisect = 1;

    build $buildtype or $failed = 1;

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
	$result = 0;

	# reboot the box to a good kernel
	if ($type ne "build") {
	    doprint "Reboot and sleep $bisect_sleep_time seconds\n";
	    reboot;
	    start_monitor;
	    wait_for_monitor $bisect_sleep_time;
	    end_monitor;
	}
    } else {
	$result = 1;
    }
    $in_bisect = 0;

    return $result;
}

sub run_bisect {
    my ($type) = @_;
    my $buildtype = "oldconfig";

    # We should have a minconfig to use?
    if (defined($minconfig)) {
	$buildtype = "useconfig:$minconfig";
    }

    my $ret = run_bisect_test $type, $buildtype;


    # Are we looking for where it worked, not failed?
    if ($reverse_bisect) {
	$ret = !$ret;
    }

    if ($ret) {
	return "good";
    } else {
	return  "bad";
    }
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

    # convert to true sha1's
    $good = get_sha1($good);
    $bad = get_sha1($bad);

    if (defined($opt{"BISECT_REVERSE[$i]"}) &&
	$opt{"BISECT_REVERSE[$i]"} == 1) {
	doprint "Performing a reverse bisect (bad is good, good is bad!)\n";
	$reverse_bisect = 1;
    } else {
	$reverse_bisect = 0;
    }

    # Can't have a test without having a test to run
    if ($type eq "test" && !defined($run_test)) {
	$type = "boot";
    }

    my $check = $opt{"BISECT_CHECK[$i]"};
    if (defined($check) && $check ne "0") {

	# get current HEAD
	my $head = get_sha1("HEAD");

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

    success $i;
}

my %config_ignore;
my %config_set;

my %config_list;
my %null_config;

my %dependency;

sub process_config_ignore {
    my ($config) = @_;

    open (IN, $config)
	or dodie "Failed to read $config";

    while (<IN>) {
	if (/^(.*?(CONFIG\S*)(=.*| is not set))/) {
	    $config_ignore{$2} = $1;
	}
    }

    close(IN);
}

sub read_current_config {
    my ($config_ref) = @_;

    %{$config_ref} = ();
    undef %{$config_ref};

    my @key = keys %{$config_ref};
    if ($#key >= 0) {
	print "did not delete!\n";
	exit;
    }
    open (IN, "$output_config");

    while (<IN>) {
	if (/^(CONFIG\S+)=(.*)/) {
	    ${$config_ref}{$1} = $2;
	}
    }
    close(IN);
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

sub create_config {
    my @configs = @_;

    open(OUT, ">$output_config") or dodie "Can not write to $output_config";

    foreach my $config (@configs) {
	print OUT "$config_set{$config}\n";
	my @deps = get_dependencies $config;
	foreach my $dep (@deps) {
	    print OUT "$config_set{$dep}\n";
	}
    }

    foreach my $config (keys %config_ignore) {
	print OUT "$config_ignore{$config}\n";
    }
    close(OUT);

#    exit;
    run_command "$make oldnoconfig" or
	dodie "failed make config oldconfig";

}

sub compare_configs {
    my (%a, %b) = @_;

    foreach my $item (keys %a) {
	if (!defined($b{$item})) {
	    print "diff $item\n";
	    return 1;
	}
	delete $b{$item};
    }

    my @keys = keys %b;
    if ($#keys) {
	print "diff2 $keys[0]\n";
    }
    return -1 if ($#keys >= 0);

    return 0;
}

sub run_config_bisect_test {
    my ($type) = @_;

    return run_bisect_test $type, "oldconfig";
}

sub process_passed {
    my (%configs) = @_;

    doprint "These configs had no failure: (Enabling them for further compiles)\n";
    # Passed! All these configs are part of a good compile.
    # Add them to the min options.
    foreach my $config (keys %configs) {
	if (defined($config_list{$config})) {
	    doprint " removing $config\n";
	    $config_ignore{$config} = $config_list{$config};
	    delete $config_list{$config};
	}
    }
    doprint "config copied to $outputdir/config_good\n";
    run_command "cp -f $output_config $outputdir/config_good";
}

sub process_failed {
    my ($config) = @_;

    doprint "\n\n***************************************\n";
    doprint "Found bad config: $config\n";
    doprint "***************************************\n\n";
}

sub run_config_bisect {

    my @start_list = keys %config_list;

    if ($#start_list < 0) {
	doprint "No more configs to test!!!\n";
	return -1;
    }

    doprint "***** RUN TEST ***\n";
    my $type = $opt{"CONFIG_BISECT_TYPE[$iteration]"};
    my $ret;
    my %current_config;

    my $count = $#start_list + 1;
    doprint "  $count configs to test\n";

    my $half = int($#start_list / 2);

    do {
	my @tophalf = @start_list[0 .. $half];

	create_config @tophalf;
	read_current_config \%current_config;

	$count = $#tophalf + 1;
	doprint "Testing $count configs\n";
	my $found = 0;
	# make sure we test something
	foreach my $config (@tophalf) {
	    if (defined($current_config{$config})) {
		logit " $config\n";
		$found = 1;
	    }
	}
	if (!$found) {
	    # try the other half
	    doprint "Top half produced no set configs, trying bottom half\n";
	    @tophalf = @start_list[$half .. $#start_list];
	    create_config @tophalf;
	    read_current_config \%current_config;
	    foreach my $config (@tophalf) {
		if (defined($current_config{$config})) {
		    logit " $config\n";
		    $found = 1;
		}
	    }
	    if (!$found) {
		doprint "Failed: Can't make new config with current configs\n";
		foreach my $config (@start_list) {
		    doprint "  CONFIG: $config\n";
		}
		return -1;
	    }
	    $count = $#tophalf + 1;
	    doprint "Testing $count configs\n";
	}

	$ret = run_config_bisect_test $type;

	if ($ret) {
	    process_passed %current_config;
	    return 0;
	}

	doprint "This config had a failure.\n";
	doprint "Removing these configs that were not set in this config:\n";
	doprint "config copied to $outputdir/config_bad\n";
	run_command "cp -f $output_config $outputdir/config_bad";

	# A config exists in this group that was bad.
	foreach my $config (keys %config_list) {
	    if (!defined($current_config{$config})) {
		doprint " removing $config\n";
		delete $config_list{$config};
	    }
	}

	@start_list = @tophalf;

	if ($#start_list == 0) {
	    process_failed $start_list[0];
	    return 1;
	}

	# remove half the configs we are looking at and see if
	# they are good.
	$half = int($#start_list / 2);
    } while ($half > 0);

    # we found a single config, try it again
    my @tophalf = @start_list[0 .. 0];

    $ret = run_config_bisect_test $type;
    if ($ret) {
	process_passed %current_config;
	return 0;
    }

    process_failed $start_list[0];
    return 1;
}

sub config_bisect {
    my ($i) = @_;

    my $start_config = $opt{"CONFIG_BISECT[$i]"};

    my $tmpconfig = "$tmpdir/use_config";

    # Make the file with the bad config and the min config
    if (defined($minconfig)) {
	# read the min config for things to ignore
	run_command "cp $minconfig $tmpconfig" or
	    dodie "failed to copy $minconfig to $tmpconfig";
    } else {
	unlink $tmpconfig;
    }

    # Add other configs
    if (defined($addconfig)) {
	run_command "cat $addconfig >> $tmpconfig" or
	    dodie "failed to append $addconfig";
    }

    my $defconfig = "";
    if (-f $tmpconfig) {
	$defconfig = "KCONFIG_ALLCONFIG=$tmpconfig";
	process_config_ignore $tmpconfig;
    }

    # now process the start config
    run_command "cp $start_config $output_config" or
	dodie "failed to copy $start_config to $output_config";

    # read directly what we want to check
    my %config_check;
    open (IN, $output_config)
	or dodie "faied to open $output_config";

    while (<IN>) {
	if (/^((CONFIG\S*)=.*)/) {
	    $config_check{$2} = $1;
	}
    }
    close(IN);

    # Now run oldconfig with the minconfig (and addconfigs)
    run_command "$defconfig $make oldnoconfig" or
	dodie "failed make config oldconfig";

    # check to see what we lost (or gained)
    open (IN, $output_config)
	or dodie "Failed to read $start_config";

    my %removed_configs;
    my %added_configs;

    while (<IN>) {
	if (/^((CONFIG\S*)=.*)/) {
	    # save off all options
	    $config_set{$2} = $1;
	    if (defined($config_check{$2})) {
		if (defined($config_ignore{$2})) {
		    $removed_configs{$2} = $1;
		} else {
		    $config_list{$2} = $1;
		}
	    } elsif (!defined($config_ignore{$2})) {
		$added_configs{$2} = $1;
		$config_list{$2} = $1;
	    }
	}
    }
    close(IN);

    my @confs = keys %removed_configs;
    if ($#confs >= 0) {
	doprint "Configs overridden by default configs and removed from check:\n";
	foreach my $config (@confs) {
	    doprint " $config\n";
	}
    }
    @confs = keys %added_configs;
    if ($#confs >= 0) {
	doprint "Configs appearing in make oldconfig and added:\n";
	foreach my $config (@confs) {
	    doprint " $config\n";
	}
    }

    my %config_test;
    my $once = 0;

    # Sometimes kconfig does weird things. We must make sure
    # that the config we autocreate has everything we need
    # to test, otherwise we may miss testing configs, or
    # may not be able to create a new config.
    # Here we create a config with everything set.
    create_config (keys %config_list);
    read_current_config \%config_test;
    foreach my $config (keys %config_list) {
	if (!defined($config_test{$config})) {
	    if (!$once) {
		$once = 1;
		doprint "Configs not produced by kconfig (will not be checked):\n";
	    }
	    doprint "  $config\n";
	    delete $config_list{$config};
	}
    }
    my $ret;
    do {
	$ret = run_config_bisect;
    } while (!$ret);

    return $ret if ($ret < 0);

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

    # Get the true sha1's since we can use things like HEAD~3
    $start = get_sha1($start);
    $end = get_sha1($end);

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

$#ARGV < 1 or die "ktest.pl version: $VERSION\n   usage: ktest.pl config-file\n";

if ($#ARGV == 0) {
    $ktest_config = $ARGV[0];
    if (! -f $ktest_config) {
	print "$ktest_config does not exist.\n";
	my $ans;
        for (;;) {
	    print "Create it? [Y/n] ";
	    $ans = <STDIN>;
	    chomp $ans;
	    if ($ans =~ /^\s*$/) {
		$ans = "y";
	    }
	    last if ($ans =~ /^y$/i || $ans =~ /^n$/i);
	    print "Please answer either 'y' or 'n'.\n";
	}
	if ($ans !~ /^y$/i) {
	    exit 0;
	}
    }
} else {
    $ktest_config = "ktest.conf";
}

if (! -f $ktest_config) {
    open(OUT, ">$ktest_config") or die "Can not create $ktest_config";
    print OUT << "EOF"
# Generated by ktest.pl
#
# Define each test with TEST_START
# The config options below it will override the defaults
TEST_START

DEFAULTS
EOF
;
    close(OUT);
}
read_config $ktest_config;

# Append any configs entered in manually to the config file.
my @new_configs = keys %entered_configs;
if ($#new_configs >= 0) {
    print "\nAppending entered in configs to $ktest_config\n";
    open(OUT, ">>$ktest_config") or die "Can not append to $ktest_config";
    foreach my $config (@new_configs) {
	print OUT "$config = $entered_configs{$config}\n";
	$opt{$config} = $entered_configs{$config};
    }
}

if ($opt{"CLEAR_LOG"} && defined($opt{"LOG_FILE"})) {
    unlink $opt{"LOG_FILE"};
}

doprint "\n\nSTARTING AUTOMATED TESTS\n\n";

for (my $i = 0, my $repeat = 1; $i <= $opt{"NUM_TESTS"}; $i += $repeat) {

    if (!$i) {
	doprint "DEFAULT OPTIONS:\n";
    } else {
	doprint "\nTEST $i OPTIONS";
	if (defined($repeat_tests{$i})) {
	    $repeat = $repeat_tests{$i};
	    doprint " ITERATE $repeat";
	}
	doprint "\n";
    }

    foreach my $option (sort keys %opt) {

	if ($option =~ /\[(\d+)\]$/) {
	    next if ($i != $1);
	} else {
	    next if ($i);
	}

	doprint "$option = $opt{$option}\n";
    }
}

sub set_test_option {
    my ($name, $i) = @_;

    my $option = "$name\[$i\]";

    if (defined($opt{$option})) {
	return $opt{$option};
    }

    foreach my $test (keys %repeat_tests) {
	if ($i >= $test &&
	    $i < $test + $repeat_tests{$test}) {
	    $option = "$name\[$test\]";
	    if (defined($opt{$option})) {
		return $opt{$option};
	    }
	}
    }

    if (defined($opt{$name})) {
	return $opt{$name};
    }

    return undef;
}

# First we need to do is the builds
for (my $i = 1; $i <= $opt{"NUM_TESTS"}; $i++) {

    $iteration = $i;

    my $makecmd = set_test_option("MAKE_CMD", $i);

    $machine = set_test_option("MACHINE", $i);
    $ssh_user = set_test_option("SSH_USER", $i);
    $tmpdir = set_test_option("TMP_DIR", $i);
    $outputdir = set_test_option("OUTPUT_DIR", $i);
    $builddir = set_test_option("BUILD_DIR", $i);
    $test_type = set_test_option("TEST_TYPE", $i);
    $build_type = set_test_option("BUILD_TYPE", $i);
    $build_options = set_test_option("BUILD_OPTIONS", $i);
    $power_cycle = set_test_option("POWER_CYCLE", $i);
    $reboot = set_test_option("REBOOT", $i);
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
    $stop_after_success = set_test_option("STOP_AFTER_SUCCESS", $i);
    $stop_after_failure = set_test_option("STOP_AFTER_FAILURE", $i);
    $build_target = set_test_option("BUILD_TARGET", $i);
    $ssh_exec = set_test_option("SSH_EXEC", $i);
    $scp_to_target = set_test_option("SCP_TO_TARGET", $i);
    $target_image = set_test_option("TARGET_IMAGE", $i);
    $localversion = set_test_option("LOCALVERSION", $i);

    chdir $builddir || die "can't change directory to $builddir";

    if (!-d $tmpdir) {
	mkpath($tmpdir) or
	    die "can't create $tmpdir";
    }

    $ENV{"SSH_USER"} = $ssh_user;
    $ENV{"MACHINE"} = $machine;

    $target = "$ssh_user\@$machine";

    $buildlog = "$tmpdir/buildlog-$machine";
    $dmesg = "$tmpdir/dmesg-$machine";
    $make = "$makecmd O=$outputdir";
    $output_config = "$outputdir/.config";

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
    } elsif ($test_type eq "config_bisect") {
	$run_type = $opt{"CONFIG_BISECT_TYPE[$i]"};
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
	run_command "cat $addconfig $minconfig > $tmpdir/add_config" or
	    dodie "Failed to create temp config";
	$minconfig = "$tmpdir/add_config";
    }

    my $checkout = $opt{"CHECKOUT[$i]"};
    if (defined($checkout)) {
	run_command "git checkout $checkout" or
	    die "failed to checkout $checkout";
    }

    if ($test_type eq "bisect") {
	bisect $i;
	next;
    } elsif ($test_type eq "config_bisect") {
	config_bisect $i;
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

doprint "\n    $successes of $opt{NUM_TESTS} tests were successful\n\n";

exit 0;
