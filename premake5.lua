workspace "kernel"
   configurations { "Debug" }
   
project "kernel"
	kind "ConsoleApp"
	language "C"
	files { "**.*" }
	includedirs { "." }
	defines {"__KERNEL__"}
