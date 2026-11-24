// core/parser.cpp — exception-free Pratt parser: source text -> Ctx constructors.
#include "keel/core.h"
#include "keel/format.h"
#include "keel/cas/core/parser.hpp"

namespace keel::cas {
namespace {

enum class Tok : keel::u8 {
    End,
    Ident,  // [a-zA-Z_][a-zA-Z_0-9]*
    Number, // digits [. digits]
    Plus,
    Minus,
    Star,
    Slash,
    Caret,
    LParen,
    RParen,
    Comma,
    Invalid,
};

struct Token {
    Tok kind = Tok::End;
    keel::usize pos = 0;  // offset of the first character
    keel::StringRef text; // Ident: name; Number: integer part
    keel::StringRef frac; // Number: digits after '.' ("" if no point)
};

bool isSpace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
           ch == '\f' || ch == '\v';
}

bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }

bool isIdentStart(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

bool isIdentChar(char ch) { return isIdentStart(ch) || isDigit(ch); }

keel::i32 bp(Tok k)
{
    switch (k) {
    case Tok::Plus:
    case Tok::Minus: return 40;
    case Tok::Star:
    case Tok::Slash: return 50;
    case Tok::Caret: return 60;
    default: return 0;
    }
}

class Parser
{
public:
    Parser(Ctx &c, keel::StringRef src) : c_(c), src_(src) { advance(); }

    bool run(Expr &out, ParseErrorInfo *err)
    {
        if (failed_) {
            if (err) *err = keel::move(err_);
            return false;
        }
        Expr e = parseExpr(0);
        if (!failed_ && cur_.kind != Tok::End)
            fail(cur_.pos, "unexpected token after expression");
        if (failed_) {
            if (err) *err = keel::move(err_);
            return false;
        }
        out = e;
        return true;
    }

private:
    static constexpr keel::i32 kUnaryBp = 55;

    void fail(keel::usize pos, keel::StringRef msg)
    {
        if (!failed_) {
            failed_ = true;
            err_.pos = pos;
            err_.message = keel::String::from(msg);
        }
    }

    Token lex()
    {
        while (pos_ < src_.size() && isSpace(src_[pos_]))
            ++pos_;
        Token t;
        t.pos = pos_;
        if (pos_ >= src_.size())
            return t;
        const char ch = src_[pos_];
        if (isIdentStart(ch)) {
            const keel::usize start = pos_;
            while (pos_ < src_.size() && isIdentChar(src_[pos_]))
                ++pos_;
            t.text = src_.substr(start, pos_ - start);
            t.kind = Tok::Ident;
            return t;
        }
        if (isDigit(ch)) {
            const keel::usize start = pos_;
            while (pos_ < src_.size() && isDigit(src_[pos_]))
                ++pos_;
            t.text = src_.substr(start, pos_ - start);
            if (pos_ < src_.size() && src_[pos_] == '.') {
                ++pos_;
                if (pos_ >= src_.size() || !isDigit(src_[pos_])) {
                    fail(pos_, "digit expected after '.'");
                    t.kind = Tok::Invalid;
                    return t;
                }
                const keel::usize fstart = pos_;
                while (pos_ < src_.size() && isDigit(src_[pos_]))
                    ++pos_;
                t.frac = src_.substr(fstart, pos_ - fstart);
            }
            t.kind = Tok::Number;
            return t;
        }
        ++pos_;
        switch (ch) {
        case '+': t.kind = Tok::Plus; break;
        case '-': t.kind = Tok::Minus; break;
        case '*': t.kind = Tok::Star; break;
        case '/': t.kind = Tok::Slash; break;
        case '^': t.kind = Tok::Caret; break;
        case '(': t.kind = Tok::LParen; break;
        case ')': t.kind = Tok::RParen; break;
        case ',': t.kind = Tok::Comma; break;
        default:
            fail(t.pos, keel::format("unexpected character '{}'", ch));
            t.kind = Tok::Invalid;
            break;
        }
        return t;
    }

    void advance()
    {
        if (failed_) {
            cur_ = Token{};
            cur_.kind = Tok::Invalid;
            return;
        }
        cur_ = lex();
    }

    void expect(Tok k, const char *msg)
    {
        if (failed_) return;
        if (cur_.kind != k) {
            fail(cur_.pos, msg);
            return;
        }
        advance();
    }

    Expr parseExpr(keel::i32 minBp)
    {
        Expr left = nud();
        while (!failed_) {
            const keel::i32 b = bp(cur_.kind);
            if (b <= minBp)
                break;
            const Tok op = cur_.kind;
            advance();
            Expr right = parseExpr(op == Tok::Caret ? b - 1 : b);
            if (failed_) return {};
            left = apply(op, left, right);
        }
        return left;
    }

    Expr nud()
    {
        if (failed_) return {};
        switch (cur_.kind) {
        case Tok::Minus: {
            advance();
            Expr e = parseExpr(kUnaryBp);
            return failed_ ? Expr{} : c_.neg(e);
        }
        case Tok::Plus: {
            advance();
            return parseExpr(kUnaryBp);
        }
        case Tok::Number:
            return numberLiteral();
        case Tok::Ident:
            return identLiteral();
        case Tok::LParen: {
            advance();
            const Expr e = parseExpr(0);
            expect(Tok::RParen, "')' expected");
            return failed_ ? Expr{} : e;
        }
        case Tok::End:
            fail(cur_.pos, "unexpected end of input");
            return {};
        default:
            fail(cur_.pos, "unexpected token");
            return {};
        }
    }

    Expr apply(Tok op, Expr a, Expr b)
    {
        switch (op) {
        case Tok::Plus: return c_.add({a, b});
        case Tok::Minus: return c_.add({a, c_.neg(b)});
        case Tok::Star: return c_.mul({a, b});
        case Tok::Slash: return c_.div(a, b);
        case Tok::Caret: return c_.pow(a, b);
        default: KEEL_CAS_ASSERT(false && "not a binary token"); return {};
        }
    }

    Expr numberLiteral()
    {
        const Token t = cur_;
        advance();
        BigInt ipart;
        if (!BigInt::tryParse(ipart, t.text)) {
            fail(t.pos, keel::format("malformed number '{}'", t.text));
            return {};
        }
        if (t.frac.empty())
            return c_.number(Rational(keel::move(ipart), BigInt(1)));
        BigInt frac;
        if (!BigInt::tryParse(frac, t.frac)) {
            fail(t.pos, keel::format("malformed number '0.{}'", t.frac));
            return {};
        }
        keel::usize k = t.frac.size();
        const BigInt five(5);
        while (k > 0) {
            BigInt q, r;
            BigInt::divMod(frac, five, q, r);
            if (!r.isZero())
                break;
            frac = keel::move(q);
            --k;
        }
        if (k != 0) {
            fail(t.pos, keel::format("decimal '{}.{}' is not exactly representable; write it as a fraction",
                                     t.text, t.frac));
            return {};
        }
        const BigInt scale = BigInt::powUInt(BigInt(2), t.frac.size());
        BigInt num = ipart * scale + frac;
        return c_.number(Rational(keel::move(num), scale));
    }

    Expr identLiteral()
    {
        const Token t = cur_;
        advance();
        if (!failed_ && cur_.kind == Tok::LParen) {
            advance();
            keel::Array<Expr> args;
            Expr first = parseExpr(0);
            if (failed_) return {};
            args.push_back(first);
            while (!failed_ && cur_.kind == Tok::Comma) {
                advance();
                Expr next = parseExpr(0);
                if (failed_) return {};
                args.push_back(next);
            }
            expect(Tok::RParen, "')' expected");
            return failed_ ? Expr{} : c_.func(t.text, args);
        }
        return c_.symbol(t.text);
    }

    Ctx &c_;
    keel::StringRef src_;
    keel::usize pos_ = 0;
    Token cur_;
    bool failed_ = false;
    ParseErrorInfo err_;
};

} // namespace

bool tryParseExpr(Ctx &c, keel::StringRef src, Expr &out, ParseErrorInfo *err)
{
    return Parser(c, src).run(out, err);
}

} // namespace keel::cas
