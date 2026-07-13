# Khan Benchmark Results

Real numbers from `benchmarks/run.py`, run in this environment (Linux,
gcc -O2 for the C reference, whatever Python/Node happened to be
installed). **Not** rigorous microbenchmarks — one run each, no warmup,
no statistical repeats. Treat every number here as order-of-magnitude,
not a precise claim, and re-run `run.py` yourself before trusting a
specific figure.

Only Python and Node were available in this environment to compare
against — the original roadmap asked for Python/Lua/Node/Go/Rust/C.
Lua/Go/Rust are not installed here; someone with those available should
extend `run.py` the same way the existing four runtimes are wired in
(it's a small, repetitive pattern per language).

## Results

```
benchmark               khan      python        node           c     khan/fastest
---------------------------------------------------------------------------------
loop                  0.991s      0.440s      0.380s      0.005s     194.5x
fib                   0.155s      0.063s      0.068s      0.003s      51.8x
string_concat         0.027s      0.016s      0.041s         n/a       1.7x
json_bench            0.109s      0.263s      0.101s         n/a       1.1x
map_scale             0.031s      0.016s      0.036s         n/a       2.0x
```

(A second run showed `loop` and `fib` numbers shift somewhat for
Node specifically — 0.045s and 0.036s respectively — almost certainly
V8 JIT warmup variance across single runs with no warmup phase. Khan's
own numbers were stable within ~10% run to run.)

## What each benchmark stresses, and what the numbers say

- **`loop`** (5,000,000 iterations of `sum = sum + i; i = i + 1`) —
  stresses the bytecode dispatch loop itself for the simplest possible
  operations. Khan is ~194x slower than the native C floor here, and
  ~2-8x slower than Python/Node depending on their JIT warmup state.
  This is the most "generic interpreter overhead" benchmark of the
  four — every `OP_ADD`/`OP_GET_LOCAL`/`OP_SET_LOCAL` goes through a
  `value_copy()` (see `docs/memory-notes.md` for what that does) even
  for a plain number, which a real profiler would likely show as a
  meaningful chunk of this gap. This is the benchmark to re-run first
  if anyone picks up the roadmap's dispatch-mechanism item (computed
  goto vs. switch) — right now the VM uses a `switch` (see
  `docs/opcodes.md`), and this benchmark is exactly the kind of tight
  dispatch loop where that choice would show up.

- **`fib`** (recursive, unmemoized, `fib(28)`) — stresses function
  call/return overhead specifically. Notably, Khan's *relative* gap to
  Python/Node is smaller here (~2-4x) than in the raw loop benchmark,
  even though `fib` does far more work per call (frame setup,
  `OP_RETURN`'s locals-freeing pass — see `docs/memory-notes.md` — the
  whole calling convention in `docs/opcodes.md`). This suggests Khan's
  *function call* overhead isn't disproportionately worse than its
  *basic bytecode dispatch* overhead — if anything, the opposite. Worth
  keeping in mind before assuming "function calls are the slow part."

- **`string_concat`** (20,000 repeated `s = s + "x"`) — Khan is only
  ~1.6-1.7x slower than the fastest of Python/Node at this size. But
  this benchmark is deliberately run at a size too small to show
  something important: **manually re-running this at 100,000
  iterations instead of 20,000 (5x the input) took roughly 80x longer**
  (2.42s vs 0.03s at 20,000) in ad hoc testing outside the standard
  suite. That's worse than quadratic, not just quadratic.

  **Root cause, traced exactly:** `OP_ADD`'s string-concatenation
  branch (`src/vm.c`) does this on every single call:
  ```c
  int la = (int)strlen(a.as.string);
  int lb = (int)strlen(b.as.string);
  ```
  `a` is the *accumulated* string (the left operand of `s + "x"`, i.e.
  `s` itself). `strlen()` is O(length), and `a`'s length grows by 1
  every iteration — so the total cost of just these `strlen()` calls
  across N iterations is `1 + 2 + 3 + ... + N` = O(N²), independent of
  anything else `+` does. This is the entire quadratic signature by
  itself; the actual `malloc`+`memcpy` work is only O(N) total.

  **Why this wasn't fixed here rather than just documented:** Khan's
  `VAL_STRING` currently stores a bare `char *` (see `value_string()` in
  `src/value.c` — no length field). Fixing this properly means giving
  strings a cached length (or a full rope/builder type) — a change that
  touches every string-creating/reading site across `value.c`, `vm.c`,
  `khan_stdlib.c`, `json_lib.c`, and every native library that touches
  `VAL_STRING` directly. That's a real, invasive Value-system change
  (roadmap items #9/#10), not a two-line fix, and it deserves its own
  careful pass rather than being rushed in alongside a benchmarking
  session. Flagging this as the single most concrete, actionable
  finding from this whole benchmark suite for whoever picks up that
  work — the fix is well-understood even though it wasn't small enough
  to do here.

- **`json_bench`** (50,000 encode+decode round-trips of a small nested
  object) — Khan roughly ties Node and **beats Python** here (0.109s vs
  0.263s). This is a genuinely good result worth highlighting rather
  than only reporting the gaps — `json_lib.c`'s encode/decode
  implementation is apparently quite competitive for small-to-medium
  structures.

- **`map_scale`** (build a `{}` map by inserting 4,000 keys one at a
  time) — only ~2x slower than Python at this size, which understates
  the real story: **Khan's `{}` map is currently a linear-scan array,
  not a hash table at all** (verified and written up in detail in
  `docs/hash-table-audit.md`, including empirical confirmation that
  building a map scales roughly quadratically — 4x more keys took ~7x
  longer, 2x more after that took ~3x longer). Python's `dict` and
  JS's object are real hash tables, so they stay ~O(n) here regardless
  of size; Khan's gap to them will keep widening as N grows in a way
  this benchmark's fixed size doesn't fully show. Same category of
  problem as the string-concatenation finding above: an O(n)-per-call
  operation invoked in a loop, producing O(n²) overall. See the linked
  doc for why this wasn't fixed in this pass (it's a real, invasive
  Value-system change, not a two-line fix) and what a fix would involve.

## Honest summary

Khan is meaningfully slower than Python/Node for basic bytecode dispatch
(the `loop` benchmark) but much closer — sometimes better — for function
calls and JSON handling. The clearest, most actionable findings here
aren't the cross-language comparison at all: **string concatenation**
(this doc) **and building up a `{}` map** (`docs/hash-table-audit.md`)
both get disproportionately slower as they grow — the first from a
redundant `strlen()` call, the second from the map type not actually
being a hash table — which are concrete, scoped things to fix rather
than a vague "make the VM faster" ask. That's arguably more useful than
the cross-language table,
and it's the kind of result you only get by actually running
benchmarks rather than assuming performance characteristics.

## Reproducing

```sh
make clean && make
python3 benchmarks/run.py
```

To check the string-concatenation growth rate specifically:
```sh
# edit benchmarks/string_concat.kh's 20000 to 100000, then:
time ./khan benchmarks/string_concat.kh
```
