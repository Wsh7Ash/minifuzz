# Architecture

## The target: SVCF

A binary config format chosen so that a single mutated byte can reach a
memory-safety bug:

```text
offset  size  field
0       4     magic "SVCF"
4       1     version (must be 1)
5       2     entry count, little endian (capped at 1024)
repeated per entry:
        1     key length
        N     key bytes            -> copied into a 32-byte arena
        1     flags
        2     value length (LE)    -> copied into a 64-byte arena
        M     value bytes
```

### The two planted bugs

```cpp
// BUG(off-by-one): should be ``available > capacity``. Allowing
// capacity + 1 writes exactly one byte into the redzone.
if (available > capacity + 1) { ... }
```

`capacity` is 32 for keys and 64 for values, so a 33-byte key or a 65-byte
value writes one byte past the buffer.

### Detection: redzones, not aborts

```text
arena layout:  [ 0 .. capacity-1 | 0xA5 x8 ]
                          usable     redzone
```

Each copy allocates `capacity + 8` bytes pre-filled with `0xA5`, writes the
field, then scans the redzone. A changed byte is a crash report:

```text
bug:   svcf-value-off-by-one
offset:64                       <- first byte past the usable buffer
detail:WRITE of 65 byte(s) into a 64-byte buffer; redzone byte 0 overwritten
```

Why not `abort()`? Because a fuzzer that kills itself cannot continue, report
statistics or minimise. So `run_target` returns *"no redzone violation"* —
and crucially, **a malformed input is a rejection, not a crash**. Conflating
the two (which an earlier draft did) makes the minimiser shrink inputs to
garbage and makes the exit code meaningless.

## The search loop

```text
seeds ──┐
        ├─► pick a corpus entry ─► mutate (1-8 ops) ─► run_target
        │                                             │
        │                              crash? ── yes ──┤
        │                                   │          │ bug already known?
        │                                   no         │   yes → count, continue
        │                                   │          │   no  → minimise → save
        └───────────────────────────────────┘          │          │
                                                       └──────────┘
```

Crashing inputs are **never** added to the corpus. They would quickly
dominate the population (every descendant also crashes) and hide every other
bug behind repeats of the first — observed directly: a draft that did this
reported 148 481 crashes of a single bug and never found the second. With the
policy fixed, the same budget finds both bugs in 6.3 s.

## Determinism

- `Rng` is xorshift64\* with an explicit seed; `below()` uses rejection
  sampling instead of a library distribution, so MSVC, libstdc++ and libc++
  produce identical sequences.
- Same `--seed`, same corpus, same code → same search, same crash, same
  minimised bytes, on any platform. CTest relies on this.

## Mutations (1–8 per input)

| # | operator | why it matters here |
|---|---|---|
| 0 | single bit flip | `0x40 → 0x41` is exactly the 64→65 length change |
| 1 | random byte set | brute-forces a specific value |
| 2 | arithmetic ±16 on a byte | walks length fields by small steps |
| 3 | insert 1–8 random bytes | grows a field past its bound |
| 4 | delete 1–16 bytes | truncates, exercises the length guards |
| 5 | duplicate a slice | repeats structure |
| 6 | swap two halves | reorders fields |
| 7 | overwrite with a dictionary token (`SVCF`, `key`, ...) | preserves the magic/header |
| 8 | byte invert | cheap diversity |

`max_input` (default 4096) caps growth on every path.

## Minimisation

Classic delta debugging over chunk sizes `n/2, n/4, ... 1`: try dropping each
chunk; keep the drop if the input still crashes *the same bug*. The result is
written to disk, and its FNV-1a 64-bit hash becomes part of the filename so
two different inputs for one bug do not overwrite each other.

## Threat model

| Threat | Handling |
|---|---|
| Fuzzing something you do not own | the only target is the bundled parser; there is no target-selection flag |
| Crash files leaking out | inputs are parser bytes, never credentials; they are small and reviewed |
| Unbounded memory/disk | `max_input` cap, corpus capped at 512 entries, one file per distinct bug |
| Nondeterministic CI failures | fixed seed + own RNG; CTest asserts the same run |
| The planted bugs being used on real data | the format has no production use; SECURITY.md says so explicitly |

## Design decisions

- **Report, don't die.** Redzone checks return a `Crash` struct so the loop,
  statistics and minimiser keep working — the trade-off is that detection is
  limited to instrumented buffers (documented in README limitations).
- **Rejection ≠ crash.** Enforced in `run_target` and asserted in tests.
- **One file per distinct bug** by default (`stop on first` unless
  `--keep-going`), so a run has a crisp answer.
