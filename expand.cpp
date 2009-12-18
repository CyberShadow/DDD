// Node-expanding function. Uses EXPAND_NAME and EXPAND_HANDLE_CHILD macros.
// TODO: reimplement X-macro as template

void EXPAND_NAME(FRAME frame, const State* state)
{
	struct Coord { BYTE x, y; };
	const int QUEUELENGTH = X+Y;
	Coord queue[QUEUELENGTH];
	BYTE distance[Y-2][X-2];
	uint32_t queueStart=0, queueEnd=1;
	memset(distance, 0xFF, sizeof(distance));
	
	BYTE x0 = state->players[state->activePlayer].x;
	BYTE y0 = state->players[state->activePlayer].y;
	queue[0].x = x0;
	queue[0].y = y0;
	distance[y0-1][x0-1] = 0;

	State newState = *state;
	Player* np = &newState.players[newState.activePlayer];
	while(queueStart != queueEnd)
	{
		Coord c = queue[queueStart];
		queueStart = (queueStart+1) % QUEUELENGTH;
		BYTE dist = distance[c.y-1][c.x-1];
		Step step;
		step.x = c.x-1;
		step.y = c.y-1;
		step.extraSteps = dist - (abs((int)c.x - (int)x0) + abs((int)c.y - (int)y0));

		#if (PLAYERS>1)
			np->x = c.x;
			np->y = c.y;
			int res = newState.perform(SWITCH);
			assert(res == DELAY_SWITCH);
			step.action = (unsigned)SWITCH;
			EXPAND_HANDLE_CHILD(&newState, step, frame + dist * DELAY_MOVE + DELAY_SWITCH);
			newState = *state;
		#endif

		for (Action action = ACTION_FIRST; action < SWITCH; action++)
		{
			BYTE nx = c.x + DX[action];
			BYTE ny = c.y + DY[action];
			BYTE m = newState.map[ny][nx];
			if (m & OBJ_MASK)
			{
				np->x = c.x;
				np->y = c.y;
				int res = newState.perform(action);
				if (res > 0)
				{
					step.action = /*(unsigned)*/action;
					EXPAND_HANDLE_CHILD(&newState, step, frame + dist * DELAY_MOVE + res);
				}
				if (res >= 0)
					newState = *state;
			}
			else
			if ((m & CELL_MASK) == 0)
				if (distance[ny-1][nx-1] == 0xFF)
				{
					distance[ny-1][nx-1] = dist+1;
					queue[queueEnd].x = nx;
					queue[queueEnd].y = ny;
					queueEnd = (queueEnd+1) % QUEUELENGTH;
					assert(queueEnd != queueStart, "Queue overflow");
				}
		}
	}
}

#undef EXPAND_NAME
#undef EXPAND_HANDLE_CHILD
