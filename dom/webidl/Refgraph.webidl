/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

interface RefgraphTypeSearchResult {
  readonly attribute DOMString typeName;
  readonly attribute unsigned long objectCount;
};

interface RefgraphVertex {
  readonly attribute DOMString typeName;
  readonly attribute unsigned long long address;
  readonly attribute unsigned long long size;
  readonly attribute unsigned long edgeCount;
  [NewObject]
  RefgraphEdge? edge(unsigned long index);

  readonly attribute unsigned long cycleIndex;
  const unsigned long ALONE_IN_CYCLE = 0xffffffff;

  readonly attribute unsigned long weakEdgeCount;
  readonly attribute unsigned long backEdgeCount;
  readonly attribute unsigned long backWeakEdgeCount;
  [NewObject]
  RefgraphVertex? weakEdge(unsigned long index);
  [NewObject]
  RefgraphVertex? backEdge(unsigned long index);
  [NewObject]
  RefgraphVertex? backWeakEdge(unsigned long index);
};

interface RefgraphEdgeRefInfo {
  readonly attribute long long offset;
  readonly attribute DOMString typeName;
};

interface RefgraphEdge {
  [NewObject]
  readonly attribute RefgraphVertex target;
  readonly attribute boolean isTraversedByCC;
  readonly attribute DOMString CCName;
  readonly attribute unsigned long refInfoCount;
  [NewObject]
  RefgraphEdgeRefInfo? refInfo(unsigned long index);
};

interface RefgraphCycle {
  [NewObject]
  RefgraphVertex? vertex(unsigned long index);
  readonly attribute unsigned long vertexCount;
  readonly attribute boolean isTraversedByCC;
  readonly attribute DOMString name;
};

interface Refgraph {
  [NewObject]
  sequence<RefgraphTypeSearchResult> typeSearch(DOMString query);
  [NewObject]
  RefgraphVertex? findVertex(DOMString typeName, RefgraphVertex? previousVertex);
  [NewObject]
  RefgraphVertex? findVertex(unsigned long long address);

  [NewObject]
  RefgraphCycle? cycle(unsigned long index);
  readonly attribute unsigned long cycleCount;
};

[Constructor]
interface RefgraphController {
  void snapshotToFile(DOMString fileName);
  [NewObject]
  Refgraph? loadFromFile(DOMString fileName);
};
