/**
 * @file        tests/unit/codegen/manifest_test.cpp
 * @brief       Unit tests for manifest TOML parser
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/manifest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  TempDir() : path(fs::temp_directory_path() / "manifest_test") { fs::create_directories(path); }
  ~TempDir() { fs::remove_all(path); }
  void writeFile(const std::string& name, const std::string& content) const {
    std::ofstream f(path / name);
    f << content;
  }
};

}  // namespace

TEST_CASE("Manifest: parse basic manifest", "[codegen][manifest]") {
  TempDir tmp;

  tmp.writeFile("manifest.toml", R"(
[project]
projectName = "mygame"

[entrypoint]
config = "mygame_default_xex.toml"

[[modules]]
config = "mygame_somelib_dll.toml"
guestPath = "bin/somelib.dll"
  )");

  auto result = rex::codegen::ManifestConfig::Load(tmp.path / "manifest.toml");
  REQUIRE(result.has_value());
  CHECK(result->projectName == "mygame");
  CHECK(result->entrypointConfig == tmp.path / "mygame_default_xex.toml");
  REQUIRE(result->modules.size() == 1u);
  CHECK(result->modules[0].config == tmp.path / "mygame_somelib_dll.toml");
  CHECK(result->modules[0].guestPath == "bin/somelib.dll");
}

TEST_CASE("Manifest: IsManifest detection", "[codegen][manifest]") {
  TempDir tmp;
  tmp.writeFile("manifest.toml",
                "[project]\nprojectName = \"test\"\n[entrypoint]\nconfig = \"x.toml\"");
  tmp.writeFile("config.toml", "project_name = \"test\"\nfile_path = \"test.xex\"");

  CHECK(rex::codegen::ManifestConfig::IsManifest(tmp.path / "manifest.toml"));
  CHECK_FALSE(rex::codegen::ManifestConfig::IsManifest(tmp.path / "config.toml"));
}

TEST_CASE("Manifest: multiple modules", "[codegen][manifest]") {
  TempDir tmp;
  tmp.writeFile("manifest.toml", R"(
[project]
projectName = "mygame"
[entrypoint]
config = "default_xex.toml"
[[modules]]
config = "lib_a.toml"
guestPath = "bin/lib_a.dll"
[[modules]]
config = "lib_b.toml"
guestPath = "data/lib_b.dll"
  )");

  auto result = rex::codegen::ManifestConfig::Load(tmp.path / "manifest.toml");
  REQUIRE(result.has_value());
  CHECK(result->modules.size() == 2u);
  CHECK(result->modules[0].guestPath == "bin/lib_a.dll");
  CHECK(result->modules[1].guestPath == "data/lib_b.dll");
}

TEST_CASE("Manifest: missing project name fails", "[codegen][manifest]") {
  TempDir tmp;
  tmp.writeFile("manifest.toml", "[entrypoint]\nconfig = \"x.toml\"");
  auto result = rex::codegen::ManifestConfig::Load(tmp.path / "manifest.toml");
  CHECK_FALSE(result.has_value());
}

TEST_CASE("CanonicalizeModuleGuestPath: device prefix", "[codegen][manifest][canonicalize]") {
  using rex::codegen::CanonicalizeModuleGuestPath;
  CHECK(CanonicalizeModuleGuestPath("game:\\bin\\foo.dll") == "bin/foo.dll");
  CHECK(CanonicalizeModuleGuestPath("game:/bin/foo.dll") == "bin/foo.dll");
  CHECK(CanonicalizeModuleGuestPath("d:\\bin\\foo.dll") == "bin/foo.dll");
}

TEST_CASE("CanonicalizeModuleGuestPath: case + slashes", "[codegen][canonicalize]") {
  using rex::codegen::CanonicalizeModuleGuestPath;
  CHECK(CanonicalizeModuleGuestPath("BIN\\Foo.DLL") == "bin/foo.dll");
  CHECK(CanonicalizeModuleGuestPath("bin\\sub\\Foo.dll") == "bin/sub/foo.dll");
}

TEST_CASE("CanonicalizeModuleGuestPath: leading slashes", "[codegen][canonicalize]") {
  using rex::codegen::CanonicalizeModuleGuestPath;
  CHECK(CanonicalizeModuleGuestPath("/bin/foo.dll") == "bin/foo.dll");
  CHECK(CanonicalizeModuleGuestPath("//bin/foo.dll") == "bin/foo.dll");
}

TEST_CASE("CanonicalizeModuleGuestPath: bare assets prefix preserved without project",
          "[codegen][canonicalize]") {
  using rex::codegen::CanonicalizeModuleGuestPath;
  CHECK(CanonicalizeModuleGuestPath("assets/bin/foo.dll") == "assets/bin/foo.dll");
  CHECK(CanonicalizeModuleGuestPath("Assets/Bin/Foo.DLL") == "assets/bin/foo.dll");
}

TEST_CASE("CanonicalizeModuleGuestPath: project assets prefix stripped",
          "[codegen][canonicalize]") {
  using rex::codegen::CanonicalizeModuleGuestPath;
  CHECK(CanonicalizeModuleGuestPath("mygame/assets/bin/foo.dll", "mygame") == "bin/foo.dll");
  CHECK(CanonicalizeModuleGuestPath("MyGame/Assets/Bin/Foo.DLL", "mygame") == "bin/foo.dll");
}

TEST_CASE("CanonicalizeModuleGuestPath: project mismatch keeps prefix", "[codegen][canonicalize]") {
  using rex::codegen::CanonicalizeModuleGuestPath;
  CHECK(CanonicalizeModuleGuestPath("othergame/assets/bin/foo.dll", "mygame") ==
        "othergame/assets/bin/foo.dll");
  // Bare assets/ is preserved even with a project name; only <project>/assets/
  // is project-aware-stripped.
  CHECK(CanonicalizeModuleGuestPath("assets/foo.dll", "mygame") == "assets/foo.dll");
}
