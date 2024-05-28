#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# -*- coding: utf-8 -*-
#
""" This utility can be used to debug and tune the performance of the
AMD P-State driver. It imports intel_pstate_tracer to analyze AMD P-State
trace event.

Prerequisites:
    Python version 2.7.x or higher
    gnuplot 5.0 or higher
    gnuplot-py 1.8 or higher
    (Most of the distributions have these required packages. They may be called
     gnuplot-py, phython-gnuplot or phython3-gnuplot, gnuplot-nox, ... )

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
sys.path.append(os.path.join(os.path.dirname(__file__), "..", "intel_pstate_tracer"))
import intel_pstate_tracer as ipt

__license__ = "GPL version 2"

MAX_CPUS = 256
# Define the csv file columns
C_COMM = 15
C_ELAPSED = 14
C_SAMPLE = 13
C_DURATION = 12
C_LOAD = 11
C_TSC = 10
C_APERF = 9
C_MPERF = 8
C_FREQ = 7
C_MAX_PERF = 6
C_DES_PERF = 5
C_MIN_PERF = 4
C_USEC = 3
C_SEC = 2
C_CPU = 1

global sample_num, last_sec_cpu, last_usec_cpu, start_time, test_name, trace_file

getcontext().prec = 11

sample_num =0
last_sec_cpu = [0] * MAX_CPUS
last_usec_cpu = [0] * MAX_CPUS

def plot_per_cpu_freq(cpu_index):
    """ Plot per cpu frequency """

    file_name = 'cpu{:0>3}.csv'.format(cpu_index)
    if os.path.exists(file_name):
        output_png = "cpu%03d_frequency.png" % cpu_index
        g_plot = ipt.common_gnuplot_settings()
        g_plot('set output "' + output_png + '"')
        g_plot('set yrange [0:7]')
        g_plot('set ytics 0, 1')
        g_plot('set ylabel "CPU Frequency (GHz)"')
        g_plot('set title "{} : frequency : CPU {:0>3} : {:%F %H:%M}"'.format(test_name, cpu_index, datetime.now()))
        g_plot('set ylabel "CPU frequency"')
        g_plot('set key off')
        ipt.set_4_plot_linestyles(g_plot)
        g_plot('plot "' + file_name + '" using {:d}:{:d} with linespoints linestyle 1 axis x1y1'.format(C_ELAPSED, C_FREQ))

def plot_per_cpu_des_perf(cpu_index):
    """ Plot per cpu desired perf """

    file_name = 'cpu{:0>3}.csv'.format(cpu_index)
    if os.path.exists(file_name):
        output_png = "cpu%03d_des_perf.png" % cpu_index
        g_plot = ipt.common_gnuplot_settings()
        g_plot('set output "' + output_png + '"')
        g_plot('set yrange [0:255]')
        g_plot('set ylabel "des perf"')
        g_plot('set title "{} : cpu des perf : CPU {:0>3} : {:%F %H:%M}"'.format(test_name, cpu_index, datetime.now()))
        g_plot('set key off')
        ipt.set_4_plot_linestyles(g_plot)
        g_plot('plot "' + file_name + '" using {:d}:{:d} with linespoints linestyle 1 axis x1y1'.format(C_ELAPSED, C_DES_PERF))

def plot_per_cpu_load(cpu_index):
    """ Plot per cpu load """

    file_name = 'cpu{:0>3}.csv'.format(cpu_index)
    if os.path.exists(file_name):
        output_png = "cpu%03d_load.png" % cpu_index
        g_plot = ipt.common_gnuplot_settings()
        g_plot('set output "' + output_png + '"')
        g_plot('set yrange [0:100]')
        g_plot('set ytics 0, 10')
        g_plot('set ylabel "CPU load (percent)"')
        g_plot('set title "{} : cpu load : CPU {:0>3} : {:%F %H:%M}"'.format(test_name, cpu_index, datetime.now()))
        g_plot('set key off')
        ipt.set_4_plot_linestyles(g_plot)
        g_plot('plot "' + file_name + '" using {:d}:{:d} with linespoints linestyle 1 axis x1y1'.format(C_ELAPSED, C_LOAD))

def plot_all_cpu_frequency():
    """ Plot all cpu frequencies """

    output_png = 'all_cpu_frequencies.png'
    g_plot = ipt.common_gnuplot_settings()
    g_plot('set output "' + output_png + '"')
    g_plot('set ylabel "CPU Frequency (GHz)"')
    g_plot('set title "{} : cpu frequencies : {:%F %H:%M}"'.format(test_name, datetime.now()))

    title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).decode('utf-8').replace('\n', ' ')
    plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 7 ps 1 title i".format(C_ELAPSED, C_FREQ)
    g_plot('title_list = "{}"'.format(title_list))
    g_plot(plot_str)

def plot_all_cpu_des_perf():
    """ Plot all cpu desired perf """

    output_png = 'all_cpu_des_perf.png'
    g_plot = ipt.common_gnuplot_settings()
    g_plot('set output "' + output_png + '"')
    g_plot('set ylabel "des perf"')
    g_plot('set title "{} : cpu des perf : {:%F %H:%M}"'.format(test_name, datetime.now()))

    title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).decode('utf-8').replace('\n', ' ')
    plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 255 ps 1 title i".format(C_ELAPSED, C_DES_PERF)
    g_plot('title_list = "{}"'.format(title_list))
    g_plot(plot_str)

def plot_all_cpu_load():
    """ Plot all cpu load  """

    output_png = 'all_cpu_load.png'
    g_plot = ipt.common_gnuplot_settings()
    g_plot('set output "' + output_png + '"')
    g_plot('set yrange [0:100]')
    g_plot('set ylabel "CPU load (percent)"')
    g_plot('set title "{} : cpu load : {:%F %H:%M}"'.format(test_name, datetime.now()))

    title_list = subprocess.check_output('ls cpu???.csv | sed -e \'s/.csv//\'',shell=True).decode('utf-8').replace('\n', ' ')
    plot_str = "plot for [i in title_list] i.'.csv' using {:d}:{:d} pt 255 ps 1 title i".format(C_ELAPSED, C_LOAD)
    g_plot('title_list = "{}"'.format(title_list))
    g_plot(plot_str)

def store_csv(cpu_int, time_pre_dec, time_post_dec, min_perf, des_perf, max_perf, freq_ghz, mperf, aperf, tsc, common_comm, load, duration_ms, sample_num, elapsed_time, cpu_mask):
    """ Store master csv file information """

    global graph_data_present

    if cpu_mask[cpu_int] == 0:
        return

    try:
        f_handle = open('cpu.csv', 'a')
        string_buffer = "CPU_%03u, %05u, %06u, %u, %u, %u, %.4f, %u, %u, %u, %.2f, %.3f, %u, %.3f, %s\n" % (cpu_int, int(time_pre_dec), int(time_post_dec), int(min_perf), int(des_perf), int(max_perf), freq_ghz, int(mperf), int(aperf), int(tsc), load, duration_ms, sample_num, elapsed_time, common_comm)
        f_handle.write(string_buffer)
        f_handle.close()
    except:
        print('IO error cpu.csv')
        return

    graph_data_present = True;


def cleanup_data_files():
    """ clean up existing data files """

    if os.path.exists('cpu.csv'):
        os.remove('cpu.csv')
    f_handle = open('cpu.csv', 'a')
    f_handle.write('common_cpu, common_secs, common_usecs, min_perf, des_perf, max_perf, freq, mperf, aperf, tsc, load, duration_ms, sample_num, elapsed_time, common_comm')
    f_handle.write('\n')
    f_handle.close()

def read_trace_data(file_name, cpu_mask):
    """ Read and parse trace data """

    global current_max_cpu
    global sample_num, last_sec_cpu, last_usec_cpu, start_time

    try:
        data = open(file_name, 'r').read()
    except:
        print('Error opening ', file_name)
        sys.exit(2)

    for line in data.splitlines():
        search_obj = \
            re.search(r'(^(.*?)\[)((\d+)[^\]])(.*?)(\d+)([.])(\d+)(.*?amd_min_perf=)(\d+)(.*?amd_des_perf=)(\d+)(.*?amd_max_perf=)(\d+)(.*?freq=)(\d+)(.*?mperf=)(\d+)(.*?aperf=)(\d+)(.*?tsc=)(\d+)'
                      , line)

        if search_obj:
            cpu = search_obj.group(3)
            cpu_int = int(cpu)
            cpu = str(cpu_int)

            time_pre_dec = search_obj.group(6)
            time_post_dec = search_obj.group(8)
            min_perf = search_obj.group(10)
            des_perf = search_obj.group(12)
            max_perf = search_obj.group(14)
            freq = search_obj.group(16)
            mperf = search_obj.group(18)
            aperf = search_obj.group(20)
            tsc = search_obj.group(22)

            common_comm = search_obj.group(2).replace(' ', '')

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
                store_csv(cpu_int, time_pre_dec, time_post_dec, min_perf, des_perf, max_perf, freq_ghz, mperf, aperf, tsc, common_comm, load, duration_ms, sample_num, elapsed_time, cpu_mask)

            if cpu_int > current_max_cpu:
                current_max_cpu = cpu_int
# Now separate the main overall csv file into per CPU csv files.
    ipt.split_csv(current_max_cpu, cpu_mask)


def signal_handler(signal, frame):
    print(' SIGINT: Forcing cleanup before exit.')
    if interval:
        ipt.disable_trace(trace_file)
        ipt.clear_trace_file()
        ipt.free_trace_buffer()
        sys.exit(0)

trace_file = "/sys/kernel/tracing/events/amd_cpu/enable"
signal.signal(signal.SIGINT, signal_handler)

interval = ""
file_name = ""
cpu_list = ""
test_name = ""
memory = "10240"
graph_data_present = False;

valid1 = False
valid2 = False

cpu_mask = zeros((MAX_CPUS,), dtype=int)


try:
    opts, args = getopt.getopt(sys.argv[1:],"ht:i:c:n:m:",["help","trace_file=","interval=","cpu=","name=","memory="])
except getopt.GetoptError:
    ipt.print_help('amd_pstate')
    sys.exit(2)
for opt, arg in opts:
    if opt == '-h':
        print()
        sys.exit()
    elif opt in ("-t", "--trace_file"):
        valid1 = True
        location = os.path.realpath(os.path.join(os.getcwd(), os.path.dirname(__file__)))
        file_name = os.path.join(location, arg)
    elif opt in ("-i", "--interval"):
        valid1 = True
        interval = arg
    elif opt in ("-c", "--cpu"):
        cpu_list = arg
    elif opt in ("-n", "--name"):
        valid2 = True
        test_name = arg
    elif opt in ("-m", "--memory"):
        memory = arg

if not (valid1 and valid2):
    ipt.print_help('amd_pstate')
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
    ipt.fix_ownership('results')

os.chdir('results')
if os.path.exists(test_name):
    print('The test name directory already exists. Please provide a unique test name. Test re-run not supported, yet.')
    sys.exit()
os.mkdir(test_name)
ipt.fix_ownership(test_name)
os.chdir(test_name)

cur_version = sys.version_info
print('python version (should be >= 2.7):')
print(cur_version)

cleanup_data_files()

if interval:
    file_name = "/sys/kernel/tracing/trace"
    ipt.clear_trace_file()
    ipt.set_trace_buffer_size(memory)
    ipt.enable_trace(trace_file)
    time.sleep(int(interval))
    ipt.disable_trace(trace_file)

current_max_cpu = 0

read_trace_data(file_name, cpu_mask)

if interval:
    ipt.clear_trace_file()
    ipt.free_trace_buffer()

if graph_data_present == False:
    print('No valid data to plot')
    sys.exit(2)

for cpu_no in range(0, current_max_cpu + 1):
    plot_per_cpu_freq(cpu_no)
    plot_per_cpu_des_perf(cpu_no)
    plot_per_cpu_load(cpu_no)

plot_all_cpu_des_perf()
plot_all_cpu_frequency()
plot_all_cpu_load()

for root, dirs, files in os.walk('.'):
    for f in files:
        ipt.fix_ownership(f)

os.chdir('../../')
