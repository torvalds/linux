include "../../../../include/lldb/Core/PropertiesBase.td"

let Definition = "platformdarwinkernel" in {
  def KextDirectories: Property<"kext-directories", "FileSpecList">,
    DefaultStringValue<"">,
    Desc<"Directories/KDKs to search for kexts in when starting a kernel debug session.">;
}

let Definition = "platformdarwin" in {
  def IgnoredExceptions: Property<"ignored-exceptions", "String">,
    DefaultStringValue<"">,
    Desc<"List the mach exceptions to ignore, separated by '|' "
         "(e.g. 'EXC_BAD_ACCESS|EXC_BAD_INSTRUCTION'). "
         "lldb will instead stop on the BSD signal the exception was converted "
         "into, if there is one.">;
}
