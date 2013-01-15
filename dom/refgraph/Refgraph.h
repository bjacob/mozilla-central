#include "mozilla/dom/Nullable.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/BindingUtils.h"

#include "mozilla/dom/RefgraphBinding.h"

#include "mozilla/RefgraphSTLAllocatorBypassingInstrumentation.h"
#include "mozilla/RefgraphInstrumentation.h"

#include "nsWrapperCache.h"

#include <vector>
#include <string>
#include <map>

#ifndef Refgraph_h_
#define Refgraph_h_

namespace refgraph {

class RefgraphTypeSearchResult {

  friend class Refgraph;

  uint32_t mObjectCount;
  nsString mTypeName;

public:

  RefgraphTypeSearchResult(uint32_t objectCount, const nsAString& typeName)
    : mObjectCount(objectCount)
    , mTypeName(typeName)
  {}

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope);

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(RefgraphTypeSearchResult)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(RefgraphTypeSearchResult)

  uint32_t ObjectCount() const {
    return mObjectCount;
  }

  void GetTypeName(nsString& retval) const {
    retval = mTypeName;
  }

  struct Comparator {
    bool LessThan(const nsRefPtr<RefgraphTypeSearchResult>& a,
                  const nsRefPtr<RefgraphTypeSearchResult>& b) const
    {
      return a->mObjectCount > b->mObjectCount ||
             (a->mObjectCount == b->mObjectCount &&
              a->mTypeName < b->mTypeName);
    }

    bool Equals(const nsRefPtr<RefgraphTypeSearchResult>& a,
                const nsRefPtr<RefgraphTypeSearchResult>& b) const
    {
      return a->mObjectCount == b->mObjectCount &&
             a->mTypeName == b->mTypeName;
    }
  };
};

typedef std::vector<uint64_t, stl_allocator_bypassing_instrumentation<uint64_t> >
        stack_t;

typedef std::basic_string<char,
                          std::char_traits<char>,
                          stl_allocator_bypassing_instrumentation<char>
                          >
        string_t;

typedef uint32_t block_index_t;

struct ref_t
{
  block_index_t target;
  uint32_t flags;
  uint64_t offset;

  ref_t()
    : target(0)
    , flags(0)
    , offset(0)
  {}

  ref_t(block_index_t t, uint32_t f, uint64_t o)
    : target(t)
    , flags(f)
    , offset(o)
  {}
};

typedef std::vector<ref_t, stl_allocator_bypassing_instrumentation<ref_t> >
        refs_vector_t;

struct block_t {
  uint64_t address;
  uint64_t size;
  uint64_t typehash;
  uint64_t workspace;
  string_t type;
  stack_t stack;
  refs_vector_t refs;

  block_t()
    : address(0)
    , size(0)
    , typehash(0)
    , workspace(0)
  {}

};

typedef std::vector<block_t, stl_allocator_bypassing_instrumentation<block_t> >
        blocks_vector_t;

typedef std::multimap<string_t, block_index_t>
        map_types_to_block_indices_t;

class Refgraph;
class RefgraphEdge;
class RefgraphVertex;

class RefgraphEdge
  : public nsISupports
  , public nsWrapperCache
{
  friend class Refgraph;

  nsRefPtr<Refgraph> mParent;
  refs_vector_t::const_iterator mRef;

public:

  RefgraphEdge(Refgraph* parent, refs_vector_t::const_iterator ref);

  nsISupports* GetParentObject() const;

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope, bool *triedToWrap);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(RefgraphEdge)

  already_AddRefed<RefgraphVertex> Target() const;
  bool IsStrong() const;
  bool IsTraversedByCC() const;
};

class RefgraphVertex
  : public nsISupports
  , public nsWrapperCache
{
  friend class Refgraph;

  nsRefPtr<Refgraph> mParent;
  map_types_to_block_indices_t::const_iterator mBlockIterator;
  const block_t& mBlock;

public:

  RefgraphVertex(Refgraph* parent, map_types_to_block_indices_t::const_iterator block);
  RefgraphVertex(Refgraph* parent, const block_t& block);

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope, bool *triedToWrap);

  nsISupports* GetParentObject() const;

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(RefgraphVertex)

  uint64_t Address() const;
  uint64_t Size() const;
  void GetTypeName(nsString& retval) const;
  uint32_t EdgeCount() const;

  already_AddRefed<RefgraphEdge> Edge(uint32_t index) const;
};

class Refgraph {

  friend class RefgraphVertex;
  friend class RefgraphEdge;

  nsRefPtr<nsISupports> mParentObject;

  blocks_vector_t mBlocks;
  block_t* mCurrentBlock;

  map_types_to_block_indices_t mMapTypesToBlockIndices;

  char* mDemanglingInputBuffer;
  char* mDemanglingOutputBuffer;
  size_t mDemanglingInputBufferCapacity, mDemanglingOutputBufferCapacity;

  class ScopedAssertWorkspacesClear {
    Refgraph* r;
  public:
    ScopedAssertWorkspacesClear(Refgraph*);
    ~ScopedAssertWorkspacesClear();
  };
  friend class ScopedAssertWorkspacesClear;

  bool Demangle(const char* in, size_t length, string_t& out);

  void AssertWorkspacesClear();

  bool Parse(const char* buffer, size_t length);
  bool ResolveHereditaryStrongRefs();
  bool Acquire();

  bool HandleLine(const char* start, const char* end);
  bool HandleLine_a(const char* start, const char* end);
  bool HandleLine_b(const char* start, const char* end);
  bool HandleLine_m(const char* start, const char* end);
  bool HandleLine_n(const char* start, const char* end);
  bool HandleLine_r(const char* start, const char* end);
  bool HandleLine_t(const char* start, const char* end);

  bool ParseNumber(const char* start,
                   const char* end,
                   uint64_t* result,
                   const char** actualEnd = nullptr);
  bool ParseTwoNumbers(const char* start,
                       const char* end,
                       uint64_t* result1,
                       uint64_t* result2);
  bool ParseThreeNumbers(const char* start,
                        const char* end,
                        uint64_t* result1,
                        uint64_t* result2,
                        uint64_t* result3);

  Refgraph()
    : mCurrentBlock(nullptr)
    , mDemanglingInputBuffer(nullptr)
    , mDemanglingOutputBuffer(nullptr)
    , mDemanglingInputBufferCapacity(0)
    , mDemanglingOutputBufferCapacity(0)
  {}

  ~Refgraph()
  {
    delete[] mDemanglingInputBuffer;
    delete[] mDemanglingOutputBuffer;
  }

public:

  nsISupports *GetParentObject() const {
    return mParentObject;
  }

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope, bool* triedToWrap = nullptr);

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(Refgraph)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(Refgraph)

  static already_AddRefed<Refgraph>
  Constructor(nsISupports*, mozilla::ErrorResult&);

  void TypeSearch(const nsAString& query,
                  nsTArray<nsRefPtr<RefgraphTypeSearchResult> >& result);
  already_AddRefed<RefgraphVertex>
  FindVertex(const nsAString& typeName,
             RefgraphVertex* previousVertex);
};

} // namespace refgraph

#endif // Refgraph_h_
