#!/usr/bin/perl -w
#
# Copyright 2010 - Steven Rostedt <srostedt@redhat.com>, Red Hat Inc.
# Licensed under the terms of the GNU GPL License version 2
#

use strict;
use IPC::Open2;
use Fcntl qw(F_GETFL F_SETFL O_NONBLOCK);
use File::Path qw(mkpath);
use File::Copy qw(cp);
use FileHandle;
use FindBin;

my $VERSION = "0.2";

$| = 1;

my %opt;
my %repeat_tests;
my %repeats;
my %evals;

#default opts
my %default = (
    "MAILER"			=> "sendmail",  # default mailer
    "EMAIL_ON_ERROR"		=> 1,
    "EMAIL_WHEN_FINISHED"	=> 1,
    "EMAIL_WHEN_CANCELED"	=> 0,
    "EMAIL_WHEN_STARTED"	=> 0,
    "NUM_TESTS"			=> 1,
    "TEST_TYPE"			=> "build",
    "BUILD_TYPE"		=> "randconfig",
    "MAKE_CMD"			=> "make",
    "CLOSE_CONSOLE_SIGNAL"	=> "INT",
    "TIMEOUT"			=> 120,
    "TMP_DIR"			=> "/tmp/ktest/\${MACHINE}",
    "SLEEP_TIME"		=> 60,	# sleep time between tests
    "BUILD_NOCLEAN"		=> 0,
    "REBOOT_ON_ERROR"		=> 0,
    "POWEROFF_ON_ERROR"		=> 0,
    "REBOOT_ON_SUCCESS"		=> 1,
    "POWEROFF_ON_SUCCESS"	=> 0,
    "BUILD_OPTIONS"		=> "",
    "BISECT_SLEEP_TIME"		=> 60,   # sleep time between bisects
    "PATCHCHECK_SLEEP_TIME"	=> 60, # sleep time between patch checks
    "CLEAR_LOG"			=> 0,
    "BISECT_MANUAL"		=> 0,
    "BISECT_SKIP"		=> 1,
    "BISECT_TRIES"		=> 1,
    "MIN_CONFIG_TYPE"		=> "boot",
    "SUCCESS_LINE"		=> "login:",
    "DETECT_TRIPLE_FAULT"	=> 1,
    "NO_INSTALL"		=> 0,
    "BOOTED_TIMEOUT"		=> 1,
    "DIE_ON_FAILURE"		=> 1,
    "SSH_EXEC"			=> "ssh \$SSH_USER\@\$MACHINE \$SSH_COMMAND",
    "SCP_TO_TARGET"		=> "scp \$SRC_FILE \$SSH_USER\@\$MACHINE:\$DST_FILE",
    "SCP_TO_TARGET_INSTALL"	=> "\${SCP_TO_TARGET}",
    "REBOOT"			=> "ssh \$SSH_USER\@\$MACHINE reboot",
    "STOP_AFTER_SUCCESS"	=> 10,
    "STOP_AFTER_FAILURE"	=> 60,
    "STOP_TEST_AFTER"		=> 600,
    "MAX_MONITOR_WAIT"		=> 1800,
    "GRUB_REBOOT"		=> "grub2-reboot",
    "SYSLINUX"			=> "extlinux",
    "SYSLINUX_PATH"		=> "/boot/extlinux",
    "CONNECT_TIMEOUT"		=> 25,

# required, and we will ask users if they don't have them but we keep the default
# value something that is common.
    "REBOOT_TYPE"		=> "grub",
    "LOCALVERSION"		=> "-test",
    "SSH_USER"			=> "root",
    "BUILD_TARGET"	 	=> "arch/x86/boot/bzImage",
    "TARGET_IMAGE"		=> "/boot/vmlinuz-test",

    "LOG_FILE"			=> undef,
    "IGNORE_UNUSED"		=> 0,
);

my $ktest_config = "ktest.conf";
my $version;
my $have_version = 0;
my $machine;
my $last_machine;
my $ssh_user;
my $tmpdir;
my $builddir;
my $outputdir;
my $output_config;
my $test_type;
my $build_type;
my $build_options;
my $final_post_ktest;
my $pre_ktest;
my $post_ktest;
my $pre_test;
my $post_test;
my $pre_build;
my $post_build;
my $pre_build_die;
my $post_build_die;
my $reboot_type;
my $reboot_script;
my $power_cycle;
my $reboot;
my $reboot_on_error;
my $switch_to_good;
my $switch_to_test;
my $poweroff_on_error;
my $reboot_on_success;
my $die_on_failure;
my $powercycle_after_reboot;
my $poweroff_after_halt;
my $max_monitor_wait;
my $ssh_exec;
my $scp_to_target;
my $scp_to_target_install;
my $power_off;
my $grub_menu;
my $last_grub_menu;
my $grub_file;
my $grub_number;
my $grub_reboot;
my $syslinux;
my $syslinux_path;
my $syslinux_label;
my $target;
my $make;
my $pre_install;
my $post_install;
my $no_install;
my $noclean;
my $minconfig;
my $start_minconfig;
my $start_minconfig_defined;
my $output_minconfig;
my $minconfig_type;
my $use_output_minconfig;
my $warnings_file;
my $ignore_config;
my $ignore_errors;
my $addconfig;
my $in_bisect = 0;
my $bisect_bad_commit = "";
my $reverse_bisect;
my $bisect_manual;
my $bisect_skip;
my $bisect_tries;
my $config_bisect_good;
my $bisect_ret_good;
my $bisect_ret_bad;
my $bisect_ret_skip;
my $bisect_ret_abort;
my $bisect_ret_default;
my $in_patchcheck = 0;
my $run_test;
my $buildlog;
my $testlog;
my $dmesg;
my $monitor_fp;
my $monitor_pid;
my $monitor_cnt = 0;
my $sleep_time;
my $bisect_sleep_time;
my $patchcheck_sleep_time;
my $ignore_warnings;
my $store_failures;
my $store_successes;
my $test_name;
my $timeout;
my $connect_timeout;
my $config_bisect_exec;
my $booted_timeout;
my $detect_triplefault;
my $console;
my $close_console_signal;
my $reboot_success_line;
my $success_line;
my $stop_after_success;
my $stop_after_failure;
my $stop_test_after;
my $build_target;
my $target_image;
my $checkout;
my $localversion;
my $iteration = 0;
my $successes = 0;
my $stty_orig;
my $run_command_status = 0;

my $bisect_good;
my $bisect_bad;
my $bisect_type;
my $bisect_start;
my $bisect_replay;
my $bisect_files;
my $bisect_reverse;
my $bisect_check;

my $config_bisect;
my $config_bisect_type;
my $config_bisect_check;

my $patchcheck_type;
my $patchcheck_start;
my $patchcheck_cherry;
my $patchcheck_end;

my $build_time;
my $install_time;
my $reboot_time;
my $test_time;

my $pwd;
my $dirname = $FindBin::Bin;

my $mailto;
my $mailer;
my $mail_path;
my $mail_command;
my $email_on_error;
my $email_when_finished;
my $email_when_started;
my $email_when_canceled;

my $script_start_time = localtime();

# set when a test is something other that just building or install
# which would require more options.
my $buildonly = 1;

# tell build not to worry about warnings, even when WARNINGS_FILE is set
my $warnings_ok = 0;

# set when creating a new config
my $newconfig = 0;

my %entered_configs;
my %config_help;
my %variable;

# force_config is the list of configs that we force enabled (or disabled)
# in a .config file. The MIN_CONFIG and ADD_CONFIG configs.
my %force_config;

# do not force reboots on config problems
my $no_reboot = 1;

# reboot on success
my $reboot_success = 0;

my %option_map = (
    "MAILTO"			=> \$mailto,
    "MAILER"			=> \$mailer,
    "MAIL_PATH"			=> \$mail_path,
    "MAIL_COMMAND"		=> \$mail_command,
    "EMAIL_ON_ERROR"		=> \$email_on_error,
    "EMAIL_WHEN_FINISHED"	=> \$email_when_finished,
    "EMAIL_WHEN_STARTED"	=> \$email_when_started,
    "EMAIL_WHEN_CANCELED"	=> \$email_when_canceled,
    "MACHINE"			=> \$machine,
    "SSH_USER"			=> \$ssh_user,
    "TMP_DIR"			=> \$tmpdir,
    "OUTPUT_DIR"		=> \$outputdir,
    "BUILD_DIR"			=> \$builddir,
    "TEST_TYPE"			=> \$test_type,
    "PRE_KTEST"			=> \$pre_ktest,
    "POST_KTEST"		=> \$post_ktest,
    "PRE_TEST"			=> \$pre_test,
    "POST_TEST"			=> \$post_test,
    "BUILD_TYPE"		=> \$build_type,
    "BUILD_OPTIONS"		=> \$build_options,
    "PRE_BUILD"			=> \$pre_build,
    "POST_BUILD"		=> \$post_build,
    "PRE_BUILD_DIE"		=> \$pre_build_die,
    "POST_BUILD_DIE"		=> \$post_build_die,
    "POWER_CYCLE"		=> \$power_cycle,
    "REBOOT"			=> \$reboot,
    "BUILD_NOCLEAN"		=> \$noclean,
    "MIN_CONFIG"		=> \$minconfig,
    "OUTPUT_MIN_CONFIG"		=> \$output_minconfig,
    "START_MIN_CONFIG"		=> \$start_minconfig,
    "MIN_CONFIG_TYPE"		=> \$minconfig_type,
    "USE_OUTPUT_MIN_CONFIG"	=> \$use_output_minconfig,
    "WARNINGS_FILE"		=> \$warnings_file,
    "IGNORE_CONFIG"		=> \$ignore_config,
    "TEST"			=> \$run_test,
    "ADD_CONFIG"		=> \$addconfig,
    "REBOOT_TYPE"		=> \$reboot_type,
    "GRUB_MENU"			=> \$grub_menu,
    "GRUB_FILE"			=> \$grub_file,
    "GRUB_REBOOT"		=> \$grub_reboot,
    "SYSLINUX"			=> \$syslinux,
    "SYSLINUX_PATH"		=> \$syslinux_path,
    "SYSLINUX_LABEL"		=> \$syslinux_label,
    "PRE_INSTALL"		=> \$pre_install,
    "POST_INSTALL"		=> \$post_install,
    "NO_INSTALL"		=> \$no_install,
    "REBOOT_SCRIPT"		=> \$reboot_script,
    "REBOOT_ON_ERROR"		=> \$reboot_on_error,
    "SWITCH_TO_GOOD"		=> \$switch_to_good,
    "SWITCH_TO_TEST"		=> \$switch_to_test,
    "POWEROFF_ON_ERROR"		=> \$poweroff_on_error,
    "REBOOT_ON_SUCCESS"		=> \$reboot_on_success,
    "DIE_ON_FAILURE"		=> \$die_on_failure,
    "POWER_OFF"			=> \$power_off,
    "POWERCYCLE_AFTER_REBOOT"	=> \$powercycle_after_reboot,
    "POWEROFF_AFTER_HALT"	=> \$poweroff_after_halt,
    "MAX_MONITOR_WAIT"		=> \$max_monitor_wait,
    "SLEEP_TIME"		=> \$sleep_time,
    "BISECT_SLEEP_TIME"		=> \$bisect_sleep_time,
    "PATCHCHECK_SLEEP_TIME"	=> \$patchcheck_sleep_time,
    "IGNORE_WARNINGS"		=> \$ignore_warnings,
    "IGNORE_ERRORS"		=> \$ignore_errors,
    "BISECT_MANUAL"		=> \$bisect_manual,
    "BISECT_SKIP"		=> \$bisect_skip,
    "BISECT_TRIES"		=> \$bisect_tries,
    "CONFIG_BISECT_GOOD"	=> \$config_bisect_good,
    "BISECT_RET_GOOD"		=> \$bisect_ret_good,
    "BISECT_RET_BAD"		=> \$bisect_ret_bad,
    "BISECT_RET_SKIP"		=> \$bisect_ret_skip,
    "BISECT_RET_ABORT"		=> \$bisect_ret_abort,
    "BISECT_RET_DEFAULT"	=> \$bisect_ret_default,
    "STORE_FAILURES"		=> \$store_failures,
    "STORE_SUCCESSES"		=> \$store_successes,
    "TEST_NAME"			=> \$test_name,
    "TIMEOUT"			=> \$timeout,
    "CONNECT_TIMEOUT"		=> \$connect_timeout,
    "CONFIG_BISECT_EXEC"	=> \$config_bisect_exec,
    "BOOTED_TIMEOUT"		=> \$booted_timeout,
    "CONSOLE"			=> \$console,
    "CLOSE_CONSOLE_SIGNAL"	=> \$close_console_signal,
    "DETECT_TRIPLE_FAULT"	=> \$detect_triplefault,
    "SUCCESS_LINE"		=> \$success_line,
    "REBOOT_SUCCESS_LINE"	=> \$reboot_success_line,
    "STOP_AFTER_SUCCESS"	=> \$stop_after_success,
    "STOP_AFTER_FAILURE"	=> \$stop_after_failure,
    "STOP_TEST_AFTER"		=> \$stop_test_after,
    "BUILD_TARGET"		=> \$build_target,
    "SSH_EXEC"			=> \$ssh_exec,
    "SCP_TO_TARGET"		=> \$scp_to_target,
    "SCP_TO_TARGET_INSTALL"	=> \$scp_to_target_install,
    "CHECKOUT"			=> \$checkout,
    "TARGET_IMAGE"		=> \$target_image,
    "LOCALVERSION"		=> \$localversion,

    "BISECT_GOOD"		=> \$bisect_good,
    "BISECT_BAD"		=> \$bisect_bad,
    "BISECT_TYPE"		=> \$bisect_type,
    "BISECT_START"		=> \$bisect_start,
    "BISECT_REPLAY"		=> \$bisect_replay,
    "BISECT_FILES"		=> \$bisect_files,
    "BISECT_REVERSE"		=> \$bisect_reverse,
    "BISECT_CHECK"		=> \$bisect_check,

    "CONFIG_BISECT"		=> \$config_bisect,
    "CONFIG_BISECT_TYPE"	=> \$config_bisect_type,
    "CONFIG_BISECT_CHECK"	=> \$config_bisect_check,

    "PATCHCHECK_TYPE"		=> \$patchcheck_type,
    "PATCHCHECK_START"		=> \$patchcheck_start,
    "PATCHCHECK_CHERRY"		=> \$patchcheck_cherry,
    "PATCHCHECK_END"		=> \$patchcheck_end,
);

# Options may be used by other options, record them.
my %used_options;

# default variables that can be used
chomp ($variable{"PWD"} = `pwd`);
$pwd = $variable{"PWD"};

$config_help{"MACHINE"} = << "EOF"
 The machine hostname that you will test.
 For build only tests, it is still needed to differentiate log files.
EOF
    ;
$config_help{"SSH_USER"} = << "EOF"
 The box is expected to have ssh on normal bootup, provide the user
  (most likely root, since you need privileged operations)
EOF
    ;
$config_help{"BUILD_DIR"} = << "EOF"
 The directory that contains the Linux source code (full path).
 You can use \${PWD} that will be the path where ktest.pl is run, or use
 \${THIS_DIR} which is assigned \${PWD} but may be changed later.
EOF
    ;
$config_help{"OUTPUT_DIR"} = << "EOF"
 The directory that the objects will be built (full path).
 (can not be same as BUILD_DIR)
 You can use \${PWD} that will be the path where ktest.pl is run, or use
 \${THIS_DIR} which is assigned \${PWD} but may be changed later.
EOF
    ;
$config_help{"BUILD_TARGET"} = << "EOF"
 The location of the compiled file to copy to the target.
 (relative to OUTPUT_DIR)
EOF
    ;
$config_help{"BUILD_OPTIONS"} = << "EOF"
 Options to add to \"make\" when building.
 i.e.  -j20
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
 Only valid options so far are "grub", "grub2", "syslinux", and "script".

 If you specify grub, it will assume grub version 1
 and will search in /boot/grub/menu.lst for the title \$GRUB_MENU
 and select that target to reboot to the kernel. If this is not
 your setup, then specify "script" and have a command or script
 specified in REBOOT_SCRIPT to boot to the target.

 The entry in /boot/grub/menu.lst must be entered in manually.
 The test will not modify that file.

 If you specify grub2, then you also need to specify both \$GRUB_MENU
 and \$GRUB_FILE.

 If you specify syslinux, then you may use SYSLINUX to define the syslinux
 command (defaults to extlinux), and SYSLINUX_PATH to specify the path to
 the syslinux install (defaults to /boot/extlinux). But you have to specify
 SYSLINUX_LABEL to define the label to boot to for the test kernel.
EOF
    ;
$config_help{"GRUB_MENU"} = << "EOF"
 The grub title name for the test kernel to boot
 (Only mandatory if REBOOT_TYPE = grub or grub2)

 Note, ktest.pl will not update the grub menu.lst, you need to
 manually add an option for the test. ktest.pl will search
 the grub menu.lst for this option to find what kernel to
 reboot into.

 For example, if in the /boot/grub/menu.lst the test kernel title has:
 title Test Kernel
 kernel vmlinuz-test
 GRUB_MENU = Test Kernel

 For grub2, a search of \$GRUB_FILE is performed for the lines
 that begin with "menuentry". It will not detect submenus. The
 menu must be a non-nested menu. Add the quotes used in the menu
 to guarantee your selection, as the first menuentry with the content
 of \$GRUB_MENU that is found will be used.
EOF
    ;
$config_help{"GRUB_FILE"} = << "EOF"
 If grub2 is used, the full path for the grub.cfg file is placed
 here. Use something like /boot/grub2/grub.cfg to search.
EOF
    ;
$config_help{"SYSLINUX_LABEL"} = << "EOF"
 If syslinux is used, the label that boots the target kernel must
 be specified with SYSLINUX_LABEL.
EOF
    ;
$config_help{"REBOOT_SCRIPT"} = << "EOF"
 A script to reboot the target into the test kernel
 (Only mandatory if REBOOT_TYPE = script)
EOF
    ;

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

sub read_prompt {
    my ($cancel, $prompt) = @_;

    my $ans;

    for (;;) {
	if ($cancel) {
	    print "$prompt [y/n/C] ";
	} else {
	    print "$prompt [Y/n] ";
	}
	$ans = <STDIN>;
	chomp $ans;
	if ($ans =~ /^\s*$/) {
	    if ($cancel) {
		$ans = "c";
	    } else {
		$ans = "y";
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

sub get_mandatory_config {
    my ($config) = @_;
    my $ans;

    return if (defined($opt{$config}));

    if (defined($config_help{$config})) {
	print "\n";
	print $config_help{$config};
    }

    for (;;) {
	print "$config = ";
	if (defined($default{$config}) && length($default{$config})) {
	    print "\[$default{$config}\] ";
	}
	$ans = <STDIN>;
	$ans =~ s/^\s*(.*\S)\s*$/$1/;
	if ($ans =~ /^\s*$/) {
	    if ($default{$config}) {
		$ans = $default{$config};
	    } else {
		print "Your answer can not be blank\n";
		next;
	    }
	}
	$entered_configs{$config} = ${ans};
	last;
    }
}

sub show_time {
    my ($time) = @_;

    my $hours = 0;
    my $minutes = 0;

    if ($time > 3600) {
	$hours = int($time / 3600);
	$time -= $hours * 3600;
    }
    if ($time > 60) {
	$minutes = int($time / 60);
	$time -= $minutes * 60;
    }

    if ($hours > 0) {
	doprint "$hours hour";
	doprint "s" if ($hours > 1);
	doprint " ";
    }

    if ($minutes > 0) {
	doprint "$minutes minute";
	doprint "s" if ($minutes > 1);
	doprint " ";
    }

    doprint "$time second";
    doprint "s" if ($time != 1);
}

sub print_times {
    doprint "\n";
    if ($build_time) {
	doprint "Build time:   ";
	show_time($build_time);
	doprint "\n";
    }
    if ($install_time) {
	doprint "Install time: ";
	show_time($install_time);
	doprint "\n";
    }
    if ($reboot_time) {
	doprint "Reboot time:  ";
	show_time($reboot_time);
	doprint "\n";
    }
    if ($test_time) {
	doprint "Test time:    ";
	show_time($test_time);
	doprint "\n";
    }
    # reset for iterations like bisect
    $build_time = 0;
    $install_time = 0;
    $reboot_time = 0;
    $test_time = 0;
}

sub get_mandatory_configs {
    get_mandatory_config("MACHINE");
    get_mandatory_config("BUILD_DIR");
    get_mandatory_config("OUTPUT_DIR");

    if ($newconfig) {
	get_mandatory_config("BUILD_OPTIONS");
    }

    # options required for other than just building a kernel
    if (!$buildonly) {
	get_mandatory_config("POWER_CYCLE");
	get_mandatory_config("CONSOLE");
    }

    # options required for install and more
    if ($buildonly != 1) {
	get_mandatory_config("SSH_USER");
	get_mandatory_config("BUILD_TARGET");
	get_mandatory_config("TARGET_IMAGE");
    }

    get_mandatory_config("LOCALVERSION");

    return if ($buildonly);

    my $rtype = $opt{"REBOOT_TYPE"};

    if (!defined($rtype)) {
	if (!defined($opt{"GRUB_MENU"})) {
	    get_mandatory_config("REBOOT_TYPE");
	    $rtype = $entered_configs{"REBOOT_TYPE"};
	} else {
	    $rtype = "grub";
	}
    }

    if ($rtype eq "grub") {
	get_mandatory_config("GRUB_MENU");
    }

    if ($rtype eq "grub2") {
	get_mandatory_config("GRUB_MENU");
	get_mandatory_config("GRUB_FILE");
    }

    if ($rtype eq "syslinux") {
	get_mandatory_config("SYSLINUX_LABEL");
    }
}

sub process_variables {
    my ($value, $remove_undef) = @_;
    my $retval = "";

    # We want to check for '\', and it is just easier
    # to check the previous characet of '$' and not need
    # to worry if '$' is the first character. By adding
    # a space to $value, we can just check [^\\]\$ and
    # it will still work.
    $value = " $value";

    while ($value =~ /(.*?[^\\])\$\{(.*?)\}(.*)/) {
	my $begin = $1;
	my $var = $2;
	my $end = $3;
	# append beginning of value to retval
	$retval = "$retval$begin";
	if (defined($variable{$var})) {
	    $retval = "$retval$variable{$var}";
	} elsif (defined($remove_undef) && $remove_undef) {
	    # for if statements, any variable that is not defined,
	    # we simple convert to 0
	    $retval = "${retval}0";
	} else {
	    # put back the origin piece.
	    $retval = "$retval\$\{$var\}";
	    # This could be an option that is used later, save
	    # it so we don't warn if this option is not one of
	    # ktests options.
	    $used_options{$var} = 1;
	}
	$value = $end;
    }
    $retval = "$retval$value";

    # remove the space added in the beginning
    $retval =~ s/ //;

    return "$retval"
}

sub set_value {
    my ($lvalue, $rvalue, $override, $overrides, $name) = @_;

    my $prvalue = process_variables($rvalue);

    if ($lvalue =~ /^(TEST|BISECT|CONFIG_BISECT)_TYPE(\[.*\])?$/ &&
	$prvalue !~ /^(config_|)bisect$/ &&
	$prvalue !~ /^build$/ &&
	$buildonly) {

	# Note if a test is something other than build, then we
	# will need other mandatory options.
	if ($prvalue ne "install") {
	    $buildonly = 0;
	} else {
	    # install still limits some mandatory options.
	    $buildonly = 2;
	}
    }

    if (defined($opt{$lvalue})) {
	if (!$override || defined(${$overrides}{$lvalue})) {
	    my $extra = "";
	    if ($override) {
		$extra = "In the same override section!\n";
	    }
	    die "$name: $.: Option $lvalue defined more than once!\n$extra";
	}
	${$overrides}{$lvalue} = $prvalue;
    }

    $opt{$lvalue} = $prvalue;
}

sub set_eval {
    my ($lvalue, $rvalue, $name) = @_;

    my $prvalue = process_variables($rvalue);
    my $arr;

    if (defined($evals{$lvalue})) {
	$arr = $evals{$lvalue};
    } else {
	$arr = [];
	$evals{$lvalue} = $arr;
    }

    push @{$arr}, $rvalue;
}

sub set_variable {
    my ($lvalue, $rvalue) = @_;

    if ($rvalue =~ /^\s*$/) {
	delete $variable{$lvalue};
    } else {
	$rvalue = process_variables($rvalue);
	$variable{$lvalue} = $rvalue;
    }
}

sub process_compare {
    my ($lval, $cmp, $rval) = @_;

    # remove whitespace

    $lval =~ s/^\s*//;
    $lval =~ s/\s*$//;

    $rval =~ s/^\s*//;
    $rval =~ s/\s*$//;

    if ($cmp eq "==") {
	return $lval eq $rval;
    } elsif ($cmp eq "!=") {
	return $lval ne $rval;
    } elsif ($cmp eq "=~") {
	return $lval =~ m/$rval/;
    } elsif ($cmp eq "!~") {
	return $lval !~ m/$rval/;
    }

    my $statement = "$lval $cmp $rval";
    my $ret = eval $statement;

    # $@ stores error of eval
    if ($@) {
	return -1;
    }

    return $ret;
}

sub value_defined {
    my ($val) = @_;

    return defined($variable{$2}) ||
	defined($opt{$2});
}

my $d = 0;
sub process_expression {
    my ($name, $val) = @_;

    my $c = $d++;

    while ($val =~ s/\(([^\(]*?)\)/\&\&\&\&VAL\&\&\&\&/) {
	my $express = $1;

	if (process_expression($name, $express)) {
	    $val =~ s/\&\&\&\&VAL\&\&\&\&/ 1 /;
	} else {
	    $val =~ s/\&\&\&\&VAL\&\&\&\&/ 0 /;
	}
    }

    $d--;
    my $OR = "\\|\\|";
    my $AND = "\\&\\&";

    while ($val =~ s/^(.*?)($OR|$AND)//) {
	my $express = $1;
	my $op = $2;

	if (process_expression($name, $express)) {
	    if ($op eq "||") {
		return 1;
	    }
	} else {
	    if ($op eq "&&") {
		return 0;
	    }
	}
    }

    if ($val =~ /(.*)(==|\!=|>=|<=|>|<|=~|\!~)(.*)/) {
	my $ret = process_compare($1, $2, $3);
	if ($ret < 0) {
	    die "$name: $.: Unable to process comparison\n";
	}
	return $ret;
    }

    if ($val =~ /^\s*(NOT\s*)?DEFINED\s+(\S+)\s*$/) {
	if (defined $1) {
	    return !value_defined($2);
	} else {
	    return value_defined($2);
	}
    }

    if ($val =~ /^\s*0\s*$/) {
	return 0;
    } elsif ($val =~ /^\s*\d+\s*$/) {
	return 1;
    }

    die ("$name: $.: Undefined content $val in if statement\n");
}

sub process_if {
    my ($name, $value) = @_;

    # Convert variables and replace undefined ones with 0
    my $val = process_variables($value, 1);
    my $ret = process_expression $name, $val;

    return $ret;
}

sub __read_config {
    my ($config, $current_test_num) = @_;

    my $in;
    open($in, $config) || die "can't read file $config";

    my $name = $config;
    $name =~ s,.*/(.*),$1,;

    my $test_num = $$current_test_num;
    my $default = 1;
    my $repeat = 1;
    my $num_tests_set = 0;
    my $skip = 0;
    my $rest;
    my $line;
    my $test_case = 0;
    my $if = 0;
    my $if_set = 0;
    my $override = 0;

    my %overrides;

    while (<$in>) {

	# ignore blank lines and comments
	next if (/^\s*$/ || /\s*\#/);

	if (/^\s*(TEST_START|DEFAULTS)\b(.*)/) {

	    my $type = $1;
	    $rest = $2;
	    $line = $2;

	    my $old_test_num;
	    my $old_repeat;
	    $override = 0;

	    if ($type eq "TEST_START") {

		if ($num_tests_set) {
		    die "$name: $.: Can not specify both NUM_TESTS and TEST_START\n";
		}

		$old_test_num = $test_num;
		$old_repeat = $repeat;

		$test_num += $repeat;
		$default = 0;
		$repeat = 1;
	    } else {
		$default = 1;
	    }

	    # If SKIP is anywhere in the line, the command will be skipped
	    if ($rest =~ s/\s+SKIP\b//) {
		$skip = 1;
	    } else {
		$test_case = 1;
		$skip = 0;
	    }

	    if ($rest =~ s/\sELSE\b//) {
		if (!$if) {
		    die "$name: $.: ELSE found with out matching IF section\n$_";
		}
		$if = 0;

		if ($if_set) {
		    $skip = 1;
		} else {
		    $skip = 0;
		}
	    }

	    if ($rest =~ s/\sIF\s+(.*)//) {
		if (process_if($name, $1)) {
		    $if_set = 1;
		} else {
		    $skip = 1;
		}
		$if = 1;
	    } else {
		$if = 0;
		$if_set = 0;
	    }

	    if (!$skip) {
		if ($type eq "TEST_START") {
		    if ($rest =~ s/\s+ITERATE\s+(\d+)//) {
			$repeat = $1;
			$repeat_tests{"$test_num"} = $repeat;
		    }
		} elsif ($rest =~ s/\sOVERRIDE\b//) {
		    # DEFAULT only
		    $override = 1;
		    # Clear previous overrides
		    %overrides = ();
		}
	    }

	    if (!$skip && $rest !~ /^\s*$/) {
		die "$name: $.: Gargbage found after $type\n$_";
	    }

	    if ($skip && $type eq "TEST_START") {
		$test_num = $old_test_num;
		$repeat = $old_repeat;
	    }

	} elsif (/^\s*ELSE\b(.*)$/) {
	    if (!$if) {
		die "$name: $.: ELSE found with out matching IF section\n$_";
	    }
	    $rest = $1;
	    if ($if_set) {
		$skip = 1;
		$rest = "";
	    } else {
		$skip = 0;

		if ($rest =~ /\sIF\s+(.*)/) {
		    # May be a ELSE IF section.
		    if (process_if($name, $1)) {
			$if_set = 1;
		    } else {
			$skip = 1;
		    }
		    $rest = "";
		} else {
		    $if = 0;
		}
	    }

	    if ($rest !~ /^\s*$/) {
		die "$name: $.: Gargbage found after DEFAULTS\n$_";
	    }

	} elsif (/^\s*INCLUDE\s+(\S+)/) {

	    next if ($skip);

	    if (!$default) {
		die "$name: $.: INCLUDE can only be done in default sections\n$_";
	    }

	    my $file = process_variables($1);

	    if ($file !~ m,^/,) {
		# check the path of the config file first
		if ($config =~ m,(.*)/,) {
		    if (-f "$1/$file") {
			$file = "$1/$file";
		    }
		}
	    }
		
	    if ( ! -r $file ) {
		die "$name: $.: Can't read file $file\n$_";
	    }

	    if (__read_config($file, \$test_num)) {
		$test_case = 1;
	    }

	} elsif (/^\s*([A-Z_\[\]\d]+)\s*=~\s*(.*?)\s*$/) {

	    next if ($skip);

	    my $lvalue = $1;
	    my $rvalue = $2;

	    if ($default || $lvalue =~ /\[\d+\]$/) {
		set_eval($lvalue, $rvalue, $name);
	    } else {
		my $val = "$lvalue\[$test_num\]";
		set_eval($val, $rvalue, $name);
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
		set_value($lvalue, $rvalue, $override, \%overrides, $name);
	    } else {
		my $val = "$lvalue\[$test_num\]";
		set_value($val, $rvalue, $override, \%overrides, $name);

		if ($repeat > 1) {
		    $repeats{$val} = $repeat;
		}
	    }
	} elsif (/^\s*([A-Z_\[\]\d]+)\s*:=\s*(.*?)\s*$/) {
	    next if ($skip);

	    my $lvalue = $1;
	    my $rvalue = $2;

	    # process config variables.
	    # Config variables are only active while reading the
	    # config and can be defined anywhere. They also ignore
	    # TEST_START and DEFAULTS, but are skipped if they are in
	    # on of these sections that have SKIP defined.
	    # The save variable can be
	    # defined multiple times and the new one simply overrides
	    # the prevous one.
	    set_variable($lvalue, $rvalue);

	} else {
	    die "$name: $.: Garbage found in config\n$_";
	}
    }

    if ($test_num) {
	$test_num += $repeat - 1;
	$opt{"NUM_TESTS"} = $test_num;
    }

    close($in);

    $$current_test_num = $test_num;

    return $test_case;
}

sub get_test_case {
	print "What test case would you like to run?\n";
	print " (build, install or boot)\n";
	print " Other tests are available but require editing ktest.conf\n";
	print " (see tools/testing/ktest/sample.conf)\n";
	my $ans = <STDIN>;
	chomp $ans;
	$default{"TEST_TYPE"} = $ans;
}

sub read_config {
    my ($config) = @_;

    my $test_case;
    my $test_num = 0;

    $test_case = __read_config $config, \$test_num;

    # make sure we have all mandatory configs
    get_mandatory_configs;

    # was a test specified?
    if (!$test_case) {
	print "No test case specified.\n";
	get_test_case;
    }

    # set any defaults

    foreach my $default (keys %default) {
	if (!defined($opt{$default})) {
	    $opt{$default} = $default{$default};
	}
    }

    if ($opt{"IGNORE_UNUSED"} == 1) {
	return;
    }

    my %not_used;

    # check if there are any stragglers (typos?)
    foreach my $option (keys %opt) {
	my $op = $option;
	# remove per test labels.
	$op =~ s/\[.*\]//;
	if (!exists($option_map{$op}) &&
	    !exists($default{$op}) &&
	    !exists($used_options{$op})) {
	    $not_used{$op} = 1;
	}
    }

    if (%not_used) {
	my $s = "s are";
	$s = " is" if (keys %not_used == 1);
	print "The following option$s not used; could be a typo:\n";
	foreach my $option (keys %not_used) {
	    print "$option\n";
	}
	print "Set IGRNORE_UNUSED = 1 to have ktest ignore unused variables\n";
	if (!read_yn "Do you want to continue?") {
	    exit -1;
	}
    }
}

sub __eval_option {
    my ($name, $option, $i) = @_;

    # Add space to evaluate the character before $
    $option = " $option";
    my $retval = "";
    my $repeated = 0;
    my $parent = 0;

    foreach my $test (keys %repeat_tests) {
	if ($i >= $test &&
	    $i < $test + $repeat_tests{$test}) {

	    $repeated = 1;
	    $parent = $test;
	    last;
	}
    }

    while ($option =~ /(.*?[^\\])\$\{(.*?)\}(.*)/) {
	my $start = $1;
	my $var = $2;
	my $end = $3;

	# Append beginning of line
	$retval = "$retval$start";

	# If the iteration option OPT[$i] exists, then use that.
	# otherwise see if the default OPT (without [$i]) exists.

	my $o = "$var\[$i\]";
	my $parento = "$var\[$parent\]";

	# If a variable contains itself, use the default var
	if (($var eq $name) && defined($opt{$var})) {
	    $o = $opt{$var};
	    $retval = "$retval$o";
	} elsif (defined($opt{$o})) {
	    $o = $opt{$o};
	    $retval = "$retval$o";
	} elsif ($repeated && defined($opt{$parento})) {
	    $o = $opt{$parento};
	    $retval = "$retval$o";
	} elsif (defined($opt{$var})) {
	    $o = $opt{$var};
	    $retval = "$retval$o";
	} elsif ($var eq "KERNEL_VERSION" && defined($make)) {
	    # special option KERNEL_VERSION uses kernel version
	    get_version();
	    $retval = "$retval$version";
	} else {
	    $retval = "$retval\$\{$var\}";
	}

	$option = $end;
    }

    $retval = "$retval$option";

    $retval =~ s/^ //;

    return $retval;
}

sub process_evals {
    my ($name, $option, $i) = @_;

    my $option_name = "$name\[$i\]";
    my $ev;

    my $old_option = $option;

    if (defined($evals{$option_name})) {
	$ev = $evals{$option_name};
    } elsif (defined($evals{$name})) {
	$ev = $evals{$name};
    } else {
	return $option;
    }

    for my $e (@{$ev}) {
	eval "\$option =~ $e";
    }

    if ($option ne $old_option) {
	doprint("$name changed from '$old_option' to '$option'\n");
    }

    return $option;
}

sub eval_option {
    my ($name, $option, $i) = @_;

    my $prev = "";

    # Since an option can evaluate to another option,
    # keep iterating until we do not evaluate any more
    # options.
    my $r = 0;
    while ($prev ne $option) {
	# Check for recursive evaluations.
	# 100 deep should be more than enough.
	if ($r++ > 100) {
	    die "Over 100 evaluations accurred with $option\n" .
		"Check for recursive variables\n";
	}
	$prev = $option;
	$option = __eval_option($name, $option, $i);
    }

    $option = process_evals($name, $option, $i);

    return $option;
}

sub run_command;
sub start_monitor;
sub end_monitor;
sub wait_for_monitor;

sub reboot {
    my ($time) = @_;
    my $powercycle = 0;

    # test if the machine can be connected to within a few seconds
    my $stat = run_ssh("echo check machine status", $connect_timeout);
    if (!$stat) {
	doprint("power cycle\n");
	$powercycle = 1;
    }

    if ($powercycle) {
	run_command "$power_cycle";

	start_monitor;
	# flush out current monitor
	# May contain the reboot success line
	wait_for_monitor 1;

    } else {
	# Make sure everything has been written to disk
	run_ssh("sync", 10);

	if (defined($time)) {
	    start_monitor;
	    # flush out current monitor
	    # May contain the reboot success line
	    wait_for_monitor 1;
	}

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

    if (defined($time)) {

	# We only want to get to the new kernel, don't fail
	# if we stumble over a call trace.
	my $save_ignore_errors = $ignore_errors;
	$ignore_errors = 1;

	# Look for the good kernel to boot
	if (wait_for_monitor($time, "Linux version")) {
	    # reboot got stuck?
	    doprint "Reboot did not finish. Forcing power cycle\n";
	    run_command "$power_cycle";
	}

	$ignore_errors = $save_ignore_errors;

	# Still need to wait for the reboot to finish
	wait_for_monitor($time, $reboot_success_line);

	end_monitor;
    }
}

sub reboot_to_good {
    my ($time) = @_;

    if (defined($switch_to_good)) {
	run_command $switch_to_good;
    }

    reboot $time;
}

sub do_not_reboot {
    my $i = $iteration;

    return $test_type eq "build" || $no_reboot ||
	($test_type eq "patchcheck" && $opt{"PATCHCHECK_TYPE[$i]"} eq "build") ||
	($test_type eq "bisect" && $opt{"BISECT_TYPE[$i]"} eq "build") ||
	($test_type eq "config_bisect" && $opt{"CONFIG_BISECT_TYPE[$i]"} eq "build");
}

my $in_die = 0;

sub dodie {

    # avoid recusion
    return if ($in_die);
    $in_die = 1;

    doprint "CRITICAL FAILURE... ", @_, "\n";

    my $i = $iteration;

    if ($reboot_on_error && !do_not_reboot) {

	doprint "REBOOTING\n";
	reboot_to_good;

    } elsif ($poweroff_on_error && defined($power_off)) {
	doprint "POWERING OFF\n";
	`$power_off`;
    }

    if (defined($opt{"LOG_FILE"})) {
	print " See $opt{LOG_FILE} for more info.\n";
    }

    if ($email_on_error) {
        send_email("KTEST: critical failure for your [$test_type] test",
                "Your test started at $script_start_time has failed with:\n@_\n");
    }

    if ($monitor_cnt) {
	    # restore terminal settings
	    system("stty $stty_orig");
    }

    if (defined($post_test)) {
	run_command $post_test;
    }

    die @_, "\n";
}

sub create_pty {
    my ($ptm, $pts) = @_;
    my $tmp;
    my $TIOCSPTLCK = 0x40045431;
    my $TIOCGPTN = 0x80045430;

    sysopen($ptm, "/dev/ptmx", O_RDWR | O_NONBLOCK) or
	dodie "Cant open /dev/ptmx";

    # unlockpt()
    $tmp = pack("i", 0);
    ioctl($ptm, $TIOCSPTLCK, $tmp) or
	dodie "ioctl TIOCSPTLCK for /dev/ptmx failed";

    # ptsname()
    ioctl($ptm, $TIOCGPTN, $tmp) or
	dodie "ioctl TIOCGPTN for /dev/ptmx failed";
    $tmp = unpack("i", $tmp);

    sysopen($pts, "/dev/pts/$tmp", O_RDWR | O_NONBLOCK) or
	dodie "Can't open /dev/pts/$tmp";
}

sub exec_console {
    my ($ptm, $pts) = @_;

    close($ptm);

    close(\*STDIN);
    close(\*STDOUT);
    close(\*STDERR);

    open(\*STDIN, '<&', $pts);
    open(\*STDOUT, '>&', $pts);
    open(\*STDERR, '>&', $pts);

    close($pts);

    exec $console or
	dodie "Can't open console $console";
}

sub open_console {
    my ($ptm) = @_;
    my $pts = \*PTSFD;
    my $pid;

    # save terminal settings
    $stty_orig = `stty -g`;

    # place terminal in cbreak mode so that stdin can be read one character at
    # a time without having to wait for a newline
    system("stty -icanon -echo -icrnl");

    create_pty($ptm, $pts);

    $pid = fork;

    if (!$pid) {
	# child
	exec_console($ptm, $pts)
    }

    # parent
    close($pts);

    return $pid;

    open(PTSFD, "Stop perl from warning about single use of PTSFD");
}

sub close_console {
    my ($fp, $pid) = @_;

    doprint "kill child process $pid\n";
    kill $close_console_signal, $pid;

    doprint "wait for child process $pid to exit\n";
    waitpid($pid, 0);

    print "closing!\n";
    close($fp);

    # restore terminal settings
    system("stty $stty_orig");
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
    return if (!defined $console);
    if (--$monitor_cnt) {
	return;
    }
    close_console($monitor_fp, $monitor_pid);
}

sub wait_for_monitor {
    my ($time, $stop) = @_;
    my $full_line = "";
    my $line;
    my $booted = 0;
    my $start_time = time;
    my $skip_call_trace = 0;
    my $bug = 0;
    my $bug_ignored = 0;
    my $now;

    doprint "** Wait for monitor to settle down **\n";

    # read the monitor and wait for the system to calm down
    while (!$booted) {
	$line = wait_for_input($monitor_fp, $time);
	last if (!defined($line));
	print "$line";
	$full_line .= $line;

	if (defined($stop) && $full_line =~ /$stop/) {
	    doprint "wait for monitor detected $stop\n";
	    $booted = 1;
	}

	if ($full_line =~ /\[ backtrace testing \]/) {
	    $skip_call_trace = 1;
	}

	if ($full_line =~ /call trace:/i) {
	    if (!$bug && !$skip_call_trace) {
		if ($ignore_errors) {
		    $bug_ignored = 1;
		} else {
		    $bug = 1;
		}
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
	$now = time;
	if ($now - $start_time >= $max_monitor_wait) {
	    doprint "Exiting monitor flush due to hitting MAX_MONITOR_WAIT\n";
	    return 1;
	}
    }
    print "** Monitor flushed **\n";

    # if stop is defined but wasn't hit, return error
    # used by reboot (which wants to see a reboot)
    if (defined($stop) && !$booted) {
	$bug = 1;
    }
    return $bug;
}

sub save_logs {
	my ($result, $basedir) = @_;
	my @t = localtime;
	my $date = sprintf "%04d%02d%02d%02d%02d%02d",
		1900+$t[5],$t[4],$t[3],$t[2],$t[1],$t[0];

	my $type = $build_type;
	if ($type =~ /useconfig/) {
	    $type = "useconfig";
	}

	my $dir = "$machine-$test_type-$type-$result-$date";

	$dir = "$basedir/$dir";

	if (!-d $dir) {
	    mkpath($dir) or
		dodie "can't create $dir";
	}

	my %files = (
		"config" => $output_config,
		"buildlog" => $buildlog,
		"dmesg" => $dmesg,
		"testlog" => $testlog,
	);

	while (my ($name, $source) = each(%files)) {
		if (-f "$source") {
			cp "$source", "$dir/$name" or
				dodie "failed to copy $source";
		}
	}

	doprint "*** Saved info to $dir ***\n";
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
	    reboot_to_good $sleep_time;
	}

	my $name = "";

	if (defined($test_name)) {
	    $name = " ($test_name)";
	}

	print_times;

	doprint "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";
	doprint "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";
	doprint "KTEST RESULT: TEST $i$name Failed: ", @_, "\n";
	doprint "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";
	doprint "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n";

	if (defined($store_failures)) {
	    save_logs "fail", $store_failures;
        }

	if (defined($post_test)) {
		run_command $post_test;
	}

	return 1;
}

sub run_command {
    my ($command, $redirect, $timeout) = @_;
    my $start_time;
    my $end_time;
    my $dolog = 0;
    my $dord = 0;
    my $dostdout = 0;
    my $pid;

    $command =~ s/\$SSH_USER/$ssh_user/g;
    $command =~ s/\$MACHINE/$machine/g;

    doprint("$command ... ");
    $start_time = time;

    $pid = open(CMD, "$command 2>&1 |") or
	(fail "unable to exec $command" and return 0);

    if (defined($opt{"LOG_FILE"})) {
	open(LOG, ">>$opt{LOG_FILE}") or
	    dodie "failed to write to log";
	$dolog = 1;
    }

    if (defined($redirect)) {
	if ($redirect eq 1) {
	    $dostdout = 1;
	    # Have the output of the command on its own line
	    doprint "\n";
	} else {
	    open (RD, ">$redirect") or
		dodie "failed to write to redirect $redirect";
	    $dord = 1;
	}
    }

    my $hit_timeout = 0;

    while (1) {
	my $fp = \*CMD;
	if (defined($timeout)) {
	    doprint "timeout = $timeout\n";
	}
	my $line = wait_for_input($fp, $timeout);
	if (!defined($line)) {
	    my $now = time;
	    if (defined($timeout) && (($now - $start_time) >= $timeout)) {
		doprint "Hit timeout of $timeout, killing process\n";
		$hit_timeout = 1;
		kill 9, $pid;
	    }
	    last;
	}
	print LOG $line if ($dolog);
	print RD $line if ($dord);
	print $line if ($dostdout);
    }

    waitpid($pid, 0);
    # shift 8 for real exit status
    $run_command_status = $? >> 8;

    close(CMD);
    close(LOG) if ($dolog);
    close(RD)  if ($dord);

    $end_time = time;
    my $delta = $end_time - $start_time;

    if ($delta == 1) {
	doprint "[1 second] ";
    } else {
	doprint "[$delta seconds] ";
    }

    if ($hit_timeout) {
	$run_command_status = 1;
    }

    if ($run_command_status) {
	doprint "FAILED!\n";
    } else {
	doprint "SUCCESS\n";
    }

    return !$run_command_status;
}

sub run_ssh {
    my ($cmd, $timeout) = @_;
    my $cp_exec = $ssh_exec;

    $cp_exec =~ s/\$SSH_COMMAND/$cmd/g;
    return run_command "$cp_exec", undef , $timeout;
}

sub run_scp {
    my ($src, $dst, $cp_scp) = @_;

    $cp_scp =~ s/\$SRC_FILE/$src/g;
    $cp_scp =~ s/\$DST_FILE/$dst/g;

    return run_command "$cp_scp";
}

sub run_scp_install {
    my ($src, $dst) = @_;

    my $cp_scp = $scp_to_target_install;

    return run_scp($src, $dst, $cp_scp);
}

sub run_scp_mod {
    my ($src, $dst) = @_;

    my $cp_scp = $scp_to_target;

    return run_scp($src, $dst, $cp_scp);
}

sub get_grub2_index {

    return if (defined($grub_number) && defined($last_grub_menu) &&
	       $last_grub_menu eq $grub_menu && defined($last_machine) &&
	       $last_machine eq $machine);

    doprint "Find grub2 menu ... ";
    $grub_number = -1;

    my $ssh_grub = $ssh_exec;
    $ssh_grub =~ s,\$SSH_COMMAND,cat $grub_file,g;

    open(IN, "$ssh_grub |")
	or dodie "unable to get $grub_file";

    my $found = 0;

    while (<IN>) {
	if (/^menuentry.*$grub_menu/) {
	    $grub_number++;
	    $found = 1;
	    last;
	} elsif (/^menuentry\s|^submenu\s/) {
	    $grub_number++;
	}
    }
    close(IN);

    dodie "Could not find '$grub_menu' in $grub_file on $machine"
	if (!$found);
    doprint "$grub_number\n";
    $last_grub_menu = $grub_menu;
    $last_machine = $machine;
}

sub get_grub_index {

    if ($reboot_type eq "grub2") {
	get_grub2_index;
	return;
    }

    if ($reboot_type ne "grub") {
	return;
    }
    return if (defined($grub_number) && defined($last_grub_menu) &&
	       $last_grub_menu eq $grub_menu && defined($last_machine) &&
	       $last_machine eq $machine);

    doprint "Find grub menu ... ";
    $grub_number = -1;

    my $ssh_grub = $ssh_exec;
    $ssh_grub =~ s,\$SSH_COMMAND,cat /boot/grub/menu.lst,g;

    open(IN, "$ssh_grub |")
	or dodie "unable to get menu.lst";

    my $found = 0;

    while (<IN>) {
	if (/^\s*title\s+$grub_menu\s*$/) {
	    $grub_number++;
	    $found = 1;
	    last;
	} elsif (/^\s*title\s/) {
	    $grub_number++;
	}
    }
    close(IN);

    dodie "Could not find '$grub_menu' in /boot/grub/menu on $machine"
	if (!$found);
    doprint "$grub_number\n";
    $last_grub_menu = $grub_menu;
    $last_machine = $machine;
}

sub wait_for_input
{
    my ($fp, $time) = @_;
    my $start_time;
    my $rin;
    my $rout;
    my $nr;
    my $buf;
    my $line;
    my $ch;

    if (!defined($time)) {
	$time = $timeout;
    }

    $rin = '';
    vec($rin, fileno($fp), 1) = 1;
    vec($rin, fileno(\*STDIN), 1) = 1;

    $start_time = time;

    while (1) {
	$nr = select($rout=$rin, undef, undef, $time);

	last if ($nr <= 0);

	# copy data from stdin to the console
	if (vec($rout, fileno(\*STDIN), 1) == 1) {
	    $nr = sysread(\*STDIN, $buf, 1000);
	    syswrite($fp, $buf, $nr) if ($nr > 0);
	}

	# The timeout is based on time waiting for the fp data
	if (vec($rout, fileno($fp), 1) != 1) {
	    last if (defined($time) && (time - $start_time > $time));
	    next;
	}

	$line = "";

	# try to read one char at a time
	while (sysread $fp, $ch, 1) {
	    $line .= $ch;
	    last if ($ch eq "\n");
	}

	last if (!length($line));

	return $line;
    }
    return undef;
}

sub reboot_to {
    if (defined($switch_to_test)) {
	run_command $switch_to_test;
    }

    if ($reboot_type eq "grub") {
	run_ssh "'(echo \"savedefault --default=$grub_number --once\" | grub --batch)'";
    } elsif ($reboot_type eq "grub2") {
	run_ssh "$grub_reboot $grub_number";
    } elsif ($reboot_type eq "syslinux") {
	run_ssh "$syslinux --once \\\"$syslinux_label\\\" $syslinux_path";
    } elsif (defined $reboot_script) {
	run_command "$reboot_script";
    }
    reboot;
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
    my $bug_ignored = 0;
    my $skip_call_trace = 0;
    my $loops;

    my $start_time = time;

    wait_for_monitor 5;

    my $line;
    my $full_line = "";

    open(DMESG, "> $dmesg") or
	dodie "unable to write to $dmesg";

    reboot_to;

    my $success_start;
    my $failure_start;
    my $monitor_start = time;
    my $done = 0;
    my $version_found = 0;

    while (!$done) {

	if ($bug && defined($stop_after_failure) &&
	    $stop_after_failure >= 0) {
	    my $time = $stop_after_failure - (time - $failure_start);
	    $line = wait_for_input($monitor_fp, $time);
	    if (!defined($line)) {
		doprint "bug timed out after $booted_timeout seconds\n";
		doprint "Test forced to stop after $stop_after_failure seconds after failure\n";
		last;
	    }
	} elsif ($booted) {
	    $line = wait_for_input($monitor_fp, $booted_timeout);
	    if (!defined($line)) {
		my $s = $booted_timeout == 1 ? "" : "s";
		doprint "Successful boot found: break after $booted_timeout second$s\n";
		last;
	    }
	} else {
	    $line = wait_for_input($monitor_fp);
	    if (!defined($line)) {
		my $s = $timeout == 1 ? "" : "s";
		doprint "Timed out after $timeout second$s\n";
		last;
	    }
	}

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
	    if (!$bug && !$skip_call_trace) {
		if ($ignore_errors) {
		    $bug_ignored = 1;
		} else {
		    $bug = 1;
		    $failure_start = time;
		}
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
	    $failure_start = time;
	    $bug = 1;
	}

	# Detect triple faults by testing the banner
	if ($full_line =~ /\bLinux version (\S+).*\n/) {
	    if ($1 eq $version) {
		$version_found = 1;
	    } elsif ($version_found && $detect_triplefault) {
		# We already booted into the kernel we are testing,
		# but now we booted into another kernel?
		# Consider this a triple fault.
		doprint "Already booted in Linux kernel $version, but now\n";
		doprint "we booted into Linux kernel $1.\n";
		doprint "Assuming that this is a triple fault.\n";
		doprint "To disable this: set DETECT_TRIPLE_FAULT to 0\n";
		last;
	    }
	}

	if ($line =~ /\n/) {
	    $full_line = "";
	}

	if ($stop_test_after > 0 && !$booted && !$bug) {
	    if (time - $monitor_start > $stop_test_after) {
		doprint "STOP_TEST_AFTER ($stop_test_after seconds) timed out\n";
		$done = 1;
	    }
	}
    }

    my $end_time = time;
    $reboot_time = $end_time - $start_time;

    close(DMESG);

    if ($bug) {
	return 0 if ($in_bisect);
	fail "failed - got a bug report" and return 0;
    }

    if (!$booted) {
	return 0 if ($in_bisect);
	fail "failed - never got a boot prompt." and return 0;
    }

    if ($bug_ignored) {
	doprint "WARNING: Call Trace detected but ignored due to IGNORE_ERRORS=1\n";
    }

    return 1;
}

sub eval_kernel_version {
    my ($option) = @_;

    $option =~ s/\$KERNEL_VERSION/$version/g;

    return $option;
}

sub do_post_install {

    return if (!defined($post_install));

    my $cp_post_install = eval_kernel_version $post_install;
    run_command "$cp_post_install" or
	dodie "Failed to run post install";
}

# Sometimes the reboot fails, and will hang. We try to ssh to the box
# and if we fail, we force another reboot, that should powercycle it.
sub test_booted {
    if (!run_ssh "echo testing connection") {
	reboot $sleep_time;
    }
}

sub install {

    return if ($no_install);

    my $start_time = time;

    if (defined($pre_install)) {
	my $cp_pre_install = eval_kernel_version $pre_install;
	run_command "$cp_pre_install" or
	    dodie "Failed to run pre install";
    }

    my $cp_target = eval_kernel_version $target_image;

    test_booted;

    run_scp_install "$outputdir/$build_target", "$cp_target" or
	dodie "failed to copy image";

    my $install_mods = 0;

    # should we process modules?
    $install_mods = 0;
    open(IN, "$output_config") or dodie("Can't read config file");
    while (<IN>) {
	if (/CONFIG_MODULES(=y)?/) {
	    if (defined($1)) {
		$install_mods = 1;
		last;
	    }
	}
    }
    close(IN);

    if (!$install_mods) {
	do_post_install;
	doprint "No modules needed\n";
	my $end_time = time;
	$install_time = $end_time - $start_time;
	return;
    }

    run_command "$make INSTALL_MOD_STRIP=1 INSTALL_MOD_PATH=$tmpdir modules_install" or
	dodie "Failed to install modules";

    my $modlib = "/lib/modules/$version";
    my $modtar = "ktest-mods.tar.bz2";

    run_ssh "rm -rf $modlib" or
	dodie "failed to remove old mods: $modlib";

    # would be nice if scp -r did not follow symbolic links
    run_command "cd $tmpdir && tar -cjf $modtar lib/modules/$version" or
	dodie "making tarball";

    run_scp_mod "$tmpdir/$modtar", "/tmp" or
	dodie "failed to copy modules";

    unlink "$tmpdir/$modtar";

    run_ssh "'(cd / && tar xjf /tmp/$modtar)'" or
	dodie "failed to tar modules";

    run_ssh "rm -f /tmp/$modtar";

    do_post_install;

    my $end_time = time;
    $install_time = $end_time - $start_time;
}

sub get_version {
    # get the release name
    return if ($have_version);
    doprint "$make kernelrelease ... ";
    $version = `$make -s kernelrelease | tail -1`;
    chomp($version);
    doprint "$version\n";
    $have_version = 1;
}

sub start_monitor_and_install {
    # Make sure the stable kernel has finished booting

    # Install bisects, don't need console
    if (defined $console) {
	start_monitor;
	wait_for_monitor 5;
	end_monitor;
    }

    get_grub_index;
    get_version;
    install;

    start_monitor if (defined $console);
    return monitor;
}

my $check_build_re = ".*:.*(warning|error|Error):.*";
my $utf8_quote = "\\x{e2}\\x{80}(\\x{98}|\\x{99})";

sub process_warning_line {
    my ($line) = @_;

    chomp $line;

    # for distcc heterogeneous systems, some compilers
    # do things differently causing warning lines
    # to be slightly different. This makes an attempt
    # to fixe those issues.

    # chop off the index into the line
    # using distcc, some compilers give different indexes
    # depending on white space
    $line =~ s/^(\s*\S+:\d+:)\d+/$1/;

    # Some compilers use UTF-8 extended for quotes and some don't.
    $line =~ s/$utf8_quote/'/g;

    return $line;
}

# Read buildlog and check against warnings file for any
# new warnings.
#
# Returns 1 if OK
#         0 otherwise
sub check_buildlog {
    return 1 if (!defined $warnings_file);

    my %warnings_list;

    # Failed builds should not reboot the target
    my $save_no_reboot = $no_reboot;
    $no_reboot = 1;

    if (-f $warnings_file) {
	open(IN, $warnings_file) or
	    dodie "Error opening $warnings_file";

	while (<IN>) {
	    if (/$check_build_re/) {
		my $warning = process_warning_line $_;
		
		$warnings_list{$warning} = 1;
	    }
	}
	close(IN);
    }

    # If warnings file didn't exist, and WARNINGS_FILE exist,
    # then we fail on any warning!

    open(IN, $buildlog) or dodie "Can't open $buildlog";
    while (<IN>) {
	if (/$check_build_re/) {
	    my $warning = process_warning_line $_;

	    if (!defined $warnings_list{$warning}) {
		fail "New warning found (not in $warnings_file)\n$_\n";
		$no_reboot = $save_no_reboot;
		return 0;
	    }
	}
    }
    $no_reboot = $save_no_reboot;
    close(IN);
}

sub check_patch_buildlog {
    my ($patch) = @_;

    my @files = `git show $patch | diffstat -l`;

    foreach my $file (@files) {
	chomp $file;
    }

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

sub apply_min_config {
    my $outconfig = "$output_config.new";

    # Read the config file and remove anything that
    # is in the force_config hash (from minconfig and others)
    # then add the force config back.

    doprint "Applying minimum configurations into $output_config.new\n";

    open (OUT, ">$outconfig") or
	dodie "Can't create $outconfig";

    if (-f $output_config) {
	open (IN, $output_config) or
	    dodie "Failed to open $output_config";
	while (<IN>) {
	    if (/^(# )?(CONFIG_[^\s=]*)/) {
		next if (defined($force_config{$2}));
	    }
	    print OUT;
	}
	close IN;
    }
    foreach my $config (keys %force_config) {
	print OUT "$force_config{$config}\n";
    }
    close OUT;

    run_command "mv $outconfig $output_config";
}

sub make_oldconfig {

    my @force_list = keys %force_config;

    if ($#force_list >= 0) {
	apply_min_config;
    }

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

# read a config file and use this to force new configs.
sub load_force_config {
    my ($config) = @_;

    doprint "Loading force configs from $config\n";
    open(IN, $config) or
	dodie "failed to read $config";
    while (<IN>) {
	chomp;
	if (/^(CONFIG[^\s=]*)(\s*=.*)/) {
	    $force_config{$1} = $_;
	} elsif (/^# (CONFIG_\S*) is not set/) {
	    $force_config{$1} = $_;
	}
    }
    close IN;
}

sub build {
    my ($type) = @_;

    unlink $buildlog;

    my $start_time = time;

    # Failed builds should not reboot the target
    my $save_no_reboot = $no_reboot;
    $no_reboot = 1;

    # Calculate a new version from here.
    $have_version = 0;

    if (defined($pre_build)) {
	my $ret = run_command $pre_build;
	if (!$ret && defined($pre_build_die) &&
	    $pre_build_die) {
	    dodie "failed to pre_build\n";
	}
    }

    if ($type =~ /^useconfig:(.*)/) {
	run_command "cp $1 $output_config" or
	    dodie "could not copy $1 to .config";

	$type = "oldconfig";
    }

    # old config can ask questions
    if ($type eq "oldconfig") {
	$type = "olddefconfig";

	# allow for empty configs
	run_command "touch $output_config";

	if (!$noclean) {
	    run_command "mv $output_config $outputdir/config_temp" or
		dodie "moving .config";

	    run_command "$make mrproper" or dodie "make mrproper";

	    run_command "mv $outputdir/config_temp $output_config" or
		dodie "moving config_temp";
	}

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
	load_force_config($minconfig);
    }

    if ($type ne "olddefconfig") {
	run_command "$make $type" or
	    dodie "failed make config";
    }
    # Run old config regardless, to enforce min configurations
    make_oldconfig;

    my $build_ret = run_command "$make $build_options", $buildlog;

    if (defined($post_build)) {
	# Because a post build may change the kernel version
	# do it now.
	get_version;
	my $ret = run_command $post_build;
	if (!$ret && defined($post_build_die) &&
	    $post_build_die) {
	    dodie "failed to post_build\n";
	}
    }

    if (!$build_ret) {
	# bisect may need this to pass
	if ($in_bisect) {
	    $no_reboot = $save_no_reboot;
	    return 0;
	}
	fail "failed build" and return 0;
    }

    $no_reboot = $save_no_reboot;

    my $end_time = time;
    $build_time = $end_time - $start_time;

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

    my $name = "";

    if (defined($test_name)) {
	$name = " ($test_name)";
    }

    print_times;

    doprint "\n\n*******************************************\n";
    doprint     "*******************************************\n";
    doprint     "KTEST RESULT: TEST $i$name SUCCESS!!!!         **\n";
    doprint     "*******************************************\n";
    doprint     "*******************************************\n";

    if (defined($store_successes)) {
        save_logs "success", $store_successes;
    }

    if ($i != $opt{"NUM_TESTS"} && !do_not_reboot) {
	doprint "Reboot and wait $sleep_time seconds\n";
	reboot_to_good $sleep_time;
    }

    if (defined($post_test)) {
	run_command $post_test;
    }
}

sub answer_bisect {
    for (;;) {
	doprint "Pass, fail, or skip? [p/f/s]";
	my $ans = <STDIN>;
	chomp $ans;
	if ($ans eq "p" || $ans eq "P") {
	    return 1;
	} elsif ($ans eq "f" || $ans eq "F") {
	    return 0;
	} elsif ($ans eq "s" || $ans eq "S") {
	    return -1;
	} else {
	    print "Please answer 'p', 'f', or 's'\n";
	}
    }
}

sub child_run_test {

    # child should have no power
    $reboot_on_error = 0;
    $poweroff_on_error = 0;
    $die_on_failure = 1;

    run_command $run_test, $testlog;

    exit $run_command_status;
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
    my $bug_ignored = 0;

    my $start_time = time;

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
	    doprint $line;

	    if ($full_line =~ /call trace:/i) {
		if ($ignore_errors) {
		    $bug_ignored = 1;
		} else {
		    $bug = 1;
		}
	    }

	    if ($full_line =~ /Kernel panic -/) {
		$bug = 1;
	    }

	    if ($line =~ /\n/) {
		$full_line = "";
	    }
	}
    } while (!$child_done && !$bug);

    if (!$bug && $bug_ignored) {
	doprint "WARNING: Call Trace detected but ignored due to IGNORE_ERRORS=1\n";
    }

    if ($bug) {
	my $failure_start = time;
	my $now;
	do {
	    $line = wait_for_input($monitor_fp, 1);
	    if (defined($line)) {
		doprint $line;
	    }
	    $now = time;
	    if ($now - $failure_start >= $stop_after_failure) {
		last;
	    }
	} while (defined($line));

	doprint "Detected kernel crash!\n";
	# kill the child with extreme prejudice
	kill 9, $child_pid;
    }

    waitpid $child_pid, 0;
    $child_exit = $? >> 8;

    my $end_time = time;
    $test_time = $end_time - $start_time;

    if (!$bug && $in_bisect) {
	if (defined($bisect_ret_good)) {
	    if ($child_exit == $bisect_ret_good) {
		return 1;
	    }
	}
	if (defined($bisect_ret_skip)) {
	    if ($child_exit == $bisect_ret_skip) {
		return -1;
	    }
	}
	if (defined($bisect_ret_abort)) {
	    if ($child_exit == $bisect_ret_abort) {
		fail "test abort" and return -2;
	    }
	}
	if (defined($bisect_ret_bad)) {
	    if ($child_exit == $bisect_ret_skip) {
		return 0;
	    }
	}
	if (defined($bisect_ret_default)) {
	    if ($bisect_ret_default eq "good") {
		return 1;
	    } elsif ($bisect_ret_default eq "bad") {
		return 0;
	    } elsif ($bisect_ret_default eq "skip") {
		return -1;
	    } elsif ($bisect_ret_default eq "abort") {
		return -2;
	    } else {
		fail "unknown default action: $bisect_ret_default"
		    and return -2;
	    }
	}
    }

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
	$bisect_bad_commit = $1;
	doprint "Found bad commit... $1\n";
	return 0;
    } else {
	# we already logged it, just print it now.
	print $output;
    }

    return 1;
}

sub bisect_reboot {
    doprint "Reboot and sleep $bisect_sleep_time seconds\n";
    reboot_to_good $bisect_sleep_time;
}

# returns 1 on success, 0 on failure, -1 on skip
sub run_bisect_test {
    my ($type, $buildtype) = @_;

    my $failed = 0;
    my $result;
    my $output;
    my $ret;

    $in_bisect = 1;

    build $buildtype or $failed = 1;

    if ($type ne "build") {
	if ($failed && $bisect_skip) {
	    $in_bisect = 0;
	    return -1;
	}
	dodie "Failed on build" if $failed;

	# Now boot the box
	start_monitor_and_install or $failed = 1;

	if ($type ne "boot") {
	    if ($failed && $bisect_skip) {
		end_monitor;
		bisect_reboot;
		$in_bisect = 0;
		return -1;
	    }
	    dodie "Failed on boot" if $failed;

	    do_run_test or $failed = 1;
	}
	end_monitor;
    }

    if ($failed) {
	$result = 0;
    } else {
	$result = 1;
    }

    # reboot the box to a kernel we can ssh to
    if ($type ne "build") {
	bisect_reboot;
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

    # If the user sets bisect_tries to less than 1, then no tries
    # is a success.
    my $ret = 1;

    # Still let the user manually decide that though.
    if ($bisect_tries < 1 && $bisect_manual) {
	$ret = answer_bisect;
    }

    for (my $i = 0; $i < $bisect_tries; $i++) {
	if ($bisect_tries > 1) {
	    my $t = $i + 1;
	    doprint("Running bisect trial $t of $bisect_tries:\n");
	}
	$ret = run_bisect_test $type, $buildtype;

	if ($bisect_manual) {
	    $ret = answer_bisect;
	}

	last if (!$ret);
    }

    # Are we looking for where it worked, not failed?
    if ($reverse_bisect && $ret >= 0) {
	$ret = !$ret;
    }

    if ($ret > 0) {
	return "good";
    } elsif ($ret == 0) {
	return  "bad";
    } elsif ($bisect_skip) {
	doprint "HIT A BAD COMMIT ... SKIPPING\n";
	return "skip";
    }
}

sub update_bisect_replay {
    my $tmp_log = "$tmpdir/ktest_bisect_log";
    run_command "git bisect log > $tmp_log" or
	dodie "can't create bisect log";
    return $tmp_log;
}

sub bisect {
    my ($i) = @_;

    my $result;

    dodie "BISECT_GOOD[$i] not defined\n"	if (!defined($bisect_good));
    dodie "BISECT_BAD[$i] not defined\n"	if (!defined($bisect_bad));
    dodie "BISECT_TYPE[$i] not defined\n"	if (!defined($bisect_type));

    my $good = $bisect_good;
    my $bad = $bisect_bad;
    my $type = $bisect_type;
    my $start = $bisect_start;
    my $replay = $bisect_replay;
    my $start_files = $bisect_files;

    if (defined($start_files)) {
	$start_files = " -- " . $start_files;
    } else {
	$start_files = "";
    }

    # convert to true sha1's
    $good = get_sha1($good);
    $bad = get_sha1($bad);

    if (defined($bisect_reverse) && $bisect_reverse == 1) {
	doprint "Performing a reverse bisect (bad is good, good is bad!)\n";
	$reverse_bisect = 1;
    } else {
	$reverse_bisect = 0;
    }

    # Can't have a test without having a test to run
    if ($type eq "test" && !defined($run_test)) {
	$type = "boot";
    }

    # Check if a bisect was running
    my $bisect_start_file = "$builddir/.git/BISECT_START";

    my $check = $bisect_check;
    my $do_check = defined($check) && $check ne "0";

    if ( -f $bisect_start_file ) {
	print "Bisect in progress found\n";
	if ($do_check) {
	    print " If you say yes, then no checks of good or bad will be done\n";
	}
	if (defined($replay)) {
	    print "** BISECT_REPLAY is defined in config file **";
	    print " Ignore config option and perform new git bisect log?\n";
	    if (read_ync " (yes, no, or cancel) ") {
		$replay = update_bisect_replay;
		$do_check = 0;
	    }
	} elsif (read_yn "read git log and continue?") {
	    $replay = update_bisect_replay;
	    $do_check = 0;
	}
    }

    if ($do_check) {

	# get current HEAD
	my $head = get_sha1("HEAD");

	if ($check ne "good") {
	    doprint "TESTING BISECT BAD [$bad]\n";
	    run_command "git checkout $bad" or
		dodie "Failed to checkout $bad";

	    $result = run_bisect $type;

	    if ($result ne "bad") {
		fail "Tested BISECT_BAD [$bad] and it succeeded" and return 0;
	    }
	}

	if ($check ne "bad") {
	    doprint "TESTING BISECT GOOD [$good]\n";
	    run_command "git checkout $good" or
		dodie "Failed to checkout $good";

	    $result = run_bisect $type;

	    if ($result ne "good") {
		fail "Tested BISECT_GOOD [$good] and it failed" and return 0;
	    }
	}

	# checkout where we started
	run_command "git checkout $head" or
	    dodie "Failed to checkout $head";
    }

    run_command "git bisect start$start_files" or
	dodie "could not start bisect";

    if (defined($replay)) {
	run_command "git bisect replay $replay" or
	    dodie "failed to run replay";
    } else {

	run_command "git bisect good $good" or
	    dodie "could not set bisect good to $good";

	run_git_bisect "git bisect bad $bad" or
	    dodie "could not set bisect bad to $bad";

    }

    if (defined($start)) {
	run_command "git checkout $start" or
	    dodie "failed to checkout $start";
    }

    my $test;
    do {
	$result = run_bisect $type;
	$test = run_git_bisect "git bisect $result";
	print_times;
    } while ($test);

    run_command "git bisect log" or
	dodie "could not capture git bisect log";

    run_command "git bisect reset" or
	dodie "could not reset git bisect";

    doprint "Bad commit was [$bisect_bad_commit]\n";

    success $i;
}

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

sub run_config_bisect_test {
    my ($type) = @_;

    my $ret = run_bisect_test $type, "oldconfig";

    if ($bisect_manual) {
	$ret = answer_bisect;
    }

    return $ret;
}

sub config_bisect_end {
    my ($good, $bad) = @_;
    my $diffexec = "diff -u";

    if (-f "$builddir/scripts/diffconfig") {
	$diffexec = "$builddir/scripts/diffconfig";
    }
    doprint "\n\n***************************************\n";
    doprint "No more config bisecting possible.\n";
    run_command "$diffexec $good $bad", 1;
    doprint "***************************************\n\n";
}

sub run_config_bisect {
    my ($good, $bad, $last_result) = @_;
    my $reset = "";
    my $cmd;
    my $ret;

    if (!length($last_result)) {
	$reset = "-r";
    }
    run_command "$config_bisect_exec $reset -b $outputdir $good $bad $last_result", 1;

    # config-bisect returns:
    #   0 if there is more to bisect
    #   1 for finding a good config
    #   2 if it can not find any more configs
    #  -1 (255) on error
    if ($run_command_status) {
	return $run_command_status;
    }

    $ret = run_config_bisect_test $config_bisect_type;
    if ($ret) {
        doprint "NEW GOOD CONFIG\n";
	# Return 3 for good config
	return 3;
    } else {
        doprint "NEW BAD CONFIG\n";
	# Return 4 for bad config
	return 4;
    }
}

sub config_bisect {
    my ($i) = @_;

    my $good_config;
    my $bad_config;

    my $type = $config_bisect_type;
    my $ret;

    $bad_config = $config_bisect;

    if (defined($config_bisect_good)) {
	$good_config = $config_bisect_good;
    } elsif (defined($minconfig)) {
	$good_config = $minconfig;
    } else {
	doprint "No config specified, checking if defconfig works";
	$ret = run_bisect_test $type, "defconfig";
	if (!$ret) {
	    fail "Have no good config to compare with, please set CONFIG_BISECT_GOOD";
	    return 1;
	}
	$good_config = $output_config;
    }

    if (!defined($config_bisect_exec)) {
	# First check the location that ktest.pl ran
	my @locations = ( "$pwd/config-bisect.pl",
			  "$dirname/config-bisect.pl",
			  "$builddir/tools/testing/ktest/config-bisect.pl",
			  undef );
	foreach my $loc (@locations) {
	    doprint "loc = $loc\n";
	    $config_bisect_exec = $loc;
	    last if (defined($config_bisect_exec && -x $config_bisect_exec));
	}
	if (!defined($config_bisect_exec)) {
	    fail "Could not find an executable config-bisect.pl\n",
		"  Set CONFIG_BISECT_EXEC to point to config-bisect.pl";
	    return 1;
	}
    }

    # we don't want min configs to cause issues here.
    doprint "Disabling 'MIN_CONFIG' for this test\n";
    undef $minconfig;

    my %good_configs;
    my %bad_configs;
    my %tmp_configs;

    if (-f "$tmpdir/good_config.tmp" || -f "$tmpdir/bad_config.tmp") {
	if (read_yn "Interrupted config-bisect. Continue (n - will start new)?") {
	    if (-f "$tmpdir/good_config.tmp") {
		$good_config = "$tmpdir/good_config.tmp";
	    } else {
		$good_config = "$tmpdir/good_config";
	    }
	    if (-f "$tmpdir/bad_config.tmp") {
		$bad_config = "$tmpdir/bad_config.tmp";
	    } else {
		$bad_config = "$tmpdir/bad_config";
	    }
	}
    }
    doprint "Run good configs through make oldconfig\n";
    assign_configs \%tmp_configs, $good_config;
    create_config "$good_config", \%tmp_configs;
    $good_config = "$tmpdir/good_config";
    system("cp $output_config $good_config") == 0 or dodie "cp good config";

    doprint "Run bad configs through make oldconfig\n";
    assign_configs \%tmp_configs, $bad_config;
    create_config "$bad_config", \%tmp_configs;
    $bad_config = "$tmpdir/bad_config";
    system("cp $output_config $bad_config") == 0 or dodie "cp bad config";

    if (defined($config_bisect_check) && $config_bisect_check ne "0") {
	if ($config_bisect_check ne "good") {
	    doprint "Testing bad config\n";

	    $ret = run_bisect_test $type, "useconfig:$bad_config";
	    if ($ret) {
		fail "Bad config succeeded when expected to fail!";
		return 0;
	    }
	}
	if ($config_bisect_check ne "bad") {
	    doprint "Testing good config\n";

	    $ret = run_bisect_test $type, "useconfig:$good_config";
	    if (!$ret) {
		fail "Good config failed when expected to succeed!";
		return 0;
	    }
	}
    }

    my $last_run = "";

    do {
	$ret = run_config_bisect $good_config, $bad_config, $last_run;
	if ($ret == 3) {
	    $last_run = "good";
	} elsif ($ret == 4) {
	    $last_run = "bad";
	}
	print_times;
    } while ($ret == 3 || $ret == 4);

    if ($ret == 2) {
        config_bisect_end "$good_config.tmp", "$bad_config.tmp";
    }

    return $ret if ($ret < 0);

    success $i;
}

sub patchcheck_reboot {
    doprint "Reboot and sleep $patchcheck_sleep_time seconds\n";
    reboot_to_good $patchcheck_sleep_time;
}

sub patchcheck {
    my ($i) = @_;

    dodie "PATCHCHECK_START[$i] not defined\n"
	if (!defined($patchcheck_start));
    dodie "PATCHCHECK_TYPE[$i] not defined\n"
	if (!defined($patchcheck_type));

    my $start = $patchcheck_start;

    my $cherry = $patchcheck_cherry;
    if (!defined($cherry)) {
	$cherry = 0;
    }

    my $end = "HEAD";
    if (defined($patchcheck_end)) {
	$end = $patchcheck_end;
    } elsif ($cherry) {
	dodie "PATCHCHECK_END must be defined with PATCHCHECK_CHERRY\n";
    }

    # Get the true sha1's since we can use things like HEAD~3
    $start = get_sha1($start);
    $end = get_sha1($end);

    my $type = $patchcheck_type;

    # Can't have a test without having a test to run
    if ($type eq "test" && !defined($run_test)) {
	$type = "boot";
    }

    if ($cherry) {
	open (IN, "git cherry -v $start $end|") or
	    dodie "could not get git list";
    } else {
	open (IN, "git log --pretty=oneline $end|") or
	    dodie "could not get git list";
    }

    my @list;

    while (<IN>) {
	chomp;
	# git cherry adds a '+' we want to remove
	s/^\+ //;
	$list[$#list+1] = $_;
	last if (/^$start/);
    }
    close(IN);

    if (!$cherry) {
	if ($list[$#list] !~ /^$start/) {
	    fail "SHA1 $start not found";
	}

	# go backwards in the list
	@list = reverse @list;
    }

    doprint("Going to test the following commits:\n");
    foreach my $l (@list) {
	doprint "$l\n";
    }

    my $save_clean = $noclean;
    my %ignored_warnings;

    if (defined($ignore_warnings)) {
	foreach my $sha1 (split /\s+/, $ignore_warnings) {
	    $ignored_warnings{$sha1} = 1;
	}
    }

    $in_patchcheck = 1;
    foreach my $item (@list) {
	my $sha1 = $item;
	$sha1 =~ s/^([[:xdigit:]]+).*/$1/;

	doprint "\nProcessing commit \"$item\"\n\n";

	run_command "git checkout $sha1" or
	    dodie "Failed to checkout $sha1";

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

	# No need to do per patch checking if warnings file exists
	if (!defined($warnings_file) && !defined($ignored_warnings{$sha1})) {
	    check_patch_buildlog $sha1 or return 0;
	}

	check_buildlog or return 0;

	next if ($type eq "build");

	my $failed = 0;

	start_monitor_and_install or $failed = 1;

	if (!$failed && $type ne "boot"){
	    do_run_test or $failed = 1;
	}
	end_monitor;
	if ($failed) {
	    print_times;
	    return 0;
	}
	patchcheck_reboot;
	print_times;
    }
    $in_patchcheck = 0;
    success $i;

    return 1;
}

my %depends;
my %depcount;
my $iflevel = 0;
my @ifdeps;

# prevent recursion
my %read_kconfigs;

sub add_dep {
    # $config depends on $dep
    my ($config, $dep) = @_;

    if (defined($depends{$config})) {
	$depends{$config} .= " " . $dep;
    } else {
	$depends{$config} = $dep;
    }

    # record the number of configs depending on $dep
    if (defined $depcount{$dep}) {
	$depcount{$dep}++;
    } else {
	$depcount{$dep} = 1;
    } 
}

# taken from streamline_config.pl
sub read_kconfig {
    my ($kconfig) = @_;

    my $state = "NONE";
    my $config;
    my @kconfigs;

    my $cont = 0;
    my $line;


    if (! -f $kconfig) {
	doprint "file $kconfig does not exist, skipping\n";
	return;
    }

    open(KIN, "$kconfig")
	or dodie "Can't open $kconfig";
    while (<KIN>) {
	chomp;

	# Make sure that lines ending with \ continue
	if ($cont) {
	    $_ = $line . " " . $_;
	}

	if (s/\\$//) {
	    $cont = 1;
	    $line = $_;
	    next;
	}

	$cont = 0;

	# collect any Kconfig sources
	if (/^source\s*"(.*)"/) {
	    $kconfigs[$#kconfigs+1] = $1;
	}

	# configs found
	if (/^\s*(menu)?config\s+(\S+)\s*$/) {
	    $state = "NEW";
	    $config = $2;

	    for (my $i = 0; $i < $iflevel; $i++) {
		add_dep $config, $ifdeps[$i];
	    }

	# collect the depends for the config
	} elsif ($state eq "NEW" && /^\s*depends\s+on\s+(.*)$/) {

	    add_dep $config, $1;

	# Get the configs that select this config
	} elsif ($state eq "NEW" && /^\s*select\s+(\S+)/) {

	    # selected by depends on config
	    add_dep $1, $config;

	# Check for if statements
	} elsif (/^if\s+(.*\S)\s*$/) {
	    my $deps = $1;
	    # remove beginning and ending non text
	    $deps =~ s/^[^a-zA-Z0-9_]*//;
	    $deps =~ s/[^a-zA-Z0-9_]*$//;

	    my @deps = split /[^a-zA-Z0-9_]+/, $deps;

	    $ifdeps[$iflevel++] = join ':', @deps;

	} elsif (/^endif/) {

	    $iflevel-- if ($iflevel);

	# stop on "help"
	} elsif (/^\s*help\s*$/) {
	    $state = "NONE";
	}
    }
    close(KIN);

    # read in any configs that were found.
    foreach $kconfig (@kconfigs) {
	if (!defined($read_kconfigs{$kconfig})) {
	    $read_kconfigs{$kconfig} = 1;
	    read_kconfig("$builddir/$kconfig");
	}
    }
}

sub read_depends {
    # find out which arch this is by the kconfig file
    open (IN, $output_config)
	or dodie "Failed to read $output_config";
    my $arch;
    while (<IN>) {
	if (m,Linux/(\S+)\s+\S+\s+Kernel Configuration,) {
	    $arch = $1;
	    last;
	}
    }
    close IN;

    if (!defined($arch)) {
	doprint "Could not find arch from config file\n";
	doprint "no dependencies used\n";
	return;
    }

    # arch is really the subarch, we need to know
    # what directory to look at.
    if ($arch eq "i386" || $arch eq "x86_64") {
	$arch = "x86";
    }

    my $kconfig = "$builddir/arch/$arch/Kconfig";

    if (! -f $kconfig && $arch =~ /\d$/) {
	my $orig = $arch;
 	# some subarchs have numbers, truncate them
	$arch =~ s/\d*$//;
	$kconfig = "$builddir/arch/$arch/Kconfig";
	if (! -f $kconfig) {
	    doprint "No idea what arch dir $orig is for\n";
	    doprint "no dependencies used\n";
	    return;
	}
    }

    read_kconfig($kconfig);
}

sub make_new_config {
    my @configs = @_;

    open (OUT, ">$output_config")
	or dodie "Failed to write $output_config";

    foreach my $config (@configs) {
	print OUT "$config\n";
    }
    close OUT;
}

sub chomp_config {
    my ($config) = @_;

    $config =~ s/CONFIG_//;

    return $config;
}

sub get_depends {
    my ($dep) = @_;

    my $kconfig = chomp_config $dep;

    $dep = $depends{"$kconfig"};

    # the dep string we have saves the dependencies as they
    # were found, including expressions like ! && ||. We
    # want to split this out into just an array of configs.

    my $valid = "A-Za-z_0-9";

    my @configs;

    while ($dep =~ /[$valid]/) {

	if ($dep =~ /^[^$valid]*([$valid]+)/) {
	    my $conf = "CONFIG_" . $1;

	    $configs[$#configs + 1] = $conf;

	    $dep =~ s/^[^$valid]*[$valid]+//;
	} else {
	    dodie "this should never happen";
	}
    }

    return @configs;
}

my %min_configs;
my %keep_configs;
my %save_configs;
my %processed_configs;
my %nochange_config;

sub test_this_config {
    my ($config) = @_;

    my $found;

    # if we already processed this config, skip it
    if (defined($processed_configs{$config})) {
	return undef;
    }
    $processed_configs{$config} = 1;

    # if this config failed during this round, skip it
    if (defined($nochange_config{$config})) {
	return undef;
    }

    my $kconfig = chomp_config $config;

    # Test dependencies first
    if (defined($depends{"$kconfig"})) {
	my @parents = get_depends $config;
	foreach my $parent (@parents) {
	    # if the parent is in the min config, check it first
	    next if (!defined($min_configs{$parent}));
	    $found = test_this_config($parent);
	    if (defined($found)) {
		return $found;
	    }
	}
    }

    # Remove this config from the list of configs
    # do a make olddefconfig and then read the resulting
    # .config to make sure it is missing the config that
    # we had before
    my %configs = %min_configs;
    delete $configs{$config};
    make_new_config ((values %configs), (values %keep_configs));
    make_oldconfig;
    undef %configs;
    assign_configs \%configs, $output_config;

    if (!defined($configs{$config}) || $configs{$config} =~ /^#/) {
	return $config;
    }

    doprint "disabling config $config did not change .config\n";

    $nochange_config{$config} = 1;

    return undef;
}

sub make_min_config {
    my ($i) = @_;

    my $type = $minconfig_type;
    if ($type ne "boot" && $type ne "test") {
	fail "Invalid MIN_CONFIG_TYPE '$minconfig_type'\n" .
	    " make_min_config works only with 'boot' and 'test'\n" and return;
    }

    if (!defined($output_minconfig)) {
	fail "OUTPUT_MIN_CONFIG not defined" and return;
    }

    # If output_minconfig exists, and the start_minconfig
    # came from min_config, than ask if we should use
    # that instead.
    if (-f $output_minconfig && !$start_minconfig_defined) {
	print "$output_minconfig exists\n";
	if (!defined($use_output_minconfig)) {
	    if (read_yn " Use it as minconfig?") {
		$start_minconfig = $output_minconfig;
	    }
	} elsif ($use_output_minconfig > 0) {
	    doprint "Using $output_minconfig as MIN_CONFIG\n";
	    $start_minconfig = $output_minconfig;
	} else {
	    doprint "Set to still use MIN_CONFIG as starting point\n";
	}
    }

    if (!defined($start_minconfig)) {
	fail "START_MIN_CONFIG or MIN_CONFIG not defined" and return;
    }

    my $temp_config = "$tmpdir/temp_config";

    # First things first. We build an allnoconfig to find
    # out what the defaults are that we can't touch.
    # Some are selections, but we really can't handle selections.

    my $save_minconfig = $minconfig;
    undef $minconfig;

    run_command "$make allnoconfig" or return 0;

    read_depends;

    process_config_ignore $output_config;

    undef %save_configs;
    undef %min_configs;

    if (defined($ignore_config)) {
	# make sure the file exists
	`touch $ignore_config`;
	assign_configs \%save_configs, $ignore_config;
    }

    %keep_configs = %save_configs;

    doprint "Load initial configs from $start_minconfig\n";

    # Look at the current min configs, and save off all the
    # ones that were set via the allnoconfig
    assign_configs \%min_configs, $start_minconfig;

    my @config_keys = keys %min_configs;

    # All configs need a depcount
    foreach my $config (@config_keys) {
	my $kconfig = chomp_config $config;
	if (!defined $depcount{$kconfig}) {
		$depcount{$kconfig} = 0;
	}
    }

    # Remove anything that was set by the make allnoconfig
    # we shouldn't need them as they get set for us anyway.
    foreach my $config (@config_keys) {
	# Remove anything in the ignore_config
	if (defined($keep_configs{$config})) {
	    my $file = $ignore_config;
	    $file =~ s,.*/(.*?)$,$1,;
	    doprint "$config set by $file ... ignored\n";
	    delete $min_configs{$config};
	    next;
	}
	# But make sure the settings are the same. If a min config
	# sets a selection, we do not want to get rid of it if
	# it is not the same as what we have. Just move it into
	# the keep configs.
	if (defined($config_ignore{$config})) {
	    if ($config_ignore{$config} ne $min_configs{$config}) {
		doprint "$config is in allnoconfig as '$config_ignore{$config}'";
		doprint " but it is '$min_configs{$config}' in minconfig .. keeping\n";
		$keep_configs{$config} = $min_configs{$config};
	    } else {
		doprint "$config set by allnoconfig ... ignored\n";
	    }
	    delete $min_configs{$config};
	}
    }

    my $done = 0;
    my $take_two = 0;

    while (!$done) {

	my $config;
	my $found;

	# Now disable each config one by one and do a make oldconfig
	# till we find a config that changes our list.

	my @test_configs = keys %min_configs;

	# Sort keys by who is most dependent on
	@test_configs = sort  { $depcount{chomp_config($b)} <=> $depcount{chomp_config($a)} }
			  @test_configs ;

	# Put configs that did not modify the config at the end.
	my $reset = 1;
	for (my $i = 0; $i < $#test_configs; $i++) {
	    if (!defined($nochange_config{$test_configs[0]})) {
		$reset = 0;
		last;
	    }
	    # This config didn't change the .config last time.
	    # Place it at the end
	    my $config = shift @test_configs;
	    push @test_configs, $config;
	}

	# if every test config has failed to modify the .config file
	# in the past, then reset and start over.
	if ($reset) {
	    undef %nochange_config;
	}

	undef %processed_configs;

	foreach my $config (@test_configs) {

	    $found = test_this_config $config;

	    last if (defined($found));

	    # oh well, try another config
	}

	if (!defined($found)) {
	    # we could have failed due to the nochange_config hash
	    # reset and try again
	    if (!$take_two) {
		undef %nochange_config;
		$take_two = 1;
		next;
	    }
	    doprint "No more configs found that we can disable\n";
	    $done = 1;
	    last;
	}
	$take_two = 0;

	$config = $found;

	doprint "Test with $config disabled\n";

	# set in_bisect to keep build and monitor from dieing
	$in_bisect = 1;

	my $failed = 0;
	build "oldconfig" or $failed = 1;
	if (!$failed) {
		start_monitor_and_install or $failed = 1;

		if ($type eq "test" && !$failed) {
		    do_run_test or $failed = 1;
		}

		end_monitor;
	}

	$in_bisect = 0;

	if ($failed) {
	    doprint "$min_configs{$config} is needed to boot the box... keeping\n";
	    # this config is needed, add it to the ignore list.
	    $keep_configs{$config} = $min_configs{$config};
	    $save_configs{$config} = $min_configs{$config};
	    delete $min_configs{$config};

	    # update new ignore configs
	    if (defined($ignore_config)) {
		open (OUT, ">$temp_config")
		    or dodie "Can't write to $temp_config";
		foreach my $config (keys %save_configs) {
		    print OUT "$save_configs{$config}\n";
		}
		close OUT;
		run_command "mv $temp_config $ignore_config" or
		    dodie "failed to copy update to $ignore_config";
	    }

	} else {
	    # We booted without this config, remove it from the minconfigs.
	    doprint "$config is not needed, disabling\n";

	    delete $min_configs{$config};

	    # Also disable anything that is not enabled in this config
	    my %configs;
	    assign_configs \%configs, $output_config;
	    my @config_keys = keys %min_configs;
	    foreach my $config (@config_keys) {
		if (!defined($configs{$config})) {
		    doprint "$config is not set, disabling\n";
		    delete $min_configs{$config};
		}
	    }

	    # Save off all the current mandatory configs
	    open (OUT, ">$temp_config")
		or dodie "Can't write to $temp_config";
	    foreach my $config (keys %keep_configs) {
		print OUT "$keep_configs{$config}\n";
	    }
	    foreach my $config (keys %min_configs) {
		print OUT "$min_configs{$config}\n";
	    }
	    close OUT;

	    run_command "mv $temp_config $output_minconfig" or
		dodie "failed to copy update to $output_minconfig";
	}

	doprint "Reboot and wait $sleep_time seconds\n";
	reboot_to_good $sleep_time;
    }

    success $i;
    return 1;
}

sub make_warnings_file {
    my ($i) = @_;

    if (!defined($warnings_file)) {
	dodie "Must define WARNINGS_FILE for make_warnings_file test";
    }

    if ($build_type eq "nobuild") {
	dodie "BUILD_TYPE can not be 'nobuild' for make_warnings_file test";
    }

    build $build_type or dodie "Failed to build";

    open(OUT, ">$warnings_file") or dodie "Can't create $warnings_file";

    open(IN, $buildlog) or dodie "Can't open $buildlog";
    while (<IN>) {

	# Some compilers use UTF-8 extended for quotes
	# for distcc heterogeneous systems, this causes issues
	s/$utf8_quote/'/g;

	if (/$check_build_re/) {
	    print OUT;
	}
    }
    close(IN);

    close(OUT);

    success $i;
}

$#ARGV < 1 or die "ktest.pl version: $VERSION\n   usage: ktest.pl [config-file]\n";

if ($#ARGV == 0) {
    $ktest_config = $ARGV[0];
    if (! -f $ktest_config) {
	print "$ktest_config does not exist.\n";
	if (!read_yn "Create it?") {
	    exit 0;
	}
    }
}

if (! -f $ktest_config) {
    $newconfig = 1;
    get_test_case;
    open(OUT, ">$ktest_config") or die "Can not create $ktest_config";
    print OUT << "EOF"
# Generated by ktest.pl
#

# PWD is a ktest.pl variable that will result in the process working
# directory that ktest.pl is executed in.

# THIS_DIR is automatically assigned the PWD of the path that generated
# the config file. It is best to use this variable when assigning other
# directory paths within this directory. This allows you to easily
# move the test cases to other locations or to other machines.
#
THIS_DIR := $variable{"PWD"}

# Define each test with TEST_START
# The config options below it will override the defaults
TEST_START
TEST_TYPE = $default{"TEST_TYPE"}

DEFAULTS
EOF
;
    close(OUT);
}
read_config $ktest_config;

if (defined($opt{"LOG_FILE"})) {
    $opt{"LOG_FILE"} = eval_option("LOG_FILE", $opt{"LOG_FILE"}, -1);
}

# Append any configs entered in manually to the config file.
my @new_configs = keys %entered_configs;
if ($#new_configs >= 0) {
    print "\nAppending entered in configs to $ktest_config\n";
    open(OUT, ">>$ktest_config") or die "Can not append to $ktest_config";
    foreach my $config (@new_configs) {
	print OUT "$config = $entered_configs{$config}\n";
	$opt{$config} = process_variables($entered_configs{$config});
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

sub option_defined {
    my ($option) = @_;

    if (defined($opt{$option}) && $opt{$option} !~ /^\s*$/) {
	return 1;
    }

    return 0;
}

sub __set_test_option {
    my ($name, $i) = @_;

    my $option = "$name\[$i\]";

    if (option_defined($option)) {
	return $opt{$option};
    }

    foreach my $test (keys %repeat_tests) {
	if ($i >= $test &&
	    $i < $test + $repeat_tests{$test}) {
	    $option = "$name\[$test\]";
	    if (option_defined($option)) {
		return $opt{$option};
	    }
	}
    }

    if (option_defined($name)) {
	return $opt{$name};
    }

    return undef;
}

sub set_test_option {
    my ($name, $i) = @_;

    my $option = __set_test_option($name, $i);
    return $option if (!defined($option));

    return eval_option($name, $option, $i);
}

sub find_mailer {
    my ($mailer) = @_;

    my @paths = split /:/, $ENV{PATH};

    # sendmail is usually in /usr/sbin
    $paths[$#paths + 1] = "/usr/sbin";

    foreach my $path (@paths) {
	if (-x "$path/$mailer") {
	    return $path;
	}
    }

    return undef;
}

sub do_send_mail {
    my ($subject, $message) = @_;

    if (!defined($mail_path)) {
	# find the mailer
	$mail_path = find_mailer $mailer;
	if (!defined($mail_path)) {
	    die "\nCan not find $mailer in PATH\n";
	}
    }

    if (!defined($mail_command)) {
	if ($mailer eq "mail" || $mailer eq "mailx") {
	    $mail_command = "\$MAIL_PATH/\$MAILER -s \'\$SUBJECT\' \$MAILTO <<< \'\$MESSAGE\'";
	} elsif ($mailer eq "sendmail" ) {
	    $mail_command =  "echo \'Subject: \$SUBJECT\n\n\$MESSAGE\' | \$MAIL_PATH/\$MAILER -t \$MAILTO";
	} else {
	    die "\nYour mailer: $mailer is not supported.\n";
	}
    }

    $mail_command =~ s/\$MAILER/$mailer/g;
    $mail_command =~ s/\$MAIL_PATH/$mail_path/g;
    $mail_command =~ s/\$MAILTO/$mailto/g;
    $mail_command =~ s/\$SUBJECT/$subject/g;
    $mail_command =~ s/\$MESSAGE/$message/g;

    run_command $mail_command;
}

sub send_email {

    if (defined($mailto)) {
	if (!defined($mailer)) {
	    doprint "No email sent: email or mailer not specified in config.\n";
	    return;
	}
	do_send_mail @_;
    }
}

sub cancel_test {
    if ($email_when_canceled) {
        send_email("KTEST: Your [$test_type] test was cancelled",
                "Your test started at $script_start_time was cancelled: sig int");
    }
    die "\nCaught Sig Int, test interrupted: $!\n"
}

$SIG{INT} = qw(cancel_test);

# First we need to do is the builds
for (my $i = 1; $i <= $opt{"NUM_TESTS"}; $i++) {

    # Do not reboot on failing test options
    $no_reboot = 1;
    $reboot_success = 0;

    $have_version = 0;

    $iteration = $i;

    $build_time = 0;
    $install_time = 0;
    $reboot_time = 0;
    $test_time = 0;

    undef %force_config;

    my $makecmd = set_test_option("MAKE_CMD", $i);

    $outputdir = set_test_option("OUTPUT_DIR", $i);
    $builddir = set_test_option("BUILD_DIR", $i);

    chdir $builddir || dodie "can't change directory to $builddir";

    if (!-d $outputdir) {
	mkpath($outputdir) or
	    dodie "can't create $outputdir";
    }

    $make = "$makecmd O=$outputdir";

    # Load all the options into their mapped variable names
    foreach my $opt (keys %option_map) {
	${$option_map{$opt}} = set_test_option($opt, $i);
    }

    $start_minconfig_defined = 1;

    # The first test may override the PRE_KTEST option
    if ($i == 1) {
        if (defined($pre_ktest)) {
            doprint "\n";
            run_command $pre_ktest;
        }
        if ($email_when_started) {
            send_email("KTEST: Your [$test_type] test was started",
                "Your test was started on $script_start_time");
        }
    }

    # Any test can override the POST_KTEST option
    # The last test takes precedence.
    if (defined($post_ktest)) {
	$final_post_ktest = $post_ktest;
    }

    if (!defined($start_minconfig)) {
	$start_minconfig_defined = 0;
	$start_minconfig = $minconfig;
    }

    if (!-d $tmpdir) {
	mkpath($tmpdir) or
	    dodie "can't create $tmpdir";
    }

    $ENV{"SSH_USER"} = $ssh_user;
    $ENV{"MACHINE"} = $machine;

    $buildlog = "$tmpdir/buildlog-$machine";
    $testlog = "$tmpdir/testlog-$machine";
    $dmesg = "$tmpdir/dmesg-$machine";
    $output_config = "$outputdir/.config";

    if (!$buildonly) {
	$target = "$ssh_user\@$machine";
	if ($reboot_type eq "grub") {
	    dodie "GRUB_MENU not defined" if (!defined($grub_menu));
	} elsif ($reboot_type eq "grub2") {
	    dodie "GRUB_MENU not defined" if (!defined($grub_menu));
	    dodie "GRUB_FILE not defined" if (!defined($grub_file));
	} elsif ($reboot_type eq "syslinux") {
	    dodie "SYSLINUX_LABEL not defined" if (!defined($syslinux_label));
	}
    }

    my $run_type = $build_type;
    if ($test_type eq "patchcheck") {
	$run_type = $patchcheck_type;
    } elsif ($test_type eq "bisect") {
	$run_type = $bisect_type;
    } elsif ($test_type eq "config_bisect") {
	$run_type = $config_bisect_type;
    } elsif ($test_type eq "make_min_config") {
	$run_type = "";
    } elsif ($test_type eq "make_warnings_file") {
	$run_type = "";
    }

    # mistake in config file?
    if (!defined($run_type)) {
	$run_type = "ERROR";
    }

    my $installme = "";
    $installme = " no_install" if ($no_install);

    my $name = "";

    if (defined($test_name)) {
	$name = " ($test_name)";
    }

    doprint "\n\n";
    doprint "RUNNING TEST $i of $opt{NUM_TESTS}$name with option $test_type $run_type$installme\n\n";

    if (defined($pre_test)) {
	run_command $pre_test;
    }

    unlink $dmesg;
    unlink $buildlog;
    unlink $testlog;

    if (defined($addconfig)) {
	my $min = $minconfig;
	if (!defined($minconfig)) {
	    $min = "";
	}
	run_command "cat $addconfig $min > $tmpdir/add_config" or
	    dodie "Failed to create temp config";
	$minconfig = "$tmpdir/add_config";
    }

    if (defined($checkout)) {
	run_command "git checkout $checkout" or
	    dodie "failed to checkout $checkout";
    }

    $no_reboot = 0;

    # A test may opt to not reboot the box
    if ($reboot_on_success) {
	$reboot_success = 1;
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
    } elsif ($test_type eq "make_min_config") {
	make_min_config $i;
	next;
    } elsif ($test_type eq "make_warnings_file") {
	$no_reboot = 1;
	make_warnings_file $i;
	next;
    }

    if ($build_type ne "nobuild") {
	build $build_type or next;
	check_buildlog or next;
    }

    if ($test_type eq "install") {
	get_version;
	install;
	success $i;
	next;
    }

    if ($test_type ne "build") {
	my $failed = 0;
	start_monitor_and_install or $failed = 1;

	if (!$failed && $test_type ne "boot" && defined($run_test)) {
	    do_run_test or $failed = 1;
	}
	end_monitor;
	if ($failed) {
	    print_times;
	    next;
	}
    }

    print_times;

    success $i;
}

if (defined($final_post_ktest)) {
    run_command $final_post_ktest;
}

if ($opt{"POWEROFF_ON_SUCCESS"}) {
    halt;
} elsif ($opt{"REBOOT_ON_SUCCESS"} && !do_not_reboot && $reboot_success) {
    reboot_to_good;
} elsif (defined($switch_to_good)) {
    # still need to get to the good kernel
    run_command $switch_to_good;
}


doprint "\n    $successes of $opt{NUM_TESTS} tests were successful\n\n";

if ($email_when_finished) {
    send_email("KTEST: Your [$test_type] test has finished!",
            "$successes of $opt{NUM_TESTS} tests started at $script_start_time were successful!");
}
exit 0;
