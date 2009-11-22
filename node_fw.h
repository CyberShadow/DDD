struct Node
{
	// compiler hack - minimum bitfield size is 32 bits
	union
	{
		Step step;
		struct
		{
			short bitfield1;
			Step stepToFinish;
		};
		struct
		{
			int bitfield2;
			NODEI next;
			NODEI parent;
			uint16_t toFinish;
			uint16_t lastVisit;
		};
	};
};
