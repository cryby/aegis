#include "gui.h"
#pragma comment(lib, "d3d11.lib")
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include "../../includes/fonts.h"

gui::gui()
{
	// make process dpi aware and get monitor scale
	ImGui_ImplWin32_EnableDpiAwareness();
	float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	// creating class definition of window (blueprint for window)
	this->wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
	::RegisterClassExW(&wc);

	// creation of physical window (name of blueprint, title, style, position and size, pass the instance of our blueprint)
	this->window = CreateWindowEx(0, wc.lpszClassName, L"Aegis", WS_POPUP, 100, 100, 300, 500, NULL, NULL, wc.hInstance, NULL);
	MARGINS margins = { -1 };
	DwmExtendFrameIntoClientArea(this->window, &margins);

	// initialize direct3d
	if (!this->create_deviced3d(this->window))
	{
		// if failed cleanup and unregister
		this->cleanup_deviced3d();
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		exit(1);
	}

	// show the window
	::ShowWindow(this->window, SW_SHOWDEFAULT);
	::UpdateWindow(this->window);
	ShowWindow(GetConsoleWindow(), SW_HIDE);

	// setup imgui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	// setup styles
	gui::setup_style();

	// setup scaling
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale); // fixed style scale
	style.FontScaleDpi = main_scale; // initial font scale

	ImGuiIO& io = ImGui::GetIO();
	ImFontConfig cfg;
	cfg.OversampleH = 3;
	cfg.OversampleV = 3;

	cfg.FontDataOwnedByAtlas = false; // this tells imgui that the font data is owned by me and to keep it after closing


	// initialize two font from memory, the definition is in fonts.h byte array from HxD
	gilroy_bold = io.Fonts->AddFontFromMemoryTTF(
		(void*)data_gilroy_bold,
		sizeof(data_gilroy_bold),
		26.0f,
		&cfg
	);


	gilroy_light = io.Fonts->AddFontFromMemoryTTF(
		(void*)data_gilroy_light,
		sizeof(data_gilroy_light),
		19.0f,
		&cfg
	);

	// setup platform and renderer backends
	ImGui_ImplWin32_Init(this->window);
	ImGui_ImplDX11_Init(this->d3d_device, this->d3d_device_context);

}

gui::~gui()
{
	// on destruct we destroy window unregister classes and cleanup
	::DestroyWindow(this->window);
	::UnregisterClassW(this->wc.lpszClassName, this->wc.hInstance);

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	this->cleanup_deviced3d();
}

void gui::render()
{
	// rendering
	ImGui::Render();
	const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	this->d3d_device_context->OMSetRenderTargets(1, &main_rendertarget_view, nullptr);
	this->d3d_device_context->ClearRenderTargetView(main_rendertarget_view, clear_color_with_alpha);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	// present
	HRESULT hr = this->swap_chain->Present(1, 0); // present with vsync
	// we check if our front buffer is visible, if not we turn of and sleep for 10ms and try again
	this->swapchain_occluded = (hr == DXGI_STATUS_OCCLUDED);
}

void gui::setup_style()
{
	// set up basic style for ui design (padding, spacing, colors)
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	style.WindowRounding = 12.0f;
	style.ChildRounding = 10.0f;
	style.FrameRounding = 6.0f;
	style.PopupRounding = 8.0f;
	style.GrabRounding = 6.0f;

	style.WindowPadding = ImVec2(20, 30);

	colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);

	colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.22f, 0.50f);

	colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);

	ImVec4 accentColor = ImVec4(0.38f, 0.45f, 0.85f, 1.00f);
	colors[ImGuiCol_Button] = accentColor;
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.52f, 0.90f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.39f, 0.80f, 1.00f);

	colors[ImGuiCol_Header] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);

	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
}

void gui::new_frame()
{
	// poll and handle messages (inputs, window resize)
	MSG msg;
	while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
	{
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
		if (msg.message == WM_QUIT)
			this->done = true;
	}

	// handle window resize
	if (this->resize_width != 0 && this->resize_height != 0)
	{
		this->cleanup_render_target();
		this->swap_chain->ResizeBuffers(0, this->resize_width, this->resize_height, DXGI_FORMAT_UNKNOWN, 0);
		this->resize_width = this->resize_height = 0;
		this->create_render_target();
	}

	// start imgui frame

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

bool gui::create_deviced3d(HWND window)
{
	// setting up swapchain (how the image should be changed)
	DXGI_SWAP_CHAIN_DESC sd;
	RtlZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2; // 2 canvases, one for visual second for drawing
	sd.BufferDesc.Width = 0; // automatic width of canvas
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Colors (r,g,b,a)
	sd.BufferDesc.RefreshRate.Numerator = 60; // 60fps
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; // allow alt-tab
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // usage for drawing
	sd.OutputWindow = window; // we set drawing to our window
	sd.SampleDesc.Count = 1; // no antialiasing for power saving
	sd.Windowed = TRUE; // no fullscreen
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; // throw away old canvas

	UINT createdevice_flags = 0;
	D3D_FEATURE_LEVEL feature_level;
	const D3D_FEATURE_LEVEL level_array[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

	HRESULT result = D3D11CreateDeviceAndSwapChain(
		NULL, // use primary gpu
		D3D_DRIVER_TYPE_HARDWARE, // use gpu (not cpu emulation)
		NULL, 
		createdevice_flags,
		level_array, 2, // support dx11 & dx10
		D3D11_SDK_VERSION,
		&sd, // point to our settings
		&swap_chain, // get swapchain
		&d3d_device, // get device
		&feature_level,
		&d3d_device_context // get context
	);

	// if failed used warp
	if (result == DXGI_ERROR_UNSUPPORTED)
		result = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, createdevice_flags, level_array, 2, D3D11_SDK_VERSION, &sd, &swap_chain, &d3d_device, &feature_level, &d3d_device_context);

	if (result != S_OK)
		return false;

	this->create_render_target();
	return true;
}

void gui::cleanup_deviced3d()
{
	this->cleanup_render_target(); // cleanup render\

	// release and clean up our variables
	if (swap_chain) { swap_chain->Release(); swap_chain = nullptr; };
	if (d3d_device_context) { d3d_device_context->Release(); d3d_device_context = nullptr; };
	if (d3d_device) { d3d_device->Release(); d3d_device = nullptr; };
}

void gui::create_render_target()
{
	ID3D11Texture2D* back_buffer = nullptr; // back canvas the one we draw on
	swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));

	if (back_buffer != 0)
	{

		d3d_device->CreateRenderTargetView(back_buffer, nullptr, &main_rendertarget_view);
		back_buffer->Release();
	}
}

void gui::cleanup_render_target()
{
	// cleanup of target
	if (main_rendertarget_view) { main_rendertarget_view->Release(); main_rendertarget_view = nullptr; };
}

// forward declare
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);

// window process for basic functions like resize or minimize
LRESULT WINAPI gui::WndProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (ImGui_ImplWin32_WndProcHandler(window, msg, wparam, lparam))
		return true;

	switch (msg)
	{
		case WM_SIZE:
		{
			if (wparam == SIZE_MINIMIZED)
				return 0;
			break;
		}
		case WM_NCHITTEST:
		{
			return HTCLIENT;
		}

	}

	return ::DefWindowProcW(window, msg, wparam, lparam);
}

// getters and setters helper
IDXGISwapChain* gui::get_swapchain()
{
	return this->swap_chain;
}

HWND gui::get_window()
{
	return this->window;
}

void gui::reset_input_text()
{
	memset(this->text, 0, 128); // didnt find much ways to clear an array of chars, so we just set the
	// memory to 0 128 times and set the first char to space
	this->text[0] = ' ';
}