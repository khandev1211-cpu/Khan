# Classes (v1)

Khan has Python-style classes: a class is a set of methods, calling the
class builds an instance, and `self` is an ordinary explicit first
parameter.

```
class Dog:
    fn __init__(self, name, age):
        self.name = name
        self.age = age

    fn birthday(self):
        self.age = self.age + 1
        return self          # returning self allows chaining

let d = Dog("Rex", 3)
d.birthday().birthday()
print d.age                  # 5
```

## What works

| Feature | Notes |
|---|---|
| `class Name:` with `fn` methods | Only `fn` declarations are allowed directly inside a class body (parse error otherwise). |
| `Name(args)` | Builds an instance and runs `__init__` if defined. With no `__init__`, the class takes zero arguments. |
| `__init__` return value | Discarded. The call expression always evaluates to the instance. |
| `obj.field` / `obj.field = v` | Fields are dynamic: assigning creates them. Reading a missing field gives `nil` (same as a missing map key). |
| `obj.method(args)` | The receiver is passed as `self`. Argument count includes `self` when checked, so error messages say "methods take 'self' first". |
| Chaining | `a.inc().inc()` works; so do method calls in the middle of expressions. |
| `type(x)` | `"class"` for a class, `"instance"` for an instance. |
| `print` / `str()` | `<class Dog>` and `<Dog instance>`. |
| `==` | Identity. Two separately built instances are never equal. |
| Reference semantics | `let b = a` aliases the same instance, like arrays and maps. |
| `try`/`catch` | A `throw` or runtime error inside a method or `__init__` unwinds correctly. |

## What does not (yet)

- **No inheritance.** There is no `class B(A)` syntax.
- **No bound-method values.** `let m = obj.method` is not supported;
  methods are only reachable through call syntax `obj.method(...)`.
  This is why `obj.method(args)` is its own AST node
  (`AST_METHOD_CALL`) rather than `AST_GET_ATTR` + a call.
- **No class-level fields or static methods.** A class holds methods only.
- **Cycles through instance fields leak.** Instances are reference
  counted but not registered with the cycle collector (same documented
  gap as closures). `a.friend = b; b.friend = a` leaks both; acyclic
  structures are freed normally.
- **Function registry limit.** Every method is a registered function, and
  the registry holds 1024 (`KHANFN_REGISTRY_MAX` in `src/vm.c`), shared
  with ordinary functions.
- **Classes declared inside a function** work (the class is rebuilt and
  rebound as a global each time the function runs, like a nested `fn`),
  but each call creates a new, distinct class value.

## How it works

**Values.** `VAL_CLASS` and `VAL_INSTANCE` are new value types. Both are
heap `Obj`s whose `as.map` storage is laid out exactly like a `VAL_MAP`
(a class's map is `method name -> function`, an instance's is
`field name -> value`), so `map_get`/`map_set` and the hash index are
reused as-is. An instance also holds a retained `class_ref` pointer to
its class, which is how a method call finds its method. Instances live
outside the cycle collector; see above.

**Compiling a class.** `AST_CLASS_STMT` compiles each method like a
top-level `fn` (`compile_class_method`, no global definition at the end),
pushes `[class name][method name][fn index]...`, then emits
`OP_MAKE_CLASS` (or `_WIDE` for more than 255 methods) and binds the
result to a global named after the class. Because of that, `Dog(...)` is
compiled as an ordinary call; nothing special happens at the call site.

**Opcodes.**

| Opcode | Stack effect |
|---|---|
| `OP_MAKE_CLASS[_WIDE]` | `[name][mname][fn_idx]... -> [class]` |
| `OP_CALL_METHOD[_WIDE]` | `[receiver][args...] ->` runs the method inline; receiver is `self` (slot 0) |
| `OP_GET_ATTR[_WIDE]` | `[obj] -> [value]` |
| `OP_SET_ATTR[_WIDE]` | `[obj][value] -> [value]` |
| `OP_CALL` on a `VAL_CLASS` | overwrites the class's stack slot with the new instance, then runs `__init__` inline (or just leaves the instance) |

**Two call-frame flags** (in `CallFrame`) exist because these calls have a
different stack layout from a plain function call, where a hidden callee
slot sits just below the arguments:

- `is_method_call`: there is no callee slot below `self`, so `OP_RETURN`
  truncates the stack to `slots`, not `slots - 1`. Getting this wrong
  corrupts the caller's expression temporaries.
- `is_constructor`: on `OP_RETURN`, yield `self` (`slots[0]`) instead of
  `__init__`'s own return value.

**Why `__init__` runs inline.** The first implementation ran `__init__`
through `vm_call_value`, which starts a nested `run_loop`. A `throw` inside
`__init__` unwound across that boundary and left the stack unbalanced
("stack underflow" on the next statement). Running it as a normal frame in
the caller's dispatch loop fixes that class of bug, and
`tests/suites/classes.kh` has a regression test for it.

## Tests

`tests/suites/classes.kh` (25 checks), registered in `tests/run_all.kh`.
It covers construction, fields, chaining, temporaries around method calls,
a 5,000-iteration stack-balance loop, reference semantics, instances in
arrays and nested instances, `type()`, every error path through
`try`/`catch`, and the constructor-throw regression.
