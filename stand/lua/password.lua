--
-- SPDX-License-Identifier: BSD-2-Clause-FreeBSD
--
-- Copyright (c) 2015 Pedro Souza <pedrosouza@freebsd.org>
-- Copyright (C) 2018 Kyle Evans <kevans@FreeBSD.org>
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
local screen = require("screen")

local password = {}

local INCORRECT_PASSWORD = "loader: incorrect password"
-- Asterisks as a password mask
local show_password_mask = false
local twiddle_chars = {"/", "-", "\\", "|"}
local screen_setup = false

local function setup_screen()
	screen.clear()
	screen.defcursor()
	screen_setup = true
end

-- Module exports
function password.read(prompt_length)
	local str = ""
	local twiddle_pos = 1

	local function draw_twiddle()
		printc(twiddle_chars[twiddle_pos])
		-- Reset cursor to just after the password prompt
		screen.setcursor(prompt_length + 2, screen.default_y)
		twiddle_pos = (twiddle_pos % #twiddle_chars) + 1
	end

	-- Space between the prompt and any on-screen feedback
	printc(" ")
	while true do
		local ch = io.getchar()
		if ch == core.KEY_ENTER then
			break
		end
		if ch == core.KEY_BACKSPACE or ch == core.KEY_DELETE then
			if #str > 0 then
				if show_password_mask then
					printc("\008 \008")
				else
					draw_twiddle()
				end
				str = str:sub(1, #str - 1)
			end
		else
			if show_password_mask then
				printc("*")
			else
				draw_twiddle()
			end
			str = str .. string.char(ch)
		end
	end
	return str
end

function password.check()
	-- pwd is optionally supplied if we want to check it
	local function doPrompt(prompt, pwd)
		local attempts = 1

		local function clear_incorrect_text_prompt()
			printc("\r" .. string.rep(" ", #INCORRECT_PASSWORD))
		end

		if not screen_setup then
			setup_screen()
		end

		while true do
			if attempts > 1 then
				clear_incorrect_text_prompt()
			end
			screen.defcursor()
			printc(prompt)
			local read_pwd = password.read(#prompt)
			if pwd == nil or pwd == read_pwd then
				-- Clear the prompt + twiddle
				printc(string.rep(" ", #prompt + 5))
				return read_pwd
			end
			printc("\n" .. INCORRECT_PASSWORD)
			attempts = attempts + 1
			loader.delay(3*1000*1000)
		end
	end
	local function compare(prompt, pwd)
		if pwd == nil then
			return
		end
		doPrompt(prompt, pwd)
	end

	local boot_pwd = loader.getenv("bootlock_password")
	compare("Bootlock password:", boot_pwd)

	local geli_prompt = loader.getenv("geom_eli_passphrase_prompt")
	if geli_prompt ~= nil and geli_prompt:lower() == "yes" then
		local passphrase = doPrompt("GELI Passphrase:")
		loader.setenv("kern.geom.eli.passphrase", passphrase)
	end

	local pwd = loader.getenv("password")
	if pwd ~= nil then
		core.autoboot()
		-- The autoboot sequence was interrupted, so we'll need to
		-- prompt for a password.  Put the screen back into a known
		-- good state, otherwise we're drawing back a couple lines
		-- in the middle of other text.
		setup_screen()
	end
	compare("Loader password:", pwd)
end

return password
