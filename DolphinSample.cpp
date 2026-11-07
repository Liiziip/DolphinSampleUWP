//--------------------------------------------------------------------------------------
// DolphinSample.cpp
//
// Modified version by jthecodebuilder and Joshua-Ashton (GitHub)
//
// Originally coded by
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "DolphinSample.h"

#include "ATGColors.h"
#include "ReadData.h"

extern void ExitSample() noexcept;

using namespace DirectX;
using namespace ATG;

using Microsoft::WRL::ComPtr;

namespace
{
	// Vertex data structure used to
    // create a constant buffer.
    struct VS_CONSTANT_BUFFER
    {
        XMVECTOR vZero;
        XMVECTOR vConstants;
        XMVECTOR vWeight;

        XMMATRIX matTranspose;
        XMMATRIX matCameraTranspose;
        XMMATRIX matViewTranspose;
        XMMATRIX matProjTranspose;

        XMVECTOR vLight;
        XMVECTOR vLightDolphinSpace;
        XMVECTOR vDiffuse;
        XMVECTOR vAmbient;
        XMVECTOR vFog;
        XMVECTOR vCaustics;
    };

    // Pixel data structure used to
    // create a constant buffer.
    struct PS_CONSTANT_BUFFER
    {
        float fAmbient[4];
        float fFogColor[4];
    };

    static_assert((sizeof(VS_CONSTANT_BUFFER) % 16) == 0, "CB must be 16 byte aligned");
    static_assert((sizeof(PS_CONSTANT_BUFFER) % 16) == 0, "CB must be 16 byte aligned");

    // 设备家族字符串比较（不区分大小写，避免 "windows.core" 这种写法匹配不上）
    bool FamilyEquals(const std::wstring& family, const wchar_t* expected)
    {
        size_t i = 0;
        for (; i < family.size() && expected[i] != L'\0'; ++i)
        {
            wchar_t a = family[i];
            wchar_t b = expected[i];
            if (a >= L'A' && a <= L'Z') a = wchar_t(a - L'A' + L'a');
            if (b >= L'A' && b <= L'Z') b = wchar_t(b - L'A' + L'a');
            if (a != b)
                return false;
        }
        return i == family.size() && expected[i] == L'\0';
    }

    // 获取产品类型 + 版本号（例如 "Windows 11 (10.0.22631.4890)"）
    // 先看设备家族（DeviceFamily）：Desktop 按 build 区分 Win10/Win11，其余按家族映射
    std::wstring GetWindowsVersionString()
    {
        ULONG major = 0;
        ULONG minor = 0;
        ULONG build = 0;
        ULONG revision = 0;
        std::wstring family;

        try
        {
            auto versionInfo = Windows::System::Profile::AnalyticsInfo::VersionInfo;
            if (versionInfo != nullptr)
            {
                Platform::String^ deviceFamily = versionInfo->DeviceFamily;
                if (deviceFamily != nullptr)
                    family = deviceFamily->Data();

                Platform::String^ version = versionInfo->DeviceFamilyVersion;
                if (version != nullptr)
                {
                    unsigned long long packed = 0;
                    for (const wchar_t* p = version->Data(); p && *p >= L'0' && *p <= L'9'; ++p)
                        packed = packed * 10ull + static_cast<unsigned long long>(*p - L'0');

                    major = static_cast<ULONG>((packed >> 48) & 0xFFFFull);
                    minor = static_cast<ULONG>((packed >> 32) & 0xFFFFull);
                    build = static_cast<ULONG>((packed >> 16) & 0xFFFFull);
                    revision = static_cast<ULONG>(packed & 0xFFFFull);
                }
            }
        }
        catch (...) {}

        if (build == 0)
            return L"Windows (unknown)";

        // 设备家族 -> 显示名
        std::wstring name;
        if (FamilyEquals(family, L"Windows.Desktop"))
            name = (major == 10 && build >= 22000) ? L"Windows 11" : L"Windows 10";
        else if (FamilyEquals(family, L"Windows.Core"))
            name = L"Windows Core";
        else if (FamilyEquals(family, L"Windows.8828080"))
            name = L"Andromeda";
        else if (FamilyEquals(family, L"Windows.Xbox"))
            name = L"Xbox";
        else if (FamilyEquals(family, L"Windows.Holographic"))
            name = L"HoloLens";
        else if (FamilyEquals(family, L"Windows.Team"))
            name = L"Surface Hub";
        else if (FamilyEquals(family, L"Windows.IoT"))
            name = L"Windows IoT";
        else if (FamilyEquals(family, L"Windows.Mobile"))
            name = L"Windows Mobile";
        else if (FamilyEquals(family, L"Windows.Universal"))
            name = L"Windows";
        else if (!family.empty())
            name = family;          // 未知设备家族：原样显示
        else
            name = L"Windows";

        wchar_t buf[128] = {};
        if (revision != 0)
            swprintf_s(buf, L"%s (%lu.%lu.%lu.%lu)", name.c_str(), major, minor, build, revision);
        else
            swprintf_s(buf, L"%s (%lu.%lu.%lu)", name.c_str(), major, minor, build);

        return buf;
    }

    // Custom effect for sea floor
    class SeaEffect : public IEffect
    {
    public:
        SeaEffect(ID3D11Device* device, ID3D11PixelShader* pixelShader) :
            m_pixelShader(pixelShader)
        {
            m_shaderBlob = DX::ReadData(L"SeaFloorVS.cso");
            DX::ThrowIfFailed(device->CreateVertexShader(m_shaderBlob.data(), m_shaderBlob.size(), nullptr, m_vertexShader.ReleaseAndGetAddressOf()));
        }

        virtual void __cdecl Apply(ID3D11DeviceContext* context) override
        {
            context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
            context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
        }

        virtual void __cdecl GetVertexShaderBytecode(_Out_ void const** pShaderByteCode, _Out_ size_t* pByteCodeLength) override
        {
            *pShaderByteCode = m_shaderBlob.data();
            *pByteCodeLength = m_shaderBlob.size();
        }

    private:
        ComPtr<ID3D11VertexShader>  m_vertexShader;
        ComPtr<ID3D11PixelShader>   m_pixelShader;
        std::vector<uint8_t>        m_shaderBlob;
    };
}

Sample::Sample() noexcept(false)
    : m_frame(0),
    m_currentCausticTextureView(0)
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();

    // Create the dolphins
    for (unsigned int i = 0; i < m_dolphins.size(); i++)
    {
        auto d = std::make_shared<Dolphin>();
        d->Translate(XMVectorSet(0, -1.0f + 2.0f * static_cast<float>(i), 10, 0));
        m_dolphins[i] = d;
    }

    // Set the water color to a nice blue.
	// TODO: Figure out why this occurs three times, and then make it easier to change?
    SetWaterColor(0.0f, 0.5f, 1.0f);

    // Set the ambient light.
    m_ambient[0] = 0.25f;
    m_ambient[1] = 0.25f;
    m_ambient[2] = 0.25f;
    m_ambient[3] = 0.25f;
}

// Initialize the Direct3D resources required to run.
void Sample::Initialize(IUnknown* window, int width, int height, DXGI_MODE_ROTATION rotation)
{
	m_gamePad = std::make_unique<GamePad>();

	m_keyboard = std::make_unique<Keyboard>();
	m_keyboard->SetWindow(reinterpret_cast<ABI::Windows::UI::Core::ICoreWindow*>(window));

	m_mouse = std::make_unique<Mouse>();
	m_mouse->SetWindow(reinterpret_cast<ABI::Windows::UI::Core::ICoreWindow*>(window));

	m_deviceResources->SetWindow(window, width, height, rotation);

	m_deviceResources->CreateDeviceResources();
	CreateDeviceDependentResources();

	m_deviceResources->CreateWindowSizeDependentResources();
	CreateWindowSizeDependentResources();
}

#pragma region Frame Update
// Executes basic render loop.
void Sample::Tick()
{
	m_timer.Tick([&]()
		{
			Update(m_timer);
		});

	Render();

	m_frame++;
}

// Updates the world.
void Sample::Update(DX::StepTimer const& timer)
{
	float elapsedTime = float(timer.GetElapsedSeconds());
	float totalTime = float(timer.GetTotalSeconds());

	// Update all the dolphins
	for (unsigned int i = 0; i < m_dolphins.size(); i++)
		m_dolphins[i]->Update(totalTime, elapsedTime);

	SetWaterColor(0.0f, 0.5f, 1.0f);

	// Animate the caustic textures
	m_currentCausticTextureView = (static_cast<unsigned int>(totalTime * 32)) % 32;

	auto context = m_deviceResources->GetD3DDeviceContext();

	D3D11_MAPPED_SUBRESOURCE mappedResource;

	context->Map(m_VSConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	{
		VS_CONSTANT_BUFFER *vertShaderConstData = (VS_CONSTANT_BUFFER*)mappedResource.pData;

		vertShaderConstData->vZero = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		vertShaderConstData->vConstants = XMVectorSet(1.0f, 0.5f, 0.2f, 0.05f);

		// weight is for dolphins, so the value doesn't matter here (since we're setting it for the sea floor)
		vertShaderConstData->vWeight = XMVectorSet(0, 0, 0, 0);

		// Lighting vectors (in world space and in dolphin model space)
		// and other constants
		vertShaderConstData->vLight = XMVectorSet(0.00f, 1.00f, 0.00f, 0.00f);
		vertShaderConstData->vLightDolphinSpace = XMVectorSet(0.00f, 1.00f, 0.00f, 0.00f);
		vertShaderConstData->vDiffuse = XMVectorSet(1.00f, 1.00f, 1.00f, 1.00f);
		vertShaderConstData->vAmbient = XMVectorSet(m_ambient[0], m_ambient[1], m_ambient[2], m_ambient[3]);
		vertShaderConstData->vFog = XMVectorSet(0.50f, 50.00f, 1.00f / (50.0f - 1.0f), 0.00f);
		vertShaderConstData->vCaustics = XMVectorSet(0.05f, 0.05f, sinf(totalTime) / 8, cosf(totalTime) / 10);

		XMVECTOR vDeterminant;
		XMMATRIX matDolphin = XMMatrixIdentity();
		XMMATRIX matDolphinInv = XMMatrixInverse(&vDeterminant, matDolphin);
		vertShaderConstData->vLightDolphinSpace = XMVector4Normalize(XMVector4Transform(vertShaderConstData->vLight, matDolphinInv));

		// Vertex shader operations use transposed matrices
		XMMATRIX mat, matCamera;
		matCamera = XMMatrixMultiply(matDolphin, XMLoadFloat4x4(&m_matView));
		mat = XMMatrixMultiply(matCamera, m_matProj);
		vertShaderConstData->matTranspose = XMMatrixTranspose(mat);
		vertShaderConstData->matCameraTranspose = XMMatrixTranspose(matCamera);
		vertShaderConstData->matViewTranspose = XMMatrixTranspose(m_matView);
		vertShaderConstData->matProjTranspose = XMMatrixTranspose(m_matProj);

	}
	context->Unmap(m_VSConstantBuffer.Get(), 0);

	context->Map(m_PSConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	{
		PS_CONSTANT_BUFFER* pPsConstData = (PS_CONSTANT_BUFFER*)mappedResource.pData;

		memcpy(pPsConstData->fAmbient, m_ambient, sizeof(m_ambient));
		memcpy(pPsConstData->fFogColor, m_waterColor, sizeof(m_waterColor));
	}
	context->Unmap(m_PSConstantBuffer.Get(), 0);

	auto kb = m_keyboard->GetState();
	m_keyboardButtons.Update(kb);

	//Gamepad map.
    auto pad = m_gamePad->GetState(0);
    if (pad.IsConnected())
    {
        m_gamePadButtons.Update(pad);

        if (pad.IsViewPressed())
        {
            ExitSample();
        }

		if (m_gamePadButtons.rightShoulder == GamePad::ButtonStateTracker::ButtonState::PRESSED)
		{
			// TODO: Figure out a way to increment the dolphins here. 
		}
		else if (m_gamePadButtons.leftShoulder == GamePad::ButtonStateTracker::ButtonState::PRESSED)
		{
			// TODO: Figure out a way to decrement the dolphins here. These array thingys are difficult. Grr!
		}

		if (m_gamePadButtons.y == GamePad::ButtonStateTracker::ButtonState::PRESSED)
		{
			// Once implemented, this button will activate wireframe mode.
		}
    }
    else
    {
        m_gamePadButtons.Reset();
    }

	//Keyboard map.
	if (kb.Escape)
	{
		ExitSample();
	}

	if (m_keyboardButtons.IsKeyPressed(Keyboard::Q))
	{
		// Increment dolphins. If array can be accessed somehow...
	}
	else if (m_keyboardButtons.IsKeyPressed(Keyboard::E))
	{
		// Decrement dolphins. I'm just not sure.
	}

	else if (m_keyboardButtons.IsKeyPressed(Keyboard::Y))
	{
		// Once implemented, this key will activate wireframe mode.
	}

    // 更新FPS
    m_fpsFrameCount++;
    m_fpsUpdateTimer += elapsedTime;

    if (m_fpsUpdateTimer >= 0.5f) // 每0.5秒更新一次FPS显示
    {
        uint32_t fps = static_cast<uint32_t>(m_fpsFrameCount / m_fpsUpdateTimer);
        swprintf_s(m_fpsText, L"FPS: %u", fps);
        m_fpsFrameCount = 0;
        m_fpsUpdateTimer = 0.0f;
    }

    // 更新窗口分辨率（像素）
    auto outputSize = m_deviceResources->GetOutputSize();
    swprintf_s(m_resText, L"Resolution: %ld x %ld",
        outputSize.right - outputSize.left,
        outputSize.bottom - outputSize.top);
}
#pragma endregion

#pragma region Frame Render
// Draws the scene.
void Sample::Render()
{
    // Don't try to render anything before the first Update.
    if (m_timer.GetFrameCount() == 0)
    {
        return;
    }

    // Prepare the render target to render a new frame.
    m_deviceResources->Prepare();
    Clear();

    auto context = m_deviceResources->GetD3DDeviceContext();

    SetWaterColor(0.0f, 0.5f, 1.0f);

    // Set state
    context->OMSetBlendState(m_states->Opaque(), 0, 0xffffffff);
    context->RSSetState(m_states->CullNone());
    context->OMSetDepthStencilState(m_states->DepthDefault(), 0);

    ID3D11SamplerState* pSamplers[] = { m_pSamplerMirror.Get(), m_states->LinearWrap() };
    context->PSSetSamplers(0, 2, pSamplers);
    
    context->VSSetConstantBuffers(0, 1, m_VSConstantBuffer.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, m_PSConstantBuffer.GetAddressOf());

    /////////////////////////////////////////////////////
    //
    // Render sea floor
    //
    ////////////////////////////////////////////////////
    assert(!m_seafloor->meshes.empty());
    assert(!m_seafloor->meshes[0]->meshParts.empty());
    m_seafloor->meshes[0]->meshParts[0]->Draw(context, m_seaEffect.get(), m_seaFloorVertexLayout.Get(), [&]
    {
        context->PSSetShaderResources(0, 1, m_seaFloorTextureView.GetAddressOf());
        context->PSSetShaderResources(1, 1, m_causticTextureViews[m_currentCausticTextureView].GetAddressOf());
    });

    /////////////////////////////////////////////////////
    //
    // Render Dolphins  
    //
    ////////////////////////////////////////////////////
    for (unsigned int i = 0; i < m_dolphins.size(); i++)
        DrawDolphin(*m_dolphins[i].get());

    // 渲染FPS文本
    RenderFPS();
    
    // 渲染 CPU 信息（右上角）
    RenderCPUInfo();

    // Show the new frame.
    m_deviceResources->Present();
    m_graphicsMemory->Commit();
}

// Helper method to clear the back buffers.
void Sample::Clear()
{
    auto context = m_deviceResources->GetD3DDeviceContext();

    // Clear the views.
    auto renderTarget = m_deviceResources->GetRenderTargetView();
    auto depthStencil = m_deviceResources->GetDepthStencilView();

    // Use linear clear color for gamma-correct rendering.
    context->ClearRenderTargetView(renderTarget, m_waterColor);
    context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    context->OMSetRenderTargets(1, &renderTarget, depthStencil);

    // Set the viewport.
    auto viewport = m_deviceResources->GetScreenViewport();
    context->RSSetViewports(1, &viewport);
}
#pragma endregion

#pragma region Message Handlers

// Message handlers
void Sample::OnActivated()
{
}

void Sample::OnDeactivated()
{
}

void Sample::OnSuspending()
{
    auto context = m_deviceResources->GetD3DDeviceContext();
	context->ClearState();

	m_deviceResources->Trim();
}

void Sample::OnResuming()
{
	m_timer.ResetElapsedTime();
	m_gamePadButtons.Reset();
	m_keyboardButtons.Reset();
}

void Sample::OnWindowSizeChanged(int width, int height, DXGI_MODE_ROTATION rotation)
{
	if (!m_deviceResources->WindowSizeChanged(width, height, rotation))
		return;

	CreateWindowSizeDependentResources();
}
void Sample::ValidateDevice()
{
	m_deviceResources->ValidateDevice();
}

// Properties
void Sample::GetDefaultSize(int& width, int& height) const
{
	width = 1280;
	height = 720;
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Sample::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto context = m_deviceResources->GetD3DDeviceContext();
    m_graphicsMemory = std::make_unique<GraphicsMemory>(device, m_deviceResources->GetBackBufferCount());

    ////////////////////////////////
    //
    // Create constant buffers
    //
    ////////////////////////////////
    {
        D3D11_BUFFER_DESC cbDesc;
        cbDesc.ByteWidth = sizeof(VS_CONSTANT_BUFFER);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        cbDesc.MiscFlags = 0;

        DX::ThrowIfFailed(device->CreateBuffer(&cbDesc, NULL, m_VSConstantBuffer.GetAddressOf()));

        cbDesc.ByteWidth = sizeof(PS_CONSTANT_BUFFER);

        DX::ThrowIfFailed(device->CreateBuffer(&cbDesc, NULL, m_PSConstantBuffer.GetAddressOf()));
    }

    m_fxFactory = std::make_unique<EffectFactory>(device);
    m_fxFactory->SetDirectory(L"Assets\\textures");

    ////////////////////////////////
    //
    // Load the dolphins
    //
    //////////////////////////////
    for (unsigned int i = 0; i < m_dolphins.size(); i++)
        m_dolphins[i]->Load(device, context, m_fxFactory.get());

    ////////////////////////////////
    //
    // Create the textures resources
    //
    ////////////////////////////////
    m_fxFactory->CreateTexture(L"Seafloor.bmp", context, m_seaFloorTextureView.ReleaseAndGetAddressOf());

    for (DWORD t = 0; t < 32; t++)
    {
        std::wstring path;
        if (t < 10)
        {
            int count = _scwprintf(L"caust0%i.DDS", t);
            path.resize(count + 1);
            swprintf_s(&path[0], path.size(), L"caust0%i.DDS", t);
        }
        else
        {
            int count = _scwprintf(L"caust%i.DDS", t);
            path.resize(count + 1);
            swprintf_s(&path[0], path.size(), L"caust%i.DDS", t);
        }

        m_fxFactory->CreateTexture(path.c_str(), context, m_causticTextureViews[t].ReleaseAndGetAddressOf());
    }

    // Create the common pixel shader
    {
        auto blob = DX::ReadData(L"CausticsPS.cso");
        DX::ThrowIfFailed(device->CreatePixelShader(blob.data(), blob.size(), nullptr, m_pixelShader.ReleaseAndGetAddressOf()));
    }

    ////////////////////////////////
    //
    // Create the mesh resources
    //
    ////////////////////////////////

    // Create sea floor objects
    m_seafloor = Model::CreateFromSDKMESH(device, L"assets\\mesh\\seafloor.sdkmesh", *m_fxFactory);

    m_seaEffect = std::make_unique<SeaEffect>(device, m_pixelShader.Get());

    m_seafloor->meshes[0]->meshParts[0]->CreateInputLayout(device, m_seaEffect.get(), m_seaFloorVertexLayout.ReleaseAndGetAddressOf());

    m_states = std::make_unique<CommonStates>(device);

    {
        D3D11_SAMPLER_DESC desc = {};
        desc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        DX::ThrowIfFailed(device->CreateSamplerState(&desc, m_pSamplerMirror.ReleaseAndGetAddressOf()));
    }

    // Determine the aspect ratio. This time we use the window size, though.
    //float fAspectRatio = 1920.0f / 1080.0f;

    // Set the transform matrices
    static const XMVECTORF32 c_vEyePt = { 0.0f, 0.0f, -5.0f, 0.0f };
    static const XMVECTORF32 c_vLookatPt = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const XMVECTORF32 c_vUpVec = { 0.0f, 1.0f, 0.0f, 0.0f };

	//auto size = m_deviceResources->GetOutputSize();

	// Set up transforms
    m_matView = XMMatrixLookAtLH(c_vEyePt, c_vLookatPt, c_vUpVec);
    //m_matProj = XMMatrixPerspectiveFovLH(XM_PI / 3, (float(size.right) / float(size.bottom)), 1.0f, 10000.0f);
	//m_matProj = XMMatrixPerspectiveFovLH(XM_PI / 3, fAspectRatio, 1.0f, 10000.0f);

    m_spriteBatch = std::make_unique<SpriteBatch>(context);

    // load font file
    try
    {
        m_spriteFont = std::make_unique<SpriteFont>(device, L"Assets\\SegoeUI_24.spritefont");
    }
    catch (...)
    {
        // if not exist, use system font
        m_spriteFont = std::make_unique<SpriteFont>(device, L"Segoe UI", 24);
    }

    // initialize
    m_fpsUpdateTimer = 0.0f;
    m_fpsFrameCount = 0;
    wcscpy_s(m_fpsText, L"FPS: 0");
    wcscpy_s(m_resText, L"0 x 0");

    // 初始化 CPU 信息显示
    InitializeCPUInfo();
}

// Allocate all memory resources that change on a window SizeChanged event.
void Sample::CreateWindowSizeDependentResources()
{
	auto size = m_deviceResources->GetOutputSize();
	//This is actually where we generate the aspect ratio.
	m_matProj = XMMatrixPerspectiveFovLH(XM_PI / 3, (float(size.right) / float(size.bottom)), 1.0f, 10000.0f);
	XMFLOAT4X4 orient = m_deviceResources->GetOrientationTransform3D();

    // 更新SpriteBatch的视口
    if (m_spriteBatch)
    {
        m_spriteBatch->SetViewport(m_deviceResources->GetScreenViewport());
    }

    // 重新计算 CPU 文本尺寸（因为视口可能改变）
    if (m_spriteFont && m_cpuText[0] != L'\0')
    {
        m_cpuTextSize = m_spriteFont->MeasureString(m_cpuText);
    }
}
#pragma endregion

void Sample::SetWaterColor(float red, float green, float blue)
{
	m_waterColor[0] = red;
	m_waterColor[1] = green;
	m_waterColor[2] = blue;
	m_waterColor[3] = 1.0f;
}

void Sample::DrawDolphin(Dolphin &dolphin)
{
	auto context = m_deviceResources->GetD3DDeviceContext();

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	context->Map(m_VSConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	{
		VS_CONSTANT_BUFFER* vertShaderConstData = (VS_CONSTANT_BUFFER*)mappedResource.pData;

		vertShaderConstData->vZero = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
		vertShaderConstData->vConstants = XMVectorSet(1.0f, 0.5f, 0.2f, 0.05f);

		FLOAT fBlendWeight = dolphin.GetBlendWeight();
		FLOAT fWeight1;
		FLOAT fWeight2;
		FLOAT fWeight3;

		if (fBlendWeight > 0.0f)
		{
			fWeight1 = fabsf(fBlendWeight);
			fWeight2 = 1.0f - fabsf(fBlendWeight);
			fWeight3 = 0.0f;
		}
		else
		{
			fWeight1 = 0.0f;
			fWeight2 = 1.0f - fabsf(fBlendWeight);
			fWeight3 = fabsf(fBlendWeight);
		}
		vertShaderConstData->vWeight = XMVectorSet(fWeight1, fWeight2, fWeight3, 0.0f);

		// Lighting vectors (in world space and in dolphin model space)
		// and other constants
		vertShaderConstData->vLight = XMVectorSet(0.00f, 1.00f, 0.00f, 0.00f);
		vertShaderConstData->vLightDolphinSpace = XMVectorSet(0.00f, 1.00f, 0.00f, 0.00f);
		vertShaderConstData->vDiffuse = XMVectorSet(1.00f, 1.00f, 1.00f, 1.00f);
		vertShaderConstData->vAmbient = XMVectorSet(m_ambient[0], m_ambient[1], m_ambient[2], m_ambient[3]);
		vertShaderConstData->vFog = XMVectorSet(0.50f, 50.00f, 1.00f / (50.0f - 1.0f), 0.00f);

		float totalSeconds = float(m_timer.GetTotalSeconds());
		vertShaderConstData->vCaustics = XMVectorSet(0.05f, 0.05f, sinf(totalSeconds) / 8, cosf(totalSeconds) / 10);

		XMVECTOR vDeterminant;
		XMMATRIX matDolphin = dolphin.GetWorld();
		XMMATRIX matDolphinInv = XMMatrixInverse(&vDeterminant, matDolphin);
		vertShaderConstData->vLightDolphinSpace = XMVector4Normalize(XMVector4Transform(vertShaderConstData->vLight, matDolphinInv));

		// Vertex shader operations use transposed matrices
		XMMATRIX matCamera = XMMatrixMultiply(matDolphin, m_matView);
		XMMATRIX mat = XMMatrixMultiply(matCamera, m_matProj);
		vertShaderConstData->matTranspose = XMMatrixTranspose(mat);
		vertShaderConstData->matCameraTranspose = XMMatrixTranspose(matCamera);
		vertShaderConstData->matViewTranspose = XMMatrixTranspose(m_matView);
		vertShaderConstData->matProjTranspose = XMMatrixTranspose(m_matProj);

	}
	context->Unmap(m_VSConstantBuffer.Get(), 0);

	context->VSSetConstantBuffers(0, 1, m_VSConstantBuffer.GetAddressOf());
	dolphin.Render(context, m_pixelShader.Get(), m_causticTextureViews[m_currentCausticTextureView].Get());
}

// DolphinSample.cpp 中添加辅助函数
void Sample::RenderFPS()
{
    if (!m_spriteBatch || !m_spriteFont)
        return;

    auto context = m_deviceResources->GetD3DDeviceContext();

    // 保存当前状态
    ID3D11RenderTargetView* oldRTV = nullptr;
    ID3D11DepthStencilView* oldDSV = nullptr;
    context->OMGetRenderTargets(1, &oldRTV, &oldDSV);

    // 设置渲染目标
    auto renderTarget = m_deviceResources->GetRenderTargetView();
    auto depthStencil = m_deviceResources->GetDepthStencilView();
    context->OMSetRenderTargets(1, &renderTarget, depthStencil);

    // 开始绘制
    m_spriteBatch->Begin();

    // 在左上角绘制FPS文本，带黑色阴影以增加可读性
    XMVECTOR textPosition = XMVectorSet(20.0f, 20.0f, 0.0f, 0.0f);
    XMVECTOR shadowOffset = XMVectorSet(1.0f, 1.0f, 0.0f, 0.0f);

    // 绘制阴影
    m_spriteFont->DrawString(m_spriteBatch.get(), m_fpsText, textPosition + shadowOffset, DirectX::Colors::Black);

    // 绘制主文本
    m_spriteFont->DrawString(m_spriteBatch.get(), m_fpsText, textPosition, DirectX::Colors::White);

    // 在 FPS 下面显示窗口分辨率（像素）
    XMVECTOR resPosition = XMVectorSet(20.0f, 20.0f + m_spriteFont->GetLineSpacing(), 0.0f, 0.0f);
    m_spriteFont->DrawString(m_spriteBatch.get(), m_resText, resPosition + shadowOffset, DirectX::Colors::Black);
    m_spriteFont->DrawString(m_spriteBatch.get(), m_resText, resPosition, DirectX::Colors::White);

    m_spriteBatch->End();

    // 恢复状态
    context->OMSetRenderTargets(1, &oldRTV, oldDSV);

    if (oldRTV) oldRTV->Release();
    if (oldDSV) oldDSV->Release();

}

// DolphinSample.cpp - 添加 CPU 信息获取函数

// DolphinSample.cpp

void Sample::InitializeCPUInfo()
{
    swprintf_s(m_cpuText, L"Unknown");
    swprintf_s(m_gpuText, L"Unknown");
    swprintf_s(m_archText, L"Unknown");

    // 核心数
    SYSTEM_INFO sysInfo = {};
    GetNativeSystemInfo(&sysInfo);
    DWORD coreCount = sysInfo.dwNumberOfProcessors;

#if defined(_M_ARM) || defined(_M_ARM64)
    m_isArmPlatform = true;
#else
    m_isArmPlatform = false;
#endif

    // ============================================================
    // 架构字符串
    // ============================================================
    const wchar_t* archStr = L"Unknown";
#if defined(_M_X64) || defined(__x86_64__)
    archStr = L"x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    archStr = L"ARM64";
#elif defined(_M_ARM) || defined(__arm__)
    archStr = L"ARM32";
#elif defined(_M_IX86) || defined(__i386__)
    archStr = L"x86";
#endif

    // x86/x64 模拟在 ARM64 上
#if defined(_M_X64) || defined(_M_IX86)
    USHORT processMachine = 0;
    USHORT nativeMachine = 0;
    if (IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine))
    {
        if (nativeMachine == IMAGE_FILE_MACHINE_ARM64)
        {
            if (processMachine == IMAGE_FILE_MACHINE_AMD64)
                archStr = L"x64 (emulated on ARM64)";
            else if (processMachine == IMAGE_FILE_MACHINE_I386)
                archStr = L"x86 (emulated on ARM64)";
            else
                archStr = L"ARM64";
        }
    }
#endif

    // ============================================================
    // 第一行内容
    // ============================================================
    std::wstring firstLine;

#if defined(_M_ARM) || defined(_M_ARM64)
    // ---------- ARM / ARM64：第一行 = GPU ----------
    {
        std::wstring gpuName = m_deviceResources->GetGPUName();
        firstLine = gpuName;
        swprintf_s(m_gpuText, L"");   // ARM 上不用第二行 GPU
    }
#elif defined(_M_IX86) || defined(_M_X64)
    // ---------- x86 / x64：第一行 = CPU ----------
    {
        int cpuInfo[4] = {};
        char brand[49] = {};
        char vendor[13] = {};

        __cpuid(cpuInfo, 0);
        memcpy(vendor + 0, &cpuInfo[1], 4);
        memcpy(vendor + 4, &cpuInfo[3], 4);
        memcpy(vendor + 8, &cpuInfo[2], 4);
        vendor[12] = '\0';

        __cpuid(cpuInfo, 0x80000000);
        if (cpuInfo[0] >= 0x80000004)
        {
            __cpuid(cpuInfo, 0x80000002);
            memcpy(brand + 0, cpuInfo, 16);
            __cpuid(cpuInfo, 0x80000003);
            memcpy(brand + 16, cpuInfo, 16);
            __cpuid(cpuInfo, 0x80000004);
            memcpy(brand + 32, cpuInfo, 16);
            brand[48] = '\0';
        }

        std::wstring cpuNameStr;
        if (brand[0] != '\0' && strlen(brand) > 4)
        {
            int len = MultiByteToWideChar(CP_ACP, 0, brand, -1, nullptr, 0);
            if (len > 0)
            {
                std::vector<wchar_t> wbrand(len);
                MultiByteToWideChar(CP_ACP, 0, brand, -1, wbrand.data(), len);
                cpuNameStr = wbrand.data();
            }
        }

        if (cpuNameStr.empty())
        {
            if (strcmp(vendor, "GenuineIntel") == 0)
                cpuNameStr = L"Intel Processor";
            else if (strcmp(vendor, "AuthenticAMD") == 0)
                cpuNameStr = L"AMD Processor";
            else
                cpuNameStr = L"x86 Processor";
        }
        firstLine = cpuNameStr;

        // ---------- x86/x64：第二行 = GPU ----------
        std::wstring gpuName = m_deviceResources->GetGPUName();
        swprintf_s(m_gpuText, L"GPU: %s", gpuName.c_str());
    }
#endif

    // ============================================================
    // 清理第一行空格
    // ============================================================
    size_t start = firstLine.find_first_not_of(L" \t");
    size_t end = firstLine.find_last_not_of(L" \t");
    if (start != std::wstring::npos && end != std::wstring::npos)
        firstLine = firstLine.substr(start, end - start + 1);
    else
        firstLine = L"Unknown";

    std::wstring cleaned;
    bool lastWasSpace = false;
    for (wchar_t c : firstLine)
    {
        if (c == L' ' || c == L'\t')
        {
            if (!lastWasSpace) { cleaned += L' '; lastWasSpace = true; }
        }
        else
        {
            cleaned += c;
            lastWasSpace = false;
        }
    }
    firstLine = cleaned;

    if (firstLine.length() > 45)
        firstLine = firstLine.substr(0, 42) + L"...";

    // ============================================================
    // 组装最终文本
    // ============================================================
    if (m_isArmPlatform)
    {
        // ARM：两行
        //   第一行 = GPU 名称
        //   第二行 = 架构 + 核心数
        swprintf_s(m_cpuText, L"%s", firstLine.c_str());
        swprintf_s(m_gpuText, L"");   // 空字符串，RenderCPUInfo 跳过
        swprintf_s(m_archText, L"%s  |  %u cores", archStr, coreCount);
    }
    else
    {
        // x86/x64：三行
        //   第一行 = CPU
        //   第二行 = GPU
        //   第三行 = 架构 + 核心数
        swprintf_s(m_cpuText, L"CPU: %s", firstLine.c_str());
        // m_gpuText 已经在上面填好（"GPU: ..."）
        swprintf_s(m_archText, L"%s  |  %u cores", archStr, coreCount);
    }

    // Windows 版本号（显示在架构信息下方）
    swprintf_s(m_osText, L"%s", GetWindowsVersionString().c_str());

    // 计算尺寸
    if (m_spriteFont)
    {
        XMStoreFloat2(&m_cpuTextSize, m_spriteFont->MeasureString(m_cpuText));
        XMStoreFloat2(&m_archTextSize, m_spriteFont->MeasureString(m_archText));
        if (m_gpuText[0] != L'\0')
            XMStoreFloat2(&m_gpuTextSize, m_spriteFont->MeasureString(m_gpuText));
        else
            m_gpuTextSize = XMFLOAT2(0, 0);
    }

    OutputDebugStringW(L"[SYS] line1 = ");
    OutputDebugStringW(m_cpuText);
    OutputDebugStringW(L"\n[SYS] line2 = ");
    OutputDebugStringW(m_gpuText);
    OutputDebugStringW(L"\n[SYS] line3 = ");
    OutputDebugStringW(m_archText);
    OutputDebugStringW(L"\n");
}
void Sample::RenderCPUInfo()
{
    if (!m_spriteBatch || !m_spriteFont)
        return;

    try
    {
        auto context = m_deviceResources->GetD3DDeviceContext();

        ID3D11RenderTargetView* oldRTV = nullptr;
        ID3D11DepthStencilView* oldDSV = nullptr;
        context->OMGetRenderTargets(1, &oldRTV, &oldDSV);

        auto renderTarget = m_deviceResources->GetRenderTargetView();
        auto depthStencil = m_deviceResources->GetDepthStencilView();
        context->OMSetRenderTargets(1, &renderTarget, depthStencil);

        auto viewport = m_deviceResources->GetScreenViewport();
        float screenWidth = viewport.Width;

        // 实时测量
        XMFLOAT2 nameSize, gpuSize, archSize;
        XMStoreFloat2(&nameSize, m_spriteFont->MeasureString(m_cpuText));
        XMStoreFloat2(&archSize, m_spriteFont->MeasureString(m_archText));

        bool hasGpuLine = (m_gpuText[0] != L'\0');
        if (hasGpuLine)
            XMStoreFloat2(&gpuSize, m_spriteFont->MeasureString(m_gpuText));
        else
            gpuSize = XMFLOAT2(0, 0);

        const float margin = 20.0f;
        const float lineHeight = nameSize.y + 2.0f;

        XMVECTOR shadowOffset = XMVectorSet(1.0f, 1.0f, 0.0f, 0.0f);

        m_spriteBatch->Begin();

        // ---- 第一行：CPU（x86/x64）或 GPU 名称（ARM）----
        float line1X = screenWidth - nameSize.x - margin;
        float line1Y = margin;
        if (line1X < 0.0f) line1X = 0.0f;

        XMVECTOR pos1 = XMVectorSet(line1X, line1Y, 0.0f, 0.0f);
        m_spriteFont->DrawString(m_spriteBatch.get(), m_cpuText,
            pos1 + shadowOffset, DirectX::Colors::Black);
        m_spriteFont->DrawString(m_spriteBatch.get(), m_cpuText,
            pos1, DirectX::Colors::White);

        // ---- 第二行：GPU（仅 x86/x64）----
        float nextY = line1Y + lineHeight;
        if (hasGpuLine)
        {
            float line2X = screenWidth - gpuSize.x - margin;
            float line2Y = nextY;
            if (line2X < 0.0f) line2X = 0.0f;

            XMVECTOR pos2 = XMVectorSet(line2X, line2Y, 0.0f, 0.0f);
            m_spriteFont->DrawString(m_spriteBatch.get(), m_gpuText,
                pos2 + shadowOffset, DirectX::Colors::Black);
            m_spriteFont->DrawString(m_spriteBatch.get(), m_gpuText,
                pos2, DirectX::Colors::White);

            nextY += lineHeight;
        }

        // ---- 第三行（或 ARM 的第二行）：架构 + 核心数 ----
        {
            float line3X = screenWidth - archSize.x - margin;
            float line3Y = nextY;
            if (line3X < 0.0f) line3X = 0.0f;

            XMVECTOR pos3 = XMVectorSet(line3X, line3Y, 0.0f, 0.0f);
            m_spriteFont->DrawString(m_spriteBatch.get(), m_archText,
                pos3 + shadowOffset, DirectX::Colors::Black);
            m_spriteFont->DrawString(m_spriteBatch.get(), m_archText,
                pos3, DirectX::Colors::White);
        }

        // ---- 第四行（或 ARM 的第三行）：Windows 版本号 ----
        {
            XMFLOAT2 osSize;
            XMStoreFloat2(&osSize, m_spriteFont->MeasureString(m_osText));

            float line4X = screenWidth - osSize.x - margin;
            float line4Y = nextY + lineHeight;
            if (line4X < 0.0f) line4X = 0.0f;

            XMVECTOR pos4 = XMVectorSet(line4X, line4Y, 0.0f, 0.0f);
            m_spriteFont->DrawString(m_spriteBatch.get(), m_osText,
                pos4 + shadowOffset, DirectX::Colors::Black);
            m_spriteFont->DrawString(m_spriteBatch.get(), m_osText,
                pos4, DirectX::Colors::White);
        }

        m_spriteBatch->End();

        context->OMSetRenderTargets(1, &oldRTV, oldDSV);
        if (oldRTV) oldRTV->Release();
        if (oldDSV) oldDSV->Release();
    }
    catch (...)
    {
        OutputDebugStringA("System info rendering failed\n");
    }
}