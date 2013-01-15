/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_RefgraphSTLAllocatorBypassingInstrumentation_h_
#define mozilla_RefgraphSTLAllocatorBypassingInstrumentation_h_

#include "mozmemory.h"

namespace refgraph {

template<class T>
class stl_allocator_bypassing_instrumentation
{
public:
    typedef size_t    size_type;
    typedef ptrdiff_t difference_type;
    typedef T*        pointer;
    typedef const T*  const_pointer;
    typedef T&        reference;
    typedef const T&  const_reference;
    typedef T         value_type;

    static const size_type size_type_max = -1;

    template<class U>
    struct rebind
    {
      typedef stl_allocator_bypassing_instrumentation<U> other;
    };

    pointer address( reference value ) const
    {
      return &value;
    }

    const_pointer address( const_reference value ) const
    {
      return &value;
    }

    stl_allocator_bypassing_instrumentation()
    {
    }

    stl_allocator_bypassing_instrumentation( const stl_allocator_bypassing_instrumentation& )
    {
    }

    template<class U>
    stl_allocator_bypassing_instrumentation( const stl_allocator_bypassing_instrumentation<U>& )
    {
    }

    ~stl_allocator_bypassing_instrumentation()
    {
    }

    size_type max_size() const
    {
      return size_type_max;
    }

    pointer allocate( size_type num, const void* hint = 0 )
    {
      (void) hint;
      if (num > size_type_max / sizeof(T))
        return nullptr;
      return static_cast<pointer>(refgraph_uninstrumented_malloc(num * sizeof(T)));
    }

    void construct( pointer p, const T& value )
    {
      ::new( p ) T( value );
    }

    void destroy( pointer p )
    {
      p->~T();
    }

    void deallocate( pointer p, size_type /*num*/ )
    {
      refgraph_uninstrumented_free(p);
    }

    bool operator!=(const stl_allocator_bypassing_instrumentation<T>& ) const
    { return false; }

    bool operator==(const stl_allocator_bypassing_instrumentation<T>& ) const
    { return true; }
};

}

#endif
