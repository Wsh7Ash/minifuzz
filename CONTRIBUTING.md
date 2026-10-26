# Contributing

Thanks for helping improve `minifuzz`.

## Build and test

```shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest -C Debug --output-on-failure   # add -C Release if built Release
```

C++20, CMake 3.16+, no third-party dependencies. Must build clean with MSVC
(`/W4 /permissive-`) and GCC/Clang (`-Wall -Wextra -Wpedantic`).

## Changing the search

- Keep `Rng` platform independent: no `std::uniform_int_distribution`, no
  library-provided hashing. Determinism is a tested contract.
- A new mutation operator needs a row in `docs/ARCHITECTURE.md` and must
  respect `max_input`.
- Never add crashing inputs to the corpus; the reason is documented in
  `docs/ARCHITECTURE.md` and was a real failure mode.

## Adding or changing the target

The target must be the bundled parser (or your own code in a fork). Implement
`run_target` so that:

1. malformed input returns "no crash" with an empty `Crash`,
2. a memory-safety violation fills `bug`, `offset` and `detail`,
3. `bug` ids are stable strings — they name crash files and deduplication.

Add unit coverage for both boundaries (exactly at capacity, one past it).

## Reporting bugs

Open a GitHub security advisory for crashes or hangs in the fuzzer itself; use
a normal issue for slow searches, bad mutations or documentation gaps. See
`SECURITY.md`.
