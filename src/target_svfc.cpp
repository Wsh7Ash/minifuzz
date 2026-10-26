#include "minifuzz.hpp"

#include <cstring>

namespace minifuzz {
namespace {

// SVCF - a tiny, deliberately vulnerable configuration format used as the
// bundled fuzz target:
//
//   bytes 0..3   magic "SVCF"
//   byte  4      version (must be 1)
//   bytes 5..6   entry count, little endian
//   repeated:
//     byte       key length   (stored into a 32-byte buffer)
//     key bytes  key text
//     byte       flags
//     bytes      value length (little endian uint16, stored into 64 bytes)
//     value bytes value blob
//
// Each field is copied into its own heap arena that carries an 8-byte redzone
// immediately after the usable buffer. The bound checks below contain the
// off-by-one the fuzzer is meant to find: they tolerate ``capacity + 1``.

constexpr std::size_t kKeyCapacity = 32;
constexpr std::size_t kValueCapacity = 64;
constexpr std::size_t kRedzoneSize = 8;
constexpr std::uint8_t kRedzoneByte = 0xA5;
constexpr char kMagic[4] = {'S', 'V', 'C', 'F'};

struct Arena {
    std::vector<std::uint8_t> bytes;  // capacity + redzone, redzone pre-filled
    std::size_t capacity = 0;

    explicit Arena(std::size_t cap) : bytes(cap + kRedzoneSize, kRedzoneByte), capacity(cap) {}
};

// Index of the first redzone byte that changed, or npos.
std::size_t redzone_hit(const Arena& arena) {
    for (std::size_t i = 0; i < kRedzoneSize; ++i) {
        if (arena.bytes[arena.capacity + i] != kRedzoneByte) return arena.capacity + i;
    }
    return std::string::npos;
}

// Buggy copy: ``available`` bytes are written into a ``capacity``-byte buffer
// guarded by a redzone, but the bound check tolerates ``capacity + 1``.
// Returns false for an impossible length *or* a redzone violation; the latter
// is additionally reported through ``crash``.
bool copy_field(const std::uint8_t* src, std::size_t available, std::size_t capacity,
                std::vector<std::uint8_t>& out, const char* bug_id, Crash* crash,
                std::string& error) {
    // BUG(off-by-one): should be ``available > capacity``. Allowing
    // capacity + 1 writes exactly one byte into the redzone.
    if (available > capacity + 1) {
        error = std::string(bug_id) + ": field longer than buffer";
        return false;
    }
    if (available == 0) {
        error = std::string(bug_id) + ": empty field";
        return false;
    }

    Arena arena(capacity);
    std::memcpy(arena.bytes.data(), src, available);

    const std::size_t hit = redzone_hit(arena);
    if (hit != std::string::npos) {
        if (crash) {
            crash->bug = bug_id;
            crash->offset = hit;
            crash->detail = "WRITE of " + std::to_string(available) + " byte(s) into a " +
                            std::to_string(capacity) + "-byte buffer; redzone byte " +
                            std::to_string(hit - capacity) + " overwritten";
        }
        error = std::string(bug_id) + ": redzone overwritten";
        return false;
    }

    out.assign(arena.bytes.begin(), arena.bytes.begin() + static_cast<std::ptrdiff_t>(available));
    return true;
}

ParseResult parse_impl(const std::uint8_t* data, std::size_t size, Crash* crash) {
    ParseResult result;
    std::size_t offset = 0;

    auto need = [&](std::size_t bytes) -> bool {
        if (offset + bytes > size) {
            result.error = "truncated input";
            return false;
        }
        return true;
    };

    if (!need(4)) return result;
    if (std::memcmp(data, kMagic, 4) != 0) {
        result.error = "bad magic";
        return result;
    }
    offset = 4;

    if (!need(1)) return result;
    if (data[offset] != 1) {
        result.error = "unsupported version";
        return result;
    }
    ++offset;

    if (!need(2)) return result;
    const std::size_t count = static_cast<std::size_t>(data[offset]) |
                              (static_cast<std::size_t>(data[offset + 1]) << 8);
    offset += 2;
    if (count > 1024) {
        result.error = "entry count too large";
        return result;
    }

    for (std::size_t i = 0; i < count; ++i) {
        if (!need(1)) return result;
        const std::size_t key_len = data[offset];
        ++offset;
        if (key_len == 0) {
            result.error = "empty key length";
            return result;
        }
        if (!need(key_len)) return result;

        Entry entry;
        std::vector<std::uint8_t> key_bytes;
        std::string error;
        if (!copy_field(data + offset, key_len, kKeyCapacity, key_bytes, "svcf-key-off-by-one",
                        crash, error)) {
            result.error = error;
            return result;
        }
        offset += key_len;
        entry.key.assign(key_bytes.begin(), key_bytes.end());

        if (!need(1)) return result;
        entry.flags = data[offset];
        ++offset;

        if (!need(2)) return result;
        const std::size_t value_len = static_cast<std::size_t>(data[offset]) |
                                      (static_cast<std::size_t>(data[offset + 1]) << 8);
        offset += 2;
        if (!need(value_len)) return result;

        if (!copy_field(data + offset, value_len, kValueCapacity, entry.value,
                        "svcf-value-off-by-one", crash, error)) {
            result.error = error;
            return result;
        }
        offset += value_len;

        result.entries.push_back(std::move(entry));
    }

    result.ok = true;
    return result;
}

}  // namespace

ParseResult parse_svfc(const std::uint8_t* data, std::size_t size) {
    return parse_impl(data, size, nullptr);
}

bool run_target(const std::uint8_t* data, std::size_t size, Crash& out) {
    out = Crash{};
    (void)parse_impl(data, size, &out);
    // A malformed input is a normal rejection, not a crash: only a redzone
    // violation counts. Keeping these apart is what lets the minimiser and the
    // exit code mean something.
    return out.bug.empty();
}

std::vector<std::vector<std::uint8_t>> default_seeds() {
    // Seed 1: well-formed single entry whose value sits exactly at the
    // 64-byte limit - mutating that length byte overflows the buffer.
    std::vector<std::uint8_t> at_limit = {'S', 'V', 'C', 'F', 1, 1, 0};
    at_limit.push_back(3);                       // key length
    at_limit.push_back('k');
    at_limit.push_back('e');
    at_limit.push_back('y');
    at_limit.push_back(0);                       // flags
    at_limit.push_back(64);                      // value length low byte
    at_limit.push_back(0);                       // value length high byte
    for (int i = 0; i < 64; ++i) at_limit.push_back(static_cast<std::uint8_t>('a' + (i % 26)));
    // One byte more than the declared length: the input already has room for a
    // 65-byte read, so a single mutation of the length byte reaches the bug.
    at_limit.push_back('z');

    // Seed 2: key at its 32-byte limit - mutating that length byte overflows.
    std::vector<std::uint8_t> key_at_limit = {'S', 'V', 'C', 'F', 1, 1, 0};
    key_at_limit.push_back(32);
    for (int i = 0; i < 32; ++i) key_at_limit.push_back(static_cast<std::uint8_t>('A' + (i % 26)));
    key_at_limit.push_back(1);                   // flags
    key_at_limit.push_back(4);                   // value length low byte
    key_at_limit.push_back(0);
    for (int i = 0; i < 4; ++i) key_at_limit.push_back('v');

    // Seed 3: two entries, small values - structural variety for the mutator.
    std::vector<std::uint8_t> two_entries = {'S', 'V', 'C', 'F', 1, 2, 0};
    const auto append_text = [](std::vector<std::uint8_t>& out, const char* text,
                                std::size_t length) {
        out.insert(out.end(), text, text + length);
    };
    two_entries.push_back(5);
    append_text(two_entries, "first", 5);
    two_entries.push_back(0);
    two_entries.push_back(3);
    two_entries.push_back(0);
    append_text(two_entries, "one", 3);
    two_entries.push_back(6);
    append_text(two_entries, "second", 6);
    two_entries.push_back(7);
    two_entries.push_back(6);
    two_entries.push_back(0);
    append_text(two_entries, "two!!!", 6);

    // Seed 4: tiny and malformed, keeping early mutations cheap.
    std::vector<std::uint8_t> tiny = {'S', 'V', 'C', 'F', 1, 0, 0};

    return {at_limit, key_at_limit, two_entries, tiny};
}

std::vector<std::vector<std::uint8_t>> default_dictionary() {
    const std::vector<const char*> tokens = {"SVCF", "key", "value", "#", "="};
    std::vector<std::vector<std::uint8_t>> dictionary;
    for (const char* token : tokens) {
        const std::size_t length = std::strlen(token);
        dictionary.emplace_back(token, token + length);
    }
    return dictionary;
}

}  // namespace minifuzz
