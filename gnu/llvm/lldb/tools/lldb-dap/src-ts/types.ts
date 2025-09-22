import * as vscode from "vscode";

/**
 * Callback used to generate the actual command to be executed to launch the lldb-dap binary.
 *
 * @param session - The information of the debug session to be launched.
 *
 * @param packageJSONExecutable - An optional {@link vscode.DebugAdapterExecutable executable} for
 * lldb-dap if specified in the package.json file.
 */
export type LLDBDapCreateDAPExecutableCommand = (
  session: vscode.DebugSession,
  packageJSONExecutable: vscode.DebugAdapterExecutable | undefined,
) => Promise<vscode.DebugAdapterExecutable | undefined>;

/**
 * The options that this extension accepts.
 */
export interface LLDBDapOptions {
  createDapExecutableCommand: LLDBDapCreateDAPExecutableCommand;
  // The name of the debugger type as specified in the package.json file.
  debuggerType: string;
}
