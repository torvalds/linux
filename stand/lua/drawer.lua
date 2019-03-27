--
-- SPDX-License-Identifier: BSD-2-Clause-FreeBSD
--
-- Copyright (c) 2015 Pedro Souza <pedrosouza@freebsd.org>
-- Copyright (c) 2018 Kyle Evans <kevans@FreeBSD.org>
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions
-- are met:
-- 1. Redistributions of source code must retain the above copyright
--    notice, this list of conditions and the following disclaimer.
-- 2. Redistributions in binary form must reproduce the above copyright
--    notice, this list of conditions and the following disclaimer in the
--    documentation and/or other materials provided with the distribution.
--
-- THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
-- ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
-- ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
-- FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
-- DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
-- OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
-- HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
-- LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
-- OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
-- SUCH DAMAGE.
--
-- $FreeBSD$
--

local color = require("color")
local config = require("config")
local core = require("core")
local screen = require("screen")

local drawer = {}

local fbsd_brand
local none

local menu_name_handlers
local branddefs
local logodefs
local brand_position
local logo_position
local menu_position
local frame_size
local default_shift
local shift

local function menuEntryName(drawing_menu, entry)
	local name_handler = menu_name_handlers[entry.entry_type]

	if name_handler ~= nil then
		return name_handler(drawing_menu, entry)
	end
	if type(entry.name) == "function" then
		return entry.name()
	end
	return entry.name
end

local function getBranddef(brand)
	if brand == nil then
		return nil
	end
	-- Look it up
	local branddef = branddefs[brand]

	-- Try to pull it in
	if branddef == nil then
		try_include('brand-' .. brand)
		branddef = branddefs[brand]
	end

	return branddef
end

local function getLogodef(logo)
	if logo == nil then
		return nil
	end
	-- Look it up
	local logodef = logodefs[logo]

	-- Try to pull it in
	if logodef == nil then
		try_include('logo-' .. logo)
		logodef = logodefs[logo]
	end

	return logodef
end

local function draw(x, y, logo)
	for i = 1, #logo do
		screen.setcursor(x, y + i - 1)
		printc(logo[i])
	end
end

local function drawmenu(menudef)
	local x = menu_position.x
	local y = menu_position.y

	x = x + shift.x
	y = y + shift.y

	-- print the menu and build the alias table
	local alias_table = {}
	local entry_num = 0
	local menu_entries = menudef.entries
	local effective_line_num = 0
	if type(menu_entries) == "function" then
		menu_entries = menu_entries()
	end
	for _, e in ipairs(menu_entries) do
		-- Allow menu items to be conditionally visible by specifying
		-- a visible function.
		if e.visible ~= nil and not e.visible() then
			goto continue
		end
		effective_line_num = effective_line_num + 1
		if e.entry_type ~= core.MENU_SEPARATOR then
			entry_num = entry_num + 1
			screen.setcursor(x, y + effective_line_num)

			printc(entry_num .. ". " .. menuEntryName(menudef, e))

			-- fill the alias table
			alias_table[tostring(entry_num)] = e
			if e.alias ~= nil then
				for _, a in ipairs(e.alias) do
					alias_table[a] = e
				end
			end
		else
			screen.setcursor(x, y + effective_line_num)
			printc(menuEntryName(menudef, e))
		end
		::continue::
	end
	return alias_table
end

local function drawbox()
	local x = menu_position.x - 3
	local y = menu_position.y - 1
	local w = frame_size.w
	local h = frame_size.h

	local framestyle = loader.getenv("loader_menu_frame") or "double"
	local framespec = drawer.frame_styles[framestyle]
	-- If we don't have a framespec for the current frame style, just don't
	-- draw a box.
	if framespec == nil then
		return
	end

	local hl = framespec.horizontal
	local vl = framespec.vertical

	local tl = framespec.top_left
	local bl = framespec.bottom_left
	local tr = framespec.top_right
	local br = framespec.bottom_right

	x = x + shift.x
	y = y + shift.y

	screen.setcursor(x, y); printc(tl)
	screen.setcursor(x, y + h); printc(bl)
	screen.setcursor(x + w, y); printc(tr)
	screen.setcursor(x + w, y + h); printc(br)

	screen.setcursor(x + 1, y)
	for _ = 1, w - 1 do
		printc(hl)
	end

	screen.setcursor(x + 1, y + h)
	for _ = 1, w - 1 do
		printc(hl)
	end

	for i = 1, h - 1 do
		screen.setcursor(x, y + i)
		printc(vl)
		screen.setcursor(x + w, y + i)
		printc(vl)
	end

	local menu_header = loader.getenv("loader_menu_title") or
	    "Welcome to FreeBSD"
	local menu_header_align = loader.getenv("loader_menu_title_align")
	local menu_header_x

	if menu_header_align ~= nil then
		menu_header_align = menu_header_align:lower()
		if menu_header_align == "left" then
			-- Just inside the left border on top
			menu_header_x = x + 1
		elseif menu_header_align == "right" then
			-- Just inside the right border on top
			menu_header_x = x + w - #menu_header
		end
	end
	if menu_header_x == nil then
		menu_header_x = x + (w / 2) - (#menu_header / 2)
	end
	screen.setcursor(menu_header_x, y)
	printc(menu_header)
end

local function drawbrand()
	local x = tonumber(loader.getenv("loader_brand_x")) or
	    brand_position.x
	local y = tonumber(loader.getenv("loader_brand_y")) or
	    brand_position.y

	local branddef = getBranddef(loader.getenv("loader_brand"))

	if branddef == nil then
		branddef = getBranddef(drawer.default_brand)
	end

	local graphic = branddef.graphic

	x = x + shift.x
	y = y + shift.y
	draw(x, y, graphic)
end

local function drawlogo()
	local x = tonumber(loader.getenv("loader_logo_x")) or
	    logo_position.x
	local y = tonumber(loader.getenv("loader_logo_y")) or
	    logo_position.y

	local logo = loader.getenv("loader_logo")
	local colored = color.isEnabled()

	local logodef = getLogodef(logo)

	if logodef == nil or logodef.graphic == nil or
	    (not colored and logodef.requires_color) then
		-- Choose a sensible default
		if colored then
			logodef = getLogodef(drawer.default_color_logodef)
		else
			logodef = getLogodef(drawer.default_bw_logodef)
		end
	end

	if logodef ~= nil and logodef.graphic == none then
		shift = logodef.shift
	else
		shift = default_shift
	end

	x = x + shift.x
	y = y + shift.y

	if logodef ~= nil and logodef.shift ~= nil then
		x = x + logodef.shift.x
		y = y + logodef.shift.y
	end

	draw(x, y, logodef.graphic)
end

fbsd_brand = {
"  ______               ____   _____ _____  ",
" |  ____|             |  _ \\ / ____|  __ \\ ",
" | |___ _ __ ___  ___ | |_) | (___ | |  | |",
" |  ___| '__/ _ \\/ _ \\|  _ < \\___ \\| |  | |",
" | |   | | |  __/  __/| |_) |____) | |__| |",
" | |   | | |    |    ||     |      |      |",
" |_|   |_|  \\___|\\___||____/|_____/|_____/ "
}
none = {""}

menu_name_handlers = {
	-- Menu name handlers should take the menu being drawn and entry being
	-- drawn as parameters, and return the name of the item.
	-- This is designed so that everything, including menu separators, may
	-- have their names derived differently. The default action for entry
	-- types not specified here is to use entry.name directly.
	[core.MENU_SEPARATOR] = function(_, entry)
		if entry.name ~= nil then
			if type(entry.name) == "function" then
				return entry.name()
			end
			return entry.name
		end
		return ""
	end,
	[core.MENU_CAROUSEL_ENTRY] = function(_, entry)
		local carid = entry.carousel_id
		local caridx = config.getCarouselIndex(carid)
		local choices = entry.items
		if type(choices) == "function" then
			choices = choices()
		end
		if #choices < caridx then
			caridx = 1
		end
		return entry.name(caridx, choices[caridx], choices)
	end,
}

branddefs = {
	-- Indexed by valid values for loader_brand in loader.conf(5). Valid
	-- keys are: graphic (table depicting graphic)
	["fbsd"] = {
		graphic = fbsd_brand,
	},
	["none"] = {
		graphic = none,
	},
}

logodefs = {
	-- Indexed by valid values for loader_logo in loader.conf(5). Valid keys
	-- are: requires_color (boolean), graphic (table depicting graphic), and
	-- shift (table containing x and y).
	["tribute"] = {
		graphic = fbsd_brand,
	},
	["tributebw"] = {
		graphic = fbsd_brand,
	},
	["none"] = {
		graphic = none,
		shift = {x = 17, y = 0},
	},
}

brand_position = {x = 2, y = 1}
logo_position = {x = 46, y = 4}
menu_position = {x = 5, y = 10}
frame_size = {w = 42, h = 13}
default_shift = {x = 0, y = 0}
shift = default_shift

-- Module exports
drawer.default_brand = 'fbsd'
drawer.default_color_logodef = 'orb'
drawer.default_bw_logodef = 'orbbw'

function drawer.addBrand(name, def)
	branddefs[name] = def
end

function drawer.addLogo(name, def)
	logodefs[name] = def
end

drawer.frame_styles = {
	-- Indexed by valid values for loader_menu_frame in loader.conf(5).
	-- All of the keys appearing below must be set for any menu frame style
	-- added to drawer.frame_styles.
	["ascii"] = {
		horizontal	= "-",
		vertical	= "|",
		top_left	= "+",
		bottom_left	= "+",
		top_right	= "+",
		bottom_right	= "+",
	},
	["single"] = {
		horizontal	= "\xC4",
		vertical	= "\xB3",
		top_left	= "\xDA",
		bottom_left	= "\xC0",
		top_right	= "\xBF",
		bottom_right	= "\xD9",
	},
	["double"] = {
		horizontal	= "\xCD",
		vertical	= "\xBA",
		top_left	= "\xC9",
		bottom_left	= "\xC8",
		top_right	= "\xBB",
		bottom_right	= "\xBC",
	},
}

function drawer.drawscreen(menudef)
	-- drawlogo() must go first.
	-- it determines the positions of other elements
	drawlogo()
	drawbrand()
	drawbox()
	return drawmenu(menudef)
end

return drawer
