// core/parser.hpp — exception-free Pratt parser from source text directly into a Ctx.
#pragma once

#include "keel/core.h"
#include "keel/string.h"
#include "keel/cas/core/expr.hpp"

namespace keel::cas {

// Populated by tryParseExpr on malformed input; `pos` is the 0-based offset
// of the offending character (or src.size() for unexpected end).
struct ParseErrorInfo {
    keel::usize pos = 0;
    keel::String message;
};

// Grammar (precedence loose to tight):
//   expr    := unary ( ('+'|'-'|'*'|'/'|'^') expr )?
//             '+' '-' bind 40 (left), '*' '/' 50 (left), '^' 60 (right)
//   unary   := ('-'|'+') unary | postfix     — looser than '^':
//             -x^2 == -(x^2), but exponents take a sign: 2^-1 == 1/2
//   postfix := primary ('^' unary)?
//   primary := number | symbol | ident '(' expr (',' expr)* ')' | '(' expr ')'
// Numbers: integer literals, `p/q` via the '/' operator (canonical folding
// makes the Rational), decimal literals only when dyadic (exactly
// representable: 0.5 -> 1/2); others fail with ParseErrorInfo.
// After a complete expression only whitespace may remain.
[[nodiscard]] bool tryParseExpr(Ctx &c, keel::StringRef src, Expr &out,
                                ParseErrorInfo *err = nullptr);

} // namespace keel::cas
