from lit.llvm import config

llvm_config = None


def initialize(lit_config, test_config):
    global llvm_config

    llvm_config = config.LLVMConfig(lit_config, test_config)
