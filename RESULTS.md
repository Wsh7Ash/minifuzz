# Results

## Environment

- OS: Windows 11 (MSYS2/UCRT64 shell for commands, Windows CMake 4.4.2)
- Compiler: MSVC 19 (Visual Studio 18.9 generator), `/W4 /permissive-`, C++20
- Build system: CMake 4.4.2
- Git: 2.56.0

## Validation performed

| Gate | Result |
|---|---|
| configure (`cmake -S . -B build`) | PASS |
| build (`cmake --build build -j`) | PASS — no warnings emitted |
| unit tests (53 checks) | PASS |
| e2e: crash directory cleaned before the search | PASS |
| e2e: fixed seed finds a crash (declared `WILL_FAIL`) | PASS |
| e2e: every saved crash replays with exit 1 | PASS |
| `--init-corpus` | PASS — 4 seed inputs written |
| fuzz run, stop at first bug | PASS — exit 1, bug found |
| fuzz run `--keep-going` | PASS — both planted bugs found |
| `--replay` | PASS — exit 1, crash reproduced |
| lint / type check | N/A — C++ project, no separate linter configured |
| secret scan | PASS — crash inputs are parser bytes only |

`ctest -C Debug --output-on-failure` → **100% tests passed (4/4)**.

## Commands executed

```shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest -C Debug --output-on-failure

build/Debug/minifuzz.exe --version
build/Debug/minifuzz.exe --init-corpus corpus
build/Debug/minifuzz.exe --seed 20261026 --iterations 200000 \
    --crashes-dir results/crashes > results/fuzz_run.txt
build/Debug/minifuzz.exe --seed 7 --iterations 500000 --keep-going \
    --crashes-dir results/crashes_more > results/fuzz_run_keepgoing.txt
build/Debug/minifuzz.exe --replay results/crashes/svcf-key-off-by-one-84ee0387fe3b2931.bin \
    > results/replay.txt
```

## Observed result

Unit suite: `53 check(s), 0 failure(s)`. CTest: `100% tests passed out of 4`.

First-bug run (deterministic seed `20261026`):

```text
executed 2594 input(s) in 0.0279s (93038/s), crashes=1 unique_bugs=1
saved results/crashes/svcf-key-off-by-one-84ee0387fe3b2931.bin
  svcf-key-off-by-one: WRITE of 33 byte(s) into a 32-byte buffer; redzone byte 0 overwritten
exit code 1
```

Keep-going run (seed `7`, 500 000 inputs):

```text
executed 500000 input(s) in 6.34461s (78807/s), crashes=114 unique_bugs=2
saved results/crashes_more/svcf-key-off-by-one-d74b3ac29139efc3.bin
saved results/crashes_more/svcf-value-off-by-one-ee06c1e8b7930916.bin
exit code 1
```

Replay of a saved input:

```text
CRASH svcf-key-off-by-one at redzone offset 32
  WRITE of 33 byte(s) into a 32-byte buffer; redzone byte 0 overwritten
  input: .../svcf-key-off-by-one-84ee0387fe3b2931.bin (41 bytes)
exit code 1
```

Both planted bugs in the bundled parser were found, minimised, saved and
reproduced: `svcf-key-off-by-one` (33 bytes into a 32-byte buffer) and
`svcf-value-off-by-one` (65 bytes into a 64-byte buffer).

## Generated artifacts

- `results/fuzz_run.txt` — stop-at-first run transcript
- `results/fuzz_run_keepgoing.txt` — both-bugs run transcript
- `results/replay.txt` — reproduction transcript
- `results/crashes/*.bin`, `results/crashes_more/*.bin` — real minimised
  crash inputs (41 and 79 bytes)
- `corpus/seed_1..4.svcf` — the seed inputs written by `--init-corpus`

## Known limitations

- Detection relies on the parser's own redzone checks; real ASan coverage
  needs `-DMINIFUZZ_ASAN=ON`, which was not exercised in this run.
- No coverage guidance: the search is blind mutation and depends on seeds
  sitting near a bound (documented in docs/ARCHITECTURE.md).
- Built with MSVC here; the GCC/Clang warning flags are configured in
  `CMakeLists.txt` but were not used by this run's build.
