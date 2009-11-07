FRAME bestSolutionFrame;
NODEI bestSolution[MAX_STEPS];
int bestSolutionSteps;

INLINE void reparentNode(NODEI n, NODEI parent, Node* np, FRAME frame, const State* state, Step step)
{
	markDirty(np);
	for (int i=0; i<bestSolutionSteps; i++)
		if (bestSolution[i] == n)
		{
			State oldState;
			FRAME oldFrame;
			replayState(np, &oldState, &oldFrame);
		   	assert(oldFrame > frame);
			bestSolutionFrame -= oldFrame - frame;
			printf("Better path to solution found (%d frames)\n", bestSolutionFrame);
			
			np->step = step;
			np->parent = parent;
			int step = i;
			while (np->step.action != NONE)
			{
				bestSolution[step++] = n;
				n = np->parent;
				np = getNode(n);
			}
			bestSolutionSteps = step;
			return;
		}
	np->step = step;
	np->parent = parent;
}

void processNodeData(NODEI n, FRAME frame, const State* state)
{
	Node* np = getNode(n);
	if (frame >= bestSolutionFrame)
		return;
	if (state->playersLeft()==0)
	{
		printf("Better solution reached (%d frames)\n", frame);
		bestSolutionFrame = frame;
		// save entire path
		int step = 0;
		while (np->step.action != NONE)
		{
			bestSolution[step++] = n;
			n = np->parent;
			np = getNode(n);
		}
		bestSolutionSteps = step;
	}
	else
		processNodeChildren(n, frame, state);
}

int printResult()
{
	if (bestSolution[0])
	{
		printf("\nSearch done, writing best path...\n");
		FILE* f = fopen(BOOST_PP_STRINGIZE(LEVEL) ".txt", "wt");
		dumpChain(f, getNode(bestSolution[0]));
		fclose(f);
		printf("Done.\n");
		return 0;
	}
	else
	{
		printf("\nSearch done, but exit not found.\n");
		return 2;
	}
}

void searchInit()
{
	bestSolutionFrame = maxFrames;
}
