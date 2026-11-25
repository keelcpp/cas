#include "keel/cas.h"
#include "keel/format.h"

namespace {

keel::i32 g_failed = 0;

void check(bool cond, const char* expr, keel::i32 line) {
    if (!cond) {
        keel::eprintln("FAIL [line {}]: {}", line, expr);
        ++g_failed;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void test_parser_and_diff() {
    keel::cas::Ctx c;
    keel::cas::Expr e;
    keel::cas::ParseErrorInfo err;
    CHECK(keel::cas::tryParseExpr(c, "x^3 + 2*x - 5", e, &err));
    const keel::cas::Expr x = c.symbol("x");
    const keel::cas::Expr de = keel::cas::simplify(c, c.diff(e, x));
    keel::cas::Expr expected;
    CHECK(keel::cas::tryParseExpr(c, "3*x^2 + 2", expected));
    CHECK(keel::cas::simplify(c, c.add({de, c.neg(expected)})) == c.zero());

    // Malformed input returns false cleanly without throwing.
    keel::cas::Expr bad;
    CHECK(!keel::cas::tryParseExpr(c, "x + * 2", bad, &err));
    CHECK(err.pos == 4);
    CHECK(!err.message.empty());
}

void test_simplify_and_integrate() {
    keel::cas::Ctx c;
    const keel::cas::Expr x = c.symbol("x");

    // sin(x)^2 + cos(x)^2 == 1
    keel::cas::Expr trig;
    CHECK(keel::cas::tryParseExpr(c, "sin(x)^2 + cos(x)^2", trig));
    CHECK(keel::cas::simplify(c, trig) == c.one());

    // integrate(1 / (x^2 + 1), x) -> atan(x)
    keel::cas::Expr f;
    CHECK(keel::cas::tryParseExpr(c, "1 / (x^2 + 1)", f));
    const keel::cas::RationalIntegral res = keel::cas::integrate(c, f, x);
    CHECK(res.proved());
    const keel::cas::Expr diff_check = keel::cas::simplify(c, c.add({c.diff(res.F, x), c.neg(f)}));
    CHECK(diff_check == c.zero());
}

void test_ode_and_laplace() {
    keel::cas::Ctx c;
    const keel::cas::Expr x = c.symbol("x");
    const keel::cas::Expr y = c.symbol("y");

    // y' = 2*x -> y = x^2 + C1
    keel::cas::Expr rhs;
    CHECK(keel::cas::tryParseExpr(c, "2*x", rhs));
    const keel::cas::OdeResult ode = keel::cas::dsolve1(c, rhs, x, y);
    CHECK(ode.proved());

    // Laplace{exp(-2*t)}(s) = 1 / (s + 2)
    const keel::cas::Expr t = c.symbol("t");
    const keel::cas::Expr s = c.symbol("s");
    keel::cas::Expr ft;
    CHECK(keel::cas::tryParseExpr(c, "exp(-2*t)", ft));
    const keel::cas::LaplaceResult L = keel::cas::laplace(c, ft, t, s);
    CHECK(L.proved());
    keel::cas::Expr wantL;
    CHECK(keel::cas::tryParseExpr(c, "1 / (s + 2)", wantL));
    CHECK(keel::cas::simplify(c, c.add({L.F, c.neg(wantL)})) == c.zero());
}

} // namespace

int main() {
    test_parser_and_diff();
    test_simplify_and_integrate();
    test_ode_and_laplace();

    if (g_failed != 0) {
        keel::eprintln("test_cas: {} failure(s)", g_failed);
        return 1;
    }
    keel::println("test_cas: all tests passed");
    return 0;
}
