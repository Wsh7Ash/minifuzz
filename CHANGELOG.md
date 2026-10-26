# Changelog

All notable changes to this project are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.0] - 2026-10-26

### Added

- SVCF bundled target: documented binary config format with two planted
  off-by-one bounds checks (32-byte key, 64-byte value) and 8-byte redzone
  detection per copy.
- `run_target` semantics that separate a malformed-input rejection from a
  redzone crash, so exit codes and minimisation are meaningful.
- Mutation engine: bit flip, byte set, arithmetic, insert, delete, slice
  duplicate, slice swap, dictionary overwrite, byte invert; 1–8 per input with
  a `max_input` cap.
- Deterministic xorshift64\* RNG with rejection sampling (platform independent).
- Delta-debugging minimiser that preserves the bug id; FNV-1a 64-bit crash
  file naming.
- Corpus policy: crashing inputs are never added back (prevents a single bug
  from dominating the search), minimisation runs only for new bugs.
- CLI: `--corpus`, `--seed`, `--iterations`, `--crashes-dir`, `--max-input`,
  `--keep-going`, `--replay`, `--init-corpus`.
- 53-check unit suite plus three CTest gates (clean crash dir, fixed-seed
  search must find a crash, every saved crash must replay).
- Optional `-DMINIFUZZ_ASAN=ON` AddressSanitizer build.
