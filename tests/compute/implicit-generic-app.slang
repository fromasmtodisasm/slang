//TEST(compute):COMPARE_COMPUTE:
//TEST(compute):COMPARE_COMPUTE:-cpu

// Testing that we can implicitly specialize a generic
// that has a constrained type parameter.


interface ISimple
{
	int getVal();
}

struct Simple : ISimple
{
	int val;

	int getVal() { return val; }
};

int doIt<T : ISimple>(T obj)
{
	return obj.getVal();
}

int test(int val)
{
	Simple simple;
	simple.val = val;
	return doIt(simple);
}

//TEST_INPUT:ubuffer(data=[0 0 0 0], stride=4):dxbinding(0),glbinding(0),out,name=outputBuffer
RWStructuredBuffer<int> outputBuffer : register(u0);

[numthreads(4, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	int tid = (int) dispatchThreadID.x;
	int outVal = test(tid);
	outputBuffer[tid] = outVal;
}