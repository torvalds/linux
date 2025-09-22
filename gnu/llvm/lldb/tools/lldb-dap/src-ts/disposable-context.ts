import * as vscode from "vscode";

/**
 * This class provides a simple wrapper around vscode.Disposable that allows
 * for registering additional disposables.
 */
export class DisposableContext implements vscode.Disposable {
  private _disposables: vscode.Disposable[] = [];

  constructor() {}

  public dispose() {
    for (const disposable of this._disposables) {
      disposable.dispose();
    }
    this._disposables = [];
  }

  /**
   * Push an additional disposable to the context.
   *
   * @param disposable The disposable to register.
   */
  public pushSubscription(disposable: vscode.Disposable) {
    this._disposables.push(disposable);
  }
}
