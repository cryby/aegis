#pragma once
#include "../../includes/globals.h"

class gui
{
	private:
		HWND window;
		WNDCLASSEXW wc;
		ID3D11Device* d3d_device = nullptr;
		ID3D11DeviceContext* d3d_device_context = nullptr;
		IDXGISwapChain* swap_chain = nullptr;
		UINT resize_width = 0, resize_height = 0;
		ID3D11RenderTargetView* main_rendertarget_view = nullptr;
		static LRESULT WINAPI WndProc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	public:
		bool done = false;
		bool swapchain_occluded = false;
		ImFont* gilroy_bold;
		ImFont* gilroy_light;

		char text[128] = "";

	public:
		gui();
		void new_frame();
		void render();
		void setup_style();
		~gui();

		IDXGISwapChain* get_swapchain();
		HWND get_window();
		void reset_input_text();

	private:
		bool create_deviced3d(HWND window);
		void cleanup_deviced3d();
		void create_render_target();
		void cleanup_render_target();
};