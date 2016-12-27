////////////////////////////////////////////////////////////////////////////////
//
// (C) Andy Thomason 2012-2016
//
// 


#ifndef _ANDYZIP_BROTLI_DECODER_HPP_
#define _ANDYZIP_BROTLI_DECODER_HPP_

#include <andyzip/huffman_table.hpp>

#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <algorithm>

namespace andyzip {
  struct brotli_decoder_state {
    enum class error_code {
      ok = 0,
      need_more_input = 1,
      syntax_error = 2,
      end = 3,
      huffman_length_error = 4,
    };

    static const int max_types = 256;
    static const bool dump_rfc = false;
    static const bool dump_reference = true;
    FILE *log_file = nullptr;
    const char *src = nullptr;
    std::uint32_t bitptr = 0;
    std::uint32_t bitptr_max = 0;
    char *dest = 0;
    char *dest_max = 0;
    error_code error;
    int max_back_ref = 0;
    int num_types[3];
    int block_type[3];
    int block_len[3];
    uint8_t context_mode[max_types];
    uint8_t literal_context_map[256]; // todo: what is the max size?
    uint8_t distance_context_map[256];

    /// debug function for dumping bit fields
    void dump_bits(unsigned value, unsigned bits, const char *name) {
      char tmp[64];
      if (bits < sizeof(tmp)-1) {
        for (unsigned i = 0; i != bits; ++i) tmp[i] = ( ( value >> (bits-1-i) ) & 1 ) + '0';
        tmp[bits] = 0;
        fprintf(log_file, "[%s] %s\n", tmp, name);
      }
    }

    int read(int bits, const char *name) {
      int value = peek(bits);
      if (dump_reference) {
        fprintf(log_file, "[BrotliReadBits]  %d %d %d val: %6x\n", (bitptr_max - bitptr - bits)/8, 24+(bitptr&7), bits, value);
      }
      drop(bits, name);
      return value;
    }

    int peek(int bits) {
      if (bitptr + bits > bitptr_max) { error = error_code::need_more_input; return 0; }
      auto i = bitptr >> 3, j = bitptr & 7;
      int value = (int)( (unsigned&)src[i] >> j ) & ( (1u << bits) - 1 );
      return value;
    }

    void drop(int bits, const char *name) {
      if (dump_rfc) {
        auto i = bitptr >> 3, j = bitptr & 7;
        int value = (int)( (unsigned&)src[i] >> j ) & ( (1u << bits) - 1 );
        dump_bits(value, bits, name);
      }
      bitptr += bits;
    }
  };

  class brotli_decoder {
    static const bool debug = true;
    static const int window_gap = 16;
    static const int literal_context_bits = 6;
    static const int distance_context_bits = 2;

    static const int num_distance_short_codes = 16;
    typedef brotli_decoder_state::error_code error_code;

    /// debug function for dumping bit fields
    static void dump_bits(unsigned value, unsigned bits, const char *name) {
      char tmp[64];
      if (bits < sizeof(tmp)-1) {
        for (unsigned i = 0; i != bits; ++i) tmp[i] = ( ( value >> (bits-1-i) ) & 1 ) + '0';
        tmp[bits] = 0;
        printf("[%s] %s\n", tmp, name);
      }
    }

    /// peek a fixed number of little-endian bits from the bitstream
    /// note: this will have to be fixed on PPC and other big-endian devices
    static unsigned peek(const char *src, std::uint32_t bitptr, size_t bits, const char *name) {
      std::uint32_t i = bitptr >> 3, j = bitptr & 7;
      unsigned value = ( (unsigned&)src[i] >> j ) & ( (1u << bits) - 1 );
      if (debug && name) dump_bits(value, bits, name);
      return value;
    }

    static unsigned read_window_size(brotli_decoder_state &s) {
      auto w0 = s.read(1, "WBITS0");
      if (s.error != error_code::ok) return 0; 
      if (w0 == 0) {
        return 16;
      }

      auto w13 = s.read(3, "WBITS1-3");
      if (s.error != error_code::ok) return 0; 
      if (w13 != 0) {
        return w13 + 17;
      }

      auto w47 = s.read(4, "WBITS4-7");
      if (s.error != error_code::ok) return 0; 

      return w47 ? w47 + 10 - 2 : 17;
    }

    // read a value from 1 to 256
    static int read_256(brotli_decoder_state &s, const char *label) {
      int nlt0 = s.read(1, label);
      if (s.error != error_code::ok) return 0; 

      if (!nlt0) return 1;

      int nlt14 = s.read(3, label);
      if (s.error != error_code::ok) return 0; 

      if (!nlt14) return 2;

      int nlt5x = s.read(nlt14, label);
      if (s.error != error_code::ok) return 0; 

      return (1 << nlt14) + nlt5x;
    }

    // From reference inplementation.
    static int log2_floor(int x) {
      int result = 0;
      while (x) {
        x >>= 1;
        ++result;
      }
      return result;
    }

    // From 7.3.  Encoding of the Context Map
    static void inverse_move_to_front(uint8_t values[], int num_values) {
      uint8_t mtf[256];
      for (int i = 0; i < 256; ++i) {
        mtf[i] = (uint8_t)i;
      }
      for (int i = 0; i < num_values; ++i) {
        uint8_t index = values[i];
        uint8_t value = mtf[index];
        values[i] = value;
        for (; index; --index) {
            mtf[index] = mtf[index - 1];
        }
        mtf[0] = value;
      }
    }

    template <class Table>
    static void read_huffman_code(brotli_decoder_state &s, Table &table, int alphabet_size) {
      int code_type = s.read(2, "HUFFTYPE");
      if (s.error != error_code::ok) return;
      alphabet_size &= 1023;
      fprintf(s.log_file, "[ReadHuffmanCode] s->sub_loop_counter = %d\n", code_type);
      if (code_type == 1) {
        // 3.4.  Simple Prefix Codes
        int num_symbols = s.read(2, "NSYM") + 1;
        int alphabet_bits = log2_floor(alphabet_size - 1);
        uint16_t symbols[Table::max_codes];
        for (int i = 0; i != num_symbols; ++i) {
          symbols[i] = (uint16_t)s.read(alphabet_bits, "symbol");
          fprintf(s.log_file, "[ReadSimpleHuffmanSymbols] s->symbols_lists_array[i] = %d\n", symbols[i]);
        }
        fprintf(s.log_file, "[ReadHuffmanCode] s->symbol = %d\n", num_symbols-1);
        static const uint8_t simple_lengths[][4] = {
          {0},
          {1, 1},
          {1, 2, 2},
          {2, 2, 2, 2},
          {1, 2, 3, 3},
        };
        int tree_select = num_symbols == 4 ? s.read(1, "tree-select") : 0;
        table.init(simple_lengths[num_symbols - 1 + tree_select], symbols, num_symbols);
      } else {
        // 3.5.  Complex Prefix Codes
        andyzip::huffman_table<18, false> complex_table;
        {
          uint8_t lengths[18] = {0};
          int space = 0;
          int num_codes = 0;
          for (int i = code_type; i != 18; ++i) {
            int bits = s.peek(4);
            if (s.error != error_code::ok) return;

            static const uint8_t kCodeLengthCodeOrder[18] = {
              1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15,
            };

            // Static prefix code for the complex code length code lengths.
            static const uint8_t kCodeLengthPrefixLength[16] = {
              2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 4,
            };

            static const uint8_t kCodeLengthPrefixValue[16] = {
              0, 4, 3, 2, 0, 4, 3, 1, 0, 4, 3, 2, 0, 4, 3, 5,
            };

            s.drop(kCodeLengthPrefixLength[bits], "LENGTH");
            uint8_t length = kCodeLengthPrefixValue[bits];
            lengths[kCodeLengthCodeOrder[i]] = length;
            fprintf(s.log_file, "[ReadCodeLengthCodeLengths] s->code_length_code_lengths[%d] = %d\n", kCodeLengthCodeOrder[i], lengths[kCodeLengthCodeOrder[i]]);
            if (length) {
              ++num_codes;
              space += 32 >> length;
              if (space >= 32) break;
            }
          }
          if (num_codes != 1 && space != 32) {
            s.error = error_code::huffman_length_error;
            return;
          }

          static const uint16_t symbols[18] = {
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17
          };
          complex_table.init(lengths, symbols, 18);
        }

        {
          int space = 0;
          uint8_t lengths[Table::max_codes];
          uint8_t nzcl = 8;
          int prev_code_len = 8;
          int repeat = 0;
          int repeat_code_len = 0;
          for(int i = 0; i < alphabet_size;) {
            auto code = complex_table.decode(s.peek(16));
            s.drop(code.first, "CPLX");
            int code_len = code.second;
            if (code_len < 16) {
              lengths[i++] = nzcl = (uint8_t)code.second;
              fprintf(s.log_file, "[ReadHuffmanCode] code_length[%d] = %d\n", i, code.second);
              repeat = 2;
            } else {
              int extra_bits = code_len == 16 ? 2 : 3;
              int new_len = code_len == 16 ? prev_code_len : 0;
              int repeat_delta = s.peek(extra_bits);
              s.drop(extra_bits, "EXTRA");
              if (repeat_code_len != new_len) {
                repeat = 0;
                repeat_code_len = new_len;
              }
              int old_repeat = repeat;
              if (repeat > 0) {
                repeat -= 2;
                repeat <<= extra_bits;
              }
              repeat += repeat_delta + 3U;
              repeat_delta = repeat - old_repeat;

              fprintf(s.log_file, "[ReadHuffmanCode] code_length[%d..%d] = %d\n", i, i + repeat_delta - 1, new_len);
              for (int j = 0; j != repeat_delta; ++j) {
                lengths[i++] = new_len;
              }
            }
            prev_code_len = code.second;
          }
        }
      }
    }

    static void read_context_map(brotli_decoder_state &s, uint8_t *context_map, int context_map_size, int num_trees) {
      fprintf(s.log_file, "[DecodeContextMap] context_map_size = %d\n", context_map_size);
      fprintf(s.log_file, "[DecodeContextMap] *num_htrees = %d\n", num_trees);

      // if NTREESL >= 2
      if (num_trees >= 2) {
        //  read literal context map, CMAPL[]
        int bits = s.peek(5);
        if (s.error != error_code::ok) return; 
        int rlemax = (bits & 1) ? (bits >> 1) + 1 : 0;
        s.drop((bits & 1) ? 5 : 1, "RLEMAX");
        fprintf(s.log_file, "[DecodeContextMap] s->max_run_length_prefix = %d\n", rlemax);
        andyzip::huffman_table<256, debug> table;
        read_huffman_code(s, table, num_trees + rlemax);
        for (int i = 0; i != context_map_size; ++i) {
          unsigned code = s.peek(16);
          auto length_value = table.decode(code);
          s.drop(length_value.first, "CODE");
          fprintf(s.log_file, "[DecodeContextMap] code = %d\n", length_value.second);
          context_map[i] = (uint8_t)length_value.second;
        }

        int imtf = s.read(1, "IMTF");
        if (s.error != error_code::ok) return; 
        if (imtf) {
          inverse_move_to_front(context_map, context_map_size);
        }
      } else {
        //  fill CMAPL[] with zeros
        std::fill(context_map, context_map + context_map_size, 0);
      }
    }

  public:
    brotli_decoder() {
    }

    // decode some input bits and return -1 if we need more input.
    brotli_decoder_state::error_code decode(brotli_decoder_state &s) {
      typedef brotli_decoder_state::error_code error_code;
      // https://tools.ietf.org/html/rfc7932
      s.error = error_code::ok;

      // read window size
      unsigned lg_window_size = read_window_size(s);
      if (s.error != error_code::ok) return s.error;
      s.max_back_ref = (1 << lg_window_size) - window_gap;
      fprintf(s.log_file, "[BrotliDecoderDecompressStream] s->window_bits = %d\n", lg_window_size);
      fprintf(s.log_file, "[BrotliDecoderDecompressStream] s->pos = %d\n", 0);

      //  do
      {
          // read ISLAST bit
          int is_last = s.read(1, "ISLAST");
          if (s.error != error_code::ok) return s.error;

          // if ISLAST
          if (is_last) {
            //  read ISLASTEMPTY bit
            int is_last_empty = s.read(1, "ISLASTEMPTY");
            if (s.error != error_code::ok) return s.error;

            //  if ISLASTEMPTY break from loop
            if (is_last_empty) { s.error = error_code::end; return s.error; }
          }

          // read MNIBBLES
          int nibbles_code = s.read(2, "MNIBBLES");
          if (s.error != error_code::ok) return s.error;

          // if MNIBBLES is zero
          int mlen = 0;
          if (nibbles_code == 3) {
            //  verify reserved bit is zero
            //  read MSKIPLEN
            //  skip any bits up to the next byte boundary
            //  skip MSKIPLEN bytes
            //  continue to the next meta-block
            s.error = error_code::syntax_error;
            return s.error;
          } else {
            //  read MLEN
            for (int i = 0; i != nibbles_code + 4; ++i) {
              int val = s.read(4, "MLEN");
              if (s.error != error_code::ok) return s.error;
              mlen |= val << (i*4);
            }
            ++mlen;
          }

          // if not ISLAST
          if (!is_last) {
            //  read ISUNCOMPRESSED bit
            int is_uncompressed = s.read(1, "ISUNCOMPRESSED");
            //  if ISUNCOMPRESSED
            if (is_uncompressed) {
              // skip any bits up to the next byte boundary
              // copy MLEN bytes of compressed data as literals
              // continue to the next meta-block
              s.error = error_code::syntax_error;
              return s.error;
            }
          }

          fprintf(s.log_file, "[BrotliDecoderDecompressStream] s->is_last_metablock = %d\n", is_last);
          fprintf(s.log_file, "[BrotliDecoderDecompressStream] s->meta_block_remaining_len = %d\n", mlen);
          fprintf(s.log_file, "[BrotliDecoderDecompressStream] s->is_metadata = %d\n", 0);
          fprintf(s.log_file, "[BrotliDecoderDecompressStream] s->is_uncompressed = %d\n", 0);

          // loop for each three block categories (i = L, I, D)
          for (int i = 0; i != 3; ++i) {
            //  read NBLTYPESi
            int nbltypesi = read_256(s, "NBLTYPESi");
            if (s.error != error_code::ok) return s.error;
            fprintf(s.log_file, "[BrotliDecoderDecompressStream] s->num_block_types[s->loop_counter] = %d\n", nbltypesi);

            s.num_types[i] = nbltypesi;

            //  if NBLTYPESi >= 2
            if (nbltypesi >= 2) {
              // read prefix code for block types, HTREE_BTYPE_i
              // read prefix code for block counts, HTREE_BLEN_i
              // read block count, BLEN_i
              // set block type, BTYPE_i to 0
              // initialize second-to-last and last block types to 0 and 1
              s.block_type[i] = 0;
              s.block_len[i] = 16777216;
              s.error = error_code::syntax_error;
              return s.error;
            } else {
              // set block type, BTYPE_i to 0
              s.block_type[i] = 0;
              // set block count, BLEN_i to 16777216
              s.block_len[i] = 16777216;
            }
          }

          // read NPOSTFIX and NDIRECT
          int pbits = s.read(6, "NPOSTFIX");
          if (s.error != error_code::ok) return s.error;
          int num_direct_distance_codes = num_distance_short_codes + ((pbits >> 2) << (pbits & 3));
          if (s.error != error_code::ok) return s.error;
          fprintf(s.log_file, "[BrotliDecoderDecompressStream] s->num_direct_distance_codes = %d\n", num_direct_distance_codes);
          fprintf(s.log_file, "[BrotliDecoderDecompressStream] s->distance_postfix_bits = %d\n", pbits & 3);

          // read array of literal context modes, CMODE[]
          for (int i = 0; i != s.num_types[0]; ++i) {
            int ctxt = s.read(2, "CTXT") * 2;
            if (s.error != error_code::ok) return s.error;
            s.context_mode[i & (brotli_decoder_state::max_types-1)] = ctxt;
            fprintf(s.log_file, "[ReadContextModes] s->context_modes[%d] = %d\n", i, s.context_mode[i]);
          }

          // read NTREESL
          int num_literal_htrees = read_256(s, "NTREESL");
          if (s.error != error_code::ok) return s.error;
          read_context_map(s, s.literal_context_map, s.num_types[0] << literal_context_bits, num_literal_htrees);
          if (s.error != error_code::ok) return s.error;

          // read NTREESD
          int num_distance_htrees = read_256(s, "NTREESD");
          if (s.error != error_code::ok) return s.error;
          read_context_map(s, s.distance_context_map, s.num_types[0] << distance_context_bits, num_distance_htrees);
          if (s.error != error_code::ok) return s.error;

          // read array of literal prefix codes, HTREEL[]
          std::vector<andyzip::huffman_table<256, debug>> literal_tables(num_literal_htrees);
          for (int i = 0; i != num_literal_htrees; ++i) {
            read_huffman_code(s, literal_tables[i], 256);
          }

          // read array of insert-and-copy length prefix codes, HTREEI[]
          andyzip::huffman_table<256, debug> iandc_table;
          read_huffman_code(s, iandc_table, 256);

          // read array of distance prefix codes, HTREED[]
          andyzip::huffman_table<256, debug> distance_table;
          read_huffman_code(s, distance_table, num_distance_htrees);

          // do
            //  if BLEN_I is zero
                // read block type using HTREE_BTYPE_I and set BTYPE_I
                  //  save previous block type
                // read block count using HTREE_BLEN_I and set BLEN_I
            //  decrement BLEN_I
            //  read insert-and-copy length symbol using HTREEI[BTYPE_I]
            //  compute insert length, ILEN, and copy length, CLEN
            //  loop for ILEN
                // if BLEN_L is zero
                  //  read block type using HTREE_BTYPE_L and set BTYPE_L
                    //   save previous block type
                  //  read block count using HTREE_BLEN_L and set BLEN_L
                // decrement BLEN_L
                // look up context mode CMODE[BTYPE_L]
                // compute context ID, CIDL from last two uncompressed bytes
                // read literal using HTREEL[CMAPL[64*BTYPE_L + CIDL]]
                // write literal to uncompressed stream
            //  if number of uncompressed bytes produced in the loop for
                // this meta-block is MLEN, then break from loop (in this
                // case the copy length is ignored and can have any value)
            // if distance code is implicit zero from insert-and-copy code
                // set backward distance to the last distance
            //  else
                // if BLEN_D is zero
                  //  read block type using HTREE_BTYPE_D and set BTYPE_D
                    //   save previous block type
                  //  read block count using HTREE_BLEN_D and set BLEN_D
                // decrement BLEN_D
                // compute context ID, CIDD from CLEN
                // read distance code using HTREED[CMAPD[4*BTYPE_D + CIDD]]
                // compute distance by distance short code substitution
                // if distance code is not zero,
                  //  and distance is not a static dictionary reference,
                  //  push distance to the ring buffer of last distances
            //  if distance is less than the max allowed distance plus one
                // move backwards distance bytes in the uncompressed data,
                // and copy CLEN bytes from this position to
                // the uncompressed stream
            //  else
                // look up the static dictionary word, transform the word as
                // directed, and copy the result to the uncompressed stream
          // while number of uncompressed bytes for this meta-block < MLEN
      } //  while not ISLAST

      return s.error;
    }

  };

}

#endif
