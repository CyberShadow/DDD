// no queue, just good old recursivity

#include "search_dfs.cpp"

INLINE void queueNode(NODEI node, FRAME frame, const State* state)
{
	processNodeData(node, frame, state);
	postNode();
}

void runSearch() { }
