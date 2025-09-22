//=- LocalizationChecker.cpp -------------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a set of checks for localizability including:
//  1) A checker that warns about uses of non-localized NSStrings passed to
//     UI methods expecting localized strings
//  2) A syntactic checker that warns against the bad practice of
//     not including a comment in NSLocalizedString macros.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Lex/Lexer.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Unicode.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {
struct LocalizedState {
private:
  enum Kind { NonLocalized, Localized } K;
  LocalizedState(Kind InK) : K(InK) {}

public:
  bool isLocalized() const { return K == Localized; }
  bool isNonLocalized() const { return K == NonLocalized; }

  static LocalizedState getLocalized() { return LocalizedState(Localized); }
  static LocalizedState getNonLocalized() {
    return LocalizedState(NonLocalized);
  }

  // Overload the == operator
  bool operator==(const LocalizedState &X) const { return K == X.K; }

  // LLVMs equivalent of a hash function
  void Profile(llvm::FoldingSetNodeID &ID) const { ID.AddInteger(K); }
};

class NonLocalizedStringChecker
    : public Checker<check::PreCall, check::PostCall, check::PreObjCMessage,
                     check::PostObjCMessage,
                     check::PostStmt<ObjCStringLiteral>> {

  const BugType BT{this, "Unlocalizable string",
                   "Localizability Issue (Apple)"};

  // Methods that require a localized string
  mutable llvm::DenseMap<const IdentifierInfo *,
                         llvm::DenseMap<Selector, uint8_t>> UIMethods;
  // Methods that return a localized string
  mutable llvm::SmallSet<std::pair<const IdentifierInfo *, Selector>, 12> LSM;
  // C Functions that return a localized string
  mutable llvm::SmallSet<const IdentifierInfo *, 5> LSF;

  void initUIMethods(ASTContext &Ctx) const;
  void initLocStringsMethods(ASTContext &Ctx) const;

  bool hasNonLocalizedState(SVal S, CheckerContext &C) const;
  bool hasLocalizedState(SVal S, CheckerContext &C) const;
  void setNonLocalizedState(SVal S, CheckerContext &C) const;
  void setLocalizedState(SVal S, CheckerContext &C) const;

  bool isAnnotatedAsReturningLocalized(const Decl *D) const;
  bool isAnnotatedAsTakingLocalized(const Decl *D) const;
  void reportLocalizationError(SVal S, const CallEvent &M, CheckerContext &C,
                               int argumentNumber = 0) const;

  int getLocalizedArgumentForSelector(const IdentifierInfo *Receiver,
                                      Selector S) const;

public:
  // When this parameter is set to true, the checker assumes all
  // methods that return NSStrings are unlocalized. Thus, more false
  // positives will be reported.
  bool IsAggressive = false;

  void checkPreObjCMessage(const ObjCMethodCall &msg, CheckerContext &C) const;
  void checkPostObjCMessage(const ObjCMethodCall &msg, CheckerContext &C) const;
  void checkPostStmt(const ObjCStringLiteral *SL, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
};

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(LocalizedMemMap, const MemRegion *,
                               LocalizedState)

namespace {
class NonLocalizedStringBRVisitor final : public BugReporterVisitor {

  const MemRegion *NonLocalizedString;
  bool Satisfied;

public:
  NonLocalizedStringBRVisitor(const MemRegion *NonLocalizedString)
      : NonLocalizedString(NonLocalizedString), Satisfied(false) {
    assert(NonLocalizedString);
  }

  PathDiagnosticPieceRef VisitNode(const ExplodedNode *Succ,
                                   BugReporterContext &BRC,
                                   PathSensitiveBugReport &BR) override;

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    ID.Add(NonLocalizedString);
  }
};
} // End anonymous namespace.

#define NEW_RECEIVER(receiver)                                                 \
  llvm::DenseMap<Selector, uint8_t> &receiver##M =                             \
      UIMethods.insert({&Ctx.Idents.get(#receiver),                            \
                        llvm::DenseMap<Selector, uint8_t>()})                  \
          .first->second;
#define ADD_NULLARY_METHOD(receiver, method, argument)                         \
  receiver##M.insert(                                                          \
      {Ctx.Selectors.getNullarySelector(&Ctx.Idents.get(#method)), argument});
#define ADD_UNARY_METHOD(receiver, method, argument)                           \
  receiver##M.insert(                                                          \
      {Ctx.Selectors.getUnarySelector(&Ctx.Idents.get(#method)), argument});
#define ADD_METHOD(receiver, method_list, count, argument)                     \
  receiver##M.insert({Ctx.Selectors.getSelector(count, method_list), argument});

/// Initializes a list of methods that require a localized string
/// Format: {"ClassName", {{"selectorName:", LocStringArg#}, ...}, ...}
void NonLocalizedStringChecker::initUIMethods(ASTContext &Ctx) const {
  if (!UIMethods.empty())
    return;

  // UI Methods
  NEW_RECEIVER(UISearchDisplayController)
  ADD_UNARY_METHOD(UISearchDisplayController, setSearchResultsTitle, 0)

  NEW_RECEIVER(UITabBarItem)
  const IdentifierInfo *initWithTitleUITabBarItemTag[] = {
      &Ctx.Idents.get("initWithTitle"), &Ctx.Idents.get("image"),
      &Ctx.Idents.get("tag")};
  ADD_METHOD(UITabBarItem, initWithTitleUITabBarItemTag, 3, 0)
  const IdentifierInfo *initWithTitleUITabBarItemImage[] = {
      &Ctx.Idents.get("initWithTitle"), &Ctx.Idents.get("image"),
      &Ctx.Idents.get("selectedImage")};
  ADD_METHOD(UITabBarItem, initWithTitleUITabBarItemImage, 3, 0)

  NEW_RECEIVER(NSDockTile)
  ADD_UNARY_METHOD(NSDockTile, setBadgeLabel, 0)

  NEW_RECEIVER(NSStatusItem)
  ADD_UNARY_METHOD(NSStatusItem, setTitle, 0)
  ADD_UNARY_METHOD(NSStatusItem, setToolTip, 0)

  NEW_RECEIVER(UITableViewRowAction)
  const IdentifierInfo *rowActionWithStyleUITableViewRowAction[] = {
      &Ctx.Idents.get("rowActionWithStyle"), &Ctx.Idents.get("title"),
      &Ctx.Idents.get("handler")};
  ADD_METHOD(UITableViewRowAction, rowActionWithStyleUITableViewRowAction, 3, 1)
  ADD_UNARY_METHOD(UITableViewRowAction, setTitle, 0)

  NEW_RECEIVER(NSBox)
  ADD_UNARY_METHOD(NSBox, setTitle, 0)

  NEW_RECEIVER(NSButton)
  ADD_UNARY_METHOD(NSButton, setTitle, 0)
  ADD_UNARY_METHOD(NSButton, setAlternateTitle, 0)
  const IdentifierInfo *radioButtonWithTitleNSButton[] = {
      &Ctx.Idents.get("radioButtonWithTitle"), &Ctx.Idents.get("target"),
      &Ctx.Idents.get("action")};
  ADD_METHOD(NSButton, radioButtonWithTitleNSButton, 3, 0)
  const IdentifierInfo *buttonWithTitleNSButtonImage[] = {
      &Ctx.Idents.get("buttonWithTitle"), &Ctx.Idents.get("image"),
      &Ctx.Idents.get("target"), &Ctx.Idents.get("action")};
  ADD_METHOD(NSButton, buttonWithTitleNSButtonImage, 4, 0)
  const IdentifierInfo *checkboxWithTitleNSButton[] = {
      &Ctx.Idents.get("checkboxWithTitle"), &Ctx.Idents.get("target"),
      &Ctx.Idents.get("action")};
  ADD_METHOD(NSButton, checkboxWithTitleNSButton, 3, 0)
  const IdentifierInfo *buttonWithTitleNSButtonTarget[] = {
      &Ctx.Idents.get("buttonWithTitle"), &Ctx.Idents.get("target"),
      &Ctx.Idents.get("action")};
  ADD_METHOD(NSButton, buttonWithTitleNSButtonTarget, 3, 0)

  NEW_RECEIVER(NSSavePanel)
  ADD_UNARY_METHOD(NSSavePanel, setPrompt, 0)
  ADD_UNARY_METHOD(NSSavePanel, setTitle, 0)
  ADD_UNARY_METHOD(NSSavePanel, setNameFieldLabel, 0)
  ADD_UNARY_METHOD(NSSavePanel, setNameFieldStringValue, 0)
  ADD_UNARY_METHOD(NSSavePanel, setMessage, 0)

  NEW_RECEIVER(UIPrintInfo)
  ADD_UNARY_METHOD(UIPrintInfo, setJobName, 0)

  NEW_RECEIVER(NSTabViewItem)
  ADD_UNARY_METHOD(NSTabViewItem, setLabel, 0)
  ADD_UNARY_METHOD(NSTabViewItem, setToolTip, 0)

  NEW_RECEIVER(NSBrowser)
  const IdentifierInfo *setTitleNSBrowser[] = {&Ctx.Idents.get("setTitle"),
                                               &Ctx.Idents.get("ofColumn")};
  ADD_METHOD(NSBrowser, setTitleNSBrowser, 2, 0)

  NEW_RECEIVER(UIAccessibilityElement)
  ADD_UNARY_METHOD(UIAccessibilityElement, setAccessibilityLabel, 0)
  ADD_UNARY_METHOD(UIAccessibilityElement, setAccessibilityHint, 0)
  ADD_UNARY_METHOD(UIAccessibilityElement, setAccessibilityValue, 0)

  NEW_RECEIVER(UIAlertAction)
  const IdentifierInfo *actionWithTitleUIAlertAction[] = {
      &Ctx.Idents.get("actionWithTitle"), &Ctx.Idents.get("style"),
      &Ctx.Idents.get("handler")};
  ADD_METHOD(UIAlertAction, actionWithTitleUIAlertAction, 3, 0)

  NEW_RECEIVER(NSPopUpButton)
  ADD_UNARY_METHOD(NSPopUpButton, addItemWithTitle, 0)
  const IdentifierInfo *insertItemWithTitleNSPopUpButton[] = {
      &Ctx.Idents.get("insertItemWithTitle"), &Ctx.Idents.get("atIndex")};
  ADD_METHOD(NSPopUpButton, insertItemWithTitleNSPopUpButton, 2, 0)
  ADD_UNARY_METHOD(NSPopUpButton, removeItemWithTitle, 0)
  ADD_UNARY_METHOD(NSPopUpButton, selectItemWithTitle, 0)
  ADD_UNARY_METHOD(NSPopUpButton, setTitle, 0)

  NEW_RECEIVER(NSTableViewRowAction)
  const IdentifierInfo *rowActionWithStyleNSTableViewRowAction[] = {
      &Ctx.Idents.get("rowActionWithStyle"), &Ctx.Idents.get("title"),
      &Ctx.Idents.get("handler")};
  ADD_METHOD(NSTableViewRowAction, rowActionWithStyleNSTableViewRowAction, 3, 1)
  ADD_UNARY_METHOD(NSTableViewRowAction, setTitle, 0)

  NEW_RECEIVER(NSImage)
  ADD_UNARY_METHOD(NSImage, setAccessibilityDescription, 0)

  NEW_RECEIVER(NSUserActivity)
  ADD_UNARY_METHOD(NSUserActivity, setTitle, 0)

  NEW_RECEIVER(NSPathControlItem)
  ADD_UNARY_METHOD(NSPathControlItem, setTitle, 0)

  NEW_RECEIVER(NSCell)
  ADD_UNARY_METHOD(NSCell, initTextCell, 0)
  ADD_UNARY_METHOD(NSCell, setTitle, 0)
  ADD_UNARY_METHOD(NSCell, setStringValue, 0)

  NEW_RECEIVER(NSPathControl)
  ADD_UNARY_METHOD(NSPathControl, setPlaceholderString, 0)

  NEW_RECEIVER(UIAccessibility)
  ADD_UNARY_METHOD(UIAccessibility, setAccessibilityLabel, 0)
  ADD_UNARY_METHOD(UIAccessibility, setAccessibilityHint, 0)
  ADD_UNARY_METHOD(UIAccessibility, setAccessibilityValue, 0)

  NEW_RECEIVER(NSTableColumn)
  ADD_UNARY_METHOD(NSTableColumn, setTitle, 0)
  ADD_UNARY_METHOD(NSTableColumn, setHeaderToolTip, 0)

  NEW_RECEIVER(NSSegmentedControl)
  const IdentifierInfo *setLabelNSSegmentedControl[] = {
      &Ctx.Idents.get("setLabel"), &Ctx.Idents.get("forSegment")};
  ADD_METHOD(NSSegmentedControl, setLabelNSSegmentedControl, 2, 0)
  const IdentifierInfo *setToolTipNSSegmentedControl[] = {
      &Ctx.Idents.get("setToolTip"), &Ctx.Idents.get("forSegment")};
  ADD_METHOD(NSSegmentedControl, setToolTipNSSegmentedControl, 2, 0)

  NEW_RECEIVER(NSButtonCell)
  ADD_UNARY_METHOD(NSButtonCell, setTitle, 0)
  ADD_UNARY_METHOD(NSButtonCell, setAlternateTitle, 0)

  NEW_RECEIVER(NSDatePickerCell)
  ADD_UNARY_METHOD(NSDatePickerCell, initTextCell, 0)

  NEW_RECEIVER(NSSliderCell)
  ADD_UNARY_METHOD(NSSliderCell, setTitle, 0)

  NEW_RECEIVER(NSControl)
  ADD_UNARY_METHOD(NSControl, setStringValue, 0)

  NEW_RECEIVER(NSAccessibility)
  ADD_UNARY_METHOD(NSAccessibility, setAccessibilityValueDescription, 0)
  ADD_UNARY_METHOD(NSAccessibility, setAccessibilityLabel, 0)
  ADD_UNARY_METHOD(NSAccessibility, setAccessibilityTitle, 0)
  ADD_UNARY_METHOD(NSAccessibility, setAccessibilityPlaceholderValue, 0)
  ADD_UNARY_METHOD(NSAccessibility, setAccessibilityHelp, 0)

  NEW_RECEIVER(NSMatrix)
  const IdentifierInfo *setToolTipNSMatrix[] = {&Ctx.Idents.get("setToolTip"),
                                                &Ctx.Idents.get("forCell")};
  ADD_METHOD(NSMatrix, setToolTipNSMatrix, 2, 0)

  NEW_RECEIVER(NSPrintPanel)
  ADD_UNARY_METHOD(NSPrintPanel, setDefaultButtonTitle, 0)

  NEW_RECEIVER(UILocalNotification)
  ADD_UNARY_METHOD(UILocalNotification, setAlertBody, 0)
  ADD_UNARY_METHOD(UILocalNotification, setAlertAction, 0)
  ADD_UNARY_METHOD(UILocalNotification, setAlertTitle, 0)

  NEW_RECEIVER(NSSlider)
  ADD_UNARY_METHOD(NSSlider, setTitle, 0)

  NEW_RECEIVER(UIMenuItem)
  const IdentifierInfo *initWithTitleUIMenuItem[] = {
      &Ctx.Idents.get("initWithTitle"), &Ctx.Idents.get("action")};
  ADD_METHOD(UIMenuItem, initWithTitleUIMenuItem, 2, 0)
  ADD_UNARY_METHOD(UIMenuItem, setTitle, 0)

  NEW_RECEIVER(UIAlertController)
  const IdentifierInfo *alertControllerWithTitleUIAlertController[] = {
      &Ctx.Idents.get("alertControllerWithTitle"), &Ctx.Idents.get("message"),
      &Ctx.Idents.get("preferredStyle")};
  ADD_METHOD(UIAlertController, alertControllerWithTitleUIAlertController, 3, 1)
  ADD_UNARY_METHOD(UIAlertController, setTitle, 0)
  ADD_UNARY_METHOD(UIAlertController, setMessage, 0)

  NEW_RECEIVER(UIApplicationShortcutItem)
  const IdentifierInfo *initWithTypeUIApplicationShortcutItemIcon[] = {
      &Ctx.Idents.get("initWithType"), &Ctx.Idents.get("localizedTitle"),
      &Ctx.Idents.get("localizedSubtitle"), &Ctx.Idents.get("icon"),
      &Ctx.Idents.get("userInfo")};
  ADD_METHOD(UIApplicationShortcutItem,
             initWithTypeUIApplicationShortcutItemIcon, 5, 1)
  const IdentifierInfo *initWithTypeUIApplicationShortcutItem[] = {
      &Ctx.Idents.get("initWithType"), &Ctx.Idents.get("localizedTitle")};
  ADD_METHOD(UIApplicationShortcutItem, initWithTypeUIApplicationShortcutItem,
             2, 1)

  NEW_RECEIVER(UIActionSheet)
  const IdentifierInfo *initWithTitleUIActionSheet[] = {
      &Ctx.Idents.get("initWithTitle"), &Ctx.Idents.get("delegate"),
      &Ctx.Idents.get("cancelButtonTitle"),
      &Ctx.Idents.get("destructiveButtonTitle"),
      &Ctx.Idents.get("otherButtonTitles")};
  ADD_METHOD(UIActionSheet, initWithTitleUIActionSheet, 5, 0)
  ADD_UNARY_METHOD(UIActionSheet, addButtonWithTitle, 0)
  ADD_UNARY_METHOD(UIActionSheet, setTitle, 0)

  NEW_RECEIVER(UIAccessibilityCustomAction)
  const IdentifierInfo *initWithNameUIAccessibilityCustomAction[] = {
      &Ctx.Idents.get("initWithName"), &Ctx.Idents.get("target"),
      &Ctx.Idents.get("selector")};
  ADD_METHOD(UIAccessibilityCustomAction,
             initWithNameUIAccessibilityCustomAction, 3, 0)
  ADD_UNARY_METHOD(UIAccessibilityCustomAction, setName, 0)

  NEW_RECEIVER(UISearchBar)
  ADD_UNARY_METHOD(UISearchBar, setText, 0)
  ADD_UNARY_METHOD(UISearchBar, setPrompt, 0)
  ADD_UNARY_METHOD(UISearchBar, setPlaceholder, 0)

  NEW_RECEIVER(UIBarItem)
  ADD_UNARY_METHOD(UIBarItem, setTitle, 0)

  NEW_RECEIVER(UITextView)
  ADD_UNARY_METHOD(UITextView, setText, 0)

  NEW_RECEIVER(NSView)
  ADD_UNARY_METHOD(NSView, setToolTip, 0)

  NEW_RECEIVER(NSTextField)
  ADD_UNARY_METHOD(NSTextField, setPlaceholderString, 0)
  ADD_UNARY_METHOD(NSTextField, textFieldWithString, 0)
  ADD_UNARY_METHOD(NSTextField, wrappingLabelWithString, 0)
  ADD_UNARY_METHOD(NSTextField, labelWithString, 0)

  NEW_RECEIVER(NSAttributedString)
  ADD_UNARY_METHOD(NSAttributedString, initWithString, 0)
  const IdentifierInfo *initWithStringNSAttributedString[] = {
      &Ctx.Idents.get("initWithString"), &Ctx.Idents.get("attributes")};
  ADD_METHOD(NSAttributedString, initWithStringNSAttributedString, 2, 0)

  NEW_RECEIVER(NSText)
  ADD_UNARY_METHOD(NSText, setString, 0)

  NEW_RECEIVER(UIKeyCommand)
  const IdentifierInfo *keyCommandWithInputUIKeyCommand[] = {
      &Ctx.Idents.get("keyCommandWithInput"), &Ctx.Idents.get("modifierFlags"),
      &Ctx.Idents.get("action"), &Ctx.Idents.get("discoverabilityTitle")};
  ADD_METHOD(UIKeyCommand, keyCommandWithInputUIKeyCommand, 4, 3)
  ADD_UNARY_METHOD(UIKeyCommand, setDiscoverabilityTitle, 0)

  NEW_RECEIVER(UILabel)
  ADD_UNARY_METHOD(UILabel, setText, 0)

  NEW_RECEIVER(NSAlert)
  const IdentifierInfo *alertWithMessageTextNSAlert[] = {
      &Ctx.Idents.get("alertWithMessageText"), &Ctx.Idents.get("defaultButton"),
      &Ctx.Idents.get("alternateButton"), &Ctx.Idents.get("otherButton"),
      &Ctx.Idents.get("informativeTextWithFormat")};
  ADD_METHOD(NSAlert, alertWithMessageTextNSAlert, 5, 0)
  ADD_UNARY_METHOD(NSAlert, addButtonWithTitle, 0)
  ADD_UNARY_METHOD(NSAlert, setMessageText, 0)
  ADD_UNARY_METHOD(NSAlert, setInformativeText, 0)
  ADD_UNARY_METHOD(NSAlert, setHelpAnchor, 0)

  NEW_RECEIVER(UIMutableApplicationShortcutItem)
  ADD_UNARY_METHOD(UIMutableApplicationShortcutItem, setLocalizedTitle, 0)
  ADD_UNARY_METHOD(UIMutableApplicationShortcutItem, setLocalizedSubtitle, 0)

  NEW_RECEIVER(UIButton)
  const IdentifierInfo *setTitleUIButton[] = {&Ctx.Idents.get("setTitle"),
                                              &Ctx.Idents.get("forState")};
  ADD_METHOD(UIButton, setTitleUIButton, 2, 0)

  NEW_RECEIVER(NSWindow)
  ADD_UNARY_METHOD(NSWindow, setTitle, 0)
  const IdentifierInfo *minFrameWidthWithTitleNSWindow[] = {
      &Ctx.Idents.get("minFrameWidthWithTitle"), &Ctx.Idents.get("styleMask")};
  ADD_METHOD(NSWindow, minFrameWidthWithTitleNSWindow, 2, 0)
  ADD_UNARY_METHOD(NSWindow, setMiniwindowTitle, 0)

  NEW_RECEIVER(NSPathCell)
  ADD_UNARY_METHOD(NSPathCell, setPlaceholderString, 0)

  NEW_RECEIVER(UIDocumentMenuViewController)
  const IdentifierInfo *addOptionWithTitleUIDocumentMenuViewController[] = {
      &Ctx.Idents.get("addOptionWithTitle"), &Ctx.Idents.get("image"),
      &Ctx.Idents.get("order"), &Ctx.Idents.get("handler")};
  ADD_METHOD(UIDocumentMenuViewController,
             addOptionWithTitleUIDocumentMenuViewController, 4, 0)

  NEW_RECEIVER(UINavigationItem)
  ADD_UNARY_METHOD(UINavigationItem, initWithTitle, 0)
  ADD_UNARY_METHOD(UINavigationItem, setTitle, 0)
  ADD_UNARY_METHOD(UINavigationItem, setPrompt, 0)

  NEW_RECEIVER(UIAlertView)
  const IdentifierInfo *initWithTitleUIAlertView[] = {
      &Ctx.Idents.get("initWithTitle"), &Ctx.Idents.get("message"),
      &Ctx.Idents.get("delegate"), &Ctx.Idents.get("cancelButtonTitle"),
      &Ctx.Idents.get("otherButtonTitles")};
  ADD_METHOD(UIAlertView, initWithTitleUIAlertView, 5, 0)
  ADD_UNARY_METHOD(UIAlertView, addButtonWithTitle, 0)
  ADD_UNARY_METHOD(UIAlertView, setTitle, 0)
  ADD_UNARY_METHOD(UIAlertView, setMessage, 0)

  NEW_RECEIVER(NSFormCell)
  ADD_UNARY_METHOD(NSFormCell, initTextCell, 0)
  ADD_UNARY_METHOD(NSFormCell, setTitle, 0)
  ADD_UNARY_METHOD(NSFormCell, setPlaceholderString, 0)

  NEW_RECEIVER(NSUserNotification)
  ADD_UNARY_METHOD(NSUserNotification, setTitle, 0)
  ADD_UNARY_METHOD(NSUserNotification, setSubtitle, 0)
  ADD_UNARY_METHOD(NSUserNotification, setInformativeText, 0)
  ADD_UNARY_METHOD(NSUserNotification, setActionButtonTitle, 0)
  ADD_UNARY_METHOD(NSUserNotification, setOtherButtonTitle, 0)
  ADD_UNARY_METHOD(NSUserNotification, setResponsePlaceholder, 0)

  NEW_RECEIVER(NSToolbarItem)
  ADD_UNARY_METHOD(NSToolbarItem, setLabel, 0)
  ADD_UNARY_METHOD(NSToolbarItem, setPaletteLabel, 0)
  ADD_UNARY_METHOD(NSToolbarItem, setToolTip, 0)

  NEW_RECEIVER(NSProgress)
  ADD_UNARY_METHOD(NSProgress, setLocalizedDescription, 0)
  ADD_UNARY_METHOD(NSProgress, setLocalizedAdditionalDescription, 0)

  NEW_RECEIVER(NSSegmentedCell)
  const IdentifierInfo *setLabelNSSegmentedCell[] = {
      &Ctx.Idents.get("setLabel"), &Ctx.Idents.get("forSegment")};
  ADD_METHOD(NSSegmentedCell, setLabelNSSegmentedCell, 2, 0)
  const IdentifierInfo *setToolTipNSSegmentedCell[] = {
      &Ctx.Idents.get("setToolTip"), &Ctx.Idents.get("forSegment")};
  ADD_METHOD(NSSegmentedCell, setToolTipNSSegmentedCell, 2, 0)

  NEW_RECEIVER(NSUndoManager)
  ADD_UNARY_METHOD(NSUndoManager, setActionName, 0)
  ADD_UNARY_METHOD(NSUndoManager, undoMenuTitleForUndoActionName, 0)
  ADD_UNARY_METHOD(NSUndoManager, redoMenuTitleForUndoActionName, 0)

  NEW_RECEIVER(NSMenuItem)
  const IdentifierInfo *initWithTitleNSMenuItem[] = {
      &Ctx.Idents.get("initWithTitle"), &Ctx.Idents.get("action"),
      &Ctx.Idents.get("keyEquivalent")};
  ADD_METHOD(NSMenuItem, initWithTitleNSMenuItem, 3, 0)
  ADD_UNARY_METHOD(NSMenuItem, setTitle, 0)
  ADD_UNARY_METHOD(NSMenuItem, setToolTip, 0)

  NEW_RECEIVER(NSPopUpButtonCell)
  const IdentifierInfo *initTextCellNSPopUpButtonCell[] = {
      &Ctx.Idents.get("initTextCell"), &Ctx.Idents.get("pullsDown")};
  ADD_METHOD(NSPopUpButtonCell, initTextCellNSPopUpButtonCell, 2, 0)
  ADD_UNARY_METHOD(NSPopUpButtonCell, addItemWithTitle, 0)
  const IdentifierInfo *insertItemWithTitleNSPopUpButtonCell[] = {
      &Ctx.Idents.get("insertItemWithTitle"), &Ctx.Idents.get("atIndex")};
  ADD_METHOD(NSPopUpButtonCell, insertItemWithTitleNSPopUpButtonCell, 2, 0)
  ADD_UNARY_METHOD(NSPopUpButtonCell, removeItemWithTitle, 0)
  ADD_UNARY_METHOD(NSPopUpButtonCell, selectItemWithTitle, 0)
  ADD_UNARY_METHOD(NSPopUpButtonCell, setTitle, 0)

  NEW_RECEIVER(NSViewController)
  ADD_UNARY_METHOD(NSViewController, setTitle, 0)

  NEW_RECEIVER(NSMenu)
  ADD_UNARY_METHOD(NSMenu, initWithTitle, 0)
  const IdentifierInfo *insertItemWithTitleNSMenu[] = {
      &Ctx.Idents.get("insertItemWithTitle"), &Ctx.Idents.get("action"),
      &Ctx.Idents.get("keyEquivalent"), &Ctx.Idents.get("atIndex")};
  ADD_METHOD(NSMenu, insertItemWithTitleNSMenu, 4, 0)
  const IdentifierInfo *addItemWithTitleNSMenu[] = {
      &Ctx.Idents.get("addItemWithTitle"), &Ctx.Idents.get("action"),
      &Ctx.Idents.get("keyEquivalent")};
  ADD_METHOD(NSMenu, addItemWithTitleNSMenu, 3, 0)
  ADD_UNARY_METHOD(NSMenu, setTitle, 0)

  NEW_RECEIVER(UIMutableUserNotificationAction)
  ADD_UNARY_METHOD(UIMutableUserNotificationAction, setTitle, 0)

  NEW_RECEIVER(NSForm)
  ADD_UNARY_METHOD(NSForm, addEntry, 0)
  const IdentifierInfo *insertEntryNSForm[] = {&Ctx.Idents.get("insertEntry"),
                                               &Ctx.Idents.get("atIndex")};
  ADD_METHOD(NSForm, insertEntryNSForm, 2, 0)

  NEW_RECEIVER(NSTextFieldCell)
  ADD_UNARY_METHOD(NSTextFieldCell, setPlaceholderString, 0)

  NEW_RECEIVER(NSUserNotificationAction)
  const IdentifierInfo *actionWithIdentifierNSUserNotificationAction[] = {
      &Ctx.Idents.get("actionWithIdentifier"), &Ctx.Idents.get("title")};
  ADD_METHOD(NSUserNotificationAction,
             actionWithIdentifierNSUserNotificationAction, 2, 1)

  NEW_RECEIVER(UITextField)
  ADD_UNARY_METHOD(UITextField, setText, 0)
  ADD_UNARY_METHOD(UITextField, setPlaceholder, 0)

  NEW_RECEIVER(UIBarButtonItem)
  const IdentifierInfo *initWithTitleUIBarButtonItem[] = {
      &Ctx.Idents.get("initWithTitle"), &Ctx.Idents.get("style"),
      &Ctx.Idents.get("target"), &Ctx.Idents.get("action")};
  ADD_METHOD(UIBarButtonItem, initWithTitleUIBarButtonItem, 4, 0)

  NEW_RECEIVER(UIViewController)
  ADD_UNARY_METHOD(UIViewController, setTitle, 0)

  NEW_RECEIVER(UISegmentedControl)
  const IdentifierInfo *insertSegmentWithTitleUISegmentedControl[] = {
      &Ctx.Idents.get("insertSegmentWithTitle"), &Ctx.Idents.get("atIndex"),
      &Ctx.Idents.get("animated")};
  ADD_METHOD(UISegmentedControl, insertSegmentWithTitleUISegmentedControl, 3, 0)
  const IdentifierInfo *setTitleUISegmentedControl[] = {
      &Ctx.Idents.get("setTitle"), &Ctx.Idents.get("forSegmentAtIndex")};
  ADD_METHOD(UISegmentedControl, setTitleUISegmentedControl, 2, 0)

  NEW_RECEIVER(NSAccessibilityCustomRotorItemResult)
  const IdentifierInfo
      *initWithItemLoadingTokenNSAccessibilityCustomRotorItemResult[] = {
          &Ctx.Idents.get("initWithItemLoadingToken"),
          &Ctx.Idents.get("customLabel")};
  ADD_METHOD(NSAccessibilityCustomRotorItemResult,
             initWithItemLoadingTokenNSAccessibilityCustomRotorItemResult, 2, 1)
  ADD_UNARY_METHOD(NSAccessibilityCustomRotorItemResult, setCustomLabel, 0)

  NEW_RECEIVER(UIContextualAction)
  const IdentifierInfo *contextualActionWithStyleUIContextualAction[] = {
      &Ctx.Idents.get("contextualActionWithStyle"), &Ctx.Idents.get("title"),
      &Ctx.Idents.get("handler")};
  ADD_METHOD(UIContextualAction, contextualActionWithStyleUIContextualAction, 3,
             1)
  ADD_UNARY_METHOD(UIContextualAction, setTitle, 0)

  NEW_RECEIVER(NSAccessibilityCustomRotor)
  const IdentifierInfo *initWithLabelNSAccessibilityCustomRotor[] = {
      &Ctx.Idents.get("initWithLabel"), &Ctx.Idents.get("itemSearchDelegate")};
  ADD_METHOD(NSAccessibilityCustomRotor,
             initWithLabelNSAccessibilityCustomRotor, 2, 0)
  ADD_UNARY_METHOD(NSAccessibilityCustomRotor, setLabel, 0)

  NEW_RECEIVER(NSWindowTab)
  ADD_UNARY_METHOD(NSWindowTab, setTitle, 0)
  ADD_UNARY_METHOD(NSWindowTab, setToolTip, 0)

  NEW_RECEIVER(NSAccessibilityCustomAction)
  const IdentifierInfo *initWithNameNSAccessibilityCustomAction[] = {
      &Ctx.Idents.get("initWithName"), &Ctx.Idents.get("handler")};
  ADD_METHOD(NSAccessibilityCustomAction,
             initWithNameNSAccessibilityCustomAction, 2, 0)
  const IdentifierInfo *initWithNameTargetNSAccessibilityCustomAction[] = {
      &Ctx.Idents.get("initWithName"), &Ctx.Idents.get("target"),
      &Ctx.Idents.get("selector")};
  ADD_METHOD(NSAccessibilityCustomAction,
             initWithNameTargetNSAccessibilityCustomAction, 3, 0)
  ADD_UNARY_METHOD(NSAccessibilityCustomAction, setName, 0)
}

#define LSF_INSERT(function_name) LSF.insert(&Ctx.Idents.get(function_name));
#define LSM_INSERT_NULLARY(receiver, method_name)                              \
  LSM.insert({&Ctx.Idents.get(receiver), Ctx.Selectors.getNullarySelector(     \
                                             &Ctx.Idents.get(method_name))});
#define LSM_INSERT_UNARY(receiver, method_name)                                \
  LSM.insert({&Ctx.Idents.get(receiver),                                       \
              Ctx.Selectors.getUnarySelector(&Ctx.Idents.get(method_name))});
#define LSM_INSERT_SELECTOR(receiver, method_list, arguments)                  \
  LSM.insert({&Ctx.Idents.get(receiver),                                       \
              Ctx.Selectors.getSelector(arguments, method_list)});

/// Initializes a list of methods and C functions that return a localized string
void NonLocalizedStringChecker::initLocStringsMethods(ASTContext &Ctx) const {
  if (!LSM.empty())
    return;

  const IdentifierInfo *LocalizedStringMacro[] = {
      &Ctx.Idents.get("localizedStringForKey"), &Ctx.Idents.get("value"),
      &Ctx.Idents.get("table")};
  LSM_INSERT_SELECTOR("NSBundle", LocalizedStringMacro, 3)
  LSM_INSERT_UNARY("NSDateFormatter", "stringFromDate")
  const IdentifierInfo *LocalizedStringFromDate[] = {
      &Ctx.Idents.get("localizedStringFromDate"), &Ctx.Idents.get("dateStyle"),
      &Ctx.Idents.get("timeStyle")};
  LSM_INSERT_SELECTOR("NSDateFormatter", LocalizedStringFromDate, 3)
  LSM_INSERT_UNARY("NSNumberFormatter", "stringFromNumber")
  LSM_INSERT_NULLARY("UITextField", "text")
  LSM_INSERT_NULLARY("UITextView", "text")
  LSM_INSERT_NULLARY("UILabel", "text")

  LSF_INSERT("CFDateFormatterCreateStringWithDate");
  LSF_INSERT("CFDateFormatterCreateStringWithAbsoluteTime");
  LSF_INSERT("CFNumberFormatterCreateStringWithNumber");
}

/// Checks to see if the method / function declaration includes
/// __attribute__((annotate("returns_localized_nsstring")))
bool NonLocalizedStringChecker::isAnnotatedAsReturningLocalized(
    const Decl *D) const {
  if (!D)
    return false;
  return std::any_of(
      D->specific_attr_begin<AnnotateAttr>(),
      D->specific_attr_end<AnnotateAttr>(), [](const AnnotateAttr *Ann) {
        return Ann->getAnnotation() == "returns_localized_nsstring";
      });
}

/// Checks to see if the method / function declaration includes
/// __attribute__((annotate("takes_localized_nsstring")))
bool NonLocalizedStringChecker::isAnnotatedAsTakingLocalized(
    const Decl *D) const {
  if (!D)
    return false;
  return std::any_of(
      D->specific_attr_begin<AnnotateAttr>(),
      D->specific_attr_end<AnnotateAttr>(), [](const AnnotateAttr *Ann) {
        return Ann->getAnnotation() == "takes_localized_nsstring";
      });
}

/// Returns true if the given SVal is marked as Localized in the program state
bool NonLocalizedStringChecker::hasLocalizedState(SVal S,
                                                  CheckerContext &C) const {
  const MemRegion *mt = S.getAsRegion();
  if (mt) {
    const LocalizedState *LS = C.getState()->get<LocalizedMemMap>(mt);
    if (LS && LS->isLocalized())
      return true;
  }
  return false;
}

/// Returns true if the given SVal is marked as NonLocalized in the program
/// state
bool NonLocalizedStringChecker::hasNonLocalizedState(SVal S,
                                                     CheckerContext &C) const {
  const MemRegion *mt = S.getAsRegion();
  if (mt) {
    const LocalizedState *LS = C.getState()->get<LocalizedMemMap>(mt);
    if (LS && LS->isNonLocalized())
      return true;
  }
  return false;
}

/// Marks the given SVal as Localized in the program state
void NonLocalizedStringChecker::setLocalizedState(const SVal S,
                                                  CheckerContext &C) const {
  const MemRegion *mt = S.getAsRegion();
  if (mt) {
    ProgramStateRef State =
        C.getState()->set<LocalizedMemMap>(mt, LocalizedState::getLocalized());
    C.addTransition(State);
  }
}

/// Marks the given SVal as NonLocalized in the program state
void NonLocalizedStringChecker::setNonLocalizedState(const SVal S,
                                                     CheckerContext &C) const {
  const MemRegion *mt = S.getAsRegion();
  if (mt) {
    ProgramStateRef State = C.getState()->set<LocalizedMemMap>(
        mt, LocalizedState::getNonLocalized());
    C.addTransition(State);
  }
}


static bool isDebuggingName(std::string name) {
  return StringRef(name).contains_insensitive("debug");
}

/// Returns true when, heuristically, the analyzer may be analyzing debugging
/// code. We use this to suppress localization diagnostics in un-localized user
/// interfaces that are only used for debugging and are therefore not user
/// facing.
static bool isDebuggingContext(CheckerContext &C) {
  const Decl *D = C.getCurrentAnalysisDeclContext()->getDecl();
  if (!D)
    return false;

  if (auto *ND = dyn_cast<NamedDecl>(D)) {
    if (isDebuggingName(ND->getNameAsString()))
      return true;
  }

  const DeclContext *DC = D->getDeclContext();

  if (auto *CD = dyn_cast<ObjCContainerDecl>(DC)) {
    if (isDebuggingName(CD->getNameAsString()))
      return true;
  }

  return false;
}


/// Reports a localization error for the passed in method call and SVal
void NonLocalizedStringChecker::reportLocalizationError(
    SVal S, const CallEvent &M, CheckerContext &C, int argumentNumber) const {

  // Don't warn about localization errors in classes and methods that
  // may be debug code.
  if (isDebuggingContext(C))
    return;

  static CheckerProgramPointTag Tag("NonLocalizedStringChecker",
                                    "UnlocalizedString");
  ExplodedNode *ErrNode = C.addTransition(C.getState(), C.getPredecessor(), &Tag);

  if (!ErrNode)
    return;

  // Generate the bug report.
  auto R = std::make_unique<PathSensitiveBugReport>(
      BT, "User-facing text should use localized string macro", ErrNode);
  if (argumentNumber) {
    R->addRange(M.getArgExpr(argumentNumber - 1)->getSourceRange());
  } else {
    R->addRange(M.getSourceRange());
  }
  R->markInteresting(S);

  const MemRegion *StringRegion = S.getAsRegion();
  if (StringRegion)
    R->addVisitor(std::make_unique<NonLocalizedStringBRVisitor>(StringRegion));

  C.emitReport(std::move(R));
}

/// Returns the argument number requiring localized string if it exists
/// otherwise, returns -1
int NonLocalizedStringChecker::getLocalizedArgumentForSelector(
    const IdentifierInfo *Receiver, Selector S) const {
  auto method = UIMethods.find(Receiver);

  if (method == UIMethods.end())
    return -1;

  auto argumentIterator = method->getSecond().find(S);

  if (argumentIterator == method->getSecond().end())
    return -1;

  int argumentNumber = argumentIterator->getSecond();
  return argumentNumber;
}

/// Check if the string being passed in has NonLocalized state
void NonLocalizedStringChecker::checkPreObjCMessage(const ObjCMethodCall &msg,
                                                    CheckerContext &C) const {
  initUIMethods(C.getASTContext());

  const ObjCInterfaceDecl *OD = msg.getReceiverInterface();
  if (!OD)
    return;
  const IdentifierInfo *odInfo = OD->getIdentifier();

  Selector S = msg.getSelector();

  std::string SelectorString = S.getAsString();
  StringRef SelectorName = SelectorString;
  assert(!SelectorName.empty());

  if (odInfo->isStr("NSString")) {
    // Handle the case where the receiver is an NSString
    // These special NSString methods draw to the screen

    if (!(SelectorName.starts_with("drawAtPoint") ||
          SelectorName.starts_with("drawInRect") ||
          SelectorName.starts_with("drawWithRect")))
      return;

    SVal svTitle = msg.getReceiverSVal();

    bool isNonLocalized = hasNonLocalizedState(svTitle, C);

    if (isNonLocalized) {
      reportLocalizationError(svTitle, msg, C);
    }
  }

  int argumentNumber = getLocalizedArgumentForSelector(odInfo, S);
  // Go up each hierarchy of superclasses and their protocols
  while (argumentNumber < 0 && OD->getSuperClass() != nullptr) {
    for (const auto *P : OD->all_referenced_protocols()) {
      argumentNumber = getLocalizedArgumentForSelector(P->getIdentifier(), S);
      if (argumentNumber >= 0)
        break;
    }
    if (argumentNumber < 0) {
      OD = OD->getSuperClass();
      argumentNumber = getLocalizedArgumentForSelector(OD->getIdentifier(), S);
    }
  }

  if (argumentNumber < 0) { // There was no match in UIMethods
    if (const Decl *D = msg.getDecl()) {
      if (const ObjCMethodDecl *OMD = dyn_cast_or_null<ObjCMethodDecl>(D)) {
        for (auto [Idx, FormalParam] : llvm::enumerate(OMD->parameters())) {
          if (isAnnotatedAsTakingLocalized(FormalParam)) {
            argumentNumber = Idx;
            break;
          }
        }
      }
    }
  }

  if (argumentNumber < 0) // Still no match
    return;

  SVal svTitle = msg.getArgSVal(argumentNumber);

  if (const ObjCStringRegion *SR =
          dyn_cast_or_null<ObjCStringRegion>(svTitle.getAsRegion())) {
    StringRef stringValue =
        SR->getObjCStringLiteral()->getString()->getString();
    if ((stringValue.trim().size() == 0 && stringValue.size() > 0) ||
        stringValue.empty())
      return;
    if (!IsAggressive && llvm::sys::unicode::columnWidthUTF8(stringValue) < 2)
      return;
  }

  bool isNonLocalized = hasNonLocalizedState(svTitle, C);

  if (isNonLocalized) {
    reportLocalizationError(svTitle, msg, C, argumentNumber + 1);
  }
}

void NonLocalizedStringChecker::checkPreCall(const CallEvent &Call,
                                             CheckerContext &C) const {
  const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!FD)
    return;

  auto formals = FD->parameters();
  for (unsigned i = 0, ei = std::min(static_cast<unsigned>(formals.size()),
                                     Call.getNumArgs()); i != ei; ++i) {
    if (isAnnotatedAsTakingLocalized(formals[i])) {
      auto actual = Call.getArgSVal(i);
      if (hasNonLocalizedState(actual, C)) {
        reportLocalizationError(actual, Call, C, i + 1);
      }
    }
  }
}

static inline bool isNSStringType(QualType T, ASTContext &Ctx) {

  const ObjCObjectPointerType *PT = T->getAs<ObjCObjectPointerType>();
  if (!PT)
    return false;

  ObjCInterfaceDecl *Cls = PT->getObjectType()->getInterface();
  if (!Cls)
    return false;

  const IdentifierInfo *ClsName = Cls->getIdentifier();

  // FIXME: Should we walk the chain of classes?
  return ClsName == &Ctx.Idents.get("NSString") ||
         ClsName == &Ctx.Idents.get("NSMutableString");
}

/// Marks a string being returned by any call as localized
/// if it is in LocStringFunctions (LSF) or the function is annotated.
/// Otherwise, we mark it as NonLocalized (Aggressive) or
/// NonLocalized only if it is not backed by a SymRegion (Non-Aggressive),
/// basically leaving only string literals as NonLocalized.
void NonLocalizedStringChecker::checkPostCall(const CallEvent &Call,
                                              CheckerContext &C) const {
  initLocStringsMethods(C.getASTContext());

  if (!Call.getOriginExpr())
    return;

  // Anything that takes in a localized NSString as an argument
  // and returns an NSString will be assumed to be returning a
  // localized NSString. (Counter: Incorrectly combining two LocalizedStrings)
  const QualType RT = Call.getResultType();
  if (isNSStringType(RT, C.getASTContext())) {
    for (unsigned i = 0; i < Call.getNumArgs(); ++i) {
      SVal argValue = Call.getArgSVal(i);
      if (hasLocalizedState(argValue, C)) {
        SVal sv = Call.getReturnValue();
        setLocalizedState(sv, C);
        return;
      }
    }
  }

  const Decl *D = Call.getDecl();
  if (!D)
    return;

  const IdentifierInfo *Identifier = Call.getCalleeIdentifier();

  SVal sv = Call.getReturnValue();
  if (isAnnotatedAsReturningLocalized(D) || LSF.contains(Identifier)) {
    setLocalizedState(sv, C);
  } else if (isNSStringType(RT, C.getASTContext()) &&
             !hasLocalizedState(sv, C)) {
    if (IsAggressive) {
      setNonLocalizedState(sv, C);
    } else {
      const SymbolicRegion *SymReg =
          dyn_cast_or_null<SymbolicRegion>(sv.getAsRegion());
      if (!SymReg)
        setNonLocalizedState(sv, C);
    }
  }
}

/// Marks a string being returned by an ObjC method as localized
/// if it is in LocStringMethods or the method is annotated
void NonLocalizedStringChecker::checkPostObjCMessage(const ObjCMethodCall &msg,
                                                     CheckerContext &C) const {
  initLocStringsMethods(C.getASTContext());

  if (!msg.isInstanceMessage())
    return;

  const ObjCInterfaceDecl *OD = msg.getReceiverInterface();
  if (!OD)
    return;
  const IdentifierInfo *odInfo = OD->getIdentifier();

  Selector S = msg.getSelector();
  std::string SelectorName = S.getAsString();

  std::pair<const IdentifierInfo *, Selector> MethodDescription = {odInfo, S};

  if (LSM.count(MethodDescription) ||
      isAnnotatedAsReturningLocalized(msg.getDecl())) {
    SVal sv = msg.getReturnValue();
    setLocalizedState(sv, C);
  }
}

/// Marks all empty string literals as localized
void NonLocalizedStringChecker::checkPostStmt(const ObjCStringLiteral *SL,
                                              CheckerContext &C) const {
  SVal sv = C.getSVal(SL);
  setNonLocalizedState(sv, C);
}

PathDiagnosticPieceRef
NonLocalizedStringBRVisitor::VisitNode(const ExplodedNode *Succ,
                                       BugReporterContext &BRC,
                                       PathSensitiveBugReport &BR) {
  if (Satisfied)
    return nullptr;

  std::optional<StmtPoint> Point = Succ->getLocation().getAs<StmtPoint>();
  if (!Point)
    return nullptr;

  auto *LiteralExpr = dyn_cast<ObjCStringLiteral>(Point->getStmt());
  if (!LiteralExpr)
    return nullptr;

  SVal LiteralSVal = Succ->getSVal(LiteralExpr);
  if (LiteralSVal.getAsRegion() != NonLocalizedString)
    return nullptr;

  Satisfied = true;

  PathDiagnosticLocation L =
      PathDiagnosticLocation::create(*Point, BRC.getSourceManager());

  if (!L.isValid() || !L.asLocation().isValid())
    return nullptr;

  auto Piece = std::make_shared<PathDiagnosticEventPiece>(
      L, "Non-localized string literal here");
  Piece->addRange(LiteralExpr->getSourceRange());

  return std::move(Piece);
}

namespace {
class EmptyLocalizationContextChecker
    : public Checker<check::ASTDecl<ObjCImplementationDecl>> {

  // A helper class, which walks the AST
  class MethodCrawler : public ConstStmtVisitor<MethodCrawler> {
    const ObjCMethodDecl *MD;
    BugReporter &BR;
    AnalysisManager &Mgr;
    const CheckerBase *Checker;
    LocationOrAnalysisDeclContext DCtx;

  public:
    MethodCrawler(const ObjCMethodDecl *InMD, BugReporter &InBR,
                  const CheckerBase *Checker, AnalysisManager &InMgr,
                  AnalysisDeclContext *InDCtx)
        : MD(InMD), BR(InBR), Mgr(InMgr), Checker(Checker), DCtx(InDCtx) {}

    void VisitStmt(const Stmt *S) { VisitChildren(S); }

    void VisitObjCMessageExpr(const ObjCMessageExpr *ME);

    void reportEmptyContextError(const ObjCMessageExpr *M) const;

    void VisitChildren(const Stmt *S) {
      for (const Stmt *Child : S->children()) {
        if (Child)
          this->Visit(Child);
      }
    }
  };

public:
  void checkASTDecl(const ObjCImplementationDecl *D, AnalysisManager &Mgr,
                    BugReporter &BR) const;
};
} // end anonymous namespace

void EmptyLocalizationContextChecker::checkASTDecl(
    const ObjCImplementationDecl *D, AnalysisManager &Mgr,
    BugReporter &BR) const {

  for (const ObjCMethodDecl *M : D->methods()) {
    AnalysisDeclContext *DCtx = Mgr.getAnalysisDeclContext(M);

    const Stmt *Body = M->getBody();
    if (!Body) {
      assert(M->isSynthesizedAccessorStub());
      continue;
    }

    MethodCrawler MC(M->getCanonicalDecl(), BR, this, Mgr, DCtx);
    MC.VisitStmt(Body);
  }
}

/// This check attempts to match these macros, assuming they are defined as
/// follows:
///
/// #define NSLocalizedString(key, comment) \
/// [[NSBundle mainBundle] localizedStringForKey:(key) value:@"" table:nil]
/// #define NSLocalizedStringFromTable(key, tbl, comment) \
/// [[NSBundle mainBundle] localizedStringForKey:(key) value:@"" table:(tbl)]
/// #define NSLocalizedStringFromTableInBundle(key, tbl, bundle, comment) \
/// [bundle localizedStringForKey:(key) value:@"" table:(tbl)]
/// #define NSLocalizedStringWithDefaultValue(key, tbl, bundle, val, comment)
///
/// We cannot use the path sensitive check because the macro argument we are
/// checking for (comment) is not used and thus not present in the AST,
/// so we use Lexer on the original macro call and retrieve the value of
/// the comment. If it's empty or nil, we raise a warning.
void EmptyLocalizationContextChecker::MethodCrawler::VisitObjCMessageExpr(
    const ObjCMessageExpr *ME) {

  // FIXME: We may be able to use PPCallbacks to check for empty context
  // comments as part of preprocessing and avoid this re-lexing hack.
  const ObjCInterfaceDecl *OD = ME->getReceiverInterface();
  if (!OD)
    return;

  const IdentifierInfo *odInfo = OD->getIdentifier();

  if (!(odInfo->isStr("NSBundle") &&
        ME->getSelector().getAsString() ==
            "localizedStringForKey:value:table:")) {
    return;
  }

  SourceRange R = ME->getSourceRange();
  if (!R.getBegin().isMacroID())
    return;

  // getImmediateMacroCallerLoc gets the location of the immediate macro
  // caller, one level up the stack toward the initial macro typed into the
  // source, so SL should point to the NSLocalizedString macro.
  SourceLocation SL =
      Mgr.getSourceManager().getImmediateMacroCallerLoc(R.getBegin());
  std::pair<FileID, unsigned> SLInfo =
      Mgr.getSourceManager().getDecomposedLoc(SL);

  SrcMgr::SLocEntry SE = Mgr.getSourceManager().getSLocEntry(SLInfo.first);

  // If NSLocalizedString macro is wrapped in another macro, we need to
  // unwrap the expansion until we get to the NSLocalizedStringMacro.
  while (SE.isExpansion()) {
    SL = SE.getExpansion().getSpellingLoc();
    SLInfo = Mgr.getSourceManager().getDecomposedLoc(SL);
    SE = Mgr.getSourceManager().getSLocEntry(SLInfo.first);
  }

  std::optional<llvm::MemoryBufferRef> BF =
      Mgr.getSourceManager().getBufferOrNone(SLInfo.first, SL);
  if (!BF)
    return;
  LangOptions LangOpts;
  Lexer TheLexer(SL, LangOpts, BF->getBufferStart(),
                 BF->getBufferStart() + SLInfo.second, BF->getBufferEnd());

  Token I;
  Token Result;    // This will hold the token just before the last ')'
  int p_count = 0; // This is for parenthesis matching
  while (!TheLexer.LexFromRawLexer(I)) {
    if (I.getKind() == tok::l_paren)
      ++p_count;
    if (I.getKind() == tok::r_paren) {
      if (p_count == 1)
        break;
      --p_count;
    }
    Result = I;
  }

  if (isAnyIdentifier(Result.getKind())) {
    if (Result.getRawIdentifier() == "nil") {
      reportEmptyContextError(ME);
      return;
    }
  }

  if (!isStringLiteral(Result.getKind()))
    return;

  StringRef Comment =
      StringRef(Result.getLiteralData(), Result.getLength()).trim('"');

  if ((Comment.trim().size() == 0 && Comment.size() > 0) || // Is Whitespace
      Comment.empty()) {
    reportEmptyContextError(ME);
  }
}

void EmptyLocalizationContextChecker::MethodCrawler::reportEmptyContextError(
    const ObjCMessageExpr *ME) const {
  // Generate the bug report.
  BR.EmitBasicReport(MD, Checker, "Context Missing",
                     "Localizability Issue (Apple)",
                     "Localized string macro should include a non-empty "
                     "comment for translators",
                     PathDiagnosticLocation(ME, BR.getSourceManager(), DCtx));
}

namespace {
class PluralMisuseChecker : public Checker<check::ASTCodeBody> {

  // A helper class, which walks the AST
  class MethodCrawler : public RecursiveASTVisitor<MethodCrawler> {
    BugReporter &BR;
    const CheckerBase *Checker;
    AnalysisDeclContext *AC;

    // This functions like a stack. We push on any IfStmt or
    // ConditionalOperator that matches the condition
    // and pop it off when we leave that statement
    llvm::SmallVector<const clang::Stmt *, 8> MatchingStatements;
    // This is true when we are the direct-child of a
    // matching statement
    bool InMatchingStatement = false;

  public:
    explicit MethodCrawler(BugReporter &InBR, const CheckerBase *Checker,
                           AnalysisDeclContext *InAC)
        : BR(InBR), Checker(Checker), AC(InAC) {}

    bool VisitIfStmt(const IfStmt *I);
    bool EndVisitIfStmt(IfStmt *I);
    bool TraverseIfStmt(IfStmt *x);
    bool VisitConditionalOperator(const ConditionalOperator *C);
    bool TraverseConditionalOperator(ConditionalOperator *C);
    bool VisitCallExpr(const CallExpr *CE);
    bool VisitObjCMessageExpr(const ObjCMessageExpr *ME);

  private:
    void reportPluralMisuseError(const Stmt *S) const;
    bool isCheckingPlurality(const Expr *E) const;
  };

public:
  void checkASTCodeBody(const Decl *D, AnalysisManager &Mgr,
                        BugReporter &BR) const {
    MethodCrawler Visitor(BR, this, Mgr.getAnalysisDeclContext(D));
    Visitor.TraverseDecl(const_cast<Decl *>(D));
  }
};
} // end anonymous namespace

// Checks the condition of the IfStmt and returns true if one
// of the following heuristics are met:
// 1) The conidtion is a variable with "singular" or "plural" in the name
// 2) The condition is a binary operator with 1 or 2 on the right-hand side
bool PluralMisuseChecker::MethodCrawler::isCheckingPlurality(
    const Expr *Condition) const {
  const BinaryOperator *BO = nullptr;
  // Accounts for when a VarDecl represents a BinaryOperator
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Condition)) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      const Expr *InitExpr = VD->getInit();
      if (InitExpr) {
        if (const BinaryOperator *B =
                dyn_cast<BinaryOperator>(InitExpr->IgnoreParenImpCasts())) {
          BO = B;
        }
      }
      if (VD->getName().contains_insensitive("plural") ||
          VD->getName().contains_insensitive("singular")) {
        return true;
      }
    }
  } else if (const BinaryOperator *B = dyn_cast<BinaryOperator>(Condition)) {
    BO = B;
  }

  if (BO == nullptr)
    return false;

  if (IntegerLiteral *IL = dyn_cast_or_null<IntegerLiteral>(
          BO->getRHS()->IgnoreParenImpCasts())) {
    llvm::APInt Value = IL->getValue();
    if (Value == 1 || Value == 2) {
      return true;
    }
  }
  return false;
}

// A CallExpr with "LOC" in its identifier that takes in a string literal
// has been shown to almost always be a function that returns a localized
// string. Raise a diagnostic when this is in a statement that matches
// the condition.
bool PluralMisuseChecker::MethodCrawler::VisitCallExpr(const CallExpr *CE) {
  if (InMatchingStatement) {
    if (const FunctionDecl *FD = CE->getDirectCallee()) {
      std::string NormalizedName =
          StringRef(FD->getNameInfo().getAsString()).lower();
      if (NormalizedName.find("loc") != std::string::npos) {
        for (const Expr *Arg : CE->arguments()) {
          if (isa<ObjCStringLiteral>(Arg))
            reportPluralMisuseError(CE);
        }
      }
    }
  }
  return true;
}

// The other case is for NSLocalizedString which also returns
// a localized string. It's a macro for the ObjCMessageExpr
// [NSBundle localizedStringForKey:value:table:] Raise a
// diagnostic when this is in a statement that matches
// the condition.
bool PluralMisuseChecker::MethodCrawler::VisitObjCMessageExpr(
    const ObjCMessageExpr *ME) {
  const ObjCInterfaceDecl *OD = ME->getReceiverInterface();
  if (!OD)
    return true;

  const IdentifierInfo *odInfo = OD->getIdentifier();

  if (odInfo->isStr("NSBundle") &&
      ME->getSelector().getAsString() == "localizedStringForKey:value:table:") {
    if (InMatchingStatement) {
      reportPluralMisuseError(ME);
    }
  }
  return true;
}

/// Override TraverseIfStmt so we know when we are done traversing an IfStmt
bool PluralMisuseChecker::MethodCrawler::TraverseIfStmt(IfStmt *I) {
  RecursiveASTVisitor<MethodCrawler>::TraverseIfStmt(I);
  return EndVisitIfStmt(I);
}

// EndVisit callbacks are not provided by the RecursiveASTVisitor
// so we override TraverseIfStmt and make a call to EndVisitIfStmt
// after traversing the IfStmt
bool PluralMisuseChecker::MethodCrawler::EndVisitIfStmt(IfStmt *I) {
  MatchingStatements.pop_back();
  if (!MatchingStatements.empty()) {
    if (MatchingStatements.back() != nullptr) {
      InMatchingStatement = true;
      return true;
    }
  }
  InMatchingStatement = false;
  return true;
}

bool PluralMisuseChecker::MethodCrawler::VisitIfStmt(const IfStmt *I) {
  const Expr *Condition = I->getCond();
  if (!Condition)
    return true;
  Condition = Condition->IgnoreParenImpCasts();
  if (isCheckingPlurality(Condition)) {
    MatchingStatements.push_back(I);
    InMatchingStatement = true;
  } else {
    MatchingStatements.push_back(nullptr);
    InMatchingStatement = false;
  }

  return true;
}

// Preliminary support for conditional operators.
bool PluralMisuseChecker::MethodCrawler::TraverseConditionalOperator(
    ConditionalOperator *C) {
  RecursiveASTVisitor<MethodCrawler>::TraverseConditionalOperator(C);
  MatchingStatements.pop_back();
  if (!MatchingStatements.empty()) {
    if (MatchingStatements.back() != nullptr)
      InMatchingStatement = true;
    else
      InMatchingStatement = false;
  } else {
    InMatchingStatement = false;
  }
  return true;
}

bool PluralMisuseChecker::MethodCrawler::VisitConditionalOperator(
    const ConditionalOperator *C) {
  const Expr *Condition = C->getCond()->IgnoreParenImpCasts();
  if (isCheckingPlurality(Condition)) {
    MatchingStatements.push_back(C);
    InMatchingStatement = true;
  } else {
    MatchingStatements.push_back(nullptr);
    InMatchingStatement = false;
  }
  return true;
}

void PluralMisuseChecker::MethodCrawler::reportPluralMisuseError(
    const Stmt *S) const {
  // Generate the bug report.
  BR.EmitBasicReport(AC->getDecl(), Checker, "Plural Misuse",
                     "Localizability Issue (Apple)",
                     "Plural cases are not supported across all languages. "
                     "Use a .stringsdict file instead",
                     PathDiagnosticLocation(S, BR.getSourceManager(), AC));
}

//===----------------------------------------------------------------------===//
// Checker registration.
//===----------------------------------------------------------------------===//

void ento::registerNonLocalizedStringChecker(CheckerManager &mgr) {
  NonLocalizedStringChecker *checker =
      mgr.registerChecker<NonLocalizedStringChecker>();
  checker->IsAggressive =
      mgr.getAnalyzerOptions().getCheckerBooleanOption(
          checker, "AggressiveReport");
}

bool ento::shouldRegisterNonLocalizedStringChecker(const CheckerManager &mgr) {
  return true;
}

void ento::registerEmptyLocalizationContextChecker(CheckerManager &mgr) {
  mgr.registerChecker<EmptyLocalizationContextChecker>();
}

bool ento::shouldRegisterEmptyLocalizationContextChecker(
                                                    const CheckerManager &mgr) {
  return true;
}

void ento::registerPluralMisuseChecker(CheckerManager &mgr) {
  mgr.registerChecker<PluralMisuseChecker>();
}

bool ento::shouldRegisterPluralMisuseChecker(const CheckerManager &mgr) {
  return true;
}
