--
-- SPDX-License-Identifier: BSD-2-Clause-FreeBSD
--
-- Copyright (c) 2015 Pedro Souza <pedrosouza@freebsd.org>
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

local core = require("core")

local color = {}

-- Module exports
color.BLACK   = 0
color.RED     = 1
color.GREEN   = 2
color.YELLOW  = 3
color.BLUE    = 4
color.MAGENTA = 5
color.CYAN    = 6
color.WHITE   = 7

color.DEFAULT = 0
color.BRIGHT  = 1
color.DIM     = 2

function color.isEnabled()
	local c = loader.getenv("loader_color")
	if c ~= nil then
		if c:lower() == "no" or c == "0" then
			return false
		end
	end
	return not core.isSerialBoot()
end

color.disabled = not color.isEnabled()

function color.escapefg(color_value)
	if color.disabled then
		return color_value
	end
	return core.KEYSTR_CSI .. "3" .. color_value .. "m"
end

function color.resetfg()
	if color.disabled then
		return ''
	end
	return color.escapefg(color.WHITE)
end

function color.escapebg(color_value)
	if color.disabled then
		return color_value
	end
	return core.KEYSTR_CSI .. "4" .. color_value .. "m"
end

function color.resetbg()
	if color.disabled then
		return ''
	end
	return color.escapebg(color.BLACK)
end

function color.escape(fg_color, bg_color, attribute)
	if color.disabled then
		return ""
	end
	if attribute == nil then
		attribute = ""
	else
		attribute = attribute .. ";"
	end
	return core.KEYSTR_CSI .. attribute ..
	    "3" .. fg_color .. ";4" .. bg_color .. "m"
end

function color.default()
	if color.disabled then
		return ""
	end
	return color.escape(color.WHITE, color.BLACK, color.DEFAULT)
end

function color.highlight(str)
	if color.disabled then
		return str
	end
	-- We need to reset attributes as well as color scheme here, just in
	-- case the terminal defaults don't match what we're expecting.
	return core.KEYSTR_CSI .. "1m" .. str .. core.KEYSTR_CSI .. "22m"
end

return color
