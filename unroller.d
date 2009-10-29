import std.stream;
import std.string;
import std.conv;
import std.stdio;

enum Action : ubyte
{
	Up,
	Right,
	Down,
	Left
}

const string[] actionNames = ["Up", "Right", "Down", "Left"];

const byte[4] DX = [0, 1, 0, -1];
const byte[4] DY = [-1, 0, 1, 0];
const X = 22;
const Y = 18;

void main(string[] args)
{
	if (args.length!=2)
		throw new Exception("Specify solution number to unroll.");
	string[] level;
	auto output = new BufferedFile(args[1] ~ "u.txt", FileMode.OutNew);
	foreach (string line; new BufferedFile(args[1] ~ ".txt"))
	{
		if (line[0]=='@')
		{
			if (level.length)
			{
				string[] coords = line[1..line.find(':')].split(",");
				ubyte x1 = toUbyte(coords[0]);
				ubyte y1 = toUbyte(coords[1]);
				ubyte x0, y0;
				foreach (uint y, l; level)
					foreach (uint x, c; l)
						if (c=='@')
							x0 = cast(ubyte)x,
							y0 = cast(ubyte)y;
				
				struct Coord { ubyte x, y; }
				const ubyte QUEUELENGTH = X+Y;
				Coord[QUEUELENGTH] queue;
				ubyte distance[Y-2][X-2];
				Action from[Y-2][X-2];
				ubyte queueStart=0, queueEnd=1;
				foreach (ref r;distance)
					foreach (ref c;r)
						c = ubyte.max;

				queue[0].x = x0;
				queue[0].y = y0;
				distance[y0-1][x0-1] = 0;

				while(queueStart != queueEnd)
				{
					Coord c = queue[queueStart];
					queueStart = cast(ubyte)((queueStart+1) % QUEUELENGTH);
					ubyte dist = distance[c.y-1][c.x-1];
	
					if (c.x==x1 && c.y==y1)
					{
						auto pos = dist;
						do
						{
							queue[pos].x = c.x;
							queue[pos].y = c.y;
							auto action = from[c.y-1][c.x-1];
							c.x -= DX[action];
							c.y -= DY[action];
						} while(pos--);
						for (pos=0; pos<=dist; pos++)
						{
							if (pos)
								output.writeLine(actionNames[from[queue[pos].y-1][queue[pos].x-1]] ~ "~");
							level[queue[pos].y][queue[pos].x] = '@';
							foreach (l; level)
								output.writeLine(l);
							level[queue[pos].y][queue[pos].x] = ' ';
						}
						goto found;
					}

					for (Action action = Action.min; action <= Action.max; action++)
					{
						ubyte nx = c.x + DX[action];
						ubyte ny = c.y + DY[action];

						if (level[ny][nx]==' ' && distance[ny-1][nx-1]==ubyte.max)
						{
							from[ny-1][nx-1] = action;
							distance[ny-1][nx-1] = dist+1;
							queue[queueEnd].x = nx;
							queue[queueEnd].y = ny;
							queueEnd = cast(ubyte)((queueEnd+1) % QUEUELENGTH);
							assert(queueEnd != queueStart, "Queue overflow");
						}
					}
				}

				throw new Exception("Can't find path");
			}
			found:
			output.writeLine(line[line.find(':')+2..$] ~ "!");
			level = null;
		}
		else
		if (line[0]=='#')
			level ~= line.dup;
		else
		{
		    foreach (l; level)
		    	output.writeLine(l);
			level = null;
			output.writeString(line);
		}
		output.flush();
	}
}