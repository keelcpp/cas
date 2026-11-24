# keelcpp/cas

Zero-STL, exception-free C++20 Computer Algebra System (CAS) library built on `keel::keel` and `keel::bignum`.

## Capabilities

- **Expression DAG & Canonicalization** (`keel/cas/core/expr.hpp`, `simplify.hpp`, `series.hpp`, `symsolve.hpp`): hash-consed immutable DAG, differentiation, substitution, Taylor/Laurent series, algebraic/trigonometric/logarithmic simplification, symbolic equation solver.
- **Exception-Free Pratt Parser** (`keel/cas/core/parser.hpp`): `tryParseExpr` parsing directly into a `Ctx`.
- **Exact Polynomial Algebra** (`keel/cas/poly/*`): univariate polynomials over Q and F_p, Sturm root isolation, algebraic numbers (`RootOf`), modular/Zassenhaus factorization, Groebner bases.
- **Symbolic Integration** (`keel/cas/integrate/*`, `keel/cas/rubi/*`): rational function integration (Hermite/Rothstein–Trager), heuristic & Rubi rule-based integration, derivative verification.
- **Symbolic Linear Algebra** (`keel/cas/linalg/symmatrix.hpp`): determinant, inverse, rank, RREF, nullspace, characteristic polynomial, eigenvalues/eigenvectors.
- **ODE & Integral Transforms** (`keel/cas/ode/dsolve.hpp`, `keel/cas/transform/*`): symbolic ODE solver, Laplace, Fourier, and Z-transforms (forward & inverse).

## Build & Test

```bash
cmake -S . -B build -DKEEL_CAS_NO_EXCEPTIONS_RTTI=ON -DKEEL_CAS_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
