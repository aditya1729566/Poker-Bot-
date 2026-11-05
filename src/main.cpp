#include "pokerzero/agents.hpp"
#include "pokerzero/match.hpp"
#include "pokerzero/small_games.hpp"
#include "pokerzero/restricted.hpp"
#include "build_info.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

using namespace pokerzero;

namespace {
std::uint64_t number(const std::string& text) {
  std::size_t used = 0;
  const auto value = std::stoull(text, &used);
  if (used != text.size()) throw std::invalid_argument("invalid integer: " + text);
  return value;
}
void usage() {
  std::cerr << "Usage:\n"
            << "  pokerzero match --agent1 NAME --agent2 NAME --hands EVEN_N --seed S [--cohort development|validation|final] [--output FILE]\n"
            << "  pokerzero train --game kuhn|leduc|hunl --algorithm cfr|cfr_plus|external_sampling_mccfr --iterations N --seed S --output FILE\n"
            << "  pokerzero evaluate --candidate FILE --game kuhn|leduc --metric exploitability\n";
}
std::map<std::string, std::string> options(int argc, char** argv) {
  std::map<std::string, std::string> result;
  for (int i = 2; i < argc; i += 2) {
    if (i + 1 >= argc) throw std::invalid_argument("missing option value");
    if (!result.emplace(argv[i], argv[i + 1]).second) throw std::invalid_argument("duplicate option");
  }
  return result;
}
std::string required(const std::map<std::string, std::string>& values, const std::string& key) {
  const auto found = values.find(key);
  if (found == values.end()) throw std::invalid_argument("missing option: " + key);
  return found->second;
}
SmallGameKind parse_game(const std::string& text) {
  if (text == "kuhn") return SmallGameKind::Kuhn;
  if (text == "leduc") return SmallGameKind::Leduc;
  throw std::invalid_argument("unknown game: " + text);
}
CfrAlgorithm parse_algorithm(const std::string& text) {
  if (text == "cfr") return CfrAlgorithm::Vanilla;
  if (text == "cfr_plus") return CfrAlgorithm::Plus;
  if (text == "external_sampling_mccfr") return CfrAlgorithm::ExternalSampling;
  throw std::invalid_argument("unknown algorithm: " + text);
}
std::string utc_now() {
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc{};
  gmtime_r(&now, &utc);
  char buffer[32]{};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buffer;
}
std::string trim(std::string text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string::npos) return "";
  const auto last = text.find_last_not_of(" \t\r");
  return text.substr(first, last - first + 1);
}
std::map<std::string, std::string> flat_yaml(const std::string& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot read configuration: " + path);
  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(file, line)) {
    line = trim(line.substr(0, line.find('#')));
    if (line.empty()) continue;
    const auto colon = line.find(':');
    if (colon == std::string::npos) throw std::runtime_error("invalid flat YAML: " + path);
    const auto key = trim(line.substr(0, colon)), value = trim(line.substr(colon + 1));
    if (key.empty() || value.empty() || !values.emplace(key, value).second)
      throw std::runtime_error("invalid or duplicate YAML key: " + path);
  }
  return values;
}
void validate_compiled_config() {
  const auto game = flat_yaml("configs/game.yaml");
  const auto abstraction = flat_yaml("configs/action_abstraction.yaml");
  const std::map<std::string, std::string> expected_game{
      {"schema_version", "1"}, {"id", "hunl_100bb_v1"}, {"players", "2"}, {"deck_cards", "52"},
      {"initial_stack_chips", "10000"}, {"small_blind_chips", "50"}, {"big_blind_chips", "100"},
      {"ante_chips", "0"}, {"rake_chips", "0"}};
  const std::map<std::string, std::string> expected_abstraction{
      {"schema_version", "1"}, {"id", "hunl_abs_v1"}, {"max_aggressive_actions_per_street", "2"},
      {"size_1_numerator", "33"}, {"size_1_denominator", "100"}, {"size_2_numerator", "3"},
      {"size_2_denominator", "4"}, {"size_3_numerator", "5"}, {"size_3_denominator", "4"},
      {"preflop_classes", "169"}, {"postflop_equity_buckets", "32"}, {"postflop_equity_samples", "64"},
      {"include_effective_all_in", "true"}, {"min_policy_visits_for_inference", "10"}};
  if (game != expected_game || abstraction != expected_abstraction)
    throw std::runtime_error("versioned config differs from compiled rules or abstraction");
}
std::string fnv_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot hash file: " + path);
  std::uint64_t hash = 14695981039346656037ULL;
  char character;
  while (file.get(character)) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ULL;
  }
  std::ostringstream formatted;
  formatted << std::hex << std::setfill('0') << std::setw(16) << hash;
  return formatted.str();
}
std::string json_quote(const std::string& value) {
  std::ostringstream result;
  result << '"';
  for (const unsigned char character : value) {
    if (character == '"' || character == '\\') result << '\\' << static_cast<char>(character);
    else if (character < 0x20) {
      result << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<int>(character) << std::dec;
    } else result << static_cast<char>(character);
  }
  result << '"';
  return result.str();
}
std::string cpu_name() {
#ifdef __APPLE__
  char name[128]{};
  std::size_t length = sizeof(name);
  if (sysctlbyname("machdep.cpu.brand_string", name, &length, nullptr, 0) == 0) return name;
#endif
  return "unknown";
}
std::uint64_t peak_rss_bytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#ifdef __APPLE__
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) { usage(); return 2; }
    const std::string command = argv[1];
    if (command == "train") {
      const auto opts = options(argc, argv);
      if (opts.size() != 5) throw std::invalid_argument("train requires game, algorithm, iterations, seed, output");
      const auto game_name = required(opts, "--game");
      const auto algorithm = parse_algorithm(required(opts, "--algorithm"));
      const auto iterations = number(required(opts, "--iterations"));
      const auto seed = number(required(opts, "--seed"));
      const auto path = required(opts, "--output");
      if (iterations == 0) throw std::invalid_argument("iterations must be positive");
      const auto start = std::chrono::steady_clock::now();
      if (game_name == "hunl") {
        validate_compiled_config();
        if (algorithm != CfrAlgorithm::ExternalSampling)
          throw std::invalid_argument("restricted HUNL currently requires external_sampling_mccfr");
        RestrictedSolver solver(seed);
        solver.train(iterations);
        solver.save(path);
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "{\"stage\":4,\"game\":\"hunl\",\"algorithm\":\"external_sampling_mccfr\",\"iterations\":" << iterations
                  << ",\"seed\":" << seed << ",\"infosets\":" << solver.infosets()
                  << ",\"bucket_cache_entries\":" << solver.bucket_cache_size()
                  << ",\"artifact_hash_fnv1a64\":\"" << fnv_file(path) << "\""
                  << ",\"game_config_hash_fnv1a64\":\"" << fnv_file("configs/game.yaml") << "\""
                  << ",\"action_config_hash_fnv1a64\":\"" << fnv_file("configs/action_abstraction.yaml") << "\""
                  << ",\"seconds\":" << elapsed << ",\"peak_rss_bytes\":" << peak_rss_bytes()
                  << ",\"exact_exploitability\":null}\n";
        return 0;
      }
      const auto game = parse_game(game_name);
      SmallSolver solver(game, algorithm, seed);
      solver.train(iterations);
      solver.save(path);
      const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      std::cout << "{\"stage\":2,\"game\":\"" << small_game_name(game) << "\",\"algorithm\":\""
                << cfr_algorithm_name(algorithm) << "\",\"iterations\":" << iterations << ",\"seed\":" << seed
                << ",\"seed_usage\":\"" << (algorithm == CfrAlgorithm::ExternalSampling ? "training_rng" : "reserved_exact_cfr_is_deterministic")
                << "\",\"infosets\":" << solver.infosets()
                << ",\"expected_value\":" << solver.expected_value() << ",\"exact_exploitability\":" << solver.exploitability()
                << ",\"seconds\":" << elapsed << "}\n";
      return 0;
    }
    if (command == "evaluate") {
      const auto opts = options(argc, argv);
      if (opts.size() != 3 || required(opts, "--metric") != "exploitability")
        throw std::invalid_argument("evaluate requires candidate, game, metric=exploitability");
      const auto game = parse_game(required(opts, "--game"));
      const auto solver = SmallSolver::load(required(opts, "--candidate"));
      if (solver.game() != game) throw std::invalid_argument("artifact game does not match --game");
      std::cout << "{\"game\":\"" << small_game_name(game) << "\",\"iterations\":" << solver.iterations()
                << ",\"exact_exploitability\":" << solver.exploitability() << "}\n";
      return 0;
    }
    if (command != "match") { usage(); return 2; }
    validate_compiled_config();
    std::string agent1, agent2, output, cohort = "development";
    std::uint64_t hands = 0, seed = 0;
    bool have_seed = false;
    for (int i = 2; i < argc; i += 2) {
      if (i + 1 >= argc) throw std::invalid_argument("missing option value");
      const std::string option = argv[i], value = argv[i + 1];
      if (option == "--agent1") agent1 = value;
      else if (option == "--agent2") agent2 = value;
      else if (option == "--hands") hands = number(value);
      else if (option == "--seed") { seed = number(value); have_seed = true; }
      else if (option == "--cohort") cohort = value;
      else if (option == "--output") output = value;
      else throw std::invalid_argument("unknown option: " + option);
    }
    if (agent1.empty() || agent2.empty() || hands < 4 || hands % 2 != 0 || !have_seed)
      throw std::invalid_argument("agent names, even hands >= 4, and seed are required");
    const auto manifest = flat_yaml("benchmarks/manifest.yaml");
    if (required(manifest, "schema_version") != "1" || required(manifest, "id") != "initial_hunl_v1")
      throw std::runtime_error("unsupported benchmark manifest");
    if (cohort == "validation" && seed != number(required(manifest, "validation_seed")))
      throw std::invalid_argument("validation seed must match benchmark manifest");
    if (cohort == "final") {
      if (seed != number(required(manifest, "stage4_final_seed")))
        throw std::invalid_argument("final seed must match benchmark manifest");
      if (POKERZERO_GIT_DIRTY || std::string(POKERZERO_GIT_COMMIT) == "unborn")
        throw std::runtime_error("final evaluation requires a frozen clean source commit");
    }
    if (cohort != "development" && cohort != "validation" && cohort != "final")
      throw std::invalid_argument("unknown cohort");
    auto construct = [](const std::string& name) -> std::unique_ptr<IAgent> {
      if (name.rfind("restricted:", 0) == 0) return std::make_unique<RestrictedAgent>(name.substr(11));
      return make_agent(name);
    };
    auto first = construct(agent1), second = construct(agent2);
    const auto result = run_paired_match(*first, *second, hands / 2, seed);
    const auto timestamp = utc_now();
    std::ostringstream json;
    const bool restricted_match = agent1.rfind("restricted:", 0) == 0 || agent2.rfind("restricted:", 0) == 0;
    json << "{\"schema_version\":1,\"run_id\":\"" << cohort << '-' << seed << '-' << timestamp
         << "\",\"utc_timestamp\":\"" << timestamp << "\",\"git_commit\":\"" << POKERZERO_GIT_COMMIT
         << "\",\"dirty_tree\":" << (POKERZERO_GIT_DIRTY ? "true" : "false")
         << ",\"build_type\":\"" << POKERZERO_BUILD_TYPE << "\",\"compiler\":\"" << POKERZERO_COMPILER
         << "\",\"platform\":\"" << POKERZERO_PLATFORM << "\",\"cpu\":\"" << cpu_name()
         << "\",\"rules_version\":\"hunl_100bb_v1\",\"game_config_hash_fnv1a64\":\""
         << fnv_file("configs/game.yaml") << "\",\"action_abstraction\":\""
         << (restricted_match ? "hunl_abs_v1" : "none") << "\",\"action_config_hash_fnv1a64\":\""
         << fnv_file("configs/action_abstraction.yaml") << "\",\"checkpoint_hash_fnv1a64\":"
         << (agent1.rfind("restricted:", 0) == 0 ? json_quote(fnv_file(agent1.substr(11))) : "null")
         << ",\"restricted_inference\":" << (restricted_match ? "\"min_policy_visits_10_v2\"" : "null")
         << ",\"cohort\":" << json_quote(cohort) << ",\"rng_derivation\":\"domain_separated_splitmix64_v2\""
         << ",\"stage\":" << (restricted_match ? 4 : 1) << ",\"agent1\":" << json_quote(agent1)
         << ",\"agent2\":" << json_quote(agent2) << ",\"hands\":" << result.hands
         << ",\"paired_blocks\":" << result.blocks << ",\"seed\":" << seed
         << ",\"net_chips\":[" << result.net_chips[0] << ',' << result.net_chips[1] << ']'
         << ",\"agent1_by_button_net_chips\":[" << result.agent0_by_button[0] << ',' << result.agent0_by_button[1] << ']'
         << ",\"mbb_per_hand\":" << result.mbb_per_hand << ",\"bb_per_100\":" << result.mbb_per_hand / 10.0
         << ",\"ci95_mbb_per_hand\":[" << result.ci95_low << ',' << result.ci95_high << ']'
         << ",\"ci_method\":\"paired_block_normal_se\",\"sample_unit\":\"duplicate_deal_block\""
         << ",\"action_budget_ms\":25,\"actions\":" << result.actions
         << ",\"timeouts\":" << result.timeouts << ",\"illegal_actions\":" << result.illegal_actions
         << ",\"fallbacks\":[" << result.fallbacks[0] << ',' << result.fallbacks[1] << ']'
         << ",\"latency_ms_p50_p95_p99\":[" << result.latency_p50_ms << ',' << result.latency_p95_ms << ',' << result.latency_p99_ms << ']'
         << ",\"seconds\":" << result.seconds << ",\"peak_rss_bytes\":" << peak_rss_bytes()
         << ",\"failure_reason\":null}\n";
    const std::string report = json.str();
    if (!output.empty()) {
      const std::filesystem::path path(output);
      if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
      std::ofstream file(path);
      if (!file) throw std::runtime_error("cannot open output file");
      file << report;
      if (!file) throw std::runtime_error("cannot write output file");
    }
    std::cout << report;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "pokerzero: " << error.what() << '\n';
    return 2;
  }
}
