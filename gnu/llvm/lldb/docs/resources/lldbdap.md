# LLDB-DAP

The `lldb-dap` tool (formerly `lldb-vscode`) is a command line tool that
implements the [Debug Adapter
Protocol](https://microsoft.github.io/debug-adapter-protocol/). It can be
installed as an extension for Visual Studio Code and other IDEs supporting DAP.
The protocol is easy to run remotely and also can allow other tools and IDEs to
get a full featured debugger with a well defined protocol.

## Local Installation for Visual Studio Code

Installing the plug-in is very straightforward and involves just a few steps.

### Pre-requisites

- Install a modern version of node (e.g. `v20.0.0`).
- On VS Code, execute the command `Install 'code' command in PATH`. You need to
  do it only once. This enables the command `code` in the PATH.

### Packaging and installation

```bash
cd /path/to/lldb/tools/lldb-dap
npm install
npm run package # This also compiles the extension.
npm run vscode-install
```

On VS Code, set the setting `lldb-dap.executable-path` to the path of your local
build of `lldb-dap`.

And then you are ready!

### Updating the extension

*Note: It's not necessary to update the extension if there has been changes
to  `lldb-dap`. The extension needs to be updated only if the TypesScript code
has changed.*

Updating the extension is pretty much the same process as installing it from
scratch. However, VS Code expects the version number of the upgraded extension
to be greater than the previous one, otherwise the installation step might have
no effect.

```bash
# Bump version in package.json
cd /path/to/lldb/tools/lldb-dap
npm install
npm run package
npm run vscode-install
```

Another way upgrade without bumping the extension version is to first uninstall
the extension, then reload VS Code, and then install it again. This is
an unfortunate limitation of the editor.

```bash
cd /path/to/lldb/tools/lldb-dap
npm run vscode-uninstall
# Then reload VS Code: reopen the IDE or execute the `Developer: Reload Window`
# command.
npm run package
npm run vscode-install
```

### Deploying for Visual Studio Code

The easiest way to deploy the extension for execution on other machines requires
copying `lldb-dap` and its dependencies into a`./bin` subfolder and then create a
standalone VSIX package.

```bash
cd /path/to/lldb/tools/lldb-dap
mkdir -p ./bin
cp /path/to/a/built/lldb-dap ./bin/
cp /path/to/a/built/liblldb.so ./bin/
npm run package
```

This will produce the file `./out/lldb-dap.vsix` that can be distributed. In
this type of installation, users don't need to manually set the path to
`lldb-dap`. The extension will automatically look for it in the `./bin`
subfolder.

*Note: It's not possible to use symlinks to `lldb-dap`, as the packaging tool
forcefully performs a deep copy of all symlinks.*

*Note: It's possible to use this kind flow for local installations, but it's
not recommended because updating `lldb-dap` requires rebuilding the extension.*

## Formatting the Typescript code

This is also very simple, just run:

```bash
npm run format
```
