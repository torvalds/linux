include "../../../../include/lldb/Core/PropertiesBase.td"

let Definition = "darwinlog" in {
  def EnableOnStartup: Property<"enable-on-startup", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"Enable Darwin os_log collection when debugged process is launched or attached.">;
  def AutoEnableOptions: Property<"auto-enable-options", "String">,
    Global,
    DefaultStringValue<"">,
    Desc<"Specify the options to 'plugin structured-data darwin-log enable' that should be applied when automatically enabling logging on startup/attach.">;
}
