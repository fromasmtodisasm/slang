// glsl-static-const-array.slang

//TEST(compute):COMPARE_COMPUTE:-vk

static const int gData[4] =
{
    0xA, 0xB, 0xC, 0xD,
};

//TEST_INPUT:ubuffer(data=[0 0 0 0], stride=4):dxbinding(0),glbinding(0),out
RWStructuredBuffer<int> gBuffer;

[numthreads(4,1,1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    gBuffer[tid.x] = gData[tid.x];
}
