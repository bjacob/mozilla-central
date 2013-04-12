/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "refgraph.h"

#include <cerrno>
#include <cstring>
#include <algorithm>

#include <cstdio>

namespace refgraph {

const malloc_table_t* gMallocFuncsTable = nullptr;

static bool gIsAnyThreadWorkingAroundGCCDemanglerStupidity = false;
static __thread bool gIsThisThreadWorkingAroundGCCDemanglerStupidity = false;

void BeginWorkingAroundGCCDemanglerStupidity() {
  assertion(!gIsAnyThreadWorkingAroundGCCDemanglerStupidity);
  assertion(!gIsThisThreadWorkingAroundGCCDemanglerStupidity);
  gIsAnyThreadWorkingAroundGCCDemanglerStupidity = true;
  gIsThisThreadWorkingAroundGCCDemanglerStupidity = true;
}

void EndWorkingAroundGCCDemanglerStupidity() {
  assertion(gIsAnyThreadWorkingAroundGCCDemanglerStupidity);
  assertion(gIsThisThreadWorkingAroundGCCDemanglerStupidity);
  gIsAnyThreadWorkingAroundGCCDemanglerStupidity = false;
  gIsThisThreadWorkingAroundGCCDemanglerStupidity = false;
}

bool IsWorkingAroundGCCDemanglerStupidity() {
  if (MOZ_UNLIKELY(gIsAnyThreadWorkingAroundGCCDemanglerStupidity)) {
    if (MOZ_UNLIKELY(gIsThisThreadWorkingAroundGCCDemanglerStupidity)) {
      return true;
    }
  }
  return false;
}

template<typename T>
void ensure_at_least_one(T& x)
{
  if (!x) {
    x = 1;
  }
}

class PagesMap {
  uint8_t* map;
  uintptr_t first_represented_page, last_represented_page;

  void EnsureCoversPageInterval(uintptr_t first_page, uintptr_t last_page);

public:
  PagesMap()
    : map(nullptr)
    , first_represented_page(uintptr_t(-1))
    , last_represented_page(0)
  {}
  ~PagesMap() { gMallocFuncsTable->free(map); }

  void MapInterval(uintptr_t first_byte, uintptr_t last_byte);
  void UnmapInterval(uintptr_t first_byte, uintptr_t last_byte);
  bool IsAddressMapped(uintptr_t address) const;
};

void PagesMap::EnsureCoversPageInterval(uintptr_t first_page, uintptr_t last_page)
{
  assertion(last_page >= first_page);

  if (first_page >= first_represented_page &&
      last_page <= last_represented_page)
  {
    return;
  }

  const uintptr_t margin = 0x4000;
  first_page -= std::min(first_page, margin);
  last_page += std::min(uintptr_t(-1) - last_page, margin);

  uintptr_t new_first_represented_page = std::min(first_page, first_represented_page);
  uintptr_t new_last_represented_page = std::max(last_page, last_represented_page);

  size_t new_map_size = new_last_represented_page - new_first_represented_page + 1;
  uint8_t* new_map = static_cast<uint8_t*>(gMallocFuncsTable->calloc(new_map_size, 1));
  if (last_represented_page) {
    size_t map_size = last_represented_page - first_represented_page + 1;
    if (map_size) {
      memcpy(new_map + first_represented_page - new_first_represented_page, map, map_size);
    }
  }
  gMallocFuncsTable->free(map);
  map = new_map;
  first_represented_page = new_first_represented_page;
  last_represented_page = new_last_represented_page;
}

void PagesMap::MapInterval(uintptr_t first_byte, uintptr_t last_byte)
{
  assertion(last_byte >= first_byte);
  uintptr_t first_page = first_byte >> page_exponent;
  uintptr_t last_page = last_byte >> page_exponent;

  EnsureCoversPageInterval(first_page, last_page);

  size_t first_index = first_page - first_represented_page;
  size_t last_index = last_page - first_represented_page;

  for (size_t i = first_index; i <= last_index; i++) {
    map[i]++;
    assertion(map[i]);
  }

  assertion(IsAddressMapped(first_byte));
  assertion(IsAddressMapped(last_byte));
}

void PagesMap::UnmapInterval(uintptr_t first_byte, uintptr_t last_byte)
{
  assertion(last_byte >= first_byte);
  uintptr_t first_page = first_byte >> page_exponent;
  uintptr_t last_page = last_byte >> page_exponent;

  size_t first_index = first_page - first_represented_page;
  size_t last_index = last_page - first_represented_page;

  assertion(IsAddressMapped(first_byte));
  assertion(IsAddressMapped(last_byte));

  for (size_t i = first_index; i <= last_index; i++) {
    assertion(map[i]);
    map[i]--;
  }
}

bool PagesMap::IsAddressMapped(uintptr_t address) const
{
  uintptr_t page = address >> page_exponent;
  return page >= first_represented_page &&
         page <= last_represented_page &&
         map[page - first_represented_page];
}

PagesMap gPagesMap;

pthread_mutex_t AutoLock::sLock = PTHREAD_MUTEX_INITIALIZER;

inline void
init_list_elem(list_elem_t* elem, real_block_t* real_block, size_t payload_size)
{
  assertion(!(sizeof(list_elem_t) % list_elem_size_guaranteed_to_be_multiple_of));
  elem->marker = list_elem_marker_value;
  elem->prev = elem;
  elem->next = elem;
  elem->payload_size = payload_size;
  elem->offset_into_real_block = (size_t)(((char*)elem) - ((char*)real_block));
  elem->type = nullptr;
}

// Returns the initial linked list element
list_elem_t*
sentinel(void)
{
  static list_elem_t sentinel_object;
  static bool sentinel_object_initialized = false;

  if (!sentinel_object_initialized) {
    init_list_elem(&sentinel_object, nullptr, 0);
    sentinel_object.marker = 0;
    sentinel_object_initialized = true;
  }
  return &sentinel_object;
}

inline void
tie_list_elems(list_elem_t* a, list_elem_t* b)
{
  a->next = b;
  b->prev = a;
}

inline void
append_list_elem(list_elem_t* elem)
{
  list_elem_t* s = sentinel();
  list_elem_t* last = s->prev;
  tie_list_elems(last, elem);
  tie_list_elems(elem, s);
  payload_t* payload = payload_for_list_elem(elem);
  uintptr_t payload_address = reinterpret_cast<uintptr_t>(payload);
  size_t payload_size = payload_size_for_list_elem(elem);
  gPagesMap.MapInterval(payload_address, payload_address + payload_size - 1);
}

inline void
remove_list_elem(list_elem_t* elem)
{
  assertion(is_actual_list_elem(elem));
  payload_t* payload = payload_for_list_elem(elem);
  uintptr_t payload_address = reinterpret_cast<uintptr_t>(payload);
  size_t payload_size = payload_size_for_list_elem(elem);
  memset(payload, 0x5a, payload_size);
  gPagesMap.UnmapInterval(payload_address, payload_address + payload_size - 1);
  elem->marker = 0;
  tie_list_elems(elem->prev, elem->next);
}

static payload_t*
instrument_new_block(real_block_t* real_block, size_t extra_space, size_t size)
{
  // can't use list_elem_for_real_block here because it asserts that the block is already instrumented
  list_elem_t* elem = reinterpret_cast<list_elem_t*>(reinterpret_cast<char*>(real_block) + extra_space - sizeof(list_elem_t));
  init_list_elem(elem, real_block, size);
  append_list_elem(elem);

  return payload_for_list_elem(elem);
}

} // end namespace refgraph

using namespace refgraph;

void* replace_malloc(size_t size)
{
  AutoLock autoLock;

  ensure_at_least_one(size);

  size_t extra_space = sizeof(list_elem_t);

  real_block_t* real_block = static_cast<real_block_t*>(gMallocFuncsTable->malloc(size + extra_space));

  if (!real_block)
    return nullptr;

  return instrument_new_block(real_block, extra_space, size);
}

int replace_posix_memalign(void **memptr, size_t alignment, size_t size)
{
  AutoLock autoLock;

  *memptr = nullptr;

  if ((!alignment) ||
      (alignment & (alignment - 1)))
  {
    return EINVAL;
  }

  ensure_at_least_one(size);

  while (alignment < sizeof(list_elem_t))
    alignment *= 2;

  real_block_t* real_block;
  int ret = gMallocFuncsTable->posix_memalign((void**)(&real_block), alignment, size + alignment);

  if (ret) {
    *memptr = nullptr;
    return ret;
  }
  *memptr = instrument_new_block(real_block, alignment, size);
  return 0;
}

void *replace_aligned_alloc(size_t alignment, size_t size)
{
  AutoLock autoLock;

  if ((!alignment) ||
      (alignment & (alignment - 1)))
  {
    errno = EINVAL;
    return nullptr;
  }

  ensure_at_least_one(size);

  while (alignment < sizeof(list_elem_t))
    alignment *= 2;

  real_block_t* real_block = static_cast<real_block_t*>(gMallocFuncsTable->aligned_alloc(alignment, size + alignment));

  if (!real_block)
    return nullptr;

  return instrument_new_block(real_block, alignment, size);
}

void *replace_calloc(size_t num, size_t size)
{
  AutoLock autoLock;

  const size_t extra_space = sizeof(list_elem_t);

  ensure_at_least_one(num);
  ensure_at_least_one(size);

  const size_t size_t_max = -1;

  if (num > size_t_max / size)
    return nullptr;

  size_t bytes = num * size;
  assertion(bytes / size == num);
  real_block_t* real_block = static_cast<real_block_t*>(gMallocFuncsTable->calloc(1, bytes + extra_space));
  if (!real_block)
    return nullptr;

  return instrument_new_block(real_block, extra_space, bytes);
}

void *replace_realloc(void *oldp, size_t newsize)
{
  ensure_at_least_one(newsize);

  if (MOZ_UNLIKELY(IsWorkingAroundGCCDemanglerStupidity())) {
    if (newsize <= max_demangled_name_length) {
      static char demangled_name_buffer[max_demangled_name_length];
      return demangled_name_buffer;
    } else {
      return nullptr;
    }
  }

  AutoLock autoLock;

  const size_t extra_space = sizeof(list_elem_t);

  if (!oldp) {
    // this just a malloc

    real_block_t* real_block = static_cast<real_block_t*>(gMallocFuncsTable->malloc(newsize + extra_space));

    if (!real_block)
      return nullptr;

    return instrument_new_block(real_block, extra_space, newsize);
  }

  list_elem_t* old_elem = list_elem_for_payload(static_cast<payload_t*>(oldp));
  real_block_t* old_real_block = real_block_for_list_elem(old_elem);

  // we can't use realloc, because remove_list_elem wants to overwrite the old block with 0x5a's.
  real_block_t* new_real_block = static_cast<real_block_t*>(gMallocFuncsTable->malloc(newsize + extra_space));
  payload_t* new_payload = nullptr;
  if (new_real_block) {
    new_payload = instrument_new_block(new_real_block, extra_space, newsize);
    list_elem_t* new_elem = list_elem_for_payload(new_payload);
    new_elem->type = old_elem->type;

    size_t size_to_copy = std::min(newsize, payload_size_for_list_elem(old_elem));
    memcpy(new_payload, payload_for_list_elem(old_elem), size_to_copy);
  }
  remove_list_elem(old_elem);
  gMallocFuncsTable->free(old_real_block);
  return new_payload;
}

void replace_free(void *ptr)
{
  if (!ptr)
    return;

  if (MOZ_UNLIKELY(IsWorkingAroundGCCDemanglerStupidity())) {
    return;
  }

  AutoLock autoLock;

  list_elem_t* elem = list_elem_for_payload(static_cast<payload_t*>(ptr));
  real_block_t* old_real_block = real_block_for_list_elem(elem);
  remove_list_elem(elem);
  gMallocFuncsTable->free(old_real_block);
}

void *replace_memalign(size_t alignment, size_t size)
{
  AutoLock autoLock;

  ensure_at_least_one(size);

  if ((!alignment) ||
      (alignment & (alignment - 1)))
  {
    errno = EINVAL;
    return nullptr;
  }

  while (alignment < sizeof(list_elem_t))
    alignment *= 2;

  real_block_t* real_block =static_cast<real_block_t*>(gMallocFuncsTable->memalign(alignment, size + alignment));
  if (!real_block)
    return nullptr;

  return instrument_new_block(real_block, alignment, size);
}

void *replace_valloc(size_t size)
{
  AutoLock autoLock;

  const size_t alignment = page_size;

  ensure_at_least_one(size);

  real_block_t* real_block = static_cast<real_block_t*>(gMallocFuncsTable->valloc(size + alignment));
  if (!real_block)
    return nullptr;

  return instrument_new_block(real_block, alignment, size);
}

size_t replace_malloc_usable_size(usable_ptr_t ptr)
{
  AutoLock autoLock;

  if (!ptr)
    return 0;

  return payload_size_for_payload((payload_t*)ptr);
}

size_t replace_malloc_good_size(size_t size)
{
  return size;
}

void replace_init(const malloc_table_t* table)
{
  gMallocFuncsTable = table;
}

inline list_elem_t*
list_elem_for_arbitrary_pointer(const void* pointer)
{
  assertion(pointer);
  uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
  if (!gPagesMap.IsAddressMapped(address)) {
    // not a heap address. Don't scan, we wouldn't find a marker and would crash.
    return nullptr;
  }
  // from here on, trust that we will find a marker by scanning backwards.
  address &= ~uintptr_t(sizeof(void*) - 1);
  while (true)
  {
    if (*reinterpret_cast<uint64_t*>(address) == list_elem_marker_value) {
      return reinterpret_cast<list_elem_t*>(address - offsetof(list_elem_t, marker));
    }
    address -= sizeof(void*);
  }
}

void
replace_refgraph_set_type(const void* pointer, const char* type)
{
  if (!pointer || !type || !type[0])
    return;

  AutoLock autoLock;

  list_elem_t* elem = list_elem_for_arbitrary_pointer(pointer);
  if (!elem)
    return;
  elem->type = type;
}
