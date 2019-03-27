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

local hook = {}

local registered_hooks = {}

-- Module exports
-- Register a hook type; these are the names that hooks may be registered for.
-- It is expected that modules will register all of their hook types upon
-- initial load. Other modules may then, at any time, register a hook for these
-- types.
--
-- Naming convention: hook types should be sensible named, preferably prefixed
-- with the name of the module that registered them. They would also ideally
-- describe an action that may or may not match a function name.
-- e.g. config.reloaded which takes place after config has been reloaded,
-- possibly from a different source.
function hook.registerType(hooktype)
	registered_hooks[hooktype] = {}
end

function hook.register(hooktype, hookfunc)
	local selected_hooks = registered_hooks[hooktype]
	if selected_hooks == nil then
		print("Tried registering a hook for an unknown hook type: " ..
		    hooktype)
		return
	end
	selected_hooks[#selected_hooks + 1] = hookfunc
	registered_hooks[hooktype] = selected_hooks
end

-- Takes a hooktype and runs all functions associated with that specific hook
-- type in the order that they were registered in. This ordering should likely
-- not be relied upon.
function hook.runAll(hooktype)
	local selected_hooks = registered_hooks[hooktype]
	if selected_hooks == nil then
		-- This message, and the print() above, should only be seen by
		-- developers. Hook type registration should have happened at
		-- module load, so if this hasn't happened then we have messed
		-- up the order in which we've loaded modules and we need to
		-- catch that as soon as possible.
		print("Tried to run hooks for an unknown hook type: " ..
		    hooktype)
		return 0
	end
	if #selected_hooks > 0 then
		for _, func in ipairs(selected_hooks) do
			func()
		end
	end
	return #selected_hooks
end

return hook
