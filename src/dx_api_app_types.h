#ifndef DX_API_APP_TYPES_H
#define DX_API_APP_TYPES_H

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include "dx_api_model_types.h"
#include "dx_api_network_types.h"

namespace dx_api_explorer
{
// Used to indicate what information is available for display.
enum struct app_status_t
{
    logged_out,
    logged_in,
    open_case,
    open_assignment,
    open_action
};

// Supported event types. Used for events which can't be handled trivially/locally.
enum struct app_event_type_t
{
    // This causes all windows to go back to default size/placement. It will generally be invoked by a menu
    // item which will likely live in a window. That window will therefore already have had its layout established
    // by the time the reset is requested. So we treat the resest as an event, and on the next frame, the window
    // will check for that event and react to it if active.
    reset_window_layout
};
using app_events_t = std::vector<app_event_type_t>;

// Application state.
struct app_context_t
{
    // Display data. //////////////////
    app_status_t    status              = app_status_t::logged_out;
    bool            show_debug_window   = true;
    bool            show_demo_window    = false;
    bool            show_xray           = false;
    int             font_index          = -1; // Will trigger auto font size selection.

    // General data. //////////////////
    std::string access_token;
    std::string flash; // Messages (usually errors) that should be highlighted to the user.
    std::string endpoint;
    std::string request_headers;
    std::string request_body;
    std::string response_headers;
    std::string response_body;
    std::string user_id;
    std::string password;
    std::string server;
    std::string dx_api_path;
    std::string token_endpoint;
    std::string client_id;
    std::string client_secret;
    std::string component_debug_json = "Click a component to display its JSON.\nThe format is:\n  Type: Name [Info]\n\nInfo varies by component:\n- Reference [Target Type]\n- View [Template]";
    std::string field_debug_json = "Click a field to display its JSON.";

    // DX API response data. //////////
    std::vector<case_type_t>    case_types;
    case_info_t				    case_info;
    resources_t                 resources;
    std::string			        open_assignment_id;
    std::string                 open_action_id;
    std::string                 root_component_key;
    std::string                 etag; // https://docs.pega.com/bundle/dx-api/page/platform/dx-api/building-constellation-dx-api-request.html

    // Threading data. ////////////////
    net_call_queue_t    dx_request_queue;
    std::mutex          dx_request_mutex;
    net_call_queue_t    dx_response_queue;
    std::mutex          dx_response_mutex;
    std::atomic_flag    shutdown_requested;

    // World's simplest event bus. ////
    app_events_t    requested_events;
    app_events_t    active_events;

    app_context_t() { shutdown_requested.clear(); }
};
}

#endif // DX_API_APP_TYPES_H