--
-- SPDX-License-Identifier: BSD-2-Clause-FreeBSD
--
-- Copyright (c) 2018 Kyle Evans <kevans@FreeBSD.org>
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

local config = require("config")
local core = require("core")

local cli = {}

-- Internal function
-- Parses arguments to boot and returns two values: kernel_name, argstr
-- Defaults to nil and "" respectively.
-- This will also parse arguments to autoboot, but the with_kernel argument
-- will need to be explicitly overwritten to false
local function parseBootArgs(argv, with_kernel)
	if with_kernel == nil then
		with_kernel = true
	end
	if #argv == 0 then
		if with_kernel then
			return nil, ""
		else
			return ""
		end
	end
	local kernel_name
	local argstr = ""

	for _, v in ipairs(argv) do
		if with_kernel and v:sub(1,1) ~= "-" then
			kernel_name = v
		else
			argstr = argstr .. " " .. v
		end
	end
	if with_kernel then
		return kernel_name, argstr
	else
		return argstr
	end
end

-- Declares a global function cli_execute that attempts to dispatch the
-- arguments passed as a lua function. This gives lua a chance to intercept
-- builtin CLI commands like "boot"
-- This function intentionally does not follow our general naming guideline for
-- functions. This is global pollution, but the clearly separated 'cli' looks
-- more like a module indicator to serve as a hint of where to look for the
-- corresponding definition.
function cli_execute(...)
	local argv = {...}
	-- Just in case...
	if #argv == 0 then
		return loader.command(...)
	end

	local cmd_name = argv[1]
	local cmd = cli[cmd_name]
	if cmd ~= nil and type(cmd) == "function" then
		-- Pass argv wholesale into cmd. We could omit argv[0] since the
		-- traditional reasons for including it don't necessarily apply,
		-- it may not be totally redundant if we want to have one global
		-- handling multiple commands
		return cmd(...)
	else
		return loader.command(...)
	end

end

function cli_execute_unparsed(str)
	return cli_execute(loader.parse(str))
end

-- Module exports

function cli.boot(...)
	local _, argv = cli.arguments(...)
	local kernel, argstr = parseBootArgs(argv)
	if kernel ~= nil then
		loader.perform("unload")
		config.selectKernel(kernel)
	end
	core.boot(argstr)
end

function cli.autoboot(...)
	local _, argv = cli.arguments(...)
	local argstr = parseBootArgs(argv, false)
	core.autoboot(argstr)
end

cli['boot-conf'] = function(...)
	local _, argv = cli.arguments(...)
	local kernel, argstr = parseBootArgs(argv)
	if kernel ~= nil then
		loader.perform("unload")
		config.selectKernel(kernel)
	end
	core.autoboot(argstr)
end

-- Used for splitting cli varargs into cmd_name and the rest of argv
function cli.arguments(...)
	local argv = {...}
	local cmd_name
	cmd_name, argv = core.popFrontTable(argv)
	return cmd_name, argv
end

return cli
