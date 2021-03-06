// render-test-main.cpp

#include "options.h"
#include "render.h"
#include "render-d3d11.h"
#include "render-d3d12.h"
#include "render-gl.h"
#include "render-vk.h"

#include "slang-support.h"
#include "surface.h"
#include "png-serialize-util.h"

#include "shader-renderer-util.h"

#include "../source/core/slang-io.h"

#include "core/slang-token-reader.h"

#include "shader-input-layout.h"
#include <stdio.h>
#include <stdlib.h>

#define SLANG_PRELUDE_NAMESPACE CPPPrelude
#include "../../prelude/slang-cpp-types.h"

#include "../../source/core/slang-test-tool-util.h"
#include "../../source/core/slang-memory-arena.h"

#include "cpu-memory-binding.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef WIN32_LEAN_AND_MEAN
#undef NOMINMAX

#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif

namespace renderer_test {

using Slang::Result;

int gWindowWidth = 1024;
int gWindowHeight = 768;

class Window: public RefObject
{
public:
    SlangResult initialize(int width, int height);

    void show();

    void* getHandle() const { return m_hwnd; }

    Window() {}
    ~Window();

    static LRESULT CALLBACK windowProc(HWND    windowHandle,
        UINT    message,
        WPARAM  wParam,
        LPARAM  lParam);

protected:

    HINSTANCE m_hinst = nullptr;
    HWND m_hwnd = nullptr;
};

//
// We use a bare-minimum window procedure to get things up and running.
//

/* static */LRESULT CALLBACK Window::windowProc(
    HWND    windowHandle,
    UINT    message,
    WPARAM  wParam,
    LPARAM  lParam)
{
    switch (message)
    {
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(windowHandle, message, wParam, lParam);
}

static ATOM _getWindowClassAtom(HINSTANCE hinst)
{
    static ATOM s_windowClassAtom;

    if (s_windowClassAtom)
    {
        return s_windowClassAtom;
    }
    WNDCLASSEXW windowClassDesc;
    windowClassDesc.cbSize = sizeof(windowClassDesc);
    windowClassDesc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    windowClassDesc.lpfnWndProc = &Window::windowProc;
    windowClassDesc.cbClsExtra = 0;
    windowClassDesc.cbWndExtra = 0;
    windowClassDesc.hInstance = hinst;
    windowClassDesc.hIcon = 0;
    windowClassDesc.hCursor = 0;
    windowClassDesc.hbrBackground = 0;
    windowClassDesc.lpszMenuName = 0;
    windowClassDesc.lpszClassName = L"SlangRenderTest";
    windowClassDesc.hIconSm = 0;
    s_windowClassAtom = RegisterClassExW(&windowClassDesc);
        
    return s_windowClassAtom;
}

SlangResult Window::initialize(int widthIn, int heightIn)
{
    // Do initial window-creation stuff here, rather than in the renderer-specific files

    m_hinst = GetModuleHandleA(0);

    // First we register a window class.
    ATOM windowClassAtom = _getWindowClassAtom(m_hinst);
    if (!windowClassAtom)
    {
        fprintf(stderr, "error: failed to register window class\n");
        return SLANG_FAIL;
    }

    // Next, we create a window using that window class.

    // We will create a borderless window since our screen-capture logic in GL
    // seems to get thrown off by having to deal with a window frame.
    DWORD windowStyle = WS_POPUP;
    DWORD windowExtendedStyle = 0;

    RECT windowRect = { 0, 0, widthIn, heightIn };
    AdjustWindowRectEx(&windowRect, windowStyle, /*hasMenu=*/false, windowExtendedStyle);

    {
        auto width = windowRect.right - windowRect.left;
        auto height = windowRect.bottom - windowRect.top;

        LPWSTR windowName = L"Slang Render Test";
        m_hwnd = CreateWindowExW(
            windowExtendedStyle,
            (LPWSTR)windowClassAtom,
            windowName,
            windowStyle,
            0, 0, // x, y
            width, height,
            NULL, // parent
            NULL, // menu
            m_hinst,
            NULL);
    }
    if (!m_hwnd)
    {
        fprintf(stderr, "error: failed to create window\n");
        return SLANG_FAIL;
    }

    return SLANG_OK;
}


void Window::show()
{
    // Once initialization is all complete, we show the window...
    int showCommand = SW_SHOW;
    ShowWindow(m_hwnd, showCommand);
}

Window::~Window()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
    }
}

//
// For the purposes of a small example, we will define the vertex data for a
// single triangle directly in the source file. It should be easy to extend
// this example to load data from an external source, if desired.
//

struct Vertex
{
    float position[3];
    float color[3];
    float uv[2];
};

static const Vertex kVertexData[] =
{
    { { 0,  0, 0.5 }, {1, 0, 0} , {0, 0} },
    { { 0,  1, 0.5 }, {0, 0, 1} , {1, 0} },
    { { 1,  0, 0.5 }, {0, 1, 0} , {1, 1} },
};
static const int kVertexCount = SLANG_COUNT_OF(kVertexData);

using namespace Slang;

class RenderTestApp
{
	public:

		// At initialization time, we are going to load and compile our Slang shader
		// code, and then create the API objects we need for rendering.
	Result initialize(SlangSession* session, Renderer* renderer, Options::ShaderProgramType shaderType, const ShaderCompilerUtil::Input& input);
	void runCompute();
	void renderFrame();
	void finalize();

	BindingStateImpl* getBindingState() const { return m_bindingState; }

    Result writeBindingOutput(const char* fileName);

    Result writeScreen(const char* filename);

	protected:
		/// Called in initialize
	Result _initializeShaders(SlangSession* session, Renderer* renderer, Options::ShaderProgramType shaderType, const ShaderCompilerUtil::Input& input);

	// variables for state to be used for rendering...
	uintptr_t m_constantBufferSize, m_computeResultBufferSize;

	RefPtr<Renderer> m_renderer;

	RefPtr<BufferResource>	m_constantBuffer;
	RefPtr<InputLayout>     m_inputLayout;
	RefPtr<BufferResource>  m_vertexBuffer;
	RefPtr<ShaderProgram>   m_shaderProgram;
    RefPtr<PipelineState>   m_pipelineState;
	RefPtr<BindingStateImpl>    m_bindingState;

	ShaderInputLayout m_shaderInputLayout;              ///< The binding layout
    int m_numAddedConstantBuffers;                      ///< Constant buffers can be added to the binding directly. Will be added at the end.
};

// Entry point name to use for vertex/fragment shader
static const char vertexEntryPointName[]    = "vertexMain";
static const char fragmentEntryPointName[]  = "fragmentMain";
static const char computeEntryPointName[]	= "computeMain";

static SlangResult _readSource(const String& inSourcePath, List<char>& outSourceText)
{
    // Read in the source code
    FILE* sourceFile = fopen(inSourcePath.getBuffer(), "rb");
    if (!sourceFile)
    {
        fprintf(stderr, "error: failed to open '%s' for reading\n", inSourcePath.getBuffer());
        return SLANG_FAIL;
    }
    fseek(sourceFile, 0, SEEK_END);
    size_t sourceSize = ftell(sourceFile);
    fseek(sourceFile, 0, SEEK_SET);

    outSourceText.setCount(sourceSize + 1);
    fread(outSourceText.getBuffer(), sourceSize, 1, sourceFile);
    fclose(sourceFile);
    outSourceText[sourceSize] = 0;

    return SLANG_OK;
}

struct CompileOutput
{
    ShaderCompilerUtil::Output compileOutput;
    ShaderInputLayout layout;
};

static SlangResult _compile(SlangSession* session, const String& sourcePath, Options::ShaderProgramType shaderType, const ShaderCompilerUtil::Input& input, CompileOutput& output)
{
    List<char> sourceText;
    SLANG_RETURN_ON_FAIL(_readSource(sourcePath, sourceText));

    auto& layout = output.layout;

    // Default the amount of renderTargets based on shader type
    switch (shaderType)
    {
        default:
            layout.numRenderTargets = 1;
            break;

        case Options::ShaderProgramType::Compute:
            layout.numRenderTargets = 0;
            break;
    }

    // Parse the layout
    layout.parse(sourceText.getBuffer());
    layout.updateForTarget(input.target);

    // Setup SourceInfo
    ShaderCompileRequest::SourceInfo sourceInfo;
    sourceInfo.path = sourcePath.getBuffer();
    sourceInfo.dataBegin = sourceText.getBuffer();
    // Subtract 1 because it's zero terminated
    sourceInfo.dataEnd = sourceText.getBuffer() + sourceText.getCount() - 1;

    ShaderCompileRequest compileRequest;
    compileRequest.source = sourceInfo;
    if (shaderType == Options::ShaderProgramType::Graphics || shaderType == Options::ShaderProgramType::GraphicsCompute)
    {
        compileRequest.vertexShader.source = sourceInfo;
        compileRequest.vertexShader.name = vertexEntryPointName;
        compileRequest.fragmentShader.source = sourceInfo;
        compileRequest.fragmentShader.name = fragmentEntryPointName;
    }
    else
    {
        compileRequest.computeShader.source = sourceInfo;
        compileRequest.computeShader.name = computeEntryPointName;
    }
    compileRequest.globalGenericTypeArguments = layout.globalGenericTypeArguments;
    compileRequest.entryPointGenericTypeArguments = layout.entryPointGenericTypeArguments;
    compileRequest.globalExistentialTypeArguments = layout.globalExistentialTypeArguments;
    compileRequest.entryPointExistentialTypeArguments = layout.entryPointExistentialTypeArguments;

    return ShaderCompilerUtil::compileProgram(session, input, compileRequest, output.compileOutput);
}

static SlangResult _writeBindings(const ShaderInputLayout& layout, const List<CPUMemoryBinding::Buffer>& buffers, const String& fileName)
{
    FILE * f = fopen(fileName.getBuffer(), "wb");
    if (!f)
    {
        return SLANG_FAIL;
    }

    const auto& entries = layout.entries;

    for (int i = 0; i < entries.getCount(); ++i)
    {
        const auto& entry = entries[i];
        if (entry.isOutput)
        {
            const auto& buffer = buffers[i];

            unsigned int* ptr = (unsigned int*)buffer.m_data;

            const int size = int(entry.bufferData.getCount());
            // Must be the same size or less thatn allocated buffer
            SLANG_ASSERT(size * sizeof(unsigned int) <= buffer.m_sizeInBytes);

            for (int i = 0; i < size; ++i)
            {
                unsigned int v = ptr[i];

                fprintf(f, "%X\n", v);
            }
        }
    }
    fclose(f);
    return SLANG_OK;
}

struct CPUResource: public RefObject
{
    void* getInterface() const { return m_interface; }
    void* m_interface;
};

template <int COUNT>
struct OneTexture2D: public CPUResource, public CPPPrelude::ITexture2D
{
    void setOne(void* out)
    {
        float* dst = (float*)out;
        for (int i = 0; i < COUNT; ++i)
        {
            dst[i] = 1.0f;
        }
    }

    virtual void Load(const CPPPrelude::int3& v, void* out) SLANG_OVERRIDE
    {
        setOne(out);
    }
    virtual void Sample(CPPPrelude::SamplerState samplerState, const CPPPrelude::float2& loc, void* out) SLANG_OVERRIDE
    {
        setOne(out);
    }
    virtual void SampleLevel(CPPPrelude::SamplerState samplerState, const CPPPrelude::float2& loc, float level, void* out) SLANG_OVERRIDE
    {
        setOne(out);
    }

    OneTexture2D()
    {
        m_interface = static_cast<CPPPrelude::ITexture2D*>(this);
    }
};

static CPUResource* _newOneTexture2D(int elemCount)
{
    switch (elemCount)
    {
    case 1: return new OneTexture2D<1>();
    case 2: return new OneTexture2D<2>();
    case 3: return new OneTexture2D<3>();
    case 4: return new OneTexture2D<4>();
    default: return nullptr;
    }
}

static SlangResult _doCPUCompute(SlangSession* session, const String& sourcePath, Options::ShaderProgramType shaderType, const ShaderCompilerUtil::Input& input)
{
    CompileOutput output;
    SLANG_RETURN_ON_FAIL(_compile(session, sourcePath, shaderType, input, output));

    ComPtr<ISlangSharedLibrary> sharedLibrary;
    SLANG_RETURN_ON_FAIL(spGetEntryPointHostCallable(output.compileOutput.request, 0, 0, sharedLibrary.writeRef()));

    // Use reflection to find the entry point name
    auto request = output.compileOutput.request;

    struct UniformState;
    typedef void(*Func)(CPPPrelude::ComputeVaryingInput* varyingInput, CPPPrelude::UniformEntryPointParams* params, UniformState* uniformState);

    auto reflection = (slang::ShaderReflection*) spGetReflection(request);

    slang::EntryPointReflection* entryPoint = nullptr;
    Func func = nullptr;
    {
        auto entryPointCount = reflection->getEntryPointCount();
        SLANG_ASSERT(entryPointCount == 1);

        entryPoint = reflection->getEntryPointByIndex(0);

        const char* entryPointName = entryPoint->getName();
        func = (Func) sharedLibrary->findFuncByName(entryPointName);

        if (!func)
        {
            return SLANG_FAIL;
        }
    }

    CPUMemoryBinding binding;
    SLANG_RETURN_ON_FAIL(binding.init(reflection, 0));

    List<CPUMemoryBinding::Buffer> buffers;

    // Okay we need to find all of the bindings and match up to those in the layout
    ShaderInputLayout& layout = output.layout;

    List<RefPtr<CPUResource> > resources;

    {
        auto& outStream = StdWriters::getOut();       
        auto& entries = layout.entries;
        buffers.setCount(entries.getCount());

        for (int entryIndex = 0; entryIndex < entries.getCount(); ++entryIndex)
        {
            auto& entry = entries[entryIndex];

            if (entry.name.getLength() == 0)
            {
                outStream.print("No 'name' specified for resources in '%s'\n", sourcePath.getBuffer());
                return SLANG_FAIL;
            }

            // We will parse the 'name' as may be path to a resource
            TokenReader parser(entry.name);

            CPUMemoryBinding::Location location;

            {
                Token nameToken = parser.ReadToken();
                if (nameToken.Type != TokenType::Identifier)
                {
                    outStream.print("Invalid input syntax at line %d", int(parser.NextToken().Position.Line));
                    return SLANG_FAIL;
                }
                location = binding.find(nameToken.Content.getBuffer());
                if (location.isInvalid())
                {
                    outStream.print("Unable to find entry in '%s' for '%s' (for CPU name must be specified) \n", sourcePath.getBuffer(), entry.name.getBuffer());
                    return SLANG_FAIL;
                }
            }

            while (!parser.IsEnd())
            {
                Token token = parser.NextToken(0);

                if (token.Type == TokenType::LBracket)
                {
                    parser.ReadToken();
                    int index = parser.ReadInt();
                    SLANG_ASSERT(index >= 0);

                    location = location.toIndex(index);
                    if (location.isInvalid())
                    {
                        outStream.print("Unable to find entry in '%d' in '%s'\n", index, entry.name.getBuffer());
                        return SLANG_FAIL;
                    }
                    parser.ReadMatchingToken(TokenType::RBracket);
                }
                else if (token.Type == TokenType::Dot)
                {
                    parser.ReadToken();
                    Token identifierToken = parser.ReadMatchingToken(TokenType::Identifier);

                    location = location.toField(identifierToken.Content.getBuffer());
                    if (location.isInvalid())
                    {
                        outStream.print("Unable to find field '%s' in '%s'\n", identifierToken.Content.getBuffer(), entry.name.getBuffer());
                        return SLANG_FAIL;
                    }
                }
                else if (token.Type == TokenType::Comma)
                {
                    // Break out
                    break;
                }
                else
                {
                    throw TextFormatException("Invalid input syntax at line " + parser.NextToken().Position.Line);
                }
            }

            auto& srcEntry = layout.entries[entryIndex];

            auto typeLayout = location.getTypeLayout();
            const auto kind = typeLayout->getKind();
            switch (kind)
            {
                case slang::TypeReflection::Kind::Vector:
                case slang::TypeReflection::Kind::Matrix:
                case slang::TypeReflection::Kind::Array:
                case slang::TypeReflection::Kind::Scalar:
                case slang::TypeReflection::Kind::Struct:
                {
                    SLANG_RETURN_ON_FAIL(binding.setInplace(location, srcEntry.bufferData.getBuffer(), srcEntry.bufferData.getCount() * sizeof(unsigned int)));
                    break;
                }
                default:
                    break;
                case slang::TypeReflection::Kind::ConstantBuffer:
                {
                    SLANG_RETURN_ON_FAIL(binding.setBufferContents(location, srcEntry.bufferData.getBuffer(),srcEntry.bufferData.getCount() * sizeof(unsigned int) ));
                    break;
                }
                case slang::TypeReflection::Kind::ParameterBlock:
                {
                    auto elementTypeLayout = typeLayout->getElementTypeLayout();
                    SLANG_UNUSED(elementTypeLayout);
                    break;
                }
                case slang::TypeReflection::Kind::TextureBuffer:
                {
                    auto elementTypeLayout = typeLayout->getElementTypeLayout();
                    SLANG_UNUSED(elementTypeLayout);
                    break;
                }
                case slang::TypeReflection::Kind::ShaderStorageBuffer:
                {
                    auto elementTypeLayout = typeLayout->getElementTypeLayout();
                    SLANG_UNUSED(elementTypeLayout);
                    break;
                }
                case slang::TypeReflection::Kind::GenericTypeParameter:
                {
                    const char* name = typeLayout->getName();
                    SLANG_UNUSED(name);
                    break;
                }
                case slang::TypeReflection::Kind::Interface:
                {
                    const char* name = typeLayout->getName();
                    SLANG_UNUSED(name);
                    break;
                } 
                case slang::TypeReflection::Kind::Resource:
                {
                    auto type = typeLayout->getType();
                    auto shape = type->getResourceShape();

                    //auto access = type->getResourceAccess();

                    switch (shape & SLANG_RESOURCE_BASE_SHAPE_MASK)
                    {
                        default:
                            assert(!"unhandled case");
                            break;
                        case SLANG_TEXTURE_2D:
                        {
                            slang::TypeReflection* typeReflection = location.getTypeLayout()->getResourceResultType();

                            int count = 1;
                            if (typeReflection->getKind() == slang::TypeReflection::Kind::Vector)
                            {
                                count = int(typeReflection->getElementCount());
                            }

                            RefPtr<CPUResource> resource = _newOneTexture2D(count);   
                            resources.add(resource);

                            SLANG_RETURN_ON_FAIL(binding.setObject(location, resource->getInterface()));
                            break;
                        }
                        case SLANG_TEXTURE_1D:
                        case SLANG_TEXTURE_3D:
                        case SLANG_TEXTURE_CUBE:
                        case SLANG_TEXTURE_BUFFER:
                        {
                            // Just set to null for now
                            SLANG_RETURN_ON_FAIL(binding.setObject(location, nullptr));
                            break;
                        }
                        case SLANG_BYTE_ADDRESS_BUFFER:
                        case SLANG_STRUCTURED_BUFFER:
                        {
                            CPUMemoryBinding::Buffer buffer;
                            SLANG_RETURN_ON_FAIL(binding.setNewBuffer(location, srcEntry.bufferData.getBuffer(), srcEntry.bufferData.getCount() * sizeof(unsigned int), buffer ));
                            buffers[entryIndex] = buffer;
                            break;
                        }
                    }
                    if (shape & SLANG_TEXTURE_ARRAY_FLAG)
                    {

                    }
                    if (shape & SLANG_TEXTURE_MULTISAMPLE_FLAG)
                    {

                    }

                    break;
                }
            }
        }
    }

    SlangUInt numThreadsPerAxis[3];
    entryPoint->getComputeThreadGroupSize(3, numThreadsPerAxis);

    {
        UniformState* uniformState = (UniformState*)binding.m_rootBuffer.m_data;
        CPPPrelude::UniformEntryPointParams* params = (CPPPrelude::UniformEntryPointParams*)binding.m_entryPointBuffer.m_data;

        CPPPrelude::ComputeVaryingInput varying;
        varying.groupID = {};

        for (int z = 0; z < int(numThreadsPerAxis[2]); ++z)
        {
            varying.groupThreadID.z = z;
            for (int y = 0; y < int(numThreadsPerAxis[1]); ++y)
            {
                varying.groupThreadID.y = y;
                for (int x = 0; x < int(numThreadsPerAxis[0]); ++x)
                {
                    varying.groupThreadID.x = x;

                    func(&varying, params, uniformState);
                }
            }
        }
    }

    // Dump everything out that was write (we wrote in place!)
    return _writeBindings(layout, buffers, gOptions.outputPath);
}

SlangResult RenderTestApp::initialize(SlangSession* session, Renderer* renderer, Options::ShaderProgramType shaderType, const ShaderCompilerUtil::Input& input)
{
    SLANG_RETURN_ON_FAIL(_initializeShaders(session, renderer, shaderType, input));

    m_numAddedConstantBuffers = 0;
	m_renderer = renderer;

    // TODO(tfoley): use each API's reflection interface to query the constant-buffer size needed
    m_constantBufferSize = 16 * sizeof(float);

    BufferResource::Desc constantBufferDesc;
    constantBufferDesc.init(m_constantBufferSize);
    constantBufferDesc.cpuAccessFlags = Resource::AccessFlag::Write;

    m_constantBuffer = renderer->createBufferResource(Resource::Usage::ConstantBuffer, constantBufferDesc);
    if (!m_constantBuffer)
        return SLANG_FAIL;

    //! Hack -> if doing a graphics test, add an extra binding for our dynamic constant buffer
    //
    // TODO: Should probably be more sophisticated than this - with 'dynamic' constant buffer/s binding always being specified
    // in the test file
    RefPtr<BufferResource> addedConstantBuffer;
    switch(shaderType)
    {
    default:
        break;

    case Options::ShaderProgramType::Graphics:
    case Options::ShaderProgramType::GraphicsCompute:
        addedConstantBuffer = m_constantBuffer;
        m_numAddedConstantBuffers++;
        break;
    }

    BindingStateImpl* bindingState = nullptr;
    SLANG_RETURN_ON_FAIL(ShaderRendererUtil::createBindingState(m_shaderInputLayout, m_renderer, addedConstantBuffer, &bindingState));
    m_bindingState = bindingState;

    // Do other initialization that doesn't depend on the source language.

    // Input Assembler (IA)

    const InputElementDesc inputElements[] = {
        { "A", 0, Format::RGB_Float32, offsetof(Vertex, position) },
        { "A", 1, Format::RGB_Float32, offsetof(Vertex, color) },
        { "A", 2, Format::RG_Float32, offsetof(Vertex, uv) },
    };

    m_inputLayout = renderer->createInputLayout(inputElements, SLANG_COUNT_OF(inputElements));
    if(!m_inputLayout)
        return SLANG_FAIL;

    BufferResource::Desc vertexBufferDesc;
    vertexBufferDesc.init(kVertexCount * sizeof(Vertex));

    m_vertexBuffer = renderer->createBufferResource(Resource::Usage::VertexBuffer, vertexBufferDesc, kVertexData);
    if(!m_vertexBuffer)
        return SLANG_FAIL;

    {
        switch(shaderType)
        {
        default:
            assert(!"unexpected test shader type");
            return SLANG_FAIL;

        case Options::ShaderProgramType::Compute:
            {
                ComputePipelineStateDesc desc;
                desc.pipelineLayout = m_bindingState->pipelineLayout;
                desc.program = m_shaderProgram;

                m_pipelineState = renderer->createComputePipelineState(desc);
            }
            break;

        case Options::ShaderProgramType::Graphics:
        case Options::ShaderProgramType::GraphicsCompute:
            {
                GraphicsPipelineStateDesc desc;
                desc.pipelineLayout = m_bindingState->pipelineLayout;
                desc.program = m_shaderProgram;
                desc.inputLayout = m_inputLayout;
                desc.renderTargetCount = m_bindingState->m_numRenderTargets;

                m_pipelineState = renderer->createGraphicsPipelineState(desc);
            }
            break;
        }
    }

    // If success must have a pipeline state
    return m_pipelineState ? SLANG_OK : SLANG_FAIL;
}

Result RenderTestApp::_initializeShaders(SlangSession* session, Renderer* renderer, Options::ShaderProgramType shaderType, const ShaderCompilerUtil::Input& input)
{
    CompileOutput output;
    SLANG_RETURN_ON_FAIL(_compile(session, gOptions.sourcePath, shaderType, input,  output));
    m_shaderInputLayout = output.layout;
    m_shaderProgram = renderer->createProgram(output.compileOutput.desc);
    return m_shaderProgram ? SLANG_OK : SLANG_FAIL;
}

void RenderTestApp::renderFrame()
{
    auto mappedData = m_renderer->map(m_constantBuffer, MapFlavor::WriteDiscard);
    if(mappedData)
    {
        const ProjectionStyle projectionStyle = RendererUtil::getProjectionStyle(m_renderer->getRendererType());
        RendererUtil::getIdentityProjection(projectionStyle, (float*)mappedData);

		m_renderer->unmap(m_constantBuffer);
    }

    auto pipelineType = PipelineType::Graphics;

    m_renderer->setPipelineState(pipelineType, m_pipelineState);

	m_renderer->setPrimitiveTopology(PrimitiveTopology::TriangleList);
	m_renderer->setVertexBuffer(0, m_vertexBuffer, sizeof(Vertex));

    m_bindingState->apply(m_renderer, pipelineType);

	m_renderer->draw(3);
}

void RenderTestApp::runCompute()
{
    auto pipelineType = PipelineType::Compute;
    m_renderer->setPipelineState(pipelineType, m_pipelineState);
    m_bindingState->apply(m_renderer, pipelineType);
	m_renderer->dispatchCompute(1, 1, 1);
}

void RenderTestApp::finalize()
{
}

Result RenderTestApp::writeBindingOutput(const char* fileName)
{
    // Submit the work
    m_renderer->submitGpuWork();
    // Wait until everything is complete
    m_renderer->waitForGpu();

    FILE * f = fopen(fileName, "wb");
    if (!f)
    {
        return SLANG_FAIL;
    }

    for(auto binding : m_bindingState->outputBindings)
    {
        auto i = binding.entryIndex;
        const auto& layoutBinding = m_shaderInputLayout.entries[i];

        assert(layoutBinding.isOutput);
        {
            if (binding.resource && binding.resource->isBuffer())
            {
                BufferResource* bufferResource = static_cast<BufferResource*>(binding.resource.Ptr());
                const size_t bufferSize = bufferResource->getDesc().sizeInBytes;

                unsigned int* ptr = (unsigned int*)m_renderer->map(bufferResource, MapFlavor::HostRead);
                if (!ptr)
                {
                    fclose(f);
                    return SLANG_FAIL;
                }

                const int size = int(bufferSize / sizeof(unsigned int));
                for (int i = 0; i < size; ++i)
                {
                    fprintf(f, "%X\n", ptr[i]);
                }
                m_renderer->unmap(bufferResource);
            }
            else
            {
                printf("invalid output type at %d.\n", int(i));
            }
        }
    }
    fclose(f);

    return SLANG_OK;
}


Result RenderTestApp::writeScreen(const char* filename)
{
    Surface surface;
    SLANG_RETURN_ON_FAIL(m_renderer->captureScreenSurface(surface));
    return PngSerializeUtil::write(filename, surface);
}

} //  namespace renderer_test

SLANG_TEST_TOOL_API SlangResult innerMain(Slang::StdWriters* stdWriters, SlangSession* session, int argcIn, const char*const* argvIn)
{
    using namespace renderer_test;
    using namespace Slang;

    StdWriters::setSingleton(stdWriters);

	// Parse command-line options
	SLANG_RETURN_ON_FAIL(parseOptions(argcIn, argvIn, StdWriters::getError()));

    // Declare window pointer before renderer, such that window is released after renderer
    RefPtr<renderer_test::Window> window;
    // Renderer is constructed (later) using the window
	Slang::RefPtr<Renderer> renderer;

    ShaderCompilerUtil::Input input;
    
    input.profile = "";
    input.target = SLANG_TARGET_NONE;
    input.args = &gOptions.slangArgs[0];
    input.argCount = gOptions.slangArgCount;

	SlangSourceLanguage nativeLanguage = SLANG_SOURCE_LANGUAGE_UNKNOWN;
	SlangPassThrough slangPassThrough = SLANG_PASS_THROUGH_NONE;
    char const* profileName = "";
	switch (gOptions.rendererType)
	{
		case RendererType::DirectX11:
			renderer = createD3D11Renderer();
			input.target = SLANG_DXBC;
            input.profile = "sm_5_0";
			nativeLanguage = SLANG_SOURCE_LANGUAGE_HLSL;
            slangPassThrough = SLANG_PASS_THROUGH_FXC;
            
			break;

		case RendererType::DirectX12:
			renderer = createD3D12Renderer();
			input.target = SLANG_DXBC;
            input.profile = "sm_5_0";
			nativeLanguage = SLANG_SOURCE_LANGUAGE_HLSL;
            slangPassThrough = SLANG_PASS_THROUGH_FXC;
            
            if( gOptions.useDXIL )
            {
                input.target = SLANG_DXIL;
                input.profile = "sm_6_0";
                slangPassThrough = SLANG_PASS_THROUGH_DXC;
            }
			break;

		case RendererType::OpenGl:
			renderer = createGLRenderer();
			input.target = SLANG_GLSL;
            input.profile = "glsl_430";
			nativeLanguage = SLANG_SOURCE_LANGUAGE_GLSL;
            slangPassThrough = SLANG_PASS_THROUGH_GLSLANG;
			break;

		case RendererType::Vulkan:
			renderer = createVKRenderer();
			input.target = SLANG_SPIRV;
            input.profile = "glsl_430";
			nativeLanguage = SLANG_SOURCE_LANGUAGE_GLSL;
            slangPassThrough = SLANG_PASS_THROUGH_GLSLANG;
			break;
        case RendererType::CPU:
            input.target = SLANG_HOST_CALLABLE;
            input.profile = "";
            nativeLanguage = SLANG_SOURCE_LANGUAGE_CPP;
            slangPassThrough = SLANG_PASS_THROUGH_GENERIC_C_CPP;
            break;
		default:
			fprintf(stderr, "error: unexpected\n");
			return SLANG_FAIL;
	}

    switch (gOptions.inputLanguageID)
    {
        case Options::InputLanguageID::Slang:
            input.sourceLanguage = SLANG_SOURCE_LANGUAGE_SLANG;
            input.passThrough = SLANG_PASS_THROUGH_NONE;
            break;

        case Options::InputLanguageID::Native:
            input.sourceLanguage = nativeLanguage;
            input.passThrough = slangPassThrough;
            break;

        default:
            break;
    }

    // Use the profile name set on options if set
    input.profile = gOptions.profileName ? gOptions.profileName : input.profile;

    StringBuilder rendererName;
    rendererName << "[" << RendererUtil::toText(gOptions.rendererType) << "] ";
    if (gOptions.adapter.getLength())
    {
        rendererName << "'" << gOptions.adapter << "'";
    }

    
    if (renderer)
    {
        Renderer::Desc desc;
        desc.width = gWindowWidth;
        desc.height = gWindowHeight;
        desc.adapter = gOptions.adapter;

        window = new renderer_test::Window;
        SLANG_RETURN_ON_FAIL(window->initialize(gWindowWidth, gWindowHeight));

        SlangResult res = renderer->initialize(desc, (HWND)window->getHandle());
        if (SLANG_FAILED(res))
        {
            if (!gOptions.onlyStartup)
            {
                fprintf(stderr, "Unable to initialize renderer %s\n", rendererName.getBuffer());
            }
            return res;
        }

        for (const auto& feature : gOptions.renderFeatures)
        {
            // If doesn't have required feature... we have to give up
            if (!renderer->hasFeature(feature.getUnownedSlice()))
            {
                return SLANG_E_NOT_AVAILABLE;
            }
        }
    }
    else
    {
        if (gOptions.rendererType != RendererType::CPU)
        {
            if (!gOptions.onlyStartup)
            {
                fprintf(stderr, "Unable to create renderer %s\n", rendererName.getBuffer());
            }
            return SLANG_FAIL;
        }
    }

    // If the only test is we can startup, then we are done
    if (gOptions.onlyStartup)
    {
        return SLANG_OK;
    }

    if (!renderer)
    {
        SLANG_RETURN_ON_FAIL(_doCPUCompute(session, gOptions.sourcePath, gOptions.shaderType, input));
        return SLANG_OK;
    }

	{
		RenderTestApp app;
		SLANG_RETURN_ON_FAIL(app.initialize(session, renderer, gOptions.shaderType, input));

        window->show();

		// ... and enter the event loop:
		for (;;)
		{
			MSG message;

			int result = PeekMessageW(&message, NULL, 0, 0, PM_REMOVE);
			if (result != 0)
			{
				if (message.message == WM_QUIT)
				{
					return (int)message.wParam;
				}

				TranslateMessage(&message);
				DispatchMessageW(&message);
			}
			else
			{
				// Whenever we don't have Windows events to process, we render a frame.
				if (gOptions.shaderType == Options::ShaderProgramType::Compute)
				{
					app.runCompute();
				}
				else
				{
					static const float kClearColor[] = { 0.25, 0.25, 0.25, 1.0 };
					renderer->setClearColor(kClearColor);
					renderer->clearFrame();

					app.renderFrame();
				}
				// If we are in a mode where output is requested, we need to snapshot the back buffer here
				if (gOptions.outputPath)
				{
                    // Submit the work
                    renderer->submitGpuWork();
                    // Wait until everything is complete
                    renderer->waitForGpu();

					if (gOptions.shaderType == Options::ShaderProgramType::Compute || gOptions.shaderType == Options::ShaderProgramType::GraphicsCompute)
                    {
                        SLANG_RETURN_ON_FAIL(app.writeBindingOutput(gOptions.outputPath));
                    }
					else
                    {
						SlangResult res = app.writeScreen(gOptions.outputPath);

                        if (SLANG_FAILED(res))
                        {
                            fprintf(stderr, "ERROR: failed to write screen capture to file\n");
                            return res;
                        }
                    }
					return SLANG_OK;
				}

				renderer->presentFrame();
			}
		}
	}

	return SLANG_OK;
}

int main(int argc, char**  argv)
{
    using namespace Slang;
    SlangSession* session = spCreateSession(nullptr);

    TestToolUtil::setSessionDefaultPrelude(argv[0], session);
    
    auto stdWriters = StdWriters::initDefaultSingleton();
    
    SlangResult res = innerMain(stdWriters, session, argc, argv);
    spDestroySession(session);

	return (int)TestToolUtil::getReturnCode(res);
}

