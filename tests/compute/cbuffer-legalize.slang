//TEST(compute):COMPARE_COMPUTE:-cpu
//TEST(compute):COMPARE_COMPUTE:

//TEST_INPUT:cbuffer(data=[1 2 3 4]):dxbinding(0),glbinding(0),name=C.p.c
//TEST_INPUT:Texture2D(size=4, content = one) : dxbinding(0),glbinding(0),name=C.p.t
//TEST_INPUT: Sampler : dxbinding(0),glbinding(0,1,2,3,4,5,6),name=C.p.s
//TEST_INPUT: ubuffer(data=[0 0 0 0], stride=4):dxbinding(0),glbinding(0),out,name=outputBuffer

struct P
{
	uint4 			c;
	Texture2D 		t;
	SamplerState 	s;
};

float4 test(P p)
{
	return p.t.SampleLevel(p.s, float2(0.0), 0) + p.c;
}

cbuffer C
{
	P p;
};

RWStructuredBuffer<float> outputBuffer;

[numthreads(1, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	float4 outVal = test(p);

    outputBuffer[0] = outVal.x;
	outputBuffer[1] = outVal.y;
	outputBuffer[2] = outVal.z;
	outputBuffer[3] = outVal.w;
}