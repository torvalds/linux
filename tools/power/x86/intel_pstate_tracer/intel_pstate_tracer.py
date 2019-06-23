#!/usr/bin/python
# SPDX-License-Identifier: GPL-2.0-only
# -*- coding: utf-8 -*-
#
""" This utility can be used to debug and tune the performance of the
intel_pstate driver. This utility can be used in two ways:
- If there is Linux trace file with pstate_sample events enabled, then
this utility can parse the trace file and generate performance plots.
- If user has not specified a trace file as input via command line parameters,
then this utility enables and collects trace data for a user specified interval
and generates performance plots.

Prerequisites:
    Python version 2.7.x
    gnuplot 5.0 or higher
    gnuplot-py 1.8
    (Most of the distributions have these required packages. They may be called
     gnuplot-py, phython-gnuplot. )

    HWP (Hardware P-States are disabled)
    Kernel config for Linux trace is enabled

    see print_help(): for Usage and Output details

"""
from __future__ import print_function
from datetime import datetime
import subprocess
import os
import time
import re
import signal
import sys
import getopt
import Gnuplot
from numpy import *
from decimal import *

__author__ = "Srinivas Pandruvada"
__copyright__ = " Copyright (c) 2017, Intel Corporation. "
__license__ = "GPL version 2"


MAX_CPUS = 256

# Define the csv file columns
C_COMM = 18
C_GHZ = 17
C_ELAPSED = 16
C_SAMPLE = 15
C_DURATION = 14
C_LOAD = 13
C_BOOST = 12
C_FREQ = 11
C_TSC = 10
C_APERF = 9
C_MPERF = 8
C_TO = 7
C_FROM = 6
C_SCALED = 5
C_CORE = 4
C_USEC = 3
C_SEC = 2
C_CPU = 1

global sample_num, last_sec_cpu, last_usec_cpu, start_time, testname

# 11 digits covers uptime to 115 days
getcontext().prec = 11

sample_num =0
last_sec_cpu = [0] * MAX_CPUS
last_usec_cpu = [0] * MAX_CPUS

def print_help():
    print('intel_pstate_tracer.py:')
    print('  Usage:')
    print('    If the trace file is available, then to simply parse and plot, use (sudo not required):')
    print('      ./intel_pstate_tracer.py [-c cpus] -t <trace_file> -n <test_name>')
    print('    Or')
    print('      ./intel_pstate_tracer.py [--cpu cpus] ---trace_file <trace_file> --name <test_name>')
    print('    To generate trace file, parse and plot, use (sudo required):')
    print('      sudo ./intel_pstate_tracer.py [-c cpus] -i <interval> -n <test_name> -m <kbytes>')
    print('    Or')
    print('      sudo ./intel_pstate_tracer.py [--cpu cpus] --interval <interval> --name <test_name> --memory <kbytes>')
    print('    Optional argument:')
    print('      cpus:   comma separated list of CPUs')
    print('      kbytes: Kilo bytes of memory per CPU to allocate to the trace buffer. Default: 10240')
    print('  Output:')
    print('    If not already present, creates a "results/test_name" folder in the current working directory with:')
    print('      cpu.csv - comma seperated values file with trace contents and some additional calculations.')
    print('      cpu???.csv - comma seperated values file for CPU number ???.')
    print('      *.png - a variety of PNG format plot files created from the trace contents and the additional calculations.')
    print('  Notes:')
    print('    Avoid the use of _ (underscore) in test names, because in gnuplot it is a subscript directive.')
    print('    Maximum number of CPUs is {0:d}. If there are more the script will abort with an error.'.format(MAX_CPUS))
    print('    Off-line CPUs cause the script to list some warnings, and create some empty files. Use the CPU mask feature for a clean run.')
    print('    Empty y range warnings for autoscaled plots can occur and can be ignored.')

def plot_perf_busy_with_sample(cpu_index):
    """ Plot method to per cpu information """

    file_name = 'cpu{:0>3}.csv'.format(cpu_index)
    if os.path.exists(file_name):
        output_png = "cpu%03d_perf_busy_vs_samples.png" % cpu_index
        g_plot = common_all_gnuplot_settings(output_png)
        g_plot('set yrange [0:40]')
        g_plot('set y2range [0:200]')
        g_plot('set y2tics 0, 10')
        g_plot('set title "{} : cpu perf busy vs. sample : CPU {:0>3} : {:%F %H:%M}"'.format(testname, cpu_index, datetime.now()))
#       Override common
        g_plot('set xlabel "Samples"')
        g_plot('set ylabel "P-State"')
        g_plot('set y2label "Scaled Busy/performance/io-busy(%)"')
        set_4_plot_linestyles(g_plot)
        g_plot('plot "' + file_name + '" using {:d}:{:d} with linespoints linestyle 1 axis x1y2 title "performance",\\'.format(C_SAMPLE, C_CORE))
        g_plot('"' + file_name + '" using {:d}:{:d} with linespoints linestyle 2 axis x1y2 title "scaled-busy",\\'.format(C_SAMPLE, C_SCALED))
        g_plot('"' + file_name + '" using {:d}:{:d} with linespoints linestyle 3 axis x1y2 title "io-boost",\\'.format(C_SAMPLE, C_BOOST))
        g_plot('"' + file_name + '" using {:d}:{:d} with linespoints linestyle 4 axis x1y1 title "P-State"'.format(C_SAMPLE, C_TO))

def plot_perf_busy(cpu_index):
    """ Plot some per cpu information """

    file_name = 'cpu{:0>3}.csv'.format(cpu_index)
    if os.path.exists(file_name):
        output_png = "cpu%03d_perf_busy.png" % cpu_index
        g_plot = common_all_gnuplot_settings(output_png)
        g_plot('set yrange [0:40]')
        g_plot('set y2range [0:200]')
        g_plot('set y2tics 0, 10')
        g_plot('set title "{} : perf busy : CPU {:0>3} : {:%F %H:%M}"'.format(testname, cpu_index, datetime.now()))
        g_plot('set ylabel "P-State"')
        g_plot('set y2label "Scaled Busy/performance/io-busy(%)"')
        set_4_plot_linestyles(g_plot)
        g_plot('plot "' + file_name + '" using {:d}:{:d} with linespoints linestyle 1 axis x1y2 title "performance",\\'.format(C_ELAPSED, C_CORE))
        g_plot('"' + file_name + '" using {:d}:{:d} with linespoints linestyle 2 axis x1y2 title "scaled-busy",\\'.format(C_ELAPSED, C_SCALED))
        g_plot('"' + file_name + '" using {:d}:{:d} with linespoints linestyle 3 axis x1y2 title "io-boost",\\'.format(C_ELAPSED, C_BOOST))
        g_plot('"' + file_name + '" using {:d}:{:d} with linespoints linestyle 4 axis x1y1 title "P-State"'.format(C_ELAPSED, C_TO))

def plot_durations(cpu_index):
    """ Plot per cpu durations """

    file_name = 'cpu{:0>3}.csv'.format(cpu_index)
    if os.path.exists(file_name):
        output_png = "cpu%03d_durations.png" % cpu_index
        g_plot = common_all_gnuplot_settings(output_png)
#       Should autoscale be used here? Should seconds be used here?
        g_plot('set yrange [0:5000]')
        g_plot('set ytics 0, 500')
        g_plot('set title "{} : durations : CPU {:0>3} : {:%F %H:%M}"'.format(testname, cpu_index, datetime.now()))
        g_plot('set ylabel "Timer Duration (MilliSeconds)"')
#       override common
        g_plot('set key off')
        set_4_plot_linestyles(g_plot)
        g_plot('plot "' + file_name + '" using {:d}:{:d} with linespoints linestyle 1 axis x1y1'.format(C_ELAPSED, C_DURATION))

def plot_loads(cpu_index):
    """ Plot per cpu loads """

    file_name = 'cpu{:0>3}.csv'.format(cpu_index)
    if os.path.exists(file_name):
        output_png = "cpu%03d_loads.png" % cpu_index
        g_plot = common_all_gnuplot_settings(output_png)
        g_plot('set yrange [0:100]')
        g_plot('set ytics 0, 10')
        g_plot('set title "{} : loads : CPU {:0>3} : {:%F %H:%M}"'.format(testname, cpu_index, datetime.now()))
        g_plot('set ylabel "CPU load (percent)"')
#       override common
        g_plot('set key off')
        set_4_plot_linestyles(g_plot)
        g_plot('plot "' + file_name + '" using {:d}:{:d} with linespoints linestyle 1 axis x1y1'.format(C_ELAPSED, C_LOAD))

def plot_pstate_cpu_with_sample():
    """ Plot all cpu information """

    if os.path.exists('cpu.csv'):
        output_png = 'all_cpu_pstates_vs_samples.png'
        g_plot = common_all_gnuplot_settings(output_png)
        g_plot('set yrange [0:40]')
#       override common
        g_plot('set xlabel "Samples"')
        g_plot('set ylabel "P-State"')
        g_plot('set title "{} : cpu pstate vs. sample : {:%F %H:%M}"'.format(testname, datetime.now()))
        title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).replace('\n', ' ')
        plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 7 ps 1 title i".format(C_SAMPLE, C_TO)
        g_plot('title_list = "{}"'.format(title_list))
        g_plot(plot_str)

def plot_pstate_cpu():
    """ Plot all cpu information from csv files """

    output_png = 'all_cpu_pstates.png'
    g_plot = common_all_gnuplot_settings(output_png)
    g_plot('set yrange [0:40]')
    g_plot('set ylabel "P-State"')
    g_plot('set title "{} : cpu pstates : {:%F %H:%M}"'.format(testname, datetime.now()))

#    the following command is really cool, but doesn't work with the CPU masking option because it aborts on the first missing file.
#    plot_str = 'plot for [i=0:*] file=sprintf("cpu%03d.csv",i) title_s=sprintf("cpu%03d",i) file using 16:7 pt 7 ps 1 title title_s'
#
    title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).replace('\n', ' ')
    plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 7 ps 1 title i".format(C_ELAPSED, C_TO)
    g_plot('title_list = "{}"'.format(title_list))
    g_plot(plot_str)

def plot_load_cpu():
    """ Plot all cpu loads """

    output_png = 'all_cpu_loads.png'
    g_plot = common_all_gnuplot_settings(output_png)
    g_plot('set yrange [0:100]')
    g_plot('set ylabel "CPU load (percent)"')
    g_plot('set title "{} : cpu loads : {:%F %H:%M}"'.format(testname, datetime.now()))

    title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).replace('\n', ' ')
    plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 7 ps 1 title i".format(C_ELAPSED, C_LOAD)
    g_plot('title_list = "{}"'.format(title_list))
    g_plot(plot_str)

def plot_frequency_cpu():
    """ Plot all cpu frequencies """

    output_png = 'all_cpu_frequencies.png'
    g_plot = common_all_gnuplot_settings(output_png)
    g_plot('set yrange [0:4]')
    g_plot('set ylabel "CPU Frequency (GHz)"')
    g_plot('set title "{} : cpu frequencies : {:%F %H:%M}"'.format(testname, datetime.now()))

    title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).replace('\n', ' ')
    plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 7 ps 1 title i".format(C_ELAPSED, C_FREQ)
    g_plot('title_list = "{}"'.format(title_list))
    g_plot(plot_str)

def plot_duration_cpu():
    """ Plot all cpu durations """

    output_png = 'all_cpu_durations.png'
    g_plot = common_all_gnuplot_settings(output_png)
    g_plot('set yrange [0:5000]')
    g_plot('set ytics 0, 500')
    g_plot('set ylabel "Timer Duration (MilliSeconds)"')
    g_plot('set title "{} : cpu durations : {:%F %H:%M}"'.format(testname, datetime.now()))

    title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).replace('\n', ' ')
    plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 7 ps 1 title i".format(C_ELAPSED, C_DURATION)
    g_plot('title_list = "{}"'.format(title_list))
    g_plot(plot_str)

def plot_scaled_cpu():
    """ Plot all cpu scaled busy """

    output_png = 'all_cpu_scaled.png'
    g_plot = common_all_gnuplot_settings(output_png)
#   autoscale this one, no set y range
    g_plot('set ylabel "Scaled Busy (Unitless)"')
    g_plot('set title "{} : cpu scaled busy : {:%F %H:%M}"'.format(testname, datetime.now()))

    title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).replace('\n', ' ')
    plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 7 ps 1 title i".format(C_ELAPSED, C_SCALED)
    g_plot('title_list = "{}"'.format(title_list))
    g_plot(plot_str)

def plot_boost_cpu():
    """ Plot all cpu IO Boosts """

    output_png = 'all_cpu_boost.png'
    g_plot = common_all_gnuplot_settings(output_png)
    g_plot('set yrange [0:100]')
    g_plot('set ylabel "CPU IO Boost (percent)"')
    g_plot('set title "{} : cpu io boost : {:%F %H:%M}"'.format(testname, datetime.now()))

    title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).replace('\n', ' ')
    plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 7 ps 1 title i".format(C_ELAPSED, C_BOOST)
    g_plot('title_list = "{}"'.format(title_list))
    g_plot(plot_str)

def plot_ghz_cpu():
    """ Plot all cpu tsc ghz """

    output_png = 'all_cpu_ghz.png'
    g_plot = common_all_gnuplot_settings(output_png)
#   autoscale this one, no set y range
    g_plot('set ylabel "TSC Frequency (GHz)"')
    g_plot('set title "{} : cpu TSC Frequencies (Sanity check calculation) : {:%F %H:%M}"'.format(testname, datetime.now()))

    title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).replace('\n', ' ')
    plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 7 ps 1 title i".format(C_ELAPSED, C_GHZ)
    g_plot('title_list = "{}"'.format(title_list))
    g_plot(plot_str)

def common_all_gnuplot_settings(output_png):
    """ common gnuplot settings for multiple CPUs one one graph. """

    g_plot = common_gnuplot_settings()
    g_plot('set output "' + output_png + '"')
    return(g_plot)

def common_gnuplot_settings():
    """ common gnuplot settings. """

    g_plot = Gnuplot.Gnuplot(persist=1)
#   The following line is for rigor only. It seems to be assumed for .csv files
    g_plot('set datafile separator \",\"')
    g_plot('set ytics nomirror')
    g_plot('set xtics nomirror')
    g_plot('set xtics font ", 10"')
    g_plot('set ytics font ", 10"')
    g_plot('set tics out scale 1.0')
    g_plot('set grid')
    g_plot('set key out horiz')
    g_plot('set key bot center')
    g_plot('set key samplen 2 spacing .8 font ", 9"')
    g_plot('set term png size 1200, 600')
    g_plot('set title font ", 11"')
    g_plot('set ylabel font ", 10"')
    g_plot('set xlabel font ", 10"')
    g_plot('set xlabel offset 0, 0.5')
    g_plot('set xlabel "Elapsed Time (Seconds)"')
    return(g_plot)

def set_4_plot_linestyles(g_plot):
    """ set the linestyles used for 4 plots in 1 graphs. """

    g_plot('set style line 1 linetype 1 linecolor rgb "green" pointtype -1')
    g_plot('set style line 2 linetype 1 linecolor rgb "red" pointtype -1')
    g_plot('set style line 3 linetype 1 linecolor rgb "purple" pointtype -1')
    g_plot('set style line 4 linetype 1 linecolor rgb "blue" pointtype -1')

def store_csv(cpu_int, time_pre_dec, time_post_dec, core_busy, scaled, _from, _to, mperf, aperf, tsc, freq_ghz, io_boost, common_comm, load, duration_ms, sample_num, elapsed_time, tsc_ghz):
    """ Store master csv file information """

    global graph_data_present

    if cpu_mask[cpu_int] == 0:
        return

    try:
        f_handle = open('cpu.csv', 'a')
        string_buffer = "CPU_%03u, %05u, %06u, %u, %u, %u, %u, %u, %u, %u, %.4f, %u, %.2f, %.3f, %u, %.3f, %.3f, %s\n" % (cpu_int, int(time_pre_dec), int(time_post_dec), int(core_busy), int(scaled), int(_from), int(_to), int(mperf), int(aperf), int(tsc), freq_ghz, int(io_boost), load, duration_ms, sample_num, elapsed_time, tsc_ghz, common_comm)
        f_handle.write(string_buffer);
        f_handle.close()
    except:
        print('IO error cpu.csv')
        return

    graph_data_present = True;

def split_csv():
    """ seperate the all csv file into per CPU csv files. """

    global current_max_cpu

    if os.path.exists('cpu.csv'):
        for index in range(0, current_max_cpu + 1):
            if cpu_mask[int(index)] != 0:
                os.system('grep -m 1 common_cpu cpu.csv > cpu{:0>3}.csv'.format(index))
                os.system('grep CPU_{:0>3} cpu.csv >> cpu{:0>3}.csv'.format(index, index))

def fix_ownership(path):
    """Change the owner of the file to SUDO_UID, if required"""

    uid = os.environ.get('SUDO_UID')
    gid = os.environ.get('SUDO_GID')
    if uid is not None:
        os.chown(path, int(uid), int(gid))

def cleanup_data_files():
    """ clean up existing data files """

    if os.path.exists('cpu.csv'):
        os.remove('cpu.csv')
    f_handle = open('cpu.csv', 'a')
    f_handle.write('common_cpu, common_secs, common_usecs, core_busy, scaled_busy, from, to, mperf, aperf, tsc, freq, boost, load, duration_ms, sample_num, elapsed_time, tsc_ghz, common_comm')
    f_handle.write('\n')
    f_handle.close()

def clear_trace_file():
    """ Clear trace file """

    try:
        f_handle = open('/sys/kernel/debug/tracing/trace', 'w')
        f_handle.close()
    except:
        print('IO error clearing trace file ')
        sys.exit(2)

def enable_trace():
    """ Enable trace """

    try:
       open('/sys/kernel/debug/tracing/events/power/pstate_sample/enable'
                 , 'w').write("1")
    except:
        print('IO error enabling trace ')
        sys.exit(2)

def disable_trace():
    """ Disable trace """

    try:
       open('/sys/kernel/debug/tracing/events/power/pstate_sample/enable'
                 , 'w').write("0")
    except:
        print('IO error disabling trace ')
        sys.exit(2)

def set_trace_buffer_size():
    """ Set trace buffer size """

    try:
       with open('/sys/kernel/debug/tracing/buffer_size_kb', 'w') as fp:
          fp.write(memory)
    except:
       print('IO error setting trace buffer size ')
       sys.exit(2)

def free_trace_buffer():
    """ Free the trace buffer memory """

    try:
       open('/sys/kernel/debug/tracing/buffer_size_kb'
                 , 'w').write("1")
    except:
        print('IO error freeing trace buffer ')
        sys.exit(2)

def read_trace_data(filename):
    """ Read and parse trace data """

    global current_max_cpu
    global sample_num, last_sec_cpu, last_usec_cpu, start_time

    try:
        data = open(filename, 'r').read()
    except:
        print('Error opening ', filename)
        sys.exit(2)

    for line in data.splitlines():
        search_obj = \
            re.search(r'(^(.*?)\[)((\d+)[^\]])(.*?)(\d+)([.])(\d+)(.*?core_busy=)(\d+)(.*?scaled=)(\d+)(.*?from=)(\d+)(.*?to=)(\d+)(.*?mperf=)(\d+)(.*?aperf=)(\d+)(.*?tsc=)(\d+)(.*?freq=)(\d+)'
                      , line)

        if search_obj:
            cpu = search_obj.group(3)
            cpu_int = int(cpu)
            cpu = str(cpu_int)

            time_pre_dec = search_obj.group(6)
            time_post_dec = search_obj.group(8)
            core_busy = search_obj.group(10)
            scaled = search_obj.group(12)
            _from = search_obj.group(14)
            _to = search_obj.group(16)
            mperf = search_obj.group(18)
            aperf = search_obj.group(20)
            tsc = search_obj.group(22)
            freq = search_obj.group(24)
            common_comm = search_obj.group(2).replace(' ', '')

            # Not all kernel versions have io_boost field
            io_boost = '0'
            search_obj = re.search(r'.*?io_boost=(\d+)', line)
            if search_obj:
                io_boost = search_obj.group(1)

            if sample_num == 0 :
                start_time = Decimal(time_pre_dec) + Decimal(time_post_dec) / Decimal(1000000)
            sample_num += 1

            if last_sec_cpu[cpu_int] == 0 :
                last_sec_cpu[cpu_int] = time_pre_dec
                last_usec_cpu[cpu_int] = time_post_dec
            else :
                duration_us = (int(time_pre_dec) - int(last_sec_cpu[cpu_int])) * 1000000 + (int(time_post_dec) - int(last_usec_cpu[cpu_int]))
                duration_ms = Decimal(duration_us) / Decimal(1000)
                last_sec_cpu[cpu_int] = time_pre_dec
                last_usec_cpu[cpu_int] = time_post_dec
                elapsed_time = Decimal(time_pre_dec) + Decimal(time_post_dec) / Decimal(1000000) - start_time
                load = Decimal(int(mperf)*100)/ Decimal(tsc)
                freq_ghz = Decimal(freq)/Decimal(1000000)
#               Sanity check calculation, typically anomalies indicate missed samples
#               However, check for 0 (should never occur)
                tsc_ghz = Decimal(0)
                if duration_ms != Decimal(0) :
                    tsc_ghz = Decimal(tsc)/duration_ms/Decimal(1000000)
                store_csv(cpu_int, time_pre_dec, time_post_dec, core_busy, scaled, _from, _to, mperf, aperf, tsc, freq_ghz, io_boost, common_comm, load, duration_ms, sample_num, elapsed_time, tsc_ghz)

            if cpu_int > current_max_cpu:
                current_max_cpu = cpu_int
# End of for each trace line loop
# Now seperate the main overall csv file into per CPU csv files.
    split_csv()

def signal_handler(signal, frame):
    print(' SIGINT: Forcing cleanup before exit.')
    if interval:
        disable_trace()
        clear_trace_file()
        # Free the memory
        free_trace_buffer()
        sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)

interval = ""
filename = ""
cpu_list = ""
testname = ""
memory = "10240"
graph_data_present = False;

valid1 = False
valid2 = False

cpu_mask = zeros((MAX_CPUS,), dtype=int)

try:
    opts, args = getopt.getopt(sys.argv[1:],"ht:i:c:n:m:",["help","trace_file=","interval=","cpu=","name=","memory="])
except getopt.GetoptError:
    print_help()
    sys.exit(2)
for opt, arg in opts:
    if opt == '-h':
        print()
        sys.exit()
    elif opt in ("-t", "--trace_file"):
        valid1 = True
        location = os.path.realpath(os.path.join(os.getcwd(), os.path.dirname(__file__)))
        filename = os.path.join(location, arg)
    elif opt in ("-i", "--interval"):
        valid1 = True
        interval = arg
    elif opt in ("-c", "--cpu"):
        cpu_list = arg
    elif opt in ("-n", "--name"):
        valid2 = True
        testname = arg
    elif opt in ("-m", "--memory"):
        memory = arg

if not (valid1 and valid2):
    print_help()
    sys.exit()

if cpu_list:
    for p in re.split("[,]", cpu_list):
        if int(p) < MAX_CPUS :
            cpu_mask[int(p)] = 1
else:
    for i in range (0, MAX_CPUS):
        cpu_mask[i] = 1

if not os.path.exists('results'):
    os.mkdir('results')
    # The regular user needs to own the directory, not root.
    fix_ownership('results')

os.chdir('results')
if os.path.exists(testname):
    print('The test name directory already exists. Please provide a unique test name. Test re-run not supported, yet.')
    sys.exit()
os.mkdir(testname)
# The regular user needs to own the directory, not root.
fix_ownership(testname)
os.chdir(testname)

# Temporary (or perhaps not)
cur_version = sys.version_info
print('python version (should be >= 2.7):')
print(cur_version)

# Left as "cleanup" for potential future re-run ability.
cleanup_data_files()

if interval:
    filename = "/sys/kernel/debug/tracing/trace"
    clear_trace_file()
    set_trace_buffer_size()
    enable_trace()
    print('Sleeping for ', interval, 'seconds')
    time.sleep(int(interval))
    disable_trace()

current_max_cpu = 0

read_trace_data(filename)

if interval:
    clear_trace_file()
    # Free the memory
    free_trace_buffer()

if graph_data_present == False:
    print('No valid data to plot')
    sys.exit(2)

for cpu_no in range(0, current_max_cpu + 1):
    plot_perf_busy_with_sample(cpu_no)
    plot_perf_busy(cpu_no)
    plot_durations(cpu_no)
    plot_loads(cpu_no)

plot_pstate_cpu_with_sample()
plot_pstate_cpu()
plot_load_cpu()
plot_frequency_cpu()
plot_duration_cpu()
plot_scaled_cpu()
plot_boost_cpu()
plot_ghz_cpu()

# It is preferrable, but not necessary, that the regular user owns the files, not root.
for root, dirs, files in os.walk('.'):
    for f in files:
        fix_ownership(f)

os.chdir('../../')
