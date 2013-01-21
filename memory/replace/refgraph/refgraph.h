/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef refgraph_h_
#define refgraph_h_

#include "replace_malloc.h"

#include "mozilla/Util.h"
#include "mozilla/Types.h"
#include "mozilla/Likely.h"

#define MOZ_REFGRAPH_REPLACE_MALLOC_IMPLEMENTATION
#include "mozilla/RefgraphInstrumentation.h"

#include <pthread.h>

namespace refgraph {

#ifdef DEBUG
// assertion with no output, to avoid calling malloc again on failure
inline void assertion(bool condition)
{
  if (!condition)
    abort();
  (void) condition;
}
#else
#define assertion(...)
#endif

void BeginWorkingAroundGCCDemanglerStupidity();
void EndWorkingAroundGCCDemanglerStupidity();
bool IsWorkingAroundGCCDemanglerStupidity();

class AutoLock
{
  static pthread_mutex_t sLock;

public:
  AutoLock()
  {
    pthread_mutex_lock(&sLock);
  }

  ~AutoLock()
  {
    pthread_mutex_unlock(&sLock);
  }
};

const size_t page_exponent = 12;
const size_t page_size = 1 << page_exponent;

extern const malloc_table_t* gMallocFuncsTable;

const size_t max_demangled_name_length = page_size;

#define list_elem_size_guaranteed_to_be_multiple_of 16

// This is the linked-list-element structure that we are storing at the beginning of each block.
// So we allocate larger blocks than requested, and store this linked list element just before
// the payload.
struct list_elem_t
{
  // Doubly linked list; could be made a XOR-linked list to save some space.
  MOZ_ALIGNED_DECL(list_elem_t*, list_elem_size_guaranteed_to_be_multiple_of) prev;
  list_elem_t* next;
  size_t offset_into_real_block;
  size_t payload_size;

  const char* type;
  size_t typeOffset;

  // Marker allowing to identify instrumented blocks.
  // Used when scanning to find if an arbitrary pointer falls into a block.
  volatile uint64_t marker;
};

// randomly chosen to minimize collisions
const uint64_t list_elem_marker_value = 0x8313f73b0b58dc49;

// dummy types to add some type safety to our pointers
struct real_block_t {};
struct payload_t {};

inline int
is_actual_list_elem(list_elem_t* elem)
{
  return elem->marker == list_elem_marker_value;
}

list_elem_t*
sentinel(void);

inline payload_t*
payload_for_list_elem(list_elem_t* elem);

inline list_elem_t*
first_list_elem(void)
{
  return sentinel()->next;
}

inline list_elem_t*
list_elem_for_payload(payload_t* payload)
{
  assertion(payload);
  list_elem_t* elem = reinterpret_cast<list_elem_t*>(payload) - 1;
  assertion(is_actual_list_elem(elem));
  return elem;
}

inline payload_t*
payload_for_list_elem(list_elem_t* elem)
{
  assertion(is_actual_list_elem(elem));
  return (payload_t* )(elem + 1);
}

inline real_block_t*
real_block_for_list_elem(list_elem_t* elem)
{
  assertion(is_actual_list_elem(elem));
  return (real_block_t* )(reinterpret_cast<char*>(elem) - elem->offset_into_real_block);
}

inline list_elem_t*
list_elem_for_real_block(real_block_t* real_block, size_t extra_space)
{
  list_elem_t* elem = reinterpret_cast<list_elem_t*>(reinterpret_cast<char*>(real_block) + extra_space - sizeof(list_elem_t));
  assertion(is_actual_list_elem(elem));
  return elem;
}

inline size_t
payload_size_for_list_elem(list_elem_t* elem)
{
  assertion(is_actual_list_elem(elem));
  return elem->payload_size;
}

inline size_t
payload_size_for_payload(payload_t* payload)
{
  return payload_size_for_list_elem(list_elem_for_payload(payload));
}

inline real_block_t*
real_block_for_payload(payload_t* payload)
{
  return real_block_for_list_elem(list_elem_for_payload(payload));
}

} // end namespace refgraph

#endif // refgraph_h_
