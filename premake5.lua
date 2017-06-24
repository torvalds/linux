local PD = {"__KERNEL__"}
local file_path = "D:\\github\\czlc\\linux\\arch\\x86\\configs\\x86_64_defconfig"
local file_data = io.open(file_path, "r"):read("*a")

for w in string.gmatch(file_data, "[^\n\r]+") do
	if (string.sub(w, 1, 1) ~= "#") then
		local M,C = string.gsub(w, "(%w+)=y", "%1")
		if (C == 1) then
			table.insert(PD, M)
		end
	end
end


workspace "kernel"
   configurations { "Debug" }
   
project "kernel"
	kind "ConsoleApp"
	language "C"
	files { "**.*" }
	includedirs { "." }
	defines(PD)
