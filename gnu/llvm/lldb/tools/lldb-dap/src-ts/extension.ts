import * as vscode from "vscode";
import { LLDBDapOptions } from "./types";
import { DisposableContext } from "./disposable-context";
import { LLDBDapDescriptorFactory } from "./debug-adapter-factory";

/**
 * This creates the configurations for this project if used as a standalone
 * extension.
 */
function createDefaultLLDBDapOptions(): LLDBDapOptions {
  return {
    debuggerType: "lldb-dap",
    async createDapExecutableCommand(
      session: vscode.DebugSession,
      packageJSONExecutable: vscode.DebugAdapterExecutable | undefined,
    ): Promise<vscode.DebugAdapterExecutable | undefined> {
      const path = vscode.workspace
        .getConfiguration("lldb-dap", session.workspaceFolder)
        .get<string>("executable-path");
      if (path) {
        return new vscode.DebugAdapterExecutable(path, []);
      }
      return packageJSONExecutable;
    },
  };
}

/**
 * This class represents the extension and manages its life cycle. Other extensions
 * using it as as library should use this class as the main entry point.
 */
export class LLDBDapExtension extends DisposableContext {
  private lldbDapOptions: LLDBDapOptions;

  constructor(lldbDapOptions: LLDBDapOptions) {
    super();
    this.lldbDapOptions = lldbDapOptions;

    this.pushSubscription(
      vscode.debug.registerDebugAdapterDescriptorFactory(
        this.lldbDapOptions.debuggerType,
        new LLDBDapDescriptorFactory(this.lldbDapOptions),
      ),
    );
  }
}

/**
 * This is the entry point when initialized by VS Code.
 */
export function activate(context: vscode.ExtensionContext) {
  context.subscriptions.push(
    new LLDBDapExtension(createDefaultLLDBDapOptions()),
  );
}
