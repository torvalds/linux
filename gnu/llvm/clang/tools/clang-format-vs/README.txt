This directory contains a VSPackage project to generate a Visual Studio extension
for clang-format.

Build prerequisites are:
- Visual Studio 2015
- Extensions SDK (you'll be prompted to install it if you open ClangFormat.sln)

The extension is built using CMake to generate the usual LLVM.sln by setting
the following CMake vars:

- BUILD_CLANG_FORMAT_VS_PLUGIN=ON

- NUGET_EXE_DIR=path/to/nuget_dir (unless nuget.exe is already available in PATH)

example:
  cd /d C:\code\llvm
  mkdir build & cd build
  cmake -DBUILD_CLANG_FORMAT_VS_PLUGIN=ON -DNUGET_EXE_DIR=C:\nuget ..

Once LLVM.sln is generated, build the clang_format_vsix target, which will build
ClangFormat.sln, the C# extension application.

The CMake build will copy clang-format.exe and LICENSE.TXT into the ClangFormat/
directory so they can be bundled with the plug-in, as well as creating
ClangFormat/source.extension.vsixmanifest. Once the plug-in has been built with
CMake once, it can be built manually from the ClangFormat.sln solution in Visual
Studio.

===========
 Debugging
===========

Once you've built the clang_format_vsix project from LLVM.sln at least once,
open ClangFormat.sln in Visual Studio, then:

- Make sure the "Debug" target is selected
- Open the ClangFormat project properties
- Select the Debug tab
- Set "Start external program:" to where your devenv.exe is installed. Typically
  it's "C:\Program Files (x86)\Microsoft Visual Studio 14.0\Common7\IDE\devenv.exe"
- Set "Command line arguments" to: /rootsuffix Exp
- You can now set breakpoints if you like
- Press F5 to build and run with debugger

If all goes well, a new instance of Visual Studio will be launched in a special
mode where it uses the experimental hive instead of the normal configuration hive.
By default, when you build a VSIX project in Visual Studio, it auto-registers the
extension in the experimental hive, allowing you to test it. In the new Visual Studio
instance, open or create a C++ solution, and you should now see the Clang Format
entries in the Tool menu. You can test it out, and any breakpoints you set will be
hit where you can debug as usual.
