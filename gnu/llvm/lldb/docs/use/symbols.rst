Symbols on macOS
================

On macOS, debug symbols are often in stand alone bundles called **dSYM** files.
These are bundles that contain DWARF debug information and other resources
related to builds and debug info.

The DebugSymbols.framework framework helps locate dSYM files when given a UUID.
It can locate the symbols using a variety of methods:

-  Spotlight
-  Explicit search paths
-  Implicit search paths
-  File mapped UUID paths
-  Running one or more shell scripts

DebugSymbols.framework also has global defaults that can be modified to allow
all of the debug tools (lldb, gdb, sample, CoreSymbolication.framework) to
easily find important debug symbols. The domain for the DebugSymbols.framework
defaults is **com.apple.DebugSymbols**, and the defaults can be read, written
or modified using the **defaults** shell command:

::

   % defaults read com.apple.DebugSymbols
   % defaults write com.apple.DebugSymbols KEY ...
   % defaults delete com.apple.DebugSymbols KEY

The following is a list of the defaults key value setting pairs that can
be used to enhance symbol location:

**DBGFileMappedPaths**

This default can be specified as a single string, or an array of
strings. Each string represents a directory that contains file mapped
UUID values that point to dSYM files. See the "File Mapped UUID
Directories" section below for more details. Whenever
DebugSymbols.framework is asked to lookup a dSYM file, it will first
look in any file mapped UUID directories for a quick match.

::

   % defaults write com.apple.DebugSymbols DBGFileMappedPaths -string /path/to/uuidmap1
   % defaults write com.apple.DebugSymbols DBGFileMappedPaths -array /path/to/uuidmap1
       /path/to/uuidmap2

**DBGShellCommands**

This default can be specified as a single string, or an array of
strings. Specifies a shell script that will get run in order to find the
dSYM. The shell script will be run given a single UUID value as the
shell command arguments and the shell command is expected to return a
property list. See the property list format defined below.

::

   % defaults write com.apple.DebugSymbols DBGShellCommands -string /path/to/script1
   % defaults write com.apple.DebugSymbols DBGShellCommands -array /path/to/script1
       /path/to/script2

**DBGSpotlightPaths**

Specifies the directories to limit spotlight searches to as a string or
array of strings. When any other defaults are supplied to
**com.apple.DebugSymbols**, spotlight searches will be disabled unless
this default is set to an empty array:

::

   # Specify an empty array to keep Spotlight searches enabled in all locations
   % defaults write com.apple.DebugSymbols DBGSpotlightPaths -array

   # Specify an array of paths to limit spotlight searches to certain directories
   % defaults write com.apple.DebugSymbols DBGSpotlightPaths -array /path/dir1 /path/dir2

Shell Script Property List Format
---------------------------------

Shell scripts that are specified with the **DBGShellCommands** defaults key
will be run in the order in which they are specified until a match is found.
The shell script will be invoked with a single UUID string value like
"23516BE4-29BE-350C-91C9-F36E7999F0F1". The shell script must respond with a
property list being written to STDOUT. The property list returned must contain
UUID string values as the root key values, with a dictionary for each UUID. The
dictionaries can contain one or more of the following keys:

+-----------------------------------+-----------------------------------+
| Key                               | Description                       |
+-----------------------------------+-----------------------------------+
| **DBGArchitecture**               | A textual architecture or target  |
|                                   | triple like "x86_64", "i386", or  |
|                                   | "x86_64-apple-macosx".            |
+-----------------------------------+-----------------------------------+
| **DBGBuildSourcePath**            | A path prefix that was used when  |
|                                   | building the dSYM file. The debug |
|                                   | information will contain paths    |
|                                   | with this prefix.                 |
+-----------------------------------+-----------------------------------+
| **DBGSourcePath**                 | A path prefix for where the       |
|                                   | sources exist after the build has |
|                                   | completed. Often when building    |
|                                   | projects, build machines will     |
|                                   | host the sources in a temporary   |
|                                   | directory while building, then    |
|                                   | move the sources to another       |
|                                   | location for archiving. If the    |
|                                   | paths in the debug info don't     |
|                                   | match where the sources are       |
|                                   | currently hosted, then specifying |
|                                   | this path along with the          |
|                                   | **DBGBuildSourcePath** will help  |
|                                   | the developer tools always show   |
|                                   | you sources when debugging or     |
|                                   | symbolicating.                    |
+-----------------------------------+-----------------------------------+
| **DBGDSYMPath**                   | A path to the dSYM mach-o file    |
|                                   | inside the dSYM bundle.           |
+-----------------------------------+-----------------------------------+
| **DBGSymbolRichExecutable**       | A path to the symbol rich         |
|                                   | executable. Binaries are often    |
|                                   | stripped after being built and    |
|                                   | packaged into a release. If your  |
|                                   | build systems saves an unstripped |
|                                   | executable a path to this         |
|                                   | executable can be provided.       |
+-----------------------------------+-----------------------------------+
| **DBGError**                      | If a binary can not be located    |
|                                   | for the supplied UUID, a user     |
|                                   | readable error can be returned.   |
+-----------------------------------+-----------------------------------+

Below is a sample shell script output for a binary that contains two
architectures:

::

   <?xml version="1.0" encoding="UTF-8"?>
   <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
   <plist version="1.0">
   <dict>
       <key>23516BE4-29BE-350C-91C9-F36E7999F0F1</key>
       <dict>
           <key>DBGArchitecture</key>
           <string>i386</string>
           <key>DBGBuildSourcePath</key>
           <string>/path/to/build/sources</string>
           <key>DBGSourcePath</key>
           <string>/path/to/actual/sources</string>
           <key>DBGDSYMPath</key>
           <string>/path/to/foo.dSYM/Contents/Resources/DWARF/foo</string>
           <key>DBGSymbolRichExecutable</key>
           <string>/path/to/unstripped/executable</string>
       </dict>
       <key>A40597AA-5529-3337-8C09-D8A014EB1578</key>
       <dict>
           <key>DBGArchitecture</key>
           <string>x86_64</string>
           <key>DBGBuildSourcePath</key>
           <string>/path/to/build/sources</string>
           <key>DBGSourcePath</key>
           <string>/path/to/actual/sources</string>
           <key>DBGDSYMPath</key>
           <string>/path/to/foo.dSYM/Contents/Resources/DWARF/foo</string>
           <key>DBGSymbolRichExecutable</key>
           <string>/path/to/unstripped/executable</string>
       </dict>
   </dict>
   </plist>

There is no timeout imposed on a shell script when is it asked to locate a dSYM
file, so be careful to not make a shell script that has high latency or takes a
long time to download unless this is really what you want. This can slow down
debug sessions in LLDB and GDB, symbolication with CoreSymbolication or Report
Crash, with no visible feedback to the user. You can quickly return a plist
with a single **DBGError** key that indicates a timeout has been reached. You
might also want to exec new processes to do the downloads so that if you return
an error that indicates a timeout, your download can still proceed after your
shell script has exited so subsequent debug sessions can use the cached files.
It is also important to track when a current download is in progress in case
you get multiple requests for the same UUID so that you don't end up
downloading the same file simultaneously. Also you will want to verify the
download was successful and then and only then place the file into the cache
for tools that will cache files locally.

Embedding UUID property lists inside the dSYM bundles
-----------------------------------------------------

Since dSYM files are bundles, you can also place UUID info plists files inside
your dSYM bundles in the **Contents/Resources** directory. One of the main
reasons to create the UUID plists inside the dSYM bundles is that it will help
LLDB and other developer tools show you source. LLDB currently knows how to
check for these plist files so it can automatically remap the source location
information in the debug info.

If we take the two UUID values from the returns plist above, we can split them
out and save then in the dSYM bundle:

::

   % ls /path/to/foo.dSYM/Contents/Resources
   23516BE4-29BE-350C-91C9-F36E7999F0F1.plist
   A40597AA-5529-3337-8C09-D8A014EB1578.plist

   % cat /path/to/foo.dSYM/Contents/Resources/23516BE4-29BE-350C-91C9-F36E7999F0F1.plist
   <?xml version="1.0" encoding="UTF-8"?>
   <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
   <plist version="1.0">
   <dict>
      <key>DBGArchitecture</key>
      <string>i386</string>
      <key>DBGBuildSourcePath</key>
      <string>/path/to/build/sources</string>
      <key>DBGSourcePath</key>
      <string>/path/to/actual/sources</string>
      <key>DBGDSYMPath</key>
      <string>/path/to/foo.dSYM/Contents/Resources/DWARF/foo</string>
      <key>DBGSymbolRichExecutable</key>
      <string>/path/to/unstripped/executable</string>
      <key>DBGVersion</key>
      <string>3</string>
      <key>DBGSourcePathRemapping</key>
      <dict>
          <key>/path/to/build/time/src/location1</key>
          <string>/path/to/debug/time/src/location</string>
          <key>/path/to/build/time/src/location2</key>
          <string>/path/to/debug/time/src/location</string>
      </dict>
      <key>DBGSymbolRichExecutable</key>
      <string>/path/to/unstripped/executable</string>
   </dict>
   </plist>

Note that the output is very close to what is needed by shell script output, so
making the results of your shell script will be very easy to create by
combining two plists into a single one where you take the UUID and use it a
string key, and the value is the contents of the plist.

LLDB will read the following entries from the per-UUID plist file in the dSYM
bundle: **DBGSymbolRichExecutable**, **DBGBuildSourcePath** and
**DBGSourcePath**, and **DBGSourcePathRemapping** if **DBGVersion** is 3 or
higher. **DBGBuildSourcePath** and **DBGSourcePath** are for remapping a single
file path. For instance, the files may be in /BuildDir/SheetApp/SheetApp-37
when built, but they are in /SourceDir/SheetApp/SheetApp-37 at debug time,
those two paths could be listed in those keys. If there are multiple source
path remappings, the **DBGSourcePathRemapping** dictionary can be used, where
an arbitrary number of entries may be present. **DBGVersion** should be 3 or
**DBGSourcePathRemapping** will not be read. If both **DBGSourcePathRemapping**
AND **DBGBuildSourcePath**/**DBGSourcePath** are present in the plist, the
**DBGSourcePathRemapping** entries will be used for path remapping first. This
may allow for more specific remappings in the **DBGSourcePathRemapping**
dictionary and a less specific remapping in the
**DBGBuildSourcePath**/**DBGSourcePath** pair as a last resort.

File Mapped UUID Directories
----------------------------

File Mapped directories can be used for efficient dSYM file lookups for local
or remote dSYM files. The UUID is broken up by splitting the first 20 hex
digits into 4 character chunks, and a directory is created for each chunk, and
each subsequent directory is created inside the previous one. A symlink is then
created whose name is the last 12 hex digits in the deepest directory. The
symlinks value is a full path to the mach-o files inside the dSYM bundle which
contains the DWARF. Whenever DebugSymbols.framework is asked to lookup a dSYM
file, it will first look in any file mapped UUID directories for a quick match
if the defaults are appropriately set.

For example, if we take the sample UUID plist information from above, we can
create a File Mapped UUID directory cache in
**~/Library/SymbolCache/dsyms/uuids**. We can easily see how things are laid
out:

::

   % find ~/Library/SymbolCache/dsyms/uuids -type l
   ~/Library/SymbolCache/dsyms/uuids/2351/6BE4/29BE/350C/91C9/F36E7999F0F1
   ~/Library/SymbolCache/dsyms/uuids/A405/97AA/5529/3337/8C09/D8A014EB1578

The last entries in these file mapped directories are symlinks to the actual
dsym mach file in the dsym bundle:

::

   % ls -lAF ~/Library/SymbolCache/dsyms/uuids/2351/6BE4/29BE/350C/91C9/F36E7999F0F1
   ~/Library/SymbolCache/dsyms/uuids/2351/6BE4/29BE/350C/91C9/F36E7999F0F1@ -> ../../../../../../dsyms/foo.dSYM/Contents/Resources/DWARF/foo

Then you can also tell DebugSymbols to check this UUID file map cache using:

::

   % defaults write com.apple.DebugSymbols DBGFileMappedPaths ~/Library/SymbolCache/dsyms/uuids

dSYM Locating Shell Script Tips
-------------------------------

One possible implementation of a dSYM finding shell script is to have the
script download and cache files locally in a known location. Then create a UUID
map for each UUID value that was found in a local UUID File Map cache so the
next query for the dSYM file will be able to use the cached version. So the
shell script is used to initially download and cache the file, and subsequent
accesses will use the cache and avoid calling the shell script.

Then the defaults for DebugSymbols.framework will entail enabling your shell
script, enabling the file mapped path setting so that already downloaded dSYMS
fill quickly be found without needing to run the shell script every time, and
also leaving spotlight enabled so that other normal dSYM files are still found:

::

   % defaults write com.apple.DebugSymbols DBGShellCommands /path/to/shellscript
   % defaults write com.apple.DebugSymbols DBGFileMappedPaths ~/Library/SymbolCache/dsyms/uuids
   % defaults write com.apple.DebugSymbols DBGSpotlightPaths -array

Hopefully this helps explain how DebugSymbols.framework can help any company
implement a smart symbol finding and caching with minimal overhead.
