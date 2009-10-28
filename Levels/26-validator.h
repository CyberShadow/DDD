bool validate()
{
	for (int y=1; y<Y-1; y++)
		for (int x=1; x<X-1; x++)
		{
			BYTE o = map[y][x] & OBJ_MASK;
			if (o>0 && o <= OBJ_BLOCKMAX)
			{
				for (char d=0;d<4;d++)
				{
					char d2 = (d+1) % 4;
					if (map[y+DY[d]][x+DX[d]]==CELL_WALL && map[y+DY[d2]][x+DX[d2]]==CELL_WALL)
					{
						return false;
					}
				}
			}
		}
	return true;
}
