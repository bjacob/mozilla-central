#include "Refgraph.h"

#include "nsContentUtils.h"
#include "nsJSEnvironment.h"
#include "nsICycleCollectorListener.h"

#include "mozmemory.h"

#include "mozilla/Assertions.h"

#include <cstdio>
#include <algorithm>

#include <unistd.h>
#include <fcntl.h>

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
#ifndef ANDROID
  if (mCurrentBlock) {
    mCurrentBlock->edges.shrink_to_fit();
    mCurrentBlock->weakrefs.shrink_to_fit();
    mCurrentBlock->backrefs.shrink_to_fit();
    mCurrentBlock->backweakrefs.shrink_to_fit();
  }
#endif
  mCurrentBlock = &mBlocks[n];
  mCurrentEdge = nullptr;
  mCurrentRefInfo = nullptr;
  return true;
}

bool Refgraph::HandleLine_f(const char* start, const char* end)
{
  if (!mCurrentEdge) {
    return false;
  }
  uint8_t flags;
  bool success = ParseNumber(start, end, &flags);
  if (!success) {
    return false;
  }
  if (flags > 0xf) {
    return false;
  }
  mCurrentEdge->flags |= flags;
  return true;
}

bool Refgraph::HandleLine_s(const char* start, const char* end)
{
  const char* pos = start;
  if (!mCurrentBlock) {
    return false;
  }
  uint32_t target;
  uint64_t offset;

  bool success = ParseNumber(pos, end, &target, &pos) &&
                 ParseNumber(pos, end, &offset, &pos);

  if (!success) {
    return false;
  }
  if (target >= mBlocks.size()) {
    return false;
  }

  if (mCurrentEdge && mCurrentEdge->target > target) {
    // input data breaks our assumption that edges are sorted by increasing target
    return false;
  }

  if (!mCurrentEdge || mCurrentEdge->target != target) {
    // we're going to start a new edge, instead of just appending a ref_info_t to
    // the current edge.
    mCurrentBlock->edges.push_back(edge_t());
    mCurrentEdge = &mCurrentBlock->edges.back();
    mCurrentEdge->target = target;
  }

  // in any case, we are definitely starting a new ref_info_t.
  mCurrentEdge->ref_infos.push_back(ref_info_t());
  mCurrentRefInfo = &mCurrentEdge->ref_infos.back();
  mCurrentRefInfo->offset = offset;

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

  mCurrentBlock->type.assign(pos, end - pos);
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

  if (!mCurrentRefInfo) {
    return false;
  }

  mCurrentRefInfo->type.assign(pos, end - pos);
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
#ifndef ANDROID
  mBlocks.shrink_to_fit();
#endif
  return true;
}

bool Refgraph::HandleLine_v(const char* start, const char* end)
{
  const char* pos = start;
  uint64_t address;
  bool success = ParseNumber(pos, end, &address, &pos);
  if (!success) {
    return false;
  }
  if (pos == end) {
    return false;
  }
  while(*pos == ' ' && pos != end) {
    pos++;
  }
  block_t b;
  b.address = address;
  blocks_vector_t::iterator it = std::lower_bound(mBlocks.begin(), mBlocks.end(), b);
  if (it == mBlocks.end() ||
      address < it->address ||
      address >= it->address + it->size)
  {
    // be graceful on not-found vertex: could conceivably be a non-heap vertex (?)
    return true;
  }
  mCurrentBlock = &mBlocks[it - mBlocks.begin()];

  // overwrite any type information we had --- the CC knows better, when it knows
  mCurrentBlock->type.assign(pos, end - pos);
  return true;
}

bool Refgraph::HandleLine_e(const char* start, const char* end)
{
  const char* pos = start;
  uint64_t address;
  bool success = ParseNumber(pos, end, &address, &pos);
  if (!success) {
    return false;
  }
  if (pos == end) {
    return false;
  }
  while(*pos == ' ' && pos != end) {
    pos++;
  }
  if (!mCurrentBlock) {
    return false;
  }
  for (edges_vector_t::iterator it1 = mCurrentBlock->edges.begin();
      it1 != mCurrentBlock->edges.end();
      ++it1)
  {
    block_t& targetBlock1 = mBlocks[it1->target];
    if (targetBlock1.contains(address)) {
      it1->flags |= traversedByCCFlag;
      it1->ccname.assign(pos, end - pos);
    }
    for (edges_vector_t::iterator it2 = targetBlock1.edges.begin();
        it2 != targetBlock1.edges.end();
        ++it2)
    {
      block_t& targetBlock2 = mBlocks[it2->target];
      if (targetBlock2.contains(address)) {
        it2->flags |= traversedByCCFlag;
        it2->ccname.assign(pos, end - pos);
      }
    }
  }
  return true;
}

bool Refgraph::HandleLine_star(const char* start, const char* end)
{
  size_t n = end - start;
  const size_t bufsize = 0x4000;
  if (n == 0 || n >= bufsize) {
    return false;
  }
  char buf[bufsize];
  memcpy(buf, start, n);
  buf[n] = 0;
  if (strstr(buf, "BEGIN REFGRAPH DUMP")) {
    if (mParserState != ParserDefaultState) {
      return false;
    }
    mParserState = ParserInRefgraphDump;
  } else if (strstr(buf, "END REFGRAPH DUMP")) {
    if (mParserState != ParserInRefgraphDump) {
      return false;
    }
    mParserState = ParserDefaultState;
  } else if (strstr(buf, "BEGIN CC DUMP")) {
    if (mParserState != ParserDefaultState) {
      return false;
    }
    mParserState = ParserInCCDump;
  } else if (strstr(buf, "END CC DUMP")) {
    if (mParserState != ParserInCCDump) {
      return false;
    }
    mParserState = ParserDefaultState;
  }
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

  switch (mParserState) {
    case ParserDefaultState:
      switch (c) {
        case '*': return HandleLine_star(start, end);
        default: return false;
      }
    case ParserInRefgraphDump:
      switch (c) {
        case 'b': return HandleLine_b(start, end);
        case 'c': return HandleLine_c(start, end);
        case 'f': return HandleLine_f(start, end);
        case 'm': return HandleLine_m(start, end);
        case 's': return HandleLine_s(start, end);
        case 't': return HandleLine_t(start, end);
        case 'u': return HandleLine_u(start, end);
        case 'w': return HandleLine_w(start, end);
        case '*': return HandleLine_star(start, end);
        default: return false;
      }
    case ParserInCCDump:
      switch (c) {
        case 'v': return HandleLine_v(start, end);
        case 'e': return HandleLine_e(start, end);
        case '*': return HandleLine_star(start, end);
        default: return false;
      }
    default:
      return false;
  }
}

bool Refgraph::ParseFile(const char* filename)
{
  FILE* file = fopen(filename, "r");

  if (!file) {
    fprintf(stderr, "Refgraph: error opening file %s\n", filename);
    return nullptr;
  }

  const size_t line_max_len = 0x4000;
  char line[line_max_len + 1];
  size_t line_number = 0;

  while (fgets(line, line_max_len, file)) {
    size_t len = strlen(line);
    bool r = len && HandleLine(line, line + len - 1);
    ++line_number;
    if (MOZ_UNLIKELY(!r)) {
      fprintf(stderr, "Refgraph: error parsing line %u (length %u):\n%s\n\n",
              unsigned(line_number), unsigned(len), line);
      fclose(file);
      return false;
    }
  }
  fclose(file);
  return true;
}

void Refgraph::ResolveHereditaryStrongRefs()
{
  for (blocks_vector_t::iterator bi = mBlocks.begin();
       bi != mBlocks.end();
       ++bi)
  {
    for (edges_vector_t::iterator ri = bi->edges.begin();
         ri != bi->edges.end();
         ++ri)
    {
      if (ri->flags & hereditaryFlag) {
        ri->flags ^= hereditaryFlag;
        block_t& hereditary_block = mBlocks[ri->target];
        for (index_vector_t::iterator weakref_it = hereditary_block.weakrefs.begin();
            weakref_it != hereditary_block.weakrefs.end();
            ++weakref_it)
        {
          edge_t e;
          e.target = *weakref_it;

          edges_vector_t::iterator ei =
            std::lower_bound(hereditary_block.edges.begin(), hereditary_block.edges.end(), e);
          if (ei == hereditary_block.edges.end() || ei->target != e.target) {
            edges_vector_t::iterator new_edge = hereditary_block.edges.insert(ei, e);
            new_edge->flags = ri->flags;
            new_edge->ccname.assign("(hereditary)");
            // leave ref_infos empty
          }
        }
        hereditary_block.weakrefs.clear();
      }
    }
  }
}

void Refgraph::RecurseCycles(
                 uint32_t& v_index,
                 uint32_t& cycle_index,
                 index_vector_t& stack)
{
  block_t& v = mBlocks[v_index];
  v.cycle_index = cycle_index;
  v.workspace = cycle_index;
  cycle_index++;
  stack.push_back(v_index);

  for (edges_vector_t::const_iterator ri = v.edges.begin();
       ri != v.edges.end();
       ++ri)
  {
    uint32_t w_index = ri->target;
    if (w_index == v_index) {
      continue;
    }
    block_t& w = mBlocks[w_index];

    if (!w.cycle_index) {
      RecurseCycles(w_index, cycle_index, stack);
      v.workspace = std::min(v.workspace, w.workspace);
    } else if (std::find(stack.begin(), stack.end(), w_index) != stack.end()) {
      v.workspace = std::min(v.workspace, w.cycle_index);
    }
  }

  if (v.workspace == v.cycle_index) {
    uint32_t w_index = stack.back();
    index_vector_t cycle_vertices;
    while (w_index != v_index) {
      stack.pop_back();
      cycle_vertices.push_back(w_index);
      w_index = stack.back();
    }
    MOZ_ASSERT(stack.back() == v_index);
    stack.pop_back();
    if (cycle_vertices.size()) {
      mCycles.push_back(cycle_t());
      mCycles.back().vertices = cycle_vertices;
      mCycles.back().vertices.push_back(v_index);
    }
  }
}

struct sort_vertices_by_type_functor {
  Refgraph* mParent;

  sort_vertices_by_type_functor(Refgraph* parent)
    : mParent(parent)
  {}

  bool operator()(uint32_t a, uint32_t b) const {
    return mParent->Blocks()[a].type < mParent->Blocks()[b].type;
  }
};

void Refgraph::ComputeCycles()
{
  ScopedAssertWorkspacesClear sawc(this);

  index_vector_t stack;
  uint32_t cycle_index = 1;

  for (uint32_t vertex_index = 0;
       vertex_index < mBlocks.size();
       ++vertex_index)
  {
    block_t& v = mBlocks[vertex_index];
    if (!v.cycle_index) {
      RecurseCycles(vertex_index, cycle_index, stack);
    }
  }

  for (blocks_vector_t::iterator bi = mBlocks.begin();
       bi != mBlocks.end();
       ++bi)
  {
    bi->cycle_index = -1;
    bi->workspace = 0;
  }

  for (size_t i = 0; i < mCycles.size(); i++) {
    index_vector_t& cycle_vertices = mCycles[i].vertices;
    for (size_t j = 0; j < cycle_vertices.size(); j++) {
      mBlocks[cycle_vertices[j]].cycle_index = i;
    }
  }

  for (size_t c = 0; c < mCycles.size(); c++)
  {
    cycle_t& cycle = mCycles[c];

    std::sort(cycle.vertices.begin(), cycle.vertices.end(),
              sort_vertices_by_type_functor(this));

    cycle.isTraversedByCC = true;
    const index_vector_t& cycle_vertices = mCycles[c].vertices;
    for (size_t v = 0; v < cycle_vertices.size(); v++) {
      const block_t& b = mBlocks[cycle_vertices[v]];
      for (edges_vector_t::const_iterator ri = b.edges.begin();
          ri != b.edges.end();
          ++ri)
      {
        if (!(ri->flags & traversedByCCFlag) &&
            ri->target != cycle_vertices[v]) /* ignore self-references. Should we? FIXME */
        {
          const block_t& target_block = mBlocks[ri->target];
          if (target_block.cycle_index == b.cycle_index) {
            cycle.isTraversedByCC = false;
            break;
          }
        }
      }
      if (!cycle.isTraversedByCC) {
        break;
      }
    }

    cycle.name.clear();
    cycle.name.reserve(8 * cycle.vertices.size());
    string_t last_type;
    for (index_vector_t::const_iterator it = cycle.vertices.begin();
        it != cycle.vertices.end();
        ++it)
    {
      if (mBlocks[*it].type != last_type) {
        const string_t& this_type = mBlocks[*it].type;
        if (it != cycle.vertices.begin()) {
          cycle.name += " / ";
        }
        if (this_type.empty()) {
          cycle.name += "(unknown)";
        } else {
          cycle.name += this_type;
        }
        last_type = this_type;
      }
    }
  }

  std::sort(mCycles.begin(), mCycles.end());

  for (size_t i = 0; i < mCycles.size(); i++) {
    index_vector_t& cycle_vertices = mCycles[i].vertices;
    for (size_t j = 0; j < cycle_vertices.size(); j++) {
      mBlocks[cycle_vertices[j]].cycle_index = i;
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

void Refgraph::ResolveBackRefs()
{
  for (uint32_t b = 0;
       b < mBlocks.size();
       ++b)
  {
    for (edges_vector_t::iterator ri = mBlocks[b].edges.begin();
         ri != mBlocks[b].edges.end();
         ++ri)
    {
      mBlocks[ri->target].backrefs.push_back(b);
    }

    for (index_vector_t::iterator wi = mBlocks[b].weakrefs.begin();
         wi != mBlocks[b].weakrefs.end();
         ++wi)
    {
      mBlocks[*wi].backweakrefs.push_back(b);
    }
  }
}

void Refgraph::PostProcess()
{
  for (uint32_t index = 0;
       index < mBlocks.size();
       index++)
  {
    mMapTypesToBlockIndices.insert(map_types_to_block_indices_t::value_type(mBlocks[index].type, index));
  }

  ResolveHereditaryStrongRefs();
  ComputeCycles();
  ResolveBackRefs();
}

bool Refgraph::LoadFromFile(const nsAString& filename)
{
  // can only be called once. If you want a new Refgraph, construct a new object.
  if (!mBlocks.empty()) {
    return false;
  }

  bool result = ParseFile(NS_LossyConvertUTF16toASCII(filename).get());
  if (!result) {
    mBlocks.clear();
    return false;
  }

  PostProcess();
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

uint32_t Refgraph::CycleCount() const {
  return mCycles.size();
}

already_AddRefed<RefgraphCycle> Refgraph::Cycle(uint32_t index)
{
  if (index >= CycleCount()) {
    return nullptr;
  }
  nsRefPtr<RefgraphCycle> r = new RefgraphCycle(this, index);
  return r.forget();
}

RefgraphCycle::RefgraphCycle(Refgraph* parent, uint32_t index)
  : mParent(parent)
  , mIndex(index)
{
}

bool RefgraphCycle::IsTraversedByCC() const
{
  return mParent->mCycles[mIndex].isTraversedByCC;
}

uint32_t RefgraphCycle::VertexCount() const
{
  return mParent->mCycles[mIndex].vertices.size();
}

already_AddRefed<RefgraphVertex> RefgraphCycle::Vertex(uint32_t index) const
{
  if (index >= VertexCount()) {
    return nullptr;
  }
  uint32_t block_index = mParent->mCycles[mIndex].vertices[index];
  nsRefPtr<RefgraphVertex> r = new RefgraphVertex(mParent, mParent->mBlocks[block_index]);
  return r.forget();
}

void RefgraphCycle::GetName(nsString& retval) const {
  retval = NS_ConvertASCIItoUTF16(nsDependentCString(mParent->mCycles[mIndex].name.c_str()));
}

RefgraphEdge::RefgraphEdge(Refgraph* parent, edges_vector_t::const_iterator edge)
  : mParent(parent)
  , mEdge(edge)
{
}

already_AddRefed<RefgraphVertex>
RefgraphEdge::Target() const
{
  nsRefPtr<RefgraphVertex> r = new RefgraphVertex(mParent, mParent->mBlocks[mEdge->target]);
  return r.forget();
}

bool RefgraphEdge::IsTraversedByCC() const {
  return mEdge->flags & traversedByCCFlag;
}

void RefgraphEdge::GetCCName(nsString& retval) const {
  retval = NS_ConvertASCIItoUTF16(nsDependentCString(mEdge->ccname.c_str()));
}

uint32_t RefgraphEdge::RefInfoCount() const {
  return mEdge->ref_infos.size();
}

already_AddRefed<RefgraphEdgeRefInfo>
RefgraphEdge::RefInfo(uint32_t index) const
{
  nsRefPtr<RefgraphEdgeRefInfo> r = new RefgraphEdgeRefInfo(mParent, mEdge->ref_infos.begin() + index);
  return r.forget();
}

RefgraphEdgeRefInfo::RefgraphEdgeRefInfo(Refgraph* parent, ref_infos_vector_t::const_iterator refInfo)
  : mParent(parent)
  , mRefInfo(refInfo)
{
}

uint64_t RefgraphEdgeRefInfo::Offset() const {
  return mRefInfo->offset;
}

void RefgraphEdgeRefInfo::GetTypeName(nsString& retval) const {
  retval = NS_ConvertASCIItoUTF16(nsDependentCString(mRefInfo->type.c_str()));
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
  return mBlock.edges.size();
}

uint32_t RefgraphVertex::CycleIndex() const {
  return mBlock.cycle_index;
}

already_AddRefed<RefgraphEdge>
RefgraphVertex::Edge(uint32_t index) const {
  if (index >= EdgeCount()) {
    return nullptr;
  }
  nsRefPtr<RefgraphEdge> r = new RefgraphEdge(mParent, mBlock.edges.begin() + index);
  return r.forget();
}

uint32_t
RefgraphVertex::WeakEdgeCount() const
{
  return mBlock.weakrefs.size();
}

uint32_t
RefgraphVertex::BackEdgeCount() const
{
  return mBlock.backrefs.size();
}

uint32_t
RefgraphVertex::BackWeakEdgeCount() const
{
  return mBlock.backweakrefs.size();
}

already_AddRefed<RefgraphVertex>
RefgraphVertex::WeakEdge(uint32_t index) const
{
  if (index >= WeakEdgeCount()) {
    return nullptr;
  }
  nsRefPtr<RefgraphVertex> r = new RefgraphVertex(mParent, mParent->mBlocks[mBlock.weakrefs[index]]);
  return r.forget();
}

already_AddRefed<RefgraphVertex>
RefgraphVertex::BackEdge(uint32_t index) const
{
  if (index >= BackEdgeCount()) {
    return nullptr;
  }
  nsRefPtr<RefgraphVertex> r = new RefgraphVertex(mParent, mParent->mBlocks[mBlock.backrefs[index]]);
  return r.forget();
}

already_AddRefed<RefgraphVertex>
RefgraphVertex::BackWeakEdge(uint32_t index) const
{
  if (index >= BackWeakEdgeCount()) {
    return nullptr;
  }
  nsRefPtr<RefgraphVertex> r = new RefgraphVertex(mParent, mParent->mBlocks[mBlock.backweakrefs[index]]);
  return r.forget();
}

already_AddRefed<RefgraphController>
RefgraphController::Constructor(mozilla::dom::GlobalObject& aGlobal, mozilla::ErrorResult&) {
  nsRefPtr<RefgraphController> r = new RefgraphController(aGlobal.GetAsSupports());
  return r.forget();
}

static void RefgraphCCDump(FILE* f);

already_AddRefed<Refgraph>
RefgraphController::LoadFromFile(const nsAString& filename)
{
  nsRefPtr<Refgraph> r = new Refgraph(this);
  if (!r->LoadFromFile(filename)) {
    return nullptr;
  }
  return r.forget();
}

void RefgraphController::SnapshotToFile(const nsAString& filename) {
  nsCString cfilename = NS_LossyConvertUTF16toASCII(filename);
  FILE* f = fopen(cfilename.get(), "w");
  if (!f) {
    fprintf(stderr, "could not open %s for writing\n", cfilename.get());
    return;
  }
  refgraph_dump(fileno(f));
  RefgraphCCDump(f);
  fclose(f);
}

JSObject*
RefgraphController::WrapObject(JSContext *cx, JS::Handle<JSObject*> scope)
{
    return RefgraphControllerBinding::Wrap(cx, scope, this);
}

NS_IMPL_CYCLE_COLLECTION_1(RefgraphController, mParent)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphController, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphController, Release)

nsISupports*
Refgraph::GetParentObject() const {
  return mParent->GetParentObject();
}

JSObject*
Refgraph::WrapObject(JSContext *cx, JS::Handle<JSObject*> scope)
{
    return RefgraphBinding::Wrap(cx, scope, this);
}

NS_IMPL_CYCLE_COLLECTION_1(Refgraph, mParent)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(Refgraph, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(Refgraph, Release)

JSObject*
RefgraphTypeSearchResult::WrapObject(JSContext *cx, JS::Handle<JSObject*> scope)
{
    return RefgraphTypeSearchResultBinding::Wrap(cx, scope, this);
}

NS_IMPL_CYCLE_COLLECTION_1(RefgraphTypeSearchResult, mDummy)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphTypeSearchResult, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphTypeSearchResult, Release)

JSObject*
RefgraphVertex::WrapObject(JSContext *cx, JS::Handle<JSObject*> scope)
{
    return dom::RefgraphVertexBinding::Wrap(cx, scope, this);
}

nsISupports*
RefgraphVertex::GetParentObject() const {
  return mParent->GetParentObject();
}

NS_IMPL_CYCLE_COLLECTION_1(RefgraphVertex, mParent)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphVertex, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphVertex, Release)

JSObject*
RefgraphEdge::WrapObject(JSContext *cx, JS::Handle<JSObject*> scope)
{
    return dom::RefgraphEdgeBinding::Wrap(cx, scope, this);
}

nsISupports*
RefgraphEdge::GetParentObject() const {
  return mParent->GetParentObject();
}

NS_IMPL_CYCLE_COLLECTION_1(RefgraphEdge, mParent)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphEdge, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphEdge, Release)

JSObject*
RefgraphEdgeRefInfo::WrapObject(JSContext *cx, JS::Handle<JSObject*> scope)
{
    return dom::RefgraphEdgeRefInfoBinding::Wrap(cx, scope, this);
}

nsISupports*
RefgraphEdgeRefInfo::GetParentObject() const {
  return mParent->GetParentObject();
}

NS_IMPL_CYCLE_COLLECTION_1(RefgraphEdgeRefInfo, mParent)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphEdgeRefInfo, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphEdgeRefInfo, Release)

JSObject*
RefgraphCycle::WrapObject(JSContext *cx, JS::Handle<JSObject*> scope)
{
    return dom::RefgraphCycleBinding::Wrap(cx, scope, this);
}

nsISupports*
RefgraphCycle::GetParentObject() const {
  return mParent->GetParentObject();
}

NS_IMPL_CYCLE_COLLECTION_1(RefgraphCycle, mParent)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(RefgraphCycle, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(RefgraphCycle, Release)


class RefgraphCCDumper MOZ_FINAL : public nsICycleCollectorListener
{
public:
    RefgraphCCDumper(FILE* file)
      : mFile(file)
      , mPrintEdges(false)
    {}

    ~RefgraphCCDumper()
    {}

    NS_DECL_ISUPPORTS

    NS_IMETHOD AllTraces(nsICycleCollectorListener** aListener)
    {
        NS_ADDREF(*aListener = this);
        return NS_OK;
    }

    NS_IMETHOD GetWantAllTraces(bool* aAllTraces)
    {
        *aAllTraces = true;
        return NS_OK;
    }

    NS_IMETHOD GetDisableLog(bool* aDisableLog)
    {
        *aDisableLog = true;
        return NS_OK;
    }

    NS_IMETHOD SetDisableLog(bool)
    {
        return NS_OK;
    }

    NS_IMETHOD GetWantAfterProcessing(bool* aWantAfterProcessing)
    {
        *aWantAfterProcessing = false;
        return NS_OK;
    }

    NS_IMETHOD SetWantAfterProcessing(bool)
    {
        return NS_OK;
    }

    NS_IMETHOD GetFilenameIdentifier(nsAString& aIdentifier)
    {
        aIdentifier.Truncate();
        return NS_OK;
    }

    NS_IMETHOD SetFilenameIdentifier(const nsAString&)
    {
        return NS_OK;
    }

    NS_IMETHOD Begin()
    {
        int ret = fprintf(mFile, "*** BEGIN CC DUMP ***\n\n");
        MOZ_ASSERT(ret > 0);
        (void) ret;
        return NS_OK;
    }

    NS_IMETHOD NoteRefCountedObject(uint64_t aAddress, uint32_t refCount,
                                    const char *aObjectDescription)
    {
        int ret = fprintf(mFile, "v %llx %s\n", (long long unsigned) aAddress, aObjectDescription);
        MOZ_ASSERT(ret > 0);
        (void) ret;
        mPrintEdges = true;
        (void) refCount;
        return NS_OK;
    }
    NS_IMETHOD NoteGCedObject(uint64_t aAddress, bool aMarked,
                              const char *aObjectDescription)
    {
        mPrintEdges = false;
        return NS_OK;
    }
    NS_IMETHOD NoteEdge(uint64_t aToAddress, const char *aEdgeName)
    {
        if (mPrintEdges) {
          int ret = fprintf(mFile, "e %llx %s\n", (long long unsigned) aToAddress, aEdgeName);
          MOZ_ASSERT(ret > 0);
          (void) ret;
        }
        return NS_OK;
    }
    NS_IMETHOD NoteWeakMapEntry(uint64_t aMap, uint64_t aKey,
                                uint64_t aKeyDelegate, uint64_t aValue)
    {
        return NS_OK;
    }
    NS_IMETHOD BeginResults()
    {
        return NS_OK;
    }
    NS_IMETHOD DescribeRoot(uint64_t, uint32_t)
    {
        return NS_OK;
    }
    NS_IMETHOD DescribeGarbage(uint64_t)
    {
        return NS_OK;
    }
    NS_IMETHOD End()
    {
        int ret = fprintf(mFile, "*** END CC DUMP ***\n\n");
        MOZ_ASSERT(ret > 0);
        (void) ret;
        return NS_OK;
    }
    NS_IMETHOD ProcessNext(nsICycleCollectorHandler*,
                           bool*)
    {
        return NS_ERROR_FAILURE;
    }

    NS_IMETHOD NoteGCedObject(uint64_t, bool, const char*, uint64_t)
    {
        return NS_OK;
    }

private:
    FILE* mFile;
    bool mPrintEdges;
};

NS_IMPL_ISUPPORTS1(RefgraphCCDumper, nsICycleCollectorListener)

static void RefgraphCCDump(FILE* f)
{
    nsCOMPtr<nsICycleCollectorListener> listener = new RefgraphCCDumper(f);
    nsJSContext::CycleCollectNow(listener, -1);
}
