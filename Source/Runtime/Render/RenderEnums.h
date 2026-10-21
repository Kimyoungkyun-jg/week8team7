#pragma once

#include <cstdint>

enum class EShaderType
{
	Vertex,
	Pixel,
	Compute,
};

enum class ERenderingPath
{
    Forward,
    Deferred
};

enum class ELightingModel
{
    Gouraud,
    Lambert,
    Phong
};

enum class ELightCullingMode
{
    Off = 0,
    Tiled = 1,
    Clustered = 2
};

enum class EPcfMode : uint32_t
{
    Off = 0,
    Sampel8, 
    Sample16 = 2,
    Sample32 = 3,
    PCSS,
    Count
};
