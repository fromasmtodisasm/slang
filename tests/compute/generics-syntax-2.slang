//TEST(compute):COMPARE_COMPUTE:
//TEST(compute):COMPARE_COMPUTE:-cpu

// Confirm that generics syntax can be used in user
// code and generates valid output.

//TEST_INPUT:ubuffer(data=[0 0 0 0], stride=4):dxbinding(0),glbinding(0),out,name=outputBuffer
RWStructuredBuffer<float> outputBuffer;

struct GenStruct<T>
{
    T x;
};

GenStruct<T> test<T>(T val)
{
    GenStruct<T> rs;
    rs.x = val;
    return rs;
}


[numthreads(4, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint tid = dispatchThreadID.x;
    float inVal = float(tid);
    GenStruct<float> outVal = test(inVal);
    outputBuffer[tid] = outVal.x;
}