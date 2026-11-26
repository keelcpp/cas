#include "keel/cas.h"
#include "keel/format.h"
#include "keel/math.h"

namespace {

keel::i32 g_failed = 0;

void check(bool cond, const char* expr, keel::i32 line) {
    if (!cond) {
        keel::eprintln("FAIL [line {}]: {}", line, expr);
        ++g_failed;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

keel::cas::Expr mustParse(keel::cas::Ctx& c, keel::StringRef src) {
    keel::cas::Expr e;
    keel::cas::ParseErrorInfo err;
    const bool ok = keel::cas::tryParseExpr(c, src, e, &err);
    CHECK(ok);
    return e;
}

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

void test_hardening_regressions() {
    using namespace keel::cas;
    Ctx c;
    const Expr x = c.symbol("x");
    const Expr y = c.symbol("y");
    const Expr k = c.symbol("k");
    const Expr infSym = c.symbol("inf");

    // 1. Alternating rational series symsum scaling fix:
    //    sum_{k=0}^{inf} (-1)^k / (2*k + 2)^2 = (1/4) * (pi^2 / 12) = pi^2 / 48.
    {
        const Expr term1 = mustParse(c, "(-1)^k / (2*k + 2)^2");
        const SymSumResult s1 = symsum(c, term1, k, c.zero(), infSym);
        CHECK(s1.proved());
        const Expr want1 = mustParse(c, "pi^2 / 48");
        CHECK(simplify(c, c.add({s1.value, c.neg(want1)})) == c.zero());

        const Expr term2 = mustParse(c, "(-1)^k / (2 * (k + 1)^2)");
        const SymSumResult s2 = symsum(c, term2, k, c.zero(), infSym);
        CHECK(s2.proved());
        const Expr want2 = mustParse(c, "pi^2 / 24");
        CHECK(simplify(c, c.add({s2.value, c.neg(want2)})) == c.zero());
    }

    // 2. Gamma pole simplification must not crash with pow(0, -1).
    {
        const Expr gRatio = mustParse(c, "gamma(0) / gamma(1)");
        const Expr sg1 = simplify(c, gRatio);
        CHECK(!sg1.isNull());
        const Expr gRefl = mustParse(c, "gamma(0) * gamma(0)");
        const Expr sg2 = simplify(c, gRefl);
        CHECK(!sg2.isNull());
    }

    // 3. Parser hardening: division by zero, 0^-1, deep nesting, huge decimal fraction.
    {
        Expr out;
        ParseErrorInfo err;
        CHECK(!tryParseExpr(c, "1 / 0", out, &err));
        CHECK(!err.message.empty());
        CHECK(!tryParseExpr(c, "0^(-1)", out, &err));
        CHECK(!err.message.empty());

        keel::String deep;
        for (keel::i32 i = 0; i < 300; ++i) deep.push_back('(');
        deep.push_back('x');
        for (keel::i32 i = 0; i < 300; ++i) deep.push_back(')');
        CHECK(!tryParseExpr(c, deep, out, &err));
        CHECK(!err.message.empty());

        keel::String longFrac("0.");
        for (keel::i32 i = 0; i < 80; ++i) longFrac.push_back('0');
        CHECK(!tryParseExpr(c, longFrac, out, &err));
        CHECK(!err.message.empty());
    }

    // 4. Null-safety and bounded expand / evalExact.
    {
        CHECK(!c.isZero(Expr{}));
        CHECK(c.expand(Expr{}).isNull());
        CHECK(c.subs(Expr{}, x, c.one()).isNull());

        const Expr binom20 = c.expand(mustParse(c, "(x + 1)^20"));
        Rational v20;
        const keel::Pair<keel::StringRef, Rational> at1[] = {{"x", Rational(keel::i64{1})}};
        CHECK(c.evalExact(binom20, at1, v20));
        CHECK(v20 == Rational(keel::i64{1048576}));

        // Huge exponent in expand stays bounded without OOM.
        const Expr binom100 = c.expand(mustParse(c, "(x + 1)^100"));
        CHECK(!binom100.isNull());

        // Huge exponent in evalExact does not hang or OOM.
        Rational hugeOut;
        CHECK(!c.evalExact(mustParse(c, "2^1000000"), {}, hugeOut));
        Rational negOnePow;
        CHECK(c.evalExact(mustParse(c, "(-1)^1000001"), {}, negOnePow));
        CHECK(negOnePow == Rational(keel::i64{-1}));
    }

    // 5. 0-arg Func nodes must never cause OOB access across core/transform/solver.
    {
        const Expr f0 = c.func("custom0", keel::Array<Expr>{});
        CHECK(c.diff(f0, x).isNull());
        CHECK(!simplify(c, f0).isNull());
        Rational rOut;
        CHECK(!c.evalExact(f0, {}, rOut));
        keel::f64 dOut = 0.0;
        CHECK(!c.evalDouble(f0, {}, dOut));
        CHECK(!c.isAssumedPositive(f0));
        CHECK(!c.isAssumedNegative(f0));
        CHECK(!c.isAssumedNonNegative(f0));
        CHECK(!c.isAssumedReal(f0));
        CHECK(taylor(c, f0, x, c.zero(), 2) == f0);
        const LimitResult l0 = limit(c, f0, x, c.zero());
        CHECK(l0.proved() || !l0.proved());
        const Expr t = c.symbol("t");
        const Expr s = c.symbol("s");
        const Expr w = c.symbol("w");
        const Expr n = c.symbol("n");
        const Expr z = c.symbol("z");
        const LaplaceResult lap0 = laplace(c, f0, t, s);
        CHECK(lap0.proved()); // constant wrt t -> f0 / s
        const FourierResult fou0 = fourier(c, f0, t, w);
        CHECK(fou0.proved()); // constant wrt t -> 2*pi*f0*dirac(w)
        const ZTransResult zt0 = ztrans(c, f0, n, z);
        CHECK(zt0.proved());  // constant wrt n -> f0*z/(z-1)
        const keel::Array<Expr> roots0 = solveSymbolic(c, f0, x);
        CHECK(roots0.empty());
    }

    // 6. evalDouble float-to-int UB and 0/0 edge-case guards.
    {
        keel::f64 v = 0.0;
        CHECK(c.evalDouble(mustParse(c, "airyAi(0)"), {}, v));
        CHECK(keel::abs(v - 0.35502805388781723926) < 1e-12);
        CHECK(c.evalDouble(mustParse(c, "airyAiPrime(0)"), {}, v));
        CHECK(keel::abs(v + 0.25881940379280679840) < 1e-12);
        CHECK(c.evalDouble(mustParse(c, "acot(0)"), {}, v));
        CHECK(keel::abs(v - 0.5 * keel::kPi) < 1e-12);

        const keel::Pair<keel::StringRef, keel::f64> huge[] = {{"x", 1e20}};
        const keel::Pair<keel::StringRef, keel::f64> negHuge[] = {{"x", -1e20}};
        CHECK(!c.evalDouble(mustParse(c, "besselj(x, 1)"), huge, v));
        CHECK(!c.evalDouble(mustParse(c, "bessely(x, 1)"), huge, v));
        CHECK(!c.evalDouble(mustParse(c, "besseli(x, 1)"), huge, v));
        CHECK(!c.evalDouble(mustParse(c, "besselk(x, 1)"), huge, v));
        CHECK(!c.evalDouble(mustParse(c, "legendreP(x, 1/2)"), huge, v));
        CHECK(!c.evalDouble(mustParse(c, "zeta(x)"), negHuge, v));
        CHECK(!c.evalDouble(mustParse(c, "polylog(x, 1/2)"), negHuge, v));
        CHECK(!c.evalDouble(mustParse(c, "bernoulliB(x)"), huge, v));
        CHECK(!c.evalDouble(mustParse(c, "bernoulliB(x, 1/2)"), huge, v));
        CHECK(!c.evalDouble(mustParse(c, "stirlingS2(5, x)"), huge, v));
        CHECK(!c.evalDouble(mustParse(c, "hypergeom1F1(x, 1, 1)"), negHuge, v));
    }

    // 7. Sturm refineRoot degenerate targetWidth and zero poly.
    {
        UPoly p({Rational(keel::i64{-2}), Rational(keel::i64{0}), Rational(keel::i64{1})});
        const keel::Array<RootInterval> ivs = isolateRealRoots(p);
        CHECK(ivs.size() == 2);
        const RootInterval r0 = refineRoot(p, ivs[0], Rational(keel::i64{0}));
        CHECK(r0.lo == ivs[0].lo && r0.hi == ivs[0].hi);
        const RootInterval rNeg = refineRoot(p, ivs[0], Rational(keel::i64{-1}));
        CHECK(rNeg.lo == ivs[0].lo && rNeg.hi == ivs[0].hi);
    }

    // 8. Multivariate factorization with high multiplicities (common^(2*mult) fix).
    {
        const Expr poly = c.expand(mustParse(c, "(x + y)^4 * (x - y)^2"));
        const FactorMultiResult fm = factorMulti(c, poly, {x, y});
        CHECK(fm.proved());
        CHECK(c.expand(c.add({fm.expr, c.neg(poly)})) == c.zero());
    }

    // 9. Hypergeometric diff zero-denominator guard and log(0) limit guard.
    {
        CHECK(c.diff(mustParse(c, "hypergeom2F1(1, 2, 0, x)"), x).isNull());
        CHECK(c.diff(mustParse(c, "hypergeom1F1(1, 0, x)"), x).isNull());
        const LimitResult lLog0 = limit(c, mustParse(c, "log(x) - log(0)"), x, c.zero());
        CHECK(!lLog0.proved());
    }

    // 10. Parametric ilaplace huge half-integer power guard and rational integration.
    {
        const Expr s = c.symbol("s");
        const Expr t = c.symbol("t");
        const LaplaceResult hugeIlap =
            ilaplace(c, mustParse(c, "s^(-100001/2)"), s, t);
        CHECK(!hugeIlap.proved());

        const Expr fRat = mustParse(c, "1 / (x^4 + 1)");
        const RationalIntegral rInt = integrate(c, fRat, x);
        CHECK(rInt.proved());
    }
}

void dump_reference() {
    using namespace keel::cas;
    Ctx c;
    const Expr x = c.symbol("x");
    const Expr y = c.symbol("y");
    const Expr z = c.symbol("z");
    const Expr t = c.symbol("t");
    const Expr s = c.symbol("s");
    const Expr w = c.symbol("w");
    const Expr n = c.symbol("n");
    const Expr k = c.symbol("k");
    const Expr infSym = c.symbol("inf");

    auto emitNum = [&](const char* key, const char* exprStr,
                       keel::ArrayRef<keel::Pair<keel::StringRef, keel::f64>> vals = {}) {
        const Expr e = mustParse(c, exprStr);
        keel::f64 val = 0.0;
        const bool ok = c.evalDouble(e, vals, val);
        CHECK(ok);
        keel::println("{}={:.17g}", key, val);
    };

    auto emitExpr = [&](const char* key, Expr e) {
        CHECK(!e.isNull());
        keel::println("{}={}", key, c.toString(e));
    };

    // --- 1. Special function numeric evaluations (evalDouble) ---
    const keel::Pair<keel::StringRef, keel::f64> v15[] = {{"x", 1.5}};
    const keel::Pair<keel::StringRef, keel::f64> v30[] = {{"x", 3.0}};
    const keel::Pair<keel::StringRef, keel::f64> v20[] = {{"x", 2.0}};
    const keel::Pair<keel::StringRef, keel::f64> v10[] = {{"x", 1.0}};
    const keel::Pair<keel::StringRef, keel::f64> vm20[] = {{"x", -2.0}};
    const keel::Pair<keel::StringRef, keel::f64> v05[] = {{"x", 0.5}};
    const keel::Pair<keel::StringRef, keel::f64> v075[] = {{"x", 0.75}};
    const keel::Pair<keel::StringRef, keel::f64> v12[] = {{"x", 1.2}};
    const keel::Pair<keel::StringRef, keel::f64> v06[] = {{"x", 0.6}};
    const keel::Pair<keel::StringRef, keel::f64> v04[] = {{"x", 0.4}};
    const keel::Pair<keel::StringRef, keel::f64> v08[] = {{"x", 0.8}};
    const keel::Pair<keel::StringRef, keel::f64> v25[] = {{"x", 2.5}};
    const keel::Pair<keel::StringRef, keel::f64> vm10[] = {{"x", -1.0}};

    emitNum("NUM_BESSELJ_0_1P5", "besselj(0, x)", v15);
    emitNum("NUM_BESSELJ_2_3P0", "besselj(2, x)", v30);
    emitNum("NUM_BESSELY_1_2P0", "bessely(1, x)", v20);
    emitNum("NUM_BESSELI_1_1P5", "besseli(1, x)", v15);
    emitNum("NUM_BESSELK_0_1P0", "besselk(0, x)", v10);
    emitNum("NUM_AIRY_AI_0", "airyAi(0)");
    emitNum("NUM_AIRY_AI_1", "airyAi(x)", v10);
    emitNum("NUM_AIRY_AI_M2", "airyAi(x)", vm20);
    emitNum("NUM_AIRY_BI_1", "airyBi(x)", v10);
    emitNum("NUM_AIRY_AIP_0P5", "airyAiPrime(x)", v05);
    emitNum("NUM_AIRY_BIP_0P5", "airyBiPrime(x)", v05);
    emitNum("NUM_ZETA_2", "zeta(2)");
    emitNum("NUM_ZETA_3", "zeta(3)");
    emitNum("NUM_ZETA_M1", "zeta(-1)");
    emitNum("NUM_ETA_2", "dirichletEta(2)");
    emitNum("NUM_DILOG_0P5", "dilog(x)", v05);
    emitNum("NUM_POLYLOG_3_0P5", "polylog(3, x)", v05);
    emitNum("NUM_DIGAMMA_2P5", "digamma(x)", v25);
    emitNum("NUM_TRIGAMMA_1P5", "trigamma(x)", v15);
    emitNum("NUM_ERF_0P75", "erf(x)", v075);
    emitNum("NUM_ERFI_0P75", "erfi(x)", v075);
    emitNum("NUM_SI_1P5", "Si(x)", v15);
    emitNum("NUM_CI_1P5", "Ci(x)", v15);
    emitNum("NUM_EI_1P5", "Ei(x)", v15);
    emitNum("NUM_FRESNELS_1P2", "FresnelS(x)", v12);
    emitNum("NUM_FRESNELC_1P2", "FresnelC(x)", v12);
    emitNum("NUM_ELLIPTICK_0P5", "ellipticK(x)", v05);
    emitNum("NUM_ELLIPTICE_0P5", "ellipticE(x)", v05);
    emitNum("NUM_LEGENDRE_4_0P6", "legendreP(4, x)", v06);
    emitNum("NUM_CHEBYSHEVT_5_0P4", "chebyshevT(5, x)", v04);
    emitNum("NUM_HERMITEH_4_1P2", "hermiteH(4, x)", v12);
    emitNum("NUM_LAGUERREL_3_0P8", "laguerreL(3, x)", v08);
    emitNum("NUM_BETA_2P5_3P5", "beta(5/2, 7/2)");
    emitNum("NUM_POCHHAMMER_1P5_4", "pochhammer(3/2, 4)");
    emitNum("NUM_IGAMMA_2P5_1P2", "igamma(5/2, x)", v12);
    emitNum("NUM_GAMMAINC_2P5_1P2", "gammainc(5/2, x)", v12);
    emitNum("NUM_1F1_1P5_2P5_0P8", "hypergeom1F1(3/2, 5/2, x)", v08);
    emitNum("NUM_2F1_0P5_1P5_2P5_0P4", "hypergeom2F1(1/2, 3/2, 5/2, x)", v04);
    emitNum("NUM_ACOT_0", "acot(0)");
    emitNum("NUM_ACOT_1", "acot(x)", v10);
    emitNum("NUM_ACOT_M1", "acot(x)", vm10);

    // --- 2. Symbolic differentiation & simplification ---
    emitExpr("SYM_DIFF_1", simplify(c, c.diff(mustParse(c, "x^3 * sin(2*x) + exp(-x^2)"), x)));
    emitExpr("SYM_DIFF_2", simplify(c, c.diff(mustParse(c, "atan(x^2)"), x)));
    emitExpr("SYM_SIMP_TRIG", simplify(c, mustParse(c, "(sin(x)^2 + cos(x)^2) / (1 + tan(x)^2)")));
    emitExpr("SYM_SIMP_GAMMA", simplify(c, mustParse(c, "gamma(x + 3) / gamma(x)")));

    // --- 3. Series, limits, residues, symsum, Pade ---
    emitExpr("SYM_TAYLOR_SINC", taylor(c, mustParse(c, "sin(x) / x"), x, c.zero(), 6));
    emitExpr("SYM_LAURENT_1", laurent(c, mustParse(c, "1 / (x * (x - 1))"), x, c.zero(), 3));

    const LimitResult lim1 = limit(c, mustParse(c, "(sin(x) - x) / x^3"), x, c.zero());
    CHECK(lim1.proved());
    emitExpr("SYM_LIM_1", lim1.value);

    const LimitResult lim2 = limitOneSided(c, mustParse(c, "x * log(x)"), x, c.zero(), +1);
    CHECK(lim2.proved());
    emitExpr("SYM_LIM_2", lim2.value);

    const LimitResult lim3 = limitAtInfinity(c, mustParse(c, "(1 + 2/x)^x"), x, +1);
    CHECK(lim3.proved());
    emitExpr("SYM_LIM_3", lim3.value);

    emitExpr("SYM_RES_1", residue(c, mustParse(c, "1 / ((z - 1)^2 * (z + 1))"), z, c.one()));

    const SymSumResult sum1 = symsum(c, mustParse(c, "k^3"), k, c.one(), n);
    CHECK(sum1.proved());
    emitExpr("SYM_SUM_CUBES", sum1.value);

    const SymSumResult sum2 = symsum(c, mustParse(c, "(-1)^k / (2*k + 2)^2"), k, c.zero(), infSym);
    CHECK(sum2.proved());
    emitExpr("SYM_SUM_ALT2", sum2.value);

    const SymSumResult sum3 = symsum(c, mustParse(c, "1 / k^4"), k, c.one(), infSym);
    CHECK(sum3.proved());
    emitExpr("SYM_SUM_ZETA4", sum3.value);

    const SymSumResult sum4 = symsum(c, mustParse(c, "(-1)^k / (2*k + 1)"), k, c.zero(), infSym);
    CHECK(sum4.proved());
    emitExpr("SYM_SUM_LEIBNIZ", sum4.value);

    const PadeResult pad1 = pade(c, mustParse(c, "exp(x)"), x, c.zero(), 2, 2);
    CHECK(pad1.proved());
    emitExpr("SYM_PADE_EXP22", pad1.value);

    // --- 4. Indefinite and definite integration ---
    const RationalIntegral int1 = integrate(c, mustParse(c, "1 / (x^3 + 1)"), x);
    CHECK(int1.proved());
    emitExpr("SYM_INT_RAT1", int1.F);

    const RationalIntegral int2 = integrate(c, mustParse(c, "x^2 * exp(-x)"), x);
    CHECK(int2.proved());
    emitExpr("SYM_INT_X2EXP", int2.F);

    const RationalIntegral int3 = integrate(c, mustParse(c, "1 / (x^4 + 1)"), x);
    CHECK(int3.proved());
    emitExpr("SYM_INT_X4P1", int3.F);

    const RationalIntegral int4 = integrate(c, mustParse(c, "sin(x)^3"), x);
    CHECK(int4.proved());
    emitExpr("SYM_INT_SIN3", int4.F);

    const DefiniteIntegral dint1 = definiteIntegrate(c, mustParse(c, "x^2 * exp(x)"), x, c.zero(), c.one());
    CHECK(dint1.proved());
    emitExpr("SYM_DINT_1", dint1.value);

    const DefiniteIntegral dint2 = definiteIntegrate(c, mustParse(c, "exp(-3*x)"), x, c.zero(), infSym);
    CHECK(dint2.proved());
    emitExpr("SYM_DINT_2", dint2.value);

    const DefiniteIntegral dint3 = definiteIntegrate(c, mustParse(c, "sin(x)^2"), x, c.zero(), c.symbol("pi"));
    CHECK(dint3.proved());
    emitExpr("SYM_DINT_3", dint3.value);

    const DefiniteIntegral dint4 = definiteIntegrate(c, mustParse(c, "1 / (x^2 + 4)"), x, c.zero(), infSym);
    CHECK(dint4.proved());
    emitExpr("SYM_DINT_4", dint4.value);

    // --- 5. Symbolic solving & polynomial algebra ---
    const keel::Array<Expr> roots = solveSymbolic(c, mustParse(c, "x^3 - 6*x^2 + 11*x - 6"), x);
    CHECK(roots.size() == 3);
    emitExpr("SYM_ROOT_0", roots[0]);
    emitExpr("SYM_ROOT_1", roots[1]);
    emitExpr("SYM_ROOT_2", roots[2]);

    const FactorMultiResult fm1 = factorMulti(c, mustParse(c, "x^4 - y^4"), {x, y});
    CHECK(fm1.proved());
    emitExpr("SYM_FACTORMULTI_1", fm1.expr);

    const GroebnerResult gb = groebnerBasis(
        c, {mustParse(c, "x^2 - y"), mustParse(c, "x*y - 1")}, {x, y}, MonomialOrder::Lex);
    CHECK(gb.proved());
    CHECK(gb.basis.size() == 2);
    emitExpr("SYM_GB_0", gb.basis[0]);
    emitExpr("SYM_GB_1", gb.basis[1]);

    // --- 6. Symbolic matrices ---
    SymMatrix A(2, 2, {c.number(keel::i64{2}), x, c.number(keel::i64{1}), c.number(keel::i64{3})});
    emitExpr("SYM_MAT_DET", simplify(c, det(c, A)));
    const SymMatrix Ainv = inv(c, A);
    CHECK(!Ainv.empty());
    emitExpr("SYM_MAT_INV00", simplify(c, Ainv(0, 0)));
    emitExpr("SYM_MAT_INV01", simplify(c, Ainv(0, 1)));
    emitExpr("SYM_MAT_CHARPOLY", simplify(c, charpoly(c, A, s)));

    SymMatrix J(2, 2, {c.zero(), c.one(), c.number(keel::i64{-1}), c.zero()});
    const SymMatrix expJ = matrixExp(c, J, t);
    CHECK(!expJ.empty());
    emitExpr("SYM_MATEXP_00", simplify(c, expJ(0, 0)));
    emitExpr("SYM_MATEXP_01", simplify(c, expJ(0, 1)));

    // --- 7. ODEs ---
    const OdeResult ode1 = dsolve1IVP(c, mustParse(c, "-2*y + exp(-x)"), x, y, c.zero(), c.one());
    CHECK(ode1.proved());
    emitExpr("SYM_ODE1_IVP", simplify(c, ode1.y));

    const OdeResult ode2 = dsolve2LinearIVP(
        c, c.one(), c.zero(), c.number(keel::i64{4}), c.zero(), x, c.zero(), c.one(), c.number(keel::i64{2}));
    CHECK(ode2.proved());
    emitExpr("SYM_ODE2_OSC", simplify(c, ode2.y));

    const OdeResult ode3 = dsolve2LinearIVP(
        c, c.one(), c.number(keel::i64{2}), c.number(keel::i64{5}), c.zero(), x, c.zero(), c.one(), c.zero());
    CHECK(ode3.proved());
    emitExpr("SYM_ODE2_DAMPED", simplify(c, ode3.y));

    // --- 8. Integral & discrete transforms + recurrences ---
    const LaplaceResult lap1 = laplace(c, mustParse(c, "t^2 * exp(-3*t)"), t, s);
    CHECK(lap1.proved());
    emitExpr("SYM_LAPLACE_1", simplify(c, lap1.F));

    const LaplaceResult lap2 = laplace(c, mustParse(c, "sin(2*t) * exp(-t)"), t, s);
    CHECK(lap2.proved());
    emitExpr("SYM_LAPLACE_2", simplify(c, lap2.F));

    const LaplaceResult ilap1 = ilaplace(c, mustParse(c, "(2*s + 3) / (s^2 + 4*s + 13)"), s, t);
    CHECK(ilap1.proved());
    emitExpr("SYM_ILAPLACE_1", simplify(c, ilap1.F));

    c.assume("a", AssumeDomain::Positive);
    const FourierResult fou1 = fourier(c, mustParse(c, "exp(-2 * t^2)"), t, w);
    CHECK(fou1.proved());
    emitExpr("SYM_FOURIER_GAUSS", simplify(c, fou1.F));

    const FourierResult fou2 = fourier(c, mustParse(c, "exp(-3 * abs(t))"), t, w);
    CHECK(fou2.proved());
    emitExpr("SYM_FOURIER_EXP", simplify(c, fou2.F));

    const ZTransResult zt1 = ztrans(c, mustParse(c, "n * 2^n"), n, z);
    CHECK(zt1.proved());
    emitExpr("SYM_ZTRANS_1", simplify(c, zt1.F));

    const ZTransResult izt1 = iztrans(c, mustParse(c, "z / (z^2 - 3*z + 2)"), z, n);
    CHECK(izt1.proved());
    emitExpr("SYM_IZTRANS_1", simplify(c, izt1.F));

    const RsolveResult fib = rsolveIVP(
        c, {c.number(keel::i64{-1}), c.number(keel::i64{-1}), c.one()}, c.zero(), n,
        {c.zero(), c.one()});
    CHECK(fib.proved());
    emitExpr("SYM_RSOLVE_FIB", simplify(c, fib.y));
}

} // namespace

int main(int argc, char** argv) {
    bool dump_ref = false;
    for (keel::i32 i = 1; i < argc; ++i) {
        if (keel::StringRef(argv[i]) == keel::StringRef("--dump-ref")) {
            dump_ref = true;
        }
    }

    if (dump_ref) {
        dump_reference();
        if (g_failed != 0) {
            keel::eprintln("test_cas --dump-ref: {} failure(s)", g_failed);
            return 1;
        }
        return 0;
    }

    test_parser_and_diff();
    test_simplify_and_integrate();
    test_ode_and_laplace();
    test_hardening_regressions();

    if (g_failed != 0) {
        keel::eprintln("test_cas: {} failure(s)", g_failed);
        return 1;
    }
    keel::println("test_cas: all tests passed");
    return 0;
}
