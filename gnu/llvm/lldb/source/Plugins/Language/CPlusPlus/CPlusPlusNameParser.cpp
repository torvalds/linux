//===-- CPlusPlusNameParser.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CPlusPlusNameParser.h"

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/TokenKinds.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Threading.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using ParsedFunction = lldb_private::CPlusPlusNameParser::ParsedFunction;
using ParsedName = lldb_private::CPlusPlusNameParser::ParsedName;
namespace tok = clang::tok;

std::optional<ParsedFunction> CPlusPlusNameParser::ParseAsFunctionDefinition() {
  m_next_token_index = 0;
  std::optional<ParsedFunction> result(std::nullopt);

  // Try to parse the name as function without a return type specified e.g.
  // main(int, char*[])
  {
    Bookmark start_position = SetBookmark();
    result = ParseFunctionImpl(false);
    if (result && !HasMoreTokens())
      return result;
  }

  // Try to parse the name as function with function pointer return type e.g.
  // void (*get_func(const char*))()
  result = ParseFuncPtr(true);
  if (result)
    return result;

  // Finally try to parse the name as a function with non-function return type
  // e.g. int main(int, char*[])
  result = ParseFunctionImpl(true);
  if (HasMoreTokens())
    return std::nullopt;
  return result;
}

std::optional<ParsedName> CPlusPlusNameParser::ParseAsFullName() {
  m_next_token_index = 0;
  std::optional<ParsedNameRanges> name_ranges = ParseFullNameImpl();
  if (!name_ranges)
    return std::nullopt;
  if (HasMoreTokens())
    return std::nullopt;
  ParsedName result;
  result.basename = GetTextForRange(name_ranges->basename_range);
  result.context = GetTextForRange(name_ranges->context_range);
  return result;
}

bool CPlusPlusNameParser::HasMoreTokens() {
  return m_next_token_index < m_tokens.size();
}

void CPlusPlusNameParser::Advance() { ++m_next_token_index; }

void CPlusPlusNameParser::TakeBack() { --m_next_token_index; }

bool CPlusPlusNameParser::ConsumeToken(tok::TokenKind kind) {
  if (!HasMoreTokens())
    return false;

  if (!Peek().is(kind))
    return false;

  Advance();
  return true;
}

template <typename... Ts> bool CPlusPlusNameParser::ConsumeToken(Ts... kinds) {
  if (!HasMoreTokens())
    return false;

  if (!Peek().isOneOf(kinds...))
    return false;

  Advance();
  return true;
}

CPlusPlusNameParser::Bookmark CPlusPlusNameParser::SetBookmark() {
  return Bookmark(m_next_token_index);
}

size_t CPlusPlusNameParser::GetCurrentPosition() { return m_next_token_index; }

clang::Token &CPlusPlusNameParser::Peek() {
  assert(HasMoreTokens());
  return m_tokens[m_next_token_index];
}

std::optional<ParsedFunction>
CPlusPlusNameParser::ParseFunctionImpl(bool expect_return_type) {
  Bookmark start_position = SetBookmark();

  ParsedFunction result;
  if (expect_return_type) {
    size_t return_start = GetCurrentPosition();
    // Consume return type if it's expected.
    if (!ConsumeToken(tok::kw_auto) && !ConsumeTypename())
      return std::nullopt;

    size_t return_end = GetCurrentPosition();
    result.return_type = GetTextForRange(Range(return_start, return_end));
  }

  auto maybe_name = ParseFullNameImpl();
  if (!maybe_name) {
    return std::nullopt;
  }

  size_t argument_start = GetCurrentPosition();
  if (!ConsumeArguments()) {
    return std::nullopt;
  }

  size_t qualifiers_start = GetCurrentPosition();
  SkipFunctionQualifiers();
  size_t end_position = GetCurrentPosition();

  result.name.basename = GetTextForRange(maybe_name->basename_range);
  result.name.context = GetTextForRange(maybe_name->context_range);
  result.arguments = GetTextForRange(Range(argument_start, qualifiers_start));
  result.qualifiers = GetTextForRange(Range(qualifiers_start, end_position));
  start_position.Remove();
  return result;
}

std::optional<ParsedFunction>
CPlusPlusNameParser::ParseFuncPtr(bool expect_return_type) {
  // This function parses a function definition
  // that returns a pointer type.
  // E.g., double (*(*func(long))(int))(float)

  // Step 1:
  // Remove the return type of the innermost
  // function pointer type.
  //
  // Leaves us with:
  //   (*(*func(long))(int))(float)
  Bookmark start_position = SetBookmark();
  if (expect_return_type) {
    // Consume return type.
    if (!ConsumeTypename())
      return std::nullopt;
  }

  // Step 2:
  //
  // Skip a pointer and parenthesis pair.
  //
  // Leaves us with:
  //   (*func(long))(int))(float)
  if (!ConsumeToken(tok::l_paren))
    return std::nullopt;
  if (!ConsumePtrsAndRefs())
    return std::nullopt;

  // Step 3:
  //
  // Consume inner function name. This will fail unless
  // we stripped all the pointers on the left hand side
  // of the function name.
  {
    Bookmark before_inner_function_pos = SetBookmark();
    auto maybe_inner_function_name = ParseFunctionImpl(false);
    if (maybe_inner_function_name)
      if (ConsumeToken(tok::r_paren))
        if (ConsumeArguments()) {
          SkipFunctionQualifiers();
          start_position.Remove();
          before_inner_function_pos.Remove();
          return maybe_inner_function_name;
        }
  }

  // Step 4:
  //
  // Parse the remaining string as a function pointer again.
  // This time don't consume the inner-most typename since
  // we're left with pointers only. This will strip another
  // layer of pointers until we're left with the innermost
  // function name/argument. I.e., func(long))(int))(float)
  //
  // Once we successfully stripped all pointers and gotten
  // the innermost function name from ParseFunctionImpl above,
  // we consume a single ')' and the arguments '(...)' that follows.
  //
  // Leaves us with:
  //   )(float)
  //
  // This is the remnant of the outer function pointers' arguments.
  // Unwinding the recursive calls will remove the remaining
  // arguments.
  auto maybe_inner_function_ptr_name = ParseFuncPtr(false);
  if (maybe_inner_function_ptr_name)
    if (ConsumeToken(tok::r_paren))
      if (ConsumeArguments()) {
        SkipFunctionQualifiers();
        start_position.Remove();
        return maybe_inner_function_ptr_name;
      }

  return std::nullopt;
}

bool CPlusPlusNameParser::ConsumeArguments() {
  return ConsumeBrackets(tok::l_paren, tok::r_paren);
}

bool CPlusPlusNameParser::ConsumeTemplateArgs() {
  Bookmark start_position = SetBookmark();
  if (!HasMoreTokens() || Peek().getKind() != tok::less)
    return false;
  Advance();

  // Consuming template arguments is a bit trickier than consuming function
  // arguments, because '<' '>' brackets are not always trivially balanced. In
  // some rare cases tokens '<' and '>' can appear inside template arguments as
  // arithmetic or shift operators not as template brackets. Examples:
  // std::enable_if<(10u)<(64), bool>
  //           f<A<operator<(X,Y)::Subclass>>
  // Good thing that compiler makes sure that really ambiguous cases of '>'
  // usage should be enclosed within '()' brackets.
  int template_counter = 1;
  bool can_open_template = false;
  while (HasMoreTokens() && template_counter > 0) {
    tok::TokenKind kind = Peek().getKind();
    switch (kind) {
    case tok::greatergreater:
      template_counter -= 2;
      can_open_template = false;
      Advance();
      break;
    case tok::greater:
      --template_counter;
      can_open_template = false;
      Advance();
      break;
    case tok::less:
      // '<' is an attempt to open a subteamplte
      // check if parser is at the point where it's actually possible,
      // otherwise it's just a part of an expression like 'sizeof(T)<(10)'. No
      // need to do the same for '>' because compiler actually makes sure that
      // '>' always surrounded by brackets to avoid ambiguity.
      if (can_open_template)
        ++template_counter;
      can_open_template = false;
      Advance();
      break;
    case tok::kw_operator: // C++ operator overloading.
      if (!ConsumeOperator())
        return false;
      can_open_template = true;
      break;
    case tok::raw_identifier:
      can_open_template = true;
      Advance();
      break;
    case tok::l_square:
      // Handle templates tagged with an ABI tag.
      // An example demangled/prettified version is:
      //   func[abi:tag1][abi:tag2]<type[abi:tag3]>(int)
      if (ConsumeAbiTag())
        can_open_template = true;
      else if (ConsumeBrackets(tok::l_square, tok::r_square))
        can_open_template = false;
      else
        return false;
      break;
    case tok::l_paren:
      if (!ConsumeArguments())
        return false;
      can_open_template = false;
      break;
    default:
      can_open_template = false;
      Advance();
      break;
    }
  }

  if (template_counter != 0) {
    return false;
  }
  start_position.Remove();
  return true;
}

bool CPlusPlusNameParser::ConsumeAbiTag() {
  Bookmark start_position = SetBookmark();
  if (!ConsumeToken(tok::l_square))
    return false;

  if (HasMoreTokens() && Peek().is(tok::raw_identifier) &&
      Peek().getRawIdentifier() == "abi")
    Advance();
  else
    return false;

  if (!ConsumeToken(tok::colon))
    return false;

  // Consume the actual tag string (and allow some special characters)
  while (ConsumeToken(tok::raw_identifier, tok::comma, tok::period,
                      tok::numeric_constant))
    ;

  if (!ConsumeToken(tok::r_square))
    return false;

  start_position.Remove();
  return true;
}

bool CPlusPlusNameParser::ConsumeAnonymousNamespace() {
  Bookmark start_position = SetBookmark();
  if (!ConsumeToken(tok::l_paren)) {
    return false;
  }
  constexpr llvm::StringLiteral g_anonymous("anonymous");
  if (HasMoreTokens() && Peek().is(tok::raw_identifier) &&
      Peek().getRawIdentifier() == g_anonymous) {
    Advance();
  } else {
    return false;
  }

  if (!ConsumeToken(tok::kw_namespace)) {
    return false;
  }

  if (!ConsumeToken(tok::r_paren)) {
    return false;
  }
  start_position.Remove();
  return true;
}

bool CPlusPlusNameParser::ConsumeLambda() {
  Bookmark start_position = SetBookmark();
  if (!ConsumeToken(tok::l_brace)) {
    return false;
  }
  constexpr llvm::StringLiteral g_lambda("lambda");
  if (HasMoreTokens() && Peek().is(tok::raw_identifier) &&
      Peek().getRawIdentifier() == g_lambda) {
    // Put the matched brace back so we can use ConsumeBrackets
    TakeBack();
  } else {
    return false;
  }

  if (!ConsumeBrackets(tok::l_brace, tok::r_brace)) {
    return false;
  }

  start_position.Remove();
  return true;
}

bool CPlusPlusNameParser::ConsumeBrackets(tok::TokenKind left,
                                          tok::TokenKind right) {
  Bookmark start_position = SetBookmark();
  if (!HasMoreTokens() || Peek().getKind() != left)
    return false;
  Advance();

  int counter = 1;
  while (HasMoreTokens() && counter > 0) {
    tok::TokenKind kind = Peek().getKind();
    if (kind == right)
      --counter;
    else if (kind == left)
      ++counter;
    Advance();
  }

  assert(counter >= 0);
  if (counter > 0) {
    return false;
  }
  start_position.Remove();
  return true;
}

bool CPlusPlusNameParser::ConsumeOperator() {
  Bookmark start_position = SetBookmark();
  if (!ConsumeToken(tok::kw_operator))
    return false;

  if (!HasMoreTokens()) {
    return false;
  }

  const auto &token = Peek();

  // When clang generates debug info it adds template parameters to names.
  // Since clang doesn't add a space between the name and the template parameter
  // in some cases we are not generating valid C++ names e.g.:
  //
  //   operator<<A::B>
  //
  // In some of these cases we will not parse them correctly. This fixes the
  // issue by detecting this case and inserting tok::less in place of
  // tok::lessless and returning successfully that we consumed the operator.
  if (token.getKind() == tok::lessless) {
    // Make sure we have more tokens before attempting to look ahead one more.
    if (m_next_token_index + 1 < m_tokens.size()) {
      // Look ahead two tokens.
      clang::Token n_token = m_tokens[m_next_token_index + 1];
      // If we find ( or < then this is indeed operator<< no need for fix.
      if (n_token.getKind() != tok::l_paren && n_token.getKind() != tok::less) {
        clang::Token tmp_tok;
        tmp_tok.startToken();
        tmp_tok.setLength(1);
        tmp_tok.setLocation(token.getLocation().getLocWithOffset(1));
        tmp_tok.setKind(tok::less);

        m_tokens[m_next_token_index] = tmp_tok;

        start_position.Remove();
        return true;
      }
    }
  }

  switch (token.getKind()) {
  case tok::kw_new:
  case tok::kw_delete:
    // This is 'new' or 'delete' operators.
    Advance();
    // Check for array new/delete.
    if (HasMoreTokens() && Peek().is(tok::l_square)) {
      // Consume the '[' and ']'.
      if (!ConsumeBrackets(tok::l_square, tok::r_square))
        return false;
    }
    break;

#define OVERLOADED_OPERATOR(Name, Spelling, Token, Unary, Binary, MemberOnly)  \
  case tok::Token:                                                             \
    Advance();                                                                 \
    break;
#define OVERLOADED_OPERATOR_MULTI(Name, Spelling, Unary, Binary, MemberOnly)
#include "clang/Basic/OperatorKinds.def"
#undef OVERLOADED_OPERATOR
#undef OVERLOADED_OPERATOR_MULTI

  case tok::l_paren:
    // Call operator consume '(' ... ')'.
    if (ConsumeBrackets(tok::l_paren, tok::r_paren))
      break;
    return false;

  case tok::l_square:
    // This is a [] operator.
    // Consume the '[' and ']'.
    if (ConsumeBrackets(tok::l_square, tok::r_square))
      break;
    return false;

  default:
    // This might be a cast operator.
    if (ConsumeTypename())
      break;
    return false;
  }
  start_position.Remove();
  return true;
}

void CPlusPlusNameParser::SkipTypeQualifiers() {
  while (ConsumeToken(tok::kw_const, tok::kw_volatile))
    ;
}

void CPlusPlusNameParser::SkipFunctionQualifiers() {
  while (ConsumeToken(tok::kw_const, tok::kw_volatile, tok::amp, tok::ampamp))
    ;
}

bool CPlusPlusNameParser::ConsumeBuiltinType() {
  bool result = false;
  bool continue_parsing = true;
  // Built-in types can be made of a few keywords like 'unsigned long long
  // int'. This function consumes all built-in type keywords without checking
  // if they make sense like 'unsigned char void'.
  while (continue_parsing && HasMoreTokens()) {
    switch (Peek().getKind()) {
    case tok::kw_short:
    case tok::kw_long:
    case tok::kw___int64:
    case tok::kw___int128:
    case tok::kw_signed:
    case tok::kw_unsigned:
    case tok::kw_void:
    case tok::kw_char:
    case tok::kw_int:
    case tok::kw_half:
    case tok::kw_float:
    case tok::kw_double:
    case tok::kw___float128:
    case tok::kw_wchar_t:
    case tok::kw_bool:
    case tok::kw_char16_t:
    case tok::kw_char32_t:
      result = true;
      Advance();
      break;
    default:
      continue_parsing = false;
      break;
    }
  }
  return result;
}

void CPlusPlusNameParser::SkipPtrsAndRefs() {
  // Ignoring result.
  ConsumePtrsAndRefs();
}

bool CPlusPlusNameParser::ConsumePtrsAndRefs() {
  bool found = false;
  SkipTypeQualifiers();
  while (ConsumeToken(tok::star, tok::amp, tok::ampamp, tok::kw_const,
                      tok::kw_volatile)) {
    found = true;
    SkipTypeQualifiers();
  }
  return found;
}

bool CPlusPlusNameParser::ConsumeDecltype() {
  Bookmark start_position = SetBookmark();
  if (!ConsumeToken(tok::kw_decltype))
    return false;

  if (!ConsumeArguments())
    return false;

  start_position.Remove();
  return true;
}

bool CPlusPlusNameParser::ConsumeTypename() {
  Bookmark start_position = SetBookmark();
  SkipTypeQualifiers();
  if (!ConsumeBuiltinType() && !ConsumeDecltype()) {
    if (!ParseFullNameImpl())
      return false;
  }
  SkipPtrsAndRefs();
  start_position.Remove();
  return true;
}

std::optional<CPlusPlusNameParser::ParsedNameRanges>
CPlusPlusNameParser::ParseFullNameImpl() {
  // Name parsing state machine.
  enum class State {
    Beginning,       // start of the name
    AfterTwoColons,  // right after ::
    AfterIdentifier, // right after alphanumerical identifier ([a-z0-9_]+)
    AfterTemplate,   // right after template brackets (<something>)
    AfterOperator,   // right after name of C++ operator
  };

  Bookmark start_position = SetBookmark();
  State state = State::Beginning;
  bool continue_parsing = true;
  std::optional<size_t> last_coloncolon_position;

  while (continue_parsing && HasMoreTokens()) {
    const auto &token = Peek();
    switch (token.getKind()) {
    case tok::raw_identifier: // Just a name.
      if (state != State::Beginning && state != State::AfterTwoColons) {
        continue_parsing = false;
        break;
      }
      Advance();
      state = State::AfterIdentifier;
      break;
    case tok::l_square: {
      // Handles types or functions that were tagged
      // with, e.g.,
      //   [[gnu::abi_tag("tag1","tag2")]] func()
      // and demangled/prettified into:
      //   func[abi:tag1][abi:tag2]()

      // ABI tags only appear after a method or type name
      const bool valid_state =
          state == State::AfterIdentifier || state == State::AfterOperator;
      if (!valid_state || !ConsumeAbiTag()) {
        continue_parsing = false;
      }

      break;
    }
    case tok::l_paren: {
      if (state == State::Beginning || state == State::AfterTwoColons) {
        // (anonymous namespace)
        if (ConsumeAnonymousNamespace()) {
          state = State::AfterIdentifier;
          break;
        }
      }

      // Type declared inside a function 'func()::Type'
      if (state != State::AfterIdentifier && state != State::AfterTemplate &&
          state != State::AfterOperator) {
        continue_parsing = false;
        break;
      }
      Bookmark l_paren_position = SetBookmark();
      // Consume the '(' ... ') [const]'.
      if (!ConsumeArguments()) {
        continue_parsing = false;
        break;
      }
      SkipFunctionQualifiers();

      // Consume '::'
      size_t coloncolon_position = GetCurrentPosition();
      if (!ConsumeToken(tok::coloncolon)) {
        continue_parsing = false;
        break;
      }
      l_paren_position.Remove();
      last_coloncolon_position = coloncolon_position;
      state = State::AfterTwoColons;
      break;
    }
    case tok::l_brace:
      if (state == State::Beginning || state == State::AfterTwoColons) {
        if (ConsumeLambda()) {
          state = State::AfterIdentifier;
          break;
        }
      }
      continue_parsing = false;
      break;
    case tok::coloncolon: // Type nesting delimiter.
      if (state != State::Beginning && state != State::AfterIdentifier &&
          state != State::AfterTemplate) {
        continue_parsing = false;
        break;
      }
      last_coloncolon_position = GetCurrentPosition();
      Advance();
      state = State::AfterTwoColons;
      break;
    case tok::less: // Template brackets.
      if (state != State::AfterIdentifier && state != State::AfterOperator) {
        continue_parsing = false;
        break;
      }
      if (!ConsumeTemplateArgs()) {
        continue_parsing = false;
        break;
      }
      state = State::AfterTemplate;
      break;
    case tok::kw_operator: // C++ operator overloading.
      if (state != State::Beginning && state != State::AfterTwoColons) {
        continue_parsing = false;
        break;
      }
      if (!ConsumeOperator()) {
        continue_parsing = false;
        break;
      }
      state = State::AfterOperator;
      break;
    case tok::tilde: // Destructor.
      if (state != State::Beginning && state != State::AfterTwoColons) {
        continue_parsing = false;
        break;
      }
      Advance();
      if (ConsumeToken(tok::raw_identifier)) {
        state = State::AfterIdentifier;
      } else {
        TakeBack();
        continue_parsing = false;
      }
      break;
    default:
      continue_parsing = false;
      break;
    }
  }

  if (state == State::AfterIdentifier || state == State::AfterOperator ||
      state == State::AfterTemplate) {
    ParsedNameRanges result;
    if (last_coloncolon_position) {
      result.context_range =
          Range(start_position.GetSavedPosition(), *last_coloncolon_position);
      result.basename_range =
          Range(*last_coloncolon_position + 1, GetCurrentPosition());
    } else {
      result.basename_range =
          Range(start_position.GetSavedPosition(), GetCurrentPosition());
    }
    start_position.Remove();
    return result;
  } else {
    return std::nullopt;
  }
}

llvm::StringRef CPlusPlusNameParser::GetTextForRange(const Range &range) {
  if (range.empty())
    return llvm::StringRef();
  assert(range.begin_index < range.end_index);
  assert(range.begin_index < m_tokens.size());
  assert(range.end_index <= m_tokens.size());
  clang::Token &first_token = m_tokens[range.begin_index];
  clang::Token &last_token = m_tokens[range.end_index - 1];
  clang::SourceLocation start_loc = first_token.getLocation();
  clang::SourceLocation end_loc = last_token.getLocation();
  unsigned start_pos = start_loc.getRawEncoding();
  unsigned end_pos = end_loc.getRawEncoding() + last_token.getLength();
  return m_text.take_front(end_pos).drop_front(start_pos);
}

static const clang::LangOptions &GetLangOptions() {
  static clang::LangOptions g_options;
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    g_options.LineComment = true;
    g_options.C99 = true;
    g_options.C11 = true;
    g_options.CPlusPlus = true;
    g_options.CPlusPlus11 = true;
    g_options.CPlusPlus14 = true;
    g_options.CPlusPlus17 = true;
    g_options.CPlusPlus20 = true;
  });
  return g_options;
}

static const llvm::StringMap<tok::TokenKind> &GetKeywordsMap() {
  static llvm::StringMap<tok::TokenKind> g_map{
#define KEYWORD(Name, Flags) {llvm::StringRef(#Name), tok::kw_##Name},
#include "clang/Basic/TokenKinds.def"
#undef KEYWORD
  };
  return g_map;
}

void CPlusPlusNameParser::ExtractTokens() {
  if (m_text.empty())
    return;
  clang::Lexer lexer(clang::SourceLocation(), GetLangOptions(), m_text.data(),
                     m_text.data(), m_text.data() + m_text.size());
  const auto &kw_map = GetKeywordsMap();
  clang::Token token;
  for (lexer.LexFromRawLexer(token); !token.is(clang::tok::eof);
       lexer.LexFromRawLexer(token)) {
    if (token.is(clang::tok::raw_identifier)) {
      auto it = kw_map.find(token.getRawIdentifier());
      if (it != kw_map.end()) {
        token.setKind(it->getValue());
      }
    }

    m_tokens.push_back(token);
  }
}
