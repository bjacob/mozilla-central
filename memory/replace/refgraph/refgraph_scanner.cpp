/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "refgraph.h"
#include "mozilla/RefgraphInstrumentation.h"
#include "mozilla/RefgraphSTLAllocatorBypassingInstrumentation.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>

#include <vector>
#include <algorithm>

#undef malloc_good_size

namespace refgraph {

class Sink
{
  bool mIsInternal;
public:
  Sink(bool isInternal) : mIsInternal(isInternal) {}
  virtual void Write(const char* buf, size_t bufsize) = 0;
  bool IsInternal() const { return mIsInternal; }
};

class FileDescriptorSink
  : public Sink
{
  int mFileDescriptor;

public:
  FileDescriptorSink(int fd)
    : Sink(false)
    , mFileDescriptor(fd)
  {}

  virtual void Write(const char* buf, size_t bufsize)
  {
    write(mFileDescriptor, buf, bufsize);
  }
};

class BufferSink
  : public Sink
{
  char* mBuffer;
  size_t mBufferLength;
  size_t mBufferCapacity;

  void EnsureCapacity(size_t requiredCapacity)
  {
    if (MOZ_LIKELY(mBufferCapacity >= requiredCapacity))
      return;

    mBufferCapacity = page_size;
    while (mBufferCapacity < requiredCapacity)
      mBufferCapacity *= 2;

    mBuffer = static_cast<char*>(gMallocFuncsTable->realloc(mBuffer, mBufferCapacity));
  }

public:
  BufferSink()
    : Sink(true)
    , mBuffer(nullptr)
    , mBufferLength(0)
    , mBufferCapacity(0)
  {}

  virtual void Write(const char* buf, size_t bufsize)
  {
    size_t newLength = mBufferLength + bufsize;
    EnsureCapacity(newLength);
    memcpy(mBuffer + mBufferLength, buf, bufsize);
    mBufferLength = newLength;
  }

  const char* Buffer() const { return mBuffer; }
  size_t BufferLength() const { return mBufferLength; }
};

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

    bool operator<(const ref_t& other) const {
      return target->address + offset < other.target->address + other.offset;
    }
  };

  typedef std::vector<ref_t, stl_allocator_bypassing_instrumentation<ref_t> >
          refs_vector_t;

public:
  Scanner(Sink* sink);
  ~Scanner();

  void Scan();

private:

  void ScanBlock(blocks_vector_t::const_iterator block);

  blocks_vector_t::const_iterator
  FindInHeap(uintptr_t address) const;

  blocks_vector_t::const_iterator
  FindInHeapNontrivialCase(uintptr_t address) const;

  void Print(const char* str);
  void Print(uintptr_t n);
  void Print(const void* ptr);

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

  Sink* mSink;
};

Scanner::Scanner(Sink* sink)
  : mPrintBufIndex(0)
  , mSink(sink)
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
    it--;
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
  mSink->Write(mPrintBuf, mPrintBufIndex);
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

void Scanner::Scan()
{
  Print("# All numbers are in hexadecimal.\n");
  Print("# All addresses are user (a.k.a. \"payload\") addresses.\n");
  Print("#\n");
  Print("# Legend:\n");
  Print("# n <count>            gives the number of blocks\n");
  Print("# b <index>            starts the description of block with given index\n");
  Print("# m <address> <size>   block metrics: address and size\n");
  Print("# a <address>          gives a frame of allocation call stack\n");
  Print("# t <typename>         type of object (mangled)\n");
  Print("# r <index> <offset> <flags>  gives a reference into another block\n");

  Print("\n");
  Print("n ", mBlocks.size(), "\n");

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

  FlushPrint();
}

void Scanner::ScanBlock(blocks_vector_t::const_iterator block)
{
  Print("b ", block - mBlocks.begin(), "\n");
  Print("m ", block->address, " ", block->size, "\n");

  list_elem_t* elem = list_elem_for_payload((payload_t*)(block->address));
  if (elem->type) {
    Print("t ", elem->type, "\n");
  }
#ifdef REFGRAPH_BACKTRACE
  for (size_t i = 0; i < backtrace_size; i++) {
    Print("a ", elem->allocation_backtrace[i], "\n");
  }
#endif

  uintptr_t start = block->address;
  uintptr_t stop = start + (block->size / sizeof(void*)) * sizeof(void*);

  assertion((start & (sizeof(void*) - 1)) == 0);
  assertion((stop  & (sizeof(void*) - 1)) == 0);

  uintptr_t scanPos = start;

  refs_vector_t refs;

  ref_t* current_ref = nullptr;

  while (scanPos != stop) {
    uintptr_t scanned = *reinterpret_cast<uintptr_t*>(scanPos);
    scanPos += sizeof(uintptr_t);

    // clear least significant bits to avoid being fooled by tagged pointers
    scanned &= ~uintptr_t(sizeof(void*) - 1);

    blocks_vector_t::const_iterator target = FindInHeap(scanned);

    if (MOZ_LIKELY(target == mBlocks.end()))
      continue;

    size_t offset = scanned - target->address;

    if (!current_ref ||
        current_ref->target != target)
    {
      ref_t r;
      r.target = target;
      r.offset = offset;
      r.flags = 0;
      refs.push_back(r);
      current_ref = &refs.back();
    }

    if (scanPos > stop - sizeof(marker_t))
      continue;

    marker_t marker = *reinterpret_cast<marker_t*>(scanPos);

    marker_t flags = marker ^ baseMarker;

    // test if we found a marker
    if (flags > 0xf)
      continue;

    scanPos += sizeof(marker_t);

    current_ref->flags |= flags;
  }

  if (!refs.size())
    return;

  std::sort(refs.begin(), refs.end());

  refs_vector_t::iterator it = refs.begin();
  do {
    blocks_vector_t::const_iterator target = it->target;
    size_t offset = it->offset;
    uint32_t flags = 0;

    do {
      flags |= it->flags;
      ++it;
    } while (it != refs.end() &&
             it->target == target &&
             it->offset == offset);

    Print("r ", target - mBlocks.begin());
    Print(" ", offset);
    Print(" ", flags, "\n");

  } while (it != refs.end());
}

} // end namespace refgraph

void replace_refgraph_dump_to_buffer(const char** buffer, size_t* length)
{
  refgraph::BufferSink sink;
  refgraph::Scanner scanner(&sink);
  scanner.Scan();
  *buffer = sink.Buffer();
  *length = sink.BufferLength();
}

void replace_refgraph_dump_to_file(const char* filename)
{
  int fd = open(filename,
                O_WRONLY | O_TRUNC | O_CREAT,
                S_IRUSR | S_IWUSR);
  if (fd < 0) {
    fprintf(stderr, "could not open %s for writing\n", filename);
    return;
  }
  refgraph::FileDescriptorSink sink(fd);
  refgraph::Scanner scanner(&sink);
  scanner.Scan();
  close(fd);
}
