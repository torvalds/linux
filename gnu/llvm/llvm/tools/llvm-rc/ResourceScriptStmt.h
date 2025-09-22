//===-- ResourceScriptStmt.h ------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This lists all the resource and statement types occurring in RC scripts.
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMRC_RESOURCESCRIPTSTMT_H
#define LLVM_TOOLS_LLVMRC_RESOURCESCRIPTSTMT_H

#include "ResourceScriptToken.h"
#include "ResourceVisitor.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/StringMap.h"

namespace llvm {
namespace rc {

// Integer wrapper that also holds information whether the user declared
// the integer to be long (by appending L to the end of the integer) or not.
// It allows to be implicitly cast from and to uint32_t in order
// to be compatible with the parts of code that don't care about the integers
// being marked long.
class RCInt {
  uint32_t Val;
  bool Long;

public:
  RCInt(const RCToken &Token)
      : Val(Token.intValue()), Long(Token.isLongInt()) {}
  RCInt(uint32_t Value) : Val(Value), Long(false) {}
  RCInt(uint32_t Value, bool IsLong) : Val(Value), Long(IsLong) {}
  operator uint32_t() const { return Val; }
  bool isLong() const { return Long; }

  RCInt &operator+=(const RCInt &Rhs) {
    std::tie(Val, Long) = std::make_pair(Val + Rhs.Val, Long | Rhs.Long);
    return *this;
  }

  RCInt &operator-=(const RCInt &Rhs) {
    std::tie(Val, Long) = std::make_pair(Val - Rhs.Val, Long | Rhs.Long);
    return *this;
  }

  RCInt &operator|=(const RCInt &Rhs) {
    std::tie(Val, Long) = std::make_pair(Val | Rhs.Val, Long | Rhs.Long);
    return *this;
  }

  RCInt &operator&=(const RCInt &Rhs) {
    std::tie(Val, Long) = std::make_pair(Val & Rhs.Val, Long | Rhs.Long);
    return *this;
  }

  RCInt operator-() const { return {-Val, Long}; }
  RCInt operator~() const { return {~Val, Long}; }

  friend raw_ostream &operator<<(raw_ostream &OS, const RCInt &Int) {
    return OS << Int.Val << (Int.Long ? "L" : "");
  }
};

class IntWithNotMask {
private:
  RCInt Value;
  int32_t NotMask;

public:
  IntWithNotMask() : IntWithNotMask(RCInt(0)) {}
  IntWithNotMask(RCInt Value, int32_t NotMask = 0) : Value(Value), NotMask(NotMask) {}

  RCInt getValue() const {
    return Value;
  }

  uint32_t getNotMask() const {
    return NotMask;
  }

  IntWithNotMask &operator+=(const IntWithNotMask &Rhs) {
    Value &= ~Rhs.NotMask;
    Value += Rhs.Value;
    NotMask |= Rhs.NotMask;
    return *this;
  }

  IntWithNotMask &operator-=(const IntWithNotMask &Rhs) {
    Value &= ~Rhs.NotMask;
    Value -= Rhs.Value;
    NotMask |= Rhs.NotMask;
    return *this;
  }

  IntWithNotMask &operator|=(const IntWithNotMask &Rhs) {
    Value &= ~Rhs.NotMask;
    Value |= Rhs.Value;
    NotMask |= Rhs.NotMask;
    return *this;
  }

  IntWithNotMask &operator&=(const IntWithNotMask &Rhs) {
    Value &= ~Rhs.NotMask;
    Value &= Rhs.Value;
    NotMask |= Rhs.NotMask;
    return *this;
  }

  IntWithNotMask operator-() const { return {-Value, NotMask}; }
  IntWithNotMask operator~() const { return {~Value, 0}; }

  friend raw_ostream &operator<<(raw_ostream &OS, const IntWithNotMask &Int) {
    return OS << Int.Value;
  }
};

// A class holding a name - either an integer or a reference to the string.
class IntOrString {
private:
  union Data {
    RCInt Int;
    StringRef String;
    Data(RCInt Value) : Int(Value) {}
    Data(const StringRef Value) : String(Value) {}
    Data(const RCToken &Token) {
      if (Token.kind() == RCToken::Kind::Int)
        Int = RCInt(Token);
      else
        String = Token.value();
    }
  } Data;
  bool IsInt;

public:
  IntOrString() : IntOrString(RCInt(0)) {}
  IntOrString(uint32_t Value) : Data(Value), IsInt(true) {}
  IntOrString(RCInt Value) : Data(Value), IsInt(true) {}
  IntOrString(StringRef Value) : Data(Value), IsInt(false) {}
  IntOrString(const RCToken &Token)
      : Data(Token), IsInt(Token.kind() == RCToken::Kind::Int) {}

  bool equalsLower(const char *Str) const {
    return !IsInt && Data.String.equals_insensitive(Str);
  }

  bool isInt() const { return IsInt; }

  RCInt getInt() const {
    assert(IsInt);
    return Data.Int;
  }

  const StringRef &getString() const {
    assert(!IsInt);
    return Data.String;
  }

  operator Twine() const {
    return isInt() ? Twine(getInt()) : Twine(getString());
  }

  friend raw_ostream &operator<<(raw_ostream &, const IntOrString &);
};

enum ResourceKind {
  // These resource kinds have corresponding .res resource type IDs
  // (TYPE in RESOURCEHEADER structure). The numeric value assigned to each
  // kind is equal to this type ID.
  RkNull = 0,
  RkSingleCursor = 1,
  RkBitmap = 2,
  RkSingleIcon = 3,
  RkMenu = 4,
  RkDialog = 5,
  RkStringTableBundle = 6,
  RkAccelerators = 9,
  RkRcData = 10,
  RkCursorGroup = 12,
  RkIconGroup = 14,
  RkVersionInfo = 16,
  RkHTML = 23,

  // These kinds don't have assigned type IDs (they might be the resources
  // of invalid kind, expand to many resource structures in .res files,
  // or have variable type ID). In order to avoid ID clashes with IDs above,
  // we assign the kinds the values 256 and larger.
  RkInvalid = 256,
  RkBase,
  RkCursor,
  RkIcon,
  RkStringTable,
  RkUser,
  RkSingleCursorOrIconRes,
  RkCursorOrIconGroupRes,
};

// Non-zero memory flags.
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms648027(v=vs.85).aspx
enum MemoryFlags {
  MfMoveable = 0x10,
  MfPure = 0x20,
  MfPreload = 0x40,
  MfDiscardable = 0x1000
};

// Base resource. All the resources should derive from this base.
class RCResource {
public:
  IntOrString ResName;
  uint16_t MemoryFlags = getDefaultMemoryFlags();
  void setName(const IntOrString &Name) { ResName = Name; }
  virtual raw_ostream &log(raw_ostream &OS) const {
    return OS << "Base statement\n";
  };
  RCResource() {}
  RCResource(uint16_t Flags) : MemoryFlags(Flags) {}
  virtual ~RCResource() {}

  virtual Error visit(Visitor *) const {
    llvm_unreachable("This is unable to call methods from Visitor base");
  }

  // Apply the statements attached to this resource. Generic resources
  // don't have any.
  virtual Error applyStmts(Visitor *) const { return Error::success(); }

  // By default, memory flags are DISCARDABLE | PURE | MOVEABLE.
  static uint16_t getDefaultMemoryFlags() {
    return MfDiscardable | MfPure | MfMoveable;
  }

  virtual ResourceKind getKind() const { return RkBase; }
  static bool classof(const RCResource *Res) { return true; }

  virtual IntOrString getResourceType() const {
    llvm_unreachable("This cannot be called on objects without types.");
  }
  virtual Twine getResourceTypeName() const {
    llvm_unreachable("This cannot be called on objects without types.");
  };
};

// An empty resource. It has no content, type 0, ID 0 and all of its
// characteristics are equal to 0.
class NullResource : public RCResource {
public:
  NullResource() : RCResource(0) {}
  raw_ostream &log(raw_ostream &OS) const override {
    return OS << "Null resource\n";
  }
  Error visit(Visitor *V) const override { return V->visitNullResource(this); }
  IntOrString getResourceType() const override { return 0; }
  Twine getResourceTypeName() const override { return "(NULL)"; }
};

// Optional statement base. All such statements should derive from this base.
class OptionalStmt : public RCResource {};

class OptionalStmtList : public OptionalStmt {
  std::vector<std::unique_ptr<OptionalStmt>> Statements;

public:
  OptionalStmtList() {}
  raw_ostream &log(raw_ostream &OS) const override;

  void addStmt(std::unique_ptr<OptionalStmt> Stmt) {
    Statements.push_back(std::move(Stmt));
  }

  Error visit(Visitor *V) const override {
    for (auto &StmtPtr : Statements)
      if (auto Err = StmtPtr->visit(V))
        return Err;
    return Error::success();
  }
};

class OptStatementsRCResource : public RCResource {
public:
  std::unique_ptr<OptionalStmtList> OptStatements;

  OptStatementsRCResource(OptionalStmtList &&Stmts,
                          uint16_t Flags = RCResource::getDefaultMemoryFlags())
      : RCResource(Flags),
        OptStatements(std::make_unique<OptionalStmtList>(std::move(Stmts))) {}

  Error applyStmts(Visitor *V) const override {
    return OptStatements->visit(V);
  }
};

// LANGUAGE statement. It can occur both as a top-level statement (in such
// a situation, it changes the default language until the end of the file)
// and as an optional resource statement (then it changes the language
// of a single resource).
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381019(v=vs.85).aspx
class LanguageResource : public OptionalStmt {
public:
  uint32_t Lang, SubLang;

  LanguageResource(uint32_t LangId, uint32_t SubLangId)
      : Lang(LangId), SubLang(SubLangId) {}
  raw_ostream &log(raw_ostream &) const override;

  // This is not a regular top-level statement; when it occurs, it just
  // modifies the language context.
  Error visit(Visitor *V) const override { return V->visitLanguageStmt(this); }
  Twine getResourceTypeName() const override { return "LANGUAGE"; }
};

// ACCELERATORS resource. Defines a named table of accelerators for the app.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa380610(v=vs.85).aspx
class AcceleratorsResource : public OptStatementsRCResource {
public:
  class Accelerator {
  public:
    IntOrString Event;
    uint32_t Id;
    uint16_t Flags;

    enum Options {
      // This is actually 0x0000 (accelerator is assumed to be ASCII if it's
      // not VIRTKEY). However, rc.exe behavior is different in situations
      // "only ASCII defined" and "neither ASCII nor VIRTKEY defined".
      // Therefore, we include ASCII as another flag. This must be zeroed
      // when serialized.
      ASCII = 0x8000,
      VIRTKEY = 0x0001,
      NOINVERT = 0x0002,
      ALT = 0x0010,
      SHIFT = 0x0004,
      CONTROL = 0x0008
    };

    static constexpr size_t NumFlags = 6;
    static StringRef OptionsStr[NumFlags];
    static uint32_t OptionsFlags[NumFlags];
  };

  AcceleratorsResource(OptionalStmtList &&List, uint16_t Flags)
      : OptStatementsRCResource(std::move(List), Flags) {}

  std::vector<Accelerator> Accelerators;

  void addAccelerator(IntOrString Event, uint32_t Id, uint16_t Flags) {
    Accelerators.push_back(Accelerator{Event, Id, Flags});
  }
  raw_ostream &log(raw_ostream &) const override;

  IntOrString getResourceType() const override { return RkAccelerators; }
  static uint16_t getDefaultMemoryFlags() { return MfPure | MfMoveable; }
  Twine getResourceTypeName() const override { return "ACCELERATORS"; }

  Error visit(Visitor *V) const override {
    return V->visitAcceleratorsResource(this);
  }
  ResourceKind getKind() const override { return RkAccelerators; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkAccelerators;
  }
};

// BITMAP resource. Represents a bitmap (".bmp") file.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa380680(v=vs.85).aspx
class BitmapResource : public RCResource {
public:
  StringRef BitmapLoc;

  BitmapResource(StringRef Location, uint16_t Flags)
      : RCResource(Flags), BitmapLoc(Location) {}
  raw_ostream &log(raw_ostream &) const override;

  IntOrString getResourceType() const override { return RkBitmap; }
  static uint16_t getDefaultMemoryFlags() { return MfPure | MfMoveable; }

  Twine getResourceTypeName() const override { return "BITMAP"; }
  Error visit(Visitor *V) const override {
    return V->visitBitmapResource(this);
  }
  ResourceKind getKind() const override { return RkBitmap; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkBitmap;
  }
};

// CURSOR resource. Represents a single cursor (".cur") file.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa380920(v=vs.85).aspx
class CursorResource : public RCResource {
public:
  StringRef CursorLoc;

  CursorResource(StringRef Location, uint16_t Flags)
      : RCResource(Flags), CursorLoc(Location) {}
  raw_ostream &log(raw_ostream &) const override;

  Twine getResourceTypeName() const override { return "CURSOR"; }
  static uint16_t getDefaultMemoryFlags() { return MfDiscardable | MfMoveable; }
  Error visit(Visitor *V) const override {
    return V->visitCursorResource(this);
  }
  ResourceKind getKind() const override { return RkCursor; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkCursor;
  }
};

// ICON resource. Represents a single ".ico" file containing a group of icons.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381018(v=vs.85).aspx
class IconResource : public RCResource {
public:
  StringRef IconLoc;

  IconResource(StringRef Location, uint16_t Flags)
      : RCResource(Flags), IconLoc(Location) {}
  raw_ostream &log(raw_ostream &) const override;

  Twine getResourceTypeName() const override { return "ICON"; }
  static uint16_t getDefaultMemoryFlags() { return MfDiscardable | MfMoveable; }
  Error visit(Visitor *V) const override { return V->visitIconResource(this); }
  ResourceKind getKind() const override { return RkIcon; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkIcon;
  }
};

// HTML resource. Represents a local webpage that is to be embedded into the
// resulting resource file. It embeds a file only - no additional resources
// (images etc.) are included with this resource.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa966018(v=vs.85).aspx
class HTMLResource : public RCResource {
public:
  StringRef HTMLLoc;

  HTMLResource(StringRef Location, uint16_t Flags)
      : RCResource(Flags), HTMLLoc(Location) {}
  raw_ostream &log(raw_ostream &) const override;

  Error visit(Visitor *V) const override { return V->visitHTMLResource(this); }

  // Curiously, file resources don't have DISCARDABLE flag set.
  static uint16_t getDefaultMemoryFlags() { return MfPure | MfMoveable; }
  IntOrString getResourceType() const override { return RkHTML; }
  Twine getResourceTypeName() const override { return "HTML"; }
  ResourceKind getKind() const override { return RkHTML; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkHTML;
  }
};

// -- MENU resource and its helper classes --
// This resource describes the contents of an application menu
// (usually located in the upper part of the dialog.)
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381025(v=vs.85).aspx

// Description of a single submenu item.
class MenuDefinition {
public:
  enum Options {
    CHECKED = 0x0008,
    GRAYED = 0x0001,
    HELP = 0x4000,
    INACTIVE = 0x0002,
    MENUBARBREAK = 0x0020,
    MENUBREAK = 0x0040
  };

  enum MenuDefKind { MkBase, MkSeparator, MkMenuItem, MkPopup };

  static constexpr size_t NumFlags = 6;
  static StringRef OptionsStr[NumFlags];
  static uint32_t OptionsFlags[NumFlags];
  static raw_ostream &logFlags(raw_ostream &, uint16_t Flags);
  virtual raw_ostream &log(raw_ostream &OS) const {
    return OS << "Base menu definition\n";
  }
  virtual ~MenuDefinition() {}

  virtual uint16_t getResFlags() const { return 0; }
  virtual MenuDefKind getKind() const { return MkBase; }
};

// Recursive description of a whole submenu.
class MenuDefinitionList : public MenuDefinition {
public:
  std::vector<std::unique_ptr<MenuDefinition>> Definitions;

  void addDefinition(std::unique_ptr<MenuDefinition> Def) {
    Definitions.push_back(std::move(Def));
  }
  raw_ostream &log(raw_ostream &) const override;
};

// Separator in MENU definition (MENUITEM SEPARATOR).
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381024(v=vs.85).aspx
class MenuSeparator : public MenuDefinition {
public:
  raw_ostream &log(raw_ostream &) const override;

  MenuDefKind getKind() const override { return MkSeparator; }
  static bool classof(const MenuDefinition *D) {
    return D->getKind() == MkSeparator;
  }
};

// MENUITEM statement definition.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381024(v=vs.85).aspx
class MenuItem : public MenuDefinition {
public:
  StringRef Name;
  uint32_t Id;
  uint16_t Flags;

  MenuItem(StringRef Caption, uint32_t ItemId, uint16_t ItemFlags)
      : Name(Caption), Id(ItemId), Flags(ItemFlags) {}
  raw_ostream &log(raw_ostream &) const override;

  uint16_t getResFlags() const override { return Flags; }
  MenuDefKind getKind() const override { return MkMenuItem; }
  static bool classof(const MenuDefinition *D) {
    return D->getKind() == MkMenuItem;
  }
};

class MenuExItem : public MenuDefinition {
public:
  StringRef Name;
  uint32_t Id;
  uint32_t Type;
  uint32_t State;

  MenuExItem(StringRef Caption, uint32_t ItemId, uint32_t Type, uint32_t State)
      : Name(Caption), Id(ItemId), Type(Type), State(State) {}
  raw_ostream &log(raw_ostream &) const override;

  MenuDefKind getKind() const override { return MkMenuItem; }
  static bool classof(const MenuDefinition *D) {
    return D->getKind() == MkMenuItem;
  }
};

// POPUP statement definition.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381030(v=vs.85).aspx
class PopupItem : public MenuDefinition {
public:
  StringRef Name;
  uint16_t Flags;
  MenuDefinitionList SubItems;

  PopupItem(StringRef Caption, uint16_t ItemFlags,
            MenuDefinitionList &&SubItemsList)
      : Name(Caption), Flags(ItemFlags), SubItems(std::move(SubItemsList)) {}
  raw_ostream &log(raw_ostream &) const override;

  // This has an additional MF_POPUP (0x10) flag.
  uint16_t getResFlags() const override { return Flags | 0x10; }
  MenuDefKind getKind() const override { return MkPopup; }
  static bool classof(const MenuDefinition *D) {
    return D->getKind() == MkPopup;
  }
};

class PopupExItem : public MenuDefinition {
public:
  StringRef Name;
  uint32_t Id;
  uint32_t Type;
  uint32_t State;
  uint32_t HelpId;
  MenuDefinitionList SubItems;

  PopupExItem(StringRef Caption, uint32_t Id, uint32_t Type, uint32_t State,
              uint32_t HelpId, MenuDefinitionList &&SubItemsList)
      : Name(Caption), Id(Id), Type(Type), State(State), HelpId(HelpId),
        SubItems(std::move(SubItemsList)) {}
  raw_ostream &log(raw_ostream &) const override;

  uint16_t getResFlags() const override { return 0x01; }
  MenuDefKind getKind() const override { return MkPopup; }
  static bool classof(const MenuDefinition *D) {
    return D->getKind() == MkPopup;
  }
};

// Menu resource definition.
class MenuResource : public OptStatementsRCResource {
public:
  MenuDefinitionList Elements;

  MenuResource(OptionalStmtList &&OptStmts, MenuDefinitionList &&Items,
               uint16_t Flags)
      : OptStatementsRCResource(std::move(OptStmts), Flags),
        Elements(std::move(Items)) {}
  raw_ostream &log(raw_ostream &) const override;

  IntOrString getResourceType() const override { return RkMenu; }
  Twine getResourceTypeName() const override { return "MENU"; }
  Error visit(Visitor *V) const override { return V->visitMenuResource(this); }
  ResourceKind getKind() const override { return RkMenu; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkMenu;
  }
};

class MenuExResource : public OptStatementsRCResource {
public:
  MenuDefinitionList Elements;

  MenuExResource(MenuDefinitionList &&Items, uint16_t Flags)
      : OptStatementsRCResource({}, Flags), Elements(std::move(Items)) {}
  raw_ostream &log(raw_ostream &) const override;

  IntOrString getResourceType() const override { return RkMenu; }
  Twine getResourceTypeName() const override { return "MENUEX"; }
  Error visit(Visitor *V) const override {
    return V->visitMenuExResource(this);
  }
  ResourceKind getKind() const override { return RkMenu; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkMenu;
  }
};

// STRINGTABLE resource. Contains a list of strings, each having its unique ID.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381050(v=vs.85).aspx
class StringTableResource : public OptStatementsRCResource {
public:
  std::vector<std::pair<uint32_t, std::vector<StringRef>>> Table;

  StringTableResource(OptionalStmtList &&List, uint16_t Flags)
      : OptStatementsRCResource(std::move(List), Flags) {}
  void addStrings(uint32_t ID, std::vector<StringRef> &&Strings) {
    Table.emplace_back(ID, Strings);
  }
  raw_ostream &log(raw_ostream &) const override;
  Twine getResourceTypeName() const override { return "STRINGTABLE"; }
  Error visit(Visitor *V) const override {
    return V->visitStringTableResource(this);
  }
};

// -- DIALOG(EX) resource and its helper classes --
//
// This resource describes dialog boxes and controls residing inside them.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381003(v=vs.85).aspx
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381002(v=vs.85).aspx

// Single control definition.
class Control {
public:
  StringRef Type;
  IntOrString Title;
  uint32_t ID, X, Y, Width, Height;
  std::optional<IntWithNotMask> Style;
  std::optional<uint32_t> ExtStyle, HelpID;
  IntOrString Class;

  // Control classes as described in DLGITEMTEMPLATEEX documentation.
  //
  // Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms645389.aspx
  enum CtlClasses {
    ClsButton = 0x80,
    ClsEdit = 0x81,
    ClsStatic = 0x82,
    ClsListBox = 0x83,
    ClsScrollBar = 0x84,
    ClsComboBox = 0x85
  };

  // Simple information about a single control type.
  struct CtlInfo {
    uint32_t Style;
    uint16_t CtlClass;
    bool HasTitle;
  };

  Control(StringRef CtlType, IntOrString CtlTitle, uint32_t CtlID,
          uint32_t PosX, uint32_t PosY, uint32_t ItemWidth, uint32_t ItemHeight,
          std::optional<IntWithNotMask> ItemStyle,
          std::optional<uint32_t> ExtItemStyle,
          std::optional<uint32_t> CtlHelpID, IntOrString CtlClass)
      : Type(CtlType), Title(CtlTitle), ID(CtlID), X(PosX), Y(PosY),
        Width(ItemWidth), Height(ItemHeight), Style(ItemStyle),
        ExtStyle(ExtItemStyle), HelpID(CtlHelpID), Class(CtlClass) {}

  static const StringMap<CtlInfo> SupportedCtls;

  raw_ostream &log(raw_ostream &) const;
};

// Single dialog definition. We don't create distinct classes for DIALOG and
// DIALOGEX because of their being too similar to each other. We only have a
// flag determining the type of the dialog box.
class DialogResource : public OptStatementsRCResource {
public:
  uint32_t X, Y, Width, Height, HelpID;
  std::vector<Control> Controls;
  bool IsExtended;

  DialogResource(uint32_t PosX, uint32_t PosY, uint32_t DlgWidth,
                 uint32_t DlgHeight, uint32_t DlgHelpID,
                 OptionalStmtList &&OptStmts, bool IsDialogEx, uint16_t Flags)
      : OptStatementsRCResource(std::move(OptStmts), Flags), X(PosX), Y(PosY),
        Width(DlgWidth), Height(DlgHeight), HelpID(DlgHelpID),
        IsExtended(IsDialogEx) {}

  void addControl(Control &&Ctl) { Controls.push_back(std::move(Ctl)); }

  raw_ostream &log(raw_ostream &) const override;

  // It was a weird design decision to assign the same resource type number
  // both for DIALOG and DIALOGEX (and the same structure version number).
  // It makes it possible for DIALOG to be mistaken for DIALOGEX.
  IntOrString getResourceType() const override { return RkDialog; }
  Twine getResourceTypeName() const override {
    return "DIALOG" + Twine(IsExtended ? "EX" : "");
  }
  Error visit(Visitor *V) const override {
    return V->visitDialogResource(this);
  }
  ResourceKind getKind() const override { return RkDialog; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkDialog;
  }
};

// User-defined resource. It is either:
//   * a link to the file, e.g. NAME TYPE "filename",
//   * or contains a list of integers and strings, e.g. NAME TYPE {1, "a", 2}.
class UserDefinedResource : public RCResource {
public:
  IntOrString Type;
  StringRef FileLoc;
  std::vector<IntOrString> Contents;
  bool IsFileResource;

  UserDefinedResource(IntOrString ResourceType, StringRef FileLocation,
                      uint16_t Flags)
      : RCResource(Flags), Type(ResourceType), FileLoc(FileLocation),
        IsFileResource(true) {}
  UserDefinedResource(IntOrString ResourceType, std::vector<IntOrString> &&Data,
                      uint16_t Flags)
      : RCResource(Flags), Type(ResourceType), Contents(std::move(Data)),
        IsFileResource(false) {}

  raw_ostream &log(raw_ostream &) const override;
  IntOrString getResourceType() const override { return Type; }
  Twine getResourceTypeName() const override { return Type; }
  static uint16_t getDefaultMemoryFlags() { return MfPure | MfMoveable; }

  Error visit(Visitor *V) const override {
    return V->visitUserDefinedResource(this);
  }
  ResourceKind getKind() const override { return RkUser; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkUser;
  }
};

// -- VERSIONINFO resource and its helper classes --
//
// This resource lists the version information on the executable/library.
// The declaration consists of the following items:
//   * A number of fixed optional version statements (e.g. FILEVERSION, FILEOS)
//   * BEGIN
//   * A number of BLOCK and/or VALUE statements. BLOCK recursively defines
//       another block of version information, whereas VALUE defines a
//       key -> value correspondence. There might be more than one value
//       corresponding to the single key.
//   * END
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381058(v=vs.85).aspx

// A single VERSIONINFO statement;
class VersionInfoStmt {
public:
  enum StmtKind { StBase = 0, StBlock = 1, StValue = 2 };

  virtual raw_ostream &log(raw_ostream &OS) const { return OS << "VI stmt\n"; }
  virtual ~VersionInfoStmt() {}

  virtual StmtKind getKind() const { return StBase; }
  static bool classof(const VersionInfoStmt *S) {
    return S->getKind() == StBase;
  }
};

// BLOCK definition; also the main VERSIONINFO declaration is considered a
// BLOCK, although it has no name.
// The correct top-level blocks are "VarFileInfo" and "StringFileInfo". We don't
// care about them at the parsing phase.
class VersionInfoBlock : public VersionInfoStmt {
public:
  std::vector<std::unique_ptr<VersionInfoStmt>> Stmts;
  StringRef Name;

  VersionInfoBlock(StringRef BlockName) : Name(BlockName) {}
  void addStmt(std::unique_ptr<VersionInfoStmt> Stmt) {
    Stmts.push_back(std::move(Stmt));
  }
  raw_ostream &log(raw_ostream &) const override;

  StmtKind getKind() const override { return StBlock; }
  static bool classof(const VersionInfoStmt *S) {
    return S->getKind() == StBlock;
  }
};

class VersionInfoValue : public VersionInfoStmt {
public:
  StringRef Key;
  std::vector<IntOrString> Values;
  BitVector HasPrecedingComma;

  VersionInfoValue(StringRef InfoKey, std::vector<IntOrString> &&Vals,
                   BitVector &&CommasBeforeVals)
      : Key(InfoKey), Values(std::move(Vals)),
        HasPrecedingComma(std::move(CommasBeforeVals)) {}
  raw_ostream &log(raw_ostream &) const override;

  StmtKind getKind() const override { return StValue; }
  static bool classof(const VersionInfoStmt *S) {
    return S->getKind() == StValue;
  }
};

class VersionInfoResource : public RCResource {
public:
  // A class listing fixed VERSIONINFO statements (occuring before main BEGIN).
  // If any of these is not specified, it is assumed by the original tool to
  // be equal to 0.
  class VersionInfoFixed {
  public:
    enum VersionInfoFixedType {
      FtUnknown,
      FtFileVersion,
      FtProductVersion,
      FtFileFlagsMask,
      FtFileFlags,
      FtFileOS,
      FtFileType,
      FtFileSubtype,
      FtNumTypes
    };

  private:
    static const StringMap<VersionInfoFixedType> FixedFieldsInfoMap;
    static const StringRef FixedFieldsNames[FtNumTypes];

  public:
    SmallVector<uint32_t, 4> FixedInfo[FtNumTypes];
    SmallVector<bool, FtNumTypes> IsTypePresent;

    static VersionInfoFixedType getFixedType(StringRef Type);
    static bool isTypeSupported(VersionInfoFixedType Type);
    static bool isVersionType(VersionInfoFixedType Type);

    VersionInfoFixed() : IsTypePresent(FtNumTypes, false) {}

    void setValue(VersionInfoFixedType Type, ArrayRef<uint32_t> Value) {
      FixedInfo[Type] = SmallVector<uint32_t, 4>(Value.begin(), Value.end());
      IsTypePresent[Type] = true;
    }

    raw_ostream &log(raw_ostream &) const;
  };

  VersionInfoBlock MainBlock;
  VersionInfoFixed FixedData;

  VersionInfoResource(VersionInfoBlock &&TopLevelBlock,
                      VersionInfoFixed &&FixedInfo, uint16_t Flags)
      : RCResource(Flags), MainBlock(std::move(TopLevelBlock)),
        FixedData(std::move(FixedInfo)) {}

  raw_ostream &log(raw_ostream &) const override;
  IntOrString getResourceType() const override { return RkVersionInfo; }
  static uint16_t getDefaultMemoryFlags() { return MfMoveable | MfPure; }
  Twine getResourceTypeName() const override { return "VERSIONINFO"; }
  Error visit(Visitor *V) const override {
    return V->visitVersionInfoResource(this);
  }
  ResourceKind getKind() const override { return RkVersionInfo; }
  static bool classof(const RCResource *Res) {
    return Res->getKind() == RkVersionInfo;
  }
};

// CHARACTERISTICS optional statement.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa380872(v=vs.85).aspx
class CharacteristicsStmt : public OptionalStmt {
public:
  uint32_t Value;

  CharacteristicsStmt(uint32_t Characteristic) : Value(Characteristic) {}
  raw_ostream &log(raw_ostream &) const override;

  Twine getResourceTypeName() const override { return "CHARACTERISTICS"; }
  Error visit(Visitor *V) const override {
    return V->visitCharacteristicsStmt(this);
  }
};

// VERSION optional statement.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381059(v=vs.85).aspx
class VersionStmt : public OptionalStmt {
public:
  uint32_t Value;

  VersionStmt(uint32_t Version) : Value(Version) {}
  raw_ostream &log(raw_ostream &) const override;

  Twine getResourceTypeName() const override { return "VERSION"; }
  Error visit(Visitor *V) const override { return V->visitVersionStmt(this); }
};

// CAPTION optional statement.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa380778(v=vs.85).aspx
class CaptionStmt : public OptionalStmt {
public:
  StringRef Value;

  CaptionStmt(StringRef Caption) : Value(Caption) {}
  raw_ostream &log(raw_ostream &) const override;
  Twine getResourceTypeName() const override { return "CAPTION"; }
  Error visit(Visitor *V) const override { return V->visitCaptionStmt(this); }
};

// FONT optional statement.
// Note that the documentation is inaccurate: it expects five arguments to be
// given, however the example provides only two. In fact, the original tool
// expects two arguments - point size and name of the typeface.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381013(v=vs.85).aspx
class FontStmt : public OptionalStmt {
public:
  uint32_t Size, Weight, Charset;
  StringRef Name;
  bool Italic;

  FontStmt(uint32_t FontSize, StringRef FontName, uint32_t FontWeight,
           bool FontItalic, uint32_t FontCharset)
      : Size(FontSize), Weight(FontWeight), Charset(FontCharset),
        Name(FontName), Italic(FontItalic) {}
  raw_ostream &log(raw_ostream &) const override;
  Twine getResourceTypeName() const override { return "FONT"; }
  Error visit(Visitor *V) const override { return V->visitFontStmt(this); }
};

// STYLE optional statement.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa381051(v=vs.85).aspx
class StyleStmt : public OptionalStmt {
public:
  uint32_t Value;

  StyleStmt(uint32_t Style) : Value(Style) {}
  raw_ostream &log(raw_ostream &) const override;
  Twine getResourceTypeName() const override { return "STYLE"; }
  Error visit(Visitor *V) const override { return V->visitStyleStmt(this); }
};

// EXSTYLE optional statement.
//
// Ref: docs.microsoft.com/en-us/windows/desktop/menurc/exstyle-statement
class ExStyleStmt : public OptionalStmt {
public:
  uint32_t Value;

  ExStyleStmt(uint32_t ExStyle) : Value(ExStyle) {}
  raw_ostream &log(raw_ostream &) const override;
  Twine getResourceTypeName() const override { return "EXSTYLE"; }
  Error visit(Visitor *V) const override { return V->visitExStyleStmt(this); }
};

// MENU optional statement.
//
// Ref: https://learn.microsoft.com/en-us/windows/win32/menurc/menu-statement
class MenuStmt : public OptionalStmt {
public:
  IntOrString Value;

  MenuStmt(IntOrString NameOrId) : Value(NameOrId) {}
  raw_ostream &log(raw_ostream &) const override;
  Twine getResourceTypeName() const override { return "MENU"; }
  Error visit(Visitor *V) const override { return V->visitMenuStmt(this); }
};

// CLASS optional statement.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa380883(v=vs.85).aspx
class ClassStmt : public OptionalStmt {
public:
  IntOrString Value;

  ClassStmt(IntOrString Class) : Value(Class) {}
  raw_ostream &log(raw_ostream &) const override;
  Twine getResourceTypeName() const override { return "CLASS"; }
  Error visit(Visitor *V) const override { return V->visitClassStmt(this); }
};

} // namespace rc
} // namespace llvm

#endif
