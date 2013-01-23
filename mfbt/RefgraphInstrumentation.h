/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Instrumentation for the refgraph tool */

#ifndef mozilla_RefgraphInstrumentation_h_
#define mozilla_RefgraphInstrumentation_h_

#include "mozmemory.h"

#include "mozilla/StandardInteger.h"
#include "mozilla/Assertions.h"
#include "mozilla/Types.h"

#if defined(_CPPRTTI) || defined(__GXX_RTTI)
#define MOZ_HAVE_RTTI
#endif

#ifdef MOZ_HAVE_RTTI
#include <typeinfo>
#endif

template<typename T>
const char* ___(const T* t)
{
#ifdef MOZ_HAVE_RTTI
  return typeid(*t).name();
#else
  return __PRETTY_FUNCTION__;
#endif
}

namespace refgraph {

typedef uint64_t marker_t;

const marker_t strongRefBaseMarker      = 0xb762f23059c146c0;
const marker_t hereditaryFlag           = 0x1;
const marker_t traversedByCCFlag        = 0x2;

class StrongRefMarker
{
  volatile marker_t mMarker;
  volatile const char* mRefTypeName;
  volatile const char* mRefName;

public:

  StrongRefMarker()
    : mMarker(strongRefBaseMarker)
    , mRefTypeName(nullptr)
    , mRefName(nullptr)
  {
  }

  template<typename T>
  StrongRefMarker(const T* parent)
    : mMarker(strongRefBaseMarker)
    , mRefTypeName(nullptr)
    , mRefName(nullptr)
  {
    SetParent(parent);
  }

  template<typename T>
  void SetParent(const T* parent) {
    if (parent) {
      mRefTypeName = ___(parent);
    }
  }

  ~StrongRefMarker() {
    mMarker = 0;
    mRefTypeName = nullptr;
    mRefName = nullptr;
  }

  void SetHereditary() { mMarker |= hereditaryFlag; }

  void SetTraversedByCC(const char *name) {
    mMarker |= traversedByCCFlag;
    mRefName = name;
  }
};

template <typename T>
T* SetType(T* pointer)
{
#ifndef NO_MOZ_GLUE
  refgraph_set_type(pointer, ___(pointer));
#endif
  return pointer;
}

} // namespace refgraph

#endif // mozilla_RefgraphInstrumentation_h_
