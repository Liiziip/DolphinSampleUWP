//--------------------------------------------------------------------------------------
// DolphinSample.h
//
// Modified version by jthecodebuilder and Joshua-Ashton (GitHub)
//
// Originally coded by
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#pragma once

#include "SpriteFont.h"
#include "SpriteBatch.h"
#include "DeviceResources.h"
#include "StepTimer.h"
#include "Dolphin.h"

#include <memory.h>

// A basic sample implementation that creates a D3D11 device and
// provides a render loop.
class Sample
{
public:

	Sample() noexcept(false);
	~Sample() = default;

	Sample(Sample&&) = default;
	Sample& operator= (Sample&&) = default;

	Sample(Sample const&) = delete;
	Sample& operator= (Sample const&) = delete;

	// Initialization and management
	void Initialize(IUnknown* window, int width, int height, DXGI_MODE_ROTATION rotation);

	// Basic render loop
	void Tick();

	// Messages
	void OnActivated();
	void OnDeactivated();
	void OnSuspending();
	void OnResuming();
	void OnWindowSizeChanged(int width, int height, DXGI_MODE_ROTATION rotation);
	void ValidateDevice();

	// Properties
	void GetDefaultSize(int& width, int& height) const;

private:

    void RenderFPS();
    void Update(DX::StepTimer const& timer);
    void Render();

    void Clear();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();

    void SetWaterColor(float red, float green, float blue);

    void DrawDolphin(Dolphin &dolphin);
    
    // Device resources.
    std::unique_ptr<DX::DeviceResources>            m_deviceResources;
                                                     
    // Rendering loop timer.                         
    uint64_t                                        m_frame;
    DX::StepTimer                                   m_timer;
                                                     
    // Input devices.                                
    std::unique_ptr<DirectX::GamePad>       m_gamePad;
    std::unique_ptr<DirectX::Keyboard>      m_keyboard;
    std::unique_ptr<DirectX::Mouse>         m_mouse;

    DirectX::GamePad::ButtonStateTracker    m_gamePadButtons;
    DirectX::Keyboard::KeyboardStateTracker m_keyboardButtons;
    bool                                    m_gamepadPresent;

    // DirectXTK objects.
    std::unique_ptr<DirectX::GraphicsMemory>        m_graphicsMemory;
                                                     
    // DirectXTK objects                             
    std::unique_ptr<DirectX::CommonStates>          m_states;
    std::unique_ptr<DirectX::EffectFactory>         m_fxFactory;
                                                     
    // Game state                                    
    Microsoft::WRL::ComPtr<ID3D11Buffer>            m_VSConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>            m_PSConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>      m_pSamplerMirror;
                                                     
    // Transform matrices                            
    DirectX::SimpleMath::Matrix                     m_matView;
    DirectX::SimpleMath::Matrix                     m_matProj;
                                                     
    // array of dolphins                             
    std::array<std::shared_ptr<Dolphin>, 1>         m_dolphins;

    // Seafloor object
    std::unique_ptr<DirectX::Model>                 m_seafloor;
    std::unique_ptr<DirectX::IEffect>               m_seaEffect;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_seaFloorTextureView;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_seaFloorVertexLayout;

    // Water caustics
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_causticTextureViews[32];
    unsigned int                                    m_currentCausticTextureView;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_pixelShader;

    float                                           m_waterColor[4];
    float                                           m_ambient[4];

    // Shader resource view for the whole screen
    // This is the input to the FrontPanelRender target
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_mainRenderTargetSRV;

    // FPS Display
    std::unique_ptr<DirectX::SpriteBatch>          m_spriteBatch;
    std::unique_ptr<DirectX::SpriteFont>           m_spriteFont;
    wchar_t                                        m_fpsText[32];
    float                                          m_fpsUpdateTimer;
    uint32_t                                       m_fpsFrameCount;
};
