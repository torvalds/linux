#!/usr/local/bin/python

# Copyright (c) 2002-2003, 2009, Jeffrey Roberson <jeff@freebsd.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice unmodified, this list of conditions, and the following
#    disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$

from __future__ import print_function
import sys
import re
import random
from Tkinter import *

# To use:
# - Install the ports/x11-toolkits/py-tkinter package; e.g.
#	portinstall x11-toolkits/py-tkinter package
# - Add KTR_SCHED to KTR_COMPILE and KTR_MASK in your KERNCONF; e.g.
#	options 	KTR
#	options 	KTR_ENTRIES=32768
#	options 	KTR_COMPILE=(KTR_SCHED)
#	options 	KTR_MASK=(KTR_SCHED)
# - It is encouraged to increase KTR_ENTRIES size to gather enough
#    information for analysis; e.g.
#	options 	KTR_ENTRIES=262144
#   as 32768 entries may only correspond to a second or two of profiling
#   data depending on your workload.
# - Rebuild kernel with proper changes to KERNCONF and boot new kernel.
# - Run your workload to be profiled.
# - While the workload is continuing (i.e. before it finishes), disable
#   KTR tracing by setting 'sysctl debug.ktr.mask=0'.  This is necessary
#   to avoid a race condition while running ktrdump, i.e. the KTR ring buffer
#   will cycle a bit while ktrdump runs, and this confuses schedgraph because
#   the timestamps appear to go backwards at some point.  Stopping KTR logging
#   while the workload is still running is to avoid wasting log entries on
#   "idle" time at the end.
# - Dump the trace to a file: 'ktrdump -ct > ktr.out'
# - Run the python script: 'python schedgraph.py ktr.out' optionally provide
#   your cpu frequency in ghz: 'python schedgraph.py ktr.out 2.4'
#
# To do:
# Add a per-source summary display
# "Vertical rule" to help relate data in different rows
# Mouse-over popup of full thread/event/row label (currently truncated)
# More visible anchors for popup event windows
#
# BUGS: 1) Only 8 CPUs are supported, more CPUs require more choices of
#          colours to represent them ;-)

eventcolors = [
	("count",	"red"),
	("running",	"green"),
	("idle",	"grey"),
	("spinning",	"red"),
	("yielding",	"yellow"),
	("swapped",	"violet"),
	("suspended",	"purple"),
	("iwait",	"grey"),
	("sleep",	"blue"),
	("blocked",	"dark red"),
	("runq add",	"yellow"),
	("runq rem",	"yellow"),
	("thread exit",	"grey"),
	("proc exit",	"grey"),
	("lock acquire", "blue"),
	("lock contest", "purple"),
	("failed lock try", "red"),
	("lock release", "grey"),
	("statclock",	"black"),
	("prio",	"black"),
	("lend prio",	"black"),
	("wokeup",	"black")
]

cpucolors = [
	("CPU 0",	"light grey"),
	("CPU 1",	"dark grey"),
	("CPU 2",	"light blue"),
	("CPU 3",	"light pink"),
	("CPU 4",	"blanched almond"),
	("CPU 5",	"slate grey"),
	("CPU 6",	"tan"),
	("CPU 7",	"thistle"),
	("CPU 8",	"white")
]

colors = [
	"white", "thistle", "blanched almond", "tan", "chartreuse",
	"dark red", "red", "pale violet red", "pink", "light pink",
	"dark orange", "orange", "coral", "light coral",
	"goldenrod", "gold", "yellow", "light yellow",
	"dark green", "green", "light green", "light sea green",
	"dark blue", "blue", "light blue", "steel blue", "light slate blue",
	"dark violet", "violet", "purple", "blue violet",
	"dark grey", "slate grey", "light grey",
	"black",
]
colors.sort()

ticksps = None
status = None
colormap = None
ktrfile = None
clockfreq = None
sources = []
lineno = -1

Y_BORDER = 10
X_BORDER = 10
Y_COUNTER = 80
Y_EVENTSOURCE = 10
XY_POINT = 4

class Colormap:
	def __init__(self, table):
		self.table = table
		self.map = {}
		for entry in table:
			self.map[entry[0]] = entry[1]

	def lookup(self, name):
		try:
			color = self.map[name]
		except:
			color = colors[random.randrange(0, len(colors))]
			print("Picking random color", color, "for", name)
			self.map[name] = color
			self.table.append((name, color))
		return (color)

def ticks2sec(ticks):
	ticks = float(ticks)
	ns = float(ticksps) / 1000000000
	ticks /= ns
	if (ticks < 1000):
		return ("%.2fns" % ticks)
	ticks /= 1000
	if (ticks < 1000):
		return ("%.2fus" % ticks)
	ticks /= 1000
	if (ticks < 1000):
		return ("%.2fms" % ticks)
	ticks /= 1000
	return ("%.2fs" % ticks)

class Scaler(Frame):
	def __init__(self, master, target):
		Frame.__init__(self, master)
		self.scale = None
		self.target = target
		self.label = Label(self, text="Ticks per pixel")
		self.label.pack(side=LEFT)
		self.resolution = 100
		self.setmax(10000)

	def scaleset(self, value):
		self.target.scaleset(int(value))

	def set(self, value):
		self.scale.set(value)

	def setmax(self, value):
		#
		# We can't reconfigure the to_ value so we delete the old
		# window and make a new one when we resize.
		#
		if (self.scale != None):
			self.scale.pack_forget()
			self.scale.destroy()
		self.scale = Scale(self, command=self.scaleset,
		    from_=100, to_=value, orient=HORIZONTAL,
		    resolution=self.resolution)
		self.scale.pack(fill="both", expand=1)
		self.scale.set(self.target.scaleget())

class Status(Frame):
	def __init__(self, master):
		Frame.__init__(self, master)
		self.label = Label(self, bd=1, relief=SUNKEN, anchor=W)
		self.label.pack(fill="both", expand=1)
		self.clear()

	def set(self, str):
		self.label.config(text=str)

	def clear(self):
		self.label.config(text="")

	def startup(self, str):
		self.set(str)
		root.update()

class ColorConf(Frame):
	def __init__(self, master, name, color):
		Frame.__init__(self, master)
		if (graph.getstate(name) == "hidden"):
			enabled = 0
		else:
			enabled = 1
		self.name = name
		self.color = StringVar()
		self.color_default = color
		self.color_current = color
		self.color.set(color)
		self.enabled = IntVar()
		self.enabled_default = enabled
		self.enabled_current = enabled
		self.enabled.set(enabled)
		self.draw()

	def draw(self):
		self.label = Label(self, text=self.name, anchor=W)
		self.sample = Canvas(self, width=24, height=24,
		    bg='grey')
		self.rect = self.sample.create_rectangle(0, 0, 24, 24,
		    fill=self.color.get())
		self.list = OptionMenu(self, self.color, command=self.setcolor,
		    *colors)
		self.checkbox = Checkbutton(self, text="enabled",
		    variable=self.enabled)
		self.label.grid(row=0, column=0, sticky=E+W)
		self.sample.grid(row=0, column=1)
		self.list.grid(row=0, column=2, sticky=E+W)
		self.checkbox.grid(row=0, column=3)
		self.columnconfigure(0, weight=1)
		self.columnconfigure(2, minsize=150)

	def setcolor(self, color):
		self.color.set(color)
		self.sample.itemconfigure(self.rect, fill=color)

	def apply(self):
		cchange = 0
		echange = 0
		if (self.color_current != self.color.get()):
			cchange = 1
		if (self.enabled_current != self.enabled.get()):
			echange = 1
		self.color_current = self.color.get()
		self.enabled_current = self.enabled.get()
		if (echange != 0):
			if (self.enabled_current):
				graph.setcolor(self.name, self.color_current)
			else:
				graph.hide(self.name)
			return
		if (cchange != 0):
			graph.setcolor(self.name, self.color_current)

	def revert(self):
		self.setcolor(self.color_default)
		self.enabled.set(self.enabled_default)

class ColorConfigure(Toplevel):
	def __init__(self, table, name):
		Toplevel.__init__(self)
		self.resizable(0, 0)
		self.title(name)
		self.items = LabelFrame(self, text="Item Type")
		self.buttons = Frame(self)
		self.drawbuttons()
		self.items.grid(row=0, column=0, sticky=E+W)
		self.columnconfigure(0, weight=1)
		self.buttons.grid(row=1, column=0, sticky=E+W)
		self.types = []
		self.irow = 0
		for type in table:
			color = graph.getcolor(type[0])
			if (color != ""):
				self.additem(type[0], color)
		self.bind("<Control-w>", self.destroycb)

	def destroycb(self, event):
		self.destroy()

	def additem(self, name, color):
		item = ColorConf(self.items, name, color)
		self.types.append(item)
		item.grid(row=self.irow, column=0, sticky=E+W)
		self.irow += 1

	def drawbuttons(self):
		self.apply = Button(self.buttons, text="Apply",
		    command=self.apress)
		self.default = Button(self.buttons, text="Revert",
		    command=self.rpress)
		self.apply.grid(row=0, column=0, sticky=E+W)
		self.default.grid(row=0, column=1, sticky=E+W)
		self.buttons.columnconfigure(0, weight=1)
		self.buttons.columnconfigure(1, weight=1)

	def apress(self):
		for item in self.types:
			item.apply()

	def rpress(self):
		for item in self.types:
			item.revert()

class SourceConf(Frame):
	def __init__(self, master, source):
		Frame.__init__(self, master)
		if (source.hidden == 1):
			enabled = 0
		else:
			enabled = 1
		self.source = source
		self.name = source.name
		self.enabled = IntVar()
		self.enabled_default = enabled
		self.enabled_current = enabled
		self.enabled.set(enabled)
		self.draw()

	def draw(self):
		self.label = Label(self, text=self.name, anchor=W)
		self.checkbox = Checkbutton(self, text="enabled",
		    variable=self.enabled)
		self.label.grid(row=0, column=0, sticky=E+W)
		self.checkbox.grid(row=0, column=1)
		self.columnconfigure(0, weight=1)

	def changed(self):
		if (self.enabled_current != self.enabled.get()):
			return 1
		return 0

	def apply(self):
		self.enabled_current = self.enabled.get()

	def revert(self):
		self.enabled.set(self.enabled_default)

	def check(self):
		self.enabled.set(1)

	def uncheck(self):
		self.enabled.set(0)

class SourceConfigure(Toplevel):
	def __init__(self):
		Toplevel.__init__(self)
		self.resizable(0, 0)
		self.title("Source Configuration")
		self.items = []
		self.iframe = Frame(self)
		self.iframe.grid(row=0, column=0, sticky=E+W)
		f = LabelFrame(self.iframe, bd=4, text="Sources")
		self.items.append(f)
		self.buttons = Frame(self)
		self.items[0].grid(row=0, column=0, sticky=E+W)
		self.columnconfigure(0, weight=1)
		self.sconfig = []
		self.irow = 0
		self.icol = 0
		for source in sources:
			self.addsource(source)
		self.drawbuttons()
		self.buttons.grid(row=1, column=0, sticky=W)
		self.bind("<Control-w>", self.destroycb)

	def destroycb(self, event):
		self.destroy()

	def addsource(self, source):
		if (self.irow > 30):
			self.icol += 1
			self.irow = 0
			c = self.icol
			f = LabelFrame(self.iframe, bd=4, text="Sources")
			f.grid(row=0, column=c, sticky=N+E+W)
			self.items.append(f)
		item = SourceConf(self.items[self.icol], source)
		self.sconfig.append(item)
		item.grid(row=self.irow, column=0, sticky=E+W)
		self.irow += 1

	def drawbuttons(self):
		self.apply = Button(self.buttons, text="Apply",
		    command=self.apress)
		self.default = Button(self.buttons, text="Revert",
		    command=self.rpress)
		self.checkall = Button(self.buttons, text="Check All",
		    command=self.cpress)
		self.uncheckall = Button(self.buttons, text="Uncheck All",
		    command=self.upress)
		self.checkall.grid(row=0, column=0, sticky=W)
		self.uncheckall.grid(row=0, column=1, sticky=W)
		self.apply.grid(row=0, column=2, sticky=W)
		self.default.grid(row=0, column=3, sticky=W)
		self.buttons.columnconfigure(0, weight=1)
		self.buttons.columnconfigure(1, weight=1)
		self.buttons.columnconfigure(2, weight=1)
		self.buttons.columnconfigure(3, weight=1)

	def apress(self):
		disable_sources = []
		enable_sources = []
		for item in self.sconfig:
			if (item.changed() == 0):
				continue
			if (item.enabled.get() == 1):
				enable_sources.append(item.source)
			else:
				disable_sources.append(item.source)

		if (len(disable_sources)):
			graph.sourcehidelist(disable_sources)
		if (len(enable_sources)):
			graph.sourceshowlist(enable_sources)

		for item in self.sconfig:
			item.apply()

	def rpress(self):
		for item in self.sconfig:
			item.revert()

	def cpress(self):
		for item in self.sconfig:
			item.check()

	def upress(self):
		for item in self.sconfig:
			item.uncheck()

# Reverse compare of second member of the tuple
def cmp_counts(x, y):
	return y[1] - x[1]

class SourceStats(Toplevel):
	def __init__(self, source):
		self.source = source
		Toplevel.__init__(self)
		self.resizable(0, 0)
		self.title(source.name + " statistics")
		self.evframe = LabelFrame(self,
		    text="Event Count, Duration, Avg Duration")
		self.evframe.grid(row=0, column=0, sticky=E+W)
		eventtypes={}
		for event in self.source.events:
			if (event.type == "pad"):
				continue
			duration = event.duration
			if (event.name in eventtypes):
				(c, d) = eventtypes[event.name]
				c += 1
				d += duration
				eventtypes[event.name] = (c, d)
			else:
				eventtypes[event.name] = (1, duration)
		events = []
		for k, v in eventtypes.iteritems():
			(c, d) = v
			events.append((k, c, d))
		events.sort(cmp=cmp_counts)

		ypos = 0
		for event in events:
			(name, c, d) = event
			Label(self.evframe, text=name, bd=1, 
			    relief=SUNKEN, anchor=W, width=30).grid(
			    row=ypos, column=0, sticky=W+E)
			Label(self.evframe, text=str(c), bd=1,
			    relief=SUNKEN, anchor=W, width=10).grid(
			    row=ypos, column=1, sticky=W+E)
			Label(self.evframe, text=ticks2sec(d),
			    bd=1, relief=SUNKEN, width=10).grid(
			    row=ypos, column=2, sticky=W+E)
			if (d and c):
				d /= c
			else:
				d = 0
			Label(self.evframe, text=ticks2sec(d),
			    bd=1, relief=SUNKEN, width=10).grid(
			    row=ypos, column=3, sticky=W+E)
			ypos += 1
		self.bind("<Control-w>", self.destroycb)

	def destroycb(self, event):
		self.destroy()


class SourceContext(Menu):
	def __init__(self, event, source):
		self.source = source
		Menu.__init__(self, tearoff=0, takefocus=0)
		self.add_command(label="hide", command=self.hide)
		self.add_command(label="hide group", command=self.hidegroup)
		self.add_command(label="stats", command=self.stats)
		self.tk_popup(event.x_root-3, event.y_root+3)

	def hide(self):
		graph.sourcehide(self.source)

	def hidegroup(self):
		grouplist = []
		for source in sources:
			if (source.group == self.source.group):
				grouplist.append(source)
		graph.sourcehidelist(grouplist)

	def show(self):
		graph.sourceshow(self.source)

	def stats(self):
		SourceStats(self.source)

class EventView(Toplevel):
	def __init__(self, event, canvas):
		Toplevel.__init__(self)
		self.resizable(0, 0)
		self.title("Event")
		self.event = event
		self.buttons = Frame(self)
		self.buttons.grid(row=0, column=0, sticky=E+W)
		self.frame = Frame(self)
		self.frame.grid(row=1, column=0, sticky=N+S+E+W)
		self.canvas = canvas
		self.drawlabels()
		self.drawbuttons()
		event.displayref(canvas)
		self.bind("<Destroy>", self.destroycb)
		self.bind("<Control-w>", self.destroycb)

	def destroycb(self, event):
		self.unbind("<Destroy>")
		if (self.event != None):
			self.event.displayunref(self.canvas)
			self.event = None
		self.destroy()

	def clearlabels(self):
		for label in self.frame.grid_slaves():
			label.grid_remove()

	def drawlabels(self):
		ypos = 0
		labels = self.event.labels()
		while (len(labels) < 7):
			labels.append(("", ""))
		for label in labels:
			name, value = label
			linked = 0
			if (name == "linkedto"):
				linked = 1
			l = Label(self.frame, text=name, bd=1, width=15,
			    relief=SUNKEN, anchor=W)
			if (linked):
				fgcolor = "blue"
			else:
				fgcolor = "black"
			r = Label(self.frame, text=value, bd=1,
			    relief=SUNKEN, anchor=W, fg=fgcolor)
			l.grid(row=ypos, column=0, sticky=E+W)
			r.grid(row=ypos, column=1, sticky=E+W)
			if (linked):
				r.bind("<Button-1>", self.linkpress)
			ypos += 1
		self.frame.columnconfigure(1, minsize=80)

	def drawbuttons(self):
		self.back = Button(self.buttons, text="<", command=self.bpress)
		self.forw = Button(self.buttons, text=">", command=self.fpress)
		self.new = Button(self.buttons, text="new", command=self.npress)
		self.back.grid(row=0, column=0, sticky=E+W)
		self.forw.grid(row=0, column=1, sticky=E+W)
		self.new.grid(row=0, column=2, sticky=E+W)
		self.buttons.columnconfigure(2, weight=1)

	def newevent(self, event):
		self.event.displayunref(self.canvas)
		self.clearlabels()
		self.event = event
		self.event.displayref(self.canvas)
		self.drawlabels()

	def npress(self):
		EventView(self.event, self.canvas)

	def bpress(self):
		prev = self.event.prev()
		if (prev == None):
			return
		while (prev.type == "pad"):
			prev = prev.prev()
			if (prev == None):
				return
		self.newevent(prev)

	def fpress(self):
		next = self.event.next()
		if (next == None):
			return
		while (next.type == "pad"):
			next = next.next()
			if (next == None):
				return
		self.newevent(next)

	def linkpress(self, wevent):
		event = self.event.getlinked()
		if (event != None):
			self.newevent(event)

class Event:
	def __init__(self, source, name, cpu, timestamp, attrs):
		self.source = source
		self.name = name
		self.cpu = cpu
		self.timestamp = int(timestamp)
		self.attrs = attrs
		self.idx = None
		self.item = None
		self.dispcnt = 0
		self.duration = 0
		self.recno = lineno

	def status(self):
		statstr = self.name + " " + self.source.name
		statstr += " on: cpu" + str(self.cpu)
		statstr += " at: " + str(self.timestamp)
		statstr += " attributes: "
		for i in range(0, len(self.attrs)):
			attr = self.attrs[i]
			statstr += attr[0] + ": " + str(attr[1])
			if (i != len(self.attrs) - 1):
				statstr += ", "
		status.set(statstr)

	def labels(self):
		return [("Source", self.source.name),
			("Event", self.name),
			("CPU", self.cpu),
			("Timestamp", self.timestamp),
			("KTR Line ", self.recno)
		] + self.attrs

	def mouseenter(self, canvas):
		self.displayref(canvas)
		self.status()

	def mouseexit(self, canvas):
		self.displayunref(canvas)
		status.clear()

	def mousepress(self, canvas):
		EventView(self, canvas)

	def draw(self, canvas, xpos, ypos, item):
		self.item = item
		if (item != None):
			canvas.items[item] = self

	def move(self, canvas, x, y):
		if (self.item == None):
			return;
		canvas.move(self.item, x, y);

	def next(self):
		return self.source.eventat(self.idx + 1)

	def nexttype(self, type):
		next = self.next()
		while (next != None and next.type != type):
			next = next.next()
		return (next)

	def prev(self):
		return self.source.eventat(self.idx - 1)

	def displayref(self, canvas):
		if (self.dispcnt == 0):
			canvas.itemconfigure(self.item, width=2)
		self.dispcnt += 1

	def displayunref(self, canvas):
		self.dispcnt -= 1
		if (self.dispcnt == 0):
			canvas.itemconfigure(self.item, width=0)
			canvas.tag_raise("point", "state")

	def getlinked(self):
		for attr in self.attrs:
			if (attr[0] != "linkedto"):
				continue
			source = ktrfile.findid(attr[1])
			return source.findevent(self.timestamp)
		return None

class PointEvent(Event):
	type = "point"
	def __init__(self, source, name, cpu, timestamp, attrs):
		Event.__init__(self, source, name, cpu, timestamp, attrs)

	def draw(self, canvas, xpos, ypos):
		color = colormap.lookup(self.name)
		l = canvas.create_oval(xpos - XY_POINT, ypos,
		    xpos + XY_POINT, ypos - (XY_POINT * 2),
		    fill=color, width=0,
		    tags=("event", self.type, self.name, self.source.tag))
		Event.draw(self, canvas, xpos, ypos, l)

		return xpos

class StateEvent(Event):
	type = "state"
	def __init__(self, source, name, cpu, timestamp, attrs):
		Event.__init__(self, source, name, cpu, timestamp, attrs)

	def draw(self, canvas, xpos, ypos):
		next = self.nexttype("state")
		if (next == None):
			return (xpos)
		self.duration = duration = next.timestamp - self.timestamp
		self.attrs.insert(0, ("duration", ticks2sec(duration)))
		color = colormap.lookup(self.name)
		if (duration < 0):
			duration = 0
			print("Unsynchronized timestamp")
			print(self.cpu, self.timestamp)
			print(next.cpu, next.timestamp)
		delta = duration / canvas.ratio
		l = canvas.create_rectangle(xpos, ypos,
		    xpos + delta, ypos - 10, fill=color, width=0,
		    tags=("event", self.type, self.name, self.source.tag))
		Event.draw(self, canvas, xpos, ypos, l)

		return (xpos + delta)

class CountEvent(Event):
	type = "count"
	def __init__(self, source, count, cpu, timestamp, attrs):
		count = int(count)
		self.count = count
		Event.__init__(self, source, "count", cpu, timestamp, attrs)

	def draw(self, canvas, xpos, ypos):
		next = self.nexttype("count")
		if (next == None):
			return (xpos)
		color = colormap.lookup("count")
		self.duration = duration = next.timestamp - self.timestamp
		if (duration < 0):
			duration = 0
			print("Unsynchronized timestamp")
			print(self.cpu, self.timestamp)
			print(next.cpu, next.timestamp)
		self.attrs.insert(0, ("count", self.count))
		self.attrs.insert(1, ("duration", ticks2sec(duration)))
		delta = duration / canvas.ratio
		yhight = self.source.yscale() * self.count
		l = canvas.create_rectangle(xpos, ypos - yhight,
		    xpos + delta, ypos, fill=color, width=0,
		    tags=("event", self.type, self.name, self.source.tag))
		Event.draw(self, canvas, xpos, ypos, l)
		return (xpos + delta)

class PadEvent(StateEvent):
	type = "pad"
	def __init__(self, source, cpu, timestamp, last=0):
		if (last):
			cpu = source.events[len(source.events) -1].cpu
		else:
			cpu = source.events[0].cpu
		StateEvent.__init__(self, source, "pad", cpu, timestamp, [])
	def draw(self, canvas, xpos, ypos):
		next = self.next()
		if (next == None):
			return (xpos)
		duration = next.timestamp - self.timestamp
		delta = duration / canvas.ratio
		Event.draw(self, canvas, xpos, ypos, None)
		return (xpos + delta)

# Sort function for start y address
def source_cmp_start(x, y):
	return x.y - y.y

class EventSource:
	def __init__(self, group, id):
		self.name = id
		self.events = []
		self.cpuitems = []
		self.group = group
		self.y = 0
		self.item = None
		self.hidden = 0
		self.tag = group + id

	def __cmp__(self, other):
		if (other == None):
			return -1
		if (self.group == other.group):
			return cmp(self.name, other.name)
		return cmp(self.group, other.group)

	# It is much faster to append items to a list then to insert them
	# at the beginning.  As a result, we add events in reverse order
	# and then swap the list during fixup.
	def fixup(self):
		self.events.reverse()

	def addevent(self, event):
		self.events.append(event)

	def addlastevent(self, event):
		self.events.insert(0, event)

	def draw(self, canvas, ypos):
		xpos = 10
		cpux = 10
		cpu = self.events[1].cpu
		for i in range(0, len(self.events)):
			self.events[i].idx = i
		for event in self.events:
			if (event.cpu != cpu and event.cpu != -1):
				self.drawcpu(canvas, cpu, cpux, xpos, ypos)
				cpux = xpos
				cpu = event.cpu
			xpos = event.draw(canvas, xpos, ypos)
		self.drawcpu(canvas, cpu, cpux, xpos, ypos)

	def drawname(self, canvas, ypos):
		self.y = ypos
		ypos = ypos - (self.ysize() / 2)
		self.item = canvas.create_text(X_BORDER, ypos, anchor="w",
		    text=self.name)
		return (self.item)

	def drawcpu(self, canvas, cpu, fromx, tox, ypos):
		cpu = "CPU " + str(cpu)
		color = cpucolormap.lookup(cpu)
		# Create the cpu background colors default to hidden
		l = canvas.create_rectangle(fromx,
		    ypos - self.ysize() - canvas.bdheight,
		    tox, ypos + canvas.bdheight, fill=color, width=0,
		    tags=("cpubg", cpu, self.tag), state="hidden")
		self.cpuitems.append(l)

	def move(self, canvas, xpos, ypos):
		canvas.move(self.tag, xpos, ypos)

	def movename(self, canvas, xpos, ypos):
		self.y += ypos
		canvas.move(self.item, xpos, ypos)

	def ysize(self):
		return (Y_EVENTSOURCE)

	def eventat(self, i):
		if (i >= len(self.events) or i < 0):
			return (None)
		event = self.events[i]
		return (event)

	def findevent(self, timestamp):
		for event in self.events:
			if (event.timestamp >= timestamp and event.type != "pad"):
				return (event)
		return (None)

class Counter(EventSource):
	#
	# Store a hash of counter groups that keeps the max value
	# for a counter in this group for scaling purposes.
	#
	groups = {}
	def __init__(self, group, id):
		try:
			Counter.cnt = Counter.groups[group]
		except:
			Counter.groups[group] = 0
		EventSource.__init__(self, group, id)

	def fixup(self):
		for event in self.events:
			if (event.type != "count"):
				continue;
			count = int(event.count)
			if (count > Counter.groups[self.group]):
				Counter.groups[self.group] = count
		EventSource.fixup(self)

	def ymax(self):
		return (Counter.groups[self.group])

	def ysize(self):
		return (Y_COUNTER)

	def yscale(self):
		return (self.ysize() / self.ymax())

class KTRFile:
	def __init__(self, file):
		self.timestamp_f = None
		self.timestamp_l = None
		self.locks = {}
		self.ticks = {}
		self.load = {}
		self.crit = {}
		self.stathz = 0
		self.eventcnt = 0
		self.taghash = {}

		self.parse(file)
		self.fixup()
		global ticksps
		ticksps = self.ticksps()
		span = self.timespan()
		ghz = float(ticksps) / 1000000000.0
		#
		# Update the title with some stats from the file
		#
		titlestr = "SchedGraph: "
		titlestr += ticks2sec(span) + " at %.3f ghz, " % ghz
		titlestr += str(len(sources)) + " event sources, "
		titlestr += str(self.eventcnt) + " events"
		root.title(titlestr)

	def parse(self, file):
		try:
			ifp = open(file)
		except:
			print("Can't open", file)
			sys.exit(1)

		# quoteexp matches a quoted string, no escaping
		quoteexp = "\"([^\"]*)\""

		#
		# commaexp matches a quoted string OR the string up
		# to the first ','
		#
		commaexp = "(?:" + quoteexp + "|([^,]+))"

		#
		# colonstr matches a quoted string OR the string up
		# to the first ':'
		#
		colonexp = "(?:" + quoteexp + "|([^:]+))"

		#
		# Match various manditory parts of the KTR string this is
		# fairly inflexible until you get to attributes to make
		# parsing faster.
		#
		hdrexp = "\s*(\d+)\s+(\d+)\s+(\d+)\s+"
		groupexp = "KTRGRAPH group:" + quoteexp + ", "
		idexp = "id:" + quoteexp + ", "
		typeexp = "([^:]+):" + commaexp + ", "
		attribexp = "attributes: (.*)"

		#
		# Matches optional attributes in the KTR string.  This
		# tolerates more variance as the users supply these values.
		#
		attrexp = colonexp + "\s*:\s*(?:" + commaexp + ", (.*)|"
		attrexp += quoteexp +"|(.*))"

		# Precompile regexp
		ktrre = re.compile(hdrexp + groupexp + idexp + typeexp + attribexp)
		attrre = re.compile(attrexp)

		global lineno
		lineno = 0
		for line in ifp.readlines():
			lineno += 1
			if ((lineno % 2048) == 0):
				status.startup("Parsing line " + str(lineno))
			m = ktrre.match(line);
			if (m == None):
				print("Can't parse", lineno, line, end=' ')
				continue;
			(index, cpu, timestamp, group, id, type, dat, dat1, attrstring) = m.groups();
			if (dat == None):
				dat = dat1
			if (self.checkstamp(timestamp) == 0):
				print("Bad timestamp at", lineno, ":", end=' ')
				print(cpu, timestamp) 
				continue
			#
			# Build the table of optional attributes
			#
			attrs = []
			while (attrstring != None):
				m = attrre.match(attrstring.strip())
				if (m == None):
					break;
				#
				# Name may or may not be quoted.
				#
				# For val we have four cases:
				# 1) quotes followed by comma and more
				#    attributes.
				# 2) no quotes followed by comma and more
				#    attributes.
				# 3) no more attributes or comma with quotes.
				# 4) no more attributes or comma without quotes.
				#
				(name, name1, val, val1, attrstring, end, end1) = m.groups();
				if (name == None):
					name = name1
				if (end == None):
					end = end1
				if (val == None):
					val = val1
				if (val == None):
					val = end
				if (name == "stathz"):
					self.setstathz(val, cpu)
				attrs.append((name, val))
			args = (dat, cpu, timestamp, attrs)
			e = self.makeevent(group, id, type, args)
			if (e == None):
				print("Unknown type", type, lineno, line, end=' ')

	def makeevent(self, group, id, type, args):
		e = None
		source = self.makeid(group, id, type)
		if (type == "state"):
			e = StateEvent(source, *args)
		elif (type == "counter"):
			e = CountEvent(source, *args)
		elif (type == "point"):
			e = PointEvent(source, *args)
		if (e != None):
			self.eventcnt += 1
			source.addevent(e);
		return e

	def setstathz(self, val, cpu):
		self.stathz = int(val)
		cpu = int(cpu)
		try:
			ticks = self.ticks[cpu]
		except:
			self.ticks[cpu] = 0
		self.ticks[cpu] += 1

	def checkstamp(self, timestamp):
		timestamp = int(timestamp)
		if (self.timestamp_f == None):
			self.timestamp_f = timestamp;
		if (self.timestamp_l != None and
		    timestamp -2048> self.timestamp_l):
			return (0)
		self.timestamp_l = timestamp;
		return (1)

	def makeid(self, group, id, type):
		tag = group + id
		if (tag in self.taghash):
			return self.taghash[tag]
		if (type == "counter"):
			source = Counter(group, id)
		else:
			source = EventSource(group, id)
		sources.append(source)
		self.taghash[tag] = source
		return (source)

	def findid(self, id):
		for source in sources:
			if (source.name == id):
				return source
		return (None)

	def timespan(self):
		return (self.timestamp_f - self.timestamp_l);

	def ticksps(self):
		oneghz = 1000000000
		# Use user supplied clock first
		if (clockfreq != None):
			return int(clockfreq * oneghz)

		# Check for a discovered clock
		if (self.stathz != 0):
			return (self.timespan() / self.ticks[0]) * int(self.stathz)
		# Pretend we have a 1ns clock
		print("WARNING: No clock discovered and no frequency ", end=' ')
		print("specified via the command line.")
		print("Using fake 1ghz clock")
		return (oneghz);

	def fixup(self):
		for source in sources:
			e = PadEvent(source, -1, self.timestamp_l)
			source.addevent(e)
			e = PadEvent(source, -1, self.timestamp_f, last=1)
			source.addlastevent(e)
			source.fixup()
		sources.sort()

class SchedNames(Canvas):
	def __init__(self, master, display):
		self.display = display
		self.parent = master
		self.bdheight = master.bdheight
		self.items = {}
		self.ysize = 0
		self.lines = []
		Canvas.__init__(self, master, width=120,
		    height=display["height"], bg='grey',
		    scrollregion=(0, 0, 50, 100))

	def moveline(self, cur_y, y):
		for line in self.lines:
			(x0, y0, x1, y1) = self.coords(line)
			if (cur_y != y0):
				continue
			self.move(line, 0, y)
			return

	def draw(self):
		status.startup("Drawing names")
		ypos = 0
		self.configure(scrollregion=(0, 0,
		    self["width"], self.display.ysize()))
		for source in sources:
			l = self.create_line(0, ypos, self["width"], ypos,
			    width=1, fill="black", tags=("all","sources"))
			self.lines.append(l)
			ypos += self.bdheight
			ypos += source.ysize()
			t = source.drawname(self, ypos)
			self.items[t] = source
			ypos += self.bdheight
		self.ysize = ypos
		self.create_line(0, ypos, self["width"], ypos,
		    width=1, fill="black", tags=("all",))
		self.bind("<Button-1>", self.master.mousepress);
		self.bind("<Button-3>", self.master.mousepressright);
		self.bind("<ButtonRelease-1>", self.master.mouserelease);
		self.bind("<B1-Motion>", self.master.mousemotion);

	def updatescroll(self):
		self.configure(scrollregion=(0, 0,
		    self["width"], self.display.ysize()))


class SchedDisplay(Canvas):
	def __init__(self, master):
		self.ratio = 1
		self.parent = master
		self.bdheight = master.bdheight
		self.items = {}
		self.lines = []
		Canvas.__init__(self, master, width=800, height=500, bg='grey',
		     scrollregion=(0, 0, 800, 500))

	def prepare(self):
		#
		# Compute a ratio to ensure that the file's timespan fits into
		# 2^31.  Although python may handle larger values for X
		# values, the Tk internals do not.
		#
		self.ratio = (ktrfile.timespan() - 1) / 2**31 + 1

	def draw(self):
		ypos = 0
		xsize = self.xsize()
		for source in sources:
			status.startup("Drawing " + source.name)
			l = self.create_line(0, ypos, xsize, ypos,
			    width=1, fill="black", tags=("all",))
			self.lines.append(l)
			ypos += self.bdheight
			ypos += source.ysize()
			source.draw(self, ypos)
			ypos += self.bdheight
		self.tag_raise("point", "state")
		self.tag_lower("cpubg", ALL)
		self.create_line(0, ypos, xsize, ypos,
		    width=1, fill="black", tags=("lines",))
		self.tag_bind("event", "<Enter>", self.mouseenter)
		self.tag_bind("event", "<Leave>", self.mouseexit)
		self.bind("<Button-1>", self.mousepress)
		self.bind("<Button-3>", self.master.mousepressright);
		self.bind("<Button-4>", self.wheelup)
		self.bind("<Button-5>", self.wheeldown)
		self.bind("<ButtonRelease-1>", self.master.mouserelease);
		self.bind("<B1-Motion>", self.master.mousemotion);

	def moveline(self, cur_y, y):
		for line in self.lines:
			(x0, y0, x1, y1) = self.coords(line)
			if (cur_y != y0):
				continue
			self.move(line, 0, y)
			return

	def mouseenter(self, event):
		item, = self.find_withtag(CURRENT)
		self.items[item].mouseenter(self)

	def mouseexit(self, event):
		item, = self.find_withtag(CURRENT)
		self.items[item].mouseexit(self)

	def mousepress(self, event):
		# Find out what's beneath us
		items = self.find_withtag(CURRENT)
		if (len(items) == 0):
			self.master.mousepress(event)
			return
		# Only grab mouse presses for things with event tags.
		item = items[0]
		tags = self.gettags(item)
		for tag in tags:
			if (tag == "event"):
				self.items[item].mousepress(self)
				return
		# Leave the rest to the master window
		self.master.mousepress(event)

	def wheeldown(self, event):
		self.parent.display_yview("scroll", 1, "units")

	def wheelup(self, event):
		self.parent.display_yview("scroll", -1, "units")

	def xsize(self):
		return ((ktrfile.timespan() / self.ratio) + (X_BORDER * 2))

	def ysize(self):
		ysize = 0
		for source in sources:
			if (source.hidden == 1):
				continue
			ysize += self.parent.sourcesize(source)
		return ysize

	def scaleset(self, ratio):
		if (ktrfile == None):
			return
		oldratio = self.ratio
		xstart, xend = self.xview()
		midpoint = xstart + ((xend - xstart) / 2)

		self.ratio = ratio
		self.updatescroll()
		self.scale(ALL, 0, 0, float(oldratio) / ratio, 1)

		xstart, xend = self.xview()
		xsize = (xend - xstart) / 2
		self.xview_moveto(midpoint - xsize)

	def updatescroll(self):
		self.configure(scrollregion=(0, 0, self.xsize(), self.ysize()))

	def scaleget(self):
		return self.ratio

	def getcolor(self, tag):
		return self.itemcget(tag, "fill")

	def getstate(self, tag):
		return self.itemcget(tag, "state")

	def setcolor(self, tag, color):
		self.itemconfigure(tag, state="normal", fill=color)

	def hide(self, tag):
		self.itemconfigure(tag, state="hidden")

class GraphMenu(Frame):
	def __init__(self, master):
		Frame.__init__(self, master, bd=2, relief=RAISED)
		self.conf = Menubutton(self, text="Configure")
		self.confmenu = Menu(self.conf, tearoff=0)
		self.confmenu.add_command(label="Event Colors",
		    command=self.econf)
		self.confmenu.add_command(label="CPU Colors",
		    command=self.cconf)
		self.confmenu.add_command(label="Source Configure",
		    command=self.sconf)
		self.conf["menu"] = self.confmenu
		self.conf.pack(side=LEFT)

	def econf(self):
		ColorConfigure(eventcolors, "Event Display Configuration")

	def cconf(self):
		ColorConfigure(cpucolors, "CPU Background Colors")

	def sconf(self):
		SourceConfigure()

class SchedGraph(Frame):
	def __init__(self, master):
		Frame.__init__(self, master)
		self.menu = None
		self.names = None
		self.display = None
		self.scale = None
		self.status = None
		self.bdheight = Y_BORDER
		self.clicksource = None
		self.lastsource = None
		self.pack(expand=1, fill="both")
		self.buildwidgets()
		self.layout()
		self.bind_all("<Control-q>", self.quitcb)

	def quitcb(self, event):
		self.quit()

	def buildwidgets(self):
		global status
		self.menu = GraphMenu(self)
		self.display = SchedDisplay(self)
		self.names = SchedNames(self, self.display)
		self.scale = Scaler(self, self.display)
		status = self.status = Status(self)
		self.scrollY = Scrollbar(self, orient="vertical",
		    command=self.display_yview)
		self.display.scrollX = Scrollbar(self, orient="horizontal",
		    command=self.display.xview)
		self.display["xscrollcommand"] = self.display.scrollX.set
		self.display["yscrollcommand"] = self.scrollY.set
		self.names["yscrollcommand"] = self.scrollY.set

	def layout(self):
		self.columnconfigure(1, weight=1)
		self.rowconfigure(1, weight=1)
		self.menu.grid(row=0, column=0, columnspan=3, sticky=E+W)
		self.names.grid(row=1, column=0, sticky=N+S)
		self.display.grid(row=1, column=1, sticky=W+E+N+S)
		self.scrollY.grid(row=1, column=2, sticky=N+S)
		self.display.scrollX.grid(row=2, column=0, columnspan=2,
		    sticky=E+W)
		self.scale.grid(row=3, column=0, columnspan=3, sticky=E+W)
		self.status.grid(row=4, column=0, columnspan=3, sticky=E+W)

	def draw(self):
		self.master.update()
		self.display.prepare()
		self.names.draw()
		self.display.draw()
		self.status.startup("")
		#
		# Configure scale related values
		#
		scalemax = ktrfile.timespan() / int(self.display["width"])
		width = int(root.geometry().split('x')[0])
		self.constwidth = width - int(self.display["width"])
		self.scale.setmax(scalemax)
		self.scale.set(scalemax)
		self.display.xview_moveto(0)
		self.bind("<Configure>", self.resize)

	def mousepress(self, event):
		self.clicksource = self.sourceat(event.y)

	def mousepressright(self, event):
		source = self.sourceat(event.y)
		if (source == None):
			return
		SourceContext(event, source)

	def mouserelease(self, event):
		if (self.clicksource == None):
			return
		newsource = self.sourceat(event.y)
		if (self.clicksource != newsource):
			self.sourceswap(self.clicksource, newsource)
		self.clicksource = None
		self.lastsource = None

	def mousemotion(self, event):
		if (self.clicksource == None):
			return
		newsource = self.sourceat(event.y)
		#
		# If we get a None source they moved off the page.
		# swapsource() can't handle moving multiple items so just
		# pretend we never clicked on anything to begin with so the
		# user can't mouseover a non-contiguous area.
		#
		if (newsource == None):
			self.clicksource = None
			self.lastsource = None
			return
		if (newsource == self.lastsource):
			return;
		self.lastsource = newsource
		if (newsource != self.clicksource):
			self.sourceswap(self.clicksource, newsource)

	# These are here because this object controls layout
	def sourcestart(self, source):
		return source.y - self.bdheight - source.ysize()

	def sourceend(self, source):
		return source.y + self.bdheight

	def sourcesize(self, source):
		return (self.bdheight * 2) + source.ysize()

	def sourceswap(self, source1, source2):
		# Sort so we always know which one is on top.
		if (source2.y < source1.y):
			swap = source1
			source1 = source2
			source2 = swap
		# Only swap adjacent sources
		if (self.sourceend(source1) != self.sourcestart(source2)):
			return
		# Compute start coordinates and target coordinates
		y1 = self.sourcestart(source1)
		y2 = self.sourcestart(source2)
		y1targ = y1 + self.sourcesize(source2)
		y2targ = y1
		#
		# If the sizes are not equal, adjust the start of the lower
		# source to account for the lost/gained space.
		#
		if (source1.ysize() != source2.ysize()):
			diff = source2.ysize() - source1.ysize()
			self.names.moveline(y2, diff);
			self.display.moveline(y2, diff)
		source1.move(self.display, 0, y1targ - y1)
		source2.move(self.display, 0, y2targ - y2)
		source1.movename(self.names, 0, y1targ - y1)
		source2.movename(self.names, 0, y2targ - y2)

	def sourcepicky(self, source):
		if (source.hidden == 0):
			return self.sourcestart(source)
		# Revert to group based sort
		sources.sort()
		prev = None
		for s in sources:
			if (s == source):
				break
			if (s.hidden == 0):
				prev = s
		if (prev == None):
			newy = 0
		else:
			newy = self.sourcestart(prev) + self.sourcesize(prev)
		return newy

	def sourceshow(self, source):
		if (source.hidden == 0):
			return;
		newy = self.sourcepicky(source)
		off = newy - self.sourcestart(source)
		self.sourceshiftall(newy-1, self.sourcesize(source))
		self.sourceshift(source, off)
		source.hidden = 0

	#
	# Optimized source show of multiple entries that only moves each
	# existing entry once.  Doing sourceshow() iteratively is too
	# expensive due to python's canvas.move().
	#
	def sourceshowlist(self, srclist):
		srclist.sort(cmp=source_cmp_start)
		startsize = []
		for source in srclist:
			if (source.hidden == 0):
				srclist.remove(source)
			startsize.append((self.sourcepicky(source),
			    self.sourcesize(source)))

		sources.sort(cmp=source_cmp_start, reverse=True)
		self.status.startup("Updating display...");
		for source in sources:
			if (source.hidden == 1):
				continue
			nstart = self.sourcestart(source)
			size = 0
			for hidden in startsize:
				(start, sz) = hidden
				if (start <= nstart or start+sz <= nstart):
					size += sz
			self.sourceshift(source, size)
		idx = 0
		size = 0
		for source in srclist:
			(newy, sz) = startsize[idx]
			off = (newy + size) - self.sourcestart(source)
			self.sourceshift(source, off)
			source.hidden = 0
			size += sz
			idx += 1
		self.updatescroll()
		self.status.set("")

	#
	# Optimized source hide of multiple entries that only moves each
	# remaining entry once.  Doing sourcehide() iteratively is too
	# expensive due to python's canvas.move().
	#
	def sourcehidelist(self, srclist):
		srclist.sort(cmp=source_cmp_start)
		sources.sort(cmp=source_cmp_start)
		startsize = []
		off = len(sources) * 100
		self.status.startup("Updating display...");
		for source in srclist:
			if (source.hidden == 1):
				srclist.remove(source)
			#
			# Remember our old position so we can sort things
			# below us when we're done.
			#
			startsize.append((self.sourcestart(source),
			    self.sourcesize(source)))
			self.sourceshift(source, off)
			source.hidden = 1

		idx = 0
		size = 0
		for hidden in startsize:
			(start, sz) = hidden
			size += sz
			if (idx + 1 < len(startsize)):
				(stop, sz) = startsize[idx+1]
			else:
				stop = self.display.ysize()
			idx += 1
			for source in sources:
				nstart = self.sourcestart(source)
				if (nstart < start or source.hidden == 1):
					continue
				if (nstart >= stop):
					break;
				self.sourceshift(source, -size)
		self.updatescroll()
		self.status.set("")

	def sourcehide(self, source):
		if (source.hidden == 1):
			return;
		# Move it out of the visible area
		off = len(sources) * 100
		start = self.sourcestart(source)
		self.sourceshift(source, off)
		self.sourceshiftall(start, -self.sourcesize(source))
		source.hidden = 1

	def sourceshift(self, source, off):
		start = self.sourcestart(source)
		source.move(self.display, 0, off)
		source.movename(self.names, 0, off)
		self.names.moveline(start, off);
		self.display.moveline(start, off)
		#
		# We update the idle tasks to shrink the dirtied area so
		# it does not always include the entire screen.
		#
		self.names.update_idletasks()
		self.display.update_idletasks()

	def sourceshiftall(self, start, off):
		self.status.startup("Updating display...");
		for source in sources:
			nstart = self.sourcestart(source)
			if (nstart < start):
				continue;
			self.sourceshift(source, off)
		self.updatescroll()
		self.status.set("")

	def sourceat(self, ypos):
		(start, end) = self.names.yview()
		starty = start * float(self.names.ysize)
		ypos += starty
		for source in sources:
			if (source.hidden == 1):
				continue;
			yend = self.sourceend(source)
			ystart = self.sourcestart(source)
			if (ypos >= ystart and ypos <= yend):
				return source
		return None

	def display_yview(self, *args):
		self.names.yview(*args)
		self.display.yview(*args)

	def resize(self, *args):
		width = int(root.geometry().split('x')[0])
		scalemax = ktrfile.timespan() / (width - self.constwidth)
		self.scale.setmax(scalemax)

	def updatescroll(self):
		self.names.updatescroll()
		self.display.updatescroll()

	def setcolor(self, tag, color):
		self.display.setcolor(tag, color)

	def hide(self, tag):
		self.display.hide(tag)

	def getcolor(self, tag):
		return self.display.getcolor(tag)

	def getstate(self, tag):
		return self.display.getstate(tag)

if (len(sys.argv) != 2 and len(sys.argv) != 3):
	print("usage:", sys.argv[0], "<ktr file> [clock freq in ghz]")
	sys.exit(1)

if (len(sys.argv) > 2):
	clockfreq = float(sys.argv[2])

root = Tk()
root.title("SchedGraph")
colormap = Colormap(eventcolors)
cpucolormap = Colormap(cpucolors)
graph = SchedGraph(root)
ktrfile = KTRFile(sys.argv[1])
graph.draw()
root.mainloop()
