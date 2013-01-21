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
  [Creator]
  RefgraphEdge? edge(unsigned long index);

  readonly attribute unsigned long cycleIndex;
  const unsigned long ALONE_IN_SCC = 0xffffffff;
};

interface RefgraphEdge {
  [Creator]
  readonly attribute RefgraphVertex target;
  readonly attribute boolean isTraversedByCC;
  readonly attribute DOMString refTypeName;
  readonly attribute DOMString refName;
};

interface RefgraphCycle {
  [Creator]
  RefgraphVertex? vertex(unsigned long index);
  readonly attribute unsigned long vertexCount;
  readonly attribute boolean isTraversedByCC;
  readonly attribute DOMString name;
};

interface Refgraph {
  [Creator]
  sequence<RefgraphTypeSearchResult> typeSearch(DOMString query);
  [Creator]
  RefgraphVertex? findVertex(DOMString typeName, RefgraphVertex? previousVertex);
  [Creator]
  RefgraphVertex? findVertex(unsigned long long address);

  [Creator]
  RefgraphCycle? cycle(unsigned long index);
  readonly attribute unsigned long cycleCount;
};

[Constructor]
interface RefgraphController {
  [Creator]
  Refgraph? snapshot();
  /*
  void snapshotToFile(DOMString fileName);
  [Creator]
  Refgraph? loadFromFile(DOMString fileName);
  */
};
