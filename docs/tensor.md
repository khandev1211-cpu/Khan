# Tensor + autograd (v1)

The first slice of Khan's AI-native-core direction (see `docs/future
plans/phase2-ai-foundation-plan.md`): a real native tensor type with
automatic differentiation, not a library bolted onto a general-purpose
language.

```
import "tensor"

let x = Tensor([[1, 2], [3, 4]], true)    # requires_grad = true
let y = Tensor([[5, 6], [7, 8]], true)

let loss = x.add(y).mul(x).sum()
loss.backward(nil)

print loss.data()       # 100
print x.grad.data()     # [[7, 10], [13, 16]]
print y.grad.data()     # [[1, 2], [3, 4]]
```

## Why this, and why now

`phase2-ai-foundation-plan.md` names the Tensor Engine and Automatic
Differentiation as the two 5/5-starred, foundational pieces of the whole
AI-native direction — "without Tensor, no AI." Its own Part 2
(reality-check) lists what was missing to build it properly: `x.grad`,
`y.backward()`, and `tensor.to(device)` all need method-call syntax on an
object, and Khan had no classes or method calls at the time that doc was
written. Both now exist (see `docs/classes.md`) — classes were built in
the session immediately before this one — so this is the first thing
built once that blocker was actually clear, not a restart.

## What works

| Feature | Notes |
|---|---|
| `VAL_TENSOR` | A real value type: one contiguous `double*` buffer + shape, not nested Khan arrays. See `src/tensor_lib.c`/`.h`. |
| `Tensor(data, requires_grad)` | `data` is a number (0-D) or a rectangular nested array (1-D, 2-D, or deeper for construction/elementwise use). |
| `.add()` / `.sub()` / `.mul()` | Elementwise. Either side can be a plain number or a single-element tensor, broadcast against the other — see "Broadcasting" below for the exact rule. |
| `.matmul()` | 2-D only (see "What does not"). |
| `.transpose()` | 2-D only. |
| `.reshape(shape)` | Any shape with the same total element count. |
| `.sum()` | Reduces to a 0-D (scalar) tensor. |
| `.data()` / `.shape()` / `.ndim()` / `.numel()` | Inspection; `.data()` converts back to a plain Khan number/array. |
| `.backward(grad)` | `nil` seeds with ones (the usual `loss.backward(nil)` entry point for a scalar loss); an explicit `Tensor` reseeds a mid-graph node. Accumulates into `.grad`, so a tensor used more than once in a graph correctly sums the contributions from every use (`y = x.add(x)` gives `dy/dx = 2`, not `1`). |
| `==` on two tensors | Real value equality (same shape and contents), unlike every other container type in Khan (arrays/maps/classes/instances compare by identity) — see `values_equal`'s `VAL_TENSOR` case in `value.c`. |
| `type(x)` | `"tensor"`. |
| Native error handling | Every shape/argument error prints a message and returns `nil` rather than crashing — the same convention `vision_lib.c`/`sqlite_lib.c` already use for their natives, which is why these aren't yet catchable with `try`/`catch` (see "What does not"). |

### Broadcasting

Exactly one rule, no more: if both operands are tensors of the same
shape, it's ordinary elementwise. Otherwise, if exactly one side is "a
scalar" — a plain number, or any tensor holding exactly one element,
regardless of its own shape or ndim — that value is broadcast against
every element of the other side. **There is no row/column broadcasting**
(a shape-`(2,)` bias against a shape-`(1,2)` matmul output is a shape
mismatch, not a broadcast) — a real bias vector currently has to be
given in the exact output shape. `tests/suites/tensor.kh`'s `Linear`
example hits this directly and documents it inline.

## What does not (yet)

- **No numpy-style broadcasting.** Only the single-scalar rule above.
  Row-vector-across-matrix, column-vector-across-matrix, and any
  mismatched-but-compatible shape are all rejected.
- **`matmul`/`transpose` are 2-D only.** No batched matmul, no N-D
  transpose with an axis permutation.
- **No GPU, no dtype system.** Everything is `double`.
- **Tensor shape/argument errors aren't catchable.** They print to
  stderr and the native function returns `nil`; nothing throws, so
  `try`/`catch` can't intercept them the way it can a Khan-level
  `throw`. Silent `nil` propagation is possible if a caller doesn't
  check for it — same tradeoff the project already accepted for
  `vision_lib.c`/`sqlite_lib.c`.
- **No gradient reduction for a broadcast operand that itself
  `requires_grad`.** If a non-scalar tensor's gradient is accumulated
  against a differently-shaped `.grad`, the accumulating `_tensor_add`
  call will raise a shape-mismatch error. In practice this only bites
  when a `requires_grad=true` *scalar* tensor (not a plain number — those
  are wrapped with `requires_grad=false` and skip backward entirely) is
  broadcast against a bigger tensor; ordinary use (constants, matching
  shapes) is unaffected.
- **No optimizer, no `nn`-style layers beyond what you build yourself**
  (the `Linear` class in `tests/suites/tensor.kh` is a test fixture, not
  a shipped API).

## How it works

**Value.** `VAL_TENSOR`'s `Obj` carries `data` (owned `double*`), `shape`
(owned `int*`, length `ndim`), `ndim`, and `size` (element count; a 0-D
tensor has `ndim == 0`, `shape == NULL`, `size == 1`). Reference-counted
like arrays/maps — `value_copy` bumps the refcount, `value_free` frees
both buffers at zero. `print`/`str()` render it as nested brackets
matching its shape; `values_equal` is real value equality (the one
exception among Khan's container types, documented above and in
`value.c`), since a tensor is conceptually a number, not a shared,
mutable container.

**Native engine** (`src/tensor_lib.c`, registered via
`tensor_register_all_vm`, called from `main.c` alongside the other
`*_register_all_vm` libraries). Functions are named `_tensor_*` — a
leading underscore, the same convention `webi_lib.c` uses for internals
the Khan-level API wraps — because user code is meant to go through the
`Tensor` class, not call these directly. `_tensor_from_nested`/
`_tensor_to_nested` convert between a plain/nested Khan array and the
flat buffer via a generic recursive depth-detector
(`nested_depth`/`nested_fill_shape`/`nested_validate_shape`), not a
hardcoded 0/1/2-D special case, so `reshape` can produce (and
`to_nested` convert back) tensors of any dimensionality even though
*construction* from a literal is most commonly 0/1/2-D in practice.
Every function follows the established error convention (see the table
above) rather than inventing a new one.

**Khan-level `Tensor` class** (`packages/tensor/tensor.kh`, replacing the
old pure-Khan nested-array version entirely — nothing else in the repo
imported it, confirmed with a repo-wide search before replacing it).
Each op (`add`/`sub`/`mul`/`matmul`/`transpose`/`reshape`/`sum`) builds
the result tensor, then records `.op` (a string tag) and `.a`/`.b` (the
input `Tensor`s) on it — a tiny computation graph, built from ordinary
instance fields rather than closures. `backward(grad)` dispatches on
`.op` with one `if`/`elif` per operation, computing and recursing into
each input's gradient with the standard reverse-mode rule for that op
(e.g. for `z = a @ b`: `dL/da = g @ bᵀ`, `dL/db = aᵀ @ g`). Every
gradient in `tests/suites/tensor.kh` is checked against a hand-worked
value, not just "runs without crashing."

**Backward-compatible free functions.** `matmul`/`dot`/`add`/`scale`
(nested-array in, nested-array out) are kept at the bottom of
`tensor.kh` for anything written against the old package's API — they
now call through the native engine instead of the old package's pure-Khan
`O(n^3)` loops, so they're also just faster.

A real bug, found by valgrind while writing the stress test for this: the
first version of the training-loop stress test called `loss.data()[0][0]`
on a **0-D** tensor's `.data()`, which returns a plain number, not a
nested array — indexing it threw an uncaught fatal runtime error on the
very first iteration, which looked like a 20+-record valgrind leak
because the VM's fatal-exit path doesn't unwind the stack. That leak
report was a symptom of the test's bug, not the tensor engine's; fixing
the test (summing `.data()` directly) and rerunning showed 0 leaks across
2000 real forward/backward iterations. Worth remembering: an unexplained
cluster of valgrind leaks right after a "Runtime error" line almost
always means a fatal exit skipped cleanup, not a real per-iteration leak
— check for the error first.

A second real bug, this one in the test *suite*, not a script: naming a
test helper `add` inside `tests/suites/stdlib_collections.kh` silently
overwrote the global `add` that `packages/tensor/tensor.kh` defines,
because nested `fn` declarations in Khan still define a *global* (see
`docs/classes.md`), and every suite file gets fully imported before any
suite function is *called* — so the last-called suite that happens to
redefine a popular name like `add` wins, however unrelated its own file
is. Fixed by renaming the test's internal helper to `_reduce_add_cb`;
worth remembering for any future suite that picks a common name for a
local helper.

## Tests

`tests/suites/tensor.kh`, registered in `tests/run_all.kh`:

- **`suite_tensor_native`** (26 checks) — construction from nested
  arrays/scalars, shape/ndim/numel, every elementwise op and scalar
  broadcast, matmul, transpose, sum, reshape (both directions), value
  equality, ones/zeros-like, and every native error path.
- **`suite_tensor_autograd`** (21 checks) — forward and backward for
  add/mul (including the reused-tensor double-accumulation regression),
  matmul backward, transpose/reshape backward, mixing a `Tensor` with a
  plain number, a small `Linear`-style layer, a 2,000-iteration
  forward/backward stress loop checked for exact numeric correctness
  (and separately confirmed leak-free under valgrind), and the
  backward-compatible free functions.

Every expected gradient in the suite was verified by hand first (shown
in the commit/session notes), not just asserted to match whatever the
code happened to produce.
