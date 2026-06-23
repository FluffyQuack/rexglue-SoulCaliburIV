// sciv - ReXGlue Recompiled Project
//
// See sciv_settings.h for the contract.

#include "sciv_settings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <rex/filesystem.h>
#include <rex/logging/api.h>
#include <rex/logging/macros.h>

namespace sciv {

namespace {

Config g_config;
std::filesystem::path g_config_path;
bool g_loaded = false;

std::string ToLower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string_view Trim(std::string_view s) {
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

// Strips an inline/line comment introduced by `//`, `;` or `#`.
std::string_view StripComment(std::string_view s) {
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == ';' || s[i] == '#') return s.substr(0, i);
    if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') return s.substr(0, i);
  }
  return s;
}

// Lowercased "section.key" -> raw value string, for every pair found.
using KeyMap = std::map<std::string, std::string>;

KeyMap Parse(std::istream& in) {
  KeyMap map;
  std::string section;
  std::string line;
  while (std::getline(in, line)) {
    std::string_view sv = Trim(StripComment(line));
    if (sv.empty()) continue;
    if (sv.front() == '[' && sv.back() == ']') {
      section = ToLower(Trim(sv.substr(1, sv.size() - 2)));
      continue;
    }
    auto eq = sv.find('=');
    if (eq == std::string_view::npos) continue;  // not a key=value line; skip
    std::string key = ToLower(Trim(sv.substr(0, eq)));
    std::string value(Trim(sv.substr(eq + 1)));
    if (key.empty()) continue;
    map[section + "." + key] = std::move(value);
  }
  return map;
}

std::optional<bool> AsBool(const std::string& v) {
  std::string s = ToLower(v);
  if (s == "true" || s == "1" || s == "yes" || s == "on") return true;
  if (s == "false" || s == "0" || s == "no" || s == "off") return false;
  return std::nullopt;
}

// --- Serialization (the inverse of Parse) -------------------------------------
// One source of truth for the file we write on first run AND re-write on quit.
// Substituting live `Config` values means a re-save preserves whatever the user
// set while filling in any keys (and refreshing any comments) that were missing.

const char* BoolStr(bool v) { return v ? "True" : "False"; }

// Builds the full commented INI text from the given config. Mirrors the format
// Parse() accepts; kept self-documenting so a fresh install explains itself.
std::string BuildIniText(const Config& c) {
  std::ostringstream os;
  os << "; sciv configuration. Delete this file to regenerate defaults.\n"
        "; Comments may start with // ; or #. Missing keys fall back to defaults.\n"
        "\n"
        "[System]\n"
        "\n"
        "// If true, keep all writable data (user data, shader cache, runtime config)\n"
        "// in the working directory so the install stays self-contained / portable.\n"
        "// If false, use the platform's per-user folders instead.\n"
     << "Portable = " << BoolStr(c.portable) << "\n"
        "\n"
        "[Game]\n"
        "\n"
        "// If true, skip the five boot logos (BANDAI NAMCO / Havok / CRI / SOUL)\n"
        "// that play before the title screen and go straight to the title.\n"
     << "SkipLogos = " << BoolStr(c.skip_logos) << "\n";
  return os.str();
}

}  // namespace

std::filesystem::path ConfigPath() {
  if (g_config_path.empty()) {
    // The INI lives in the working directory (`./`), not the executable folder:
    // it is the bootstrap file carrying [System] Portable, which then decides
    // where the rest of the writable data goes. current_path() is absolute, so
    // the resolved path is stable even if the CWD changes later.
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (ec) cwd = rex::filesystem::GetExecutableFolder();  // unusual; fall back
    g_config_path = cwd / "sciv.ini";
  }
  return g_config_path;
}

const Config& config() { return g_config; }

const Config& LoadConfig() {
  if (g_loaded) return g_config;
  g_loaded = true;

  const std::filesystem::path path = ConfigPath();

  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    // First run: write a commented default and keep the compiled-in defaults.
    std::ofstream out(path, std::ios::binary);
    if (out) {
      out << BuildIniText(g_config);
      REXLOG_INFO("config: no INI found, wrote defaults to {}", path.string());
    } else {
      REXLOG_WARN("config: no INI found and could not write {} (using defaults)", path.string());
    }
    return g_config;
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    REXLOG_WARN("config: could not open {} (using defaults)", path.string());
    return g_config;
  }

  KeyMap map = Parse(in);

  if (auto it = map.find("system.portable"); it != map.end()) {
    if (auto b = AsBool(it->second)) {
      g_config.portable = *b;
    } else {
      REXLOG_WARN("config: [System] Portable = '{}' is not a bool; using default ({})", it->second,
                  g_config.portable);
    }
  }

  if (auto it = map.find("game.skiplogos"); it != map.end()) {
    if (auto b = AsBool(it->second)) {
      g_config.skip_logos = *b;
    } else {
      REXLOG_WARN("config: [Game] SkipLogos = '{}' is not a bool; using default ({})", it->second,
                  g_config.skip_logos);
    }
  }

  REXLOG_INFO("config: loaded {} -> [System] Portable = {}, [Game] SkipLogos = {}", path.string(),
              g_config.portable ? "True" : "False", g_config.skip_logos ? "True" : "False");
  return g_config;
}

void SaveConfig() {
  const std::filesystem::path path = ConfigPath();

  // Re-write the file from the live config so any keys missing from an older INI
  // get populated (and comments refreshed) while the user's values are kept.
  // Written through a temp file + rename so a crash mid-write can't truncate a
  // good INI to nothing.
  std::filesystem::path tmp = path;
  tmp += ".tmp";

  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      REXLOG_WARN("config: could not write {} (config not re-saved)", tmp.string());
      return;
    }
    out << BuildIniText(g_config);
    if (!out) {
      REXLOG_WARN("config: error writing {} (config not re-saved)", tmp.string());
      return;
    }
  }

  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    REXLOG_WARN("config: could not replace {} ({}); config not re-saved", path.string(),
                ec.message());
    std::filesystem::remove(tmp, ec);
    return;
  }

  REXLOG_INFO("config: re-saved {} (populated any missing settings)", path.string());
}

}  // namespace sciv
