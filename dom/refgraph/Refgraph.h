#include "mozilla/dom/Nullable.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/BindingUtils.h"

#include "mozilla/dom/RefgraphBinding.h"

#include "mozilla/RefgraphSTLAllocatorBypassingInstrumentation.h"
#include "mozilla/RefgraphInstrumentation.h"

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

typedef std::basic_string<char,
                          std::char_traits<char>,
                          stl_allocator_bypassing_instrumentation<char>
                          >
        string_t;

struct ref_t
{
  uint32_t target;
  uint8_t flags;
  uint64_t offset;
  string_t refname;
  string_t reftypename;

  ref_t()
    : target(0)
    , flags(0)
    , offset(0)
  {}
};

typedef std::vector<ref_t, stl_allocator_bypassing_instrumentation<ref_t> >
        refs_vector_t;

typedef std::vector<uint32_t, stl_allocator_bypassing_instrumentation<uint32_t> >
        index_vector_t;

struct cycle_t {
  index_vector_t vertices;
  string_t name;
  bool isTraversedByCC;

  bool operator< (const cycle_t& other) const {
    if (name < other.name) {
      return true;
    }
    if (name == other.name) {
      if (vertices.size() > other.vertices.size()) {
        return true;
      }
      if (vertices.size() == other.vertices.size()) {
        if (!isTraversedByCC && other.isTraversedByCC) {
          return true;
        }
      }
    }
    return false;
  }
};

typedef std::vector<cycle_t, stl_allocator_bypassing_instrumentation<cycle_t> >
        cycles_vector_t;

struct block_t {
  uint64_t address;
  uint64_t size;

  string_t type;
  refs_vector_t refs;
  index_vector_t weakrefs;
  index_vector_t backrefs;
  index_vector_t backweakrefs;

  uint32_t workspace;
  uint32_t cycle_index;

  block_t()
    : address(0)
    , size(0)
    , workspace(0)
    , cycle_index(0)
  {}

  bool operator<(const block_t& other) const {
    return address < other.address;
  }

  bool operator==(const block_t& other) const {
    return address == other.address;
  }

  bool contains(uint64_t otherAddress) const {
    return address <= otherAddress &&
           (address + size) > otherAddress;
  }
};

typedef std::vector<block_t, stl_allocator_bypassing_instrumentation<block_t> >
        blocks_vector_t;

typedef std::multimap<string_t,
                      uint32_t,
                      std::less<string_t>,
                      stl_allocator_bypassing_instrumentation<std::pair<const string_t, uint32_t> > >
        map_types_to_block_indices_t;

class Refgraph;
class RefgraphEdge;
class RefgraphVertex;

class RefgraphEdge
{
  friend class Refgraph;

  nsRefPtr<Refgraph> mParent;
  refs_vector_t::const_iterator mRef;

public:

  RefgraphEdge(Refgraph* parent, refs_vector_t::const_iterator ref);

  nsISupports* GetParentObject() const;

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope);

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(RefgraphEdge)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(RefgraphEdge)

  already_AddRefed<RefgraphVertex> Target() const;
  bool IsTraversedByCC() const;
  void GetRefName(nsString& retval) const;
  void GetRefTypeName(nsString& retval) const;
};

class RefgraphVertex
{
  friend class Refgraph;

  nsRefPtr<Refgraph> mParent;
  map_types_to_block_indices_t::const_iterator mBlockIterator;
  const block_t& mBlock;

public:

  RefgraphVertex(Refgraph* parent, map_types_to_block_indices_t::const_iterator block);
  RefgraphVertex(Refgraph* parent, const block_t& block);

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope);

  nsISupports* GetParentObject() const;

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(RefgraphVertex)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(RefgraphVertex)

  uint64_t Address() const;
  uint64_t Size() const;
  void GetTypeName(nsString& retval) const;
  uint32_t EdgeCount() const;
  uint32_t CycleIndex() const;

  already_AddRefed<RefgraphEdge> Edge(uint32_t index) const;

  uint32_t WeakEdgeCount() const;
  uint32_t BackEdgeCount() const;
  uint32_t BackWeakEdgeCount() const;

  already_AddRefed<RefgraphVertex> WeakEdge(uint32_t index) const;
  already_AddRefed<RefgraphVertex> BackEdge(uint32_t index) const;
  already_AddRefed<RefgraphVertex> BackWeakEdge(uint32_t index) const;
};

class RefgraphCycle
{
  nsRefPtr<Refgraph> mParent;
  const uint32_t mIndex;

public:

  RefgraphCycle(Refgraph* parent, uint32_t index);

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope);

  nsISupports* GetParentObject() const;

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(RefgraphCycle)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(RefgraphCycle)

  uint32_t VertexCount() const;
  bool IsTraversedByCC() const;
  already_AddRefed<RefgraphVertex> Vertex(uint32_t index) const;

  void GetName(nsString& retval) const;
};

class RefgraphController;


class Refgraph {

  friend class RefgraphVertex;
  friend class RefgraphEdge;
  friend class RefgraphController;
  friend class RefgraphCycle;

  nsRefPtr<RefgraphController> mParent;

  blocks_vector_t mBlocks;

  // parser state

  block_t* mCurrentBlock;
  ref_t* mCurrentRef;
  enum ParserState {
    ParserDefaultState,
    ParserInRefgraphDump,
    ParserInCCDump
  } mParserState;

  map_types_to_block_indices_t mMapTypesToBlockIndices;

  cycles_vector_t mCycles;

  class ScopedAssertWorkspacesClear {
    Refgraph* r;

public:
    ScopedAssertWorkspacesClear(Refgraph*);
    ~ScopedAssertWorkspacesClear();
  };
  friend class ScopedAssertWorkspacesClear;

  void AssertWorkspacesClear();

  bool ParseFile(const char* filename);
  void ResolveHereditaryStrongRefs();
  void RecurseCycles(
         uint32_t& vertex_index,
         uint32_t& cycle_index,
         index_vector_t& stack);
  void ComputeCycles();
  void ResolveBackRefs();
  bool LoadFromFile(const nsAString& filename);
  void PostProcess();

  bool HandleLine(const char* start, const char* end);
  bool HandleLine_b(const char* start, const char* end);
  bool HandleLine_c(const char* start, const char* end);
  bool HandleLine_e(const char* start, const char* end);
  bool HandleLine_f(const char* start, const char* end);
  bool HandleLine_m(const char* start, const char* end);
  bool HandleLine_n(const char* start, const char* end);
  bool HandleLine_s(const char* start, const char* end);
  bool HandleLine_t(const char* start, const char* end);
  bool HandleLine_u(const char* start, const char* end);
  bool HandleLine_v(const char* start, const char* end);
  bool HandleLine_w(const char* start, const char* end);
  bool HandleLine_star(const char* start, const char* end);

  template<typename T>
  bool ParseNumber(const char* start,
                   const char* end,
                   T* result,
                   const char** actualEnd = nullptr);

  Refgraph(RefgraphController* parent)
    : mParent(parent)
    , mCurrentBlock(nullptr)
    , mCurrentRef(nullptr)
    , mParserState(ParserDefaultState)
  {}

  ~Refgraph()
  {
  }

public:

  nsISupports *GetParentObject() const;

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope);

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(Refgraph)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(Refgraph)

  const blocks_vector_t& Blocks() const { return mBlocks; }

  void TypeSearch(const nsAString& query,
                  nsTArray<nsRefPtr<RefgraphTypeSearchResult> >& result);
  already_AddRefed<RefgraphVertex>
  FindVertex(const nsAString& typeName,
             RefgraphVertex* previousVertex);
  already_AddRefed<RefgraphVertex>
  FindVertex(uint64_t address);

  uint32_t CycleCount() const;
  already_AddRefed<RefgraphCycle> Cycle(uint32_t index);
};

class RefgraphController {

  nsRefPtr<nsISupports> mParent;

public:

  nsISupports *GetParentObject() const { return mParent; }

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope);

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(RefgraphController)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(RefgraphController)

  RefgraphController(nsISupports* aParent)
    : mParent(aParent)
  {}

  static already_AddRefed<RefgraphController>
  Constructor(mozilla::dom::GlobalObject&, mozilla::ErrorResult&);

  void SnapshotToFile(const nsAString& filename);

  already_AddRefed<Refgraph>
  LoadFromFile(const nsAString& filename);
};

} // namespace refgraph

#endif // Refgraph_h_
