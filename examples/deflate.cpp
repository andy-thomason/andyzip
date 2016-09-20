
#include <cstdint>
#include <cstdio>
#include <vector>
#include <iostream>


#include <minizip/deflate_encoder.hpp>

int main() {
  minizip::deflate_encoder enc;

  static const uint8_t text[] = "to be or not to be, that is the question! 1234 1234 1234";

  std::vector<uint8_t> buffer(0x10000);
  enc.encode(buffer.data(), buffer.data() + buffer.size(), text, text + sizeof(text) - 1);
}
