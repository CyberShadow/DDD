int iteration;
FILE *tipOut, *tipIn, *finish;

void nextIteration()
{
	if (iteration>1)
	{
		fclose(tipIn);
		// optional: remove file here
	}
	if (iteration>0)
	{
		tipIn = tipOut;
		fseek(tipIn, 0, SEEK_SET);
	}
	tipOut = fopen(format("tip-%d-%d.bin", LEVEL, iteration), "w+b");
	iteration++;
}

void saveTip(NODEI n)
{
	fwrite(&n, sizeof(NODEI), 1, tipOut);
}

NODEI readTip()
{
	NODEI n = 0;
	fread(&n, sizeof(NODEI), 1, tipIn);
	return n;
}

void openFinishFile() { finish = fopen(format("finish-%d.bin", LEVEL), "w+b"); }
void rewindFinishFile() { fseek(finish, 0, SEEK_SET); }

void saveFinish(NODEI n)
{
	fwrite(&n, sizeof(NODEI), 1, finish);
}

NODEI readFinish()
{
	NODEI n = 0;
	fread(&n, sizeof(NODEI), 1, finish);
	return n;
}

// ******************************************************************************************************

INLINE void reparentNode(NODEI n, NODEI parent, Node* np, FRAME frame, const State* state, Step step)
{
	//printf("Reparenting %d from %d to %d: %d->%d frames\n", n, np->parent, parent, getFrames(n), frame);
	np->step = step;
	np->parent = parent;
	markDirty(np);
}

void processNodeData(NODEI n, FRAME frame, const State* state)
{
	Node* np = getNode(n);
	if (frame > maxFrames)
	{
		saveTip(n);
		return;
	}
	if (state->playersLeft()==0)
	{
		if (frame < maxFrames)
			printf("Better solution reached (%d frames)\n", frame);
		maxFrames = frame;
		saveFinish(n);
	}
	else
		processNodeChildren(n, frame, state);
}

void processNode(NODEI n)
{
	Node* np = getNode(n);
	State state;
	FRAME stateFrame;
	replayState(np, &state, &stateFrame);
	processNodeData(n, stateFrame, &state);
}

void searchInit()
{
	nextIteration();
	openFinishFile();
}

void runSearch();

// Search will stop at certain depths to prevent searching deeper than necessary.
// However, as new paths are found to nodes which would eventually lead up to "abandoned" tips,
// we need to revisit those tips that are now below the search depth threshold.

void iterate()
{
	bool active;
	do
	{
		nextIteration();
		active = false;
		NODEI n;
		while ((n=readTip())!=0)
		{
			NODEI nodes = nodeCount;
			processNode(n);
			if (nodes != nodeCount)
			{
				active = true;
				runSearch();
			}
		}
	} while (active);
}

NODEI bestFinish;

void findBestFinish()
{
	rewindFinishFile();
	NODEI n;
	while ((n=readFinish())!=0)
	{
		Node* np = getNode(n);
		State state;
		FRAME stateFrame;
		replayState(np, &state, &stateFrame);
		assert(state.playersLeft() == 0, "False finish");
		if (stateFrame <= maxFrames)
		{
			bestFinish = n;
			maxFrames = stateFrame;
		}
	}
}

int printResult()
{
	if (bestFinish)
	{
		printf("\nSearch done, writing best path...\n");
		FILE* f = fopen(BOOST_PP_STRINGIZE(LEVEL) ".txt", "wt");
		dumpChain(f, bestFinish);
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

int search()
{
	runSearch();
	iterate();
	findBestFinish();
	return printResult();
}
