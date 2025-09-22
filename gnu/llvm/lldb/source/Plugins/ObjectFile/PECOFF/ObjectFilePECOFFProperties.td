include "../../../../include/lldb/Core/PropertiesBase.td"

let Definition = "objectfilepecoff" in {
  def ABI: Property<"abi", "Enum">,
    Global,
    DefaultEnumValue<"llvm::Triple::UnknownEnvironment">,
    EnumValues<"OptionEnumValues(g_abi_enums)">,
    Desc<"ABI to use when loading a PE/COFF module. This configures the C++ ABI used, which affects things like the handling of class layout. Accepted values are: `msvc` for the MSVC ABI, `gnu` for the MinGW / Itanium ABI, and `default` to follow the default target if it is a Windows triple or use the MSVC ABI by default.">;
  def ModuleABIMap: Property<"module-abi", "Dictionary">,
    Global,
    ElementType<"Enum">,
    EnumValues<"OptionEnumValues(g_abi_enums)">,
    Desc<"A mapping of ABI override to use for specific modules. The module name is matched by its file name with extension. These versions are checked in sequence: exact, lowercase, exact with '.debug' suffix stripped, lowercase with '.debug' suffix stripped. Accepted values are: `msvc` for the MSVC ABI, `gnu` for the MinGW / Itanium ABI, and `default` to follow the default target if it is a Windows triple or use the MSVC ABI by default.">;
}
