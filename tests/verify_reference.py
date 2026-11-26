#!/usr/bin/env python3
"""Differential verification of keel_cas against SymPy and MATLAB R2025b."""

from __future__ import annotations

import math
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import sympy as sp
from sympy.parsing.sympy_parser import (
    convert_xor,
    parse_expr,
    standard_transformations,
)


TRANSFORMS = standard_transformations + (convert_xor,)
X, Y, Z, T, S, W, N, K = sp.symbols("x y z t s w n k")
SYMPY_LOCALS = {
    "x": X,
    "y": Y,
    "z": Z,
    "t": T,
    "s": S,
    "w": W,
    "n": N,
    "k": K,
    "pi": sp.pi,
    "e": sp.E,
    "E": sp.E,
    "I": sp.I,
    "sin": sp.sin,
    "cos": sp.cos,
    "tan": sp.tan,
    "cot": sp.cot,
    "sec": sp.sec,
    "csc": sp.csc,
    "asin": sp.asin,
    "acos": sp.acos,
    "atan": sp.atan,
    "acot": sp.acot,
    "sinh": sp.sinh,
    "cosh": sp.cosh,
    "tanh": sp.tanh,
    "exp": sp.exp,
    "log": sp.log,
    "sqrt": sp.sqrt,
    "abs": sp.Abs,
    "gamma": sp.gamma,
    "erf": sp.erf,
    "erfc": sp.erfc,
    "erfi": sp.erfi,
    "dirac": sp.DiracDelta,
    "heaviside": sp.Heaviside,
    "sign": sp.sign,
}


def parse_cas_expr(text: str) -> sp.Expr:
    return parse_expr(text.strip(), local_dict=SYMPY_LOCALS, transformations=TRANSFORMS)


def dump_keel_cas(exe_path: Path) -> dict[str, str]:
    proc = subprocess.run(
        [str(exe_path), "--dump-ref"],
        capture_output=True,
        text=True,
        check=True,
        timeout=120,
    )
    kv: dict[str, str] = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        if "=" in line:
            k, v = line.split("=", 1)
            kv[k.strip()] = v.strip()
    return kv


def find_matlab() -> str | None:
    for cand in [
        r"C:\Program Files\MATLAB\R2025b\bin\matlab.exe",
        r"C:\Program Files\MATLAB\R2024b\bin\matlab.exe",
    ]:
        if os.path.isfile(cand):
            return cand
    return None


def sym_expr_equal(a: sp.Expr, b: sp.Expr, test_points: dict[sp.Symbol, float] | None = None) -> tuple[bool, str]:
    diff = a - b
    try:
        simp = sp.simplify(sp.expand(diff))
        if simp == 0:
            return True, "exact(0)"
    except Exception:
        pass
    try:
        simp2 = sp.simplify(diff)
        if simp2 == 0:
            return True, "exact(0)"
    except Exception:
        pass
    # Numeric spot-check fallback for transcendental/radical forms that simplify slowly
    free = sorted(diff.free_symbols, key=lambda sym: sym.name)
    pts = test_points or {sym: 0.37 + 0.19 * (i + 1) for i, sym in enumerate(free)}
    try:
        val_a = complex(a.subs(pts).evalf(30))
        val_b = complex(b.subs(pts).evalf(30))
        err = abs(val_a - val_b)
        scale = max(1.0, abs(val_b))
        if err <= 1e-12 * scale:
            return True, f"num_err={err:.2e}"
        return False, f"a={val_a} vs b={val_b} (err={err:.2e})"
    except Exception as exc:
        return False, f"eval error: {exc}"


def verify_sympy(kv: dict[str, str]) -> tuple[int, int]:
    passed = 0
    failed = 0

    def check_num(key: str, ref_val: float, rtol: float = 1e-11, atol: float = 1e-12) -> None:
        nonlocal passed, failed
        if key not in kv:
            print(f"[FAIL][SymPy] {key}: missing from keel_cas output")
            failed += 1
            return
        got = float(kv[key])
        err = abs(got - ref_val)
        tol = atol + rtol * abs(ref_val)
        if math.isfinite(got) and err <= tol:
            passed += 1
        else:
            print(f"[FAIL][SymPy] {key}: got={got:.17g}, ref={ref_val:.17g}, err={err:.3e} > tol={tol:.3e}")
            failed += 1

    def check_sym(key: str, ref_expr: sp.Expr, test_points: dict[sp.Symbol, float] | None = None) -> None:
        nonlocal passed, failed
        if key not in kv:
            print(f"[FAIL][SymPy] {key}: missing from keel_cas output")
            failed += 1
            return
        got_expr = parse_cas_expr(kv[key])
        ok, detail = sym_expr_equal(got_expr, ref_expr, test_points)
        if ok:
            passed += 1
        else:
            print(f"[FAIL][SymPy] {key}: got='{kv[key]}' vs ref='{ref_expr}' ({detail})")
            failed += 1

    def check_antideriv(key: str, integrand: sp.Expr) -> None:
        nonlocal passed, failed
        if key not in kv:
            print(f"[FAIL][SymPy] {key}: missing from keel_cas output")
            failed += 1
            return
        got_F = parse_cas_expr(kv[key])
        dF = sp.diff(got_F, X)
        ok, detail = sym_expr_equal(dF, integrand, {X: 0.73})
        if ok:
            passed += 1
        else:
            print(f"[FAIL][SymPy] {key}: d/dx('{kv[key]}') != '{integrand}' ({detail})")
            failed += 1

    # 1. Numeric special functions via SymPy/mpmath (30-digit precision)
    num_refs: dict[str, sp.Expr] = {
        "NUM_BESSELJ_0_1P5": sp.besselj(0, sp.Rational(3, 2)),
        "NUM_BESSELJ_2_3P0": sp.besselj(2, 3),
        "NUM_BESSELY_1_2P0": sp.bessely(1, 2),
        "NUM_BESSELI_1_1P5": sp.besseli(1, sp.Rational(3, 2)),
        "NUM_BESSELK_0_1P0": sp.besselk(0, 1),
        "NUM_AIRY_AI_0": sp.airyai(0),
        "NUM_AIRY_AI_1": sp.airyai(1),
        "NUM_AIRY_AI_M2": sp.airyai(-2),
        "NUM_AIRY_BI_1": sp.airybi(1),
        "NUM_AIRY_AIP_0P5": sp.airyaiprime(sp.Rational(1, 2)),
        "NUM_AIRY_BIP_0P5": sp.airybiprime(sp.Rational(1, 2)),
        "NUM_ZETA_2": sp.zeta(2),
        "NUM_ZETA_3": sp.zeta(3),
        "NUM_ZETA_M1": sp.zeta(-1),
        "NUM_ETA_2": sp.dirichlet_eta(2),
        "NUM_DILOG_0P5": sp.polylog(2, sp.Rational(1, 2)),
        "NUM_POLYLOG_3_0P5": sp.polylog(3, sp.Rational(1, 2)),
        "NUM_DIGAMMA_2P5": sp.digamma(sp.Rational(5, 2)),
        "NUM_TRIGAMMA_1P5": sp.polygamma(1, sp.Rational(3, 2)),
        "NUM_ERF_0P75": sp.erf(sp.Rational(3, 4)),
        "NUM_ERFI_0P75": sp.erfi(sp.Rational(3, 4)),
        "NUM_SI_1P5": sp.Si(sp.Rational(3, 2)),
        "NUM_CI_1P5": sp.Ci(sp.Rational(3, 2)),
        "NUM_EI_1P5": sp.Ei(sp.Rational(3, 2)),
        "NUM_FRESNELS_1P2": sp.fresnels(sp.Rational(6, 5)),
        "NUM_FRESNELC_1P2": sp.fresnelc(sp.Rational(6, 5)),
        "NUM_ELLIPTICK_0P5": sp.elliptic_k(sp.Rational(1, 2)),
        "NUM_ELLIPTICE_0P5": sp.elliptic_e(sp.Rational(1, 2)),
        "NUM_LEGENDRE_4_0P6": sp.legendre(4, sp.Rational(3, 5)),
        "NUM_CHEBYSHEVT_5_0P4": sp.chebyshevt(5, sp.Rational(2, 5)),
        "NUM_HERMITEH_4_1P2": sp.hermite(4, sp.Rational(6, 5)),
        "NUM_LAGUERREL_3_0P8": sp.laguerre(3, sp.Rational(4, 5)),
        "NUM_BETA_2P5_3P5": sp.beta(sp.Rational(5, 2), sp.Rational(7, 2)),
        "NUM_POCHHAMMER_1P5_4": sp.rf(sp.Rational(3, 2), 4),
        "NUM_IGAMMA_2P5_1P2": sp.uppergamma(sp.Rational(5, 2), sp.Rational(6, 5)),
        "NUM_GAMMAINC_2P5_1P2": sp.lowergamma(sp.Rational(5, 2), sp.Rational(6, 5)),
        "NUM_1F1_1P5_2P5_0P8": sp.hyper([sp.Rational(3, 2)], [sp.Rational(5, 2)], sp.Rational(4, 5)),
        "NUM_2F1_0P5_1P5_2P5_0P4": sp.hyper([sp.Rational(1, 2), sp.Rational(3, 2)], [sp.Rational(5, 2)], sp.Rational(2, 5)),
        "NUM_ACOT_0": sp.pi / 2,
        "NUM_ACOT_1": sp.pi / 4,
        # keel_cas acot branch is (0, pi) -> acot(-1) = 3*pi/4 (principal positive branch)
        "NUM_ACOT_M1": 3 * sp.pi / 4,
    }
    for key, expr in num_refs.items():
        check_num(key, float(expr.evalf(30)))

    # 2. Diff & simplify
    check_sym("SYM_DIFF_1", sp.diff(X**3 * sp.sin(2 * X) + sp.exp(-(X**2)), X))
    check_sym("SYM_DIFF_2", sp.diff(sp.atan(X**2), X))
    check_sym("SYM_SIMP_TRIG", sp.cos(X) ** 2)
    check_sym("SYM_SIMP_GAMMA", X * (X + 1) * (X + 2))

    # 3. Series, limits, residues, symsum, Pade
    check_sym("SYM_TAYLOR_SINC", sp.series(sp.sin(X) / X, X, 0, 7).removeO())
    check_sym("SYM_LAURENT_1", sp.series(1 / (X * (X - 1)), X, 0, 4).removeO())
    check_sym("SYM_LIM_1", sp.limit((sp.sin(X) - X) / X**3, X, 0))
    check_sym("SYM_LIM_2", sp.limit(X * sp.log(X), X, 0, "+"))
    check_sym("SYM_LIM_3", sp.limit((1 + 2 / X) ** X, X, sp.oo))
    check_sym("SYM_RES_1", sp.residue(1 / ((Z - 1) ** 2 * (Z + 1)), Z, 1))
    check_sym("SYM_SUM_CUBES", sp.summation(K**3, (K, 1, N)))
    check_sym("SYM_SUM_ALT2", sp.summation((-1) ** K / (2 * K + 2) ** 2, (K, 0, sp.oo)))
    check_sym("SYM_SUM_ZETA4", sp.summation(1 / K**4, (K, 1, sp.oo)))
    check_sym("SYM_SUM_LEIBNIZ", sp.summation((-1) ** K / (2 * K + 1), (K, 0, sp.oo)))
    check_sym("SYM_PADE_EXP22", (1 + X / 2 + X**2 / 12) / (1 - X / 2 + X**2 / 12))

    # 4. Integration (indefinite via derivative check, definite via exact value)
    check_antideriv("SYM_INT_RAT1", 1 / (X**3 + 1))
    check_antideriv("SYM_INT_X2EXP", X**2 * sp.exp(-X))
    check_antideriv("SYM_INT_X4P1", 1 / (X**4 + 1))
    check_antideriv("SYM_INT_SIN3", sp.sin(X) ** 3)

    check_sym("SYM_DINT_1", sp.integrate(X**2 * sp.exp(X), (X, 0, 1)))
    check_sym("SYM_DINT_2", sp.integrate(sp.exp(-3 * X), (X, 0, sp.oo)))
    check_sym("SYM_DINT_3", sp.integrate(sp.sin(X) ** 2, (X, 0, sp.pi)))
    check_sym("SYM_DINT_4", sp.integrate(1 / (X**2 + 4), (X, 0, sp.oo)))

    # 5. Roots, factorMulti, Groebner basis
    got_roots = sorted(float(parse_cas_expr(kv[f"SYM_ROOT_{i}"]).evalf()) for i in range(3))
    if got_roots == [1.0, 2.0, 3.0]:
        passed += 1
    else:
        print(f"[FAIL][SymPy] SYM_ROOTS: got={got_roots}, expected=[1, 2, 3]")
        failed += 1

    check_sym("SYM_FACTORMULTI_1", X**4 - Y**4)

    gb_ref = list(sp.groebner([X**2 - Y, X * Y - 1], X, Y, order="lex"))
    gb_got = [parse_cas_expr(kv["SYM_GB_0"]), parse_cas_expr(kv["SYM_GB_1"])]
    gb_ok = all(any(sym_expr_equal(g, r)[0] for g in gb_got) for r in gb_ref)
    if gb_ok:
        passed += 1
    else:
        print(f"[FAIL][SymPy] SYM_GB: got={gb_got} vs ref={gb_ref}")
        failed += 1

    # 6. Symbolic matrices
    A = sp.Matrix([[2, X], [1, 3]])
    check_sym("SYM_MAT_DET", A.det())
    Ainv = A.inv()
    check_sym("SYM_MAT_INV00", Ainv[0, 0])
    check_sym("SYM_MAT_INV01", Ainv[0, 1])
    check_sym("SYM_MAT_CHARPOLY", A.charpoly(S).as_expr())
    J = sp.Matrix([[0, 1], [-1, 0]])
    expJ = (J * T).exp()
    check_sym("SYM_MATEXP_00", expJ[0, 0])
    check_sym("SYM_MATEXP_01", expJ[0, 1])

    # 7. ODEs
    check_sym("SYM_ODE1_IVP", sp.exp(-X))
    check_sym("SYM_ODE2_OSC", sp.cos(2 * X) + sp.sin(2 * X))
    check_sym("SYM_ODE2_DAMPED", sp.exp(-X) * (sp.cos(2 * X) + sp.Rational(1, 2) * sp.sin(2 * X)))

    # 8. Transforms & recurrences
    check_sym("SYM_LAPLACE_1", sp.laplace_transform(T**2 * sp.exp(-3 * T), T, S, noconds=True))
    check_sym("SYM_LAPLACE_2", sp.laplace_transform(sp.sin(2 * T) * sp.exp(-T), T, S, noconds=True))
    check_sym("SYM_ILAPLACE_1", sp.inverse_laplace_transform((2 * S + 3) / (S**2 + 4 * S + 13), S, T))
    # Fourier convention in keel_cas: int_{-inf}^{inf} f(t) exp(-i w t) dt
    check_sym("SYM_FOURIER_GAUSS", sp.sqrt(sp.pi / 2) * sp.exp(-(W**2) / 8))
    check_sym("SYM_FOURIER_EXP", 6 / (9 + W**2))
    check_sym("SYM_ZTRANS_1", 2 * Z / (Z - 2) ** 2)
    check_sym("SYM_IZTRANS_1", 2**N - 1, {N: 5.0})
    phi = (1 + sp.sqrt(5)) / 2
    psi = (1 - sp.sqrt(5)) / 2
    check_sym("SYM_RSOLVE_FIB", (phi**N - psi**N) / sp.sqrt(5), {N: 6.0})

    return passed, failed


def verify_matlab(kv: dict[str, str], matlab_exe: str) -> tuple[int, int]:
    script_lines = [
        "format long g;",
        "syms x y z t s w n k real;",
        "fprintf('NUM_BESSELJ_0_1P5=%.17g\\n', besselj(0, 1.5));",
        "fprintf('NUM_BESSELJ_2_3P0=%.17g\\n', besselj(2, 3.0));",
        "fprintf('NUM_BESSELY_1_2P0=%.17g\\n', bessely(1, 2.0));",
        "fprintf('NUM_BESSELI_1_1P5=%.17g\\n', besseli(1, 1.5));",
        "fprintf('NUM_BESSELK_0_1P0=%.17g\\n', besselk(0, 1.0));",
        "fprintf('NUM_AIRY_AI_0=%.17g\\n', airy(0, 0.0));",
        "fprintf('NUM_AIRY_AI_1=%.17g\\n', airy(0, 1.0));",
        "fprintf('NUM_AIRY_AI_M2=%.17g\\n', airy(0, -2.0));",
        "fprintf('NUM_AIRY_BI_1=%.17g\\n', airy(2, 1.0));",
        "fprintf('NUM_AIRY_AIP_0P5=%.17g\\n', airy(1, 0.5));",
        "fprintf('NUM_AIRY_BIP_0P5=%.17g\\n', airy(3, 0.5));",
        "fprintf('NUM_ZETA_2=%.17g\\n', double(zeta(sym(2))));",
        "fprintf('NUM_ZETA_3=%.17g\\n', double(zeta(sym(3))));",
        "fprintf('NUM_ZETA_M1=%.17g\\n', double(zeta(sym(-1))));",
        "fprintf('NUM_ETA_2=%.17g\\n', double((1 - 2^(1 - 2)) * zeta(sym(2))));",
        "fprintf('NUM_DILOG_0P5=%.17g\\n', double(polylog(2, sym(1)/2)));",
        "fprintf('NUM_POLYLOG_3_0P5=%.17g\\n', double(polylog(3, sym(1)/2)));",
        "fprintf('NUM_DIGAMMA_2P5=%.17g\\n', psi(0, 2.5));",
        "fprintf('NUM_TRIGAMMA_1P5=%.17g\\n', psi(1, 1.5));",
        "fprintf('NUM_ERF_0P75=%.17g\\n', erf(0.75));",
        "fprintf('NUM_ERFI_0P75=%.17g\\n', double(erfi(sym(3)/4)));",
        "fprintf('NUM_SI_1P5=%.17g\\n', double(sinint(sym(3)/2)));",
        "fprintf('NUM_CI_1P5=%.17g\\n', double(cosint(sym(3)/2)));",
        "fprintf('NUM_EI_1P5=%.17g\\n', double(ei(sym(3)/2)));",
        "fprintf('NUM_FRESNELS_1P2=%.17g\\n', double(fresnels(sym(6)/5)));",
        "fprintf('NUM_FRESNELC_1P2=%.17g\\n', double(fresnelc(sym(6)/5)));",
        "fprintf('NUM_ELLIPTICK_0P5=%.17g\\n', double(ellipticK(sym(1)/2)));",
        "fprintf('NUM_ELLIPTICE_0P5=%.17g\\n', double(ellipticE(sym(1)/2)));",
        "fprintf('NUM_LEGENDRE_4_0P6=%.17g\\n', double(legendreP(4, sym(3)/5)));",
        "fprintf('NUM_CHEBYSHEVT_5_0P4=%.17g\\n', double(chebyshevT(5, sym(2)/5)));",
        "fprintf('NUM_HERMITEH_4_1P2=%.17g\\n', double(hermiteH(4, sym(6)/5)));",
        "fprintf('NUM_LAGUERREL_3_0P8=%.17g\\n', double(laguerreL(3, sym(4)/5)));",
        "fprintf('NUM_BETA_2P5_3P5=%.17g\\n', beta(2.5, 3.5));",
        "fprintf('NUM_POCHHAMMER_1P5_4=%.17g\\n', double(pochhammer(sym(3)/2, 4)));",
        "fprintf('NUM_IGAMMA_2P5_1P2=%.17g\\n', double(igamma(sym(5)/2, sym(6)/5)));",
        "fprintf('NUM_GAMMAINC_2P5_1P2=%.17g\\n', double(gamma(sym(5)/2) - igamma(sym(5)/2, sym(6)/5)));",
        "fprintf('NUM_1F1_1P5_2P5_0P8=%.17g\\n', double(hypergeom(sym(3)/2, sym(5)/2, sym(4)/5)));",
        "fprintf('NUM_2F1_0P5_1P5_2P5_0P4=%.17g\\n', double(hypergeom([sym(1)/2, sym(3)/2], sym(5)/2, sym(2)/5)));",
        "fprintf('NUM_ACOT_0=%.17g\\n', pi/2);",
        "fprintf('NUM_ACOT_1=%.17g\\n', acot(1.0));",
        "fprintf('NUM_ACOT_M1=%.17g\\n', pi + acot(-1.0));",
        # Symbolic evaluations in MATLAB Symbolic Math Toolbox
        "fprintf('SYM_DIFF_1=%.17g\\n', double(subs(diff(x^3*sin(2*x) + exp(-x^2), x), x, 0.73)));",
        "fprintf('SYM_DIFF_2=%.17g\\n', double(subs(diff(atan(x^2), x), x, 0.73)));",
        "fprintf('SYM_SIMP_TRIG=%.17g\\n', double(subs(simplify((sin(x)^2+cos(x)^2)/(1+tan(x)^2)), x, 0.73)));",
        "fprintf('SYM_SIMP_GAMMA=%.17g\\n', double(subs(gamma(x+3)/gamma(x), x, 0.73)));",
        "fprintf('SYM_TAYLOR_SINC=%.17g\\n', double(subs(taylor(sin(x)/x, x, 'Order', 7), x, 0.73)));",
        "fprintf('SYM_LAURENT_1=%.17g\\n', double(subs(taylor(1/(x-1), x, 'Order', 5) / x, x, 0.73)));",
        "fprintf('SYM_LIM_1=%.17g\\n', double(limit((sin(x)-x)/x^3, x, 0)));",
        "fprintf('SYM_LIM_2=%.17g\\n', double(limit(x*log(x), x, 0, 'right')));",
        "fprintf('SYM_LIM_3=%.17g\\n', double(limit((1+2/x)^x, x, inf)));",
        "fprintf('SYM_RES_1=%.17g\\n', double(limit(diff((z-1)^2 / ((z-1)^2*(z+1)), z), z, 1)));",
        "fprintf('SYM_SUM_CUBES=%.17g\\n', double(subs(symsum(k^3, k, 1, n), n, 5)));",
        "fprintf('SYM_SUM_ALT2=%.17g\\n', double(symsum((-1)^k / (2*k + 2)^2, k, 0, inf)));",
        "fprintf('SYM_SUM_ZETA4=%.17g\\n', double(symsum(1 / k^4, k, 1, inf)));",
        "fprintf('SYM_SUM_LEIBNIZ=%.17g\\n', double(symsum((-1)^k / (2*k + 1), k, 0, inf)));",
        "fprintf('SYM_PADE_EXP22=%.17g\\n', double(subs(pade(exp(x), x, 'Order', [2 2]), x, 0.73)));",
        "fprintf('SYM_DINT_1=%.17g\\n', double(int(x^2*exp(x), x, 0, 1)));",
        "fprintf('SYM_DINT_2=%.17g\\n', double(int(exp(-3*x), x, 0, inf)));",
        "fprintf('SYM_DINT_3=%.17g\\n', double(int(sin(x)^2, x, 0, pi)));",
        "fprintf('SYM_DINT_4=%.17g\\n', double(int(1/(x^2+4), x, 0, inf)));",
        "A = [2, x; 1, 3];",
        "fprintf('SYM_MAT_DET=%.17g\\n', double(subs(det(A), x, 0.73)));",
        "Ai = inv(A);",
        "fprintf('SYM_MAT_INV00=%.17g\\n', double(subs(Ai(1,1), x, 0.73)));",
        "fprintf('SYM_MAT_INV01=%.17g\\n', double(subs(Ai(1,2), x, 0.73)));",
        "fprintf('SYM_MAT_CHARPOLY=%.17g\\n', double(subs(charpoly(A, s), [x, s], [0.73, 1.11])));",
        "eJ = expm([0, 1; -1, 0] * t);",
        "fprintf('SYM_MATEXP_00=%.17g\\n', double(subs(eJ(1,1), t, 0.73)));",
        "fprintf('SYM_MATEXP_01=%.17g\\n', double(subs(eJ(1,2), t, 0.73)));",
        "syms yy(x);",
        "s1 = dsolve(diff(yy, x) == -2*yy + exp(-x), yy(0) == 1);",
        "fprintf('SYM_ODE1_IVP=%.17g\\n', double(subs(s1, x, 0.73)));",
        "s2 = dsolve(diff(yy, x, 2) + 4*yy == 0, yy(0) == 1, subs(diff(yy, x), x, 0) == 2);",
        "fprintf('SYM_ODE2_OSC=%.17g\\n', double(subs(s2, x, 0.73)));",
        "s3 = dsolve(diff(yy, x, 2) + 2*diff(yy, x) + 5*yy == 0, yy(0) == 1, subs(diff(yy, x), x, 0) == 0);",
        "fprintf('SYM_ODE2_DAMPED=%.17g\\n', double(subs(s3, x, 0.73)));",
        "fprintf('SYM_LAPLACE_1=%.17g\\n', double(subs(laplace(t^2*exp(-3*t), t, s), s, 1.11)));",
        "fprintf('SYM_LAPLACE_2=%.17g\\n', double(subs(laplace(sin(2*t)*exp(-t), t, s), s, 1.11)));",
        "fprintf('SYM_ILAPLACE_1=%.17g\\n', double(subs(ilaplace((2*s+3)/(s^2+4*s+13), s, t), t, 0.73)));",
        "fprintf('SYM_FOURIER_GAUSS=%.17g\\n', double(subs(fourier(exp(-2*t^2), t, w), w, 0.73)));",
        "fprintf('SYM_FOURIER_EXP=%.17g\\n', double(subs(fourier(exp(-3*abs(t)), t, w), w, 0.73)));",
        "fprintf('SYM_ZTRANS_1=%.17g\\n', double(subs(ztrans(n*2^n, n, z), z, 3.5)));",
        "fprintf('SYM_IZTRANS_1=%.17g\\n', double(subs(iztrans(z/(z^2 - 3*z + 2), z, n), n, 5)));",
        "fprintf('SYM_RSOLVE_FIB=%.17g\\n', double(fibonacci(6)));",
        "fprintf('MATLAB_DONE=1\\n');",
    ]

    with tempfile.NamedTemporaryFile("w", suffix=".m", delete=False, encoding="utf-8") as tmp:
        tmp.write("\n".join(script_lines) + "\n")
        m_path = tmp.name

    try:
        proc = subprocess.run(
            [matlab_exe, "-batch", f"run('{m_path.replace(os.sep, '/')}')"],
            capture_output=True,
            text=True,
            timeout=180,
        )
    finally:
        try:
            os.unlink(m_path)
        except OSError:
            pass

    mkv: dict[str, str] = {}
    for line in proc.stdout.splitlines():
        line = line.strip()
        if "=" in line:
            k, v = line.split("=", 1)
            mkv[k.strip()] = v.strip()

    if mkv.get("MATLAB_DONE") != "1":
        print(f"[FAIL][MATLAB] did not reach MATLAB_DONE=1 (rc={proc.returncode})")
        print("STDOUT:", proc.stdout[-1000:])
        print("STDERR:", proc.stderr[-1000:])
        return 0, 1

    # Map symbolic keys to evaluation points matching the MATLAB script
    eval_points: dict[str, dict[sp.Symbol, float]] = {
        "SYM_DIFF_1": {X: 0.73},
        "SYM_DIFF_2": {X: 0.73},
        "SYM_SIMP_TRIG": {X: 0.73},
        "SYM_SIMP_GAMMA": {X: 0.73},
        "SYM_TAYLOR_SINC": {X: 0.73},
        "SYM_LAURENT_1": {X: 0.73},
        "SYM_LIM_1": {},
        "SYM_LIM_2": {},
        "SYM_LIM_3": {},
        "SYM_RES_1": {},
        "SYM_SUM_CUBES": {N: 5.0},
        "SYM_SUM_ALT2": {},
        "SYM_SUM_ZETA4": {},
        "SYM_SUM_LEIBNIZ": {},
        "SYM_PADE_EXP22": {X: 0.73},
        "SYM_DINT_1": {},
        "SYM_DINT_2": {},
        "SYM_DINT_3": {},
        "SYM_DINT_4": {},
        "SYM_MAT_DET": {X: 0.73},
        "SYM_MAT_INV00": {X: 0.73},
        "SYM_MAT_INV01": {X: 0.73},
        "SYM_MAT_CHARPOLY": {X: 0.73, S: 1.11},
        "SYM_MATEXP_00": {T: 0.73},
        "SYM_MATEXP_01": {T: 0.73},
        "SYM_ODE1_IVP": {X: 0.73},
        "SYM_ODE2_OSC": {X: 0.73},
        "SYM_ODE2_DAMPED": {X: 0.73},
        "SYM_LAPLACE_1": {S: 1.11},
        "SYM_LAPLACE_2": {S: 1.11},
        "SYM_ILAPLACE_1": {T: 0.73},
        "SYM_FOURIER_GAUSS": {W: 0.73},
        "SYM_FOURIER_EXP": {W: 0.73},
        "SYM_ZTRANS_1": {Z: 3.5},
        "SYM_IZTRANS_1": {N: 5.0},
        "SYM_RSOLVE_FIB": {N: 6.0},
    }

    passed = 0
    failed = 0
    for key, ref_str in mkv.items():
        if key == "MATLAB_DONE":
            continue
        if key not in kv:
            print(f"[FAIL][MATLAB] {key}: missing from keel_cas output")
            failed += 1
            continue
        ref_val = float(ref_str)
        if key.startswith("NUM_"):
            got_val = float(kv[key])
        else:
            expr = parse_cas_expr(kv[key])
            got_val = float(complex(expr.subs(eval_points[key]).evalf(30)).real)
        err = abs(got_val - ref_val)
        tol = 1e-11 + 1e-10 * abs(ref_val)
        if math.isfinite(got_val) and err <= tol:
            passed += 1
        else:
            print(f"[FAIL][MATLAB] {key}: got={got_val:.17g}, matlab={ref_val:.17g}, err={err:.3e} > tol={tol:.3e}")
            failed += 1

    return passed, failed


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: verify_reference.py <path-to-test_cas.exe>")
        return 2
    exe = Path(sys.argv[1])
    if not exe.is_file():
        print(f"Executable not found: {exe}")
        return 2

    kv = dump_keel_cas(exe)
    sp_pass, sp_fail = verify_sympy(kv)
    print(f"SymPy verification: {sp_pass} passed, {sp_fail} failed")

    matlab_exe = find_matlab()
    if matlab_exe:
        ml_pass, ml_fail = verify_matlab(kv, matlab_exe)
        print(f"MATLAB R2025b verification: {ml_pass} passed, {ml_fail} failed")
    else:
        ml_pass, ml_fail = 0, 0
        print("MATLAB not found; skipped MATLAB cross-check")

    total_fail = sp_fail + ml_fail
    if total_fail != 0:
        print(f"TOTAL FAILURES: {total_fail}")
        return 1
    print(f"ALL REFERENCE CHECKS PASSED ({sp_pass} SymPy + {ml_pass} MATLAB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
