import * as vscode from 'vscode';
import * as fs from 'fs';

interface LITTaskDefinition extends vscode.TaskDefinition {
	/**
	 * The task name
	 */
  task: string;
}

export class LITTaskProvider implements vscode.TaskProvider {
  static LITType: string = 'llvm-lit';
  private cmd: string;
  private args: string[] = [];
  private litPromise: Thenable<vscode.Task[]> | undefined = undefined;

  constructor() {
    const isWindows = process.platform === 'win32';
    if (isWindows) {
      this.cmd = "py"
      this.args = ["-3", "${config:cmake.buildDirectory}\\bin\\llvm-lit.py", "-vv"]
    } else {
      this.cmd = "python3"
      this.args = ["${config:cmake.buildDirectory}/bin/llvm-lit", "-vv"]
    }
  }

  public provideTasks(): Thenable<vscode.Task[]> | undefined {
    if (!this.litPromise) {
      this.litPromise = this.getLITTasks();
    }
    return this.litPromise;
  }

  public resolveTask(_task: vscode.Task): vscode.Task | undefined {
    const task = _task.definition.task;
    if (task) {
      let args: string[] = this.args;
      const definition: LITTaskDefinition = <any>_task.definition;
      if (definition.task === 'llvm-lit file') {
        args.push("${file}")
      } else if (definition.task === 'llvm-lit directory') {
        args.push("${fileDirname}")
      }

      return new vscode.Task(
        definition,
        definition.task,
        'llvm',
        new vscode.ShellExecution(this.cmd, args),
        ["$llvm-lit", "$llvm-filecheck"]
      );
    }
    return undefined;
  }

  private async getLITTasks(): Promise<vscode.Task[]> {
    let result: vscode.Task[] = [];

    let bld_dir: string | undefined = vscode.workspace.getConfiguration().get("cmake.buildDirectory");
    if (bld_dir == undefined || !fs.existsSync(bld_dir)) {
      return result;
    }

    let taskName = 'llvm-lit file';
    result.push(new vscode.Task({ type: 'llvm-lit', task: taskName },
      taskName, 'llvm',
      new vscode.ShellExecution(this.cmd, this.args.concat(["${file}"])),
      ["$llvm-lit", "$llvm-filecheck"]));

    taskName = 'llvm-lit directory';
    result.push(new vscode.Task({ type: 'llvm-lit', task: taskName },
      taskName, 'llvm',
      new vscode.ShellExecution(this.cmd, this.args.concat(["${fileDirname}"])),
      ["$llvm-lit", "$llvm-filecheck"])
    );
    return result;
  }
}
