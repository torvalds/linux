# SchedGui.py - Python extension for perf trace, basic GUI code for
#		traces drawing and overview.
#
# Copyright (C) 2010 by Frederic Weisbecker <fweisbec@gmail.com>
#
# This software is distributed under the terms of the GNU General
# Public License ("GPL") version 2 as published by the Free Software
# Foundation.


try:
	import wx
except ImportError:
	raise ImportError, "You need to install the wxpython lib for this script"


class RootFrame(wx.Frame):
	Y_OFFSET = 100
	RECT_HEIGHT = 100
	RECT_SPACE = 50
	EVENT_MARKING_WIDTH = 5

	def __init__(self, sched_tracer, title, parent = None, id = -1):
		wx.Frame.__init__(self, parent, id, title)

		(self.screen_width, self.screen_height) = wx.GetDisplaySize()
		self.screen_width -= 10
		self.screen_height -= 10
		self.zoom = 0.5
		self.scroll_scale = 20
		self.sched_tracer = sched_tracer
		self.sched_tracer.set_root_win(self)
		(self.ts_start, self.ts_end) = sched_tracer.interval()
		self.update_width_virtual()
		self.nr_rects = sched_tracer.nr_rectangles() + 1
		self.height_virtual = RootFrame.Y_OFFSET + (self.nr_rects * (RootFrame.RECT_HEIGHT + RootFrame.RECT_SPACE))

		# whole window panel
		self.panel = wx.Panel(self, size=(self.screen_width, self.screen_height))

		# scrollable container
		self.scroll = wx.ScrolledWindow(self.panel)
		self.scroll.SetScrollbars(self.scroll_scale, self.scroll_scale, self.width_virtual / self.scroll_scale, self.height_virtual / self.scroll_scale)
		self.scroll.EnableScrolling(True, True)
		self.scroll.SetFocus()

		# scrollable drawing area
		self.scroll_panel = wx.Panel(self.scroll, size=(self.screen_width - 15, self.screen_height / 2))
		self.scroll_panel.Bind(wx.EVT_PAINT, self.on_paint)
		self.scroll_panel.Bind(wx.EVT_KEY_DOWN, self.on_key_press)
		self.scroll_panel.Bind(wx.EVT_LEFT_DOWN, self.on_mouse_down)
		self.scroll.Bind(wx.EVT_PAINT, self.on_paint)
		self.scroll.Bind(wx.EVT_KEY_DOWN, self.on_key_press)
		self.scroll.Bind(wx.EVT_LEFT_DOWN, self.on_mouse_down)

		self.scroll.Fit()
		self.Fit()

		self.scroll_panel.SetDimensions(-1, -1, self.width_virtual, self.height_virtual, wx.SIZE_USE_EXISTING)

		self.txt = None

		self.Show(True)

	def us_to_px(self, val):
		return val / (10 ** 3) * self.zoom

	def px_to_us(self, val):
		return (val / self.zoom) * (10 ** 3)

	def scroll_start(self):
		(x, y) = self.scroll.GetViewStart()
		return (x * self.scroll_scale, y * self.scroll_scale)

	def scroll_start_us(self):
		(x, y) = self.scroll_start()
		return self.px_to_us(x)

	def paint_rectangle_zone(self, nr, color, top_color, start, end):
		offset_px = self.us_to_px(start - self.ts_start)
		width_px = self.us_to_px(end - self.ts_start)

		offset_py = RootFrame.Y_OFFSET + (nr * (RootFrame.RECT_HEIGHT + RootFrame.RECT_SPACE))
		width_py = RootFrame.RECT_HEIGHT

		dc = self.dc

		if top_color is not None:
			(r, g, b) = top_color
			top_color = wx.Colour(r, g, b)
			brush = wx.Brush(top_color, wx.SOLID)
			dc.SetBrush(brush)
			dc.DrawRectangle(offset_px, offset_py, width_px, RootFrame.EVENT_MARKING_WIDTH)
			width_py -= RootFrame.EVENT_MARKING_WIDTH
			offset_py += RootFrame.EVENT_MARKING_WIDTH

		(r ,g, b) = color
		color = wx.Colour(r, g, b)
		brush = wx.Brush(color, wx.SOLID)
		dc.SetBrush(brush)
		dc.DrawRectangle(offset_px, offset_py, width_px, width_py)

	def update_rectangles(self, dc, start, end):
		start += self.ts_start
		end += self.ts_start
		self.sched_tracer.fill_zone(start, end)

	def on_paint(self, event):
		dc = wx.PaintDC(self.scroll_panel)
		self.dc = dc

		width = min(self.width_virtual, self.screen_width)
		(x, y) = self.scroll_start()
		start = self.px_to_us(x)
		end = self.px_to_us(x + width)
		self.update_rectangles(dc, start, end)

	def rect_from_ypixel(self, y):
		y -= RootFrame.Y_OFFSET
		rect = y / (RootFrame.RECT_HEIGHT + RootFrame.RECT_SPACE)
		height = y % (RootFrame.RECT_HEIGHT + RootFrame.RECT_SPACE)

		if rect < 0 or rect > self.nr_rects - 1 or height > RootFrame.RECT_HEIGHT:
			return -1

		return rect

	def update_summary(self, txt):
		if self.txt:
			self.txt.Destroy()
		self.txt = wx.StaticText(self.panel, -1, txt, (0, (self.screen_height / 2) + 50))


	def on_mouse_down(self, event):
		(x, y) = event.GetPositionTuple()
		rect = self.rect_from_ypixel(y)
		if rect == -1:
			return

		t = self.px_to_us(x) + self.ts_start

		self.sched_tracer.mouse_down(rect, t)


	def update_width_virtual(self):
		self.width_virtual = self.us_to_px(self.ts_end - self.ts_start)

	def __zoom(self, x):
		self.update_width_virtual()
		(xpos, ypos) = self.scroll.GetViewStart()
		xpos = self.us_to_px(x) / self.scroll_scale
		self.scroll.SetScrollbars(self.scroll_scale, self.scroll_scale, self.width_virtual / self.scroll_scale, self.height_virtual / self.scroll_scale, xpos, ypos)
		self.Refresh()

	def zoom_in(self):
		x = self.scroll_start_us()
		self.zoom *= 2
		self.__zoom(x)

	def zoom_out(self):
		x = self.scroll_start_us()
		self.zoom /= 2
		self.__zoom(x)


	def on_key_press(self, event):
		key = event.GetRawKeyCode()
		if key == ord("+"):
			self.zoom_in()
			return
		if key == ord("-"):
			self.zoom_out()
			return

		key = event.GetKeyCode()
		(x, y) = self.scroll.GetViewStart()
		if key == wx.WXK_RIGHT:
			self.scroll.Scroll(x + 1, y)
		elif key == wx.WXK_LEFT:
			self.scroll.Scroll(x - 1, y)
		elif key == wx.WXK_DOWN:
			self.scroll.Scroll(x, y + 1)
		elif key == wx.WXK_UP:
			self.scroll.Scroll(x, y - 1)
