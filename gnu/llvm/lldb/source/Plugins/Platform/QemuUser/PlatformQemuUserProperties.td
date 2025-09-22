include "../../../../include/lldb/Core/PropertiesBase.td"

let Definition = "platformqemuuser" in {
  def Architecture: Property<"architecture", "String">,
    Global,
    DefaultStringValue<"">,
    Desc<"Architecture to emulate.">;
  def EmulatorPath: Property<"emulator-path", "FileSpec">,
    Global,
    DefaultStringValue<"">,
    Desc<"Path to the emulator binary. If the path does not contain a directory separator, the filename is looked up in the PATH environment variable. If empty, the filename is derived from the architecture setting.">;
  def EmulatorArgs: Property<"emulator-args", "Args">,
    Global,
    DefaultStringValue<"">,
    Desc<"Extra arguments to pass to the emulator.">;
  def EmulatorEnvVars: Property<"emulator-env-vars", "Dictionary">,
    Global,
    ElementType<"String">,
    Desc<"Extra variables to add to the emulator environment.">;
  def TargetEnvVars: Property<"target-env-vars", "Dictionary">,
    Global,
    ElementType<"String">,
    Desc<"Extra variables to add to emulated target environment.">;
}
