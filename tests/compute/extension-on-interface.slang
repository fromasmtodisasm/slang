//TEST(compute):COMPARE_COMPUTE:
//TEST(compute):COMPARE_COMPUTE:-cpu

//TEST_INPUT:ubuffer(data=[0 0 0 0], stride=4):dxbinding(0),glbinding(0),out,name=outputBuffer
RWStructuredBuffer<float> outputBuffer;

interface IOp
{
    float addf(float u, float v);
}

interface ISub
{
    float subf(float u, float v);
}

extension IOp : ISub
{
}

struct Simple : IOp
{
    float base;
    float addf(float u, float v)
    {
        return u+v;
    }
};

__extension Simple : ISub
{
    float subf(float u, float v)
    {
        return base+u-v;
    }
};

float testAddSub<T:IOp>(T t)
{
    return t.subf(t.addf(1.0, 1.0), 1.0);
}

[numthreads(4, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Simple s;
    s.base = 0.0;
	float outVal = testAddSub(s);
	outputBuffer[dispatchThreadID.x] = outVal;
}