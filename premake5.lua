workspace "kernel"
   configurations { "Debug" }
   
project "kernel"
	kind "ConsoleApp"
	language "C"
	files { "**.h", "**.c", "**.S" }
	includedirs { "." }
	defines {"__KERNEL__"}
