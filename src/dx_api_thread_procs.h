#ifndef DX_API_THREAD_PROCS_H
#define DX_API_THREAD_PROCS_H

#include <thread>
#include "dx_api_app_types.h"
#include "dx_api_constants.h"
#include "dx_api_draw_procs.h"
#include "dx_api_helper_procs.h"
#include "dx_api_network_procs.h"
#include "dx_api_network_types.h"
#include "imgui.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_impl_sdl2.h"
#include "SDL.h"

namespace dx_api_explorer
{
// Main loop for processing network requests. Should be run on its own thread. 
// Runs continously at around 40 hertz to ensure we don't fry a CPU core idling.
auto network_thread_main_loop(app_context_t& app) -> void
{
    while (!app.shutdown_requested.test())
    {
        auto ticks_begin = get_ticks();

        net_call_t call;
        bool have_call = false;
        {
            // Lock just long enough to pop off one call, ensuring smooth FPS on GUI thread.
            std::scoped_lock lock(app.dx_request_mutex);
            if (!app.dx_request_queue.empty())
            {
                call = app.dx_request_queue.front();
                have_call = true;
            }
        }

        if (have_call)
        {
            // Execute the call, then lock both queues to prevent any race conditions
            // when moving the op from the pending to the ready queue.
            handle_request(call);
            std::scoped_lock request_lock(app.dx_request_mutex);
            std::scoped_lock response_lock(app.dx_response_mutex);
            app.dx_response_queue.push(call);
            app.dx_request_queue.pop();
        }

        auto ticks_end = get_ticks();
        auto delta_ticks = ticks_end - ticks_begin;
        if (delta_ticks < network_thread_period_ticks)
        {
            auto sleep_ticks = network_thread_period_ticks - delta_ticks;
            std::this_thread::sleep_for(sleep_ticks);
        }
    }
}

// Primary main loop.
auto app_thread_main_loop(app_context_t& app, SDL_Window* window, SDL_Renderer* renderer) -> void
{
    auto& io = ImGui::GetIO();

    while (!app.shutdown_requested.test())
    {
        // Handle events.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            // First we let Dear ImGui process the event.
            ImGui_ImplSDL2_ProcessEvent(&event);

            // Now we take a look to see if there's anything of interest.
            if (event.type == SDL_QUIT)
            {
                app.shutdown_requested.test_and_set();
            }
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
            {
                app.shutdown_requested.test_and_set();
            }
        }

        // Process network responses. We handle all responses at once because we
        // can reasonably assume that processing will go quickly and there will be no
        // GUI slowdown.
        {
            std::scoped_lock lock(app.dx_response_mutex);

            while (!app.dx_response_queue.empty())
            {
                auto& call = app.dx_response_queue.front();
                handle_response(call, app);
                app.dx_response_queue.pop();
            }
        }

        // If window is minimized, skip frame and sleep.
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // Check to see if we should switch fonts.
        static int old_font_index = app.font_index;
        if (app.font_index != old_font_index)
        {
            io.FontDefault = io.Fonts->Fonts[app.font_index];
            old_font_index = app.font_index;
        }

        // Start frame.
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Show windows.
        draw_main_window(app);
        if (app.show_debug_window)
        {
            draw_debug_window(app);
        }
        if (app.show_demo_window)
        {
            ImGui::ShowDemoWindow(&app.show_demo_window);
        }
        if (!app.flash.empty())
        {
            draw_flash_window(app);
        }

        // End frame and render
        ImGui::Render();
        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
}
}

#endif // DX_API_THREAD_PROCS_H