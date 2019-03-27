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

local config = require("config")
local hook = require("hook")

local core = {}

local default_safe_mode = false
local default_single_user = false
local default_verbose = false

local function composeLoaderCmd(cmd_name, argstr)
	if argstr ~= nil then
		cmd_name = cmd_name .. " " .. argstr
	end
	return cmd_name
end

local function recordDefaults()
	-- On i386, hint.acpi.0.rsdp will be set before we're loaded. On !i386,
	-- it will generally be set upon execution of the kernel. Because of
	-- this, we can't (or don't really want to) detect/disable ACPI on !i386
	-- reliably. Just set it enabled if we detect it and leave well enough
	-- alone if we don't.
	local boot_acpi = core.isSystem386() and core.getACPIPresent(false)
	local boot_single = loader.getenv("boot_single") or "no"
	local boot_verbose = loader.getenv("boot_verbose") or "no"
	default_single_user = boot_single:lower() ~= "no"
	default_verbose = boot_verbose:lower() ~= "no"

	if boot_acpi then
		core.setACPI(true)
	end
	core.setSingleUser(default_single_user)
	core.setVerbose(default_verbose)
end


-- Globals
-- try_include will return the loaded module on success, or false and the error
-- message on failure.
function try_include(module)
	local status, ret = pcall(require, module)
	-- ret is the module if we succeeded.
	if status then
		return ret
	end
	return false, ret
end

-- Module exports
-- Commonly appearing constants
core.KEY_BACKSPACE	= 8
core.KEY_ENTER		= 13
core.KEY_DELETE		= 127

-- Note that this is a decimal representation, despite the leading 0 that in
-- other contexts (outside of Lua) may mean 'octal'
core.KEYSTR_ESCAPE	= "\027"
core.KEYSTR_CSI		= core.KEYSTR_ESCAPE .. "["

core.MENU_RETURN	= "return"
core.MENU_ENTRY		= "entry"
core.MENU_SEPARATOR	= "separator"
core.MENU_SUBMENU	= "submenu"
core.MENU_CAROUSEL_ENTRY	= "carousel_entry"

function core.setVerbose(verbose)
	if verbose == nil then
		verbose = not core.verbose
	end

	if verbose then
		loader.setenv("boot_verbose", "YES")
	else
		loader.unsetenv("boot_verbose")
	end
	core.verbose = verbose
end

function core.setSingleUser(single_user)
	if single_user == nil then
		single_user = not core.su
	end

	if single_user then
		loader.setenv("boot_single", "YES")
	else
		loader.unsetenv("boot_single")
	end
	core.su = single_user
end

function core.getACPIPresent(checking_system_defaults)
	local c = loader.getenv("hint.acpi.0.rsdp")

	if c ~= nil then
		if checking_system_defaults then
			return true
		end
		-- Otherwise, respect disabled if it's set
		c = loader.getenv("hint.acpi.0.disabled")
		return c == nil or tonumber(c) ~= 1
	end
	return false
end

function core.setACPI(acpi)
	if acpi == nil then
		acpi = not core.acpi
	end

	if acpi then
		loader.setenv("acpi_load", "YES")
		loader.setenv("hint.acpi.0.disabled", "0")
		loader.unsetenv("loader.acpi_disabled_by_user")
	else
		loader.unsetenv("acpi_load")
		loader.setenv("hint.acpi.0.disabled", "1")
		loader.setenv("loader.acpi_disabled_by_user", "1")
	end
	core.acpi = acpi
end

function core.setSafeMode(safe_mode)
	if safe_mode == nil then
		safe_mode = not core.sm
	end
	if safe_mode then
		loader.setenv("kern.smp.disabled", "1")
		loader.setenv("hw.ata.ata_dma", "0")
		loader.setenv("hw.ata.atapi_dma", "0")
		loader.setenv("hw.ata.wc", "0")
		loader.setenv("hw.eisa_slots", "0")
		loader.setenv("kern.eventtimer.periodic", "1")
		loader.setenv("kern.geom.part.check_integrity", "0")
	else
		loader.unsetenv("kern.smp.disabled")
		loader.unsetenv("hw.ata.ata_dma")
		loader.unsetenv("hw.ata.atapi_dma")
		loader.unsetenv("hw.ata.wc")
		loader.unsetenv("hw.eisa_slots")
		loader.unsetenv("kern.eventtimer.periodic")
		loader.unsetenv("kern.geom.part.check_integrity")
	end
	core.sm = safe_mode
end

function core.clearCachedKernels()
	-- Clear the kernel cache on config changes, autodetect might have
	-- changed or if we've switched boot environments then we could have
	-- a new kernel set.
	core.cached_kernels = nil
end

function core.kernelList()
	if core.cached_kernels ~= nil then
		return core.cached_kernels
	end

	local k = loader.getenv("kernel")
	local v = loader.getenv("kernels")
	local autodetect = loader.getenv("kernels_autodetect") or ""

	local kernels = {}
	local unique = {}
	local i = 0
	if k ~= nil then
		i = i + 1
		kernels[i] = k
		unique[k] = true
	end

	if v ~= nil then
		for n in v:gmatch("([^;, ]+)[;, ]?") do
			if unique[n] == nil then
				i = i + 1
				kernels[i] = n
				unique[n] = true
			end
		end
	end

	-- Base whether we autodetect kernels or not on a loader.conf(5)
	-- setting, kernels_autodetect. If it's set to 'yes', we'll add
	-- any kernels we detect based on the criteria described.
	if autodetect:lower() ~= "yes" then
		core.cached_kernels = kernels
		return core.cached_kernels
	end

	-- Automatically detect other bootable kernel directories using a
	-- heuristic.  Any directory in /boot that contains an ordinary file
	-- named "kernel" is considered eligible.
	for file in lfs.dir("/boot") do
		local fname = "/boot/" .. file

		if file == "." or file == ".." then
			goto continue
		end

		if lfs.attributes(fname, "mode") ~= "directory" then
			goto continue
		end

		if lfs.attributes(fname .. "/kernel", "mode") ~= "file" then
			goto continue
		end

		if unique[file] == nil then
			i = i + 1
			kernels[i] = file
			unique[file] = true
		end

		::continue::
	end
	core.cached_kernels = kernels
	return core.cached_kernels
end

function core.bootenvDefault()
	return loader.getenv("zfs_be_active")
end

function core.bootenvList()
	local bootenv_count = tonumber(loader.getenv("bootenvs_count"))
	local bootenvs = {}
	local curenv
	local envcount = 0
	local unique = {}

	if bootenv_count == nil or bootenv_count <= 0 then
		return bootenvs
	end

	-- Currently selected bootenv is always first/default
	curenv = core.bootenvDefault()
	if curenv ~= nil then
		envcount = envcount + 1
		bootenvs[envcount] = curenv
		unique[curenv] = true
	end

	for curenv_idx = 0, bootenv_count - 1 do
		curenv = loader.getenv("bootenvs[" .. curenv_idx .. "]")
		if curenv ~= nil and unique[curenv] == nil then
			envcount = envcount + 1
			bootenvs[envcount] = curenv
			unique[curenv] = true
		end
	end
	return bootenvs
end

function core.setDefaults()
	core.setACPI(core.getACPIPresent(true))
	core.setSafeMode(default_safe_mode)
	core.setSingleUser(default_single_user)
	core.setVerbose(default_verbose)
end

function core.autoboot(argstr)
	-- loadelf() only if we've not already loaded a kernel
	if loader.getenv("kernelname") == nil then
		config.loadelf()
	end
	loader.perform(composeLoaderCmd("autoboot", argstr))
end

function core.boot(argstr)
	-- loadelf() only if we've not already loaded a kernel
	if loader.getenv("kernelname") == nil then
		config.loadelf()
	end
	loader.perform(composeLoaderCmd("boot", argstr))
end

function core.isSingleUserBoot()
	local single_user = loader.getenv("boot_single")
	return single_user ~= nil and single_user:lower() == "yes"
end

function core.isUEFIBoot()
	local efiver = loader.getenv("efi-version")

	return efiver ~= nil
end

function core.isZFSBoot()
	local c = loader.getenv("currdev")

	if c ~= nil then
		return c:match("^zfs:") ~= nil
	end
	return false
end

function core.isSerialBoot()
	local s = loader.getenv("boot_serial")
	if s ~= nil then
		return true
	end

	local m = loader.getenv("boot_multicons")
	if m ~= nil then
		return true
	end
	return false
end

function core.isSystem386()
	return loader.machine_arch == "i386"
end

-- Is the menu skipped in the environment in which we've booted?
function core.isMenuSkipped()
	return string.lower(loader.getenv("beastie_disable") or "") == "yes"
end

-- This may be a better candidate for a 'utility' module.
function core.deepCopyTable(tbl)
	local new_tbl = {}
	for k, v in pairs(tbl) do
		if type(v) == "table" then
			new_tbl[k] = core.deepCopyTable(v)
		else
			new_tbl[k] = v
		end
	end
	return new_tbl
end

-- XXX This should go away if we get the table lib into shape for importing.
-- As of now, it requires some 'os' functions, so we'll implement this in lua
-- for our uses
function core.popFrontTable(tbl)
	-- Shouldn't reasonably happen
	if #tbl == 0 then
		return nil, nil
	elseif #tbl == 1 then
		return tbl[1], {}
	end

	local first_value = tbl[1]
	local new_tbl = {}
	-- This is not a cheap operation
	for k, v in ipairs(tbl) do
		if k > 1 then
			new_tbl[k - 1] = v
		end
	end

	return first_value, new_tbl
end

recordDefaults()
hook.register("config.reloaded", core.clearCachedKernels)
return core
