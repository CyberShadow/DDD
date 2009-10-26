#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <boost/preprocessor/iteration/local.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <algorithm>
using namespace std;

#ifndef NDEBUG
#define DEBUG
#endif

#include "Levels/13.h"

void error(const char* message = NULL)
{
	puts(""); // newline
	if (message)
		puts(message);
	else
		puts("Unspecified error");
	exit(1);
}

char* format(const char *fmt, ...) 
{    
	va_list argptr;
	va_start(argptr,fmt);
	static char buf[1024];
	vsprintf(buf, fmt, argptr);
	va_end(argptr);
	return buf;
}

#ifdef DEBUG
#define assert(expr,...) while(!(expr)){error(__VA_ARGS__);throw "Assertion failure at " __FILE__ ":" BOOST_PP_STRINGIZE(__LINE__);}
#define INLINE
#else
#if defined(_MSC_VER)
#define assert(expr,...) __assume((expr)!=0)
#define INLINE __forceinline
#elif defined(__GNUC__)
#define assert(expr,...) __builtin_expect((expr)!=0,1)
#define INLINE inline
#else
#error Unknown compiler
#endif
#endif

#define MAX_FRAMES (MAX_STEPS*18)

#define	CELL_EMPTY        0x00
#define CELL_MASK         0xC0
#define CELL_WALL         0x40 // also used for "other player"
#define CELL_HOLE         0x80

#define OBJ_NONE          0x00
#define OBJ_MASK          0x3F
	
// bitfield of borders
#define OBJ_BLOCKUP       0x01
#define OBJ_BLOCKRIGHT    0x02
#define OBJ_BLOCKDOWN     0x04
#define OBJ_BLOCKLEFT     0x08
#define OBJ_BLOCKMAX      0x0F

#define OBJ_ROTATORCENTER 0x10
#define OBJ_ROTATORUP     0x11
#define OBJ_ROTATORRIGHT  0x12
#define OBJ_ROTATORDOWN   0x13
#define OBJ_ROTATORLEFT   0x14

#define OBJ_EXIT          0x20

typedef unsigned char BYTE;

enum Action
#ifndef __GNUC__
 : BYTE
#endif
{
	UP,
	RIGHT,
	DOWN,
	LEFT,
	SWITCH,
	NONE,
	
	ACTION_FIRST=UP,
	ACTION_LAST =SWITCH
};

inline Action operator++(Action &rs, int) {return rs = (Action)(rs + 1);}

enum
{
	DELAY_MOVE   =  9, // 1+8
	DELAY_PUSH   = 10, // 2+8
	DELAY_FILL   = 26,
	DELAY_ROTATE = 12,
	DELAY_SWITCH = 30,
};

const char DX[4] = {0, 1, 0, -1};
const char DY[4] = {-1, 0, 1, 0};
const char DR[4+1] = "^>`<";

struct Player
{
	BYTE x, y;
	
	INLINE void set(int x, int y) { this->x = x; this->y = y; }
	INLINE bool exited() const { return x==255; }
	INLINE void exit() { x = 255; }
};

bool holeMap[Y][X];

struct State
{
	BYTE map[Y][X];
	Player players[PLAYERS];
	
#if (PLAYERS==1)
	enum { activePlayer = 0 };
#else
	BYTE activePlayer;
#endif
	
	/// Returns frame delay, 0 if move is invalid and the state was altered, -1 if move is invalid and the state was not altered
	int perform(Action action)
	{
		assert (action <= ACTION_LAST);
		if (action == SWITCH)
		{
#if (PLAYERS==1)
			return -1;
#else
			if (playersLeft())
			{
				switchPlayers();
				return DELAY_SWITCH;
			}
			else
				return -1;
#endif
		}
		Player n, p = players[activePlayer];
		n.x = p.x + DX[action];
		n.y = p.y + DY[action];
		BYTE dmap = map[n.y][n.x];
		BYTE dobj = dmap & OBJ_MASK;
		if (dobj == OBJ_EXIT)
		{
			players[activePlayer] = n;
			players[activePlayer].exit();
			if (playersLeft())
			{
				switchPlayers();
				return DELAY_MOVE + DELAY_SWITCH;
			}
			else
				return DELAY_MOVE;
		}
		if (dmap & CELL_MASK) // neither wall nor hole
			return -1;
		if (dobj == OBJ_NONE)
		{
			players[activePlayer] = n;
			return DELAY_MOVE;
		}
		else
		if (dobj <= OBJ_BLOCKMAX)
		{
			// find block bounds
			BYTE x1=n.x, y1=n.y, x2=n.x, y2=n.y;
			while (!(map[n.y][x1] & OBJ_BLOCKLEFT )) x1--;
			while (!(map[n.y][x2] & OBJ_BLOCKRIGHT)) x2++;
			while (!(map[y1][n.x] & OBJ_BLOCKUP   )) y1--;
			while (!(map[y2][n.x] & OBJ_BLOCKDOWN )) y2++;
			// check if destination is free, clear source row/column
			switch (action)
			{
				case UP:
					for (int x=x1; x<=x2; x++) if (map[y1-1][x]&(CELL_WALL | OBJ_MASK)) return -1;
					for (int x=x1; x<=x2; x++) map[n.y][x] &= CELL_MASK;
					break;
				case DOWN:
					for (int x=x1; x<=x2; x++) if (map[y2+1][x]&(CELL_WALL | OBJ_MASK)) return -1;
					for (int x=x1; x<=x2; x++) map[n.y][x] &= CELL_MASK;
					break;
				case LEFT:
					for (int y=y1; y<=y2; y++) if (map[y][x1-1]&(CELL_WALL | OBJ_MASK)) return -1;
					for (int y=y1; y<=y2; y++) map[y][n.x] &= CELL_MASK;
					break;
				case RIGHT:
					for (int y=y1; y<=y2; y++) if (map[y][x2+1]&(CELL_WALL | OBJ_MASK)) return -1;
					for (int y=y1; y<=y2; y++) map[y][n.x] &= CELL_MASK;
					break;
			}
			// move player
			players[activePlayer] = n;
			x1 += DX[action];
			y1 += DY[action];
			x2 += DX[action];
			y2 += DY[action];
			// check for holes
			bool allHoles = true;
			for (int y=y1; y<=y2; y++)
				for (int x=x1; x<=x2; x++)
					if (!(map[y][x] & CELL_HOLE))
					{
						allHoles = false;
						goto holeScanDone;
					}
		holeScanDone:
			if (allHoles)
			{
				// fill holes
				for (int y=y1; y<=y2; y++)
					for (int x=x1; x<=x2; x++)
					{
						if (!holeMap[y][x])
							return 0; // boring hole
						map[y][x] = 0;
					}
				return DELAY_PUSH + DELAY_FILL;
			}
			else
			{
				// draw the new block
				for (int y=y1; y<=y2; y++)
					for (int x=x1; x<=x2; x++)
						map[y][x] = (map[y][x] & CELL_MASK) | (
							((y==y1) << UP   ) |
							((x==x2) << RIGHT) |
							((y==y2) << DOWN ) |
							((x==x1) << LEFT ));
				return DELAY_PUSH;
			}
		}
		else 
		if (dobj == OBJ_ROTATORCENTER)
			return -1;
		else // rotator
		{
			char rd = dobj - OBJ_ROTATORUP;
			if (rd%2 == action%2)
				return -1;
			char dd = (char)action - rd; // rotation direction: 1=clockwise, -1=CCW
			if (dd<0) dd+=4;
			char rd2 = (rd+2)%4;
			BYTE rx = n.x+DX[rd2]; // rotator center coords
			BYTE ry = n.y+DY[rd2];
			// check for obstacles
			bool oldFlippers[4], newFlippers[4];
			for (char d=0;d<4;d++)
			{
				BYTE d2 = (d+dd)%4; // rotated direction
				if ((map[ry+DY[d]][rx+DX[d]] & OBJ_MASK) == OBJ_ROTATORUP + d)
				{
					oldFlippers[d ] =
					newFlippers[d2] = true;
					if (map[ry+DY[d]+DY[d2]][rx+DX[d]+DX[d2]] & (CELL_WALL | OBJ_MASK))                   // no object/wall in corner
						return -1;
					BYTE d2m = 
					    map[ry+      DY[d2]][rx+      DX[d2]];
					if (d2m & CELL_WALL)
						return -1;
					BYTE d2obj = d2m & OBJ_MASK;
					if (d2obj                                   != OBJ_ROTATORUP + d2 &&       // no object in destination (other than part of the rotator)
					    d2obj                                   != OBJ_NONE)
					    return -1;
				}
				else
					oldFlippers[d ] =
					newFlippers[d2] = false;
			}
			// rotate it
			for (char d=0;d<4;d++)
				if (!oldFlippers[d] && newFlippers[d])
				{
					BYTE* m = &map[ry+DY[d]][rx+DX[d]];
					*m = (*m & CELL_MASK) | (OBJ_ROTATORUP + d);
				}
				else
				if (oldFlippers[d] && !newFlippers[d])
					map[ry+DY[d]][rx+DX[d]] &= CELL_MASK;
			if (map[n.y][n.x]) // full push
			{
				n.x += DX[action];
				n.y += DY[action];
			}
			players[activePlayer] = n;
			return DELAY_ROTATE;
		}
	}

	void switchPlayers()
	{
#if (PLAYERS==1)
		assert(0);
#else
		Player p = players[activePlayer];
		if (!p.exited())
		{
			assert(map[p.y][p.x]==0 || map[p.y][p.x]==(CELL_WALL | OBJ_EXIT));
			map[p.y][p.x] = CELL_WALL;
		}
		do { activePlayer = (activePlayer+1)%PLAYERS; } while (players[activePlayer].exited());
		p = players[activePlayer];
		assert(map[p.y][p.x]==CELL_WALL);
		map[p.y][p.x] = CELL_EMPTY;
#endif
	}

	INLINE BYTE playersLeft()
	{
		return (BYTE)!players[0].exited()
#if (PLAYERS>1)
			+  (BYTE)!players[1].exited()
#endif
#if (PLAYERS>2)
			+  (BYTE)!players[2].exited()
#endif
#if (PLAYERS>3)
			+  (BYTE)!players[3].exited()
#endif
			;
	}

	void load()
	{
		int maxPlayer = 0;

		for (int y=0;y<Y;y++)
			for (int x=0;x<X;x++)
			{
				holeMap[y][x] = false;
				char c = level[y][x];
				switch (c)
				{
					case ' ':
						map[y][x] = CELL_EMPTY;
						break;
					case '#':
					case '+':
						map[y][x] = CELL_WALL;
						break;
					case 'O':
						map[y][x] = CELL_HOLE;
						holeMap[y][x] = true;
						break;
					case '.': // boring hole
						map[y][x] = CELL_HOLE;
						break;
					case '1':
						map[y][x] = CELL_EMPTY;
						players[0].set(x, y);
						break;
					case '2':
						map[y][x] = CELL_WALL | OBJ_EXIT;
						break;
					case '3':
#if (PLAYERS >= 2)
						map[y][x] = CELL_WALL;
						players[1].set(x, y);
						maxPlayer = max(maxPlayer, 1);
#else
						assert(0, "Invalid player");
#endif
						break;
					case '4':
#if (PLAYERS >= 3)
						map[y][x] = CELL_WALL;
						players[2].set(x, y);
						maxPlayer = max(maxPlayer, 2);
#else
						assert(0, "Invalid player");
#endif
						break;
					case '5':
#if (PLAYERS >= 4)
						map[y][x] = CELL_WALL;
						players[3].set(x, y);
						maxPlayer = max(maxPlayer, 3);
#else
						assert(0, "Invalid player");
#endif
						break;
					case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
						assert(x>0); assert(x<X-1);
						assert(y>0); assert(y<Y-1);
						map[y][x] = 
							(level[y-1][x  ]!=c ? OBJ_BLOCKUP    : 0) |
							(level[y  ][x+1]!=c ? OBJ_BLOCKRIGHT : 0) |
							(level[y+1][x  ]!=c ? OBJ_BLOCKDOWN  : 0) |
							(level[y  ][x-1]!=c ? OBJ_BLOCKLEFT  : 0);
						break;
					case '^':
						map[y][x] = OBJ_ROTATORUP;
						break;
					case '>':
						map[y][x] = OBJ_ROTATORRIGHT;
						break;
					case '`':
						map[y][x] = OBJ_ROTATORDOWN;
						break;
					case '<':
						map[y][x] = OBJ_ROTATORLEFT;
						break;
					case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':           case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
					{
						BYTE neighbors[4];
						BYTE neighborCount = 0;
						bool isCenter = false;
						for (BYTE d=0;d<4;d++)
						{
							char c2 = level[y+DY[d]][x+DX[d]];
							if (c2==DR[d])
								isCenter = true;
							if (c2==c || c2==DR[d])
								neighbors[neighborCount++] = d;
						}
						//assert (neighbors.length > 0);
						if (neighborCount>1 || isCenter)
							map[y][x] = OBJ_ROTATORCENTER;
						else
							map[y][x] = OBJ_ROTATORUP + (2+neighbors[0])%4;
						break;
					}
					default:
						error(format("Unknown character in level: %c", c));
				}
			}

		for (int y=0;y<Y;y++)
			for (int x=0;x<X;x++)
				if (map[y][x] >= OBJ_ROTATORUP && map[y][x] <= OBJ_ROTATORLEFT)
				{
					BYTE d = (map[y][x]-OBJ_ROTATORUP+2)%4;
					assert(map[y+DY[d]][x+DX[d]] == OBJ_ROTATORCENTER, "Invalid rotator configuration");
				}

#if (PLAYERS >= 2)
		activePlayer = 0;
#endif
	}

	char* toString()
	{
		char level[Y][X];
		for (int y=0; y<Y; y++)
			for (int x=0; x<X; x++)
				switch (map[y][x] & OBJ_MASK)
				{
					case OBJ_ROTATORCENTER:
						level[y][x] = '+'; break;
					case OBJ_ROTATORUP:
						level[y][x] = '^'; break;
					case OBJ_ROTATORDOWN:
						level[y][x] = 'v'; break;
					case OBJ_ROTATORLEFT:
						level[y][x] = '<'; break;
					case OBJ_ROTATORRIGHT:
						level[y][x] = '>'; break;
					case OBJ_EXIT:
						level[y][x] = 'X'; break;
					case OBJ_NONE:
						switch (map[y][x] & CELL_MASK) 
						{
							case CELL_EMPTY:
								level[y][x] = ' '; break;
							case CELL_WALL:
								level[y][x] = '#'; break;
							case CELL_HOLE:
								level[y][x] = 'O'; break;
						}
						break;
					default:
						level[y][x] = 'x';
						//level[y][x] = "0123456789ABCDEF"[objs[y][x]];
						break;
				}
		for (int p=0;p<PLAYERS;p++)
			if (!players[p].exited())
				level[players[p].y][players[p].x] = p==activePlayer ? '@' : '&';
		
		static char levelstr[Y*(X+1)+1];
		levelstr[Y*(X+1)] = 0;
		for (int y=0; y<Y; y++)
		{
			for (int x=0; x<X; x++)
				levelstr[y*(X+1) + x] = level[y][x];
			levelstr[y*(X+1)+X ] = '\n';
		}
		return levelstr;
	}
};

INLINE bool operator==(State& a, State& b)
{
	return memcmp(&a, &b, sizeof (State))==0;
}
