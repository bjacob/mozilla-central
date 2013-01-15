/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

interface RefgraphTypeSearchResult {
  readonly attribute DOMString typeName;
  readonly attribute long objectCount;
};

interface RefgraphVertex {
  readonly attribute DOMString typeName;
  readonly attribute long long address;
  readonly attribute long long size;

  readonly attribute long edgeCount;
  RefgraphEdge edge(long index);
};

interface RefgraphEdge {
  readonly attribute RefgraphVertex target;
  readonly attribute boolean isStrong;
  readonly attribute boolean isTraversedByCC;
};

[Constructor]
interface Refgraph {
  [Creator]
  sequence<RefgraphTypeSearchResult> typeSearch(DOMString query);
  [Creator]
  RefgraphVertex? findVertex(DOMString typeName, RefgraphVertex? previousVertex);
};
