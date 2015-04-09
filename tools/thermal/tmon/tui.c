/*
 * tui.c ncurses text user interface for TMON program
 *
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 or later as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Jacob Pan <jacob.jun.pan@linux.intel.com>
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ncurses.h>
#include <time.h>
#include <syslog.h>
#include <panel.h>
#include <pthread.h>
#include <signal.h>

#include "tmon.h"

#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })

static PANEL *data_panel;
static PANEL *dialogue_panel;
static PANEL *top;

static WINDOW *title_bar_window;
static WINDOW *tz_sensor_window;
static WINDOW *cooling_device_window;
static WINDOW *control_window;
static WINDOW *status_bar_window;
static WINDOW *thermal_data_window;
static WINDOW *dialogue_window;

char status_bar_slots[10][40];
static void draw_hbar(WINDOW *win, int y, int start, int len,
		unsigned long pattern, bool end);

static int maxx, maxy;
static int maxwidth = 200;

#define TITLE_BAR_HIGHT 1
#define SENSOR_WIN_HIGHT 4 /* one row for tz name, one for trip points */


/* daemon mode flag (set by startup parameter -d) */
static int  tui_disabled;

static void close_panel(PANEL *p)
{
	if (p) {
		del_panel(p);
		p = NULL;
	}
}

static void close_window(WINDOW *win)
{
	if (win) {
		delwin(win);
		win = NULL;
	}
}

void close_windows(void)
{
	if (tui_disabled)
		return;
	/* must delete panels before their attached windows */
	if (dialogue_window)
		close_panel(dialogue_panel);
	if (cooling_device_window)
		close_panel(data_panel);

	close_window(title_bar_window);
	close_window(tz_sensor_window);
	close_window(status_bar_window);
	close_window(cooling_device_window);
	close_window(control_window);
	close_window(thermal_data_window);
	close_window(dialogue_window);

}

void write_status_bar(int x, char *line)
{
	mvwprintw(status_bar_window, 0, x, "%s", line);
	wrefresh(status_bar_window);
}

/* wrap at 5 */
#define DIAG_DEV_ROWS  5
/*
 * list cooling devices + "set temp" entry; wraps after 5 rows, if they fit
 */
static int diag_dev_rows(void)
{
	int entries = ptdata.nr_cooling_dev + 1;
	int rows = max(DIAG_DEV_ROWS, (entries + 1) / 2);
	return min(rows, entries);
}

void setup_windows(void)
{
	int y_begin = 1;

	if (tui_disabled)
		return;

	getmaxyx(stdscr, maxy, maxx);
	resizeterm(maxy, maxx);

	title_bar_window = subwin(stdscr, TITLE_BAR_HIGHT, maxx, 0, 0);
	y_begin += TITLE_BAR_HIGHT;

	tz_sensor_window = subwin(stdscr, SENSOR_WIN_HIGHT, maxx, y_begin, 0);
	y_begin += SENSOR_WIN_HIGHT;

	cooling_device_window = subwin(stdscr, ptdata.nr_cooling_dev + 3, maxx,
				y_begin, 0);
	y_begin += ptdata.nr_cooling_dev + 3; /* 2 lines for border */
	/* two lines to show borders, one line per tz show trip point position
	 * and value.
	 * dialogue window is a pop-up, when needed it lays on top of cdev win
	 */

	dialogue_window = subwin(stdscr, diag_dev_rows() + 5, maxx-50,
				DIAG_Y, DIAG_X);

	thermal_data_window = subwin(stdscr, ptdata.nr_tz_sensor *
				NR_LINES_TZDATA + 3, maxx, y_begin, 0);
	y_begin += ptdata.nr_tz_sensor * NR_LINES_TZDATA + 3;
	control_window = subwin(stdscr, 4, maxx, y_begin, 0);

	scrollok(cooling_device_window, TRUE);
	maxwidth = maxx - 18;
	status_bar_window = subwin(stdscr, 1, maxx, maxy-1, 0);

	strcpy(status_bar_slots[0], " Ctrl-c - Quit ");
	strcpy(status_bar_slots[1], " TAB - Tuning ");
	wmove(status_bar_window, 1, 30);

	/* prepare panels for dialogue, if panel already created then we must
	 * be doing resizing, so just replace windows with new ones, old ones
	 * should have been deleted by close_window
	 */
	data_panel = new_panel(cooling_device_window);
	if (!data_panel)
		syslog(LOG_DEBUG, "No data panel\n");
	else {
		if (dialogue_window) {
			dialogue_panel = new_panel(dialogue_window);
			if (!dialogue_panel)
				syslog(LOG_DEBUG, "No dialogue panel\n");
			else {
				/* Set up the user pointer to the next panel*/
				set_panel_userptr(data_panel, dialogue_panel);
				set_panel_userptr(dialogue_panel, data_panel);
				top = data_panel;
			}
		} else
			syslog(LOG_INFO, "no dialogue win, term too small\n");
	}
	doupdate();
	werase(stdscr);
	refresh();
}

void resize_handler(int sig)
{
	/* start over when term gets resized, but first we clean up */
	close_windows();
	endwin();
	refresh();
	clear();
	getmaxyx(stdscr, maxy, maxx);  /* get the new screen size */
	setup_windows();
	/* rate limit */
	sleep(1);
	syslog(LOG_DEBUG, "SIG %d, term resized to %d x %d\n",
		sig, maxy, maxx);
	signal(SIGWINCH, resize_handler);
}

const char cdev_title[] = " COOLING DEVICES ";
void show_cooling_device(void)
{
	int i, j, x, y = 0;

	if (tui_disabled || !cooling_device_window)
		return;

	werase(cooling_device_window);
	wattron(cooling_device_window, A_BOLD);
	mvwprintw(cooling_device_window,  1, 1,
		"ID  Cooling Dev   Cur    Max   Thermal Zone Binding");
	wattroff(cooling_device_window, A_BOLD);
	for (j = 0; j <	ptdata.nr_cooling_dev; j++) {
		/* draw cooling device list on the left in the order of
		 * cooling device instances. skip unused idr.
		 */
		mvwprintw(cooling_device_window, j + 2, 1,
			"%02d %12.12s%6d %6d",
			ptdata.cdi[j].instance,
			ptdata.cdi[j].type,
			ptdata.cdi[j].cur_state,
			ptdata.cdi[j].max_state);
	}

	/* show cdev binding, y is the global cooling device instance */
	for (i = 0; i < ptdata.nr_tz_sensor; i++) {
		int tz_inst = ptdata.tzi[i].instance;
		for (j = 0; j < ptdata.nr_cooling_dev; j++) {
			int cdev_inst;
			y = j;
			x = tz_inst * TZONE_RECORD_SIZE + TZ_LEFT_ALIGN;

			draw_hbar(cooling_device_window, y+2, x,
				TZONE_RECORD_SIZE-1, ACS_VLINE, false);

			/* draw a column of spaces to separate thermal zones */
			mvwprintw(cooling_device_window, y+2, x-1, " ");
			if (ptdata.tzi[i].cdev_binding) {
				cdev_inst = ptdata.cdi[j].instance;
				unsigned long trip_binding =
					ptdata.tzi[i].trip_binding[cdev_inst];
				int k = 0; /* per zone trip point id that
					    * binded to this cdev, one to
					    * many possible based on the
					    * binding bitmask.
					    */
				syslog(LOG_DEBUG,
					"bind tz%d cdev%d tp%lx %d cdev%lx\n",
					i, j, trip_binding, y,
					ptdata.tzi[i].cdev_binding);
				/* draw each trip binding for the cdev */
				while (trip_binding >>= 1) {
					k++;
					if (!(trip_binding & 1))
						continue;
					/* draw '*' to show binding */
					mvwprintw(cooling_device_window,
						y + 2,
						x + ptdata.tzi[i].nr_trip_pts -
						k - 1, "*");
				}
			}
		}
	}
	/* draw border after data so that border will not be messed up
	 * even there is not enough space for all the data to be shown
	 */
	wborder(cooling_device_window, 0, 0, 0, 0, 0, 0, 0, 0);
	wattron(cooling_device_window, A_BOLD);
	mvwprintw(cooling_device_window, 0, maxx/2 - sizeof(cdev_title),
		cdev_title);
	wattroff(cooling_device_window, A_BOLD);

	wrefresh(cooling_device_window);
}

const char DIAG_TITLE[] = "[ TUNABLES ]";
void show_dialogue(void)
{
	int j, x = 0, y = 0;
	int rows, cols;
	WINDOW *w = dialogue_window;

	if (tui_disabled || !w)
		return;

	getmaxyx(w, rows, cols);

	/* Silence compiler 'unused' warnings */
	(void)cols;

	werase(w);
	box(w, 0, 0);
	mvwprintw(w, 0, maxx/4, DIAG_TITLE);
	/* list all the available tunables */
	for (j = 0; j <= ptdata.nr_cooling_dev; j++) {
		y = j % diag_dev_rows();
		if (y == 0 && j != 0)
			x += 20;
		if (j == ptdata.nr_cooling_dev)
			/* save last choice for target temp */
			mvwprintw(w, y+1, x+1, "%C-%.12s", 'A'+j, "Set Temp");
		else
			mvwprintw(w, y+1, x+1, "%C-%.10s-%2d", 'A'+j,
				ptdata.cdi[j].type, ptdata.cdi[j].instance);
	}
	wattron(w, A_BOLD);
	mvwprintw(w, diag_dev_rows()+1, 1, "Enter Choice [A-Z]?");
	wattroff(w, A_BOLD);
	/* print legend at the bottom line */
	mvwprintw(w, rows - 2, 1,
		"Legend: A=Active, P=Passive, C=Critical");

	wrefresh(dialogue_window);
}

void write_dialogue_win(char *buf, int y, int x)
{
	WINDOW *w = dialogue_window;

	mvwprintw(w, y, x, "%s", buf);
}

const char control_title[] = " CONTROLS ";
void show_control_w(void)
{
	unsigned long state;

	get_ctrl_state(&state);

	if (tui_disabled || !control_window)
		return;

	werase(control_window);
	mvwprintw(control_window, 1, 1,
		"PID gain: kp=%2.2f ki=%2.2f kd=%2.2f Output %2.2f",
		p_param.kp, p_param.ki, p_param.kd, p_param.y_k);

	mvwprintw(control_window, 2, 1,
		"Target Temp: %2.1fC, Zone: %d, Control Device: %.12s",
		p_param.t_target, target_thermal_zone, ctrl_cdev);

	/* draw border last such that everything is within boundary */
	wborder(control_window, 0, 0, 0, 0, 0, 0, 0, 0);
	wattron(control_window, A_BOLD);
	mvwprintw(control_window, 0, maxx/2 - sizeof(control_title),
		control_title);
	wattroff(control_window, A_BOLD);

	wrefresh(control_window);
}

void initialize_curses(void)
{
	if (tui_disabled)
		return;

	initscr();
	start_color();
	keypad(stdscr, TRUE);	/* enable keyboard mapping */
	nonl();			/* tell curses not to do NL->CR/NL on output */
	cbreak();		/* take input chars one at a time */
	noecho();		/* dont echo input */
	curs_set(0);		/* turn off cursor */
	use_default_colors();

	init_pair(PT_COLOR_DEFAULT, COLOR_WHITE, COLOR_BLACK);
	init_pair(PT_COLOR_HEADER_BAR, COLOR_BLACK, COLOR_WHITE);
	init_pair(PT_COLOR_ERROR, COLOR_BLACK, COLOR_RED);
	init_pair(PT_COLOR_RED, COLOR_WHITE, COLOR_RED);
	init_pair(PT_COLOR_YELLOW, COLOR_WHITE, COLOR_YELLOW);
	init_pair(PT_COLOR_GREEN, COLOR_WHITE, COLOR_GREEN);
	init_pair(PT_COLOR_BLUE, COLOR_WHITE, COLOR_BLUE);
	init_pair(PT_COLOR_BRIGHT, COLOR_WHITE, COLOR_BLACK);

}

void show_title_bar(void)
{
	int i;
	int x = 0;

	if (tui_disabled || !title_bar_window)
		return;

	wattrset(title_bar_window, COLOR_PAIR(PT_COLOR_HEADER_BAR));
	wbkgd(title_bar_window, COLOR_PAIR(PT_COLOR_HEADER_BAR));
	werase(title_bar_window);

	mvwprintw(title_bar_window, 0, 0,
		"     TMON v%s", VERSION);

	wrefresh(title_bar_window);

	werase(status_bar_window);

	for (i = 0; i < 10; i++) {
		if (strlen(status_bar_slots[i]) == 0)
			continue;
		wattron(status_bar_window, A_REVERSE);
		mvwprintw(status_bar_window, 0, x, "%s", status_bar_slots[i]);
		wattroff(status_bar_window, A_REVERSE);
		x += strlen(status_bar_slots[i]) + 1;
	}
	wrefresh(status_bar_window);
}

static void handle_input_val(int ch)
{
	char buf[32];
	int val;
	char path[256];
	WINDOW *w = dialogue_window;

	echo();
	keypad(w, TRUE);
	wgetnstr(w, buf, 31);
	val = atoi(buf);

	if (ch == ptdata.nr_cooling_dev) {
		snprintf(buf, 31, "Invalid Temp %d! %d-%d", val,
			MIN_CTRL_TEMP, MAX_CTRL_TEMP);
		if (val < MIN_CTRL_TEMP || val > MAX_CTRL_TEMP)
			write_status_bar(40, buf);
		else {
			p_param.t_target = val;
			snprintf(buf, 31, "Set New Target Temp %d", val);
			write_status_bar(40, buf);
		}
	} else {
		snprintf(path, 256, "%s/%s%d", THERMAL_SYSFS,
			CDEV, ptdata.cdi[ch].instance);
		sysfs_set_ulong(path, "cur_state", val);
	}
	noecho();
	dialogue_on = 0;
	show_data_w();
	show_control_w();

	top = (PANEL *)panel_userptr(top);
	top_panel(top);
}

static void handle_input_choice(int ch)
{
	char buf[48];
	int base = 0;
	int cdev_id = 0;

	if ((ch >= 'A' && ch <= 'A' + ptdata.nr_cooling_dev) ||
		(ch >= 'a' && ch <= 'a' + ptdata.nr_cooling_dev)) {
		base = (ch < 'a') ? 'A' : 'a';
		cdev_id = ch - base;
		if (ptdata.nr_cooling_dev == cdev_id)
			snprintf(buf, sizeof(buf), "New Target Temp:");
		else
			snprintf(buf, sizeof(buf), "New Value for %.10s-%2d: ",
				ptdata.cdi[cdev_id].type,
				ptdata.cdi[cdev_id].instance);
		write_dialogue_win(buf, diag_dev_rows() + 2, 2);
		handle_input_val(cdev_id);
	} else {
		snprintf(buf, sizeof(buf), "Invalid selection %d", ch);
		write_dialogue_win(buf, 8, 2);
	}
}

void *handle_tui_events(void *arg)
{
	int ch;

	keypad(cooling_device_window, TRUE);
	while ((ch = wgetch(cooling_device_window)) != EOF) {
		if (tmon_exit)
			break;
		/* when term size is too small, no dialogue panels are set.
		 * we need to filter out such cases.
		 */
		if (!data_panel || !dialogue_panel ||
			!cooling_device_window ||
			!dialogue_window) {

			continue;
		}
		pthread_mutex_lock(&input_lock);
		if (dialogue_on) {
			handle_input_choice(ch);
			/* top panel filter */
			if (ch == 'q' || ch == 'Q')
				ch = 0;
		}
		switch (ch) {
		case KEY_LEFT:
			box(cooling_device_window, 10, 0);
			break;
		case 9: /* TAB */
			top = (PANEL *)panel_userptr(top);
			top_panel(top);
			if (top == dialogue_panel) {
				dialogue_on = 1;
				show_dialogue();
			} else {
				dialogue_on = 0;
				/* force refresh */
				show_data_w();
				show_control_w();
			}
			break;
		case 'q':
		case 'Q':
			tmon_exit = 1;
			break;
		}
		update_panels();
		doupdate();
		pthread_mutex_unlock(&input_lock);
	}

	if (arg)
		*(int *)arg = 0; /* make gcc happy */

	return NULL;
}

/* draw a horizontal bar in given pattern */
static void draw_hbar(WINDOW *win, int y, int start, int len, unsigned long ptn,
		bool end)
{
	mvwaddch(win, y, start, ptn);
	whline(win, ptn, len);
	if (end)
		mvwaddch(win, y, MAX_DISP_TEMP+TDATA_LEFT, ']');
}

static char trip_type_to_char(int type)
{
	switch (type) {
	case THERMAL_TRIP_CRITICAL: return 'C';
	case THERMAL_TRIP_HOT: return 'H';
	case THERMAL_TRIP_PASSIVE: return 'P';
	case THERMAL_TRIP_ACTIVE: return 'A';
	default:
		return '?';
	}
}

/* fill a string with trip point type and value in one line
 * e.g.      P(56)    C(106)
 * maintain the distance one degree per char
 */
static void draw_tp_line(int tz, int y)
{
	int j;
	int x;

	for (j = 0; j < ptdata.tzi[tz].nr_trip_pts; j++) {
		x = ptdata.tzi[tz].tp[j].temp / 1000;
		mvwprintw(thermal_data_window, y + 0, x + TDATA_LEFT,
			"%c%d", trip_type_to_char(ptdata.tzi[tz].tp[j].type),
			x);
		syslog(LOG_INFO, "%s:tz %d tp %d temp = %lu\n", __func__,
			tz, j, ptdata.tzi[tz].tp[j].temp);
	}
}

const char data_win_title[] = " THERMAL DATA ";
void show_data_w(void)
{
	int i;


	if (tui_disabled || !thermal_data_window)
		return;

	werase(thermal_data_window);
	wattron(thermal_data_window, A_BOLD);
	mvwprintw(thermal_data_window, 0, maxx/2 - sizeof(data_win_title),
		data_win_title);
	wattroff(thermal_data_window, A_BOLD);
	/* draw a line as ruler */
	for (i = 10; i < MAX_DISP_TEMP; i += 10)
		mvwprintw(thermal_data_window, 1, i+TDATA_LEFT, "%2d", i);

	for (i = 0; i < ptdata.nr_tz_sensor; i++) {
		int temp = trec[cur_thermal_record].temp[i] / 1000;
		int y = 0;

		y = i * NR_LINES_TZDATA + 2;
		/* y at tz temp data line */
		mvwprintw(thermal_data_window, y, 1, "%6.6s%2d:[%3d][",
			ptdata.tzi[i].type,
			ptdata.tzi[i].instance, temp);
		draw_hbar(thermal_data_window, y, TDATA_LEFT, temp, ACS_RARROW,
			true);
		draw_tp_line(i, y);
	}
	wborder(thermal_data_window, 0, 0, 0, 0, 0, 0, 0, 0);
	wrefresh(thermal_data_window);
}

const char tz_title[] = "THERMAL ZONES(SENSORS)";

void show_sensors_w(void)
{
	int i, j;
	char buffer[512];

	if (tui_disabled || !tz_sensor_window)
		return;

	werase(tz_sensor_window);

	memset(buffer, 0, sizeof(buffer));
	wattron(tz_sensor_window, A_BOLD);
	mvwprintw(tz_sensor_window, 1, 1, "Thermal Zones:");
	wattroff(tz_sensor_window, A_BOLD);

	mvwprintw(tz_sensor_window, 1, TZ_LEFT_ALIGN, "%s", buffer);
	/* fill trip points for each tzone */
	wattron(tz_sensor_window, A_BOLD);
	mvwprintw(tz_sensor_window, 2, 1, "Trip Points:");
	wattroff(tz_sensor_window, A_BOLD);

	/* draw trip point from low to high for each tz */
	for (i = 0; i < ptdata.nr_tz_sensor; i++) {
		int inst = ptdata.tzi[i].instance;

		mvwprintw(tz_sensor_window, 1,
			TZ_LEFT_ALIGN+TZONE_RECORD_SIZE * inst, "%.9s%02d",
			ptdata.tzi[i].type, ptdata.tzi[i].instance);
		for (j = ptdata.tzi[i].nr_trip_pts - 1; j >= 0; j--) {
			/* loop through all trip points */
			char type;
			int tp_pos;
			/* reverse the order here since trips are sorted
			 * in ascending order in terms of temperature.
			 */
			tp_pos = ptdata.tzi[i].nr_trip_pts - j - 1;

			type = trip_type_to_char(ptdata.tzi[i].tp[j].type);
			mvwaddch(tz_sensor_window, 2,
				inst * TZONE_RECORD_SIZE + TZ_LEFT_ALIGN +
				tp_pos,	type);
			syslog(LOG_DEBUG, "draw tz %d tp %d ch:%c\n",
				inst, j, type);
		}
	}
	wborder(tz_sensor_window, 0, 0, 0, 0, 0, 0, 0, 0);
	wattron(tz_sensor_window, A_BOLD);
	mvwprintw(tz_sensor_window, 0, maxx/2 - sizeof(tz_title), tz_title);
	wattroff(tz_sensor_window, A_BOLD);
	wrefresh(tz_sensor_window);
}

void disable_tui(void)
{
	tui_disabled = 1;
}
