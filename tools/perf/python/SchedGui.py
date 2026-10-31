#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# SchedGui.py - Python extension for perf script, basic GUI code for
#		traces drawing and overview.
#
# Copyright (C) 2010 by Frederic Weisbecker <fweisbec@gmail.com>
#
# Ported to modern directory structure.

"""SchedGui.py - Python extension for perf script, basic GUI code for traces drawing and overview."""
from __future__ import annotations

import importlib
from typing import Any

class _DummyWx:
    """Dummy wx module fallback when wxPython is not installed."""
    Frame = object


try:
    wx: Any = importlib.import_module("wx")
    WX_AVAILABLE = True
except ImportError:
    wx = _DummyWx
    WX_AVAILABLE = False


class RootFrame(wx.Frame):
    """Main window frame for scheduling trace visualization."""
    Y_OFFSET = 100
    RECT_HEIGHT = 100
    RECT_SPACE = 50
    EVENT_MARKING_WIDTH = 5

    def __init__(self, sched_tracer, title, parent=None, win_id=-1):
        if not WX_AVAILABLE:
            raise ImportError("You need to install the wxpython lib for this script")
        wx.Frame.__init__(self, parent, win_id, title)

        self.dc = None
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
        self.height_virtual = RootFrame.Y_OFFSET + \
            (self.nr_rects * (RootFrame.RECT_HEIGHT + RootFrame.RECT_SPACE))

        # whole window panel
        self.panel = wx.Panel(self, size=(self.screen_width, self.screen_height))

        # scrollable container
        # Create SplitterWindow
        self.splitter = wx.SplitterWindow(self.panel, style=wx.SP_3D)

        # scrollable container (Top)
        self.scroll = wx.ScrolledWindow(self.splitter)
        self.scroll.SetScrollbars(self.scroll_scale, self.scroll_scale,
                                  int(self.width_virtual // self.scroll_scale),
                                  int(self.height_virtual // self.scroll_scale))
        self.scroll.EnableScrolling(True, True)
        self.scroll.SetFocus()

        # scrollable drawing area
        self.scroll_panel = wx.Panel(self.scroll,
                                     size=(self.screen_width - 15, self.screen_height // 2))
        self.scroll_panel.Bind(wx.EVT_PAINT, self.on_paint)
        self.scroll_panel.Bind(wx.EVT_KEY_DOWN, self.on_key_press)
        self.scroll_panel.Bind(wx.EVT_LEFT_DOWN, self.on_mouse_down)
        self.scroll.Bind(wx.EVT_KEY_DOWN, self.on_key_press)
        self.scroll.Bind(wx.EVT_LEFT_DOWN, self.on_mouse_down)

        self.scroll_panel.SetSize(int(self.width_virtual), int(self.height_virtual))

        # Create a separate panel for text (Bottom)
        self.text_panel = wx.Panel(self.splitter)
        self.text_sizer = wx.BoxSizer(wx.VERTICAL)
        self.txt = wx.TextCtrl(self.text_panel, -1, "Click a bar to see details",
                               style=wx.TE_MULTILINE)
        self.text_sizer.Add(self.txt, 1, wx.EXPAND | wx.ALL, 5)
        self.text_panel.SetSizer(self.text_sizer)

        # Split the window
        self.splitter.SplitHorizontally(self.scroll, self.text_panel, (self.screen_height * 3) // 4)

        # Main sizer to layout splitter
        self.main_sizer = wx.BoxSizer(wx.VERTICAL)
        self.main_sizer.Add(self.splitter, 1, wx.EXPAND)
        self.panel.SetSizer(self.main_sizer)

        self.scroll.Fit()
        self.Fit()

        self.Show(True)

    def us_to_px(self, val):
        """Convert microseconds to pixels."""
        return val / (10 ** 3) * self.zoom

    def px_to_us(self, val):
        """Convert pixels to microseconds."""
        return (val / self.zoom) * (10 ** 3)

    def scroll_start(self):
        """Get scroll start position in pixels."""
        (x, y) = self.scroll.GetViewStart()
        return (x * self.scroll_scale, y * self.scroll_scale)

    def scroll_start_us(self):
        """Get scroll start position in microseconds."""
        (x, _) = self.scroll_start()
        return self.px_to_us(x)

    def paint_rectangle_zone(self, nr, color, top_color, start, end):
        """Draw a rectangle zone for a CPU."""
        offset_px = self.us_to_px(start - self.ts_start)
        width_px = self.us_to_px(end - start)

        offset_py = RootFrame.Y_OFFSET + (nr * (RootFrame.RECT_HEIGHT + RootFrame.RECT_SPACE))
        width_py = RootFrame.RECT_HEIGHT

        dc = self.dc

        if top_color is not None:
            (r, g, b) = top_color
            top_color = wx.Colour(r, g, b)
            brush = wx.Brush(top_color, wx.SOLID)
            dc.SetBrush(brush)
            dc.DrawRectangle(int(offset_px), int(offset_py),
                             int(width_px), RootFrame.EVENT_MARKING_WIDTH)
            width_py -= RootFrame.EVENT_MARKING_WIDTH
            offset_py += RootFrame.EVENT_MARKING_WIDTH

        (r, g, b) = color
        color = wx.Colour(r, g, b)
        brush = wx.Brush(color, wx.SOLID)
        dc.SetBrush(brush)
        dc.DrawRectangle(int(offset_px), int(offset_py), int(width_px), int(width_py))

    def update_rectangles(self, start, end):
        """Update rectangles in the given time window."""
        start += self.ts_start
        end += self.ts_start
        self.sched_tracer.fill_zone(start, end)

    def on_paint(self, event):
        """Handle paint event."""
        window = event.GetEventObject()
        dc = wx.PaintDC(window)

        # Clear background to avoid ghosting
        dc.SetBackground(wx.Brush(window.GetBackgroundColour()))
        dc.Clear()

        self.dc = dc
        try:
            width = min(self.width_virtual, self.screen_width)
            (x, _) = self.scroll_start()
            start = self.px_to_us(x)
            end = self.px_to_us(x + width)
            self.update_rectangles(start, end)

            # Draw CPU labels at the left edge of the visible area
            (x_scroll, _) = self.scroll_start()
            for nr in range(self.nr_rects):
                offset_py = RootFrame.Y_OFFSET + (nr * (RootFrame.RECT_HEIGHT + RootFrame.RECT_SPACE))
                dc.DrawText(f"CPU {nr}", x_scroll + 10, offset_py + 10)
        finally:
            self.dc = None

    def rect_from_ypixel(self, y):
        y -= RootFrame.Y_OFFSET
        rect = y // (RootFrame.RECT_HEIGHT + RootFrame.RECT_SPACE)
        height = y % (RootFrame.RECT_HEIGHT + RootFrame.RECT_SPACE)

        if rect < 0 or rect > self.nr_rects - 1 or height > RootFrame.RECT_HEIGHT:
            return -1

        return rect

    def update_summary(self, txt):
        self.txt.SetValue(txt)
        self.text_panel.Layout()
        self.splitter.Layout()
        self.text_panel.Refresh()

    def on_mouse_down(self, event):
        pos = event.GetPosition()
        x, y = pos.x, pos.y
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
        xpos = int(self.us_to_px(x) // self.scroll_scale)
        self.scroll_panel.SetSize((int(self.width_virtual), int(self.height_virtual)))
        self.scroll.SetScrollbars(self.scroll_scale, self.scroll_scale,
                                  int(self.width_virtual // self.scroll_scale),
                                  int(self.height_virtual // self.scroll_scale),
                                  xpos, ypos)
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
