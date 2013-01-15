#include "Refgraph.h"

#include "nsContentUtils.h"

#include "mozmemory.h"

#include "mozilla/Assertions.h"

#include <cxxabi.h>
#include <cstdio>
#include <algorithm>

using namespace mozilla;
using namespace mozilla::dom;
using namespace refgraph;

bool Refgraph::ParseNumber(const char* start,
                           const char* end,
                           uint64_t* result,
                           const char** actualEnd)
{
  const char* ptr = start;

  // typical case: exactly one space before the number
  if (MOZ_LIKELY(end - ptr > 1 &&
                 *ptr == ' ' &&
                 *(ptr+1) != ' '))
  {
    ptr++;
  }
  else
  {
    while(ptr != end && *ptr == ' ') {
      ptr++;
    }
    if (ptr == end) {
      return false;
    }
  }

  uint64_t n = 0;
  while(true) {
    char c = *ptr;
    char d;
    if (MOZ_LIKELY(c >= '0' && c <= '9')) {
      d = c - '0';
    } else if (MOZ_LIKELY(c >= 'a' && c <= 'f')) {
      d = c - 'a' + 0xa;
    } else if (MOZ_LIKELY(c == ' ')) {
      break;
    } else {
      return false;
    }
    n = (n << 4) + d;
    ptr++;
    if (MOZ_UNLIKELY(ptr == end)) {
      break;
    }
  }

  *result = n;
  if (actualEnd) {
    *actualEnd = ptr;
  }
  return true;
}

bool Refgraph::ParseTwoNumbers(const char* start,
                               const char* end,
                               uint64_t* result1,
                               uint64_t* result2)
{
  const char* afterFirst;
  return ParseNumber(start, end, result1, &afterFirst) &&
         ParseNumber(afterFirst, end, result2);
}

bool Refgraph::ParseThreeNumbers(const char* start,
                                 const char* end,
                                 uint64_t* result1,
                                 uint64_t* result2,
                                 uint64_t* result3)
{
  const char* afterFirst;
  const char* afterSecond;
  return ParseNumber(start, end, result1, &afterFirst) &&
         ParseNumber(afterFirst, end, result2, &afterSecond) &&
         ParseNumber(afterSecond, end, result3);
}

bool Refgraph::Demangle(const char* in, size_t length, string_t& out)
{
  const size_t inputBufferCapacity = length + 1;
  if (inputBufferCapacity > mDemanglingInputBufferCapacity) {
    mDemanglingInputBufferCapacity = inputBufferCapacity;
    mDemanglingInputBuffer = static_cast<char*>(
      realloc(mDemanglingInputBuffer, mDemanglingInputBufferCapacity));
  }
  const size_t outputBufferCapacity = std::max(size_t(4096), 2 * length);
  if (outputBufferCapacity > mDemanglingOutputBufferCapacity) {
    mDemanglingOutputBufferCapacity = outputBufferCapacity;
    mDemanglingOutputBuffer = static_cast<char*>(
      realloc(mDemanglingOutputBuffer, mDemanglingOutputBufferCapacity));
  }
  memcpy(mDemanglingInputBuffer, in, length);
  mDemanglingInputBuffer[length] = 0;
  int status = 0;
  size_t actualOutCapacity = mDemanglingOutputBufferCapacity;
  abi::__cxa_demangle(mDemanglingInputBuffer, mDemanglingOutputBuffer, &actualOutCapacity, &status);
  MOZ_ASSERT(actualOutCapacity == mDemanglingOutputBufferCapacity,
             "Thank you. Now go away.");
  if (status) {
    return false;
  }
  out.assign(mDemanglingOutputBuffer);
  return true;
}

bool Refgraph::HandleLine_a(const char* start, const char* end)
{
  uint64_t n;
  bool success = ParseNumber(start, end, &n);
  if (!success) {
    return false;
  }
  if (!mCurrentBlock) {
    return false;
  }
  mCurrentBlock->stack.push_back(n);
  return true;
}

bool Refgraph::HandleLine_b(const char* start, const char* end)
{
  uint64_t n;
  bool success = ParseNumber(start, end, &n);
  if (!success) {
    return false;
  }
  if (n >= mBlocks.size()) {
    return false;
  }
  mCurrentBlock = &mBlocks[n];
  return true;
}

bool Refgraph::HandleLine_r(const char* start, const char* end)
{
  uint64_t target, offset, flags;
  bool success = ParseThreeNumbers(start, end, &target, &offset, &flags);
  if (!success) {
    return false;
  }
  if (target >= mBlocks.size()) {
    return false;
  }
  if (!mCurrentBlock) {
    return false;
  }
  if (flags > 0xf) {
    return false;
  }
  mCurrentBlock->refs.push_back(ref_t(target, flags, offset));
  return true;
}

bool Refgraph::HandleLine_m(const char* start, const char* end)
{
  uint64_t address, size;
  bool success = ParseTwoNumbers(start, end, &address, &size);
  if (!success) {
    return false;
  }
  if (!mCurrentBlock) {
    return false;
  }
  mCurrentBlock->address = address;
  mCurrentBlock->size = size;
  return true;
}

bool Refgraph::HandleLine_t(const char* start, const char* end)
{
  while(*start == ' ' && start != end) {
    start++;
  }

  if (start == end) {
    return false;
  }

  if (!mCurrentBlock) {
    return false;
  }

  Demangle(start, end - start, mCurrentBlock->type);
  return true;
}

bool Refgraph::HandleLine_n(const char* start, const char* end)
{
  uint64_t n;
  bool success = ParseNumber(start, end, &n);
  if (!success) {
    return false;
  }
  mBlocks.resize(n);
  return true;
}

bool Refgraph::HandleLine(const char* start, const char* end)
{
  if (MOZ_UNLIKELY(*end != '\n')) {
    return false;
  }

  if (start == end) {
    return true;
  }

  MOZ_ASSERT(end > start);

  char c = *start;

  if (c == '#') {
    return true;
  }

  start++;

  if (MOZ_UNLIKELY(start == end)) {
    return false;
  }

  switch (c) {
    case 'a': return HandleLine_a(start, end);
    case 'b': return HandleLine_b(start, end);
    case 'm': return HandleLine_m(start, end);
    case 'n': return HandleLine_n(start, end);
    case 'r': return HandleLine_r(start, end);
    case 't': return HandleLine_t(start, end);
    default: return false;
  }
}

bool Refgraph::Parse(const char* buffer, size_t length)
{
  const char* line_start = buffer;
  const char* pos = buffer;
  const char* last = buffer + length - 1;
  size_t line_number = 0;

  if (!length ||
      *pos != '#' ||
      *last != '\n')
  {
    return false;
  }

  while (true) {
    if (MOZ_UNLIKELY(*pos == '\n')) {
      bool r = HandleLine(line_start, pos);
      if (MOZ_UNLIKELY(!r)) {
        size_t length = pos - line_start;
        int ilength = std::min(int(length), 256);
        fprintf(stderr, "Refgraph: error parsing line %lu (length %lu):\n%.*s\n\n",
                line_number, length, ilength, line_start);
        return false;
      }
      // Since we already checked that the last character is '\n',
      // it is enough to check for the end of the buffer here.
      if (MOZ_UNLIKELY(pos == last)) {
        return true;
      }
      line_start = pos + 1;
      ++line_number;
    }
    ++pos;
  }
}

bool Refgraph::ResolveHereditaryStrongRefs()
{
  ScopedAssertWorkspacesClear sawc(this);

  for (blocks_vector_t::iterator bi = mBlocks.begin();
       bi != mBlocks.end();
       ++bi)
  {
    for (refs_vector_t::iterator ri = bi->refs.begin();
       ri != bi->refs.end();
       ++ri)
    {
      if (ri->flags & hereditaryStrongRefFlag) {
        mBlocks[ri->target].workspace = 1;
        ri->flags ^= hereditaryStrongRefFlag;
        MOZ_ASSERT(ri->flags & strongRefFlag);
      }
    }
  }

  for (blocks_vector_t::iterator bi = mBlocks.begin();
       bi != mBlocks.end();
       ++bi)
  {
    if (!bi->workspace) {
      continue;
    }

    for (refs_vector_t::iterator ri = bi->refs.begin();
         ri != bi->refs.end();
         ++ri)
    {
      ri->flags |= strongRefFlag;
    }

    bi->workspace = 0;
  }

  return true;
}

void Refgraph::AssertWorkspacesClear()
{
#ifdef DEBUG
  for (blocks_vector_t::iterator bi = mBlocks.begin();
       bi != mBlocks.end();
       ++bi)
  {
    MOZ_ASSERT(!bi->workspace);
  }
#endif
}

Refgraph::ScopedAssertWorkspacesClear::ScopedAssertWorkspacesClear(Refgraph* _r)
  : r(_r)
{
  r->AssertWorkspacesClear();
}

Refgraph::ScopedAssertWorkspacesClear::~ScopedAssertWorkspacesClear()
{
  r->AssertWorkspacesClear();
}

bool Refgraph::Acquire()
{
  const char* buffer;
  size_t length;
  refgraph_dump_to_buffer(&buffer, &length);
  bool result = Parse(buffer, length);
  refgraph_uninstrumented_free((void*)buffer);
  if (!result) {
    mBlocks.clear();
    return false;
  }

  for (block_index_t index = 0;
       index < mBlocks.size();
       index++)
  {
    mMapTypesToBlockIndices.insert(map_types_to_block_indices_t::value_type(mBlocks[index].type, index));
  }

  return ResolveHereditaryStrongRefs();
}

already_AddRefed<Refgraph>
Refgraph::Constructor(nsISupports*, mozilla::ErrorResult&) {
  nsRefPtr<Refgraph> r = new Refgraph;
  r->Acquire();
  return r.forget();
}

void Refgraph::TypeSearch(const nsAString& name, nsTArray<nsRefPtr<RefgraphTypeSearchResult> >& retval)
{
  string_t t(NS_LossyConvertUTF16toASCII(name).get());
  for (map_types_to_block_indices_t::const_iterator it = mMapTypesToBlockIndices.begin();
       it != mMapTypesToBlockIndices.end();
       it = mMapTypesToBlockIndices.upper_bound(it->first))
  {
    if (it->first.find(t) != string_t::npos) {
      retval.AppendElement(new RefgraphTypeSearchResult(
        mMapTypesToBlockIndices.count(it->first),
        NS_ConvertASCIItoUTF16(nsDependentCString(it->first.c_str()))));
    }
  }
  retval.Sort(RefgraphTypeSearchResult::Comparator());
}

already_AddRefed<RefgraphVertex>
Refgraph::FindVertex(const nsAString& typeName,
                     RefgraphVertex* previousVertex)
{
  string_t t(NS_LossyConvertUTF16toASCII(typeName).get());
  map_types_to_block_indices_t::const_iterator it;
  if (previousVertex && previousVertex->mBlockIterator != mMapTypesToBlockIndices.end()) {
    it = previousVertex->mBlockIterator;
    ++it;
  } else {
    it = mMapTypesToBlockIndices.lower_bound(t);
  }
  if (it == mMapTypesToBlockIndices.end() ||
      it->first != t)
  {
    return nullptr;
  }
  nsRefPtr<RefgraphVertex> r = new RefgraphVertex(this, it);
  return r.forget();
}

RefgraphEdge::RefgraphEdge(Refgraph* parent, refs_vector_t::const_iterator ref)
  : mParent(parent)
  , mRef(ref)
{
  SetIsDOMBinding();
}

already_AddRefed<RefgraphVertex>
RefgraphEdge::Target() const
{
  nsRefPtr<RefgraphVertex> r = new RefgraphVertex(mParent, mParent->mBlocks[mRef->target]);
  return r.forget();
}

bool RefgraphEdge::IsStrong() const {
  return mRef->flags & strongRefFlag;
}

bool RefgraphEdge::IsTraversedByCC() const {
  return mRef->flags & traversedByCCFlag;
}

RefgraphVertex::RefgraphVertex(Refgraph* parent, map_types_to_block_indices_t::const_iterator block)
  : mParent(parent)
  , mBlockIterator(block)
  , mBlock(mParent->mBlocks[mBlockIterator->second])
{
  SetIsDOMBinding();
}

RefgraphVertex::RefgraphVertex(Refgraph* parent, const block_t& block)
  : mParent(parent)
  , mBlockIterator(mParent->mMapTypesToBlockIndices.end())
  , mBlock(block)
{
  SetIsDOMBinding();
}


uint64_t RefgraphVertex::Address() const {
  return mBlock.address;
}

uint64_t RefgraphVertex::Size() const {
  return mBlock.size;
}

void RefgraphVertex::GetTypeName(nsString& retval) const {
  retval = NS_ConvertASCIItoUTF16(nsDependentCString(mBlock.type.c_str()));
}

uint32_t RefgraphVertex::EdgeCount() const {
  return mBlock.refs.size();
}

already_AddRefed<RefgraphEdge>
RefgraphVertex::Edge(uint32_t index) const {
  nsRefPtr<RefgraphEdge> r = new RefgraphEdge(mParent, mBlock.refs.begin() + index);
  return r.forget();
}

JSObject*
Refgraph::WrapObject(JSContext *cx, JSObject *scope, bool*)
{
    return RefgraphBinding::Wrap(cx, scope, this);
}

NS_IMPL_CYCLE_COLLECTION_NATIVE_CLASS(Refgraph)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Refgraph)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Refgraph)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(Refgraph, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(Refgraph, Release)


JSObject*
RefgraphTypeSearchResult::WrapObject(JSContext *cx, JSObject *scope)
{
    return RefgraphTypeSearchResultBinding::Wrap(cx, scope, this);
}

NS_IMPL_CYCLE_COLLECTION_NATIVE_CLASS(RefgraphTypeSearchResult)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(RefgraphTypeSearchResult)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(RefgraphTypeSearchResult)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphTypeSearchResult, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphTypeSearchResult, Release)

JSObject*
RefgraphVertex::WrapObject(JSContext *cx, JSObject *scope, bool *triedToWrap)
{
    return dom::RefgraphVertexBinding::Wrap(cx, scope, this, triedToWrap);
}

nsISupports*
RefgraphVertex::GetParentObject() const {
  return mParent->GetParentObject();
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_1(RefgraphVertex, mParent)

NS_IMPL_CYCLE_COLLECTING_ADDREF(RefgraphVertex)
NS_IMPL_CYCLE_COLLECTING_RELEASE(RefgraphVertex)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(RefgraphVertex)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

JSObject*
RefgraphEdge::WrapObject(JSContext *cx, JSObject *scope, bool *triedToWrap)
{
    return dom::RefgraphEdgeBinding::Wrap(cx, scope, this, triedToWrap);
}

nsISupports*
RefgraphEdge::GetParentObject() const {
  return mParent->GetParentObject();
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_1(RefgraphEdge, mParent)

NS_IMPL_CYCLE_COLLECTING_ADDREF(RefgraphEdge)
NS_IMPL_CYCLE_COLLECTING_RELEASE(RefgraphEdge)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(RefgraphEdge)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

