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
#include "mozilla/NullPtr.h"

#if defined(_CPPRTTI) || defined(__GXX_RTTI)
#define MOZ_HAVE_RTTI
#endif

#ifdef MOZ_HAVE_RTTI
#include <typeinfo>
#endif

namespace refgraph {

template<typename T>
const char* gettypename(const T* t)
{
#ifdef MOZ_HAVE_RTTI
  return typeid(*t).name();
#else
  return __PRETTY_FUNCTION__;
#endif
}

typedef uint32_t marker_t;

const marker_t strongRefBaseMarker1     = 0xb762f23d;
const marker_t strongRefBaseMarker2     = 0x59c146c0;
const marker_t hereditaryFlag           = 0x1;
const marker_t traversedByCCFlag        = 0x2;

class StrongRefMarker
{
  volatile marker_t mMarker1;
  volatile marker_t mMarker2;
  volatile const char* mRefTypeName;
  volatile const char* mRefName;

public:

  StrongRefMarker()
    : mMarker1(strongRefBaseMarker1)
    , mMarker2(strongRefBaseMarker2)
    , mRefTypeName(nullptr)
    , mRefName(nullptr)
  {
  }

  template<typename T>
  StrongRefMarker(const T* parent)
    : mMarker1(strongRefBaseMarker1)
    , mMarker2(strongRefBaseMarker2)
    , mRefTypeName(nullptr)
    , mRefName(nullptr)
  {
    SetParent(parent);
  }

  template<typename T>
  void SetParent(const T* parent) {
    if (parent) {
      mRefTypeName = gettypename(parent);
    }
  }

  ~StrongRefMarker() {
    mMarker1 = 0;
    mMarker2 = 0;
    mRefTypeName = nullptr;
    mRefName = nullptr;
  }

  void SetHereditary() {
    mMarker2 |= hereditaryFlag;
  }
};

template <typename T>
T* SetType(T* pointer)
{
#ifndef NO_MOZ_GLUE
  refgraph_set_type(pointer, gettypename(pointer));
#endif
  return pointer;
}

} // namespace refgraph

#endif // mozilla_RefgraphInstrumentation_h_
