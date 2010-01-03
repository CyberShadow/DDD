// Kwirk DDD module
// Configuration:
// LEVEL - sets the level to solve, from 0 (1-1) to 29 (3-10)


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <boost/preprocessor/iteration/local.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#include BOOST_PP_STRINGIZE(Levels/LEVEL.h)

#if (X-2<=4)
#define XBITS 2
#elif (X-2<=8)
#define XBITS 3
#elif (X-2<=16)
#define XBITS 4
#else
#define XBITS 5
#endif

#if (Y-2<=4)
#define YBITS 2
#elif (Y-2<=8)
#define YBITS 3
#elif (Y-2<=16)
#define YBITS 4
#else
#define YBITS 5
#endif

#if ((X-2)!=4 && (X-2)!=8 && (X-2)!=16)
#define XBITS_WITH_INVAL XBITS
#define YBITS_WITH_INVAL YBITS
#define IS_INVALID(CX,CY) (CX==X-2)
#elif ((Y-2)!=4 && (Y-2)!=8 && (Y-2)!=16)
#define XBITS_WITH_INVAL XBITS
#define YBITS_WITH_INVAL YBITS
#define IS_INVALID(CX,CY) (CY==Y-2)
#else
#define XBITS_WITH_INVAL (XBITS+1)
#define YBITS_WITH_INVAL YBITS
#define IS_INVALID(CX,CY) (CX==X-2)
#endif

#define INVALID_X ((1<<XBITS_WITH_INVAL)-1)
#define INVALID_Y ((1<<YBITS_WITH_INVAL)-1)

#ifndef HOLES
#define HOLES 0
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

enum Action
#ifndef __GNUC__
 : uint8_t
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
const char* actionNames[] = {"Up", "Right", "Down", "Left", "Switch", "None"};

enum
{
	DELAY_MOVE   =  9, // 1+8
	DELAY_PUSH   = 10, // 2+8
	DELAY_FILL   = 26,
	DELAY_ROTATE = 12,
	DELAY_SWITCH = 30,
	DELAY_EXIT   =  1, // fake delay to prevent grouping into one frame group
};

const char DX[4] = {0, 1, 0, -1};
const char DY[4] = {-1, 0, 1, 0};
const char DR[4+1] = "^>`<";

struct Player
{
	uint8_t x, y;
	
	INLINE void set(int x, int y) { this->x = x; this->y = y; }
	INLINE bool exited() const { return x==INVALID_X+1; }
	INLINE void exit() { x=INVALID_X+1, y=INVALID_Y+1; }
};

#define GROUP_FRAMES
#define FRAMES_PER_GROUP 10 // minimal distance between two states

#define COMPRESSED_BITS ( \
	(PLAYERS>2 ? 2 : (PLAYERS>1 ? 1 : 0)) + \
	(XBITS_WITH_INVAL + YBITS_WITH_INVAL) * PLAYERS + \
	(XBITS_WITH_INVAL + YBITS_WITH_INVAL) * BLOCKS + \
	4 * ROTATORS + \
	HOLES \
)

#define COMPRESSED_BYTES ((COMPRESSED_BITS + 7) / 8)
#define COMPRESSED_SLACK_BYTES (3 - ((3 + COMPRESSED_BYTES) % 4))

struct CompressedState
{
	// OPTIMIZATION TODO: use 2 bits instead of 4 for rotators?

	#if (PLAYERS>2)
		unsigned activePlayer : 2;
	#elif (PLAYERS>1)
		unsigned activePlayer : 1;
	#endif
	
	#define BOOST_PP_LOCAL_LIMITS (0, PLAYERS-1)
	#define BOOST_PP_LOCAL_MACRO(n) \
		unsigned BOOST_PP_CAT(player, BOOST_PP_CAT(n, x)) : XBITS_WITH_INVAL; \
		unsigned BOOST_PP_CAT(player, BOOST_PP_CAT(n, y)) : YBITS_WITH_INVAL;
	#include BOOST_PP_LOCAL_ITERATE()

	#if (BLOCKS>0)
	#define BOOST_PP_LOCAL_LIMITS (0, BLOCKS-1)
	#define BOOST_PP_LOCAL_MACRO(n) \
		unsigned BOOST_PP_CAT(block, BOOST_PP_CAT(n, x)) : XBITS_WITH_INVAL; \
		unsigned BOOST_PP_CAT(block, BOOST_PP_CAT(n, y)) : YBITS_WITH_INVAL;
	#include BOOST_PP_LOCAL_ITERATE()
	#endif

	#if (ROTATORS>0)
	#define BOOST_PP_LOCAL_LIMITS (0, ROTATORS-1)
	#define BOOST_PP_LOCAL_MACRO(n) \
		unsigned BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, u)) : 1; \
		unsigned BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, r)) : 1; \
		unsigned BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, d)) : 1; \
		unsigned BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, l)) : 1;
	#include BOOST_PP_LOCAL_ITERATE()
	#endif

	#if (HOLES>0)
	#define BOOST_PP_LOCAL_LIMITS (0, HOLES-1)
	#define BOOST_PP_LOCAL_MACRO(n) \
		unsigned BOOST_PP_CAT(hole, n) : 1;
	#include BOOST_PP_LOCAL_ITERATE()
	#endif

	// TODO: use zero-width fields for alignment?
	#if (COMPRESSED_BITS % 8 != 0) // Align the next field to a byte boundary. This must be done in bitfield semantics, 
	unsigned _align : 8 - (COMPRESSED_BITS % 8); // because the size of a bitfield seems to always be a multiple of 4 bytes.
	#endif
	#if (COMPRESSED_SLACK_BYTES == 3) // Align to word boundary if we have the space.
	unsigned _align2 : 8;
	#endif

	unsigned subframe : 8; // Used in search (actually, only 3 bits are needed)

	//#if (COMPRESSED_SLACK_BYTES != 1) // Align the structure size to dword boundary
	//unsigned _align3 : 8 * (COMPRESSED_SLACK_BYTES == 0 ? 3 : 1);
	//#endif
};

// It is very important that these comparison operators are as fast as possible.

#if   (COMPRESSED_BITS >  24 && COMPRESSED_BITS <=  32) // 4 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] == ((const uint32_t*)&b)[0]; }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] != ((const uint32_t*)&b)[0]; }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] <  ((const uint32_t*)&b)[0]; }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return ((const uint32_t*)&a)[0] <= ((const uint32_t*)&b)[0]; }
#elif (COMPRESSED_BITS >  56 && COMPRESSED_BITS <=  64) // 8 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0]; }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] != ((const uint64_t*)&b)[0]; }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <  ((const uint64_t*)&b)[0]; }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <= ((const uint64_t*)&b)[0]; }
#elif (COMPRESSED_BITS >  96 && COMPRESSED_BITS <= 112) // 13-14 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && (((const uint64_t*)&a)[1]&0x0000FFFFFFFFFFFFLL) == (((const uint64_t*)&b)[1]&0x0000FFFFFFFFFFFFLL); }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] != ((const uint64_t*)&b)[0] || (((const uint64_t*)&a)[1]&0x0000FFFFFFFFFFFFLL) != (((const uint64_t*)&b)[1]&0x0000FFFFFFFFFFFFLL); }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <  ((const uint64_t*)&b)[0] || 
                                                                                   (((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && (((const uint64_t*)&a)[1]&0x0000FFFFFFFFFFFFLL) <  (((const uint64_t*)&b)[1]&0x0000FFFFFFFFFFFFLL)); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <  ((const uint64_t*)&b)[0] || 
                                                                                   (((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && (((const uint64_t*)&a)[1]&0x0000FFFFFFFFFFFFLL) <= (((const uint64_t*)&b)[1]&0x0000FFFFFFFFFFFFLL)); }
#elif (COMPRESSED_BITS > 120 && COMPRESSED_BITS <= 128) // 16 bytes
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && ((const uint64_t*)&a)[1] == ((const uint64_t*)&b)[1]; }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] != ((const uint64_t*)&b)[0] || ((const uint64_t*)&a)[1] != ((const uint64_t*)&b)[1]; }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <  ((const uint64_t*)&b)[0] || 
                                                                                   (((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && ((const uint64_t*)&a)[1] <  ((const uint64_t*)&b)[1]); }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return ((const uint64_t*)&a)[0] <  ((const uint64_t*)&b)[0] || 
                                                                                   (((const uint64_t*)&a)[0] == ((const uint64_t*)&b)[0] && ((const uint64_t*)&a)[1] <= ((const uint64_t*)&b)[1]); }
#else
#pragma message("Performance warning: using memcmp for CompressedState comparison")
INLINE bool operator==(const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)==0; }
INLINE bool operator!=(const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)!=0; }
INLINE bool operator< (const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)< 0; }
INLINE bool operator<=(const CompressedState& a, const CompressedState& b) { return memcmp(&a, &b, COMPRESSED_BYTES)<=0; }
#endif

INLINE bool operator> (const CompressedState& a, const CompressedState& b) { return b< a; }
INLINE bool operator>=(const CompressedState& a, const CompressedState& b) { return b<=a; }

const char* dumpCompressedState(const CompressedState* cs)
{
	static char s[sizeof(CompressedState)*3+1];
	for (int i=0; i<sizeof(CompressedState); i++)
		sprintf(s + i*3, "%02X ", ((const uint8_t*)cs)[i]);
	return s;
}

#if (BLOCKS > 0)
struct { uint8_t x, y; } blockSize[BLOCKS];
int blockSizeIndex[BLOCKY][BLOCKX];
#endif
#if (ROTATORS > 0)
struct { uint8_t x, y; } rotators[ROTATORS];
#endif
#if (HOLES > 0)
struct { uint8_t x, y; } holes[HOLES];
#endif
bool holeMap[Y][X];

struct State
{
	static State initial, blanked;

	uint8_t map[Y][X];
	Player players[PLAYERS];
	
#if (PLAYERS==1)
	enum { activePlayer = 0 };
#else
	uint8_t activePlayer;
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
		uint8_t dmap = map[n.y][n.x];
		uint8_t dobj = dmap & OBJ_MASK;
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
				return DELAY_MOVE + DELAY_EXIT;
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
			uint8_t x1=n.x, y1=n.y, x2=n.x, y2=n.y;
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
			uint8_t rx = n.x+DX[rd2]; // rotator center coords
			uint8_t ry = n.y+DY[rd2];
			// check for obstacles
			bool oldFlippers[4], newFlippers[4];
			for (char d=0;d<4;d++)
			{
				uint8_t d2 = (d+dd)%4; // rotated direction
				if ((map[ry+DY[d]][rx+DX[d]] & OBJ_MASK) == OBJ_ROTATORUP + d)
				{
					oldFlippers[d ] =
					newFlippers[d2] = true;
					if (map[ry+DY[d]+DY[d2]][rx+DX[d]+DX[d2]] & (CELL_WALL | OBJ_MASK))                   // no object/wall in corner
						return -1;
					uint8_t d2m = 
					    map[ry+      DY[d2]][rx+      DX[d2]];
					if (d2m & CELL_WALL)
						return -1;
					uint8_t d2obj = d2m & OBJ_MASK;
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
					uint8_t* m = &map[ry+DY[d]][rx+DX[d]];
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

	INLINE uint8_t playersLeft() const
	{
		return (uint8_t)!players[0].exited()
#if (PLAYERS>1)
			+  (uint8_t)!players[1].exited()
#endif
#if (PLAYERS>2)
			+  (uint8_t)!players[2].exited()
#endif
#if (PLAYERS>3)
			+  (uint8_t)!players[3].exited()
#endif
			;
	}

	INLINE bool isFinish() const
	{
		return playersLeft()==0;
	}

	#ifdef HAVE_VALIDATOR
	#include BOOST_PP_STRINGIZE(Levels/LEVEL-validator.h)
	#endif

	void load()
	{
		int maxPlayer = 0;
		int seenHoles = 0;

#if (BLOCKS > 0)
		int seenBlocks = 0;
		int blockSizeCount[BLOCKY][BLOCKX];
		memset(blockSizeCount, 0, sizeof blockSizeCount);
#endif

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
#if (HOLES > 0)
					case 'O':
						map[y][x] = CELL_HOLE;
						holeMap[y][x] = true;
						holes[seenHoles].x = x;
						holes[seenHoles].y = y;
						seenHoles++;
						break;
#endif
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
						enforce(0, "Invalid player");
#endif
						break;
					case '4':
#if (PLAYERS >= 3)
						map[y][x] = CELL_WALL;
						players[2].set(x, y);
						maxPlayer = max(maxPlayer, 2);
#else
						enforce(0, "Invalid player");
#endif
						break;
					case '5':
#if (PLAYERS >= 4)
						map[y][x] = CELL_WALL;
						players[3].set(x, y);
						maxPlayer = max(maxPlayer, 3);
#else
						enforce(0, "Invalid player");
#endif
						break;
#if (BLOCKS > 0)
					case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
						enforce(x>0); enforce(x<X-1);
						enforce(y>0); enforce(y<Y-1);
						map[y][x] = 
							(level[y-1][x  ]!=c ? OBJ_BLOCKUP    : 0) |
							(level[y  ][x+1]!=c ? OBJ_BLOCKRIGHT : 0) |
							(level[y+1][x  ]!=c ? OBJ_BLOCKDOWN  : 0) |
							(level[y  ][x-1]!=c ? OBJ_BLOCKLEFT  : 0);
						if ((map[y][x] & (OBJ_BLOCKUP | OBJ_BLOCKLEFT)) == (OBJ_BLOCKUP | OBJ_BLOCKLEFT))
						{
							seenBlocks++;
							int x2 = x;
							while (level[y][x2+1] == c)
								x2++;
							enforce(x2-x < BLOCKX, "Block too wide");
							int y2 = y;
							while (level[y2+1][x] == c)
								y2++;
							enforce(y2-y < BLOCKY, "Block too tall");
							blockSizeCount[y2-y][x2-x]++;
						}
						break;
#endif
#if (ROTATORS > 0)
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
						uint8_t neighbors[4];
						uint8_t neighborCount = 0;
						bool isCenter = false;
						for (uint8_t d=0;d<4;d++)
						{
							char c2 = level[y+DY[d]][x+DX[d]];
							if (c2==DR[d])
								isCenter = true;
							if (c2==c || c2==DR[d])
								neighbors[neighborCount++] = d;
						}
						//enforce (neighbors.length > 0);
						if (neighborCount>1 || isCenter)
							map[y][x] = OBJ_ROTATORCENTER;
						else
							map[y][x] = OBJ_ROTATORUP + (2+neighbors[0])%4;
						break;
					}
#endif
					default:
						error(format("Unknown character in level: %c", c));
				}
			}

#if (ROTATORS > 0)
		int seenRotators = 0;
		for (int y=0;y<Y;y++)
			for (int x=0;x<X;x++)
				if (map[y][x] >= OBJ_ROTATORUP && map[y][x] <= OBJ_ROTATORLEFT)
				{
					uint8_t d = (map[y][x]-OBJ_ROTATORUP+2)%4;
					enforce(map[y+DY[d]][x+DX[d]] == OBJ_ROTATORCENTER, "Invalid rotator configuration");
				}
				else
				if (map[y][x] == OBJ_ROTATORCENTER)
				{
					rotators[seenRotators].x = x;
					rotators[seenRotators].y = y;
					seenRotators++;
				}
		enforce(seenRotators == ROTATORS, format("Mismatching number of rotators: is %d, should be %d", ROTATORS, seenRotators));
#endif

#if (BLOCKS > 0)
		int index = 0;
		for (int y=0;y<BLOCKY;y++)
			for (int x=0;x<BLOCKX;x++)
			{
				blockSizeIndex[y][x] = index;
				int count = blockSizeCount[y][x];
				for (int i=index; i<index+count; i++)
					blockSize[i].x = x,
					blockSize[i].y = y;
				index += count;
			}
		enforce(seenBlocks == BLOCKS, format("Mismatching number of blocks: is %d, should be %d", BLOCKS, seenBlocks));
#endif

		enforce(maxPlayer+1 == PLAYERS, format("Mismatching number of players: is %d, should be %d", PLAYERS, maxPlayer+1));
		enforce(seenHoles == HOLES, format("Mismatching number of holes: is %d, should be %d", HOLES, seenHoles));


#if (PLAYERS >= 2)
		activePlayer = 0;
#endif
	}

	void compress(CompressedState* s) const
	{
		memset(s, 0, sizeof CompressedState);

		#if (PLAYERS>1)
		s->activePlayer = activePlayer;
		#endif

		#define BOOST_PP_LOCAL_LIMITS (0, PLAYERS-1)
		#define BOOST_PP_LOCAL_MACRO(n) \
			s->BOOST_PP_CAT(player, BOOST_PP_CAT(n, x))=players[n].x-1, \
			s->BOOST_PP_CAT(player, BOOST_PP_CAT(n, y))=players[n].y-1;
		#include BOOST_PP_LOCAL_ITERATE()
		
		#if (BLOCKS > 0)
		int seenBlocks = 0;
		struct { uint8_t x, y; } blocks[BLOCKS];
		int blockSizeCount[BLOCKY][BLOCKX];
		
		memset(blocks, 0xFF, sizeof blocks);
		memset(blockSizeCount, 0, sizeof blockSizeCount);
		#endif
		#if (ROTATORS > 0)
		int seenRotators = 0;
		struct { bool u, r, d, l; } rotators[ROTATORS];
		#endif
		#if (HOLES>0)
		unsigned int holePos = 0;
		bool holes[HOLES];
		#endif

		for (int y=1;y<Y-1;y++)
			for (int x=1;x<X-1;x++)
			{
				uint8_t m = map[y][x];

				#if (BLOCKS > 0)
				if ((m & (OBJ_BLOCKUP | OBJ_BLOCKLEFT)) == (OBJ_BLOCKUP | OBJ_BLOCKLEFT))
				{
					int x2 = x;
					while ((map[y][x2] & OBJ_BLOCKRIGHT) == 0)
						x2++;
					int y2 = y;
					while ((map[y2][x] & OBJ_BLOCKDOWN) == 0)
						y2++;
					x2-=x;
					y2-=y;
					assert(x2 < BLOCKX, "Block too wide");
					assert(y2 < BLOCKY, "Block too tall");
					int index = blockSizeIndex[y2][x2] + blockSizeCount[y2][x2];
					blocks[index].x = x-1;
					blocks[index].y = y-1;
					blockSizeCount[y2][x2]++;
					seenBlocks++;
				}
				#endif

				#if (ROTATORS > 0)
				if ((m & OBJ_MASK) == OBJ_ROTATORCENTER)
				{
					rotators[seenRotators].u = (map[y-1][x  ]&OBJ_MASK)==OBJ_ROTATORUP;
					rotators[seenRotators].r = (map[y  ][x+1]&OBJ_MASK)==OBJ_ROTATORRIGHT;
					rotators[seenRotators].d = (map[y+1][x  ]&OBJ_MASK)==OBJ_ROTATORDOWN;
					rotators[seenRotators].l = (map[y  ][x-1]&OBJ_MASK)==OBJ_ROTATORLEFT;
					seenRotators++;
				}
				#endif

				#if (HOLES>0)
				if (holeMap[y][x])
					holes[holePos++] = (m & CELL_MASK) == CELL_HOLE;
				#endif
			}
		
		#if (BLOCKS > 0)
		assert(seenBlocks <= BLOCKS, "Too many blocks");

		#define BOOST_PP_LOCAL_LIMITS (0, BLOCKS-1)
		#define BOOST_PP_LOCAL_MACRO(n) \
			s->BOOST_PP_CAT(block, BOOST_PP_CAT(n, x)) = blocks[n].x ; \
			s->BOOST_PP_CAT(block, BOOST_PP_CAT(n, y)) = blocks[n].y ;
		#include BOOST_PP_LOCAL_ITERATE()
		#endif

		#if (ROTATORS > 0)
		assert(seenRotators == ROTATORS, "Vanished rotator?");
		#define BOOST_PP_LOCAL_LIMITS (0, ROTATORS-1)
		#define BOOST_PP_LOCAL_MACRO(n) \
			s->BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, u)) = rotators[n].u; \
			s->BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, r)) = rotators[n].r; \
			s->BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, d)) = rotators[n].d; \
			s->BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, l)) = rotators[n].l;
		#include BOOST_PP_LOCAL_ITERATE()
		#endif

		#if (HOLES>0)
		assert(holePos == HOLES);
		#define BOOST_PP_LOCAL_LIMITS (0, HOLES-1)
		#define BOOST_PP_LOCAL_MACRO(n) \
			s->BOOST_PP_CAT(hole, n) = holes[n];
		#include BOOST_PP_LOCAL_ITERATE()
		#endif
	}

	void decompress(const CompressedState* s)
	{
		*this = blanked;
		#if (PLAYERS>1)
		activePlayer = s->activePlayer;
		#endif

		#define BOOST_PP_LOCAL_LIMITS (0, PLAYERS-1)
		#define BOOST_PP_LOCAL_MACRO(n) \
			players[n].x = s->BOOST_PP_CAT(player, BOOST_PP_CAT(n, x))+1, \
			players[n].y = s->BOOST_PP_CAT(player, BOOST_PP_CAT(n, y))+1;
		#include BOOST_PP_LOCAL_ITERATE()
		
		#if (HOLES>0)
		#define BOOST_PP_LOCAL_LIMITS (0, HOLES-1)
		#define BOOST_PP_LOCAL_MACRO(n) \
			if (!s->BOOST_PP_CAT(hole, n)) map[holes[n].y][holes[n].x] = CELL_EMPTY;
		#include BOOST_PP_LOCAL_ITERATE()
		#endif

		#if (BLOCKS > 0)
		#define BOOST_PP_LOCAL_LIMITS (0, BLOCKS-1)
		#define BOOST_PP_LOCAL_MACRO(n) \
		{ \
			uint8_t x1 = s->BOOST_PP_CAT(block, BOOST_PP_CAT(n, x)); \
			uint8_t y1 = s->BOOST_PP_CAT(block, BOOST_PP_CAT(n, y)); \
			if (x1 != INVALID_X || y1 != INVALID_Y) \
			{ \
				/* Hack: increment x and y after decrementing */ \
				x1++; y1++; \
				uint8_t x2 = x1 + blockSize[n].x; \
				uint8_t y2 = y1 + blockSize[n].y; \
				for (uint8_t x=x1; x<=x2; x++) \
					map[y1][x] |= OBJ_BLOCKUP, \
					map[y2][x] |= OBJ_BLOCKDOWN; \
				for (uint8_t y=y1; y<=y2; y++) \
					map[y][x1] |= OBJ_BLOCKLEFT, \
					map[y][x2] |= OBJ_BLOCKRIGHT; \
			} \
		} 
		#include BOOST_PP_LOCAL_ITERATE()
		#endif

		#if (ROTATORS > 0)
		#define BOOST_PP_LOCAL_LIMITS (0, ROTATORS-1)
		#define BOOST_PP_LOCAL_MACRO(n) \
		{ \
			uint8_t x = rotators[n].x; \
			uint8_t y = rotators[n].y; \
			map[y][x] |= OBJ_ROTATORCENTER; \
			if (s->BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, u))) map[y-1][x  ] |= OBJ_ROTATORUP;    \
			if (s->BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, r))) map[y  ][x+1] |= OBJ_ROTATORRIGHT; \
			if (s->BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, d))) map[y+1][x  ] |= OBJ_ROTATORDOWN;  \
			if (s->BOOST_PP_CAT(rotator, BOOST_PP_CAT(n, l))) map[y  ][x-1] |= OBJ_ROTATORLEFT;  \
		}
		#include BOOST_PP_LOCAL_ITERATE()
		#endif
	}

	/// Optimize state for decompression
	void blank()
	{
		for (int y=0;y<Y;y++)
			for (int x=0;x<X;x++)
				map[y][x] &= ~0x1F; // clear blocks and rotators
	}

	const char* toString() const
	{
		char level[Y][X];
		int blockSizeCount[BLOCKY][BLOCKX];
		memset(blockSizeCount, 0, sizeof blockSizeCount);

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
						//level[y][x] = 'x';
						/*if ((map[y][x] & (OBJ_BLOCKUP | OBJ_BLOCKLEFT)) == (OBJ_BLOCKUP | OBJ_BLOCKLEFT))
							level[y][x] = blockLetter++;
						else
						if (map[y][x] & OBJ_BLOCKUP)
							level[y][x] = level[y][x-1];
						else
							level[y][x] = level[y-1][x];*/
						if ((map[y][x] & (OBJ_BLOCKUP | OBJ_BLOCKLEFT)) == (OBJ_BLOCKUP | OBJ_BLOCKLEFT))
						{
							int x2 = x;
							while ((map[y][x2] & OBJ_BLOCKRIGHT) == 0)
								x2++;
							int y2 = y;
							while ((map[y2][x] & OBJ_BLOCKDOWN) == 0)
								y2++;
							x2-=x;
							y2-=y;
							assert(x2 < BLOCKX, "Block too wide");
							assert(y2 < BLOCKY, "Block too tall");
							int index = blockSizeIndex[y2][x2] + blockSizeCount[y2][x2];
							for (int by=y; by<=y+y2; by++)
								for (int bx=x; bx<=x+x2; bx++)
									level[by][bx] = 'a'+index;
							blockSizeCount[y2][x2]++;
						}
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

INLINE bool operator==(const State& a, const State& b)
{
	return memcmp(&a, &b, sizeof (State))==0;
}

State State::initial, State::blanked;

// ******************************************************************************************************

// Defines a move within the graph. x and y are the player's position after movement (used to collapse multiple movement steps that don't change the level layout)
//#pragma pack(1)
struct Step
{
	Action action;
	uint8_t x;
	uint8_t y;
	uint8_t extraSteps;

	const char* toString()
	{
		return format("@%u,%u: %s", x+1, y+1, actionNames[action]);
	}
};

INLINE int replayStep(State* state, FRAME* frame, Step step)
{
	Player* p = &state->players[state->activePlayer];
	int nx = step.x+1;
	int ny = step.y+1;
	int steps = abs((int)p->x - nx) + abs((int)p->y - ny) + step.extraSteps;
	p->x = nx;
	p->y = ny;
	assert(state->map[ny][nx]==0, "Bad coordinates");
	int res = state->perform((Action)step.action);
	assert(res>0, "Replay failed");
	*frame += steps * DELAY_MOVE + res;
	return steps; // not counting actual action
}

// ******************************************************************************************************

template <class CHILD_HANDLER>
void expandChildren(FRAME frame, const State* state)
{
	struct Coord { uint8_t x, y; };
	const int QUEUELENGTH = X+Y;
	Coord queue[QUEUELENGTH];
	uint8_t distance[Y-2][X-2];
	uint32_t queueStart=0, queueEnd=1;
	memset(distance, 0xFF, sizeof(distance));
	
	uint8_t x0 = state->players[state->activePlayer].x;
	uint8_t y0 = state->players[state->activePlayer].y;
	queue[0].x = x0;
	queue[0].y = y0;
	distance[y0-1][x0-1] = 0;

	State newState = *state;
	Player* np = &newState.players[newState.activePlayer];
	while(queueStart != queueEnd)
	{
		Coord c = queue[queueStart];
		queueStart = (queueStart+1) % QUEUELENGTH;
		uint8_t dist = distance[c.y-1][c.x-1];
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
			CHILD_HANDLER::handleChild(&newState, step, frame + dist * DELAY_MOVE + DELAY_SWITCH);
			newState = *state;
		#endif

		for (Action action = ACTION_FIRST; action < SWITCH; action++)
		{
			uint8_t nx = c.x + DX[action];
			uint8_t ny = c.y + DY[action];
			uint8_t m = newState.map[ny][nx];
			if (m & OBJ_MASK)
			{
				np->x = c.x;
				np->y = c.y;
				int res = newState.perform(action);
				if (res > 0)
				{
					step.action = /*(unsigned)*/action;
					CHILD_HANDLER::handleChild(&newState, step, frame + dist * DELAY_MOVE + res);
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

// ******************************************************************************************************

const char* formatProblemFileName(const char* name, const char* detail, const char* ext)
{
	return format("%s%s%u%s%s.%s", name ? name : "", name ? "-" : "", LEVEL, detail ? "-" : "", detail ? detail : "", ext);
}

// ******************************************************************************************************

void writeSolution(FILE* f, const State* initialState, Step steps[], int stepNr)
{
	steps[stepNr].action = NONE;
	steps[stepNr].x = initialState->players[0].x-1;
	steps[stepNr].y = initialState->players[0].y-1;
	unsigned int totalSteps = 0;
	State state = *initialState;
	FRAME frame = 0;
	while (stepNr)
	{
		fprintf(f, "%s\n", steps[stepNr].toString());
		fprintf(f, "%s", state.toString());
		totalSteps += (steps[stepNr].action<SWITCH ? 1 : 0) + replayStep(&state, &frame, steps[--stepNr]);
	}
	// last one
	fprintf(f, "%s\n%s", steps[0].toString(), state.toString());
	fprintf(f, "Total steps: %u", totalSteps);
}

// ******************************************************************************************************

State* initialStates = &State::initial;
int initialStateCount = 1;

void initProblem()
{
	printf("Kwirk Level %u (%d-%d): %ux%u, %u players\n", LEVEL, LEVEL/10+1, LEVEL%10+1, X, Y, PLAYERS);

#ifdef HAVE_VALIDATOR
	printf("Level state validator present\n");
#endif

	State::initial.load();
	State::blanked = State::initial;
	State::blanked.blank();
}
