#pragma once

#include "Core/Types.h"
#include "Math/Rect.h"
#include "Containers/Array.h"
class FQuadTree
{
public:
	explicit FQuadTree(uint32 InRootsize);

	bool InsertSquare(uint16 SquareSize, FRect& OutRect);
	bool ReleaseSquare(const FRect& InRect);
	uint32 GetRootSize() const { return RootSize; }

private:
	static constexpr int32 InvalidNodeIndex = -1;

	struct FNode
	{
		uint32 X = 0;
		uint32 Y = 0;
		uint32 Size = 0;
		int32 Children[4] =
		{
			InvalidNodeIndex,
			InvalidNodeIndex,
			InvalidNodeIndex,
			InvalidNodeIndex
		};
		
		bool bOccupied = false;
	};
	
	bool IsValidSquareSize(uint32 SquareSize) const;
	int32 AllocateNode();
	void SplitNode(int32 NodeIndex);
	void FindBestFitNode(int32 NodeIndex, uint32 SquareSize, int32& OutBestNodeIndex) const;
	bool InsertNode(int32 NodeIndex, uint32 SquareSize, FRect& OutRect);
	bool ReleaseNode(int32 NodeIndex, uint32 X, uint32 Y, uint32 SquareSize);
	bool IsSubtreeEmpty(int32 NodeIndex) const;
	void CollapseIfEmpty(int32 NodeIndex);
	void RecycleSubtree(int32 NodeIndex);

	uint32 RootSize = 0;
	TArray<FNode> Nodes;
	TArray<int32> FreeNodeIndices;
};