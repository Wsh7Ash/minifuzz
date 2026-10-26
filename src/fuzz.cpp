#include "minifuzz.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace minifuzz {

// ---------------------------------------------------------------- rng

Rng::Rng(std::uint64_t seed) : state_(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}

std::uint64_t Rng::next() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 0x2545F4914F6CDD1DULL;
}

std::size_t Rng::below(std::size_t bound) {
    if (bound == 0) return 0;
    // Rejection sampling keeps this platform independent (no libc distribution).
    const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % bound);
    std::uint64_t value = next();
    while (value >= limit) value = next();
    return static_cast<std::size_t>(value % bound);
}

// ---------------------------------------------------------------- mutation

void mutate(std::vector<std::uint8_t>& data, Rng& rng, const MutationOptions& options) {
    const std::size_t rounds = 1 + rng.below(8);
    for (std::size_t round = 0; round < rounds; ++round) {
        if (data.empty()) data.push_back(static_cast<std::uint8_t>(rng.next() & 0xFF));

        const std::size_t choice = rng.below(9);
        switch (choice) {
            case 0: {  // single bit flip
                const std::size_t index = rng.below(data.size());
                data[index] = static_cast<std::uint8_t>(data[index] ^ (1u << rng.below(8)));
                break;
            }
            case 1: {  // set a byte to a random value
                const std::size_t index = rng.below(data.size());
                data[index] = static_cast<std::uint8_t>(rng.next() & 0xFF);
                break;
            }
            case 2: {  // arithmetic on a byte
                const std::size_t index = rng.below(data.size());
                const int delta = static_cast<int>(rng.below(33)) - 16;
                data[index] = static_cast<std::uint8_t>(data[index] + delta);
                break;
            }
            case 3: {  // insert random bytes
                const std::size_t count = 1 + rng.below(8);
                if (data.size() + count > options.max_input) break;
                const std::size_t at = rng.below(data.size() + 1);
                std::vector<std::uint8_t> insert(count);
                for (std::size_t i = 0; i < count; ++i) {
                    insert[i] = static_cast<std::uint8_t>(rng.next() & 0xFF);
                }
                data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), insert.begin(),
                            insert.end());
                break;
            }
            case 4: {  // delete a slice
                if (data.size() <= 1) break;
                const std::size_t at = rng.below(data.size());
                const std::size_t count = 1 + rng.below(std::min<std::size_t>(16, data.size() - at));
                data.erase(data.begin() + static_cast<std::ptrdiff_t>(at),
                           data.begin() + static_cast<std::ptrdiff_t>(at + count));
                break;
            }
            case 5: {  // duplicate a slice
                if (data.size() * 2 > options.max_input) break;
                const std::size_t length = 1 + rng.below(std::min<std::size_t>(32, data.size()));
                const std::size_t from = rng.below(data.size() - length + 1);
                const std::size_t to = rng.below(data.size() + 1);
                std::vector<std::uint8_t> slice(data.begin() + static_cast<std::ptrdiff_t>(from),
                                                data.begin() +
                                                    static_cast<std::ptrdiff_t>(from + length));
                data.insert(data.begin() + static_cast<std::ptrdiff_t>(to), slice.begin(),
                            slice.end());
                break;
            }
            case 6: {  // swap two slices
                if (data.size() < 4) break;
                const std::size_t half = data.size() / 2;
                const std::size_t width = 1 + rng.below(half);
                const std::size_t a = rng.below(half - width + 1);
                const std::size_t b = half + rng.below(data.size() - half - width + 1);
                for (std::size_t i = 0; i < width; ++i) std::swap(data[a + i], data[b + i]);
                break;
            }
            case 7: {  // overwrite with a dictionary token
                if (options.dictionary.empty()) break;
                const auto& token = options.dictionary[rng.below(options.dictionary.size())];
                if (token.empty()) break;
                const std::size_t at = rng.below(data.size());
                for (std::size_t i = 0; i < token.size() && at + i < data.size(); ++i) {
                    data[at + i] = token[i];
                }
                break;
            }
            default: {  // splice with another corpus entry handled by caller data only
                const std::size_t index = rng.below(data.size());
                data[index] = static_cast<std::uint8_t>(~data[index]);
                break;
            }
        }
        if (data.size() > options.max_input) data.resize(options.max_input);
    }
}

// ---------------------------------------------------------------- io

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream handle(path, std::ios::binary);
    if (!handle) return {};
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(handle)),
                                     std::istreambuf_iterator<char>());
}

bool write_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    const fs::path target(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    std::ofstream handle(path, std::ios::binary | std::ios::trunc);
    if (!handle) return false;
    if (!bytes.empty()) {
        handle.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    return handle.good();
}

std::vector<std::vector<std::uint8_t>> load_corpus(const std::vector<std::string>& paths) {
    std::vector<std::vector<std::uint8_t>> corpus;
    for (const std::string& path : paths) {
        std::error_code ec;
        const fs::path entry(path);
        if (fs::is_directory(entry, ec)) {
            std::vector<std::string> files;
            for (const auto& item : fs::directory_iterator(entry, ec)) {
                if (item.is_regular_file()) files.push_back(item.path().string());
            }
            std::sort(files.begin(), files.end());
            for (const std::string& file : files) {
                std::vector<std::uint8_t> bytes = read_file(file);
                if (!bytes.empty()) corpus.push_back(std::move(bytes));
            }
        } else if (fs::is_regular_file(entry, ec)) {
            std::vector<std::uint8_t> bytes = read_file(path);
            if (!bytes.empty()) corpus.push_back(std::move(bytes));
        }
    }
    return corpus;
}

// ---------------------------------------------------------------- reporting

std::string crash_name(const std::string& bug, const std::vector<std::uint8_t>& bytes) {
    // FNV-1a 64-bit: stable, dependency free, enough to disambiguate crashes.
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001B3ULL;
    }
    std::ostringstream name;
    name << bug << "-" << std::hex << std::setfill('0') << std::setw(16) << hash << ".bin";
    return name.str();
}

std::vector<std::uint8_t> minimize(const std::vector<std::uint8_t>& input,
                                   const std::function<bool(const std::vector<std::uint8_t>&)>&
                                       still_crashes) {
    if (!still_crashes(input)) return input;  // nothing to do; keep as-is

    std::vector<std::uint8_t> current = input;
    std::size_t chunk = current.size() / 2;
    while (chunk > 0) {
        bool reduced = false;
        for (std::size_t start = 0; start + chunk <= current.size();) {
            std::vector<std::uint8_t> candidate;
            candidate.reserve(current.size() - chunk);
            candidate.insert(candidate.end(), current.begin(),
                             current.begin() + static_cast<std::ptrdiff_t>(start));
            candidate.insert(candidate.end(),
                             current.begin() + static_cast<std::ptrdiff_t>(start + chunk),
                             current.end());
            if (candidate.size() < current.size() && still_crashes(candidate)) {
                current.swap(candidate);
                reduced = true;
                // stay at the same start: the next chunk shifted into place
            } else {
                start += chunk;
            }
        }
        if (!reduced) chunk /= 2;
    }
    return current;
}

// ---------------------------------------------------------------- fuzz loop

RunStats fuzz(const RunOptions& options) {
    RunStats stats;
    if (options.seeds.empty()) return stats;

    Rng rng(options.seed == 0 ? 0x5EED5EED5EED5EEDULL : options.seed);
    MutationOptions mutation = options.mutation;
    if (mutation.dictionary.empty()) mutation.dictionary = default_dictionary();
    mutation.max_input = options.max_input;

    std::vector<std::vector<std::uint8_t>> corpus = options.seeds;
    std::vector<std::string> seen_bugs;

    const auto started = std::chrono::steady_clock::now();

    for (std::size_t iteration = 0; iteration < options.iterations; ++iteration) {
        std::vector<std::uint8_t> candidate = corpus[rng.below(corpus.size())];
        mutate(candidate, rng, mutation);

        Crash crash;
        ++stats.executed;
        if (run_target(candidate.data(), candidate.size(), crash)) continue;

        ++stats.crashes;
        const bool known =
            std::find(seen_bugs.begin(), seen_bugs.end(), crash.bug) != seen_bugs.end();

        // Only a *new* bug costs a minimisation pass, and crashing inputs are
        // deliberately never added to the corpus: they would dominate the
        // population and hide every other bug behind repeats of the first one.
        if (known) continue;
        seen_bugs.push_back(crash.bug);
        stats.bug_ids.push_back(crash.bug);

        const std::vector<std::uint8_t> minimal = minimize(candidate, [&](const auto& input) {
            Crash probe;
            return !run_target(input.data(), input.size(), probe) && probe.bug == crash.bug;
        });

        const std::string name = crash_name(crash.bug, minimal);
        const std::string path = options.crashes_dir + "/" + name;
        if (write_file(path, minimal)) stats.crash_files.push_back(path);

        if (!options.keep_going) {
            stats.stopped_early = true;
            break;
        }
    }

    const auto ended = std::chrono::steady_clock::now();
    stats.seconds = std::chrono::duration<double>(ended - started).count();
    stats.unique_bugs = seen_bugs.size();
    return stats;
}

}  // namespace minifuzz
