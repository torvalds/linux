include "../../../../include/lldb/Core/PropertiesBase.td"

let Definition = "jitloadergdb" in {
  def Enable: Property<"enable", "Enum">,
    Global,
    DefaultEnumValue<"eEnableJITLoaderGDBDefault">,
    EnumValues<"OptionEnumValues(g_enable_jit_loader_gdb_enumerators)">,
    Desc<"Enable GDB's JIT compilation interface (default: enabled on all platforms except macOS)">;
}
