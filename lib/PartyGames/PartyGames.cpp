#include "PartyGames.h"

#include <cstdio>
#include <cstring>

namespace party {

uint32_t Rng::next() {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

uint32_t Rng::below(const uint32_t bound) { return bound ? next() % bound : 0; }

void Rng::shuffle(uint8_t* values, const uint8_t count) {
  for (uint8_t i = count; i > 1; i--) {
    const uint8_t j = static_cast<uint8_t>(below(i));
    const uint8_t tmp = values[i - 1];
    values[i - 1] = values[j];
    values[j] = tmp;
  }
}

void JsonBuf::ch(const char c) {
  if (length + 1 >= cap) {
    overflow = true;
    return;
  }
  buf[length++] = c;
  buf[length] = '\0';
}

void JsonBuf::raw(const char* text) {
  for (const char* p = text; *p; p++) ch(*p);
}

void JsonBuf::str(const char* text) {
  ch('"');
  for (const char* p = text; *p; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '"' || c == '\\') {
      ch('\\');
      ch(static_cast<char>(c));
    } else if (c < 0x20) {
      char esc[7];
      snprintf(esc, sizeof(esc), "\\u%04x", c);
      raw(esc);
    } else {
      ch(static_cast<char>(c));
    }
  }
  ch('"');
}

void JsonBuf::num(const long value) {
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%ld", value);
  raw(tmp);
}

void JsonBuf::boolean(const bool value) { raw(value ? "true" : "false"); }

void JsonBuf::key(const char* name) {
  str(name);
  ch(':');
}

void JsonBuf::keyStr(const char* name, const char* text) {
  key(name);
  str(text);
}

void JsonBuf::keyNum(const char* name, const long value) {
  key(name);
  num(value);
}

void JsonBuf::keyBool(const char* name, const bool value) {
  key(name);
  boolean(value);
}

uint8_t sanitizeName(const char* in, char* out, const uint8_t outCapacity) {
  if (!out || outCapacity == 0) return 0;
  uint8_t written = 0;
  if (in) {
    for (const char* p = in; *p && written + 1 < outCapacity; p++) {
      const unsigned char c = static_cast<unsigned char>(*p);
      // Printable ASCII only: names reach the phone page, the shared screen and
      // the JSON, and the built-in UI fonts are Latin anyway.
      if (c < 0x20 || c > 0x7E) continue;
      if (c == '"' || c == '\\' || c == '<' || c == '>' || c == '&') continue;
      out[written++] = static_cast<char>(c);
    }
  }
  // Trim trailing spaces so " " does not become a nameless player.
  while (written > 0 && out[written - 1] == ' ') written--;
  out[written] = '\0';
  return written;
}

}  // namespace party
