/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "refgraph.h"
#include "mozilla/RefgraphInstrumentation.h"

#define refgraph_uninstrumented_free gMallocFuncsTable->free
#define refgraph_uninstrumented_malloc gMallocFuncsTable->malloc
#include "mozilla/RefgraphSTLAllocatorBypassingInstrumentation.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>

#include <vector>
#include <algorithm>

#ifdef MOZ_HAVE_RTTI
#include <cxxabi.h>
#endif

#undef malloc_good_size

namespace refgraph {

static const size_t sBufSize = 65536;
static const size_t sMaxPrintSize = 4096;
static const size_t sBufFlushThreshold = sBufSize - sMaxPrintSize;

class Scanner
  : AutoLock
{
  struct block_t {
    uintptr_t address;
    size_t size;

    bool operator< (const block_t& other) const {
      return address < other.address;
    }
  };

  typedef std::vector<block_t, stl_allocator_bypassing_instrumentation<block_t> >
          blocks_vector_t;

  struct ref_t
  {
    blocks_vector_t::const_iterator target;
    size_t offset;
    uint32_t flags;
    const char* reftypename;
    const char* refname;

    bool operator<(const ref_t& other) const {
      return target->address + offset < other.target->address + other.offset;
    }
  };

  typedef std::vector<ref_t, stl_allocator_bypassing_instrumentation<ref_t> >
          refs_vector_t;

public:
  Scanner(int fd);
  ~Scanner();

  void Scan();

private:

  void ScanBlock(blocks_vector_t::const_iterator block);

  blocks_vector_t::const_iterator
  FindInHeap(uintptr_t address) const;

  blocks_vector_t::const_iterator
  FindInHeapNontrivialCase(uintptr_t address) const;

  void Print(const char* str);
  void Print(const void* ptr);
  void Print(uintptr_t n);

  template<typename A, typename B>
  void Print(A a, B b)
  { Print(a); Print(b); }
  template<typename A, typename B, typename C>
  void Print(A a, B b, C c)
  { Print(a); Print(b, c); }
  template<typename A, typename B, typename C, typename D>
  void Print(A a, B b, C c, D d)
  { Print(a); Print(b, c, d);}
  template<typename A, typename B, typename C, typename D, typename E>
  void Print(A a, B b, C c, D d, E e)
  { Print(a); Print(b, c, d, e); }

  void FlushPrint();
  void FlushPrintIfAboveThreshold();

  char mPrintBuf[sBufSize];
  size_t mPrintBufIndex;

  blocks_vector_t mBlocks;
  uintptr_t mHeapStartAddress;
  uintptr_t mHeapEndAddress;

  const int mFileDescriptor;
};

Scanner::Scanner(int fd)
  : mPrintBufIndex(0)
  , mFileDescriptor(fd)
{
  // collect all blocks in a vector
  for (list_elem_t *elem = first_list_elem();
       elem != sentinel();
       elem = elem->next)
  {
    block_t b;
    b.address = reinterpret_cast<uintptr_t>(payload_for_list_elem(elem));
    b.size = payload_size_for_list_elem(elem);
    mBlocks.push_back(b);
  }

  // sort blocks to allow fast binary searches
  std::sort(mBlocks.begin(), mBlocks.end());

  mHeapStartAddress = mBlocks.front().address;
  mHeapEndAddress = mBlocks.back().address + mBlocks.back().size;
}

Scanner::~Scanner()
{
}

Scanner::blocks_vector_t::const_iterator
Scanner::FindInHeap(uintptr_t address) const {
  if (MOZ_LIKELY(address < mHeapStartAddress || address > mHeapEndAddress))
    return mBlocks.end();

  return FindInHeapNontrivialCase(address);
}

Scanner::blocks_vector_t::const_iterator
Scanner::FindInHeapNontrivialCase(uintptr_t address) const {
  block_t b;
  b.address = address;
  blocks_vector_t::const_iterator it
    = std::lower_bound(mBlocks.begin(), mBlocks.end(), b);

  if (it->address > address) {
    if (it == mBlocks.begin())
      return mBlocks.end();
    --it;
  }

  assertion(it->address <= address);

  if (it->address + it->size <= address)
    return mBlocks.end();

  return it;
}

void Scanner::FlushPrintIfAboveThreshold()
{
  if (mPrintBufIndex >= sBufFlushThreshold) {
    FlushPrint();
  }
}

void Scanner::FlushPrint()
{
  (void) write(mFileDescriptor, mPrintBuf, mPrintBufIndex);
  mPrintBufIndex = 0;
}

void Scanner::Print(const char* str)
{
  FlushPrintIfAboveThreshold();
  size_t len = std::min(sMaxPrintSize, strlen(str));
  memcpy(mPrintBuf + mPrintBufIndex, str, len);
  mPrintBufIndex += len;
}

void Scanner::Print(uintptr_t n)
{
  FlushPrintIfAboveThreshold();

  if (!n) {
    mPrintBuf[mPrintBufIndex++] = '0';
    return;
  }

  const size_t bufsize = 16;
  char buf[bufsize];
  size_t i = 0;

  while (n) {
    char d = n & uintptr_t(0xf);
    n >>= 4;
    char c = '0' + d;
    if (d >= 0xa)
      c += 'a' - '0' - 10;
    buf[i++] = c;
    assertion(i <= bufsize);
  }

  while (i) {
    mPrintBuf[mPrintBufIndex++] = buf[--i];
  }
}

void Scanner::Print(const void* ptr)
{
  Print(reinterpret_cast<uintptr_t>(ptr));
}

static const char* Demangled(const char* in)
{
  static char output[max_demangled_name_length];
#ifdef MOZ_HAVE_RTTI
  int status = 0;
  size_t output_capacity = max_demangled_name_length;
  BeginWorkingAroundGCCDemanglerStupidity();
  abi::__cxa_demangle(in, output, &output_capacity, &status);
  EndWorkingAroundGCCDemanglerStupidity();
  if (status) {
    return in;
  }
  return output;
#else
  const char prefix[] = "with T = ";
  const char* pos = strstr(in, prefix);
  if (!pos) {
    return in;
  }
  strncpy(output,
          pos + sizeof(prefix) - 1,
          max_demangled_name_length - 1);
  size_t len = strlen(output);
  if (!len) {
    return in;
  }
  output[len - 1] = 0;
  return output;
#endif
}

void Scanner::Scan()
{
  Print("*** BEGIN REFGRAPH DUMP ***\n");
  Print("\n");
  Print("# All numbers are in hexadecimal.\n");
  Print("# All addresses are user (a.k.a. \"payload\") addresses.\n");
  Print("#\n");
  Print("# Legend:\n");
  Print("# c <count>            gives the number of blocks\n");
  Print("# b <index>            starts the description of block with given index\n");
  Print("# m <address> <size>   block metrics: address and size\n");
  Print("# t <typename>         type of object (mangled)\n");
  Print("# s <index> <offset>   starts the description of a strong reference\n");
  Print("# f <flags>            gives the current strong reference's flags\n");
  Print("# n <name>             gives the current strong reference's name\n");
  Print("# u <typename>         gives the current strong reference's type name\n");
  Print("# w <index>            gives a weak reference into another block\n");

  Print("\n");
  Print("c ", mBlocks.size(), "\n");

  uintptr_t total_blocks = 0;
  uintptr_t total_payloads_size = 0;
  uintptr_t total_list_elems_size = 0;
  uintptr_t total_waste_size = 0;
  uintptr_t total_real_usable_size = 0;
  uintptr_t total_hypothetical_real_usable_size_without_instrumentation = 0;

  for (blocks_vector_t::iterator it = mBlocks.begin();
       it != mBlocks.end();
       ++it)
  {
    Print("\n");

    ScanBlock(it);

    list_elem_t* elem = list_elem_for_payload((payload_t*)it->address);
    total_blocks     += 1;
    total_payloads_size   += elem->payload_size;
    total_list_elems_size += sizeof(list_elem_t);
    total_waste_size      += elem->offset_into_real_block;

    uintptr_t real_usable_size = gMallocFuncsTable->malloc_usable_size(real_block_for_list_elem(elem));
    assertion (real_usable_size >= elem->payload_size + sizeof(list_elem_t) + elem->offset_into_real_block);
    total_real_usable_size += real_usable_size;

    uintptr_t hypothetical_real_usable_size_without_instrumentation
      = gMallocFuncsTable->malloc_good_size(elem->payload_size);
    assertion(hypothetical_real_usable_size_without_instrumentation >= elem->payload_size);
    assertion(hypothetical_real_usable_size_without_instrumentation <= real_usable_size);
    total_hypothetical_real_usable_size_without_instrumentation
      += hypothetical_real_usable_size_without_instrumentation;
  }

  Print("\n");
  Print("# Reminder: All numbers are in hexadecimal.\n");
  Print("#\n");
  Print("# Application metrics:\n");
  Print("#   Number of live blocks:                           ",
        total_blocks, "\n");
  Print("#   Total size of payloads:                          ",
        total_payloads_size, "\n");
  Print("#\n");
  Print("# Instrumentation metrics:\n");
  Print("#   Size of instrumentation data per block:          ",
        sizeof(list_elem_t), "\n");
  Print("#   Total size of instrumentation data:              ",
        total_list_elems_size, "\n");
  Print("#   Total size of instrumentation waste:             ",
        total_waste_size, "\n");
  uintptr_t total_real_blocks_size = total_payloads_size + total_list_elems_size + total_waste_size;
  Print("#   Total size of real blocks:                       ",
        total_real_blocks_size, "\n");
  Print("#\n");
  Print("# Memory allocator metrics:\n");
  Print("#   What we have with instrumentation:\n");
  Print("#     Total size of extra usable space:              ",
        total_real_usable_size - total_real_blocks_size, "\n");
  Print("#     Total usable size of real blocks:              ",
        total_real_usable_size, "\n");

  Print("#   What we would hypothetically have without instrumentation:\n");
  Print("#     Total size of extra usable space:              ",
        total_hypothetical_real_usable_size_without_instrumentation - total_payloads_size, "\n");
  Print("#     Total usable size of real blocks:              ",
        total_hypothetical_real_usable_size_without_instrumentation, "\n");
  Print("#\n");
  Print("# Overhead summary:\n");
  Print("#   Total size of payloads:                          ",
        total_payloads_size, "\n");
  Print("#   Overhead of instrumentation alone:               ",
        total_real_blocks_size - total_payloads_size, "\n");
  Print("#   Total overhead of instrumentation and allocator: ",
        total_real_usable_size - total_payloads_size, "\n");
  Print("#   Overhead of allocator without instrumentation:   ",
        total_hypothetical_real_usable_size_without_instrumentation - total_payloads_size, "\n");
  Print("#   Total overhead incurred by instrumentation:      ",
        total_real_usable_size - total_hypothetical_real_usable_size_without_instrumentation, "\n");
  Print("\n");
  Print("*** END REFGRAPH DUMP ***\n\n");

  FlushPrint();
}

void Scanner::ScanBlock(blocks_vector_t::const_iterator block)
{
  Print("b ", block - mBlocks.begin(), "\n");
  Print("m ", block->address, " ", block->size, "\n");

  list_elem_t* elem = list_elem_for_payload((payload_t*)(block->address));
  if (elem->type) {
    const char* demangled = Demangled(elem->type);
    if (demangled) {
      Print("t ", demangled, "\n");
    }
  }

  uintptr_t start = block->address;
  uintptr_t stop = start + (block->size / sizeof(void*)) * sizeof(void*);

  assertion((start & (sizeof(void*) - 1)) == 0);
  assertion((stop  & (sizeof(void*) - 1)) == 0);

  uintptr_t scanPos = start;

  refs_vector_t refs;

  typedef std::vector<uint32_t, stl_allocator_bypassing_instrumentation<uint32_t> >
          index_vector_t;

  index_vector_t weakrefs;

  while (scanPos != stop) {
    uintptr_t scanned = *reinterpret_cast<uintptr_t*>(scanPos);
    scanPos += sizeof(uintptr_t);

    // clear least significant bits to avoid being fooled by tagged pointers
    scanned &= ~uintptr_t(sizeof(void*) - 1);

    blocks_vector_t::const_iterator target = FindInHeap(scanned);

    if (MOZ_LIKELY(target == mBlocks.end()))
      continue;

    bool isStrong = false;
    marker_t flags = 0;
    if (scanPos <= stop - 2 * sizeof(marker_t)) {
      marker_t marker1 = *reinterpret_cast<marker_t*>(scanPos);
      if (marker1 == strongRefBaseMarker1) {
        marker_t marker2 = *reinterpret_cast<marker_t*>(scanPos + sizeof(marker_t));
        flags = marker2 ^ strongRefBaseMarker2;
        isStrong = flags < 0x10;
      }
    }

    if (isStrong) {
      scanPos += 2 * sizeof(marker_t);
      ref_t r;
      r.target = target;
      r.offset = scanned - target->address;
      r.flags = flags;

      assertion(scanPos <= stop - 2 * sizeof(const char*));
      r.reftypename = *reinterpret_cast<const char**>(scanPos);
      scanPos += sizeof(const char*);
      r.refname = *reinterpret_cast<const char**>(scanPos);
      scanPos += sizeof(const char*);

      refs.push_back(r);
    } else {
      weakrefs.push_back(target - mBlocks.begin());
    }
  }

  if (!refs.empty()) {
    std::sort(refs.begin(), refs.end());
    for(refs_vector_t::const_iterator it = refs.begin();
        it != refs.end();
        ++it)
    {
      uint32_t target = it->target - mBlocks.begin();
      size_t offset = it->offset;
      const char* reftypename = it->reftypename;
      const char* refname = it->refname;
      uint32_t flags = it->flags;

      Print("s ", target, " ", offset, "\n");
      Print("f ", flags, "\n");
      if (refname) {
        Print("n ", refname, "\n");
      }
      if (reftypename) {
        const char* demangled = Demangled(reftypename);
        if (demangled) {
          Print("u ", demangled, "\n");
        }
      }
    }
  }

  if (!weakrefs.empty()) {
    std::sort(weakrefs.begin(), weakrefs.end());
    uint32_t last = -1;
    for(index_vector_t::const_iterator it = weakrefs.begin();
        it != weakrefs.end();
        ++it)
    {
      if (*it == last) {
        continue;
      }
      Print("w ", *it, "\n");
      last = *it;
    }
  }
}

} // end namespace refgraph

void replace_refgraph_dump(int fd)
{
  refgraph::Scanner(fd).Scan();
}
