/**
 * @file        rex/codegen/manifest.h
 * @brief       Manifest TOML parser for multi-binary projects
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rex::codegen {

struct ManifestModuleEntry {
  std::filesystem::path config;  ///< Absolute path to per-binary config TOML
  std::string guestPath;         ///< Canonicalized guest path; populated by Load
};

/**
 * Canonicalize a module guest path: device-stripped, slashes/case normalized,
 * with `<project>/assets/` stripped when a matching project name is given.
 */
std::string CanonicalizeModuleGuestPath(std::string_view path, std::string_view project_name = {});

/**
 * Parsed manifest TOML. Construct via Load(); treat as read-only after.
 */
struct ManifestConfig {
  std::string projectName;
  std::filesystem::path entrypointConfig;  ///< Absolute path to entrypoint config
  std::vector<ManifestModuleEntry> modules;
  std::filesystem::path manifestDir;  ///< Directory containing the manifest

  /**
   * Load a manifest TOML file. Returns nullopt on parse failure.
   */
  static std::optional<ManifestConfig> Load(const std::filesystem::path& path);

  /**
   * Detect whether a TOML file is a manifest (has [entrypoint] key) vs a
   * legacy single-binary config.
   */
  static bool IsManifest(const std::filesystem::path& path);
};

}  // namespace rex::codegen
