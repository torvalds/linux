// Base class for all options.
class Property<string name, string type> {
  string Name = name;
  string Type = type;
  string Definition;
}

// Sets the description for the property that should be displayed to the user.
class Desc<string description> {
  string Description = description;
}

// Marks the property as global.
class Global {
  bit Global = 1;
}

class DefaultTrue {
  int DefaultUnsignedValue = 1;
  bit HasDefaultUnsignedValue = 1;
  bit HasDefaultBooleanValue = 1;
}

class DefaultFalse {
  int DefaultUnsignedValue = 0;
  bit HasDefaultUnsignedValue = 1;
  bit HasDefaultBooleanValue = 1;
}

// Gives the property a default string value.
class DefaultStringValue<string value> {
  string DefaultStringValue = value;
  bit HasDefaultStringValue = 1;
}

// Gives the property a default enum value.
class DefaultEnumValue<string value> {
  string DefaultEnumValue = value;
  bit HasDefaultEnumValue = 1;
}

// Gives the property a default string value.
class DefaultUnsignedValue<int value> {
  int DefaultUnsignedValue = value;
  bit HasDefaultUnsignedValue = 1;
}

// Gives the property enum values.
class EnumValues<string enum> {
  string EnumValues = enum;
}

// Determines the element type for arrays and dictionaries.
class ElementType<string value> {
  string ElementType = value;
  bit HasElementType = 1;
}
