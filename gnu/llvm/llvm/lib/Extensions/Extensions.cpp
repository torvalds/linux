#include "llvm/Passes/PassPlugin.h"
#define HANDLE_EXTENSION(Ext)                                                  \
		llvm::PassPluginLibraryInfo get##Ext##PluginInfo();
#include "llvm/Support/Extension.def"


namespace llvm {
	namespace details {
		void extensions_anchor() {
#define HANDLE_EXTENSION(Ext)                                                  \
			get##Ext##PluginInfo();
#include "llvm/Support/Extension.def"
		}
	}
}
