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
    };

    void assign(const uint8_t *src, const uint8_t *src_max) {
      size_t size = src_max - src;

      sorter.resize(size + 1);
      for (size_t i = 0; i != size; ++i) {
        sorter_t &s = sorter[i];
        s.group = src[i];
        s.next_group = 1;
        s.addr = (addr_type)i;
      }
      {
        sorter_t &s = sorter[size];
        s.group = 0;
        s.next_group = 0;
        s.addr = (addr_type)size;
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
          auto &s = sorter[i];
          printf("%4d %12d %4d %4d %s\n", (int)i, (int)s.group, (int)s.next_group, (int)s.addr, (char*)src + s.addr);
        }
      };

      bool debug = true;
      for (size_t h = 1; ; h *= 2) {
        // sort by group    
        std::sort(sorter.begin(), sorter.end(), sort_by_group_and_next);
    
        if (debug) debug_dump("sorted group and next_group", h);

        // build the group numbers
        auto t = sorter[0];
        addr_type group = 0;
        sorter[0].group = group++;
        int finished = 1;
        for (size_t i = 1; i != size+1; ++i) {
          auto &s = sorter[i];
          int different = (t.group != s.group) | (t.next_group != s.next_group);
          finished &= different;
          group = different ? (addr_type)i : group;
          t = s;
          s.group = group;
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
    
        if (debug) debug_dump("set next_group", h);
      }
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
