//TEST(smoke,compute):COMPARE_COMPUTE:
//TEST(smoke,compute):COMPARE_COMPUTE:-cpu

// Confirm that generics syntax can be used in user
// code and generates valid output.

//TEST_INPUT:ubuffer(data=[0 0 0 0], stride=4):dxbinding(0),glbinding(0),out,name=outputBuffer
RWStructuredBuffer<float> outputBuffer;

struct GenStruct<T>
{
	T x;
};

T test<T>(T val)
{
	return val;
}


[numthreads(4, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	uint tid = dispatchThreadID.x;

	float inVal = float(tid);

	float outVal = test<float>(inVal);

	outputBuffer[tid] = outVal;
}