include "../../../../include/lldb/Core/PropertiesBase.td"

let Definition = "dynamicloaderdarwinkernel" in {
  def LoadKexts: Property<"load-kexts", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"Automatically loads kext images when attaching to a kernel.">;
  def ScanType: Property<"scan-type", "Enum">,
    Global,
    DefaultEnumValue<"eKASLRScanNearPC">,
    EnumValues<"OptionEnumValues(g_kaslr_kernel_scan_enum_values)">,
    Desc<"Control how many reads lldb will make while searching for a Darwin kernel on attach.">;
}
