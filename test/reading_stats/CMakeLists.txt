add_executable(ReadingStatsStoreTest
  ReadingStatsStoreTest.cpp
  ${REPO_ROOT}/src/ReadingStatsStore.cpp
)

target_include_directories(ReadingStatsStoreTest PRIVATE
  ../library_builder/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(ReadingStatsStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingStatsStoreTest)
