#! /usr/bin/env python3

# ################################################################
# Copyright (c) 2016-present, Przemyslaw Skibinski, Yann Collet, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ##########################################################################

# Limitations:
# - doesn't support filenames with spaces
# - dir1/zstd and dir2/zstd will be merged in a single results file

import argparse
import os           # getloadavg
import string
import subprocess
import time         # strftime
import traceback
import hashlib
import platform     # system

script_version = 'v1.1.2 (2017-03-26)'
default_repo_url = 'https://github.com/facebook/zstd.git'
working_dir_name = 'speedTest'
working_path = os.getcwd() + '/' + working_dir_name     # /path/to/zstd/tests/speedTest
clone_path = working_path + '/' + 'zstd'                # /path/to/zstd/tests/speedTest/zstd
email_header = 'ZSTD_speedTest'
pid = str(os.getpid())
verbose = False
clang_version = "unknown"
gcc_version = "unknown"
args = None


def hashfile(hasher, fname, blocksize=65536):
    with open(fname, "rb") as f:
        for chunk in iter(lambda: f.read(blocksize), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def log(text):
    print(time.strftime("%Y/%m/%d %H:%M:%S") + ' - ' + text)


def execute(command, print_command=True, print_output=False, print_error=True, param_shell=True):
    if print_command:
        log("> " + command)
    popen = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=param_shell, cwd=execute.cwd)
    stdout_lines, stderr_lines = popen.communicate(timeout=args.timeout)
    stderr_lines = stderr_lines.decode("utf-8")
    stdout_lines = stdout_lines.decode("utf-8")
    if print_output:
        if stdout_lines:
            print(stdout_lines)
        if stderr_lines:
            print(stderr_lines)
    if popen.returncode is not None and popen.returncode != 0:
        if stderr_lines and not print_output and print_error:
            print(stderr_lines)
        raise RuntimeError(stdout_lines + stderr_lines)
    return (stdout_lines + stderr_lines).splitlines()
execute.cwd = None


def does_command_exist(command):
    try:
        execute(command, verbose, False, False)
    except Exception:
        return False
    return True


def send_email(emails, topic, text, have_mutt, have_mail):
    logFileName = working_path + '/' + 'tmpEmailContent'
    with open(logFileName, "w") as myfile:
        myfile.writelines(text)
        myfile.close()
        if have_mutt:
            execute('mutt -s "' + topic + '" ' + emails + ' < ' + logFileName, verbose)
        elif have_mail:
            execute('mail -s "' + topic + '" ' + emails + ' < ' + logFileName, verbose)
        else:
            log("e-mail cannot be sent (mail or mutt not found)")


def send_email_with_attachments(branch, commit, last_commit, args, text, results_files,
                                logFileName, have_mutt, have_mail):
    with open(logFileName, "w") as myfile:
        myfile.writelines(text)
        myfile.close()
        email_topic = '[%s:%s] Warning for %s:%s last_commit=%s speed<%s ratio<%s' \
                      % (email_header, pid, branch, commit, last_commit,
                         args.lowerLimit, args.ratioLimit)
        if have_mutt:
            execute('mutt -s "' + email_topic + '" ' + args.emails + ' -a ' + results_files
                    + ' < ' + logFileName)
        elif have_mail:
            execute('mail -s "' + email_topic + '" ' + args.emails + ' < ' + logFileName)
        else:
            log("e-mail cannot be sent (mail or mutt not found)")


def git_get_branches():
    execute('git fetch -p', verbose)
    branches = execute('git branch -rl', verbose)
    output = []
    for line in branches:
        if ("HEAD" not in line) and ("coverity_scan" not in line) and ("gh-pages" not in line):
            output.append(line.strip())
    return output


def git_get_changes(branch, commit, last_commit):
    fmt = '--format="%h: (%an) %s, %ar"'
    if last_commit is None:
        commits = execute('git log -n 10 %s %s' % (fmt, commit))
    else:
        commits = execute('git --no-pager log %s %s..%s' % (fmt, last_commit, commit))
    return str('Changes in %s since %s:\n' % (branch, last_commit)) + '\n'.join(commits)


def get_last_results(resultsFileName):
    if not os.path.isfile(resultsFileName):
        return None, None, None, None
    commit = None
    csize = []
    cspeed = []
    dspeed = []
    with open(resultsFileName, 'r') as f:
        for line in f:
            words = line.split()
            if len(words) <= 4:   # branch + commit + compilerVer + md5
                commit = words[1]
                csize = []
                cspeed = []
                dspeed = []
            if (len(words) == 8) or (len(words) == 9):  # results: "filename" or "XX files"
                csize.append(int(words[1]))
                cspeed.append(float(words[3]))
                dspeed.append(float(words[5]))
    return commit, csize, cspeed, dspeed


def benchmark_and_compare(branch, commit, last_commit, args, executableName, md5sum, compilerVersion, resultsFileName,
                          testFilePath, fileName, last_csize, last_cspeed, last_dspeed):
    sleepTime = 30
    while os.getloadavg()[0] > args.maxLoadAvg:
        log("WARNING: bench loadavg=%.2f is higher than %s, sleeping for %s seconds"
            % (os.getloadavg()[0], args.maxLoadAvg, sleepTime))
        time.sleep(sleepTime)
    start_load = str(os.getloadavg())
    osType = platform.system()
    if osType == 'Linux':
        cpuSelector = "taskset --cpu-list 0"
    else:
        cpuSelector = ""
    if args.dictionary:
        result = execute('%s programs/%s -rqi5b1e%s -D %s %s' % (cpuSelector, executableName, args.lastCLevel, args.dictionary, testFilePath), print_output=True)
    else:
        result = execute('%s programs/%s -rqi5b1e%s %s' % (cpuSelector, executableName, args.lastCLevel, testFilePath), print_output=True)
    end_load = str(os.getloadavg())
    linesExpected = args.lastCLevel + 1
    if len(result) != linesExpected:
        raise RuntimeError("ERROR: number of result lines=%d is different that expected %d\n%s" % (len(result), linesExpected, '\n'.join(result)))
    with open(resultsFileName, "a") as myfile:
        myfile.write('%s %s %s md5=%s\n' % (branch, commit, compilerVersion, md5sum))
        myfile.write('\n'.join(result) + '\n')
        myfile.close()
        if (last_cspeed == None):
            log("WARNING: No data for comparison for branch=%s file=%s " % (branch, fileName))
            return ""
        commit, csize, cspeed, dspeed = get_last_results(resultsFileName)
        text = ""
        for i in range(0, min(len(cspeed), len(last_cspeed))):
            print("%s:%s -%d cSpeed=%6.2f cLast=%6.2f cDiff=%1.4f dSpeed=%6.2f dLast=%6.2f dDiff=%1.4f ratioDiff=%1.4f %s" % (branch, commit, i+1, cspeed[i], last_cspeed[i], cspeed[i]/last_cspeed[i], dspeed[i], last_dspeed[i], dspeed[i]/last_dspeed[i], float(last_csize[i])/csize[i], fileName))
            if (cspeed[i]/last_cspeed[i] < args.lowerLimit):
                text += "WARNING: %s -%d cSpeed=%.2f cLast=%.2f cDiff=%.4f %s\n" % (executableName, i+1, cspeed[i], last_cspeed[i], cspeed[i]/last_cspeed[i], fileName)
            if (dspeed[i]/last_dspeed[i] < args.lowerLimit):
                text += "WARNING: %s -%d dSpeed=%.2f dLast=%.2f dDiff=%.4f %s\n" % (executableName, i+1, dspeed[i], last_dspeed[i], dspeed[i]/last_dspeed[i], fileName)
            if (float(last_csize[i])/csize[i] < args.ratioLimit):
                text += "WARNING: %s -%d cSize=%d last_cSize=%d diff=%.4f %s\n" % (executableName, i+1, csize[i], last_csize[i], float(last_csize[i])/csize[i], fileName)
        if text:
            text = args.message + ("\nmaxLoadAvg=%s  load average at start=%s end=%s\n%s  last_commit=%s  md5=%s\n" % (args.maxLoadAvg, start_load, end_load, compilerVersion, last_commit, md5sum)) + text
        return text


def update_config_file(branch, commit):
    last_commit = None
    commitFileName = working_path + "/commit_" + branch.replace("/", "_") + ".txt"
    if os.path.isfile(commitFileName):
        with open(commitFileName, 'r') as infile:
            last_commit = infile.read()
    with open(commitFileName, 'w') as outfile:
        outfile.write(commit)
    return last_commit


def double_check(branch, commit, args, executableName, md5sum, compilerVersion, resultsFileName, filePath, fileName):
    last_commit, csize, cspeed, dspeed = get_last_results(resultsFileName)
    if not args.dry_run:
        text = benchmark_and_compare(branch, commit, last_commit, args, executableName, md5sum, compilerVersion, resultsFileName, filePath, fileName, csize, cspeed, dspeed)
        if text:
            log("WARNING: redoing tests for branch %s: commit %s" % (branch, commit))
            text = benchmark_and_compare(branch, commit, last_commit, args, executableName, md5sum, compilerVersion, resultsFileName, filePath, fileName, csize, cspeed, dspeed)
    return text


def test_commit(branch, commit, last_commit, args, testFilePaths, have_mutt, have_mail):
    local_branch = branch.split('/')[1]
    version = local_branch.rpartition('-')[2] + '_' + commit
    if not args.dry_run:
        execute('make -C programs clean zstd CC=clang MOREFLAGS="-Werror -Wconversion -Wno-sign-conversion -DZSTD_GIT_COMMIT=%s" && ' % version +
                'mv programs/zstd programs/zstd_clang && ' +
                'make -C programs clean zstd zstd32 MOREFLAGS="-DZSTD_GIT_COMMIT=%s"' % version)
    md5_zstd = hashfile(hashlib.md5(), clone_path + '/programs/zstd')
    md5_zstd32 = hashfile(hashlib.md5(), clone_path + '/programs/zstd32')
    md5_zstd_clang = hashfile(hashlib.md5(), clone_path + '/programs/zstd_clang')
    print("md5(zstd)=%s\nmd5(zstd32)=%s\nmd5(zstd_clang)=%s" % (md5_zstd, md5_zstd32, md5_zstd_clang))
    print("gcc_version=%s clang_version=%s" % (gcc_version, clang_version))

    logFileName = working_path + "/log_" + branch.replace("/", "_") + ".txt"
    text_to_send = []
    results_files = ""
    if args.dictionary:
        dictName = args.dictionary.rpartition('/')[2]
    else:
        dictName = None

    for filePath in testFilePaths:
        fileName = filePath.rpartition('/')[2]
        if dictName:
            resultsFileName = working_path + "/" + dictName.replace(".", "_") + "_" + branch.replace("/", "_") + "_" + fileName.replace(".", "_") + ".txt"
        else:
            resultsFileName = working_path + "/results_" + branch.replace("/", "_") + "_" + fileName.replace(".", "_") + ".txt"
        text = double_check(branch, commit, args, 'zstd', md5_zstd, 'gcc_version='+gcc_version, resultsFileName, filePath, fileName)
        if text:
            text_to_send.append(text)
            results_files += resultsFileName + " "
        resultsFileName = working_path + "/results32_" + branch.replace("/", "_") + "_" + fileName.replace(".", "_") + ".txt"
        text = double_check(branch, commit, args, 'zstd32', md5_zstd32, 'gcc_version='+gcc_version, resultsFileName, filePath, fileName)
        if text:
            text_to_send.append(text)
            results_files += resultsFileName + " "
        resultsFileName = working_path + "/resultsClang_" + branch.replace("/", "_") + "_" + fileName.replace(".", "_") + ".txt"
        text = double_check(branch, commit, args, 'zstd_clang', md5_zstd_clang, 'clang_version='+clang_version, resultsFileName, filePath, fileName)
        if text:
            text_to_send.append(text)
            results_files += resultsFileName + " "
    if text_to_send:
        send_email_with_attachments(branch, commit, last_commit, args, text_to_send, results_files, logFileName, have_mutt, have_mail)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('testFileNames', help='file or directory names list for speed benchmark')
    parser.add_argument('emails', help='list of e-mail addresses to send warnings')
    parser.add_argument('--dictionary', '-D', help='path to the dictionary')
    parser.add_argument('--message', '-m', help='attach an additional message to e-mail', default="")
    parser.add_argument('--repoURL', help='changes default repository URL', default=default_repo_url)
    parser.add_argument('--lowerLimit', '-l', type=float, help='send email if speed is lower than given limit', default=0.98)
    parser.add_argument('--ratioLimit', '-r', type=float, help='send email if ratio is lower than given limit', default=0.999)
    parser.add_argument('--maxLoadAvg', type=float, help='maximum load average to start testing', default=0.75)
    parser.add_argument('--lastCLevel', type=int, help='last compression level for testing', default=5)
    parser.add_argument('--sleepTime', '-s', type=int, help='frequency of repository checking in seconds', default=300)
    parser.add_argument('--timeout', '-t', type=int, help='timeout for executing shell commands', default=1800)
    parser.add_argument('--dry-run', dest='dry_run', action='store_true', help='not build', default=False)
    parser.add_argument('--verbose', '-v', action='store_true', help='more verbose logs', default=False)
    args = parser.parse_args()
    verbose = args.verbose

    # check if test files are accessible
    testFileNames = args.testFileNames.split()
    testFilePaths = []
    for fileName in testFileNames:
        fileName = os.path.expanduser(fileName)
        if os.path.isfile(fileName) or os.path.isdir(fileName):
            testFilePaths.append(os.path.abspath(fileName))
        else:
            log("ERROR: File/directory not found: " + fileName)
            exit(1)

    # check if dictionary is accessible
    if args.dictionary:
        args.dictionary = os.path.abspath(os.path.expanduser(args.dictionary))
        if not os.path.isfile(args.dictionary):
            log("ERROR: Dictionary not found: " + args.dictionary)
            exit(1)

    # check availability of e-mail senders
    have_mutt = does_command_exist("mutt -h")
    have_mail = does_command_exist("mail -V")
    if not have_mutt and not have_mail:
        log("ERROR: e-mail senders 'mail' or 'mutt' not found")
        exit(1)

    clang_version = execute("clang -v 2>&1 | grep ' version ' | sed -e 's:.*version \\([0-9.]*\\).*:\\1:' -e 's:\\.\\([0-9][0-9]\\):\\1:g'", verbose)[0];
    gcc_version = execute("gcc -dumpversion", verbose)[0];

    if verbose:
        print("PARAMETERS:\nrepoURL=%s" % args.repoURL)
        print("working_path=%s" % working_path)
        print("clone_path=%s" % clone_path)
        print("testFilePath(%s)=%s" % (len(testFilePaths), testFilePaths))
        print("message=%s" % args.message)
        print("emails=%s" % args.emails)
        print("dictionary=%s" % args.dictionary)
        print("maxLoadAvg=%s" % args.maxLoadAvg)
        print("lowerLimit=%s" % args.lowerLimit)
        print("ratioLimit=%s" % args.ratioLimit)
        print("lastCLevel=%s" % args.lastCLevel)
        print("sleepTime=%s" % args.sleepTime)
        print("timeout=%s" % args.timeout)
        print("dry_run=%s" % args.dry_run)
        print("verbose=%s" % args.verbose)
        print("have_mutt=%s have_mail=%s" % (have_mutt, have_mail))

    # clone ZSTD repo if needed
    if not os.path.isdir(working_path):
        os.mkdir(working_path)
    if not os.path.isdir(clone_path):
        execute.cwd = working_path
        execute('git clone ' + args.repoURL)
    if not os.path.isdir(clone_path):
        log("ERROR: ZSTD clone not found: " + clone_path)
        exit(1)
    execute.cwd = clone_path

    # check if speedTest.pid already exists
    pidfile = "./speedTest.pid"
    if os.path.isfile(pidfile):
        log("ERROR: %s already exists, exiting" % pidfile)
        exit(1)

    send_email(args.emails, '[%s:%s] test-zstd-speed.py %s has been started' % (email_header, pid, script_version), args.message, have_mutt, have_mail)
    with open(pidfile, 'w') as the_file:
        the_file.write(pid)

    branch = ""
    commit = ""
    first_time = True
    while True:
        try:
            if first_time:
                first_time = False
            else:
                time.sleep(args.sleepTime)
            loadavg = os.getloadavg()[0]
            if (loadavg <= args.maxLoadAvg):
                branches = git_get_branches()
                for branch in branches:
                    commit = execute('git show -s --format=%h ' + branch, verbose)[0]
                    last_commit = update_config_file(branch, commit)
                    if commit == last_commit:
                        log("skipping branch %s: head %s already processed" % (branch, commit))
                    else:
                        log("build branch %s: head %s is different from prev %s" % (branch, commit, last_commit))
                        execute('git checkout -- . && git checkout ' + branch)
                        print(git_get_changes(branch, commit, last_commit))
                        test_commit(branch, commit, last_commit, args, testFilePaths, have_mutt, have_mail)
            else:
                log("WARNING: main loadavg=%.2f is higher than %s" % (loadavg, args.maxLoadAvg))
            if verbose:
                log("sleep for %s seconds" % args.sleepTime)
        except Exception as e:
            stack = traceback.format_exc()
            email_topic = '[%s:%s] ERROR in %s:%s' % (email_header, pid, branch, commit)
            send_email(args.emails, email_topic, stack, have_mutt, have_mail)
            print(stack)
        except KeyboardInterrupt:
            os.unlink(pidfile)
            send_email(args.emails, '[%s:%s] test-zstd-speed.py %s has been stopped' % (email_header, pid, script_version), args.message, have_mutt, have_mail)
            exit(0)
