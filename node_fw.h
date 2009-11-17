struct Node
{
	// compiler hack - minimum bitfield size is 32 bits
	union
	{
		struct
		{
			uint16_t bitfield;
			uint16_t toFinish;
			uint16_t lastVisit;
		};
		Step step; // CAREFUL! Setting "step" directly will overwrite "toFinish" because "step" is 4 bytes in size.
	};
};
