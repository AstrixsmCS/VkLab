import Common;

[[vk::binding(0, 0)]] Texture2D<float4> g_SampledImages[];
[[vk::binding(1, 0)]] SamplerState g_Samplers[];

static const uint LINEAR_CLAMP_SAMPLER = 1;

static const uint TONEMAPPER_REINHARD = 0;
static const uint TONEMAPPER_ACES = 1;
static const uint TONEMAPPER_UNCHARTED2 = 2;
static const uint TONEMAPPER_AGX        = 3;
static const uint TONEMAPPER_PBR_NEUTRAL = 4;

struct PushConstants
{
	uint SceneTexture;
	uint ToneMapper;

	float Exposure;
	float WhitePoint;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

static const float3x3 ACESInputMat =
{
	float3(0.59719, 0.35458, 0.04823),
	float3(0.07600, 0.90834, 0.01566),
	float3(0.02840, 0.13383, 0.83777)
};

static const float3x3 ACESOutputMat =
{
	float3( 1.60475, -0.53108, -0.07367),
	float3(-0.10208,  1.10813, -0.00605),
	float3(-0.00327, -0.07276,  1.07602)
};

float3 RRTAndODTFit(float3 value)
{
	const float3 a = value * (value + 0.0245786) - 0.000090537;
	const float3 b = value * (0.983729 * value + 0.4329510) + 0.238081;

	return a / b;
}

float3 ACESFitted(float3 color)
{
	color = mul(ACESInputMat, color);

	color = clamp(color, 0.0, 100.0);

	color = RRTAndODTFit(color);

	color = mul(ACESOutputMat, color);

	return saturate(color);
}

static const float3x3 AgXInputMat =
{
	float3(0.842479062253094,  0.0423282422610123, 0.0423756549057051),
	float3(0.0784335999999992, 0.878468636469772,  0.0784336),
	float3(0.0792237451477643, 0.0791661274605434, 0.879142973793104)
};

static const float3x3 AgXOutputMat =
{
	float3( 1.19687900512017,   -0.0528968517574562, -0.0529716355144438),
	float3(-0.0980208811401368,  1.15190312990417,   -0.0980434501171241),
	float3(-0.0990297440797205, -0.0989611768448433,  1.15107367264116)
};

float3 AgXToneCurve(float3 x)
{
	const float3 x2 = x * x;
	const float3 x4 = x2 * x2;
	const float3 x6 = x4 * x2;

	return
		15.5   * x6
		- 40.14  * x4 * x
		+ 31.96  * x4
		- 6.868  * x2 * x
		+ 0.4298 * x2
		+ 0.1191 * x
		- 0.00232;
}

float3 AgX(float3 color)
{
	const float AgXMinEv = -12.47393;
	const float AgXMaxEv = 4.026069;

	color = max(color, 0.0);

	// Linear Rec.709 -> AgX working space.
	color = mul(color, AgXInputMat);

	// Log2 encoding.
	color = max(color, 1e-10);
	color = clamp(log2(color), AgXMinEv, AgXMaxEv);
	color = (color - AgXMinEv) / (AgXMaxEv - AgXMinEv);

	// AgX sigmoid.
	color = AgXToneCurve(color);

	// AgX working space -> Linear Rec.709.
	color = mul(color, AgXOutputMat);

	// AgX reference output transform.
	color = pow(max(color, 0.0), 2.2);

	return color;
}

float3 KhronosPBRNeutral(float3 color)
{
	const float F90 = 0.04;
	const float Ks = 0.8 - F90;
	const float Kd = 0.15;

	const float x = min(color.r, min(color.g, color.b));

	float f;

	if (x < 2.0 * F90)
		f = x - (x * x) / (4.0 * F90);
	else
		f = F90;

	color -= f;

	const float p = max(color.r, max(color.g, color.b));

	if (p < Ks)
		return color;

	const float pn = 1.0 - ((1.0 - Ks) * (1.0 - Ks)) / (p + 1.0 - 2.0 * Ks);

	color *= pn / p;

	const float desaturation = 1.0 - 1.0 / (Kd * (p - pn) + 1.0);

	return lerp(color, float3(pn, pn, pn), desaturation);
}

float3 Uncharted2Curve(float3 color)
{
	const float A = 0.15;
	const float B = 0.50;
	const float C = 0.10;
	const float D = 0.20;
	const float E = 0.02;
	const float F = 0.30;

	return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

float3 Uncharted2(float3 color)
{
	const float exposureBias = 2.0;
	const float whitePoint = 11.2;

	color = Uncharted2Curve(color * exposureBias);

	const float3 whiteScale = 1.0 / Uncharted2Curve(float3(whitePoint, whitePoint, whitePoint));

	return saturate(color * whiteScale);
}

float Luminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 ExtendedReinhardLuminance(float3 color, float whitePoint)
{
	const float oldLuminance = Luminance(color);

	if (oldLuminance <= 0.0)
		return color;

	const float whitePointSquared = whitePoint * whitePoint;

	const float numerator = oldLuminance * (1.0 + oldLuminance / whitePointSquared);

	const float newLuminance = numerator / (1.0 + oldLuminance);

	return color * (newLuminance / oldLuminance);
}

[shader("vertex")]
VertexOutput main(uint vertexID : SV_VertexID)
{
	const float2 positions[3] =
	{
		float2(-1.0, -1.0),
		float2( 3.0, -1.0),
		float2(-1.0,  3.0)
	};

	const float2 texCoords[3] =
	{
		float2(0.0, 0.0),
		float2(2.0, 0.0),
		float2(0.0, 2.0)
	};

	VertexOutput output;
	output.Position = float4(positions[vertexID], 0.0, 1.0);
	output.TexCoord = texCoords[vertexID];

	return output;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target0
{
	float3 color = g_SampledImages[g_PushConstants.SceneTexture].SampleLevel(g_Samplers[LINEAR_CLAMP_SAMPLER], input.TexCoord, 0.0).rgb;

	color *= g_PushConstants.Exposure;

	switch (g_PushConstants.ToneMapper)
	{
		case TONEMAPPER_ACES:
		{
			color = ACESFitted(color);
			break;
		}

		case TONEMAPPER_AGX:
		{
			color = AgX(color);
			break;
		}

		case TONEMAPPER_PBR_NEUTRAL:
		{
			color = KhronosPBRNeutral(color);
			break;
		}

		case TONEMAPPER_UNCHARTED2:
		{
			color = Uncharted2(color);
			break;
		}

		case TONEMAPPER_REINHARD:
		default:
		{
			color = ExtendedReinhardLuminance(color, g_PushConstants.WhitePoint);
			break;
		}
	}

	color = max(color, 0.0f);
	color = ConvertSRGBFromLinear(color);

	return float4(color, 1.0);
}
