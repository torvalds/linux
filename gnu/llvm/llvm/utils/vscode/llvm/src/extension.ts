import * as vscode from 'vscode';
import { LITTaskProvider } from './litTaskProvider';

let litTaskProvider: vscode.Disposable | undefined;
let customTaskProvider: vscode.Disposable | undefined;

export function activate(_context: vscode.ExtensionContext): void {
	litTaskProvider = vscode.tasks.registerTaskProvider(LITTaskProvider.LITType, new LITTaskProvider());
}

export function deactivate(): void {
	if (litTaskProvider) {
		litTaskProvider.dispose();
	}
}