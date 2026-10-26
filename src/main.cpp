#include "minifuzz.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage(std::ostream& out) {
    out << "usage: minifuzz [options] [--replay FILE | --init-corpus DIR]\n"
           "\n"
           "Mutation fuzzer for the bundled SVCF parser (see docs/ARCHITECTURE.md).\n"
           "\n"
           "options:\n"
           "  --corpus PATH        seed corpus file or directory (repeatable)\n"
           "  --seed N             RNG seed; the same seed reproduces the same search\n"
           "  --iterations N       inputs to execute (default 100000)\n"
           "  --crashes-dir DIR    where crash inputs are written (default ./crashes)\n"
           "  --max-input N        cap on generated input size (default 4096)\n"
           "  --keep-going         do not stop at the first crash\n"
           "  --replay FILE        run one input and report; no fuzzing\n"
           "  --init-corpus DIR    write the built-in seed inputs into DIR and exit\n"
           "  --version            print version and exit\n"
           "\n"
           "exit codes: 0 no crash, 1 crash found or reproduced, 2 usage error\n";
}

int replay(const std::string& path) {
    const std::vector<std::uint8_t> bytes = minifuzz::read_file(path);
    if (bytes.empty()) {
        std::cerr << "minifuzz: cannot read " << path << "\n";
        return 2;
    }
    minifuzz::Crash crash;
    if (minifuzz::run_target(bytes.data(), bytes.size(), crash)) {
        std::cout << "no crash: " << path << " (" << bytes.size() << " bytes)\n";
        return 0;
    }
    std::cout << "CRASH " << crash.bug << " at redzone offset " << crash.offset << "\n"
              << "  " << crash.detail << "\n"
              << "  input: " << path << " (" << bytes.size() << " bytes)\n";
    return 1;
}

int init_corpus(const std::string& dir) {
    const std::vector<std::vector<std::uint8_t>> seeds = minifuzz::default_seeds();
    int written = 0;
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        const std::string path =
            dir + "/seed_" + std::to_string(i + 1) + ".svcf";
        if (minifuzz::write_file(path, seeds[i])) ++written;
    }
    if (written != static_cast<int>(seeds.size())) {
        std::cerr << "minifuzz: wrote " << written << " of " << seeds.size() << " seeds\n";
        return 2;
    }
    std::cout << "wrote " << written << " seed input(s) to " << dir << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> corpus_paths;
    std::string crashes_dir = "crashes";
    std::string replay_path;
    std::string init_dir;
    std::size_t iterations = 100000;
    std::size_t max_input = 4096;
    std::uint64_t seed = 0;
    bool keep_going = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "minifuzz: " << flag << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            usage(std::cout);
            return 0;
        } else if (arg == "--version") {
            std::cout << "minifuzz 0.1.0\n";
            return 0;
        } else if (arg == "--corpus") {
            corpus_paths.push_back(value("--corpus"));
        } else if (arg == "--seed") {
            seed = std::stoull(value("--seed"));
        } else if (arg == "--iterations") {
            iterations = static_cast<std::size_t>(std::stoull(value("--iterations")));
        } else if (arg == "--max-input") {
            max_input = static_cast<std::size_t>(std::stoull(value("--max-input")));
        } else if (arg == "--crashes-dir") {
            crashes_dir = value("--crashes-dir");
        } else if (arg == "--keep-going") {
            keep_going = true;
        } else if (arg == "--replay") {
            replay_path = value("--replay");
        } else if (arg == "--init-corpus") {
            init_dir = value("--init-corpus");
        } else {
            std::cerr << "minifuzz: unknown option: " << arg << "\n";
            usage(std::cerr);
            return 2;
        }
    }

    if (!init_dir.empty()) return init_corpus(init_dir);
    if (!replay_path.empty()) return replay(replay_path);

    minifuzz::RunOptions options;
    options.iterations = iterations;
    options.seed = seed;
    options.crashes_dir = crashes_dir;
    options.max_input = max_input;
    options.keep_going = keep_going;
    options.mutation.max_input = max_input;

    if (corpus_paths.empty()) {
        options.seeds = minifuzz::default_seeds();
        std::cout << "using " << options.seeds.size() << " built-in seed input(s)\n";
    } else {
        options.seeds = minifuzz::load_corpus(corpus_paths);
        if (options.seeds.empty()) {
            std::cerr << "minifuzz: no usable seed inputs in the given corpus\n";
            return 2;
        }
        std::cout << "loaded " << options.seeds.size() << " seed input(s)\n";
    }

    std::cout << "seed=" << (seed == 0 ? 0x5EED5EED5EED5EEDULL : seed)
              << " iterations=" << iterations << " max_input=" << max_input
              << " target=svcf-parser\n";

    const minifuzz::RunStats stats = minifuzz::fuzz(options);

    const double rate = stats.seconds > 0.0 ? stats.executed / stats.seconds : 0.0;
    std::cout << "executed " << stats.executed << " input(s) in " << stats.seconds << "s ("
              << static_cast<std::size_t>(rate) << "/s), "
              << "crashes=" << stats.crashes << " unique_bugs=" << stats.unique_bugs << "\n";

    if (stats.unique_bugs == 0) {
        std::cout << "no crash found\n";
        return 0;
    }

    for (const std::string& path : stats.crash_files) {
        std::cout << "saved " << path << "\n";
        minifuzz::Crash crash;
        const std::vector<std::uint8_t> bytes = minifuzz::read_file(path);
        if (!bytes.empty() && !minifuzz::run_target(bytes.data(), bytes.size(), crash)) {
            std::cout << "  " << crash.bug << ": " << crash.detail << "\n"
                      << "  replay: minifuzz --replay " << path << "\n";
        }
    }
    return 1;
}
