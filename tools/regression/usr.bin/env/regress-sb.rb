#!/usr/local/bin/ruby
# -------+---------+---------+-------- + --------+---------+---------+---------+
# Copyright (c) 2005  - Garance Alistair Drosehn <gad@FreeBSD.org>.
# All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
#  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
#  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
#  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
#  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
#  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
#  SUCH DAMAGE.
# -------+---------+---------+-------- + --------+---------+---------+---------+
# $FreeBSD$
# -------+---------+---------+-------- + --------+---------+---------+---------+
#   This script was written to provide a battery of regression-tests for some
# changes I am making to the `env' command.  I wrote a new script for this
# for several reasons.  1) I needed to test all kinds of special-character
# combinations, and I wanted to be able to type those in exactly as they would
# would be in real-life situations.  2) I wanted to set environment variables
# before executing a test, 3) I had many different details to test, so I wanted
# to write up dozens of tests, without needing to create a hundred separate
# little tiny files, 4) I wanted to test *failure* conditions, where I expected
# the test would fail but I wanted to be sure that it failed the way I intended
# it to fail.
#   This script was written for the special "shebang-line" testing that I
# wanted for my changes to `env', but I expect it could be turned into a
# general-purpose test-suite with a little more work.
#							Garance/June 12/2005
# -------+---------+---------+-------- + --------+---------+---------+---------+


# -------+---------+---------+-------- + --------+---------+---------+---------+
class ExpectedResult
    attr_writer :cmdvalue, :shebang_args, :user_args
    @@gbl_envs = Hash.new

    def ExpectedResult.add_gblenv(avar, avalue)
	@@gbl_envs[avar] = avalue
    end

    def initialize
	@shebang_args = ""
	@cmdvalue = 0
	@clear_envs = Hash.new
	@new_envs = Hash.new
	@old_envs = Hash.new
	@script_lines = ""
	@expect_err = Array.new
	@expect_out = Array.new
	@symlinks = Array.new
	@user_args = nil
    end

    def add_expecterr(aline)
	@expect_err << aline
    end

    def add_expectout(aline)
	@expect_out << aline
    end

    def add_script(aline)
	@script_lines += aline
	@script_lines += "\n"	if aline[-1] != "\n"
    end

    def add_clearenv(avar)
	@clear_envs[avar] = true
    end

    def add_setenv(avar, avalue)
	@new_envs[avar] = avalue
    end

    def add_symlink(srcf, newf)
	@symlinks << Array.[](srcf, newf)
    end

    def check_out(name, fname, expect_arr)
	idx = -1
	all_matched = true
	extra_lines = 0
	rdata = File.open(fname)
	rdata.each_line { |rline|
	    rline.chomp!
	    idx += 1
	    if idx > expect_arr.length - 1
		if extra_lines == 0 and $verbose >= 1
		    printf "--   Extra line(s) on %s:\n", name
		end
		printf "--    [%d] > %s\n", idx, rline if $verbose >= 1
		extra_lines += 1
	    elsif rline != expect_arr[idx]
		if all_matched and $verbose >= 1
		    printf "--   Mismatched line(s) on %s:\n", name
		end
		printf "--    [%d] < %s\n", idx, expect_arr[idx] if $verbose >= 2
		printf "--        > %s\n", rline if $verbose >= 1
		all_matched = false
	    else
		printf "--    %s[%d] = %s\n", name, idx, rline if $verbose >= 5
	    end
	}
	rdata.close
	if extra_lines > 0
	    printf "--   %d extra line(s) found on %s\n", extra_lines,
	      name if $verbose == 0
	    return false
	end
	if not all_matched
	    printf "--   Mismatched line(s) found on %s\n",
	      name if $verbose == 0
	    return false
	end
	return true
    end

    def create_links
	@symlinks.each { |fnames|
	    if $verbose >= 2
		printf "--  Creating: symlink %s %s\n", fnames[0], fnames[1]
	    end
	    symres = File.symlink(fnames[0], fnames[1])
	    return false if symres == nil
	    return false unless File.symlink?(fnames[1])
	}
	return true
    end

    def destroy_links
	@symlinks.each { |fnames|
	    if $verbose >= 2
		printf "--  Removing: %s (symlink)\n", fnames[1]
	    end
	    if File.symlink?(fnames[1])
		if File.delete(fnames[1]) != 1
		    $stderr.printf "Warning: problem removing symlink '%s'\n",
		      fnames[1]
		end
	    else
		$stderr.printf "Warning: Symlink '%s' does not exist?!?\n",
		  fnames[1]
	    end
	}
	return true
    end

    def init_io_files
	@stderr = $scriptfile + ".stderr"
	@stdout = $scriptfile + ".stdout"
	File.delete(@stderr)	if File.exists?(@stderr)
	File.delete(@stdout)	if File.exists?(@stdout)
	@stdin = "/dev/null"

	@redirs = " <" + @stdin
	@redirs += " >" + @stdout
	@redirs += " 2>" + @stderr

    end

    def pop_envs
	@new_envs.each_key { |evar|
	    if @old_envs.has_key?(evar)
		ENV[evar] = @old_envs[evar]
	    else
		ENV.delete(evar)
	    end
	}
    end

    def push_envs
	@@gbl_envs.each_pair { |evar, eval|
	    ENV[evar] = eval
	}
	@new_envs.each_pair { |evar, eval|
	    if ENV.has_key?(evar)
		@old_envs[evar] = ENV[evar]
	    end
	    ENV[evar] = eval
	}
    end

    def run_test
	tscript = File.open($scriptfile, "w")
	tscript.printf "#!%s", $testpgm
	tscript.printf " %s", @shebang_args if @shebang_args != ""
	tscript.printf "\n"
	tscript.printf "%s", @script_lines if @script_lines != ""
	tscript.close
	File.chmod(0755, $scriptfile)

	usercmd = $scriptfile
	usercmd += " " + @user_args	if @user_args != nil
	init_io_files

	push_envs
	return 0 unless create_links
	printf "-  Executing: %s\n", usercmd if $verbose >= 1
	printf "-----   with: %s\n", @redirs if $verbose >= 6
	sys_ok = system(usercmd + @redirs)
	if sys_ok
	    @sav_cmdvalue = 0
	elsif $?.exited?
	    @sav_cmdvalue = $?.exitstatus
	else
	    @sav_cmdvalue = 125
	end
	destroy_links
	pop_envs
	sys_ok = true
	if @sav_cmdvalue != @cmdvalue
	    printf "--   Expecting cmdvalue of %d, but $? == %d\n", @cmdvalue,
	      @sav_cmdvalue
	    sys_ok = false
	end
	sys_ok = false	unless check_out("stdout", @stdout, @expect_out)
	sys_ok = false	unless check_out("stderr", @stderr, @expect_err)
	return 1	if sys_ok
	return 0
    end
end

# -------+---------+---------+-------- + --------+---------+---------+---------+
#   Processing of the command-line options given to the regress-sb.rb script.
#
class CommandOptions
    def CommandOptions.parse(command_args)
	parse_ok = true
	command_args.each { |userarg|
	    case userarg
	    when /^--rgdata=(\S+)$/
		parse_ok = false	unless set_rgdatafile($1)
	    when /^--testpgm=(\S+)$/
		parse_ok = false	unless set_testpgm($1)
		$cmdopt_testpgm = $testpgm
	    when "--stop-on-error", "--stop_on_error"
		$stop_on_error = true
	    when /^--/
		$stderr.printf "Error: Invalid long option: %s\n", userarg
		parse_ok = false
	    when /^-/
		userarg = userarg[1...userarg.length]
		userarg.each_byte { |byte|
		    char = byte.chr
		    case char
		    when "v"
			$verbose += 1
		    else
			$stderr.printf "Error: Invalid short option: -%s\n", char
			parse_ok = false
		    end
		}
	    else
		$stderr.printf "Error: Invalid request: %s\n", userarg
		parse_ok = false
	    end
	}
	if $rgdatafile == nil
	    rgmatch = Dir.glob("regress*.rgdata")
	    if rgmatch.length == 1
		$rgdatafile = rgmatch[0]
		printf "Assuming --rgdata=%s\n", $rgdatafile
	    else
		$stderr.printf "Error: The --rgdata file was not specified\n"
		parse_ok = false
	    end
	end
	return parse_ok
    end

    def CommandOptions.set_rgdatafile(fname)
	if not File.exists?(fname)
	    $stderr.printf "Error: Rgdata file '%s' does not exist\n", fname
	    return false
	elsif not File.readable?(fname)
	    $stderr.printf "Error: Rgdata file '%s' is not readable\n", fname
	    return false
	end
	$rgdatafile = File.expand_path(fname)
	return true
    end

    def CommandOptions.set_testpgm(fname)
	if not File.exists?(fname)
	    $stderr.printf "Error: Testpgm file '%s' does not exist\n", fname
	    return false
	elsif not File.executable?(fname)
	    $stderr.printf "Error: Testpgm file '%s' is not executable\n", fname
	    return false
	end
	$testpgm = File.expand_path(fname)
	return true
    end
end

# -------+---------+---------+-------- + --------+---------+---------+---------+
#   Processing of the test-specific options specifed in each [test]/[run]
#   section of the regression-data file.  This will set values in the
#   global $testdata object.
#
class RGTestOptions
    @@rgtest_opts = nil;

    def RGTestOptions.init_rgtopts
	@@rgtest_opts = Hash.new
	@@rgtest_opts["$?"] = true
	@@rgtest_opts["clearenv"] = true
	@@rgtest_opts["sb_args"] = true
	@@rgtest_opts["script"] = true
	@@rgtest_opts["setenv"] = true
	@@rgtest_opts["stderr"] = true
	@@rgtest_opts["stdout"] = true
	@@rgtest_opts["symlink"] = true
	@@rgtest_opts["user_args"] = true
    end

    def RGTestOptions.parse(optname, optval)
	init_rgtopts	unless @@rgtest_opts

	if not @@rgtest_opts.has_key?(optname)
	    $stderr.printf "Error: Invalid test-option in rgdata file: %s\n",
	      optname
	    return false
	end

	#   Support a few very specific substitutions in values specified
	#   for test data.  Format of all recognized values should be:
	#		[%-object.value-%]
	#   which is hopefully distinctive-enough that they will never
	#   conflict with any naturally-occurring string.  Also note that
	#   we only match the specific values that we recognize, and not
	#   "just anything" that matches the general pattern.  There are
	#   no blanks in the recognized values, but I use an x-tended
	#   regexp and then add blanks to make it more readable.
	optval.gsub!(/\[%- testpgm\.pathname -%\]/x, $testpgm)
	optval.gsub!(/\[%- testpgm\.basename -%\]/x, File.basename($testpgm))
	optval.gsub!(/\[%- script\.pathname  -%\]/x, $scriptfile)

	invalid_value = false
	case optname
	when "$?"
	    if optval =~ /^\d+$/
		$testdata.cmdvalue = optval.to_i
	    else
		invalid_value = true
	    end
	when "clearenv"
	    if optval =~ /^\s*([A-Za-z]\w*)\s*$/
		$testdata.add_clearenv($1)
	    else
		invalid_value = true
	    end
	when "sb_args"
	    $testdata.shebang_args = optval
	when "script"
	    $testdata.add_script(optval)
	when "setenv"
	    if optval =~ /^\s*([A-Za-z]\w*)=(.*)$/
		$testdata.add_setenv($1, $2)
	    else
		invalid_value = true
	    end
	when "stderr"
	    $testdata.add_expecterr(optval)
	when "stdout"
	    $testdata.add_expectout(optval)
	when "symlink"
	    if optval =~ /^\s*(\S+)\s+(\S+)\s*$/
		srcfile = $1
		newfile = $2
		if not File.exists?(srcfile)
		    $stderr.printf "Error: source file '%s' does not exist.\n",
			srcfile
		    invalid_value = true
		elsif File.exists?(newfile)
		    $stderr.printf "Error: new file '%s' already exists.\n",
			newfile
		    invalid_value = true
		else
		    $testdata.add_symlink(srcfile, newfile)
		end
	    else
		invalid_value = true
	    end
	when "user_args"
	    $testdata.user_args = optval
	else
	    $stderr.printf "InternalError: Invalid test-option in rgdata file: %s\n",
		optname
	    return false
	end

	if invalid_value
	    $stderr.printf "Error: Invalid value(s) for %s: %s\n",
	      optname, optval
	    return false
	end
	return true
    end
end

# -------+---------+---------+-------- + --------+---------+---------+---------+
#   Here's where the "main" routine begins...
#

$cmdopt_testpgm = nil
$testpgm = nil
$rgdatafile = nil
$scriptfile = "/tmp/env-regress"
$stop_on_error = false
$verbose = 0

exit 1 unless CommandOptions.parse(ARGV)

errline = nil
test_count = 0
testok_count = 0
test_lineno = -1
max_test = -1
regress_data = File.open($rgdatafile)
regress_data.each_line { |dline|
    case dline
    when /^\s*#/, /^\s*$/
	#  Just a comment line, ignore it.
    when /^\s*gblenv=\s*(.+)$/
	if test_lineno > 0
	    $stderr.printf "Error: Cannot define a global-value in the middle of a test (#5d)\n", test_lineno
	    errline = regress_data.lineno
	    break;
	end
        tempval = $1
	if tempval !~ /^([A-Za-z]\w*)=(.*)$/
	    $stderr.printf "Error: Invalid value for 'gblenv=' request: %s\n",
	      tempval
	    errline = regress_data.lineno
	    break;
	end
	ExpectedResult.add_gblenv($1, $2)
    when /^testpgm=\s*(\S+)\s*/
	#   Set the location of the program to be tested, if it wasn't set
	#   on the command-line processing.
	if $cmdopt_testpgm == nil
	    if not CommandOptions.set_testpgm($1)
		errline = regress_data.lineno
		break;
	    end
	end
    when /^\[test\]$/
	if test_lineno > 0
	    $stderr.printf "Error: Request to define a [test], but we are still defining\n"
	    $stderr.printf "       the [test] at line #%s\n", test_lineno
	    errline = regress_data.lineno
	    break;
	end
	test_lineno = regress_data.lineno
	max_test = test_lineno
	printf "- Defining test at line #%s\n", test_lineno if $verbose >= 6
	$testdata = ExpectedResult.new
    when /^\[end\]$/
	#   User wants us to ignore the remainder of the rgdata file...
	break;
    when /^\[run\]$/
	if test_lineno < 0
	    $stderr.printf "Error: Request to [run] a test, but no test is presently defined\n"
	    errline = regress_data.lineno
	    break;
	end
	printf "-  Running test at line #%s\n", test_lineno if $verbose >= 1
	run_result = $testdata.run_test
	test_count += 1
	printf "[Test #%3d: ", test_count
	case run_result
	when 0
	    #   Test failed
	    printf "Failed!  (line %4d)]\n", test_lineno
	    break if $stop_on_error
	when 1
	    #   Test ran as expected
	    testok_count += 1
	    printf "OK]\n"
	else
	    #   Internal error of some sort
	    printf "InternalError!  (line %4d)]\n", test_lineno
            errline = regress_data.lineno
            break;
	end
	test_lineno = -1

    when /^(\s*)([^\s:]+)\s*:(.+)$/
	blankpfx = $1
	test_lhs = $2
	test_rhs = $3
	if test_lineno < 0
	    $stderr.printf "Error: No test is presently being defined\n"
	    errline = regress_data.lineno
	    break;
	end
	#   All the real work happens in RGTestOptions.parse
	if not RGTestOptions.parse(test_lhs, test_rhs)
	    errline = regress_data.lineno
	    break;
	end
	if blankpfx.length == 0
	    $stderr.printf "Note: You should at least one blank before:%s\n",
	      dline.chomp
	    $stderr.printf "      at line %d of rgdata file %s\n",
	      regress_data.lineno, $rgdatafile
	end

    else
	$stderr.printf "Error: Invalid line: %s\n", dline.chomp
	errline = regress_data.lineno
	break;
    end
}
regress_data.close
if errline != nil
    $stderr.printf "       at line %d of rgdata file %s\n", errline, $rgdatafile
    exit 2
end
if testok_count != test_count
    printf "%d of %d tests were successful.\n", testok_count, test_count
    exit 1
end

printf "All %d tests were successful!\n", testok_count
exit 0
