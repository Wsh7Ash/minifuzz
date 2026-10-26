# Project spec — minifuzz

| Field | Value |
|---|---|
| Name | `minifuzz` |
| Category | `testing-qa` |
| Stack | C++20, CMake, no third-party dependencies |

## Target user

Developers learning or demonstrating fuzz testing, and QA/security engineers
who want a self-contained, reproducible fuzz loop to point at a parser they
own.

## Problem

Fuzzing tooling optimises for throughput on large codebases: coverage
instrumentation, compiler runtimes, corpus daemons. That is the wrong shape
for demonstrating the loop itself — generate, execute, detect, minimise,
reproduce — and for CI jobs that need a deterministic answer rather than a
nightly campaign.

## Solution

A complete fuzz loop around one bundled, intentionally vulnerable parser:
deterministic RNG, eight mutation operators, a dictionary, redzone-based crash
detection, delta-debugging minimisation, stable crash artefacts and `--replay`
for regression checks.

## Core workflow

1. `minifuzz --init-corpus corpus` writes the four seed inputs.
2. `minifuzz --corpus corpus --seed 1 --iterations 1000000` searches.
3. The first distinct bug stops the run (exit 1) with a minimised input on
   disk and a printed `--replay` command.
4. `--keep-going` enumerates every distinct bug instead.
5. `minifuzz --replay <file>` reproduces; exit 1 means "still crashes", which
   CI can assert forever after.

## Must-have features

- SVCF parser with two documented off-by-one bugs and redzone detection.
- Rejection (malformed input) strictly distinguished from crash.
- Deterministic RNG independent of the standard library's distributions.
- Mutation operators listed in docs/ARCHITECTURE.md with a size cap.
- Minimisation that preserves the same bug id.
- Crash files named `<bug>-<fnv1a64>.bin`; `--init-corpus`, `--replay`.
- Exit codes 0/1/2 as documented.

## Nice-to-have (out of scope for v0.1)

Coverage feedback, multiple bundled targets, corpus minimisation, alphabet
shrinking in the minimiser, crash deduplication across runs by stack trace.

## Test strategy

- Unit (53 checks): parse round-trips for all seeds, malformed-input
  rejections, exact-boundary clean run, 33/65-byte overflows with the right bug
  id and redzone offset, rejection ≠ crash, RNG reproducibility, mutation
  reproducibility and size cap, crash-name stability, minimiser shrink + same
  bug, corpus I/O round-trip, and a short fuzz run that must find a bug and
  reproduce it from disk.
- E2E (CTest): crash directory cleaned first, fixed-seed search must find a
  crash (`WILL_FAIL`), then every saved crash must replay with exit 1.

## Demo/result strategy

`results/fuzz_run.txt`, `results/fuzz_run_keepgoing.txt`,
`results/replay.txt` and the real crash inputs under `results/crashes*/` are
committed and regenerable with the commands in RESULTS.md.

## Safety considerations

- The only target is the bundled parser; there is no flag to point it at
  someone else's software or network service.
- Crash inputs are parser bytes produced locally; no credentials, no network.
- SECURITY.md states the planted bugs exist for education and must never be
  reused in production code.

## Definition of done

Configures and builds on MSVC/GCC, `ctest` green (unit + 3 e2e gates), both
planted bugs demonstrably found by a fixed seed, results regenerated, no
secrets in tree, git repository with docs.
