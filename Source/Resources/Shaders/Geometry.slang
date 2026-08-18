struct VertexInput
{
	float3 position : POSITION;
	float3 color    : COLOR0;
};

struct VertexOutput
{
	float4 position : SV_Position;
	float3 color    : COLOR0;
};

[shader("vertex")]
VertexOutput main(VertexInput input)
{
	VertexOutput output;
	output.position = float4(input.position, 1.0);
	output.color    = input.color;
	return output;
}

[shader("fragment")]
float4 main(VertexOutput input) : SV_Target
{
	return float4(input.color, 1.0);
}
