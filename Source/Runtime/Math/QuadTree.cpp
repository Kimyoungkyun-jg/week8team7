#include "EnginePCH.h"
#include "QuadTree.h"

FQuadTree::FQuadTree(uint32 InRootsize) : RootSize(InRootsize)
{
	const bool bValidRoot = RootSize > 0 && (RootSize & (RootSize - 1)) == 0;
	assert(bValidRoot);

	if (!bValidRoot)
	{
		RootSize = 0;
		return;
	}

	FNode Root{};
	Root.Size = RootSize;
	Nodes.Add(Root);
}

bool FQuadTree::InsertSquare(uint16 SquareSize, FRect& OutRect)
{
	OutRect = {};

	if (!IsValidSquareSize(SquareSize) || Nodes.IsEmpty())
	{
		return false;
	}

	int32 BestNodeIndex = InvalidNodeIndex;

	FindBestFitNode(0, SquareSize, BestNodeIndex);

	return BestNodeIndex != InvalidNodeIndex && InsertNode(BestNodeIndex, SquareSize, OutRect);
}

bool FQuadTree::ReleaseSquare(const FRect& InRect)
{
	if (Nodes.IsEmpty() || InRect.X < 0.0f || InRect.Y < 0.0f || InRect.Width <= 0.0f || InRect.Height <= 0.0f || InRect.Width != InRect.Height)
	{
		return false;
	}

	const uint32 X = static_cast<uint32>(InRect.X);
	const uint32 Y = static_cast<uint32>(InRect.Y);
	const uint32 Size = static_cast<uint32>(InRect.Width);
	if (static_cast<float>(X) != InRect.X || static_cast<float>(Y) != InRect.Y || static_cast<float>(Size) != InRect.Width || static_cast<float>(Size) != InRect.Height)
	{
		return false;
	}
	return ReleaseNode(0, X, Y, Size);
}

int32 FQuadTree::AllocateNode()
{
	if (!FreeNodeIndices.IsEmpty())
	{
		const int32 Index = FreeNodeIndices.Last();
		FreeNodeIndices.RemoveAtSwap(FreeNodeIndices.Num() - 1);
		Nodes[Index] = FNode{};
		return Index;
	}

	Nodes.Add(FNode{});
	return Nodes.Num() - 1;
}

void FQuadTree::FindBestFitNode(int32 NodeIndex, uint32 SquareSize, int32& OutBestNodeIndex) const
{
	const FNode& Node = Nodes[NodeIndex];

	if (Node.bOccupied || Node.Size < SquareSize)
	{
		return;
	}

	if (Node.Children[0] == InvalidNodeIndex)
	{
		if (OutBestNodeIndex == InvalidNodeIndex || Node.Size < Nodes[OutBestNodeIndex].Size)
		{
			OutBestNodeIndex = NodeIndex;
		}

		return;
	}

	for (int32 ChildIndex : Node.Children)
	{
		FindBestFitNode(ChildIndex, SquareSize, OutBestNodeIndex);
	}
}

bool FQuadTree::ReleaseNode(int32 NodeIndex, uint32 X, uint32 Y, uint32 SquareSize)
{
	const FNode& Node = Nodes[NodeIndex];
	if (X < Node.X || Y < Node.Y || X + SquareSize > Node.X + Node.Size || Y + SquareSize > Node.Y + Node.Size || SquareSize > Node.Size)
	{
		return false;
	}

	if (Node.Size == SquareSize)
	{
		if (!Node.bOccupied)
		{
			return false;
		}
		Nodes[NodeIndex].bOccupied = false;
		return true;
	}
	if (Node.Children[0] == InvalidNodeIndex)
	{
		return false;
	}

	for (int32 ChildIndex : Node.Children)
	{
		if (ReleaseNode(ChildIndex, X, Y, SquareSize))
		{
			CollapseIfEmpty(NodeIndex);
			return false;
		}
	}
	return false;
}

bool FQuadTree::IsSubtreeEmpty(int32 NodeIndex) const
{
	const FNode& Node = Nodes[NodeIndex];
	if (Node.bOccupied)
	{
		return false;
	}
	if (Node.Children[0] == InvalidNodeIndex)
	{
		return true;
	}
	for (int32 ChildIndex : Node.Children)
	{
		if (!IsSubtreeEmpty(ChildIndex))
		{
			return false;
		}
	}
	return true;
}

void FQuadTree::CollapseIfEmpty(int32 NodeIndex)
{
	if (Nodes[NodeIndex].Children[0] == InvalidNodeIndex)
	{
		return;
	}
	for (int32 ChildIndex : Nodes[NodeIndex].Children)
	{
		if (!IsSubtreeEmpty(ChildIndex))
		{
			return;
		}
	}
	const int32 Children[4] = { Nodes[NodeIndex].Children[0], Nodes[NodeIndex].Children[1], Nodes[NodeIndex].Children[2], Nodes[NodeIndex].Children[3] };
	for (int32 ChildIndex : Children)
	{
		RecycleSubtree(ChildIndex);
	}
	for (int32 i = 0; i < 4;++i)
	{
		Nodes[NodeIndex].Children[i] = InvalidNodeIndex;
	}
}

void FQuadTree::RecycleSubtree(int32 NodeIndex)
{
	const int32 Children[4] = { Nodes[NodeIndex].Children[0], Nodes[NodeIndex].Children[1], Nodes[NodeIndex].Children[2], Nodes[NodeIndex].Children[3] };
	for (int32 ChildIndex : Children)
	{
		if (ChildIndex != InvalidNodeIndex)
		{
			RecycleSubtree(ChildIndex);
		}
	}
	Nodes[NodeIndex] = FNode{};
	FreeNodeIndices.Add(NodeIndex);
}

bool FQuadTree::IsValidSquareSize(uint32 SquareSize) const
{
	return RootSize > 0 && SquareSize > 0 && SquareSize <= RootSize && (SquareSize & (SquareSize - 1)) == 0;
}

void FQuadTree::SplitNode(int32 NodeIndex)
{
	if (Nodes[NodeIndex].Children[0] != InvalidNodeIndex)
	{
		return;
	}
	const uint32 NodeX = Nodes[NodeIndex].X;
	const uint32 NodeY = Nodes[NodeIndex].Y;
	const uint32 HalfSize = Nodes[NodeIndex].Size / 2;

	if (HalfSize == 0)
	{
		return;
	}
	
	int32 ChildIndices[4]{};

	for (int32 i = 0; i < 4; ++i)
	{
		FNode Child{};
		Child.X = NodeX + ((i & 1) ? HalfSize : 0u);
		Child.Y = NodeY + ((i & 2) ? HalfSize : 0u);
		Child.Size = HalfSize;
		const int32 ChildIndex = AllocateNode();
		Nodes[ChildIndex] = Child;
		ChildIndices[i] = ChildIndex;
	}

	for (int32 i = 0; i < 4; ++i)
	{
		Nodes[NodeIndex].Children[i] = ChildIndices[i];
	}
}

bool FQuadTree::InsertNode(int32 NodeIndex, uint32 SquareSize, FRect& OutRect)
{
	if (Nodes[NodeIndex].bOccupied || SquareSize > Nodes[NodeIndex].Size)
	{
		return false;
	}

	if (Nodes[NodeIndex].Children[0] != InvalidNodeIndex)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			const int32 ChildIndex = Nodes[NodeIndex].Children[i];
			if (InsertNode(ChildIndex, SquareSize, OutRect))
			{
				return true;
			}
		}
		return false;
	}

	if (SquareSize == Nodes[NodeIndex].Size)
	{
		Nodes[NodeIndex].bOccupied = true;
		OutRect.X = static_cast<float>(Nodes[NodeIndex].X);
		OutRect.Y = static_cast<float>(Nodes[NodeIndex].Y);
		OutRect.Width = static_cast<float>(SquareSize);
		OutRect.Height = static_cast<float>(SquareSize);
		return true;
	}
	
	if (Nodes[NodeIndex].Size / 2 < SquareSize)
	{
		return false;
	}

	SplitNode(NodeIndex);

	for (int32 i = 0; i < 4; ++i)
	{
		const int32 ChildIndex = Nodes[NodeIndex].Children[i];
		if (InsertNode(ChildIndex, SquareSize, OutRect))
		{
			return true;
		}
	}

	return false;
}
