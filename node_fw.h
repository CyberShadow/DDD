struct Node
{
	// compiler hack - minimum bitfield size is 32 bits
	union
	{
		Step step;  // CAREFUL! Setting "step" directly will overwrite "stepToFinish" because "step" is 4 bytes in size.
		struct
		{
			uint16_t bitfield1;
			Step stepToFinish;   // CAREFUL! Setting "stepToFinish" directly will overwrite "toFinish" because "stepToFinish" is 4 bytes in size.
		};
		struct
		{
			uint32_t bitfield2;
			uint16_t toFinish;
			uint16_t lastVisit;
			NODEI next;
			NODEI parent;
		};
	};
};
