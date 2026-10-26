# Usage

## Build

```shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest -C Debug --output-on-failure   # add -C Release if built Release
```

With AddressSanitizer (optional, GCC/Clang/MSVC):

```shell
cmake -S . -B build-asan -DMINIFUZZ_ASAN=ON
cmake --build build-asan -j
```

## Prepare a corpus

```shell
minifuzz --init-corpus corpus
```

```text
wrote 4 seed input(s) to corpus
```

The seeds are not arbitrary: two of them sit *exactly* at a buffer limit with
one spare byte of headroom, which is what makes the bugs reachable in a
handful of mutations. See `docs/ARCHITECTURE.md`.

## Fuzz

```shell
minifuzz --corpus corpus --seed 1 --iterations 1000000
```

```text
loaded 4 seed input(s)
seed=1 iterations=1000000 max_input=4096 target=svcf-parser
executed 2594 input(s) in 0.0279s (93038/s), crashes=1 unique_bugs=1
saved crashes/svcf-key-off-by-one-84ee0387fe3b2931.bin
  svcf-key-off-by-one: WRITE of 33 byte(s) into a 32-byte buffer; redzone byte 0 overwritten
  replay: minifuzz --replay crashes/svcf-key-off-by-one-84ee0387fe3b2931.bin
```

Without `--corpus` the four built-in seeds are used.

## Flags

| Flag | Default | Effect |
|---|---|---|
| `--corpus PATH` | built-in seeds | seed file or directory (repeatable) |
| `--seed N` | `0x5EED5EED5EED5EED` | RNG seed; identical seeds reproduce the run |
| `--iterations N` | 100000 | inputs to execute |
| `--crashes-dir DIR` | `./crashes` | where crash inputs are written |
| `--max-input N` | 4096 | cap on generated input size |
| `--keep-going` | off | do not stop at the first distinct bug |
| `--replay FILE` | – | run one input; no fuzzing |
| `--init-corpus DIR` | – | write built-in seeds into DIR and exit |
| `--version`, `-h` | – | version / help |

Exit codes: `0` no crash, `1` crash found or reproduced, `2` usage error.

## Replay a crash

```shell
minifuzz --replay crashes/svcf-value-off-by-one-ee06c1e8b7930916.bin
```

```text
CRASH svcf-value-off-by-one at redzone offset 64
  WRITE of 65 byte(s) into a 64-byte buffer; redzone byte 0 overwritten
  input: crashes/svcf-value-off-by-one-ee06c1e8b7930916.bin (79 bytes)
```

`--replay` exits `1` when the input crashes and `0` when it does not, so it
doubles as a regression check: keep the file, replay it in CI forever.

## Finding every bug, not just the first

```shell
minifuzz --seed 7 --iterations 500000 --keep-going --crashes-dir crashes-more
```

```text
executed 500000 input(s) in 6.34s (78807/s), crashes=114 unique_bugs=2
saved crashes-more/svcf-key-off-by-one-d74b3ac29139efc3.bin
saved crashes-more/svcf-value-off-by-one-ee06c1e8b7930916.bin
```

## Using it in CI

```yaml
- name: Fuzz the bundled parser
  run: |
    cmake -S . -B build && cmake --build build -j
    ctest --test-dir build --output-on-failure     # unit + find + replay gates
    ./build/minifuzz --seed 20261026 --iterations 200000 --crashes-dir /tmp/crashes
```

`e2e_finds_crash` is declared `WILL_FAIL` because exit code 1 *is* success
there; `e2e_replay_reproduces` then proves every saved input reproduces.

## Troubleshooting

- **"no crash found" (exit 0)** — the seeds may be too far from the bug. Keep
  seeds structurally valid and one byte below any bound you care about.
- **"no usable seed inputs" (exit 2)** — the `--corpus` path is empty or the
  files are empty.
- **Different results on another machine** — check that `--seed`, corpus and
  iterations match; the RNG itself is platform independent.
- **Slower than expected** — minimisation runs once per *new* bug; a run that
  keeps reporting one bug is not minimising again.
