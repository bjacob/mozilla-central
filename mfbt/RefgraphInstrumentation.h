/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Instrumentation for the refgraph tool */

#ifndef mozilla_RefgraphInstrumentation_h_
#define mozilla_RefgraphInstrumentation_h_

#if (!defined NO_MOZ_GLUE) && (!defined MOZ_REFGRAPH_REPLACE_MALLOC_IMPLEMENTATION)
#include "mozmemory.h"
#endif

#include "mozilla/StandardInteger.h"
#include "mozilla/Assertions.h"
#include "mozilla/Types.h"

#include <typeinfo>

namespace refgraph {

typedef uint64_t marker_t;

const marker_t baseMarker               = 0xb762f23059c146c0;
const marker_t strongRefFlag            = 0x1;
const marker_t hereditaryStrongRefFlag  = 0x2;
const marker_t traversedByCCFlag        = 0x4;

class StrongRefMarker
{
  volatile marker_t mMarker;
  volatile const char* mRefTypeName;

public:

  StrongRefMarker()
    : mMarker(baseMarker | strongRefFlag)
    , mRefTypeName(nullptr)
  {
  }

  template<typename T>
  StrongRefMarker(const T* parent)
    : mMarker(baseMarker | strongRefFlag)
    , mRefTypeName(nullptr)
  {
    SetParent(parent);
  }

  template<typename T>
  void SetParent(const T* parent) {
    if (parent) {
      mRefTypeName = typeid(*parent).name();
    }
  }

  ~StrongRefMarker() {
    mMarker = 0;
    mRefTypeName = nullptr;
  }

  void SetHereditary() { mMarker |= hereditaryStrongRefFlag; }

  void SetTraversedByCC() { mMarker |= traversedByCCFlag; }
};

template <typename T>
T* SetType(T* pointer)
{
#ifndef NO_MOZ_GLUE
  refgraph_set_type(pointer, typeid(*pointer).name());
#endif
  return pointer;
}

} // namespace refgraph

#endif // mozilla_RefgraphInstrumentation_h_
