////////////////////////////////////////////////////////////////////////////////
//
// (C) Andy Thomason 2012-2016
//
// 


#ifndef MINIZIP_DEFLATE_ENCODER_INCLUDED
#define MINIZIP_DEFLATE_ENCODER_INCLUDED

#include <algorithm>

namespace minizip {
  class suffix_array {
  public:
    typedef uint32_t addr_type;

    struct sorter_t {
      addr_type group;
      addr_type next_group;
      addr_type addr;
      addr_type depth;
    };

    void assign(const uint8_t *src, const uint8_t *src_max) {
      size_t size = src_max - src;

      sorter.resize(size + 1);
      for (size_t i = 0; i != size+1; ++i) {
        sorter_t &s = sorter[i];
        s.group = i == size ? 0 : src[i];
        s.next_group = i == size ? 0 : 1;
        s.addr = (addr_type)i;
        s.depth = 0;
      }

      auto sort_by_address = [](const sorter_t &a, const sorter_t &b) { return a.addr < b.addr; };

      auto sort_by_group = [](const sorter_t &a, const sorter_t &b) {
        return a.group < b.group;
      };

      auto sort_by_group_and_next = [](const sorter_t &a, const sorter_t &b) {
        return a.group < b.group + (a.next_group < b.next_group);
      };

      auto debug_dump = [this, src](const char *msg, size_t h) {
        printf("\n%s h=%d\n", msg, (int)h);
        for (size_t i = 0; i != sorter.size(); ++i) {
        //for (size_t i = 0; i != 4; ++i) {
          auto &s = sorter[i];
          size_t acp = 0;
          if (i != 0) {
            auto p = src + s.addr;
            auto q = src + sorter[i-1].addr;
            while (*p && *p == *q) { ++p; ++q; ++acp; }
          }
          printf("%4d %12d %4d %4d %4d %4d %s\n", (int)i, (int)s.group, (int)s.next_group, (int)s.addr, (int)s.depth, (int)acp, (char*)src + s.addr);
        }
      };

      bool debug = true;
      for (size_t h = 1; ; h *= 2) {
        // sort by group    
        std::sort(sorter.begin(), sorter.end(), sort_by_group_and_next);
    
        int finished = 1;
        for (size_t i = 0; i != size+1;) {
          auto si = sorter[i];
          size_t j = i+1;
          sorter[i].group = (addr_type)i;
          int done = 0;
          for (; j != size+1 && sorter[j].group == si.group && sorter[j].next_group == si.next_group; ++j) {
            sorter[j].group = (addr_type)i;
            sorter[j].depth++;
            done = 1;
          }
          sorter[i].depth += done;
          finished &= !done;
          i = j;
        }

        if (debug) debug_dump("built group numbers", h);

        if (finished) break;
    
        // invert the sort, restoring the original order of the sequence
        std::sort(sorter.begin(), sorter.end(), sort_by_address);

        // avoid random access to the string by using a sort.                
        for (size_t i = 0; i != size+1; ++i) {
          sorter_t &s = sorter[i];
          s.next_group = i < size - h ? sorter[i+h].group : 0;
        }
    
        //if (debug) debug_dump("set next_group", h);
      }

      //std::sort(sorter.begin(), sorter.end(), sort_by_address);
      debug_dump("final", 1);
    }

    sorter_t &operator[](size_t i) { return sorter[i]; }
    size_t size() const { return sorter.size(); }
  private:

    std::vector<sorter_t> sorter;
  };

  class deflate_encoder {
  public:
    deflate_encoder() {
    }

    bool encode(uint8_t *dest, uint8_t *dest_max, const uint8_t *src, const uint8_t *src_max) const {
      size_t block_size = 0x100000;
      suffix_array sa;

      while (src != src_max) {
        size_t size = std::min((size_t)(src_max - src), block_size);
        sa.assign(src, src + size);

        for (size_t i = 0; i != size; ++i) {
          suffix_array::sorter_t &s = sa[i];
        }
        src += size;
      }
      return false;
    }
  private:
  };

}

#endif
