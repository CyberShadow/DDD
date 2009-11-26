struct Node
{
	// compiler hack - minimum bitfield size is 32 bits
	union
	{
		struct
		{
			uint16_t bitfield;
			uint16_t debugDummy;
			NODEI next;
			NODEI parent;
		};
		Step step; // CAREFUL! Setting "step" directly will partially overwrite "next" because "step" is 4 bytes in size.
	};
};
