#include "../includes/globals.h"
#include <stdio.h>
#include <vector>
// user mode entry point

std::vector<std::string> processes;

int main()
{
	// we create menu object
	gui menu;

	// initialize service manager with path to our driver
	// to-do: download file through ht client and save to disk
	service_manager manager(L"C:/drivers/km.sys");
	ioctl_manager ioctl(L"\\\\.\\Aegis");



	// run a loop while menu is not closed
	while (!menu.done)
	{
		if (menu.done)
			break;

		// handle window minimized or screen locked
		if (menu.swapchain_occluded && menu.get_swapchain()->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
		{
			::Sleep(10);
			continue;
		}

		menu.swapchain_occluded = false;

		// setup new frames, position and size
		menu.new_frame();

		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

		// beginning of imgui window, after just basic styling and components
		ImGui::Begin("Aegis", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);                          // Create a window called "Hello, world!" and append into it.

		ImGui::PushFont(menu.gilroy_bold);
		ImGui::Text("Aegis");
		ImGui::PopFont();
		ImGui::PushFont(menu.gilroy_light);
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetIO().DisplaySize.x-50);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, -100));
		ImGui::PushFont(menu.gilroy_bold);
		if (ImGui::Button("-", ImVec2(25, 25))) {
			menu.done = true;
		}
		ImGui::PopFont();
		ImGui::PopStyleVar();
		ImGui::Separator();

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 150);
		ImGui::Text("Type in the .exe name of an app:");
		ImGui::PushItemWidth(ImGui::GetIO().DisplaySize.x - 50);
		ImGui::InputText(" ", menu.text, sizeof(menu.text));
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 15);
		if (ImGui::Button("Protect", ImVec2(ImGui::GetIO().DisplaySize.x - 50, 50)))
		{
			if (!ioctl.check(menu.text))
			{
				auto err = GetLastError();
				char szerr[128];
				sprintf_s(szerr, "%d", err);
				MessageBoxA(NULL, szerr, "Error", MB_OK | MB_ICONERROR);
			}
			else
			{
				processes.push_back(menu.text);
				menu.reset_input_text();
				MessageBoxA(NULL, "Successful", "Done", MB_OK);
			}
		}

		if(!processes.empty())
			for (std::string text : processes)
			{
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
				ImGui::Text(text.c_str());
			}


		// if we hover over the window and not hover on any element we give control of our mouse to windows
		if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{

			ReleaseCapture();
			SendMessage(menu.get_window(), WM_NCLBUTTONDOWN, HTCAPTION, 0);


			// after moving window we give control back to imgui and reset mouse
			// DISCLAIMER: if we remove this after moving a window we need to click once to reset mouse state
			ImGui::GetIO().MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
			ImGui::GetIO().MouseDown[0] = false;
		}

		ImGui::PopFont();
		ImGui::End();


		// call to render all of our components
		menu.render();
	}
}