// TODO: header template

#include "clang/AST/OSLog.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/FormatString.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/SmallBitVector.h"
#include <optional>

using namespace clang;

using clang::analyze_os_log::OSLogBufferItem;
using clang::analyze_os_log::OSLogBufferLayout;

namespace {
class OSLogFormatStringHandler
    : public analyze_format_string::FormatStringHandler {
private:
  struct ArgData {
    const Expr *E = nullptr;
    std::optional<OSLogBufferItem::Kind> Kind;
    std::optional<unsigned> Size;
    std::optional<const Expr *> Count;
    std::optional<const Expr *> Precision;
    std::optional<const Expr *> FieldWidth;
    unsigned char Flags = 0;
    StringRef MaskType;
  };
  SmallVector<ArgData, 4> ArgsData;
  ArrayRef<const Expr *> Args;

  OSLogBufferItem::Kind
  getKind(analyze_format_string::ConversionSpecifier::Kind K) {
    switch (K) {
    case clang::analyze_format_string::ConversionSpecifier::sArg: // "%s"
      return OSLogBufferItem::StringKind;
    case clang::analyze_format_string::ConversionSpecifier::SArg: // "%S"
      return OSLogBufferItem::WideStringKind;
    case clang::analyze_format_string::ConversionSpecifier::PArg: { // "%P"
      return OSLogBufferItem::PointerKind;
    case clang::analyze_format_string::ConversionSpecifier::ObjCObjArg: // "%@"
      return OSLogBufferItem::ObjCObjKind;
    case clang::analyze_format_string::ConversionSpecifier::PrintErrno: // "%m"
      return OSLogBufferItem::ErrnoKind;
    default:
      return OSLogBufferItem::ScalarKind;
    }
    }
  }

public:
  OSLogFormatStringHandler(ArrayRef<const Expr *> Args) : Args(Args) {
    ArgsData.reserve(Args.size());
  }

  bool HandlePrintfSpecifier(const analyze_printf::PrintfSpecifier &FS,
                             const char *StartSpecifier, unsigned SpecifierLen,
                             const TargetInfo &) override {
    if (!FS.consumesDataArgument() &&
        FS.getConversionSpecifier().getKind() !=
            clang::analyze_format_string::ConversionSpecifier::PrintErrno)
      return true;

    ArgsData.emplace_back();
    unsigned ArgIndex = FS.getArgIndex();
    if (ArgIndex < Args.size())
      ArgsData.back().E = Args[ArgIndex];

    // First get the Kind
    ArgsData.back().Kind = getKind(FS.getConversionSpecifier().getKind());
    if (ArgsData.back().Kind != OSLogBufferItem::ErrnoKind &&
        !ArgsData.back().E) {
      // missing argument
      ArgsData.pop_back();
      return false;
    }

    switch (FS.getConversionSpecifier().getKind()) {
    case clang::analyze_format_string::ConversionSpecifier::sArg:   // "%s"
    case clang::analyze_format_string::ConversionSpecifier::SArg: { // "%S"
      auto &precision = FS.getPrecision();
      switch (precision.getHowSpecified()) {
      case clang::analyze_format_string::OptionalAmount::NotSpecified: // "%s"
        break;
      case clang::analyze_format_string::OptionalAmount::Constant: // "%.16s"
        ArgsData.back().Size = precision.getConstantAmount();
        break;
      case clang::analyze_format_string::OptionalAmount::Arg: // "%.*s"
        ArgsData.back().Count = Args[precision.getArgIndex()];
        break;
      case clang::analyze_format_string::OptionalAmount::Invalid:
        return false;
      }
      break;
    }
    case clang::analyze_format_string::ConversionSpecifier::PArg: { // "%P"
      auto &precision = FS.getPrecision();
      switch (precision.getHowSpecified()) {
      case clang::analyze_format_string::OptionalAmount::NotSpecified: // "%P"
        return false; // length must be supplied with pointer format specifier
      case clang::analyze_format_string::OptionalAmount::Constant: // "%.16P"
        ArgsData.back().Size = precision.getConstantAmount();
        break;
      case clang::analyze_format_string::OptionalAmount::Arg: // "%.*P"
        ArgsData.back().Count = Args[precision.getArgIndex()];
        break;
      case clang::analyze_format_string::OptionalAmount::Invalid:
        return false;
      }
      break;
    }
    default:
      if (FS.getPrecision().hasDataArgument()) {
        ArgsData.back().Precision = Args[FS.getPrecision().getArgIndex()];
      }
      break;
    }
    if (FS.getFieldWidth().hasDataArgument()) {
      ArgsData.back().FieldWidth = Args[FS.getFieldWidth().getArgIndex()];
    }

    if (FS.isSensitive())
      ArgsData.back().Flags |= OSLogBufferItem::IsSensitive;
    else if (FS.isPrivate())
      ArgsData.back().Flags |= OSLogBufferItem::IsPrivate;
    else if (FS.isPublic())
      ArgsData.back().Flags |= OSLogBufferItem::IsPublic;

    ArgsData.back().MaskType = FS.getMaskType();
    return true;
  }

  void computeLayout(ASTContext &Ctx, OSLogBufferLayout &Layout) const {
    Layout.Items.clear();
    for (auto &Data : ArgsData) {
      if (!Data.MaskType.empty()) {
        CharUnits Size = CharUnits::fromQuantity(8);
        Layout.Items.emplace_back(OSLogBufferItem::MaskKind, nullptr,
                                  Size, 0, Data.MaskType);
      }

      if (Data.FieldWidth) {
        CharUnits Size = Ctx.getTypeSizeInChars((*Data.FieldWidth)->getType());
        Layout.Items.emplace_back(OSLogBufferItem::ScalarKind, *Data.FieldWidth,
                                  Size, 0);
      }
      if (Data.Precision) {
        CharUnits Size = Ctx.getTypeSizeInChars((*Data.Precision)->getType());
        Layout.Items.emplace_back(OSLogBufferItem::ScalarKind, *Data.Precision,
                                  Size, 0);
      }
      if (Data.Count) {
        // "%.*P" has an extra "count" that we insert before the argument.
        CharUnits Size = Ctx.getTypeSizeInChars((*Data.Count)->getType());
        Layout.Items.emplace_back(OSLogBufferItem::CountKind, *Data.Count, Size,
                                  0);
      }
      if (Data.Size)
        Layout.Items.emplace_back(Ctx, CharUnits::fromQuantity(*Data.Size),
                                  Data.Flags);
      if (Data.Kind) {
        CharUnits Size;
        if (*Data.Kind == OSLogBufferItem::ErrnoKind)
          Size = CharUnits::Zero();
        else
          Size = Ctx.getTypeSizeInChars(Data.E->getType());
        Layout.Items.emplace_back(*Data.Kind, Data.E, Size, Data.Flags);
      } else {
        auto Size = Ctx.getTypeSizeInChars(Data.E->getType());
        Layout.Items.emplace_back(OSLogBufferItem::ScalarKind, Data.E, Size,
                                  Data.Flags);
      }
    }
  }
};
} // end anonymous namespace

bool clang::analyze_os_log::computeOSLogBufferLayout(
    ASTContext &Ctx, const CallExpr *E, OSLogBufferLayout &Layout) {
  ArrayRef<const Expr *> Args(E->getArgs(), E->getArgs() + E->getNumArgs());

  const Expr *StringArg;
  ArrayRef<const Expr *> VarArgs;
  switch (E->getBuiltinCallee()) {
  case Builtin::BI__builtin_os_log_format_buffer_size:
    assert(E->getNumArgs() >= 1 &&
           "__builtin_os_log_format_buffer_size takes at least 1 argument");
    StringArg = E->getArg(0);
    VarArgs = Args.slice(1);
    break;
  case Builtin::BI__builtin_os_log_format:
    assert(E->getNumArgs() >= 2 &&
           "__builtin_os_log_format takes at least 2 arguments");
    StringArg = E->getArg(1);
    VarArgs = Args.slice(2);
    break;
  default:
    llvm_unreachable("non-os_log builtin passed to computeOSLogBufferLayout");
  }

  const StringLiteral *Lit = cast<StringLiteral>(StringArg->IgnoreParenCasts());
  assert(Lit && (Lit->isOrdinary() || Lit->isUTF8()));
  StringRef Data = Lit->getString();
  OSLogFormatStringHandler H(VarArgs);
  ParsePrintfString(H, Data.begin(), Data.end(), Ctx.getLangOpts(),
                    Ctx.getTargetInfo(), /*isFreeBSDKPrintf*/ false);

  H.computeLayout(Ctx, Layout);
  return true;
}
