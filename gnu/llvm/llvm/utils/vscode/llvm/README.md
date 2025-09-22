# VS Code Extension For LLVM Dev

## Features
 - LLVM IR files (.ll) syntax highlighting.
    (manually translated from `llvm/utils/vim/syntax/llvm.vim`)
 - TableGen files (.td) syntax highlighting.
    (translated from `llvm/utils/textmate`)
 - PatternMatchers for LIT test output.
    (`$llvm-lit`, `$llvm-filecheck`)
 - Tasks to run LIT on current selected file.
    (`Terminal` -> `Run Task` -> `llvm-lit`)

## Installation

```sh
sudo apt-get install nodejs-dev node-gyp npm
sudo npm install -g typescript npx vsce
```

### Install From Source
```sh
cd <extensions-installation-folder>
cp -r llvm/utils/vscode/llvm .
cd llvm
npm install
npm run vscode:prepublish
```
`<extensions-installation-folder>` is OS dependent.

Please refer to https://code.visualstudio.com/docs/editor/extension-gallery#_where-are-extensions-installed

### Install From Package (.vsix)

First package the extension according to
https://code.visualstudio.com/api/working-with-extensions/publishing-extension#usage.

Then install the package according to
https://code.visualstudio.com/docs/editor/extension-gallery#_install-from-a-vsix.

## Setup

Set `cmake.buildDirectory` to your build directory.

https://code.visualstudio.com/docs/getstarted/settings

https://vector-of-bool.github.io/docs/vscode-cmake-tools/settings.html#cmake-builddirectory
