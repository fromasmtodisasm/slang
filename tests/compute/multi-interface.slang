//TEST(compute):COMPARE_COMPUTE:
//TEST(compute):COMPARE_COMPUTE:-cpu

//TEST_INPUT:ubuffer(data=[0 0 0 0], stride=4):dxbinding(0),glbinding(0),out,name=outputBuffer
RWStructuredBuffer<float> outputBuffer;

interface IAdd
{
    float addf(float u, float v);
}

interface ISub
{
    float subf(float u, float v);
}

interface IAddAndSub
{
    float addf(float u, float v);
    float subf(float u, float v);    
}

struct Simple : IAdd, ISub, IAddAndSub
{
    float addf(float u, float v)
    {
        return u+v;
    }
    float subf(float u, float v)
    {
        return u-v;
    }
};

float testAddSub<T:IAddAndSub>(T t)
{
    return t.subf(t.addf(1.0, 1.0), 1.0);
}

[numthreads(4, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Simple s;
	float outVal = testAddSub(s);
	outputBuffer[dispatchThreadID.x] = outVal;
}