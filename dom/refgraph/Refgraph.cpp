#include "Refgraph.h"

#include "nsContentUtils.h"

#include "mozmemory.h"

#include "mozilla/Assertions.h"

#include <cstdio>
#include <algorithm>

using namespace mozilla;
using namespace mozilla::dom;
using namespace refgraph;

template<typename T>
bool Refgraph::ParseNumber(const char* start,
                           const char* end,
                           T* result,
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

  T n = 0;
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
  uint32_t n;
  bool success = ParseNumber(start, end, &n);
  if (!success) {
    return false;
  }
  if (n >= mBlocks.size()) {
    return false;
  }
  if (mCurrentBlock) {
    mCurrentBlock->refs.shrink_to_fit();
    mCurrentBlock->stack.shrink_to_fit();
    mCurrentBlock->weakrefs.shrink_to_fit();
  }
  mCurrentBlock = &mBlocks[n];
  return true;
}

bool Refgraph::HandleLine_f(const char* start, const char* end)
{
  if (!mCurrentRef) {
    return false;
  }
  bool success = ParseNumber(start, end, &mCurrentRef->flags);
  if (!success) {
    return false;
  }
  if (mCurrentRef->flags > 0xf) {
    return false;
  }
  return true;
}

bool Refgraph::HandleLine_s(const char* start, const char* end)
{
  const char* pos = start;
  if (!mCurrentBlock) {
    return false;
  }
  mCurrentBlock->refs.push_back(ref_t());
  mCurrentRef = &mCurrentBlock->refs.back();
  bool success = ParseNumber(pos, end, &mCurrentRef->target, &pos) &&
                 ParseNumber(pos, end, &mCurrentRef->offset, &pos);
  if (!success) {
    return false;
  }
  if (mCurrentRef->target >= mBlocks.size()) {
    return false;
  }
  return true;
}

bool Refgraph::HandleLine_m(const char* start, const char* end)
{
  uint64_t address, size;
  const char* pos = start;
  bool success = ParseNumber(pos, end, &address, &pos) &&
                 ParseNumber(pos, end, &size);
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

bool Refgraph::HandleLine_n(const char* start, const char* end)
{
  const char* pos = start;
  while(*pos == ' ' && pos != end) {
    pos++;
  }

  if (pos == end) {
    return false;
  }

  if (!mCurrentRef) {
    return false;
  }
  mCurrentRef->refname.assign(pos, end - pos);
  return true;
}

bool Refgraph::HandleLine_t(const char* start, const char* end)
{
  const char* pos = start;
  while(*pos == ' ' && pos != end) {
    pos++;
  }

  if (pos == end) {
    return false;
  }

  if (!mCurrentBlock) {
    return false;
  }

  mCurrentBlock->type.assign(start, end - start);
  return true;
}

bool Refgraph::HandleLine_u(const char* start, const char* end)
{
  const char* pos = start;
  while(*pos == ' ' && pos != end) {
    pos++;
  }

  if (pos == end) {
    return false;
  }

  if (!mCurrentRef) {
    return false;
  }

  mCurrentRef->reftypename.assign(pos, end - pos);
  return true;
}

bool Refgraph::HandleLine_w(const char* start, const char* end)
{
  const char* pos = start;
  uint32_t target;
  bool success = ParseNumber(pos, end, &target, &pos);
  if (!success) {
    return false;
  }
  if (target >= mBlocks.size()) {
    return false;
  }
  if (!mCurrentBlock) {
    return false;
  }
  mCurrentBlock->weakrefs.push_back(target);
  return true;
}

bool Refgraph::HandleLine_c(const char* start, const char* end)
{
  uint32_t n;
  bool success = ParseNumber(start, end, &n);
  if (!success) {
    return false;
  }
  mBlocks.resize(n);
  mBlocks.shrink_to_fit();
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
    case 'c': return HandleLine_c(start, end);
    case 'f': return HandleLine_f(start, end);
    case 'm': return HandleLine_m(start, end);
    case 'n': return HandleLine_n(start, end);
    case 's': return HandleLine_s(start, end);
    case 't': return HandleLine_t(start, end);
    case 'u': return HandleLine_u(start, end);
    case 'w': return HandleLine_w(start, end);
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

void Refgraph::ResolveHereditaryStrongRefs()
{
  for (blocks_vector_t::iterator bi = mBlocks.begin();
       bi != mBlocks.end();
       ++bi)
  {
    for (refs_vector_t::iterator ri = bi->refs.begin();
         ri != bi->refs.end();
         ++ri)
    {
      if (ri->flags & hereditaryFlag) {
        ri->flags ^= hereditaryFlag;
        block_t& hereditary_block = mBlocks[ri->target];
        for (index_vector_t::iterator weakref_it = hereditary_block.weakrefs.begin();
            weakref_it != hereditary_block.weakrefs.end();
            ++weakref_it)
        {
          ref_t r;
          r.target = *weakref_it;
          r.flags = ri->flags;
          r.reftypename.assign("(inherited)");
          hereditary_block.refs.push_back(r);
        }
        hereditary_block.weakrefs.clear();
      }
    }
  }
}

void Refgraph::RecurseStronglyConnectedComponents(
                 uint32_t& v_index,
                 uint32_t& scc_index,
                 index_vector_t& stack)
{
  block_t& v = mBlocks[v_index];
  v.scc_index = scc_index;
  v.workspace = scc_index;
  scc_index++;
  stack.push_back(v_index);

  for (refs_vector_t::const_iterator ri = v.refs.begin();
       ri != v.refs.end();
       ++ri)
  {
    uint32_t w_index = ri->target;
    if (w_index == v_index) {
      continue;
    }
    block_t& w = mBlocks[w_index];
    if (!w.scc_index) {
      RecurseStronglyConnectedComponents(w_index, scc_index, stack);
      v.workspace = std::min(v.workspace, w.workspace);
    } else if (std::binary_search(stack.begin(), stack.end(), w_index)) {
      v.workspace = std::min(v.workspace, w.scc_index);
    }
  }

  if (v.workspace == v.scc_index) {
    uint32_t w_index = stack.back();
    index_vector_t scc;
    while (w_index != v_index) {
      stack.pop_back();
      scc.push_back(w_index);
      w_index = stack.back();
    }
    MOZ_ASSERT(stack.back() == v_index);
    stack.pop_back();
    if (scc.size()) {
      scc.push_back(v_index);
      mSCCs.push_back(scc);
    }
  }
}

void Refgraph::ComputeStronglyConnectedComponents()
{
  ScopedAssertWorkspacesClear sawc(this);

  index_vector_t stack;
  uint32_t scc_index = 1;

  for (uint32_t vertex_index = 0;
       vertex_index < mBlocks.size();
       ++vertex_index)
  {
    block_t& v = mBlocks[vertex_index];
    if (!v.scc_index) {
      RecurseStronglyConnectedComponents(vertex_index, scc_index, stack);
    }
  }

  for (blocks_vector_t::iterator bi = mBlocks.begin();
       bi != mBlocks.end();
       ++bi)
  {
    bi->scc_index = -1;
    bi->workspace = 0;
  }

  for (size_t i = 0; i < mSCCs.size(); i++) {
    index_vector_t& scc = mSCCs[i];
    for (size_t j = 0; j < scc.size(); j++) {
      mBlocks[scc[j]].scc_index = i;
    }
  }
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
  // can only be called once. If you want a new Refgraph, construct a new object.
  if (!mBlocks.empty()) {
    return false;
  }

  const char* buffer;
  size_t length;
  refgraph_dump_to_buffer(&buffer, &length);
  bool result = Parse(buffer, length);
  refgraph_uninstrumented_free((void*)buffer);
  if (!result) {
    mBlocks.clear();
    return false;
  }

  for (uint32_t index = 0;
       index < mBlocks.size();
       index++)
  {
    mMapTypesToBlockIndices.insert(map_types_to_block_indices_t::value_type(mBlocks[index].type, index));
  }

  ResolveHereditaryStrongRefs();
  ComputeStronglyConnectedComponents();

  return true;
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

already_AddRefed<RefgraphVertex>
Refgraph::FindVertex(uint64_t address)
{
  block_t b;
  b.address = address;
  blocks_vector_t::const_iterator it = std::lower_bound(mBlocks.begin(), mBlocks.end(), b);
  if (it == mBlocks.end() || it->address != address) {
    return nullptr;
  }
  nsRefPtr<RefgraphVertex> r = new RefgraphVertex(this, *it);
  return r.forget();
}

uint32_t Refgraph::SccCount() const {
  return mSCCs.size();
}

void Refgraph::Scc(uint32_t index, nsTArray<nsRefPtr<RefgraphVertex> >& result)
{
  if (index >= SccCount()) {
    return;
  }
  index_vector_t& scc = mSCCs[index];
  for (size_t i = 0; i < scc.size(); i++) {
    result.AppendElement(new RefgraphVertex(this, mBlocks[scc[i]]));
  }
}

RefgraphEdge::RefgraphEdge(Refgraph* parent, refs_vector_t::const_iterator ref)
  : mParent(parent)
  , mRef(ref)
{
}

already_AddRefed<RefgraphVertex>
RefgraphEdge::Target() const
{
  nsRefPtr<RefgraphVertex> r = new RefgraphVertex(mParent, mParent->mBlocks[mRef->target]);
  return r.forget();
}

bool RefgraphEdge::IsTraversedByCC() const {
  return mRef->flags & traversedByCCFlag;
}

void RefgraphEdge::GetRefName(nsString& retval) const {
  retval = NS_ConvertASCIItoUTF16(nsDependentCString(mRef->refname.c_str()));
}

void RefgraphEdge::GetRefTypeName(nsString& retval) const {
  retval = NS_ConvertASCIItoUTF16(nsDependentCString(mRef->reftypename.c_str()));
}

RefgraphVertex::RefgraphVertex(Refgraph* parent, map_types_to_block_indices_t::const_iterator block)
  : mParent(parent)
  , mBlockIterator(block)
  , mBlock(mParent->mBlocks[mBlockIterator->second])
{
}

RefgraphVertex::RefgraphVertex(Refgraph* parent, const block_t& block)
  : mParent(parent)
  , mBlockIterator(mParent->mMapTypesToBlockIndices.end())
  , mBlock(block)
{
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

uint32_t RefgraphVertex::SccIndex() const {
  return mBlock.scc_index;
}

already_AddRefed<RefgraphEdge>
RefgraphVertex::Edge(uint32_t index) const {
  nsRefPtr<RefgraphEdge> r = new RefgraphEdge(mParent, mBlock.refs.begin() + index);
  return r.forget();
}

already_AddRefed<RefgraphController>
RefgraphController::Constructor(nsISupports* aGlobal, mozilla::ErrorResult&) {
  nsRefPtr<RefgraphController> r = new RefgraphController(aGlobal);
  return r.forget();
}

already_AddRefed<Refgraph>
RefgraphController::Snapshot()
{
  nsRefPtr<Refgraph> r = new Refgraph(this);
  if (!r->Acquire()) {
    return nullptr;
  }
  return r.forget();
}

JSObject*
RefgraphController::WrapObject(JSContext *cx, JSObject *scope)
{
    return RefgraphControllerBinding::Wrap(cx, scope, this);
}

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(RefgraphController)
NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(RefgraphController)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphController, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphController, Release)

nsISupports*
Refgraph::GetParentObject() const {
  return mParent->GetParentObject();
}

JSObject*
Refgraph::WrapObject(JSContext *cx, JSObject *scope)
{
    return RefgraphBinding::Wrap(cx, scope, this);
}

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Refgraph)
NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Refgraph)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(Refgraph, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(Refgraph, Release)

JSObject*
RefgraphTypeSearchResult::WrapObject(JSContext *cx, JSObject *scope)
{
    return RefgraphTypeSearchResultBinding::Wrap(cx, scope, this);
}

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(RefgraphTypeSearchResult)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(RefgraphTypeSearchResult)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphTypeSearchResult, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphTypeSearchResult, Release)

JSObject*
RefgraphVertex::WrapObject(JSContext *cx, JSObject *scope)
{
    return dom::RefgraphVertexBinding::Wrap(cx, scope, this);
}

nsISupports*
RefgraphVertex::GetParentObject() const {
  return mParent->GetParentObject();
}

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(RefgraphVertex)
NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(RefgraphVertex)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphVertex, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphVertex, Release)

JSObject*
RefgraphEdge::WrapObject(JSContext *cx, JSObject *scope)
{
    return dom::RefgraphEdgeBinding::Wrap(cx, scope, this);
}

nsISupports*
RefgraphEdge::GetParentObject() const {
  return mParent->GetParentObject();
}

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(RefgraphEdge)
NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(RefgraphEdge)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphEdge, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphEdge, Release)
