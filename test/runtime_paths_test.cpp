#include "../include/runtime_paths.h"

#include <gtest/gtest.h>

#include <filesystem>
namespace {

class RuntimePathsTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_directory = std::filesystem::temp_directory_path()
            / (std::string("engine-sim-runtime-paths-test-") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
    }

    void TearDown() override {
        std::filesystem::remove_all(m_directory);
    }

    std::filesystem::path m_directory;
};

TEST_F(RuntimePathsTest, UsesAdjacentAssetsWithoutConfiguration) {
    const RuntimePaths paths = RuntimePaths::discover(m_directory);

    EXPECT_EQ(paths.applicationDirectory, m_directory);
    EXPECT_EQ(paths.assetDirectory, m_directory / "../assets");
}

TEST_F(RuntimePathsTest, UsesDevelopmentAssetsOnlyWhenPackagedAssetsAreMissing) {
    const std::filesystem::path developmentAssets = m_directory / "source-assets";
    std::filesystem::create_directories(developmentAssets);

    const RuntimePaths paths = RuntimePaths::discover(m_directory, developmentAssets);

    EXPECT_EQ(paths.assetDirectory, developmentAssets);
}

TEST_F(RuntimePathsTest, FindsBundleAssetsFromConsoleAndAppEntrypoints) {
    const auto bundle = m_directory / "engine-sim.app";
    const auto assets = bundle / "Contents/Resources/assets";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(bundle / "Contents/MacOS");
    for (const auto &base : {bundle, bundle / "Contents/MacOS", bundle / "Contents/Resources"}) {
        const auto paths = RuntimePaths::discover(base, m_directory / "source-assets");
        EXPECT_TRUE(std::filesystem::equivalent(paths.assetDirectory, assets));
    }
}

} // namespace
