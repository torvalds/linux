// SPDX-License-Identifier: GPL-2.0-only
/*
 * svghelper.c - helper functions for outputting svg
 *
 * (C) Copyright 2009 Intel Corporation
 *
 * Authors:
 *     Arjan van de Ven <arjan@linux.intel.com>
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/bitmap.h>
#include <linux/time64.h>

#include "perf.h"
#include "svghelper.h"
#include "util.h"
#include "cpumap.h"

static u64 first_time, last_time;
static u64 turbo_frequency, max_freq;


#define SLOT_MULT 30.0
#define SLOT_HEIGHT 25.0
#define SLOT_HALF (SLOT_HEIGHT / 2)

int svg_page_width = 1000;
u64 svg_highlight;
const char *svg_highlight_name;

#define MIN_TEXT_SIZE 0.01

static u64 total_height;
static FILE *svgfile;

static double cpu2slot(int cpu)
{
	return 2 * cpu + 1;
}

static int *topology_map;

static double cpu2y(int cpu)
{
	if (topology_map)
		return cpu2slot(topology_map[cpu]) * SLOT_MULT;
	else
		return cpu2slot(cpu) * SLOT_MULT;
}

static double time2pixels(u64 __time)
{
	double X;

	X = 1.0 * svg_page_width * (__time - first_time) / (last_time - first_time);
	return X;
}

/*
 * Round text sizes so that the svg viewer only needs a discrete
 * number of renderings of the font
 */
static double round_text_size(double size)
{
	int loop = 100;
	double target = 10.0;

	if (size >= 10.0)
		return size;
	while (loop--) {
		if (size >= target)
			return target;
		target = target / 2.0;
	}
	return size;
}

void open_svg(const char *filename, int cpus, int rows, u64 start, u64 end)
{
	int new_width;

	svgfile = fopen(filename, "w");
	if (!svgfile) {
		fprintf(stderr, "Cannot open %s for output\n", filename);
		return;
	}
	first_time = start;
	first_time = first_time / 100000000 * 100000000;
	last_time = end;

	/*
	 * if the recording is short, we default to a width of 1000, but
	 * for longer recordings we want at least 200 units of width per second
	 */
	new_width = (last_time - first_time) / 5000000;

	if (new_width > svg_page_width)
		svg_page_width = new_width;

	total_height = (1 + rows + cpu2slot(cpus)) * SLOT_MULT;
	fprintf(svgfile, "<?xml version=\"1.0\" standalone=\"no\"?> \n");
	fprintf(svgfile, "<!DOCTYPE svg SYSTEM \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n");
	fprintf(svgfile, "<svg width=\"%i\" height=\"%" PRIu64 "\" version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\">\n", svg_page_width, total_height);

	fprintf(svgfile, "<defs>\n  <style type=\"text/css\">\n    <![CDATA[\n");

	fprintf(svgfile, "      rect          { stroke-width: 1; }\n");
	fprintf(svgfile, "      rect.process  { fill:rgb(180,180,180); fill-opacity:0.9; stroke-width:1;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.process2 { fill:rgb(180,180,180); fill-opacity:0.9; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.process3 { fill:rgb(180,180,180); fill-opacity:0.5; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.sample   { fill:rgb(  0,  0,255); fill-opacity:0.8; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.sample_hi{ fill:rgb(255,128,  0); fill-opacity:0.8; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.error    { fill:rgb(255,  0,  0); fill-opacity:0.5; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.net      { fill:rgb(  0,128,  0); fill-opacity:0.5; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.disk     { fill:rgb(  0,  0,255); fill-opacity:0.5; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.sync     { fill:rgb(128,128,  0); fill-opacity:0.5; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.poll     { fill:rgb(  0,128,128); fill-opacity:0.2; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.blocked  { fill:rgb(255,  0,  0); fill-opacity:0.5; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.waiting  { fill:rgb(224,214,  0); fill-opacity:0.8; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.WAITING  { fill:rgb(255,214, 48); fill-opacity:0.6; stroke-width:0;   stroke:rgb(  0,  0,  0); } \n");
	fprintf(svgfile, "      rect.cpu      { fill:rgb(192,192,192); fill-opacity:0.2; stroke-width:0.5; stroke:rgb(128,128,128); } \n");
	fprintf(svgfile, "      rect.pstate   { fill:rgb(128,128,128); fill-opacity:0.8; stroke-width:0; } \n");
	fprintf(svgfile, "      rect.c1       { fill:rgb(255,214,214); fill-opacity:0.5; stroke-width:0; } \n");
	fprintf(svgfile, "      rect.c2       { fill:rgb(255,172,172); fill-opacity:0.5; stroke-width:0; } \n");
	fprintf(svgfile, "      rect.c3       { fill:rgb(255,130,130); fill-opacity:0.5; stroke-width:0; } \n");
	fprintf(svgfile, "      rect.c4       { fill:rgb(255, 88, 88); fill-opacity:0.5; stroke-width:0; } \n");
	fprintf(svgfile, "      rect.c5       { fill:rgb(255, 44, 44); fill-opacity:0.5; stroke-width:0; } \n");
	fprintf(svgfile, "      rect.c6       { fill:rgb(255,  0,  0); fill-opacity:0.5; stroke-width:0; } \n");
	fprintf(svgfile, "      line.pstate   { stroke:rgb(255,255,  0); stroke-opacity:0.8; stroke-width:2; } \n");

	fprintf(svgfile, "    ]]>\n   </style>\n</defs>\n");
}

static double normalize_height(double height)
{
	if (height < 0.25)
		return 0.25;
	else if (height < 0.50)
		return 0.50;
	else if (height < 0.75)
		return 0.75;
	else
		return 0.100;
}

void svg_ubox(int Yslot, u64 start, u64 end, double height, const char *type, int fd, int err, int merges)
{
	double w = time2pixels(end) - time2pixels(start);
	height = normalize_height(height);

	if (!svgfile)
		return;

	fprintf(svgfile, "<g>\n");
	fprintf(svgfile, "<title>fd=%d error=%d merges=%d</title>\n", fd, err, merges);
	fprintf(svgfile, "<rect x=\"%.8f\" width=\"%.8f\" y=\"%.1f\" height=\"%.1f\" class=\"%s\"/>\n",
		time2pixels(start),
		w,
		Yslot * SLOT_MULT,
		SLOT_HALF * height,
		type);
	fprintf(svgfile, "</g>\n");
}

void svg_lbox(int Yslot, u64 start, u64 end, double height, const char *type, int fd, int err, int merges)
{
	double w = time2pixels(end) - time2pixels(start);
	height = normalize_height(height);

	if (!svgfile)
		return;

	fprintf(svgfile, "<g>\n");
	fprintf(svgfile, "<title>fd=%d error=%d merges=%d</title>\n", fd, err, merges);
	fprintf(svgfile, "<rect x=\"%.8f\" width=\"%.8f\" y=\"%.1f\" height=\"%.1f\" class=\"%s\"/>\n",
		time2pixels(start),
		w,
		Yslot * SLOT_MULT + SLOT_HEIGHT - SLOT_HALF * height,
		SLOT_HALF * height,
		type);
	fprintf(svgfile, "</g>\n");
}

void svg_fbox(int Yslot, u64 start, u64 end, double height, const char *type, int fd, int err, int merges)
{
	double w = time2pixels(end) - time2pixels(start);
	height = normalize_height(height);

	if (!svgfile)
		return;

	fprintf(svgfile, "<g>\n");
	fprintf(svgfile, "<title>fd=%d error=%d merges=%d</title>\n", fd, err, merges);
	fprintf(svgfile, "<rect x=\"%.8f\" width=\"%.8f\" y=\"%.1f\" height=\"%.1f\" class=\"%s\"/>\n",
		time2pixels(start),
		w,
		Yslot * SLOT_MULT + SLOT_HEIGHT - SLOT_HEIGHT * height,
		SLOT_HEIGHT * height,
		type);
	fprintf(svgfile, "</g>\n");
}

void svg_box(int Yslot, u64 start, u64 end, const char *type)
{
	if (!svgfile)
		return;

	fprintf(svgfile, "<rect x=\"%.8f\" width=\"%.8f\" y=\"%.1f\" height=\"%.1f\" class=\"%s\"/>\n",
		time2pixels(start), time2pixels(end)-time2pixels(start), Yslot * SLOT_MULT, SLOT_HEIGHT, type);
}

static char *time_to_string(u64 duration);
void svg_blocked(int Yslot, int cpu, u64 start, u64 end, const char *backtrace)
{
	if (!svgfile)
		return;

	fprintf(svgfile, "<g>\n");
	fprintf(svgfile, "<title>#%d blocked %s</title>\n", cpu,
		time_to_string(end - start));
	if (backtrace)
		fprintf(svgfile, "<desc>Blocked on:\n%s</desc>\n", backtrace);
	svg_box(Yslot, start, end, "blocked");
	fprintf(svgfile, "</g>\n");
}

void svg_running(int Yslot, int cpu, u64 start, u64 end, const char *backtrace)
{
	double text_size;
	const char *type;

	if (!svgfile)
		return;

	if (svg_highlight && end - start > svg_highlight)
		type = "sample_hi";
	else
		type = "sample";
	fprintf(svgfile, "<g>\n");

	fprintf(svgfile, "<title>#%d running %s</title>\n",
		cpu, time_to_string(end - start));
	if (backtrace)
		fprintf(svgfile, "<desc>Switched because:\n%s</desc>\n", backtrace);
	fprintf(svgfile, "<rect x=\"%.8f\" width=\"%.8f\" y=\"%.1f\" height=\"%.1f\" class=\"%s\"/>\n",
		time2pixels(start), time2pixels(end)-time2pixels(start), Yslot * SLOT_MULT, SLOT_HEIGHT,
		type);

	text_size = (time2pixels(end)-time2pixels(start));
	if (cpu > 9)
		text_size = text_size/2;
	if (text_size > 1.25)
		text_size = 1.25;
	text_size = round_text_size(text_size);

	if (text_size > MIN_TEXT_SIZE)
		fprintf(svgfile, "<text x=\"%.8f\" y=\"%.8f\" font-size=\"%.8fpt\">%i</text>\n",
			time2pixels(start), Yslot *  SLOT_MULT + SLOT_HEIGHT - 1, text_size,  cpu + 1);

	fprintf(svgfile, "</g>\n");
}

static char *time_to_string(u64 duration)
{
	static char text[80];

	text[0] = 0;

	if (duration < NSEC_PER_USEC) /* less than 1 usec */
		return text;

	if (duration < NSEC_PER_MSEC) { /* less than 1 msec */
		sprintf(text, "%.1f us", duration / (double)NSEC_PER_USEC);
		return text;
	}
	sprintf(text, "%.1f ms", duration / (double)NSEC_PER_MSEC);

	return text;
}

void svg_waiting(int Yslot, int cpu, u64 start, u64 end, const char *backtrace)
{
	char *text;
	const char *style;
	double font_size;

	if (!svgfile)
		return;

	style = "waiting";

	if (end-start > 10 * NSEC_PER_MSEC) /* 10 msec */
		style = "WAITING";

	text = time_to_string(end-start);

	font_size = 1.0 * (time2pixels(end)-time2pixels(start));

	if (font_size > 3)
		font_size = 3;

	font_size = round_text_size(font_size);

	fprintf(svgfile, "<g transform=\"translate(%.8f,%.8f)\">\n", time2pixels(start), Yslot * SLOT_MULT);
	fprintf(svgfile, "<title>#%d waiting %s</title>\n", cpu, time_to_string(end - start));
	if (backtrace)
		fprintf(svgfile, "<desc>Waiting on:\n%s</desc>\n", backtrace);
	fprintf(svgfile, "<rect x=\"0\" width=\"%.8f\" y=\"0\" height=\"%.1f\" class=\"%s\"/>\n",
		time2pixels(end)-time2pixels(start), SLOT_HEIGHT, style);
	if (font_size > MIN_TEXT_SIZE)
		fprintf(svgfile, "<text transform=\"rotate(90)\" font-size=\"%.8fpt\"> %s</text>\n",
			font_size, text);
	fprintf(svgfile, "</g>\n");
}

static char *cpu_model(void)
{
	static char cpu_m[255];
	char buf[256];
	FILE *file;

	cpu_m[0] = 0;
	/* CPU type */
	file = fopen("/proc/cpuinfo", "r");
	if (file) {
		while (fgets(buf, 255, file)) {
			if (strstr(buf, "model name")) {
				strlcpy(cpu_m, &buf[13], 255);
				break;
			}
		}
		fclose(file);
	}

	/* CPU type */
	file = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies", "r");
	if (file) {
		while (fgets(buf, 255, file)) {
			unsigned int freq;
			freq = strtoull(buf, NULL, 10);
			if (freq > max_freq)
				max_freq = freq;
		}
		fclose(file);
	}
	return cpu_m;
}

void svg_cpu_box(int cpu, u64 __max_freq, u64 __turbo_freq)
{
	char cpu_string[80];
	if (!svgfile)
		return;

	max_freq = __max_freq;
	turbo_frequency = __turbo_freq;

	fprintf(svgfile, "<g>\n");

	fprintf(svgfile, "<rect x=\"%.8f\" width=\"%.8f\" y=\"%.1f\" height=\"%.1f\" class=\"cpu\"/>\n",
		time2pixels(first_time),
		time2pixels(last_time)-time2pixels(first_time),
		cpu2y(cpu), SLOT_MULT+SLOT_HEIGHT);

	sprintf(cpu_string, "CPU %i", (int)cpu);
	fprintf(svgfile, "<text x=\"%.8f\" y=\"%.8f\">%s</text>\n",
		10+time2pixels(first_time), cpu2y(cpu) + SLOT_HEIGHT/2, cpu_string);

	fprintf(svgfile, "<text transform=\"translate(%.8f,%.8f)\" font-size=\"1.25pt\">%s</text>\n",
		10+time2pixels(first_time), cpu2y(cpu) + SLOT_MULT + SLOT_HEIGHT - 4, cpu_model());

	fprintf(svgfile, "</g>\n");
}

void svg_process(int cpu, u64 start, u64 end, int pid, const char *name, const char *backtrace)
{
	double width;
	const char *type;

	if (!svgfile)
		return;

	if (svg_highlight && end - start >= svg_highlight)
		type = "sample_hi";
	else if (svg_highlight_name && strstr(name, svg_highlight_name))
		type = "sample_hi";
	else
		type = "sample";

	fprintf(svgfile, "<g transform=\"translate(%.8f,%.8f)\">\n", time2pixels(start), cpu2y(cpu));
	fprintf(svgfile, "<title>%d %s running %s</title>\n", pid, name, time_to_string(end - start));
	if (backtrace)
		fprintf(svgfile, "<desc>Switched because:\n%s</desc>\n", backtrace);
	fprintf(svgfile, "<rect x=\"0\" width=\"%.8f\" y=\"0\" height=\"%.1f\" class=\"%s\"/>\n",
		time2pixels(end)-time2pixels(start), SLOT_MULT+SLOT_HEIGHT, type);
	width = time2pixels(end)-time2pixels(start);
	if (width > 6)
		width = 6;

	width = round_text_size(width);

	if (width > MIN_TEXT_SIZE)
		fprintf(svgfile, "<text transform=\"rotate(90)\" font-size=\"%.8fpt\">%s</text>\n",
			width, name);

	fprintf(svgfile, "</g>\n");
}

void svg_cstate(int cpu, u64 start, u64 end, int type)
{
	double width;
	char style[128];

	if (!svgfile)
		return;


	fprintf(svgfile, "<g>\n");

	if (type > 6)
		type = 6;
	sprintf(style, "c%i", type);

	fprintf(svgfile, "<rect class=\"%s\" x=\"%.8f\" width=\"%.8f\" y=\"%.1f\" height=\"%.1f\"/>\n",
		style,
		time2pixels(start), time2pixels(end)-time2pixels(start),
		cpu2y(cpu), SLOT_MULT+SLOT_HEIGHT);

	width = (time2pixels(end)-time2pixels(start))/2.0;
	if (width > 6)
		width = 6;

	width = round_text_size(width);

	if (width > MIN_TEXT_SIZE)
		fprintf(svgfile, "<text x=\"%.8f\" y=\"%.8f\" font-size=\"%.8fpt\">C%i</text>\n",
			time2pixels(start), cpu2y(cpu)+width, width, type);

	fprintf(svgfile, "</g>\n");
}

static char *HzToHuman(unsigned long hz)
{
	static char buffer[1024];
	unsigned long long Hz;

	memset(buffer, 0, 1024);

	Hz = hz;

	/* default: just put the Number in */
	sprintf(buffer, "%9lli", Hz);

	if (Hz > 1000)
		sprintf(buffer, " %6lli Mhz", (Hz+500)/1000);

	if (Hz > 1500000)
		sprintf(buffer, " %6.2f Ghz", (Hz+5000.0)/1000000);

	if (Hz == turbo_frequency)
		sprintf(buffer, "Turbo");

	return buffer;
}

void svg_pstate(int cpu, u64 start, u64 end, u64 freq)
{
	double height = 0;

	if (!svgfile)
		return;

	fprintf(svgfile, "<g>\n");

	if (max_freq)
		height = freq * 1.0 / max_freq * (SLOT_HEIGHT + SLOT_MULT);
	height = 1 + cpu2y(cpu) + SLOT_MULT + SLOT_HEIGHT - height;
	fprintf(svgfile, "<line x1=\"%.8f\" x2=\"%.8f\" y1=\"%.1f\" y2=\"%.1f\" class=\"pstate\"/>\n",
		time2pixels(start), time2pixels(end), height, height);
	fprintf(svgfile, "<text x=\"%.8f\" y=\"%.8f\" font-size=\"0.25pt\">%s</text>\n",
		time2pixels(start), height+0.9, HzToHuman(freq));

	fprintf(svgfile, "</g>\n");
}


void svg_partial_wakeline(u64 start, int row1, char *desc1, int row2, char *desc2, const char *backtrace)
{
	double height;

	if (!svgfile)
		return;


	fprintf(svgfile, "<g>\n");

	fprintf(svgfile, "<title>%s wakes up %s</title>\n",
		desc1 ? desc1 : "?",
		desc2 ? desc2 : "?");

	if (backtrace)
		fprintf(svgfile, "<desc>%s</desc>\n", backtrace);

	if (row1 < row2) {
		if (row1) {
			fprintf(svgfile, "<line x1=\"%.8f\" y1=\"%.2f\" x2=\"%.8f\" y2=\"%.2f\" style=\"stroke:rgb(32,255,32);stroke-width:0.009\"/>\n",
				time2pixels(start), row1 * SLOT_MULT + SLOT_HEIGHT,  time2pixels(start), row1 * SLOT_MULT + SLOT_HEIGHT + SLOT_MULT/32);
			if (desc2)
				fprintf(svgfile, "<g transform=\"translate(%.8f,%.8f)\"><text transform=\"rotate(90)\" font-size=\"0.02pt\">%s &gt;</text></g>\n",
					time2pixels(start), row1 * SLOT_MULT + SLOT_HEIGHT + SLOT_HEIGHT/48, desc2);
		}
		if (row2) {
			fprintf(svgfile, "<line x1=\"%.8f\" y1=\"%.2f\" x2=\"%.8f\" y2=\"%.2f\" style=\"stroke:rgb(32,255,32);stroke-width:0.009\"/>\n",
				time2pixels(start), row2 * SLOT_MULT - SLOT_MULT/32,  time2pixels(start), row2 * SLOT_MULT);
			if (desc1)
				fprintf(svgfile, "<g transform=\"translate(%.8f,%.8f)\"><text transform=\"rotate(90)\" font-size=\"0.02pt\">%s &gt;</text></g>\n",
					time2pixels(start), row2 * SLOT_MULT - SLOT_MULT/32, desc1);
		}
	} else {
		if (row2) {
			fprintf(svgfile, "<line x1=\"%.8f\" y1=\"%.2f\" x2=\"%.8f\" y2=\"%.2f\" style=\"stroke:rgb(32,255,32);stroke-width:0.009\"/>\n",
				time2pixels(start), row2 * SLOT_MULT + SLOT_HEIGHT,  time2pixels(start), row2 * SLOT_MULT + SLOT_HEIGHT + SLOT_MULT/32);
			if (desc1)
				fprintf(svgfile, "<g transform=\"translate(%.8f,%.8f)\"><text transform=\"rotate(90)\" font-size=\"0.02pt\">%s &lt;</text></g>\n",
					time2pixels(start), row2 * SLOT_MULT + SLOT_HEIGHT + SLOT_MULT/48, desc1);
		}
		if (row1) {
			fprintf(svgfile, "<line x1=\"%.8f\" y1=\"%.2f\" x2=\"%.8f\" y2=\"%.2f\" style=\"stroke:rgb(32,255,32);stroke-width:0.009\"/>\n",
				time2pixels(start), row1 * SLOT_MULT - SLOT_MULT/32,  time2pixels(start), row1 * SLOT_MULT);
			if (desc2)
				fprintf(svgfile, "<g transform=\"translate(%.8f,%.8f)\"><text transform=\"rotate(90)\" font-size=\"0.02pt\">%s &lt;</text></g>\n",
					time2pixels(start), row1 * SLOT_MULT - SLOT_HEIGHT/32, desc2);
		}
	}
	height = row1 * SLOT_MULT;
	if (row2 > row1)
		height += SLOT_HEIGHT;
	if (row1)
		fprintf(svgfile, "<circle  cx=\"%.8f\" cy=\"%.2f\" r = \"0.01\"  style=\"fill:rgb(32,255,32)\"/>\n",
			time2pixels(start), height);

	fprintf(svgfile, "</g>\n");
}

void svg_wakeline(u64 start, int row1, int row2, const char *backtrace)
{
	double height;

	if (!svgfile)
		return;


	fprintf(svgfile, "<g>\n");

	if (backtrace)
		fprintf(svgfile, "<desc>%s</desc>\n", backtrace);

	if (row1 < row2)
		fprintf(svgfile, "<line x1=\"%.8f\" y1=\"%.2f\" x2=\"%.8f\" y2=\"%.2f\" style=\"stroke:rgb(32,255,32);stroke-width:0.009\"/>\n",
			time2pixels(start), row1 * SLOT_MULT + SLOT_HEIGHT,  time2pixels(start), row2 * SLOT_MULT);
	else
		fprintf(svgfile, "<line x1=\"%.8f\" y1=\"%.2f\" x2=\"%.8f\" y2=\"%.2f\" style=\"stroke:rgb(32,255,32);stroke-width:0.009\"/>\n",
			time2pixels(start), row2 * SLOT_MULT + SLOT_HEIGHT,  time2pixels(start), row1 * SLOT_MULT);

	height = row1 * SLOT_MULT;
	if (row2 > row1)
		height += SLOT_HEIGHT;
	fprintf(svgfile, "<circle  cx=\"%.8f\" cy=\"%.2f\" r = \"0.01\"  style=\"fill:rgb(32,255,32)\"/>\n",
			time2pixels(start), height);

	fprintf(svgfile, "</g>\n");
}

void svg_interrupt(u64 start, int row, const char *backtrace)
{
	if (!svgfile)
		return;

	fprintf(svgfile, "<g>\n");

	fprintf(svgfile, "<title>Wakeup from interrupt</title>\n");

	if (backtrace)
		fprintf(svgfile, "<desc>%s</desc>\n", backtrace);

	fprintf(svgfile, "<circle  cx=\"%.8f\" cy=\"%.2f\" r = \"0.01\"  style=\"fill:rgb(255,128,128)\"/>\n",
			time2pixels(start), row * SLOT_MULT);
	fprintf(svgfile, "<circle  cx=\"%.8f\" cy=\"%.2f\" r = \"0.01\"  style=\"fill:rgb(255,128,128)\"/>\n",
			time2pixels(start), row * SLOT_MULT + SLOT_HEIGHT);

	fprintf(svgfile, "</g>\n");
}

void svg_text(int Yslot, u64 start, const char *text)
{
	if (!svgfile)
		return;

	fprintf(svgfile, "<text x=\"%.8f\" y=\"%.8f\">%s</text>\n",
		time2pixels(start), Yslot * SLOT_MULT+SLOT_HEIGHT/2, text);
}

static void svg_legenda_box(int X, const char *text, const char *style)
{
	double boxsize;
	boxsize = SLOT_HEIGHT / 2;

	fprintf(svgfile, "<rect x=\"%i\" width=\"%.8f\" y=\"0\" height=\"%.1f\" class=\"%s\"/>\n",
		X, boxsize, boxsize, style);
	fprintf(svgfile, "<text transform=\"translate(%.8f, %.8f)\" font-size=\"%.8fpt\">%s</text>\n",
		X + boxsize + 5, boxsize, 0.8 * boxsize, text);
}

void svg_io_legenda(void)
{
	if (!svgfile)
		return;

	fprintf(svgfile, "<g>\n");
	svg_legenda_box(0,	"Disk", "disk");
	svg_legenda_box(100,	"Network", "net");
	svg_legenda_box(200,	"Sync", "sync");
	svg_legenda_box(300,	"Poll", "poll");
	svg_legenda_box(400,	"Error", "error");
	fprintf(svgfile, "</g>\n");
}

void svg_legenda(void)
{
	if (!svgfile)
		return;

	fprintf(svgfile, "<g>\n");
	svg_legenda_box(0,	"Running", "sample");
	svg_legenda_box(100,	"Idle","c1");
	svg_legenda_box(200,	"Deeper Idle", "c3");
	svg_legenda_box(350,	"Deepest Idle", "c6");
	svg_legenda_box(550,	"Sleeping", "process2");
	svg_legenda_box(650,	"Waiting for cpu", "waiting");
	svg_legenda_box(800,	"Blocked on IO", "blocked");
	fprintf(svgfile, "</g>\n");
}

void svg_time_grid(double min_thickness)
{
	u64 i;

	if (!svgfile)
		return;

	i = first_time;
	while (i < last_time) {
		int color = 220;
		double thickness = 0.075;
		if ((i % 100000000) == 0) {
			thickness = 0.5;
			color = 192;
		}
		if ((i % 1000000000) == 0) {
			thickness = 2.0;
			color = 128;
		}

		if (thickness >= min_thickness)
			fprintf(svgfile, "<line x1=\"%.8f\" y1=\"%.2f\" x2=\"%.8f\" y2=\"%" PRIu64 "\" style=\"stroke:rgb(%i,%i,%i);stroke-width:%.3f\"/>\n",
				time2pixels(i), SLOT_MULT/2, time2pixels(i),
				total_height, color, color, color, thickness);

		i += 10000000;
	}
}

void svg_close(void)
{
	if (svgfile) {
		fprintf(svgfile, "</svg>\n");
		fclose(svgfile);
		svgfile = NULL;
	}
}

#define cpumask_bits(maskp) ((maskp)->bits)
typedef struct { DECLARE_BITMAP(bits, MAX_NR_CPUS); } cpumask_t;

struct topology {
	cpumask_t *sib_core;
	int sib_core_nr;
	cpumask_t *sib_thr;
	int sib_thr_nr;
};

static void scan_thread_topology(int *map, struct topology *t, int cpu, int *pos)
{
	int i;
	int thr;

	for (i = 0; i < t->sib_thr_nr; i++) {
		if (!test_bit(cpu, cpumask_bits(&t->sib_thr[i])))
			continue;

		for_each_set_bit(thr,
				 cpumask_bits(&t->sib_thr[i]),
				 MAX_NR_CPUS)
			if (map[thr] == -1)
				map[thr] = (*pos)++;
	}
}

static void scan_core_topology(int *map, struct topology *t)
{
	int pos = 0;
	int i;
	int cpu;

	for (i = 0; i < t->sib_core_nr; i++)
		for_each_set_bit(cpu,
				 cpumask_bits(&t->sib_core[i]),
				 MAX_NR_CPUS)
			scan_thread_topology(map, t, cpu, &pos);
}

static int str_to_bitmap(char *s, cpumask_t *b)
{
	int i;
	int ret = 0;
	struct cpu_map *m;
	int c;

	m = cpu_map__new(s);
	if (!m)
		return -1;

	for (i = 0; i < m->nr; i++) {
		c = m->map[i];
		if (c >= MAX_NR_CPUS) {
			ret = -1;
			break;
		}

		set_bit(c, cpumask_bits(b));
	}

	cpu_map__put(m);

	return ret;
}

int svg_build_topology_map(char *sib_core, int sib_core_nr,
			   char *sib_thr, int sib_thr_nr)
{
	int i;
	struct topology t;

	t.sib_core_nr = sib_core_nr;
	t.sib_thr_nr = sib_thr_nr;
	t.sib_core = calloc(sib_core_nr, sizeof(cpumask_t));
	t.sib_thr = calloc(sib_thr_nr, sizeof(cpumask_t));

	if (!t.sib_core || !t.sib_thr) {
		fprintf(stderr, "topology: no memory\n");
		goto exit;
	}

	for (i = 0; i < sib_core_nr; i++) {
		if (str_to_bitmap(sib_core, &t.sib_core[i])) {
			fprintf(stderr, "topology: can't parse siblings map\n");
			goto exit;
		}

		sib_core += strlen(sib_core) + 1;
	}

	for (i = 0; i < sib_thr_nr; i++) {
		if (str_to_bitmap(sib_thr, &t.sib_thr[i])) {
			fprintf(stderr, "topology: can't parse siblings map\n");
			goto exit;
		}

		sib_thr += strlen(sib_thr) + 1;
	}

	topology_map = malloc(sizeof(int) * MAX_NR_CPUS);
	if (!topology_map) {
		fprintf(stderr, "topology: no memory\n");
		goto exit;
	}

	for (i = 0; i < MAX_NR_CPUS; i++)
		topology_map[i] = -1;

	scan_core_topology(topology_map, &t);

	return 0;

exit:
	zfree(&t.sib_core);
	zfree(&t.sib_thr);

	return -1;
}
