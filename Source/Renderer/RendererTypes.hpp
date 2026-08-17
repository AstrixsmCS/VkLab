#pragma once

#include <cstdint>

enum class LoadOp : uint8_t
{
	Load = 0,
	Clear,
	DontCare,
	None,
	Invalid = 0xFF
};

enum class StoreOp : uint8_t
{
	Store = 0,
	DontCare,
	None
};

enum class Format : uint8_t
{
	Invalid = 0,

	R_UN8,
	R_UI16,
	R_UI32,
	R_UN16,
	R_F16,
	R_F32,

	A_UN8,

	RG_UN8,
	RG_UI16,
	RG_UI32,
	RG_UN16,
	RG_F16,
	RG_F32,

	RGBA_UN8,
	RGBA_UI32,
	RGBA_F16,
	RGBA_F32,
	RGBA_SRGB8,

	BGRA_UN8,
	BGRA_SRGB8,

	A2B10G10R10_UN,
	A2R10G10B10_UN,
	A1B5G5R5_UN,

	ETC2_RGB8,
	ETC2_SRGB8,
	BC7_RGBA,
	BC7_SRGBA,

	D32_Float_S8_UInt,
	D32_Float,
	D24_UNorm_S8_UInt,
	D16_UNorm_S8_UInt,
	D16_UNorm,

	YUV_NV12,
	YUV_420p
};

enum class PolygonMode : uint8_t
{
	Fill = 0,
	Line,
	Point
};

enum class CompareOp : uint8_t
{
	Never = 0,
	Less,
	Equal,
	LessEqual,
	Greater,
	NotEqual,
	GreaterEqual,
	Always
};

enum class StencilOp : uint8_t
{
	Keep = 0,
	Zero,
	Replace,
	IncrementClamp,
	DecrementClamp,
	Invert,
	IncrementWrap,
	DecrementWrap
};

enum class BlendOp : uint8_t
{
	Add = 0,
	Subtract,
	ReverseSubtract,
	Min,
	Max
};

enum class BlendFactor : uint8_t
{
	Zero = 0,
	One,
	SrcColor,
	OneMinusSrcColor,
	SrcAlpha,
	OneMinusSrcAlpha,
	DstColor,
	OneMinusDstColor,
	DstAlpha,
	OneMinusDstAlpha,
	SrcAlphaSaturated,
	BlendColor,
	OneMinusBlendColor,
	BlendAlpha,
	OneMinusBlendAlpha,
	Src1Color,
	OneMinusSrc1Color,
	Src1Alpha,
	OneMinusSrc1Alpha
};

enum class CullMode : uint8_t
{
	None = 0,
	Front,
	Back
};

enum class WindingMode : uint8_t
{
	CCW = 0,
	CW
};

enum class TextureType : uint8_t
{
	Texture2D = 0,
	Texture3D,
	Cube
};

enum class SamplerFilter : uint8_t
{
	Nearest = 0,
	Linear
};

enum class SamplerMip : uint8_t
{
	Disabled = 0,
	Nearest,
	Linear
};

enum class SamplerWrap : uint8_t
{
	Repeat = 0,
	Clamp,
	ClampToBorder,
	MirrorRepeat,
	MirrorClampToEdge
};

enum class Topology : uint8_t
{
	Point = 0,
	Line,
	LineStrip,
	Triangle,
	TriangleStrip,
	Patch
};

enum class ColorSpace : uint8_t
{
	SRGBNonlinear = 0,
	SRGBExtendedLinear,
	HDR10,
	BT709Linear
};

enum class ShaderStage : uint32_t
{
	None = 0,

	Vertex,
	Fragment,
	Compute,

	RayGen,
	Miss,
	ClosestHit,
	AnyHit,
	Intersection,
	Callable
};
