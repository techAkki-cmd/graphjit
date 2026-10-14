# GraphJIT

**A small, end-to-end tensor compiler built in modern C++20 on LLVM.**

GraphJIT takes a mathematical expression through a handwritten lexer and
recursive-descent parser, builds an owned AST, runs analysis and optimization
passes, lowers the result to LLVM IR, and executes the generated machine code
in-process with LLVM ORC JIT.

This project is deliberately built from first principles. There is no
Flex/Bison frontend, no external CLI framework, and no hidden tensor runtime.
The interesting parts—source locations, AST ownership, shape inference,
rewriting, loop construction, PHI wiring, module ownership, and JIT execution—
are visible in the code.

![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)
![LLVM](https://img.shields.io/badge/LLVM-23.1.3-262D3A?logo=llvm)
![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C?logo=cmake)

[Quick demo](#see-it-work) · [Architecture](#compiler-pipeline) ·
[Build](#build) · [Testing](#testing-and-verification) ·
[Benchmark](#benchmark) · [Limitations](#current-scope-and-limitations)

## See it work

Build GraphJIT, then ask it to show the optimized AST, emit LLVM IR, and run
the native kernel:

```bash
./build/graphjit --emit-ast --emit-llvm --run "A + B"
```

The important parts of the output are:

```text
[AST]
[Fused Kernel]
  BinaryOpNode: +
    Left: VariableNode: A
    Right: VariableNode: B

define void @__tensor_expr(ptr %A_ptr, ptr %B_ptr, ptr %Out_ptr) {
entry:
  br label %loop.cond.element

loop.cond.element:
  %element = phi i64 [ 0, %entry ], [ %element.next, %loop.inc.element ]
  %element.cond = icmp slt i64 %element, 3
  ; ... GEPs, loads, fadd, store, and the loop backedge ...
}

Input A: [ 1.0, 2.0, 3.0 ]
Input B: [ 10.0, 20.0, 30.0 ]
Output:  [ 11.0, 22.0, 33.0 ]
```

That is one command exercising the lexer, parser, AST, constant folding,
kernel-fusion marking, shape inference, LLVM lowering, verification, ORC
materialization, and native execution.

## Compiler pipeline

```mermaid
flowchart LR
    A[Source expression] --> B[Lexer]
    B --> C[Recursive-descent parser]
    C --> D[Owned AST]
    D --> E[Constant folding]
    E --> F[Kernel-fusion marking]
    F --> G[Shape inference]
    G --> H[LLVM IR generation]
    H --> I[LLVM verifier]
    I --> J[ORC LLJIT]
    J --> K[Native machine code]
    K --> L[Caller-owned buffers]
```

| Stage | What is implemented |
| --- | --- |
| Lexer | ASCII identifiers, keywords, integers, decimal/exponent floats, operators, punctuation, comments, CRLF/LF handling, and one-based source locations |
| Parser | Complete-expression recursive descent with grouping, left associativity, and explicit precedence for `+ -`, `* /`, and `@` |
| AST | Move-only `std::unique_ptr` ownership, visitor double dispatch, printer, function nodes, and fused-kernel nodes |
| Shape analysis | Scalar propagation, strict element-wise compatibility, scalar broadcasting, rank-2 matrix multiplication, and per-node annotations |
| Optimization | Bottom-up finite constant folding and maximal element-wise fusion-region discovery |
| Scalar backend | Verified `double @__anon_expr()` IR for `+`, `-`, `*`, and `/` |
| Tensor backend | Verified row-major loop IR for tensor addition and rank-2 matrix multiplication, including nested PHI-driven loops |
| JIT runtime | Native target setup, safe module/context transfer, ORC symbol lookup, and direct execution against C++ buffers |
| Driver | Dependency-free CLI, AST/IR emission, scalar and tensor execution, deterministic diagnostics, and benchmarking |

## Why the implementation is interesting

### Ownership is explicit

AST nodes are move-only and own their children with `std::unique_ptr`. Rewriter
passes transfer ownership out of a node, rewrite the subtree, and restore or
replace it. There is no garbage collector and no shared ownership to obscure
node lifetime.

### Analysis does not mutate syntax

Shape inference stores results in an external map keyed by AST node address.
The tree remains syntax; analysis results remain analysis. Rewriting invalidates
old annotations by design, so the driver optimizes first and infers shapes on
the final tree.

### LLVM generation is transactional

IR generation verifies every emitted function. If lowering or verification
fails, the incomplete function is removed and transient generator state is
cleared. A failed compile does not leave a corrupt module behind.

### Tensor loops are real control flow

Tensor addition emits a flattened `i64` loop with a PHI induction variable,
signed comparison, GEP address calculation, loads, `fadd`, store, increment,
and backedge. Matrix multiplication emits nested `i`, `j`, and `k` loops and
computes row-major flat indices for the dot product.

### The JIT owns what it executes

LLVM modules cannot outlive their `LLVMContext`. GraphJIT transfers both in one
ownership bundle, resets the builder before transfer, wraps the pair in an ORC
`ThreadSafeModule`, and prevents the generator from being reused afterward.

## Language snapshot

GraphJIT currently parses one complete expression:

```ebnf
expression = term, { ("+" | "-"), term } ;
term       = matmul, { ("*" | "/"), matmul } ;
matmul     = factor, { "@", factor } ;
factor     = "(", expression, ")" | primary ;
primary    = float-literal | identifier ;
```

The current precedence, from lowest to highest, is:

| Precedence | Operators | Associativity |
| --- | --- | --- |
| 1 | `+`, `-` | Left |
| 2 | `*`, `/` | Left |
| 3 | `@` | Left |
| 4 | Parentheses | Explicit grouping |

The lexer already recognizes keywords, assignment, integer literals, colons,
braces, commas, and semicolons for later language phases. The expression parser
intentionally accepts only identifiers and floating-point literals today.

## Build

### Requirements

- A C++20 compiler
- CMake 3.20 or newer
- LLVM development files with the Core, Support, ORC JIT, and native target
  components

The verified development configuration is LLVM 23.1.3, but the project uses
LLVM's supported CMake package rather than hard-coded library paths.

### macOS with Homebrew

```bash
brew install cmake llvm

cmake -S . -B build \
  -DLLVM_DIR="$(brew --prefix llvm)/lib/cmake/llvm" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --parallel
```

### Generic LLVM installation

```bash
cmake -S . -B build \
  -DLLVM_DIR="$(llvm-config --cmakedir)" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --parallel
```

The final executable is `build/graphjit`.

### Build without LLVM

The frontend, AST, optimizers, and shape analysis remain independently
buildable when LLVM is unavailable:

```bash
cmake -S . -B build-no-llvm \
  -DGRAPHJIT_ENABLE_LLVM=OFF \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-no-llvm --parallel
ctest --test-dir build-no-llvm --output-on-failure
```

## CLI reference

```text
Usage: graphjit [options] [expression]

Options:
  --emit-ast       Print the optimized abstract syntax tree
  --emit-llvm      Print generated LLVM IR
  --run            JIT-compile and execute the expression
  --benchmark      Benchmark the fixed expression A + B
  --size N         Set the tensor element count
  --help, -h       Show help
```

If no expression is supplied, the driver uses `A + B`. Normal execution uses
three elements by default; benchmarking uses 10,000,000. `--size` overrides
either default.

### Inspect an optimized tree

```bash
./build/graphjit --emit-ast "A + B"
```

### See scalar constant folding in LLVM IR

```bash
./build/graphjit --emit-ast --emit-llvm --run "2.0 + 3.0 * 4.0"
```

The expression folds to one literal before lowering:

```llvm
define double @__anon_expr() {
entry:
  ret double 1.400000e+01
}
```

### Execute a tensor kernel

```bash
./build/graphjit --run --size 8 "A + B"
```

For large buffers, the driver prints a sample and checksum rather than flooding
the terminal.

### Inspect matrix shape safety

```bash
./build/graphjit_shape_demo
```

```text
[INFO] Running shape inference on: A @ B + 2.0 * (C - D)
[INFO] Root node deduced shape: [128, 256]

[INFO] Running shape inference on: A @ C
[ERROR] ShapeMismatchException: Cannot multiply matrices of shape [128, 64]
and [128, 256]. Inner dimensions 64 and 128 must match.
```

## Testing and verification

Run the complete LLVM-enabled suite:

```bash
ctest --test-dir build --output-on-failure
```

The suite covers:

- Every token family, comments, line endings, malformed numbers, unknown
  characters, positions, and stable EOF behavior
- AST ownership, accessors, visitor dispatch, validation, and exact printer
  output
- Precedence, associativity, grouping, trailing input, malformed expressions,
  and numeric conversion failures
- Scalar broadcasting, element-wise compatibility, matrix multiplication,
  undefined symbols, and annotation lifecycle
- Deep constant folding, non-finite arithmetic preservation, fusion barriers,
  idempotence, and function-body rewriting
- Scalar and tensor LLVM structure, PHIs, backedges, GEPs, temporary buffers,
  verifier success, and transactional failure cleanup
- ORC execution, symbol lookup, module/context validation, and ownership
  transfer lifecycle
- CLI validation, AST/IR emission, scalar execution, tensor execution, and a
  correctness-checked benchmark run

### Sanitizers

```bash
cmake -S . -B build-sanitize \
  -DLLVM_DIR="$(llvm-config --cmakedir)" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"

cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

### Verified baseline

The following checks were run from clean CMake configurations with the project
version set to `0.10.0`:

| Configuration | Result |
| --- | --- |
| Release, LLVM enabled | 10/10 test executables passed |
| Release, LLVM disabled | 6/6 applicable test executables passed |
| Debug, AddressSanitizer + UndefinedBehaviorSanitizer | 10/10 test executables passed |
| Full 10,000,000-element benchmark | Every output element verified within tolerance |

Test success is the correctness signal. Performance numbers are reported
separately and are never used to hide a failed output comparison.

## Benchmark

```bash
./build/graphjit --benchmark
```

The benchmark:

1. Allocates two random input vectors and two output vectors.
2. Uses a fixed PRNG seed so the workload is reproducible.
3. Compiles and materializes the JIT kernel before timing.
4. Warms up both implementations once.
5. Alternates native-first and JIT-first ordering across five trials.
6. Reports the best time and effective bandwidth based on two reads plus one
   write.
7. Checks every JIT result against the native output with absolute and relative
   tolerance.
8. Prints a checksum so the computed result remains externally observable.

One verified local run produced:

```text
[INFO] Allocating 10000000 elements (160.0 MB inputs, 320.0 MB total buffers)...

[BENCHMARK] Best of 5 measured runs after one warmup
Native C++: 2.595 ms (92.47 GB/s)
GraphJIT:   2.790 ms (86.02 GB/s)
Checksum:   -23.452770694231

[SUCCESS] Output verified. GraphJIT achieved 93.0% of native C++ performance.
```

Environment for that result:

| Component | Value |
| --- | --- |
| CPU | Apple M4 |
| Memory | 16 GB |
| Architecture | arm64 |
| Build | Release |
| Host compiler | Apple Clang 17.0.0 |
| LLVM | 23.1.3 |

This is a development benchmark, not a universal performance claim. Results
depend on hardware, compiler flags, thermal state, and LLVM version. Compilation,
allocation, initialization, warmup, and verification are deliberately excluded
from the timed region.

## Repository map

```text
lexer.*                 Tokenization and lexical diagnostics
parser.*                Recursive-descent expression parser
ast.*                   Move-only AST nodes and visitor interface
ast_printer.*           Human-readable structural printer
shape_inference.*       Shape analysis and annotations
constant_folding.*      Bottom-up scalar folding
kernel_fusion.*         Element-wise fusion-region discovery
ir_generator.*          Scalar and tensor LLVM lowering
jit_engine.*            ORC LLJIT ownership and symbol lookup
compiler_driver.*       End-to-end pipeline and benchmark logic
graphjit_main.cpp       graphjit command-line entry point
tests/                  Dependency-free phase and integration tests
```

The phase-specific demo executables remain available as focused examples:

```text
graphjit_lexer_demo       graphjit_shape_demo
graphjit_ast_demo         graphjit_opt_demo
graphjit_parser_demo      graphjit_fusion_demo
graphjit_ir_demo          graphjit_jit_demo
```

## Current scope and limitations

GraphJIT is a working compiler prototype, not yet a general tensor framework.
Its boundaries are explicit:

- The CLI assigns every variable the same rank-one shape `[N]`. The rank-2
  matrix multiplication path is demonstrated and tested through the analysis
  and backend APIs, but the CLI does not yet accept shape declarations.
- Tensor LLVM lowering currently supports element-wise addition and rank-2
  matrix multiplication. Tensor subtraction, multiplication, division, and
  scalar/tensor mixed lowering remain future work.
- Kernel fusion currently records maximal fusion regions in the AST. Dedicated
  single-loop lowering for every fused operator is not implemented yet.
- Function declaration nodes exist, but the current parser and driver compile
  one anonymous expression at a time.
- Shapes are static, buffers are contiguous row-major `double` arrays, and the
  caller owns input/output storage.
- Integer tokens are recognized by the lexer but are intentionally outside the
  current float-expression AST grammar.

These constraints keep the implementation small enough to study while leaving
clear extension points rather than pretending the prototype is production
complete.

## Roadmap

- Add explicit tensor shapes and named inputs to the CLI
- Lower all fused element-wise operators into a single loop
- Add tensor broadcasting beyond scalar-or-identical shapes
- Introduce function parsing and multiple compiled entry points
- Add LLVM optimization pipelines and vectorization reporting
- Add runtime-sized tensors and a managed allocation strategy
- Add continuous integration across macOS and Linux
- Package a reproducible container and release artifacts

## A short reviewer walkthrough

If you have only a few minutes, this sequence shows the whole project:

```bash
# Correctness across all compiler phases
ctest --test-dir build --output-on-failure

# Optimized syntax, generated control flow, and native tensor execution
./build/graphjit --emit-ast --emit-llvm --run "A + B"

# Scalar constant folding and execution
./build/graphjit --emit-llvm --run "2.0 + 3.0 * 4.0"

# Valid and invalid matrix-shape analysis
./build/graphjit_shape_demo

# Native-versus-JIT performance with full output verification
./build/graphjit --benchmark
```

For implementation details, good starting points are
[`ast.hpp`](ast.hpp), [`shape_inference.cpp`](shape_inference.cpp),
[`ir_generator.cpp`](ir_generator.cpp), and [`jit_engine.cpp`](jit_engine.cpp).

---

GraphJIT was built as a systems-engineering exercise in compiler construction:
make ownership visible, make invalid programs fail clearly, verify generated IR,
and measure native execution without compromising correctness.
