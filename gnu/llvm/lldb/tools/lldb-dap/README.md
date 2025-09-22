# LLDB DAP

## `lldb-dap` Configurations

The extension requires the `lldb-dap` (formerly `lldb-vscode`) binary. It is a
command line tool that implements the [Debug Adapter
Protocol](https://microsoft.github.io/debug-adapter-protocol/). It is used to power the Visual Studio Code extension but can also be used with other IDEs and editors that support DAP.
The protocol is easy to run remotely and also can allow other tools and IDEs to
get a full featured debugger with a well defined protocol.

## Launching & Attaching Configuration

Launching to attaching require you to create a [launch configuration](https://code.visualstudio.com/Docs/editor/debugging#_launch-configurations).
This file defines arguments that get passed to `lldb-dap` and the configuration settings control how the launch or attach happens.

### Launch Configuration Settings

When you launch a program with Visual Studio Code you will need to create a [launch.json](https://code.visualstudio.com/Docs/editor/debugging#_launch-configurations)
file that defines how your program will be run. The JSON configuration file can contain the following `lldb-dap` specific launch key/value pairs:

|parameter          |type|req |         |
|-------------------|----|:--:|---------|
|**name**           |string|Y| A configuration name that will be displayed in the IDE.
|**type**           |string|Y| Must be "lldb-dap".
|**request**        |string|Y| Must be "launch".
|**program**        |string|Y| Path to the executable to launch.
|**args**           |[string]|| An array of command line argument strings to be passed to the program being launched.
|**cwd**            |string| | The program working directory.
|**env**            |dictionary| | Environment variables to set when launching the program. The format of each environment variable string is "VAR=VALUE" for environment variables with values or just "VAR" for environment variables with no values.
|**stopOnEntry**    |boolean| | Whether to stop program immediately after launching.
|**initCommands**   |[string]| | LLDB commands executed upon debugger startup prior to creating the LLDB target. Commands and command output will be sent to the debugger console when they are executed.
|**preRunCommands** |[string]| | LLDB commands executed just before launching after the LLDB target has been created. Commands and command output will be sent to the debugger console when they are executed.
|**stopCommands**   |[string]| | LLDB commands executed just after each stop. Commands and command output will be sent to the debugger console when they are executed.
|**launchCommands** |[string]| | LLDB commands executed to launch the program. Commands and command output will be sent to the debugger console when they are executed.
|**exitCommands**   |[string]| | LLDB commands executed when the program exits. Commands and command output will be sent to the debugger console when they are executed.
|**terminateCommands** |[string]| | LLDB commands executed when the debugging session ends. Commands and command output will be sent to the debugger console when they are executed.
|**sourceMap**      |[string[2]]| | Specify an array of path re-mappings. Each element in the array must be a two element array containing a source and destination pathname.
|**debuggerRoot**   | string| |Specify a working directory to use when launching lldb-dap. If the debug information in your executable contains relative paths, this option can be used so that `lldb-dap` can find source files and object files that have relative paths.

### Attaching Settings

When attaching to a process using LLDB you can attach in a few ways

1. Attach to an existing process using the process ID
2. Attach to an existing process by name
3. Attach by name by waiting for the next instance of a process to launch

The JSON configuration file can contain the following `lldb-dap` specific launch key/value pairs:

|parameter          |type    |req |         |
|-------------------|--------|:--:|---------|
|**name**           |string  |Y| A configuration name that will be displayed in the IDE.
|**type**           |string  |Y| Must be "lldb-dap".
|**request**        |string  |Y| Must be "attach".
|**program**        |string  | | Path to the executable to attach to. This value is optional but can help to resolve breakpoints prior the attaching to the program.
|**pid**            |number  | | The process id of the process you wish to attach to. If **pid** is omitted, the debugger will attempt to attach to the program by finding a process whose file name matches the file name from **porgram**. Setting this value to `${command:pickMyProcess}` will allow interactive process selection in the IDE.
|**stopOnEntry**    |boolean| | Whether to stop program immediately after launching.
|**waitFor**        |boolean | | Wait for the process to launch.
|**initCommands**   |[string]| | LLDB commands executed upon debugger startup prior to creating the LLDB target. Commands and command output will be sent to the debugger console when they are executed.
|**preRunCommands** |[string]| | LLDB commands executed just before launching after the LLDB target has been created. Commands and command output will be sent to the debugger console when they are executed.
|**stopCommands**   |[string]| | LLDB commands executed just after each stop. Commands and command output will be sent to the debugger console when they are executed.
|**exitCommands**   |[string]| | LLDB commands executed when the program exits. Commands and command output will be sent to the debugger console when they are executed.
|**terminateCommands** |[string]| | LLDB commands executed when the debugging session ends. Commands and command output will be sent to the debugger console when they are executed.
|**attachCommands** |[string]| | LLDB commands that will be executed after **preRunCommands** which take place of the code that normally does the attach. The commands can create a new target and attach or launch it however desired. This allows custom launch and attach configurations. Core files can use `target create --core /path/to/core` to attach to core files.

### Example configurations

#### Launching

This will launch `/tmp/a.out` with arguments `one`, `two`, and `three` and
adds `FOO=1` and `bar` to the environment:

```javascript
{
  "type": "lldb-dap",
  "request": "launch",
  "name": "Debug",
  "program": "/tmp/a.out",
  "args": [ "one", "two", "three" ],
  "env": [ "FOO=1", "BAR" ],
}
```

#### Attach using PID

This will attach to a process `a.out` whose process ID is 123:

```javascript
{
  "type": "lldb-dap",
  "request": "attach",
  "name": "Attach to PID",
  "program": "/tmp/a.out",
  "pid": 123
}
```

#### Attach by Name

This will attach to an existing process whose base
name matches `a.out`. All we have to do is leave the `pid` value out of the
above configuration:

```javascript
{
  "name": "Attach to Name",
  "type": "lldb-dap",
  "request": "attach",
  "program": "/tmp/a.out",
}
```

If you want to ignore any existing a.out processes and wait for the next instance
to be launched you can add the "waitFor" key value pair:

```javascript
{
  "name": "Attach to Name (wait)",
  "type": "lldb-dap",
  "request": "attach",
  "program": "/tmp/a.out",
  "waitFor": true
}
```

This will work as long as the architecture, vendor and OS supports waiting
for processes. Currently MacOS is the only platform that supports this.

#### Loading a Core File

This loads the coredump file `/cores/123.core` associated with the program
`/tmp/a.out`:

```javascript
{
  "name": "Load coredump",
  "type": "lldb-dap",
  "request": "attach",
  "coreFile": "/cores/123.core",
  "program": "/tmp/a.out"
}
```

#### Connect to a Debug Server on the Current Machine

This connects to a debug server (e.g. `lldb-server`, `gdbserver`) on
the current machine, that is debugging the program `/tmp/a.out` and listening
locally on port `2345`.

```javascript
{
  "name": "Local Debug Server",
  "type": "lldb-dap",
  "request": "attach",
  "program": "/tmp/a.out",
  "attachCommands": ["gdb-remote 2345"],
}
```

#### Connect to a Debug Server on Another Machine

This connects to a debug server running on another machine with hostname
`hostnmame`. Which is debugging the program `/tmp/a.out` and listening on
port `5678` of that other machine.

```javascript
{
  "name": "Remote Debug Server",
  "type": "lldb-dap",
  "request": "attach",
  "program": "/tmp/a.out",
  "attachCommands": ["gdb-remote hostname:5678"],
}
```

## Custom debugger commands

The `lldb-dap` tool includes additional custom commands to support the Debug
Adapter Protocol features.

### startDebugging

Using the command `lldb-dap startDebugging` it is possible to trigger a
reverse request to the client requesting a child debug session with the
specified configuration. For example, this can be used to attached to forked or
spawned processes. For more information see
[Reverse Requests StartDebugging](https://microsoft.github.io/debug-adapter-protocol/specification#Reverse_Requests_StartDebugging).

The custom command has the following format:

```
lldb-dap startDebugging <launch|attach> <configuration>
```

This will launch a server and then request a child debug session for a client.

```javascript
{
  "program": "server",
  "postRunCommand": [
    "lldb-dap startDebugging launch '{\"program\":\"client\"}'"
  ]
}
```

### repl-mode

Inspect or adjust the behavior of lldb-dap repl evaluation requests. The
supported modes are `variable`, `command` and `auto`.

- `variable` - Variable mode expressions are evaluated in the context of the
   current frame. Use a `\`` prefix on the command to run an lldb command.
- `command` - Command mode expressions are evaluated as lldb commands, as a
   result, values printed by lldb are always stringified representations of the
   expression output.
- `auto` - Auto mode will attempt to infer if the expression represents an lldb
   command or a variable expression. A heuristic is used to infer if the input
   represents a variable or a command. Use a `\`` prefix to ensure an expression
   is evaluated as a command.

The initial repl-mode can be configured with the cli flag `--repl-mode=<mode>`
and may also be adjusted at runtime using the lldb command
`lldb-dap repl-mode <mode>`.
