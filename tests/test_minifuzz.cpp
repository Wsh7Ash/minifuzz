// Unit tests for minifuzz: parser correctness, redzone detection, RNG
// determinism, mutation, corpus I/O, minimisation and a short fuzz run.
// Tiny assert-style harness on purpose - no third-party test framework.

#include "minifuzz.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what, const std::string& detail = "") {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << what;
    if (!detail.empty()) std::cerr << "  [" << detail << "]";
    std::cerr << "\n";
}

std::vector<std::uint8_t> bytes_of(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

// Build a well-formed SVCF buffer with explicit key/value lengths.
std::vector<std::uint8_t> make_file(std::size_t key_len, std::size_t value_len) {
    std::vector<std::uint8_t> data = {'S', 'V', 'C', 'F', 1, 1, 0};
    data.push_back(static_cast<std::uint8_t>(key_len));
    for (std::size_t i = 0; i < key_len; ++i) {
        data.push_back(static_cast<std::uint8_t>('k' + (i % 3)));
    }
    data.push_back(0);  // flags
    data.push_back(static_cast<std::uint8_t>(value_len & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value_len >> 8) & 0xFF));
    for (std::size_t i = 0; i < value_len; ++i) {
        data.push_back(static_cast<std::uint8_t>('v'));
    }
    return data;
}

void test_parse_valid_file() {
    const std::vector<std::uint8_t> first_seed = minifuzz::default_seeds()[0];
    const minifuzz::ParseResult result =
        minifuzz::parse_svfc(first_seed.data(), first_seed.size());
    check(result.ok, "seed 1 parses", result.error);
    check(result.entries.size() == 1, "seed 1 has one entry");
    if (!result.entries.empty()) {
        check(result.entries[0].key == "key", "seed 1 key round-trips",
              result.entries[0].key);
        check(result.entries[0].value.size() == 64, "seed 1 value is 64 bytes",
              std::to_string(result.entries[0].value.size()));
        check(result.entries[0].flags == 0, "seed 1 flags");
    }

    const auto seeds = minifuzz::default_seeds();
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        const minifuzz::ParseResult parsed =
            minifuzz::parse_svfc(seeds[i].data(), seeds[i].size());
        check(parsed.ok, "seed " + std::to_string(i + 1) + " parses", parsed.error);
    }
}

void test_parse_rejects_malformed() {
    const std::vector<std::uint8_t> no_magic = bytes_of("XXXX");
    check(!minifuzz::parse_svfc(no_magic.data(), no_magic.size()).ok, "bad magic rejected");

    const std::vector<std::uint8_t> short_input = bytes_of("SV");
    check(!minifuzz::parse_svfc(short_input.data(), short_input.size()).ok,
          "truncated input rejected");

    std::vector<std::uint8_t> bad_version = {'S', 'V', 'C', 'F', 9, 0, 0};
    check(!minifuzz::parse_svfc(bad_version.data(), bad_version.size()).ok,
          "unsupported version rejected");

    std::vector<std::uint8_t> huge_count = {'S', 'V', 'C', 'F', 1, 0xFF, 0xFF};
    check(!minifuzz::parse_svfc(huge_count.data(), huge_count.size()).ok,
          "entry count guard rejected");
}

void test_boundary_is_clean() {
    // Exactly at capacity: no crash.
    const std::vector<std::uint8_t> at_limit = make_file(32, 64);
    minifuzz::Crash crash;
    check(minifuzz::run_target(at_limit.data(), at_limit.size(), crash),
          "32/64-byte fields are exactly at the limit", crash.detail);

    // One past capacity on the value: redzone hit.
    const std::vector<std::uint8_t> value_overflow = make_file(32, 65);
    minifuzz::Crash overflow;
    check(!minifuzz::run_target(value_overflow.data(), value_overflow.size(), overflow),
          "65-byte value is reported as a crash");
    check(overflow.bug == "svcf-value-off-by-one", "value bug id", overflow.bug);
    check(overflow.offset == 64, "redzone offset is 64", std::to_string(overflow.offset));
    check(overflow.detail.find("64-byte buffer") != std::string::npos,
          "detail names the buffer size", overflow.detail);

    // One past capacity on the key.
    const std::vector<std::uint8_t> key_overflow = make_file(33, 4);
    minifuzz::Crash key_crash;
    check(!minifuzz::run_target(key_overflow.data(), key_overflow.size(), key_crash),
          "33-byte key is reported as a crash");
    check(key_crash.bug == "svcf-key-off-by-one", "key bug id", key_crash.bug);
    check(key_crash.offset == 32, "key redzone offset is 32",
          std::to_string(key_crash.offset));

    // Far too long is rejected by the bound check without touching a redzone.
    const std::vector<std::uint8_t> way_too_long = make_file(32, 400);
    minifuzz::Crash long_crash;
    check(minifuzz::run_target(way_too_long.data(), way_too_long.size(), long_crash),
          "400-byte value is rejected without touching a redzone");
    check(long_crash.bug.empty(), "rejection is not reported as a crash", long_crash.bug);

    // Ordinary malformed input is a rejection, not a crash.
    minifuzz::Crash malformed;
    check(minifuzz::run_target(reinterpret_cast<const std::uint8_t*>("nope"), 4, malformed),
          "bad magic is a clean rejection", malformed.bug);
}

void test_run_target_reports_only_on_crash() {
    const std::vector<std::uint8_t> ok = make_file(4, 8);
    minifuzz::Crash crash;
    crash.bug = "preset";
    check(minifuzz::run_target(ok.data(), ok.size(), crash), "clean input returns true");
    check(crash.bug.empty(), "crash struct is reset on success", crash.bug);
}

void test_rng_is_deterministic() {
    minifuzz::Rng a(42);
    minifuzz::Rng b(42);
    bool identical = true;
    for (int i = 0; i < 1000; ++i) {
        if (a.next() != b.next()) {
            identical = false;
            break;
        }
    }
    check(identical, "same seed produces the same stream");

    minifuzz::Rng c(43);
    bool differs = false;
    minifuzz::Rng d(42);
    for (int i = 0; i < 100; ++i) {
        if (c.next() != d.next()) {
            differs = true;
            break;
        }
    }
    check(differs, "different seeds produce different streams");

    minifuzz::Rng e(7);
    bool in_range = true;
    for (int i = 0; i < 2000; ++i) {
        if (e.below(10) >= 10) {
            in_range = false;
            break;
        }
    }
    check(in_range, "below() stays in range");
}

void test_mutate_deterministic_and_bounded() {
    minifuzz::MutationOptions options;
    options.max_input = 256;
    options.dictionary = minifuzz::default_dictionary();

    std::vector<std::uint8_t> first = minifuzz::default_seeds()[0];
    std::vector<std::uint8_t> second = first;
    minifuzz::Rng a(1234);
    minifuzz::Rng b(1234);
    minifuzz::mutate(first, a, options);
    minifuzz::mutate(second, b, options);
    check(first == second, "mutation is reproducible for a fixed seed");

    minifuzz::Rng c(99);
    std::vector<std::uint8_t> data = minifuzz::default_seeds()[0];
    for (int i = 0; i < 500; ++i) {
        minifuzz::mutate(data, c, options);
        if (data.size() > options.max_input) break;
    }
    check(data.size() <= options.max_input, "max_input is respected",
          std::to_string(data.size()));

    minifuzz::Rng empty_rng(5);
    std::vector<std::uint8_t> empty;
    minifuzz::mutate(empty, empty_rng, options);
    check(!empty.empty(), "mutating an empty buffer produces input");
}

void test_crash_name_is_stable() {
    const std::vector<std::uint8_t> a = bytes_of("abc");
    const std::vector<std::uint8_t> b = bytes_of("abc");
    const std::vector<std::uint8_t> c = bytes_of("abd");
    check(minifuzz::crash_name("bug", a) == minifuzz::crash_name("bug", b),
          "same content -> same name");
    check(minifuzz::crash_name("bug", a) != minifuzz::crash_name("bug", c),
          "different content -> different name");
    check(minifuzz::crash_name("bug", a).rfind("bug-", 0) == 0, "name starts with bug id",
          minifuzz::crash_name("bug", a));
}

void test_minimiser_shrinks_and_keeps_crash() {
    const std::vector<std::uint8_t> crashing = make_file(32, 65);
    // Pad it so there is something to remove.
    std::vector<std::uint8_t> padded = crashing;
    const std::vector<std::uint8_t> tail = bytes_of("PADDING-TO-REMOVE");
    padded.insert(padded.end(), tail.begin(), tail.end());

    minifuzz::Crash initial;
    check(!minifuzz::run_target(padded.data(), padded.size(), initial),
          "padded input still crashes", initial.detail);
    minifuzz::Crash probe;
    const auto still_crashes = [&](const std::vector<std::uint8_t>& input) {
        return !minifuzz::run_target(input.data(), input.size(), probe);
    };

    const std::vector<std::uint8_t> minimal = minifuzz::minimize(padded, still_crashes);
    check(minimal.size() <= padded.size(), "minimiser never grows the input",
          std::to_string(minimal.size()) + " <= " + std::to_string(padded.size()));
    check(minimal.size() < padded.size(), "minimiser removes the padding",
          std::to_string(minimal.size()));
    check(still_crashes(minimal), "minimised input still crashes");
    check(probe.bug == "svcf-value-off-by-one", "minimised input hits the same bug",
          probe.bug);
}

void test_corpus_io_round_trip() {
    const fs::path dir = fs::temp_directory_path() / "minifuzz-test-corpus";
    std::error_code ec;
    fs::remove_all(dir, ec);

    const auto seeds = minifuzz::default_seeds();
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        const std::string path = (dir / ("seed_" + std::to_string(i) + ".svcf")).string();
        check(minifuzz::write_file(path, seeds[i]), "write seed " + path);
    }

    const auto loaded = minifuzz::load_corpus({dir.string()});
    check(loaded.size() == seeds.size(), "loaded seed count",
          std::to_string(loaded.size()));
    bool same = loaded.size() == seeds.size();
    if (same) {
        for (std::size_t i = 0; i < loaded.size(); ++i) {
            if (loaded[i] != seeds[i]) same = false;
        }
    }
    check(same, "corpus round-trips byte for byte");

    check(minifuzz::load_corpus({"does-not-exist"}).empty(), "missing corpus is empty");
    fs::remove_all(dir, ec);
}

void test_short_fuzz_run_finds_the_bug() {
    minifuzz::RunOptions options;
    options.seeds = minifuzz::default_seeds();
    options.seed = 20261026;
    options.iterations = 200000;
    options.max_input = 512;
    options.crashes_dir = (fs::temp_directory_path() / "minifuzz-test-crashes").string();
    options.mutation.max_input = 512;

    std::error_code ec;
    fs::remove_all(options.crashes_dir, ec);

    const minifuzz::RunStats stats = minifuzz::fuzz(options);
    check(stats.executed > 0, "fuzzer executed inputs");
    check(stats.unique_bugs >= 1, "fuzzer found at least one bug",
          "executed=" + std::to_string(stats.executed));
    check(!stats.crash_files.empty(), "a crash file was written");
    check(stats.bug_ids.size() == stats.unique_bugs, "unique bug list matches counter");

    if (!stats.crash_files.empty()) {
        const std::vector<std::uint8_t> saved = minifuzz::read_file(stats.crash_files[0]);
        minifuzz::Crash crash;
        check(!saved.empty(), "crash file readable");
        check(!minifuzz::run_target(saved.data(), saved.size(), crash),
              "saved crash reproduces");
    }
    fs::remove_all(options.crashes_dir, ec);
}

}  // namespace

int main() {
    test_parse_valid_file();
    test_parse_rejects_malformed();
    test_boundary_is_clean();
    test_run_target_reports_only_on_crash();
    test_rng_is_deterministic();
    test_mutate_deterministic_and_bounded();
    test_crash_name_is_stable();
    test_minimiser_shrinks_and_keeps_crash();
    test_corpus_io_round_trip();
    test_short_fuzz_run_finds_the_bug();

    std::cout << g_checks << " check(s), " << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}
