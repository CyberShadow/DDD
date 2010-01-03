enum Action
{
	UP,
	RIGHT,
	DOWN,
	LEFT,
	NONE,
	
	ACTION_FIRST=UP,
	ACTION_LAST =LEFT
};

inline Action operator++(Action &rs, int) {return rs = (Action)(rs + 1);}
const char* actionNames[] = {"Up", "Right", "Down", "Left", "None"};

const char DX[4] = {0, 1, 0, -1};
const char DY[4] = {-1, 0, 1, 0};

struct CompressedState
{
	uint16_t x;
	uint16_t y;
};

// It is very important that these comparison operators are as fast as possible.

INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] == ((const uint32_t*)&b)[0]; }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] != ((const uint32_t*)&b)[0]; }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] <  ((const uint32_t*)&b)[0]; }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] <= ((const uint32_t*)&b)[0]; }

INLINE bool operator> (const CompressedState& a, const CompressedState& b) { return b< a; }
INLINE bool operator>=(const CompressedState& a, const CompressedState& b) { return b<=a; }

#define X 15
#define Y 15

const char level[Y][X+1] = {
"###############",
"#S#         # #",
"# ##### ### # #",
"#     #   #   #",
"#####   # # # #",
"#     # ### # #",
"# ### # #   # #",
"# # ### ##### #",
"# #   # #     #",
"### # ### #####",
"#S# #     #   #",
"# # # # ### # #",
"# # # # #   # #",
"#   # #   # #F#",
"###############",
};

#define MAX_FRAMES 100
#define MAX_STEPS MAX_FRAMES

struct State
{
	int x, y;

	/// Returns frame delay, 0 if move is invalid and the state was altered, -1 if move is invalid and the state was not altered
	int perform(Action action)
	{
		assert (action <= ACTION_LAST);
		if (level[y+DY[action]][x+DX[action]] == '#')
			return -1;
		x += DX[action];
		y += DY[action];
		return 1;
	}

	INLINE bool isFinish() const
	{
		return level[y][x] == 'F';
	}

	void compress(CompressedState* s) const
	{
		s->x = x;
		s->y = y;
	}

	void decompress(const CompressedState* s)
	{
	    x = s->x;
	    y = s->y;
	}

	const char* toString() const
	{
		static char levelstr[Y*(X+1)+1];
		levelstr[Y*(X+1)] = 0;
		for (int sy=0; sy<Y; sy++)
		{
			for (int sx=0; sx<X; sx++)
				levelstr[sy*(X+1) + sx] = level[sy][sx];
			levelstr[sy*(X+1)+X ] = '\n';
		}
		levelstr[y*(X+1) + x] = '@';
		return levelstr;
	}
};

INLINE bool operator==(const State& a, const State& b)
{
	return memcmp(&a, &b, sizeof (State))==0;
}

// ******************************************************************************************************

// Defines a move within the graph. x and y are the player's position after movement (used to collapse multiple movement steps that don't change the level layout)
//#pragma pack(1)
struct Step
{
	Action action;

	const char* toString()
	{
		return format("%s", actionNames[action]);
	}
};

INLINE void replayStep(State* state, FRAME* frame, Step step)
{
	int res = state->perform((Action)step.action);
	assert(res>0, "Replay failed");
	*frame += res;
}

// ******************************************************************************************************

template <class CHILD_HANDLER>
void expandChildren(FRAME frame, const State* state)
{
	State newState = *state;
	for (Action action = ACTION_FIRST; action <= ACTION_LAST; action++)
	{
		int res = newState.perform(action);
		Step step;
		if (res > 0)
		{
			step.action = action;
			CHILD_HANDLER::handleChild(&newState, step, frame + res);
		}
		if (res >= 0)
			newState = *state;
	}
}

// ******************************************************************************************************

const char* formatProblemFileName(const char* name, const char* detail, const char* ext)
{
	return format("%s%s%s.%s", name ? name : "", (name && detail) ? "-" : "", detail ? detail : "", ext);
}

// ******************************************************************************************************

void writeSolution(FILE* f, const State* initialState, Step steps[], int stepNr)
{
	steps[stepNr].action = NONE;
	State state = *initialState;
	FRAME frame = 0;
	while (stepNr)
	{
		fprintf(f, "%s\n", steps[stepNr].toString());
		fprintf(f, "%s", state.toString());
		replayStep(&state, &frame, steps[--stepNr]);
	}
	// last one
	fprintf(f, "%s\n%s", steps[0].toString(), state.toString());
}

// ******************************************************************************************************

#define MAX_INITIAL_STATES 4

State initialStates[MAX_INITIAL_STATES];
int initialStateCount = 0;

void initProblem()
{
	printf("SampleMaze: %ux%u\n", X, Y);

	for (int y=0; y<Y; y++)
		for (int x=0; x<X; x++)
			if (level[y][x] == 'S')
			{
				initialStates[initialStateCount].x = x;
				initialStates[initialStateCount].y = y;
				initialStateCount++;
			}
}
