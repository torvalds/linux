#!/usr/bin/perl -w
#
# Copyright 2005-2009 - Steven Rostedt
# Licensed under the terms of the GNU GPL License version 2
#
#  It's simple enough to figure out how this works.
#  If not, then you can ask me at stripconfig@goodmis.org
#
# What it does?
#
#   If you have installed a Linux kernel from a distribution
#   that turns on way too many modules than you need, and
#   you only want the modules you use, then this program
#   is perfect for you.
#
#   It gives you the ability to turn off all the modules that are
#   not loaded on your system.
#
# Howto:
#
#  1. Boot up the kernel that you want to stream line the config on.
#  2. Change directory to the directory holding the source of the
#       kernel that you just booted.
#  3. Copy the configuraton file to this directory as .config
#  4. Have all your devices that you need modules for connected and
#      operational (make sure that their corresponding modules are loaded)
#  5. Run this script redirecting the output to some other file
#       like config_strip.
#  6. Back up your old config (if you want too).
#  7. copy the config_strip file to .config
#  8. Run "make oldconfig"
#
#  Now your kernel is ready to be built with only the modules that
#  are loaded.
#
# Here's what I did with my Debian distribution.
#
#    cd /usr/src/linux-2.6.10
#    cp /boot/config-2.6.10-1-686-smp .config
#    ~/bin/streamline_config > config_strip
#    mv .config config_sav
#    mv config_strip .config
#    make oldconfig
#
use strict;
use Getopt::Long;

# set the environment variable LOCALMODCONFIG_DEBUG to get
# debug output.
my $debugprint = 0;
$debugprint = 1 if (defined($ENV{LOCALMODCONFIG_DEBUG}));

sub dprint {
    return if (!$debugprint);
    print STDERR @_;
}

my $config = ".config";

my $uname = `uname -r`;
chomp $uname;

my @searchconfigs = (
	{
	    "file" => ".config",
	    "exec" => "cat",
	},
	{
	    "file" => "/proc/config.gz",
	    "exec" => "zcat",
	},
	{
	    "file" => "/boot/config-$uname",
	    "exec" => "cat",
	},
	{
	    "file" => "/boot/vmlinuz-$uname",
	    "exec" => "scripts/extract-ikconfig",
	    "test" => "scripts/extract-ikconfig",
	},
	{
	    "file" => "vmlinux",
	    "exec" => "scripts/extract-ikconfig",
	    "test" => "scripts/extract-ikconfig",
	},
	{
	    "file" => "/lib/modules/$uname/kernel/kernel/configs.ko",
	    "exec" => "scripts/extract-ikconfig",
	    "test" => "scripts/extract-ikconfig",
	},
	{
	    "file" => "kernel/configs.ko",
	    "exec" => "scripts/extract-ikconfig",
	    "test" => "scripts/extract-ikconfig",
	},
	{
	    "file" => "kernel/configs.o",
	    "exec" => "scripts/extract-ikconfig",
	    "test" => "scripts/extract-ikconfig",
	},
);

sub read_config {
    foreach my $conf (@searchconfigs) {
	my $file = $conf->{"file"};

	next if ( ! -f "$file");

	if (defined($conf->{"test"})) {
	    `$conf->{"test"} $conf->{"file"} 2>/dev/null`;
	    next if ($?);
	}

	my $exec = $conf->{"exec"};

	print STDERR "using config: '$file'\n";

	open(my $infile, '-|', "$exec $file") || die "Failed to run $exec $file";
	my @x = <$infile>;
	close $infile;
	return @x;
    }
    die "No config file found";
}

my @config_file = read_config;

# Parse options
my $localmodconfig = 0;
my $localyesconfig = 0;

GetOptions("localmodconfig" => \$localmodconfig,
	   "localyesconfig" => \$localyesconfig);

# Get the build source and top level Kconfig file (passed in)
my $ksource = ($ARGV[0] ? $ARGV[0] : '.');
my $kconfig = $ARGV[1];
my $lsmod_file = $ENV{'LSMOD'};

my @makefiles = `find $ksource -name Makefile -or -name Kbuild 2>/dev/null`;
chomp @makefiles;

my %depends;
my %selects;
my %prompts;
my %objects;
my $var;
my $iflevel = 0;
my @ifdeps;

# prevent recursion
my %read_kconfigs;

sub read_kconfig {
    my ($kconfig) = @_;

    my $state = "NONE";
    my $config;

    my $cont = 0;
    my $line;

    my $source = "$ksource/$kconfig";
    my $last_source = "";

    # Check for any environment variables used
    while ($source =~ /\$(\w+)/ && $last_source ne $source) {
	my $env = $1;
	$last_source = $source;
	$source =~ s/\$$env/$ENV{$env}/;
    }

    open(my $kinfile, '<', $source) || die "Can't open $kconfig";
    while (<$kinfile>) {
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
	if (/^source\s+"?([^"]+)/) {
	    my $kconfig = $1;
	    # prevent reading twice.
	    if (!defined($read_kconfigs{$kconfig})) {
		$read_kconfigs{$kconfig} = 1;
		read_kconfig($kconfig);
	    }
	    next;
	}

	# configs found
	if (/^\s*(menu)?config\s+(\S+)\s*$/) {
	    $state = "NEW";
	    $config = $2;

	    # Add depends for 'if' nesting
	    for (my $i = 0; $i < $iflevel; $i++) {
		if ($i) {
		    $depends{$config} .= " " . $ifdeps[$i];
		} else {
		    $depends{$config} = $ifdeps[$i];
		}
		$state = "DEP";
	    }

	# collect the depends for the config
	} elsif ($state eq "NEW" && /^\s*depends\s+on\s+(.*)$/) {
	    $state = "DEP";
	    $depends{$config} = $1;
	} elsif ($state eq "DEP" && /^\s*depends\s+on\s+(.*)$/) {
	    $depends{$config} .= " " . $1;
	} elsif ($state eq "DEP" && /^\s*def(_(bool|tristate)|ault)\s+(\S.*)$/) {
	    my $dep = $3;
	    if ($dep !~ /^\s*(y|m|n)\s*$/) {
		$dep =~ s/.*\sif\s+//;
		$depends{$config} .= " " . $dep;
		dprint "Added default depends $dep to $config\n";
	    }

	# Get the configs that select this config
	} elsif ($state ne "NONE" && /^\s*select\s+(\S+)/) {
	    my $conf = $1;
	    if (defined($selects{$conf})) {
		$selects{$conf} .= " " . $config;
	    } else {
		$selects{$conf} = $config;
	    }

	# configs without prompts must be selected
	} elsif ($state ne "NONE" && /^\s*(tristate\s+\S|prompt\b)/) {
	    # note if the config has a prompt
	    $prompts{$config} = 1;

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

	# stop on "help" and keywords that end a menu entry
	} elsif (/^\s*(---)?help(---)?\s*$/ || /^(comment|choice|menu)\b/) {
	    $state = "NONE";
	}
    }
    close($kinfile);
}

if ($kconfig) {
    read_kconfig($kconfig);
}

# Makefiles can use variables to define their dependencies
sub convert_vars {
    my ($line, %vars) = @_;

    my $process = "";

    while ($line =~ s/^(.*?)(\$\((.*?)\))//) {
	my $start = $1;
	my $variable = $2;
	my $var = $3;

	if (defined($vars{$var})) {
	    $process .= $start . $vars{$var};
	} else {
	    $process .= $start . $variable;
	}
    }

    $process .= $line;

    return $process;
}

# Read all Makefiles to map the configs to the objects
foreach my $makefile (@makefiles) {

    my $line = "";
    my %make_vars;

    open(my $infile, '<', $makefile) || die "Can't open $makefile";
    while (<$infile>) {
	# if this line ends with a backslash, continue
	chomp;
	if (/^(.*)\\$/) {
	    $line .= $1;
	    next;
	}

	$line .= $_;
	$_ = $line;
	$line = "";

	my $objs;

	# Convert variables in a line (could define configs)
	$_ = convert_vars($_, %make_vars);

	# collect objects after obj-$(CONFIG_FOO_BAR)
	if (/obj-\$\((CONFIG_[^\)]*)\)\s*[+:]?=\s*(.*)/) {
	    $var = $1;
	    $objs = $2;

	# check if variables are set
	} elsif (/^\s*(\S+)\s*[:]?=\s*(.*\S)/) {
	    $make_vars{$1} = $2;
	}
	if (defined($objs)) {
	    foreach my $obj (split /\s+/,$objs) {
		$obj =~ s/-/_/g;
		if ($obj =~ /(.*)\.o$/) {
		    # Objects may be enabled by more than one config.
		    # Store configs in an array.
		    my @arr;

		    if (defined($objects{$1})) {
			@arr = @{$objects{$1}};
		    }

		    $arr[$#arr+1] = $var;

		    # The objects have a hash mapping to a reference
		    # of an array of configs.
		    $objects{$1} = \@arr;
		}
	    }
	}
    }
    close($infile);
}

my %modules;
my $linfile;

if (defined($lsmod_file)) {
    if ( ! -f $lsmod_file) {
	if ( -f $ENV{'objtree'}."/".$lsmod_file) {
	    $lsmod_file = $ENV{'objtree'}."/".$lsmod_file;
	} else {
		die "$lsmod_file not found";
	}
    }

    my $otype = ( -x $lsmod_file) ? '-|' : '<';
    open($linfile, $otype, $lsmod_file);

} else {

    # see what modules are loaded on this system
    my $lsmod;

    foreach my $dir ( ("/sbin", "/bin", "/usr/sbin", "/usr/bin") ) {
	if ( -x "$dir/lsmod" ) {
	    $lsmod = "$dir/lsmod";
	    last;
	}
}
    if (!defined($lsmod)) {
	# try just the path
	$lsmod = "lsmod";
    }

    open($linfile, '-|', $lsmod) || die "Can not call lsmod with $lsmod";
}

while (<$linfile>) {
	next if (/^Module/);  # Skip the first line.
	if (/^(\S+)/) {
		$modules{$1} = 1;
	}
}
close ($linfile);

# add to the configs hash all configs that are needed to enable
# a loaded module. This is a direct obj-${CONFIG_FOO} += bar.o
# where we know we need bar.o so we add FOO to the list.
my %configs;
foreach my $module (keys(%modules)) {
    if (defined($objects{$module})) {
	my @arr = @{$objects{$module}};
	foreach my $conf (@arr) {
	    $configs{$conf} = $module;
	    dprint "$conf added by direct ($module)\n";
	    if ($debugprint) {
		my $c=$conf;
		$c =~ s/^CONFIG_//;
		if (defined($depends{$c})) {
		    dprint " deps = $depends{$c}\n";
		} else {
		    dprint " no deps\n";
		}
	    }
	}
    } else {
	# Most likely, someone has a custom (binary?) module loaded.
	print STDERR "$module config not found!!\n";
    }
}

# Read the current config, and see what is enabled. We want to
# ignore configs that we would not enable anyway.

my %orig_configs;
my $valid = "A-Za-z_0-9";

foreach my $line (@config_file) {
    $_ = $line;

    if (/(CONFIG_[$valid]*)=(m|y)/) {
	$orig_configs{$1} = $2;
    }
}

my $repeat = 1;

my $depconfig;

#
# Note, we do not care about operands (like: &&, ||, !) we want to add any
# config that is in the depend list of another config. This script does
# not enable configs that are not already enabled. If we come across a
# config A that depends on !B, we can still add B to the list of depends
# to keep on. If A was on in the original config, B would not have been
# and B would not be turned on by this script.
#
sub parse_config_depends
{
    my ($p) = @_;

    while ($p =~ /[$valid]/) {

	if ($p =~ /^[^$valid]*([$valid]+)/) {
	    my $conf = "CONFIG_" . $1;

	    $p =~ s/^[^$valid]*[$valid]+//;

	    # We only need to process if the depend config is a module
	    if (!defined($orig_configs{$conf}) || $orig_configs{$conf} eq "y") {
		next;
	    }

	    if (!defined($configs{$conf})) {
		# We must make sure that this config has its
		# dependencies met.
		$repeat = 1; # do again
		dprint "$conf selected by depend $depconfig\n";
		$configs{$conf} = 1;
	    }
	} else {
	    die "this should never happen";
	}
    }
}

# Select is treated a bit differently than depends. We call this
# when a config has no prompt and requires another config to be
# selected. We use to just select all configs that selected this
# config, but found that that can balloon into enabling hundreds
# of configs that we do not care about.
#
# The idea is we look at all the configs that select it. If one
# is already in our list of configs to enable, then there's nothing
# else to do. If there isn't, we pick the first config that was
# enabled in the orignal config and use that.
sub parse_config_selects
{
    my ($config, $p) = @_;

    my $next_config;

    while ($p =~ /[$valid]/) {

	if ($p =~ /^[^$valid]*([$valid]+)/) {
	    my $conf = "CONFIG_" . $1;

	    $p =~ s/^[^$valid]*[$valid]+//;

	    # Make sure that this config exists in the current .config file
	    if (!defined($orig_configs{$conf})) {
		dprint "$conf not set for $config select\n";
		next;
	    }

	    # Check if something other than a module selects this config
	    if (defined($orig_configs{$conf}) && $orig_configs{$conf} ne "m") {
		dprint "$conf (non module) selects config, we are good\n";
		# we are good with this
		return;
	    }
	    if (defined($configs{$conf})) {
		dprint "$conf selects $config so we are good\n";
		# A set config selects this config, we are good
		return;
	    }
	    # Set this config to be selected
	    if (!defined($next_config)) {
		$next_config = $conf;
	    }
	} else {
	    die "this should never happen";
	}
    }

    # If no possible config selected this, then something happened.
    if (!defined($next_config)) {
	print STDERR "WARNING: $config is required, but nothing in the\n";
	print STDERR "  current config selects it.\n";
	return;
    }

    # If we are here, then we found no config that is set and
    # selects this config. Repeat.
    $repeat = 1;
    # Make this config need to be selected
    $configs{$next_config} = 1;
    dprint "$next_config selected by select $config\n";
}

my %process_selects;

# loop through all configs, select their dependencies.
sub loop_depend {
    $repeat = 1;

    while ($repeat) {
	$repeat = 0;

      forloop:
	foreach my $config (keys %configs) {

	    # If this config is not a module, we do not need to process it
	    if (defined($orig_configs{$config}) && $orig_configs{$config} ne "m") {
		next forloop;
	    }

	    $config =~ s/^CONFIG_//;
	    $depconfig = $config;

	    if (defined($depends{$config})) {
		# This config has dependencies. Make sure they are also included
		parse_config_depends $depends{$config};
	    }

	    # If the config has no prompt, then we need to check if a config
	    # that is enabled selected it. Or if we need to enable one.
	    if (!defined($prompts{$config}) && defined($selects{$config})) {
		$process_selects{$config} = 1;
	    }
	}
    }
}

sub loop_select {

    foreach my $config (keys %process_selects) {
	$config =~ s/^CONFIG_//;

	dprint "Process select $config\n";

	# config has no prompt and must be selected.
	parse_config_selects $config, $selects{$config};
    }
}

while ($repeat) {
    # Get the first set of configs and their dependencies.
    loop_depend;

    $repeat = 0;

    # Now we need to see if we have to check selects;
    loop_select;
}

my %setconfigs;

# Finally, read the .config file and turn off any module enabled that
# we could not find a reason to keep enabled.
foreach my $line (@config_file) {
    $_ = $line;

    if (/CONFIG_IKCONFIG/) {
	if (/# CONFIG_IKCONFIG is not set/) {
	    # enable IKCONFIG at least as a module
	    print "CONFIG_IKCONFIG=m\n";
	    # don't ask about PROC
	    print "# CONFIG_IKCONFIG_PROC is not set\n";
	} else {
	    print;
	}
	next;
    }

    if (/CONFIG_MODULE_SIG_KEY="(.+)"/) {
        my $orig_cert = $1;
        my $default_cert = "certs/signing_key.pem";

        # Check that the logic in this script still matches the one in Kconfig
        if (!defined($depends{"MODULE_SIG_KEY"}) ||
            $depends{"MODULE_SIG_KEY"} !~ /"\Q$default_cert\E"/) {
            print STDERR "WARNING: MODULE_SIG_KEY assertion failure, ",
                "update needed to ", __FILE__, " line ", __LINE__, "\n";
            print;
        } elsif ($orig_cert ne $default_cert && ! -f $orig_cert) {
            print STDERR "Module signature verification enabled but ",
                "module signing key \"$orig_cert\" not found. Resetting ",
                "signing key to default value.\n";
            print "CONFIG_MODULE_SIG_KEY=\"$default_cert\"\n";
        } else {
            print;
        }
        next;
    }

    if (/CONFIG_SYSTEM_TRUSTED_KEYS="(.+)"/) {
        my $orig_keys = $1;

        if (! -f $orig_keys) {
            print STDERR "System keyring enabled but keys \"$orig_keys\" ",
                "not found. Resetting keys to default value.\n";
            print "CONFIG_SYSTEM_TRUSTED_KEYS=\"\"\n";
        } else {
            print;
        }
        next;
    }

    if (/^(CONFIG.*)=(m|y)/) {
	if (defined($configs{$1})) {
	    if ($localyesconfig) {
	        $setconfigs{$1} = 'y';
		print "$1=y\n";
		next;
	    } else {
	        $setconfigs{$1} = $2;
	    }
	} elsif ($2 eq "m") {
	    print "# $1 is not set\n";
	    next;
	}
    }
    print;
}

# Integrity check, make sure all modules that we want enabled do
# indeed have their configs set.
loop:
foreach my $module (keys(%modules)) {
    if (defined($objects{$module})) {
	my @arr = @{$objects{$module}};
	foreach my $conf (@arr) {
	    if (defined($setconfigs{$conf})) {
		next loop;
	    }
	}
	print STDERR "module $module did not have configs";
	foreach my $conf (@arr) {
	    print STDERR " " , $conf;
	}
	print STDERR "\n";
    }
}
