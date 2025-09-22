//===-- MSVCUndecoratedNameParser.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MSVCUndecoratedNameParser.h"

#include <stack>

MSVCUndecoratedNameParser::MSVCUndecoratedNameParser(llvm::StringRef name) {
  // Global ctor and dtor are global functions.
  if (name.contains("dynamic initializer for") ||
      name.contains("dynamic atexit destructor for")) {
    m_specifiers.emplace_back(name, name);
    return;
  }

  std::size_t last_base_start = 0;

  std::stack<std::size_t> stack;
  unsigned int open_angle_brackets = 0;
  for (size_t i = 0; i < name.size(); i++) {
    switch (name[i]) {
    case '<':
      // Do not treat `operator<' and `operator<<' as templates
      // (sometimes they represented as `<' and `<<' in the name).
      if (i == last_base_start ||
          (i == last_base_start + 1 && name[last_base_start] == '<'))
        break;

      stack.push(i);
      open_angle_brackets++;

      break;
    case '>':
      if (!stack.empty() && name[stack.top()] == '<') {
        open_angle_brackets--;
        stack.pop();
      }

      break;
    case '`':
      stack.push(i);

      break;
    case '\'':
      while (!stack.empty()) {
        std::size_t top = stack.top();
        if (name[top] == '<')
          open_angle_brackets--;

        stack.pop();

        if (name[top] == '`')
          break;
      }

      break;
    case ':':
      if (open_angle_brackets)
        break;
      if (i == 0 || name[i - 1] != ':')
        break;

      m_specifiers.emplace_back(name.take_front(i - 1),
                                name.slice(last_base_start, i - 1));

      last_base_start = i + 1;
      break;
    default:
      break;
    }
  }

  m_specifiers.emplace_back(name, name.drop_front(last_base_start));
}

bool MSVCUndecoratedNameParser::IsMSVCUndecoratedName(llvm::StringRef name) {
  return name.contains('`');
}

bool MSVCUndecoratedNameParser::ExtractContextAndIdentifier(
    llvm::StringRef name, llvm::StringRef &context,
    llvm::StringRef &identifier) {
  MSVCUndecoratedNameParser parser(name);
  llvm::ArrayRef<MSVCUndecoratedNameSpecifier> specs = parser.GetSpecifiers();

  std::size_t count = specs.size();
  identifier = count > 0 ? specs[count - 1].GetBaseName() : "";
  context = count > 1 ? specs[count - 2].GetFullName() : "";

  return count;
}

llvm::StringRef MSVCUndecoratedNameParser::DropScope(llvm::StringRef name) {
  MSVCUndecoratedNameParser parser(name);
  llvm::ArrayRef<MSVCUndecoratedNameSpecifier> specs = parser.GetSpecifiers();
  if (specs.empty())
    return "";

  return specs[specs.size() - 1].GetBaseName();
}
