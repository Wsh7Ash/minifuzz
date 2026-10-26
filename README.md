# minifuzz

A small mutation fuzzer for a bundled, deliberately buggy C++ parser — it finds a real out-of-bounds write, minimises the input that triggers it, saves the reproducer and replays it on demand.

## Problem

Fuzzing is the most effective way to find memory-safety bugs in a parser, but
the tooling is usually heavy: coverage instrumentation, compiler runtimes,
corpus management daemons. When you want to *demonstrate* the loop — generate,
run, detect, minimise, reproduce — on a target you own, you need something
that runs anywhere in under a second and shows every step.

`minifuzz` is that loop, complete: deterministic RNG, eight mutation
operators, a dictionary, delta-debugging minimisation, stable crash filenames,
`--replay` and exit codes CI can use.

## Key features

- **Bundled target: the SVCF parser** — a documented binary config format
  with two planted off-by-one bugs (32-byte key buffer, 64-byte value
  buffer), each guarded by an 8-byte redzone the parser checks after every
  copy.
- **Redzone detection instead of a sanitizer** — portable, no `-fsanitize`
  required (though `-DMINIFUZZ_ASAN=ON` is there for real ASan runs). Ordinary
  malformed input is a *rejection*, not a crash, so the exit code means
  something.
- **Deterministic search** — own xorshift64\* RNG and own rejection sampling,
  so `--seed 20261026` performs the same search on every platform.
- **Eight mutation operators** — bit flip, byte set, arithmetic, insert,
  delete, slice duplicate, slice swap, dictionary overwrite, plus a byte
  invert; 1–8 applied per input.
- **Minimisation** — delta-debugging chunk removal while the same bug still
  reproduces; the saved file is the smallest input found, not the raw one.
- **Stable artefacts** — crash files are named `<bug>-<fnv1a64>.bin`, so
  re-runs overwrite instead of piling up.
- **Zero dependencies** — C++20 standard library, CMake 3.16+.

## Example run

```text
$ minifuzz --seed 20261026 --iterations 200000 --crashes-dir results/crashes
using 4 built-in seed input(s)
seed=20261026 iterations=200000 max_input=4096 target=svcf-parser
executed 2594 input(s) in 0.0279s (93038/s), crashes=1 unique_bugs=1
saved results/crashes/svcf-key-off-by-one-84ee0387fe3b2931.bin
  svcf-key-off-by-one: WRITE of 33 byte(s) into a 32-byte buffer; redzone byte 0 overwritten
  replay: minifuzz --replay results/crashes/svcf-key-off-by-one-84ee0387fe3b2931.bin
$ echo $?
1
```

With `--keep-going` the same search finds **both** planted bugs:

```text
executed 500000 input(s) in 6.34s (78807/s), crashes=114 unique_bugs=2
saved results/crashes_more/svcf-key-off-by-one-d74b3ac29139efc3.bin
saved results/crashes_more/svcf-value-off-by-one-ee06c1e8b7930916.bin
```

Replay:

```text
$ minifuzz --replay results/crashes/svcf-key-off-by-one-84ee0387fe3b2931.bin
CRASH svcf-key-off-by-one at redzone offset 32
  WRITE of 33 byte(s) into a 32-byte buffer; redzone byte 0 overwritten
  input: ... (41 bytes)
$ echo $?
1
```

Committed evidence: [results/fuzz_run.txt](results/fuzz_run.txt),
[results/fuzz_run_keepgoing.txt](results/fuzz_run_keepgoing.txt),
[results/replay.txt](results/replay.txt), [results/crashes/](results/crashes/).

## Architecture

```text
main.cpp            CLI: corpus, seed, iterations, crashes dir, replay, init-corpus
fuzz.cpp            Rng, mutate(), fuzz() loop, minimize(), corpus I/O, hashing
target_svfc.cpp     SVCF parser + redzone arenas + the two planted bugs
tests/              53-check unit suite + 3 CTest gates (clean, find, replay)
```

The format, the bug and the detection model are described in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Requirements

- C++20 compiler (MSVC 19, GCC 13+, Clang 16+)
- CMake 3.16+

## Installation

```shell
git clone https://github.com/Wsh7Ash/minifuzz
cd minifuzz
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Quick start

```shell
# write the built-in seeds and fuzz with them
build/Debug/minifuzz --init-corpus corpus
build/Debug/minifuzz --corpus corpus --seed 1 --iterations 100000

# reproduce a saved crash
build/Debug/minifuzz --replay results/crashes/svcf-key-off-by-one-84ee0387fe3b2931.bin
```

> Single-config generators put the binary at `build/minifuzz`; Visual Studio
> puts it at `build/<Config>/minifuzz.exe`.

## Usage examples

```shell
minifuzz --seed 42 --iterations 1000000          # long search, stop at first bug
minifuzz --seed 7 --keep-going                   # enumerate every distinct bug
minifuzz --corpus corpus --corpus extra/         # seed from files/directories
minifuzz --max-input 256                         # keep generated inputs small
minifuzz --crashes-dir /tmp/crashes --keep-going
```

Exit codes: `0` no crash, `1` crash found or reproduced, `2` usage error.

## Testing

```shell
cmake -S . -B build && cmake --build build -j
cd build && ctest -C Debug --output-on-failure
```

Four tests: the unit suite (parser round-trips, redzone boundaries, RNG
determinism, mutation determinism and size caps, minimiser, corpus I/O, and a
short fuzz run that must find a bug), a fixture that cleans the crash
directory, the end-to-end search (`WILL_FAIL` — finding a crash is success),
and a replay check that every saved crash reproduces.

## Project structure

```text
minifuzz/
├─ src/               minifuzz.hpp, fuzz.cpp, target_svfc.cpp, main.cpp
├─ tests/             test_minifuzz.cpp, replay_check.cmake
├─ corpus/            4 committed seed inputs (from --init-corpus)
├─ docs/              ARCHITECTURE.md, USAGE.md, PROJECT_SPEC.md
├─ results/           run transcripts + real crash inputs
├─ CMakeLists.txt     library, CLI, unit + e2e tests, optional ASan
└─ SECURITY.md
```

## Limitations

- No coverage guidance: the search is blind mutation over a seed corpus, so it
  needs seeds that sit near the bug. That is exactly why the seeds carry
  length fields sitting one byte below the overflow.
- Detection is the parser's own redzone check, not a sanitizer: it catches
  writes past *these* buffers only. Build with `-DMINIFUZZ_ASAN=ON` for real
  ASan coverage of the whole process.
- One target (SVCF) is bundled. Adding another means implementing
  `run_target` for it; the loop is target-agnostic.
- The minimiser removes bytes only; it does not shrink by alphabet.

## Security notes

The bundled parser is intentionally vulnerable and is **never used for real
data**. See [SECURITY.md](SECURITY.md).

## License

MIT — see [LICENSE](LICENSE).
