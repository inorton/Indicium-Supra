#include <Utils/Windows.h>
#include <Utils/Hook.h>
#include <Utils/PipeServer.h>

#include "Game.h"
#include "Messagehandler.h"

#include "Rendering/Renderer.h"

#include <Psapi.h>
#pragma comment(lib, "psapi.lib")

#include <Game/Hook/Direct3D9.h>
#include <Game/Hook/Direct3D9Ex.h>
#include <Game/Hook/DXGI.h>
#include <Game/Hook/Direct3D10.h>
#include <Game/Hook/Direct3D11.h>
#include <Game/Hook/DirectInput8.h>

#include <Utils/PluginManager.h>


#define BIND(T) PaketHandler[PipeMessages::T] = std::bind(T, std::placeholders::_1, std::placeholders::_2);

// D3D9
Hook<CallConvention::stdcall_t, HRESULT, LPDIRECT3DDEVICE9, CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *> g_present9Hook;
Hook<CallConvention::stdcall_t, HRESULT, LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS *> g_reset9Hook;
Hook<CallConvention::stdcall_t, HRESULT, LPDIRECT3DDEVICE9> g_endScene9Hook;

// D3D9Ex
Hook<CallConvention::stdcall_t, HRESULT, LPDIRECT3DDEVICE9EX, CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *, DWORD> g_present9ExHook;
Hook<CallConvention::stdcall_t, HRESULT, LPDIRECT3DDEVICE9EX, D3DPRESENT_PARAMETERS *, D3DDISPLAYMODEEX *> g_reset9ExHook;

// D3D10
Hook<CallConvention::stdcall_t, HRESULT, IDXGISwapChain*, UINT, UINT> g_swapChainPresent10Hook;
Hook<CallConvention::stdcall_t, HRESULT, IDXGISwapChain*, const DXGI_MODE_DESC*> g_swapChainResizeTarget10Hook;

// D3D11
Hook<CallConvention::stdcall_t, HRESULT, IDXGISwapChain*, UINT, UINT> g_swapChainPresent11Hook;
Hook<CallConvention::stdcall_t, HRESULT, IDXGISwapChain*, const DXGI_MODE_DESC*> g_swapChainResizeTarget11Hook;

// DInput8
Hook<CallConvention::stdcall_t, HRESULT, LPDIRECTINPUTDEVICE8> g_acquire8Hook;
Hook<CallConvention::stdcall_t, HRESULT, LPDIRECTINPUTDEVICE8, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD> g_getDeviceData8Hook;
Hook<CallConvention::stdcall_t, HRESULT, LPDIRECTINPUTDEVICE8, LPDIDEVICEINSTANCE> g_getDeviceInfo8Hook;
Hook<CallConvention::stdcall_t, HRESULT, LPDIRECTINPUTDEVICE8, DWORD, LPVOID> g_getDeviceState8Hook;
Hook<CallConvention::stdcall_t, HRESULT, LPDIRECTINPUTDEVICE8, LPDIDEVICEOBJECTINSTANCE, DWORD, DWORD> g_getObjectInfo8Hook;


PluginManager g_plugins;
Renderer g_pRenderer;
bool g_bEnabled = false;
bool g_bIsUsingPresent = false;

extern "C" __declspec(dllexport) void enable()
{
	g_bEnabled = true;
}


void initGame()
{
	bool d3d9_available, d3d9ex_available, d3d10_available, d3d11_available, dinput8_available;

	auto sz_ProcName = static_cast<LPSTR>(malloc(MAX_PATH + 1));
	GetProcessImageFileName(GetCurrentProcess(), sz_ProcName, MAX_PATH);
	BOOST_LOG_TRIVIAL(info) << "Library loaded into " << sz_ProcName;
	free(sz_ProcName);

	BOOST_LOG_TRIVIAL(info) << "Library enabled";

	g_plugins.refresh();
	g_plugins.load();

	UINTX vtable9[Direct3D9Hooking::Direct3D9::VTableElements] = {0};
	UINTX vtable9Ex[Direct3D9Hooking::Direct3D9Ex::VTableElements] = {0};
	UINTX vtable10SwapChain[DXGIHooking::DXGI::SwapChainVTableElements] = {0};
	UINTX vtable11SwapChain[DXGIHooking::DXGI::SwapChainVTableElements] = {0};
	UINTX vtable8[DirectInput8Hooking::DirectInput8::VTableElements] = {0};

	// get VTable for Direct3DCreate9
	{
		Direct3D9Hooking::Direct3D9 d3d;
		d3d9_available = d3d.GetDeviceVTable(vtable9);

		if (!d3d9_available)
		{
			BOOST_LOG_TRIVIAL(error) << "Couldn't get VTable for Direct3DCreate9";
		}
	}

	// get VTable for Direct3DCreate9Ex
	{
		Direct3D9Hooking::Direct3D9Ex d3dEx;
		d3d9ex_available = d3dEx.GetDeviceVTable(vtable9Ex);

		if (!d3d9ex_available)
		{
			BOOST_LOG_TRIVIAL(error) << "Couldn't get VTable for Direct3DCreate9Ex";
		}
	}

	// get VTable for IDXGISwapChain (v10)
	{
		Direct3D10Hooking::Direct3D10 d3d10;
		d3d10_available = d3d10.GetSwapChainVTable(vtable10SwapChain);

		if (!d3d10_available)
		{
			BOOST_LOG_TRIVIAL(error) << "Couldn't get VTable for IDXGISwapChain";
		}
	}

	// get VTable for IDXGISwapChain (v11)
	{
		Direct3D11Hooking::Direct3D11 d3d11;
		d3d11_available = d3d11.GetSwapChainVTable(vtable11SwapChain);

		if (!d3d11_available)
		{
			BOOST_LOG_TRIVIAL(error) << "Couldn't get VTable for IDXGISwapChain";
		}
	}

	// Dinput8
	{
		DirectInput8Hooking::DirectInput8 di8;
		dinput8_available = di8.GetVTable(vtable8);

		if (!dinput8_available)
		{
			BOOST_LOG_TRIVIAL(error) << "Couldn't get VTable for DirectInput8";
		}
	}

	BOOST_LOG_TRIVIAL(info) << "Initializing hook engine...";

	if (MH_Initialize() != MH_OK)
	{
		BOOST_LOG_TRIVIAL(fatal) << "Couldn't initialize hook engine";
		return;
	}

	BOOST_LOG_TRIVIAL(info) << "Hook engine initialized";

	if (d3d9_available)
	{
		HookDX9(vtable9);
	}

	if (d3d9ex_available)
	{
		HookDX9Ex(vtable9Ex);
	}

	if (d3d10_available)
	{
		HookDX10(vtable10SwapChain);
	}

	if (d3d11_available)
	{
		HookDX11(vtable11SwapChain);
	}

	if (dinput8_available)
	{
		HookDInput8(vtable8);
	}


	typedef std::map<PipeMessages, std::function<void(Serializer&, Serializer&)>> MessagePaketHandler;
	MessagePaketHandler PaketHandler;

	BIND(TextCreate);
	BIND(TextDestroy);
	BIND(TextSetShadow);
	BIND(TextSetShown);
	BIND(TextSetColor);
	BIND(TextSetPos);
	BIND(TextSetString);
	BIND(TextUpdate);

	BIND(BoxCreate);
	BIND(BoxDestroy);
	BIND(BoxSetShown);
	BIND(BoxSetBorder);
	BIND(BoxSetBorderColor);
	BIND(BoxSetColor);
	BIND(BoxSetHeight);
	BIND(BoxSetPos);
	BIND(BoxSetWidth);

	BIND(LineCreate);
	BIND(LineDestroy);
	BIND(LineSetShown);
	BIND(LineSetColor);
	BIND(LineSetWidth);
	BIND(LineSetPos);

	BIND(ImageCreate);
	BIND(ImageDestroy);
	BIND(ImageSetShown);
	BIND(ImageSetAlign);
	BIND(ImageSetPos);
	BIND(ImageSetRotation);
	BIND(ImageSetScale);

	BIND(DestroyAllVisual);
	BIND(ShowAllVisual);
	BIND(HideAllVisual);

	BIND(GetFrameRate);
	BIND(GetScreenSpecs);

	BIND(SetCalculationRatio);
	BIND(SetOverlayPriority);

	new PipeServer([&](Serializer& serializerIn, Serializer& serializerOut)
		{
			SERIALIZATION_READ(serializerIn, PipeMessages, eMessage);

			try
			{
				auto it = PaketHandler.find(eMessage);
				if (it == PaketHandler.end())
					return;

				if (!PaketHandler[eMessage])
					return;

				PaketHandler[eMessage](serializerIn, serializerOut);
			}
			catch (...)
			{
			}
		});

	// block this thread infinitely
	WaitForSingleObject(INVALID_HANDLE_VALUE, INFINITE);
}

void logOnce(std::string message)
{
	BOOST_LOG_TRIVIAL(info) << message;
}

void HookDX9(UINTX* vtable9)
{
	BOOST_LOG_TRIVIAL(info) << "Hooking IDirect3DDevice9::Present";

	g_present9Hook.apply(vtable9[Direct3D9Hooking::Present], [](LPDIRECT3DDEVICE9 dev, CONST RECT* a1, CONST RECT* a2, HWND a3, CONST RGNDATA* a4) -> HRESULT
	                     {
		                     static boost::once_flag flag = BOOST_ONCE_INIT;
		                     boost::call_once(flag, boost::bind(&logOnce, "++ IDirect3DDevice9::Present called"));

		                     g_plugins.present(IID_IDirect3DDevice9, dev);

		                     return g_present9Hook.callOrig(dev, a1, a2, a3, a4);
	                     });

	BOOST_LOG_TRIVIAL(info) << "Hooking IDirect3DDevice9::Reset";

	g_reset9Hook.apply(vtable9[Direct3D9Hooking::Reset], [](LPDIRECT3DDEVICE9 dev, D3DPRESENT_PARAMETERS* pp) -> HRESULT
	                   {
		                   static boost::once_flag flag = BOOST_ONCE_INIT;
		                   boost::call_once(flag, boost::bind(&logOnce, "++ IDirect3DDevice9::Reset called"));

		                   // g_pRenderer.reset(dev);

		                   return g_reset9Hook.callOrig(dev, pp);
	                   });

	BOOST_LOG_TRIVIAL(info) << "Hooking IDirect3DDevice9::EndScene";

	g_endScene9Hook.apply(vtable9[Direct3D9Hooking::EndScene], [](LPDIRECT3DDEVICE9 dev) -> HRESULT
	                      {
		                      static boost::once_flag flag = BOOST_ONCE_INIT;
		                      boost::call_once(flag, boost::bind(&logOnce, "++ IDirect3DDevice9::EndScene called"));

		                      /* 		if (!g_bIsUsingPresent)
				                      {
					                      if (!g_bIsImGuiInitialized)
					                      {
						                      if (g_hWnd)
						                      {
							                      ImGui_ImplDX9_Init(g_hWnd, dev);
		                      
							                      g_bIsImGuiInitialized = true;
						                      }
					                      }
					                      else
					                      {
						                      ImGui_ImplDX9_NewFrame();
						                      RenderScene();
					                      }
				                      }
				                      */

		                      return g_endScene9Hook.callOrig(dev);
	                      });
}

void HookDX9Ex(UINTX* vtable9Ex)
{
	BOOST_LOG_TRIVIAL(info) << "Hooking IDirect3DDevice9Ex::PresentEx";

	g_present9ExHook.apply(vtable9Ex[Direct3D9Hooking::PresentEx], [](LPDIRECT3DDEVICE9EX dev, CONST RECT* a1, CONST RECT* a2, HWND a3, CONST RGNDATA* a4, DWORD a5) -> HRESULT
	                       {
		                       static boost::once_flag flag = BOOST_ONCE_INIT;
		                       boost::call_once(flag, boost::bind(&logOnce, "++ IDirect3DDevice9Ex::PresentEx called"));

		                       g_plugins.present(IID_IDirect3DDevice9Ex, dev);

		                       return g_present9ExHook.callOrig(dev, a1, a2, a3, a4, a5);
	                       });

	BOOST_LOG_TRIVIAL(info) << "Hooking IDirect3DDevice9Ex::ResetEx";

	g_reset9ExHook.apply(vtable9Ex[Direct3D9Hooking::ResetEx], [](LPDIRECT3DDEVICE9EX dev, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* ppp) -> HRESULT
	                     {
		                     static boost::once_flag flag = BOOST_ONCE_INIT;
		                     boost::call_once(flag, boost::bind(&logOnce, "++ IDirect3DDevice9Ex::ResetEx called"));

		                     // g_pRenderer.reset(dev);

		                     return g_reset9ExHook.callOrig(dev, pp, ppp);
	                     });
}

void HookDX10(UINTX* vtable10SwapChain)
{
	BOOST_LOG_TRIVIAL(info) << "Hooking IDXGISwapChain::Present";

	g_swapChainPresent10Hook.apply(vtable10SwapChain[DXGIHooking::Present], [](IDXGISwapChain* chain, UINT SyncInterval, UINT Flags) -> HRESULT
	                               {
		                               g_plugins.present(IID_IDXGISwapChain, chain);

		                               /* if (!failed)
		                               {
			                               if (!g_bIsImGuiInitialized)
			                               {
				                               if (g_hWnd)
				                               {
					                               static ID3D10Device* dev = nullptr;
					                               auto hr = chain->GetDevice(__uuidof(dev), reinterpret_cast<void**>(&dev));
                               
					                               if (SUCCEEDED(hr))
					                               {
						                               ImGui_ImplDX10_Init(g_hWnd, dev);
                               
						                               BOOST_LOG_TRIVIAL(info) << "ImGui (DX10) initialized";
                               
						                               g_bIsImGuiInitialized = true;
					                               }
					                               else
					                               {
						                               BOOST_LOG_TRIVIAL(error) << "!! Couldn't get ID3D10Device";
                               
						                               failed = true;
					                               }
				                               }
			                               }
			                               else
			                               {
				                               ImGui_ImplDX10_NewFrame();
				                               RenderScene();
			                               }
		                               }*/

		                               return g_swapChainPresent10Hook.callOrig(chain, SyncInterval, Flags);
	                               });

	BOOST_LOG_TRIVIAL(info) << "Hooking IDXGISwapChain::ResizeTarget";

	g_swapChainResizeTarget10Hook.apply(vtable10SwapChain[DXGIHooking::ResizeTarget], [](IDXGISwapChain* chain, const DXGI_MODE_DESC* pNewTargetParameters) -> HRESULT
	                                    {
		                                    static boost::once_flag flag = BOOST_ONCE_INIT;
		                                    boost::call_once(flag, boost::bind(&logOnce, "++ IDXGISwapChain::ResizeTarget (v10) called"));

		                                    return g_swapChainResizeTarget10Hook.callOrig(chain, pNewTargetParameters);
	                                    });
}

void HookDX11(UINTX* vtable11SwapChain)
{
	BOOST_LOG_TRIVIAL(info) << "Hooking IDXGISwapChain::Present";

	g_swapChainPresent11Hook.apply(vtable11SwapChain[DXGIHooking::Present], [](IDXGISwapChain* chain, UINT SyncInterval, UINT Flags) -> HRESULT
	                               {
		                               static boost::once_flag flag = BOOST_ONCE_INIT;
		                               boost::call_once(flag, boost::bind(&logOnce, "++ IDXGISwapChain::Present (v11) called"));

		                               g_plugins.present(IID_IDXGISwapChain, chain);

		                               return g_swapChainPresent11Hook.callOrig(chain, SyncInterval, Flags);
	                               });

	BOOST_LOG_TRIVIAL(info) << "Hooking IDXGISwapChain::ResizeTarget";

	g_swapChainResizeTarget11Hook.apply(vtable11SwapChain[DXGIHooking::ResizeTarget], [](IDXGISwapChain* chain, const DXGI_MODE_DESC* pNewTargetParameters) -> HRESULT
	                                    {
		                                    static boost::once_flag flag = BOOST_ONCE_INIT;
		                                    boost::call_once(flag, boost::bind(&logOnce, "++ IDXGISwapChain::ResizeTarget (v11) called"));

		                                    return g_swapChainResizeTarget11Hook.callOrig(chain, pNewTargetParameters);
	                                    });
}

void HookDInput8(UINTX* vtable8)
{
	BOOST_LOG_TRIVIAL(info) << "Hooking IDirectInputDevice8::Acquire";

	g_acquire8Hook.apply(vtable8[DirectInput8Hooking::Acquire], [](LPDIRECTINPUTDEVICE8 dev) -> HRESULT
	                     {
		                     static boost::once_flag flag = BOOST_ONCE_INIT;
		                     boost::call_once(flag, boost::bind(&logOnce, "++ IDirectInputDevice8::Acquire called"));

		                     return g_acquire8Hook.callOrig(dev);
	                     });

	BOOST_LOG_TRIVIAL(info) << "Hooking IDirectInputDevice8::GetDeviceData";

	g_getDeviceData8Hook.apply(vtable8[DirectInput8Hooking::GetDeviceData], [](LPDIRECTINPUTDEVICE8 dev, DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) -> HRESULT
	                           {
		                           static boost::once_flag flag = BOOST_ONCE_INIT;
		                           boost::call_once(flag, boost::bind(&logOnce, "++ IDirectInputDevice8::Acquire called"));

		                           return g_getDeviceData8Hook.callOrig(dev, cbObjectData, rgdod, pdwInOut, dwFlags);
	                           });

	BOOST_LOG_TRIVIAL(info) << "Hooking IDirectInputDevice8::GetDeviceInfo ";

	g_getDeviceInfo8Hook.apply(vtable8[DirectInput8Hooking::GetDeviceInfo], [](LPDIRECTINPUTDEVICE8 dev, LPDIDEVICEINSTANCE pdidi) -> HRESULT
	                           {
		                           static boost::once_flag flag = BOOST_ONCE_INIT;
		                           boost::call_once(flag, boost::bind(&logOnce, "++ IDirectInputDevice8::GetDeviceInfo called"));

		                           return g_getDeviceInfo8Hook.callOrig(dev, pdidi);
	                           });

	BOOST_LOG_TRIVIAL(info) << "Hooking IDirectInputDevice8::GetDeviceState";

	g_getDeviceState8Hook.apply(vtable8[DirectInput8Hooking::GetDeviceState], [](LPDIRECTINPUTDEVICE8 dev, DWORD cbData, LPVOID lpvData) -> HRESULT
	                            {
		                            static boost::once_flag flag = BOOST_ONCE_INIT;
		                            boost::call_once(flag, boost::bind(&logOnce, "++ IDirectInputDevice8::GetDeviceState called"));

		                            return g_getDeviceState8Hook.callOrig(dev, cbData, lpvData);
	                            });

	BOOST_LOG_TRIVIAL(info) << "Hooking IDirectInputDevice8::GetObjectInfo";

	g_getObjectInfo8Hook.apply(vtable8[DirectInput8Hooking::GetObjectInfo], [](LPDIRECTINPUTDEVICE8 dev, LPDIDEVICEOBJECTINSTANCE pdidoi, DWORD dwObj, DWORD dwHow) -> HRESULT
	                           {
		                           static boost::once_flag flag = BOOST_ONCE_INIT;
		                           boost::call_once(flag, boost::bind(&logOnce, "++ IDirectInputDevice8::GetObjectInfo called"));

		                           return g_getObjectInfo8Hook.callOrig(dev, pdidoi, dwObj, dwHow);
	                           });
}

