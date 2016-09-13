
// diff4 code a file

#include <minizip/algorithm.hpp>
#include <cstdio>
#include <algorithm>
#include <fstream>

int main(int argc, const char **argv) {
  const char *in_file = nullptr;
  const char *out_file = nullptr;
  for (int i = 1; i != argc; ++i) {
    const char *arg = argv[i];
    if (arg[0] == '-') {
    } else if (!in_file) {
      in_file = arg;
    } else if (!out_file) {
      out_file = arg;
    } else {
      return 1;
    }
  }

  std::ifstream istr(in_file, std::ios::binary);
  std::ofstream ostr(out_file, std::ios::binary);
  int prev[4] = { 0 };
  int i = 0;
  while (istr) {
    int chr = istr.get();
    ostr.put(chr - prev[i & 3]);
    prev[i & 3] = chr;
    ++i;
  }
}


