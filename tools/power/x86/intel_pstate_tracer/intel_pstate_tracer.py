#!/usr/bin/python
# -*- coding: utf-8 -*-
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
    (Most of the distributions have these required packages)

    HWP (Hardware P-States are disabled)
    Kernel config for Linux trace is enabled

    Usage:
        If the trace file is available, then to simply parse and plot, use
            sudo ./intel_pstate_tracer.py -t <trace_file>
        To generate trace file, parse and plot, use
            sudo ./intel_pstate_tracer.py -i <interval>
    Output:
        Creates a "data" folder in the current working directory and copies:
        cpu*.dat           - Raw performance data
        cpu_perf_busy*.png - Plots of P-States, Performance, busy
	                     and io_busy
        all_cpu_per.png    - Plot of P-State transition on each CPU

"""
from __future__ import print_function
import os
import time
import re
import sys
import getopt
import Gnuplot

__author__ = "Srinivas Pandruvada"
__copyright__ = " Copyright (c) 2017, Intel Corporation. "
__license__ = "GPL version 2"


MAX_CPUS = 256

def plot_perf_busy(cpu_index):
    """ Plot method to per cpu information """

    file_name = 'cpu' + str(cpu_index) + '.dat'
    if os.path.exists(file_name):

        output_png = "cpu_perf_busy%d.png" % cpu_index
        g_plot = Gnuplot.Gnuplot(persist=1)
        g_plot('set yrange [0:40]')
        g_plot('set y2range [0:100]')
        g_plot('set y2tics 0, 10')
        g_plot('set ytics nomirror')
        g_plot('set xlabel "Samples"')
        g_plot('set ylabel "P-State"')
        g_plot('set y2label "Scaled Busy"')
        g_plot('set term png size 1280, 720')
        g_plot('set output "' + output_png + '"')

        g_plot.plot(Gnuplot.File(file_name, using='1',
                                 with_="lines linecolor 'green' axis x1y2",
                                 title='performance'),
                    Gnuplot.File(file_name,
                                 using='2', with_="lines linecolor 'red' axis x1y2",
                                 title='scaled-busy'),
                    Gnuplot.File(file_name,
                                 using='4',
                                 with_="lines linecolor 'purple' axis x1y2",
                                 title='io-busy'),
                    Gnuplot.File(file_name, using='3',
                                 with_="lines linecolor 'blue' axis x1y1",
                                 title='P-State'))


def plot_perf_cpu():
    """ Plot all cpu information """

    if os.path.exists('cpu.dat'):
        output_png = 'all_cpu_perf.png'
        g_plot = Gnuplot.Gnuplot(persist=1)
        g_plot('set yrange [0:30]')
        g_plot('set xlabel "Samples"')
        g_plot('set ylabel "P-State"')
        g_plot('set term png size 1280, 720')
        g_plot('set output "' + output_png + '"')

        plot_str = 'plot for [col=1:' + str(current_max_cpu + 1) \
                              + "] 'cpu.dat' using 0:col with lines title columnheader"

        g_plot.__call__(plot_str)

def store_per_cpu_information(cup_index, perf, busy, pstate, io_boost_pct):
    """ Store information for each CPU """

    try:
        f_handle = open('cpu' + cup_index + '.dat', 'a')
        sep = "\t";
        f_handle.write(sep.join((perf, busy, pstate, io_boost_pct)) + '\n');
        f_handle.close()
    except:
        print('IO error cpu*.dat')
        return

    try:
        f_handle = open('cpu.dat', 'a')
        for index in range(0, int(cup_index)):
            f_handle.write('0\t')

        f_handle.write(pstate + '\t')
        for index in range(int(cup_index) + 1, MAX_CPUS):
            f_handle.write('0\t')
        f_handle.write('\n')
        f_handle.close()
    except:
        print('IO error cpu.dat')
        return

def cleanup_data_files():
    """ clean up existing data files """

    for index in range(0, MAX_CPUS):
        filename = 'cpu' + str(index) + '.dat'
        if os.path.exists(filename):
            os.remove(filename)

    if os.path.exists('cpu.dat'):
        os.remove('cpu.dat')
        f_handle = open('cpu.dat', 'a')
        for index in range(0, MAX_CPUS):
            f_handle.write('cpu' + str(index) + '\t')
        f_handle.close()

def clear_trace_file():
    """ Clear trace file """
    try:
        f_handle = open('/sys/kernel/debug/tracing/trace', 'w')
        f_handle.close()
    except:
        print('IO error clearing trace file ')
        quit()

def enable_trace():
    """ Enable trace """
    try:
       open('/sys/kernel/debug/tracing/events/power/pstate_sample/enable'
                 , 'w').write("1")
    except:
        print('IO error enabling trace ')
        quit()

def disable_trace():
    """ Enable trace """
    try:
       open('/sys/kernel/debug/tracing/events/power/pstate_sample/enable'
                 , 'w').write("0")
    except:
        print('IO error disabling trace ')
        quit()

def set_trace_buffer_size():
    """ Set trace buffer size """
    try:
       open('/sys/kernel/debug/tracing/buffer_size_kb'
                 , 'w').write("10240")
    except:
        print('IO error setting trace buffer size ')
        quit()

def read_and_plot_trace_data(filename):
    """ Read trace data and call for plot """

    global current_max_cpu
    try:
        data = open(filename, 'r').read()
    except:
        print('Error opening ', filename)
        quit()

    for line in data.splitlines():
        search_obj = \
            re.search(r'(^(.*?)\[)((\d+)[^\]])(.*?core_busy=)(\d+)(.*?scaled=)(\d+)(.*?from=)(\d+)(.*?to=)(\d+)(.*?mperf=)(\d+)(.*?aperf=)(\d+)(.*?tsc=)(\d+)(.*?freq=)(\d+)'
                      , line)

        if search_obj:
            cpu = search_obj.group(3)
            cpu_int = int(cpu)
            cpu = str(cpu_int)

            print('[\ncpu : ', cpu)

            core_busy = search_obj.group(6)
            print('core_busy : ', core_busy)

            scaled = search_obj.group(8)
            print('scaled : ', scaled)

            _from = search_obj.group(10)
            print('from : ', _from)

            _to = search_obj.group(12)
            print('to : ', _to)

            mperf = search_obj.group(14)
            print('mperf : ', mperf)

            aperf = search_obj.group(16)
            print('aperf : ', aperf)

            tsc = search_obj.group(18)
            print('tsc : ', tsc)

            freq = search_obj.group(20)
            print('freq : ', freq)

            # Not all kernel version has io_boost field

            io_boost = '0'
            search_obj = re.search(r'.*?io_boost=(\d+)', line)
            if search_obj:
                print('io_boost : ', search_obj.group(1))

            store_per_cpu_information(cpu, core_busy, scaled, _to, io_boost)

            if int(cpu) > current_max_cpu:
                current_max_cpu = int(cpu)

interval = ""
filename = ""
valid = False

try:
    opts, args = getopt.getopt(sys.argv[1:],"ht:i:",["trace_file=","interval="])
except getopt.GetoptError:
    print('intel_pstate_tracer.py -t <trace_file>')
    print('intel_pstate_tracer.py -i <interval>')
    sys.exit(2)
for opt, arg in opts:
    if opt == '-h':
        print('intel_pstate_tracer.py -t <trace_file>')
        print('intel_pstate_tracer.py -i <interval>')
        sys.exit()
    elif opt in ("-t", "--trace_file"):
        valid = True
        filename = arg
    elif opt in ("-i", "--interval"):
        valid = True
        interval = arg

if not valid:
        print('intel_pstate_tracer.py -t <trace_file>')
        print('intel_pstate_tracer.py -i <interval>')
        sys.exit()

if not os.path.exists('data'):
    os.mkdir('data')

os.chdir('data')

cleanup_data_files()

if interval:
    filename = "/sys/kernel/debug/tracing/trace"
    clear_trace_file()
    set_trace_buffer_size()
    enable_trace()
    print('Sleeping for ', interval, 'seconds')
    time.sleep(int(interval))

current_max_cpu = 0

read_and_plot_trace_data(filename)

if interval:
    disable_trace()

for cpu_no in range(0, current_max_cpu + 1):
    plot_perf_busy(cpu_no)

plot_perf_cpu()

os.chdir('../')
