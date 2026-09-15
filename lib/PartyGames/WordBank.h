#pragma once

#include <cstdint>

namespace party {

// Word data for Codenames tiles and Undercover rounds.
//
// Stored as fixed-stride character arrays rather than arrays of pointers: a
// pointer table would add four bytes plus a relocation per entry in DRAM, while
// a 2D char array is one flash-resident blob indexed by multiplication.
namespace words {

inline constexpr uint8_t CODE_WORD_STRIDE = 10;  // 9 characters plus the terminator
uint16_t codeWordCount();
const char* codeWord(uint16_t index);

inline constexpr uint8_t PAIR_WORD_STRIDE = 12;
uint16_t pairCount();
const char* pairCivilian(uint16_t index);
const char* pairUndercover(uint16_t index);

}  // namespace words
}  // namespace party
