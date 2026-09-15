add_executable(PartyGamesTest
  PartyGamesTest.cpp
  ${REPO_ROOT}/lib/PartyGames/PartyGames.cpp
  ${REPO_ROOT}/lib/PartyGames/WordBank.cpp
  ${REPO_ROOT}/lib/PartyGames/Undercover.cpp
  ${REPO_ROOT}/lib/PartyGames/Codenames.cpp
  ${REPO_ROOT}/lib/PartyGames/Battleship.cpp
  ${REPO_ROOT}/lib/PartyGames/Ludo.cpp
  ${REPO_ROOT}/lib/PartyGames/GameSession.cpp
)

target_include_directories(PartyGamesTest PRIVATE
  ${REPO_ROOT}/lib/PartyGames
)

target_link_libraries(PartyGamesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PartyGamesTest)
