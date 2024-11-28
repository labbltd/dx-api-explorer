#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <unordered_map>
#include <vector>

#include "httplib.h"
#include "imgui.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_impl_sdl2.h"
#include "imgui_stdlib.h"
#include "nlohmann/json.hpp"
#include "SDL.h"

using json = nlohmann::json;

namespace dx_api_explorer
{
namespace
{

#pragma region Constants:
///////////////////////////////////////////////////////////////////////////////

    constexpr auto config_file_name = "dx_api_explorer_config.json";
    constexpr auto json_indent = 2;
    constexpr auto network_thread_period_ticks = std::chrono::milliseconds{ 25 };
    constexpr auto spinner_period_ticks = std::chrono::milliseconds{ 15 };
    constexpr std::array<std::pair<float, const char*>, 6> font_sizes =
    { 
        std::make_pair(10.0f, "10"), 
        std::make_pair(20.0f, "20"),
        std::make_pair(30.0f, "30"),
        std::make_pair(40.0f, "40"),
        std::make_pair(50.0f, "50"),
        std::make_pair(60.0f, "60")
    };
    constexpr auto font_file_name = "Cousine-Regular.ttf";
    constexpr auto hidpi_pixel_width_threshold = 900; // Screen width divided by this gives us our default index into our font sizes array.

///////////////////////////////////////////////////////////////////////////////
#pragma endregion

#pragma region Helper data:
///////////////////////////////////////////////////////////////////////////////

// This function object lets us have default-constructible unique_ptr's with custom deleters.
template<typename data_t, void (*delete_data)(data_t*)>
class deleter_t
{
public:
    void operator()(data_t* data)
    {
        delete_data(data);
    }
};

// This alias is much nicer to use than the preceding.
template<typename data_t, void (*delete_data)(data_t*)>
using unique_ptr_t = std::unique_ptr<data_t, deleter_t<data_t, delete_data>>;

// A la: https://en.cppreference.com/w/cpp/experimental/scope_exit
template<typename exit_func>
class scope_exit
{
private:
    exit_func func;
public:
    scope_exit(exit_func _func) : func(_func) {}
    ~scope_exit() { func(); }
};

// For enum type specifier strings.
template<int enum_count>
using enum_c_strs_t = std::array<const char*, enum_count>;

///////////////////////////////////////////////////////////////////////////////
#pragma endregion

#pragma region DX API data:
///////////////////////////////////////////////////////////////////////////////

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e1783
struct field_t
{
    std::string id;
    std::string class_id;
    std::string label;
    std::string type;
    std::string data;
    std::string json;

    bool is_special     = false;
    bool is_class_key   = false;
    bool is_dirty       = false;
};
using field_map_t = std::unordered_map<std::string, field_t>;

// Types of components: https://docs.pega.com/bundle/constellation-sdk/page/constellation-sdks/sdks/using-dx-component-builder.html#d15866e82
enum component_type_t
{
    component_type_unspecified,
    component_type_unknown,
    // ...always first.

    // Design system extensions:
    // ...nothing yet!

    // Infrastructure:
    component_type_reference,
    component_type_region,
    component_type_view,

    // Fields:
    component_type_text_area,
    component_type_text_input,

    // Templates:
    component_type_default_form,

    // Widgets:
    // ...nothing yet!

    // Always last:
    component_type_count,
};

// Component strings as we'll see them in DX API responses, should be in same order as enum.
constexpr enum_c_strs_t<component_type_count> component_type_strings =
{
    "Unspecified",
    "Unknown",
    "Reference",
    "Region",
    "View",
    "TextArea",
    "TextInput",
    "DefaultForm",
};

// Component megastruct:
struct component_t;
using component_list_t = std::vector<component_t>;
struct component_t
{
    component_type_t    type = component_type_t::component_type_unspecified;
    std::string name;
    std::string class_id;
    std::string key; // Identifies this rule, or the referenced rule in the case of references/fields.
    // ...required for all components!

    std::string label;
    std::string json;
    std::string debug_string;
    std::string broken_string;

    bool is_readonly = false;
    bool is_required = false;
    bool is_disabled = false;
    bool is_broken = false;

    component_type_t ref_type = component_type_t::component_type_unspecified; // Referenced component / type of template.
    component_list_t children;
};
using component_map_t = std::unordered_map<std::string, component_t>;

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e2455
struct action_t
{
    std::string id;
    std::string name;
    std::string type;
};
using action_map_t = std::unordered_map<std::string, action_t>;

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e1053
struct assignment_t
{
    std::string id;
    std::string name;
    bool can_perform = false;
    action_map_t actions;
};
using assignment_map_t = std::unordered_map<std::string, assignment_t>;

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e800
using content_map_t = std::unordered_map<std::string, std::string>;

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/endpoint-get-casetypes.html
struct case_type_t
{
    std::string id;
    std::string name;
};

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e350
struct case_info_t
{
    case_type_t	type;
    std::string id;
    std::string business_id;
    std::string create_time;
    std::string created_by;
    std::string last_update_time;
    std::string last_updated_by;
    std::string name;
    std::string owner;
    std::string status;

    assignment_map_t	assignments;
    content_map_t       content;
};

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e1783
struct resources_t
{
    field_map_t fields;
    component_map_t components;
};

///////////////////////////////////////////////////////////////////////////////
#pragma endregion

#pragma region Network data:
///////////////////////////////////////////////////////////////////////////////

// https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods
// Using a plain enum so it can index into an array without casting.
enum http_method_t
{
    http_method_unspecified,
    http_method_unknown,
    // ...always first.

    http_method_get,
    http_method_head,
    http_method_options,
    http_method_trace,
    http_method_put,
    http_method_delete,
    http_method_post,
    http_method_patch,
    http_method_connect,

    // Always last:
    http_method_count
};

// Strings that correspond to http method enums.
constexpr enum_c_strs_t<http_method_count> http_method_strings =
{
    "Unspecified",
    "Unknown",
    "Get",
    "Head",
    "Options",
    "Trace",
    "Put",
    "Delete",
    "Post",
    "Patch",
    "Connect"
};

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/constellacion-dx-api-endponts.html
enum struct net_call_type_t
{
    none,
    login,
    refresh_case_types,
    create_case,
    open_assignment,
    open_assignment_action,
    submit_assignment_action
};

// Megastruct representation of a DX API call. Main thread provides call type and appropriate
// input variables. Network thread executes call and fills out output variables.
struct net_call_t
{
    // Input:
    net_call_type_t type = net_call_type_t::none;
    std::string client_id;
    std::string client_secret;
    std::string dx_api_path;
    std::string id1; // pzInsKey such as: "MYORG-MYCO-WORK-MYCASE C-123" or "ASSIGN-WORKLIST MYORG-MYCO-WORK-MYCASE C-123!MY_FLOW"
    std::string id2; // pyID such as: "MyFlowAction"
    std::string password;
    std::string server;
    std::string user_id;
    std::string work_type_id;

    // Input/Output:
    std::string access_token;
    std::string endpoint;

    // Output:
    bool        succeeded = false; // Important that this is always initialized to false!
    std::string method;
    std::string error_message;
    std::string etag;
    std::string request_headers;
    std::string request_body;
    std::string response_headers;
    std::string response_body;
};
using net_call_queue_t = std::queue<net_call_t>;

///////////////////////////////////////////////////////////////////////////////
#pragma endregion

#pragma region Application data:
///////////////////////////////////////////////////////////////////////////////

// Used to indicate what information is available for display.
enum struct app_status_t
{
    logged_out,
    logged_in,
    open_case,
    open_assignment,
    open_action
};

// Application state.
struct app_context_t
{
    // Display data. //////////////////
    app_status_t status = app_status_t::logged_out;
    bool show_debug_window	= true;
    bool show_demo_window	= false;
    int font_index = -1;
    
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
    net_call_queue_t     dx_request_queue;
    std::mutex          dx_request_mutex;
    net_call_queue_t     dx_response_queue;
    std::mutex          dx_response_mutex;
    std::atomic_flag    shutdown_requested;

    app_context_t() { shutdown_requested.clear(); }
};

///////////////////////////////////////////////////////////////////////////////
#pragma endregion

#pragma region Helper functions:
///////////////////////////////////////////////////////////////////////////////

// Returns the number of milliseconds that have passed since the high resolution clock epoch.
auto get_ticks() -> std::chrono::milliseconds
{
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    return ticks;
}

// Return lowercase string. "Good enough is good enough."
auto to_lower(std::string_view str) -> std::string
{
    std::string result{ str };
    std::transform(str.begin(), str.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return result;
}

// This converts a string to an enum type identifier. It assumes that:
//  - The first enum is 0 and corresponds to "unspecified."
//  - The second enum is 1 and corresponds to "unknown."
//  - The provided enum strings correspond 1:1 to the count of the provided enum type.
//
// Usage looks like:
//  enum_type_t result = to_enum<enum_type_t>(str, enum_type_strings);
//
// You shouldn't have to specifiy the enum count because that should be inferred
// from the enum strings parameter.
template<typename enum_t, int enum_count>
auto to_enum(std::string_view str, enum_c_strs_t<enum_count> enum_strings) -> enum_t
{
    auto str_lower = to_lower(str);
    auto result = static_cast<enum_t>(1);
    for (int i = result + 1; i < enum_count; ++i)
    {
        auto enum_str_lower = to_lower(enum_strings[i]);
        if (str_lower == enum_str_lower) result = static_cast<enum_t>(i);
    };

    return result;
}

// Robustly extracts a boolean value from the provided JSON.
auto to_bool(const json& j) -> bool
{
    if (j.is_boolean()) return j.get<bool>();

    if (j.is_string())
    {
        auto str = to_lower(j.get<std::string>());
        if (str == "true") return true;
    }

    return false;
}

// Read applicaton configuration.
auto read_config(app_context_t& app) -> void
{
    try
    {
        std::ifstream i(config_file_name);
        json j;
        i >> j;

        app.user_id = j["user_id"];
        app.password = j["password"];
        app.server = j["server"];
        app.dx_api_path = j["dx_api_path"];
        app.token_endpoint = j["token_endpoint"];
        app.client_id = j["client_id"];
        app.client_secret = j["client_secret"];
        app.font_index = j["font_index"];
    }
    catch (const std::exception& e)
    {
        SDL_Log("Error reading settings file: %s", config_file_name);
        SDL_Log("Reason: %s", e.what());
    }
}

// Write application configuration.
auto write_config(app_context_t& app) -> void
{
    std::ofstream o(config_file_name);
    json j;
    j["user_id"] = app.user_id;
    j["password"] = app.password;
    j["server"] = app.server;
    j["dx_api_path"] = app.dx_api_path;
    j["token_endpoint"] = app.token_endpoint;
    j["client_id"] = app.client_id;
    j["client_secret"] = app.client_secret;
    j["font_index"] = app.font_index;
    o << std::setw(json_indent) << j << std::endl;
}

///////////////////////////////////////////////////////////////////////////////
#pragma endregion

#pragma region DX API functions:
///////////////////////////////////////////////////////////////////////////////

// Maps component type to string.
auto to_c_str(component_type_t type) -> const char*
{
    return component_type_strings[type];
}

// Returns the component type which corresponds to the provided string.
auto to_component_type(std::string_view str) -> component_type_t
{
    return to_enum<component_type_t>(str, component_type_strings);
}

// Produces a debug string for the component type and name.
auto to_string(component_type_t type, std::string_view name) -> std::string
{
    return std::format("{}: {}", to_c_str(type), name);
}

// Produces a debug string for a component type, name, and referenced component type.
auto to_string(component_type_t type, std::string_view name, component_type_t ref_type) -> std::string
{
    return std::format("{}: {} [{}]", to_c_str(type), name, to_c_str(ref_type));
}

// Returns true if the provided field should be rendered with an editable interface.
auto is_editable(const component_t& component, const field_t& field) -> bool
{
    if (component.is_readonly)  return false;
    if (component.is_disabled)  return false;
    if (field.is_special)       return false;
    if (field.is_class_key)     return false;

    return true;
}

// Creates a key from a class id and a name such as: "The-Class-ID.TheName"
auto make_key(std::string_view class_id, std::string_view name) -> std::string
{
    return std::format("{}.{}", class_id, name);
}

// Takes a DX API label property like "@L Blah", "@FL .BlahBlah", or "Blah Blah Blah"
// and returns correct response:
//  "@L Blah"           -> "Blah".
//  "@FL .BlahBlah"     -> The value of the label property for field .BlahBlah.
//  "Blah Blah Blah"    -> "Blah Blah Blah"
auto resolve_label(std::string_view raw_label, const field_map_t& fields, std::string_view class_id) -> std::string
{
    assert(!class_id.empty());
    std::string result{ raw_label };

    if (!raw_label.empty())
    {
        if (raw_label[0] == '@')
        {
            if (raw_label[1] == 'L')
            {
                // "@L Blah"
                //  0123456
                //     ^
                //     Start here.
                result = raw_label.substr(3);
            }
            else if (raw_label[1] == 'F' && raw_label[2] == 'L')
            {
                // "@FL .Blah"
                //  012345678
                //       ^
                //       Start here.
                auto field_id = raw_label.substr(5);
                auto field_key = make_key(class_id, field_id);
                result = fields.at(field_key).label;
            }
        }
    }

    return result;
};

// Returns the value of the provided name in the content map. In strict mode, throws an exception if the name can't be
// found or the content doesn't match.
auto get_content(const content_map_t& content, std::string_view class_id, const char* name, bool strict_mode = true) -> std::string
{
    std::string result;
    if (content.contains("classID"))
    {
        if (content.at("classID") == class_id)
        {
            if (content.contains(name))
            {
                result = content.at(name);
            }
            else if (strict_mode)
            {
                std::string e = std::format("Could not resolve name: {}\nName not found in content.", name);
                throw std::runtime_error(e);
            }
        }
        else if (strict_mode)
        {
            std::string e = std::format("Could not resolve name: {}\ncontent['classID'] = {}\nclass_id = {}", name, content.at("classID"), class_id);
            throw std::runtime_error(e);
        }
    }
    else if (strict_mode)
    {
        std::string e = std::format("Could not resolve name: {}\ncontent does not contain 'classID'", name);
        throw std::runtime_error(e);
    }
    return result;
}

// Takes a DX API name property like "@P .Blah" or "Blah Blah Blah" and returns correct response:
//  "@P .Blah"  -> "Blah" if dereferencing isn't required, otherwise the value of the content for "Blah".
//  "Blah Blah" -> "Blah Blah"
auto resolve_name(std::string_view raw_name, const content_map_t& content, std::string_view class_id, bool dereference_property_name = true) -> std::string
{
    assert(!class_id.empty());
    std::string result{ raw_name };

    if (raw_name.length() >= 2 && raw_name.substr(0, 2) == "@P")
    {
        // "@P .Blah"
        //  01234567
        //      ^
        //      Start here.
        std::string name{ raw_name.substr(4) };
        if (dereference_property_name)
        {
            result = get_content(content, class_id, name.c_str());
        }
        else
        {
            result = name;
        }
    }

    return result;
}

// Recursively validates that component and all of its children are in a valid
// state for submission. Only applies to field components.
auto validate_component_r(const component_t& component, const component_map_t& components, const field_map_t& fields) -> bool
{
    bool is_valid = true;

    switch (component.type)
    {
    case component_type_text_input:
    case component_type_text_area:
    {
        if (component.is_required)
        {
            const auto& field = fields.at(component.key);
            if (field.data.empty())
            {
                is_valid = false;
            }
        }
    } break;
    }

    // Process children.
    if (!component.children.empty())
    {
        for (auto& child : component.children)
        {
            if (!is_valid) break;
            is_valid = validate_component_r(child, components, fields);
        }
    }

    return is_valid;
}

// Recursively makes component and its children from DX API JSON response data.
auto make_component_r(const json& component_json, app_context_t& app, std::string_view parent_class_id = "") -> component_t
{
    component_t new_component;
    new_component.json = component_json.dump(json_indent);
    new_component.type = to_component_type(component_json["type"]);

    switch (new_component.type)
    {
        case component_type_unknown:
        {
            new_component.class_id = parent_class_id;
            // ...always first.

            new_component.name = component_json["type"];

            // Always last:
            new_component.debug_string = to_string(new_component.type, new_component.name);
        } break;
        case component_type_reference:
        {
            new_component.class_id = parent_class_id;
            // ...always first.

            auto& config_json = component_json["config"];
            new_component.name = resolve_name(config_json["name"], app.case_info.content, new_component.class_id);
            new_component.ref_type = to_component_type(config_json["type"]);

            // References might specify a context. If that context exists, we use it if we support it. If it exists
            // and we don't support it, we mark this reference as broken.
            if (config_json.contains("context"))
            {
                std::string context = config_json["context"];
                
                if (context.substr(0, 6) == "@CLASS")
                {
                    // "@CLASS The-Class-Name"
                    //  0123456789...
                    //         ^
                    //         Start here.
                    new_component.class_id = context.substr(7);
                }
                else
                {
                    new_component.is_broken = true;
                    new_component.broken_string = std::format("Unsupported context: {}", context);
                }
            }

            // Always last:
            new_component.debug_string = to_string(new_component.type, new_component.name, new_component.ref_type);
        } break;
        case component_type_region:
        {
            new_component.class_id = parent_class_id;
            // ...always first.

            new_component.name = resolve_name(component_json["name"], app.case_info.content, new_component.class_id);

            // Always last:
            new_component.debug_string = to_string(new_component.type, new_component.name);
        } break;
        case component_type_view:
        {
            new_component.class_id = component_json["classID"];
            // ...always first.

            new_component.name = resolve_name(component_json["name"], app.case_info.content, new_component.class_id);

            // Views usually, but not always, specify a template in the config.
            auto& config_json = component_json["config"];
            if (config_json.contains("template")) new_component.ref_type = to_component_type(config_json["template"]);

            // Always last:
            new_component.debug_string = to_string(new_component.type, new_component.name, new_component.ref_type);
        } break;
        case component_type_text_area:
        case component_type_text_input:
        {
            new_component.class_id = parent_class_id;
            // ...always first.

            auto& config_json = component_json["config"];
            new_component.name = resolve_name(config_json["value"], app.case_info.content, new_component.class_id, false);
            new_component.label = resolve_label(config_json["label"], app.resources.fields, new_component.class_id);

            // Check for optional attributes.
            if (config_json.contains("disabled")) new_component.is_disabled = to_bool(config_json["disabled"]);
            if (config_json.contains("readOnly")) new_component.is_readonly = to_bool(config_json["readOnly"]);
            if (config_json.contains("required")) new_component.is_required = to_bool(config_json["required"]);

            // Always last:
            new_component.debug_string = to_string(new_component.type, new_component.label);
        } break;
    }

    // Validate the component and finalize it.
    if (   new_component.name.empty()
        || new_component.class_id.empty()
        || new_component.type == component_type_unspecified)
    {
        std::string error_message = std::format("Failed to make component from JSON:\n{}", new_component.json);
        throw std::runtime_error(error_message);
    }
    else
    {
        new_component.key = make_key(new_component.class_id, new_component.name);
    }

    // Process children:
    if (component_json.contains("children"))
    {
        for (const auto& child : component_json["children"])
        {
            //component_t new_child_component = make_component_r(child, app, parent_class_id);
            component_t new_child_component = make_component_r(child, app, new_component.class_id);
            new_component.children.push_back(new_child_component);
        }
    }

    return new_component;
};

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e350
auto parse_dx_response(app_context_t& app, std::string_view response_body) -> void
{
    auto j = json::parse(response_body);
    auto& info = j["data"]["caseInfo"];

    // Case info.
    {
        app.case_info.id = info["ID"];
        app.case_info.business_id = info["businessID"];
        app.case_info.type.id = info["caseTypeID"];
        app.case_info.type.name = info["caseTypeName"];
        app.case_info.create_time = info["createTime"];
        app.case_info.created_by = info["createdBy"];
        app.case_info.last_update_time = info["lastUpdateTime"];
        app.case_info.last_updated_by = info["lastUpdatedBy"];
        app.case_info.name = info["name"];
        app.case_info.owner = info["owner"];
        app.case_info.status = info["status"];

        app.case_info.assignments.clear();
        for (const auto& assignment : info["assignments"])
        {
            assignment_t new_assignment;
            new_assignment.id = assignment["ID"];
            new_assignment.name = assignment["name"];
            new_assignment.can_perform = to_bool(assignment["canPerform"]);

            for (const auto& action : assignment["actions"])
            {
                action_t new_action;
                new_action.id = action["ID"];
                new_action.name = action["name"];
                new_action.type = action["type"];

                new_assignment.actions[new_action.id] = new_action;
            }

            app.case_info.assignments[new_assignment.id] = new_assignment;
        }

        app.case_info.content.clear();
        for (const auto& content : info["content"].items())
        {
            std::string k = content.key();
            const auto& content_value = content.value();

            std::string v;
            if      (content_value.is_string())         v = content_value.get<std::string>();
            else if (content_value.is_number_integer()) v = std::to_string(content_value.get<int>());
            else if (content_value.is_number_float())   v = std::to_string(content_value.get<float>());
            else if (content_value.is_boolean())        v = std::to_string(content_value.get<bool>());

            app.case_info.content[k] = v;
        }
    } // End of case info parsing.

    // UI resources.
    if (j.contains("uiResources"))
    {
        auto& ui_resources_json = j["uiResources"];
        auto& resources_json = ui_resources_json["resources"];
        auto& fields_json = resources_json["fields"];
        auto& views_json = resources_json["views"];

        // Fields:
        {
            app.resources.fields.clear();
            for (const auto& field_array : fields_json.items())
            {
                for (const auto& field : field_array.value().items())
                {
                    auto& value = field.value();

                    field_t new_field;
                    new_field.id = field_array.key();
                    new_field.json = value.dump(json_indent);
                    new_field.type = value["type"];

                    // Sometimes we get an "Unknown" type which is completely malformed,
                    // in which case we just skip it entirely.
                    if (to_lower(new_field.type) == "unknown") continue;

                    new_field.class_id = value["classID"];
                    new_field.label = value["label"];

                    if (value.contains("isSpecial")) new_field.is_special       = to_bool(value["isSpecial"]);
                    if (value.contains("isClassKey")) new_field.is_class_key    = to_bool(value["isClassKey"]);

                    new_field.data = get_content(app.case_info.content, new_field.class_id, new_field.id.c_str(), false);

                    auto new_field_key = make_key(new_field.class_id, new_field.id);
                    app.resources.fields[new_field_key] = new_field;
                }
            }
        }

        // Views (components):
        {
            app.resources.components.clear();
            for (const auto& view_array : views_json.items())
            {
                for (const auto& view : view_array.value().items())
                {
                    const auto& value = view.value();
                    component_t new_component = make_component_r(value, app);
                    app.resources.components[new_component.key] = new_component;
                }
            }
        }

        // Root:
        {
            const auto& config_json = ui_resources_json["root"]["config"];
            std::string context = config_json["context"];

            if (context == "caseInfo.content")
            {
                std::string name = config_json["name"];
                std::string type = config_json["type"];

                if (type == "view")
                {
                    std::string& class_id = app.case_info.content.at("classID");
                    app.root_component_key = make_key(class_id, name);
                }
                else
                {
                    std::string e = std::format("Root component uses unsupported type: {}", type);
                    throw std::runtime_error(e);
                }
            }
            else
            {
                std::string e = std::format("Root component uses unsupported context: ", context);
                throw std::runtime_error(e);
            }
        }
    } // End of UI resources parsing.
}

///////////////////////////////////////////////////////////////////////////////
#pragma endregion

#pragma region Network functions:
///////////////////////////////////////////////////////////////////////////////

// Helper to convert httplib headers into a human-friendly string.
auto to_string(const httplib::Headers& headers) -> std::string
{
    std::string result;

    for (auto& header : headers)
    {
        result += std::format("{}: {}\n", header.first, header.second);
    }

    return result;
}

// Returns an httplib client with the most common initialization and an endpoint built from the provided arg strings.
template<typename... strings>
auto init_call_and_create_client(net_call_t& call, http_method_t method, strings... args) -> httplib::Client
{
    httplib::Client client(call.server);
    client.set_bearer_token_auth(call.access_token);
    call.method = http_method_strings[method];
    std::vector<std::string_view> params = { args... };

    call.endpoint = call.dx_api_path;
    for (std::string_view sv : params)
    {
        call.endpoint += sv;
    }

    return client;
}

// Maps an httplib result into the output params of a dx call struct.
auto set_call_output(net_call_t& call, const httplib::Result& result) -> void
{
    auto err = result.error();
    if (err != httplib::Error::Success)
    {
        call.error_message = httplib::to_string(err);
        return;
    }

    call.etag = result->get_header_value("eTag");
    call.response_headers = to_string(result->headers);
    call.response_body = result->body;
    call.succeeded = true;
}

// Should be called from the network thread. Executes the specified network call and stores the response.
auto handle_request(net_call_t& call) -> void
{
    switch (call.type)
    {
    case net_call_type_t::login:
    {
        httplib::Client cli(call.server);
        cli.set_basic_auth(call.client_id, call.client_secret);
        call.method = "POST";

        httplib::Headers request_headers =
        {
            { "Accept", "application/json" }
        };
        call.request_headers = to_string(request_headers);

        call.request_body = std::format("grant_type=password&username={}&password={}", call.user_id, call.password);

        auto result = cli.Post(call.endpoint, request_headers, call.request_body, "application/x-www-form-urlencoded");
        auto err = result.error();
        if (err != httplib::Error::Success)
        {
            call.error_message = httplib::to_string(err);
            return;
        }

        call.response_headers = to_string(result->headers);
        call.response_body = result->body;
        call.succeeded = true;
    } break;
    case net_call_type_t::refresh_case_types:
    {
        auto client = init_call_and_create_client(call, http_method_get, "/casetypes");
        auto result = client.Get(call.endpoint);
        set_call_output(call, result);
    } break;
    case net_call_type_t::create_case:
    {
        auto client = init_call_and_create_client(call, http_method_post, "/cases");
        json request_body_json =
        {
            { "caseTypeID", call.work_type_id }
        };
        call.request_body = request_body_json.dump(json_indent);
        auto result = client.Post(call.endpoint, call.request_body, "application/json");
        set_call_output(call, result);
    } break;
    case net_call_type_t::open_assignment:
    {
        auto client = init_call_and_create_client(call, http_method_get, "/assignments/", call.id1);
        auto result = client.Get(call.endpoint);
        set_call_output(call, result);
    } break;
    case net_call_type_t::open_assignment_action:
    {
        auto client = init_call_and_create_client(call, http_method_get, "/assignments/", call.id1, "/actions/", call.id2);
        auto result = client.Get(call.endpoint);
        set_call_output(call, result);
    } break;
    case net_call_type_t::submit_assignment_action:
    {
        auto client = init_call_and_create_client(call, http_method_patch, "/assignments/", call.id1, "/actions/", call.id2);
        httplib::Headers request_headers =
        {
            { "if-match", call.etag }
        };
        call.request_headers = to_string(request_headers);
        auto result = client.Patch(call.endpoint, request_headers, call.request_body, "application/json");
        set_call_output(call, result);
    } break;
    }
}

// Should be called from the main thread. Processes a response from a network call and maps the response into
// the application context.
auto handle_response(net_call_t& call, app_context_t& app) -> void
{
    app.flash = "";
    app.endpoint = call.method + " - " + app.server + call.endpoint;
    app.request_headers = call.request_headers;
    app.response_headers = call.response_headers;
    app.request_body = "";
    app.response_body = "";
    
    // Format JSON bodies if applicable.
    // Not super efficient, but not happening in a tight inner loop so how much does it matter?
    app.request_body = call.request_body;
    try
    {
        if (!call.response_body.empty())
        {
            // Parsing the result twice (here and below) makes me twitch. But in practice,
            // there is no perceptible performance impact, and this is for debugging anyway.
            // We could disable this (and all debugging facilities) for a release build if
            // the need ever arose.
            app.response_body = json::parse(call.response_body).dump(json_indent);
        }
    }
    catch (const std::exception& e)
    {
        IM_UNUSED(e);
        app.response_body = call.response_body;
    }

    if (!call.succeeded)
    {
        app.flash = call.error_message;
        return;
    }

    switch (call.type)
    {
    case net_call_type_t::login:
    {
        app.access_token = call.access_token;
        try
        {
            json j = json::parse(call.response_body);
            if (j.contains("access_token"))
            {
                app.access_token = j["access_token"];
                app.status = app_status_t::logged_in;
            }
            else
            {
                throw std::runtime_error("No access token received.");
            }
        }
        catch (const std::exception& ex)
        {
            app.flash = app.flash + "Failed to login: " + ex.what();
        }
    } break;
    case net_call_type_t::refresh_case_types:
    {
        app.case_types.clear();
        try
        {
            json j = json::parse(call.response_body);
            if (j.contains("applicationIsConstellationCompatible") &&
                j["applicationIsConstellationCompatible"].get<bool>() &&
                j.contains("caseTypes"))
            {
                for (auto& item : j["caseTypes"].items())
                {
                    auto& case_type_json = item.value();
                    case_type_t case_type;
                    case_type.id = case_type_json["ID"].get<std::string>();
                    case_type.name = case_type_json["name"].get<std::string>();
                    app.case_types.push_back(case_type);
                }
            }
            else
            {
                app.flash = "Not constellation compatible and/or no case types defined.";
            }
        }
        catch (const std::exception& e)
        {
            app.flash = std::format("Failed to refresh cases: {}", e.what());
        }
    } break;
    case net_call_type_t::create_case:
    {
        try
        {
            parse_dx_response(app, call.response_body);
            app.status = app_status_t::open_case;
        }
        catch (const std::exception& e)
        {
            app.flash = std::format("Failed to create case: {}", e.what());
        }
    } break;
    case net_call_type_t::open_assignment:
    {
        try
        {
            parse_dx_response(app, call.response_body);
            app.etag = call.etag;
            app.open_assignment_id = call.id1;
            app.status = app_status_t::open_assignment;
        }
        catch (const std::exception& e)
        {
            app.flash = std::format("Failed to open assignment: {}", e.what());
        }
    } break;
    case net_call_type_t::open_assignment_action:
    {
        try
        {
            parse_dx_response(app, call.response_body);
            app.etag = call.etag;
            app.open_assignment_id = call.id1;
            app.open_action_id = call.id2;
            app.status = app_status_t::open_action;
        }
        catch (const std::exception& e)
        {
            app.flash = std::format("Failed to open assignment action: {}", + e.what());
        }
    } break;
    case net_call_type_t::submit_assignment_action:
    {
        try
        {
            parse_dx_response(app, call.response_body);
            app.open_assignment_id = call.id1;
            app.status = app_status_t::open_case;
        }
        catch (const std::exception& e)
        {
            app.flash = std::format("Failed to submit assignment action: {}", e.what());
        }
    } break;
    }
}

// Returns a net call struct with the most common initialization.
auto make_net_call(const app_context_t& app, const net_call_type_t type) -> net_call_t
{
    net_call_t call;
    call.type = type;
    call.server = app.server;
    call.access_token = app.access_token;
    call.dx_api_path = app.dx_api_path;

    return call;
}

// Pushes a network call to login to the Pega instance.
auto login(app_context_t& app) -> void
{
    auto call = make_net_call(app, net_call_type_t::login);
    call.client_id = app.client_id;
    call.client_secret = app.client_secret;
    call.user_id = app.user_id;
    call.password = app.password;
    call.endpoint = app.token_endpoint;

    std::scoped_lock lock(app.dx_request_mutex);
    app.dx_request_queue.push(call);
}

// Pushes a network call to refresh the case types defined in the Pega app.
auto refresh_case_types(app_context_t& app) -> void
{
    auto call = make_net_call(app, net_call_type_t::refresh_case_types);
    std::scoped_lock lock(app.dx_request_mutex);
    app.dx_request_queue.push(call);
}

// Pushes a network call to create a new case of the specified type.
auto create_case(app_context_t& app, std::string_view work_type_id) -> void
{
    auto call = make_net_call(app, net_call_type_t::create_case);
    call.work_type_id = work_type_id;

    std::scoped_lock lock(app.dx_request_mutex);
    app.dx_request_queue.push(call);
}

// Pushes a network call to open the specified assignment.
auto open_assignment(app_context_t& app, std::string_view assignment_id) -> void
{
    auto call = make_net_call(app, net_call_type_t::open_assignment);
    call.id1 = assignment_id;
    std::scoped_lock lock(app.dx_request_mutex);
    app.dx_request_queue.push(call);
}

// Pushes a network call to open the specified action.
auto open_assignment_action(app_context_t& app, std::string_view action_id) -> void
{
    auto call = make_net_call(app, net_call_type_t::open_assignment_action);
    call.id1 = app.open_assignment_id;
    call.id2 = action_id;
    std::scoped_lock lock(app.dx_request_mutex);
    app.dx_request_queue.push(call);
}

// Pushes a network call to submit the currently open assignment action. Assumes that
// validation has already passed.
auto submit_open_assignment_action(app_context_t& app) -> void
{
    auto call = make_net_call(app, net_call_type_t::submit_assignment_action);

    json content;
    bool have_content = false;
    for (const auto& field_pair : app.resources.fields)
    {
        const auto& field = field_pair.second;
        if (field.is_special || field.is_class_key) continue;
        if (field.is_dirty)
        {
            content[field.id] = field.data;
            have_content = true;
        }
    }

    json body;
    body["content"] = content;

    call.id1 = app.open_assignment_id;
    call.id2 = app.open_action_id;
    call.etag = app.etag;

    if (have_content)
    {
        call.request_body = body.dump(json_indent);
    }

    std::scoped_lock lock(app.dx_request_mutex);
    app.dx_request_queue.push(call);
}

///////////////////////////////////////////////////////////////////////////////
#pragma endregion

#pragma region Draw functions:
///////////////////////////////////////////////////////////////////////////////

// Recursively draws debug component information.
auto draw_component_debug_r(component_t& component, component_map_t& component_map, std::string& component_debug_json) -> void
{
    ImGui::TreePush(&component);
    ImGui::Text(component.debug_string.c_str());
    if (ImGui::IsItemClicked())
    {
        component_debug_json = component.json;
    }

    if (component.is_broken)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "(!)");
        ImGui::SetItemTooltip(component.broken_string.c_str());
    }
    else if (component.type == component_type_reference)
    {
        auto& reference = component_map.at(component.key);
        draw_component_debug_r(reference, component_map, component_debug_json);
    }

    if (!component.children.empty())
    {
        for (auto& child : component.children)
        {
            draw_component_debug_r(child, component_map, component_debug_json);
        }
    }

    ImGui::TreePop();
}

// Recursively draws components.
auto draw_component_r(const component_t& component, resources_t& resources, int& id) -> void
{
    ImGui::PushID(id++);
    switch (component.type)
    {
        case component_type_reference:
        {
            if (!component.is_broken)
            {
                auto& reference = resources.components.at(component.key);
                draw_component_r(reference, resources, id);
            }
        } break;
        case component_type_text_area:
        case component_type_text_input:
        {
            auto& field = resources.fields.at(component.key);
            if (is_editable(component, field))
            {
                field.is_dirty = true;

                if (component.type == component_type_text_area)
                {
                    ImGui::InputTextMultiline(component.label.c_str(), &field.data);
                }
                else
                {
                    ImGui::InputText(component.label.c_str(), &field.data);
                }

                if (component.is_required)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "*");
                }
            }
            else
            {
                ImGui::LabelText(component.label.c_str(), field.data.c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            ImGui::SetItemTooltip(component.key.c_str());
        } break;
    }
    ImGui::PopID();

    // If this is a view with an unsupported template, bail.
    if (component.type == component_type_view)
    {
        if (component.ref_type == component_type_unspecified) return;
        if (component.ref_type == component_type_unknown) return;
    }

    // Process children.
    if (!component.children.empty())
    {
        for (auto& child : component.children)
        {
            draw_component_r(child, resources, id);
        }
    }
}

// Draws a spinner/throbber/whatchamacallit to indicate waiting for an action to complete.
auto draw_spinner() -> void
{
    static std::string spinner = "|/-\\";
    static int index = 0;
    static auto last_ticks = get_ticks();
        
    ImGui::Text("Loading %c", spinner[index]);

    auto current_ticks = get_ticks();
    auto delta_ticks = current_ticks - last_ticks;
    if (delta_ticks > spinner_period_ticks)
    {
        if (++index >= spinner.length()) index = 0;
        last_ticks = current_ticks;
    }
}

// Draws the main menu.
auto draw_main_menu(app_context_t& app) -> void
{
    if (ImGui::BeginMenuBar())
    {
        // Logged-in menu
        if (app.status != app_status_t::logged_out)
        {
            // User menu
            if (ImGui::BeginMenu(app.user_id.c_str()))
            {
                if (ImGui::MenuItem("Logout"))
                {
                    app.case_types.clear();
                    app.status = app_status_t::logged_out;
                }
                ImGui::EndMenu();
            }

            // Create menu
            if (ImGui::BeginMenu("Create"))
            {
                if (ImGui::MenuItem("Refresh Case Types"))
                {
                    refresh_case_types(app);
                }

                // Case types
                if (!app.case_types.empty())
                {
                    ImGui::Separator();
                    for (auto& work_type : app.case_types)
                    {
                        if (ImGui::MenuItem(work_type.name.c_str()))
                        {
                            create_case(app, work_type.id);
                        }
                        ImGui::SetItemTooltip(work_type.id.c_str());
                    }
                }

                ImGui::EndMenu();
            }
        }

        // View menu
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Show debug window", nullptr, &app.show_debug_window);
            ImGui::MenuItem("Show Dear ImGui demo", nullptr, &app.show_demo_window);
            if (ImGui::BeginMenu("Font size"))
            {
                for (int i = 0; i < font_sizes.size(); ++i)
                {
                    auto& io = ImGui::GetIO();
                    bool selected = (io.FontDefault->FontSize == font_sizes[i].first);

                    if (ImGui::MenuItem(font_sizes[i].second, nullptr, &selected))
                    {
                        app.font_index = i;
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

// Draws the login form.
auto draw_login_form(app_context_t& app) -> void
{
    ImGui::InputText("Server", &app.server);
    ImGui::InputText("DX API Path", &app.dx_api_path);
    ImGui::InputText("Token Endpoint", &app.token_endpoint);
    ImGui::InputText("Client ID", &app.client_id);
    ImGui::InputText("Client Secret", &app.client_secret);
    ImGui::InputText("User ID", &app.user_id);
    ImGui::InputText("Password", &app.password);

    if (ImGui::Button("Login"))
    {
        login(app);
    }
}

// Draws the currently open case.
auto draw_open_case(app_context_t& app) -> void
{
    auto& work = app.case_info;

    if (ImGui::CollapsingHeader("Case", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SeparatorText("Info");
        ImGui::LabelText("Case ID", work.business_id.c_str());
        ImGui::SetItemTooltip(work.id.c_str());
        ImGui::LabelText("Name", work.name.c_str());
        ImGui::SetItemTooltip("%s: %s", work.type.id.c_str(), work.type.name.c_str());
        ImGui::LabelText("Status", work.status.c_str());
        ImGui::LabelText("Owner", work.owner.c_str());
        ImGui::LabelText("Created on", work.create_time.c_str());
        ImGui::LabelText("Created by", work.created_by.c_str());
        ImGui::LabelText("Updated on", work.last_update_time.c_str());
        ImGui::LabelText("Updated by", work.last_updated_by.c_str());

        if (!work.assignments.empty())
        {
            ImGui::SeparatorText("Assignments");
            for (auto& assignment_pair : work.assignments)
            {
                auto& assignment = assignment_pair.second;

                ImGui::PushID(&assignment);
                if (assignment.can_perform)
                {
                    if (ImGui::Button(assignment.name.c_str()))
                    {
                        open_assignment(app, assignment.id);
                    }
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, { 1, 0, 0, 1 });
                    ImGui::Button(assignment.name.c_str());
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("You cannot perform this assignment.");
                }
                ImGui::PopID();
            }
        }
    }
}

// Draws the currently open assignment.
auto draw_open_assignment(app_context_t& app)
{
    assert(!app.open_assignment_id.empty());
    auto& assignment = app.case_info.assignments[app.open_assignment_id];

    if (ImGui::CollapsingHeader("Assignment", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SeparatorText("Info");
        ImGui::LabelText("Name", assignment.name.c_str());

        ImGui::SeparatorText("Actions");
        for (auto& pair : assignment.actions)
        {
            auto& action = pair.second;
            ImGui::PushID(&action);
            if (ImGui::Button(action.name.c_str()))
            {
                open_assignment_action(app, action.id);
            }
            ImGui::PopID();
        }
    }
}

// Draws the currently open assignment action.
auto draw_open_assignment_action(app_context_t& app) -> void
{
    assert(!app.open_assignment_id.empty());
    assert(!app.open_action_id.empty());
    auto& action = app.case_info.assignments[app.open_assignment_id].actions[app.open_action_id];

    if (ImGui::CollapsingHeader("Action", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SeparatorText("Info");
        ImGui::LabelText("Name", action.name.c_str());

        ImGui::SeparatorText("View");
        int component_id = 0;
        draw_component_r(app.resources.components[app.root_component_key], app.resources, component_id);

        if (ImGui::Button("Submit"))
        {
            const auto& root_component = app.resources.components[app.root_component_key];
            auto are_components_valid = validate_component_r(root_component, app.resources.components, app.resources.fields);
            if (are_components_valid)
            {
                submit_open_assignment_action(app);
            }
            else
            {
                app.flash = "Validation failed. Did you fill out all required fields?";
            }
        }
    }
}

// Draws the main user interface.
auto draw_main_window(app_context_t& app) -> void
{
    static bool first_call = true;

    if (first_call)
    {
        first_call = false;

        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        auto font_size = ImGui::GetFontSize();
        ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x + font_size, main_viewport->WorkPos.y + font_size), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(main_viewport->WorkSize.x / 2.0f - font_size*1.5f, main_viewport->WorkSize.y - font_size * 2.0f), ImGuiCond_FirstUseEver);
    }

    ImGui::Begin("Main", nullptr, ImGuiWindowFlags_MenuBar);

    // If we have net ops pending, make a note and show a spinner. We
    // do things this way to minimize the amount of time we're 
    // holding the queue mutex, and to not have to remember to call
    // ImGui::End() in multiple places.
    bool have_pending_requests = false;
    {
        std::scoped_lock lock(app.dx_request_mutex);
        if (!app.dx_request_queue.empty())
        {
            have_pending_requests = true;
            draw_spinner();
        }
    }

    // Don't render any more interactive UI if we have a pending network
    // operation. That means  UI interaction is effectively blocked by net ops, no
    // different from if we ran on a single thread. But we can at least render
    // a spinner/progress bar/something better than just freezing up.
    // 
    // Even if we were single threaded, we would still want to work in this manner —
    // queue up network operations to take some action and refresh our cache,
    // then render the updated cache — and its not much harder to do that in multithreaded
    // way.
    if (!have_pending_requests)
    {
        draw_main_menu(app);

        // Show login form.
        if (app.status == app_status_t::logged_out)
        {
            draw_login_form(app);
        }

        // Show open work object.
        else if (app.status == app_status_t::open_case || app.status == app_status_t::open_assignment || app.status == app_status_t::open_action)
        {
            draw_open_case(app);

            // Show open assignment.
            if (app.status == app_status_t::open_assignment || app.status == app_status_t::open_action)
            {
                draw_open_assignment(app);
            }

            // Show open action.
            if (app.status == app_status_t::open_action)
            {
                draw_open_assignment_action(app);
            }
        }
    }

    ImGui::End();
}

// Draws information about network calls and responses.
auto draw_debug_calls(app_context_t& app) -> void
{
    auto font_size = ImGui::GetFontSize();

    ImGui::PushItemWidth(font_size * -10); // In practice this seems to work out to a little less than 20 characters of space for labels.

    ImGui::InputText("Endpoint", &app.endpoint, ImGuiInputTextFlags_ReadOnly);

    ImGui::InputTextMultiline("Request headers", &app.request_headers, ImVec2(0, 3 * font_size), ImGuiInputTextFlags_ReadOnly);
    ImGui::InputTextMultiline("Request body", &app.request_body, ImVec2(0, 5 * font_size), ImGuiInputTextFlags_ReadOnly);
    ImGui::InputTextMultiline("Response headers", &app.response_headers, ImVec2(0, 10 * font_size), ImGuiInputTextFlags_ReadOnly);
    ImGui::InputTextMultiline("Response body", &app.response_body, ImVec2(0, 20 * font_size), ImGuiInputTextFlags_ReadOnly);

    ImGui::PopItemWidth();
}

// Draws a tree view of components in use starting with root.
auto draw_debug_components(app_context_t& app) -> void
{
    auto font_size = ImGui::GetFontSize();

    ImGui::BeginGroup();
    draw_component_debug_r(app.resources.components[app.root_component_key], app.resources.components, app.component_debug_json);
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::InputTextMultiline("##ComponentJSON", &app.component_debug_json, ImVec2(-font_size, -font_size), ImGuiInputTextFlags_ReadOnly);
}

// Draws fields currently in use.
auto draw_debug_fields(app_context_t& app) -> void
{
    auto font_size = ImGui::GetFontSize();

    ImGui::BeginGroup();
    for (const auto& field : app.resources.fields)
    {
        ImGui::Text(field.first.c_str());
        if (ImGui::IsItemClicked())
        {
            app.field_debug_json = field.second.json;
        }
    }
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::InputTextMultiline("##FieldJSON", &app.field_debug_json, ImVec2(-font_size, -font_size), ImGuiInputTextFlags_ReadOnly);
}

// Draws content currently in use.
auto draw_debug_content(app_context_t& app) -> void
{
    for (const auto& content : app.case_info.content)
    {
        ImGui::Text("%s: %s", content.first.c_str(), content.second.c_str());
    }
}

// Draws the debug user interface.
auto draw_debug_window(app_context_t& app) -> void
{
    static bool first_call = true;

    if (first_call)
    {
        first_call = false;

        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        auto font_size = ImGui::GetFontSize();
        float next_pos_x = main_viewport->WorkSize.x / 2.0f + font_size / 2.0f;
        ImGui::SetNextWindowPos(ImVec2(next_pos_x, main_viewport->WorkPos.y + font_size), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(main_viewport->WorkSize.x - next_pos_x - font_size, main_viewport->WorkSize.y - font_size*2.0f), ImGuiCond_FirstUseEver);
    }

    ImGui::Begin("Debug", &app.show_debug_window);

    if (ImGui::BeginTabBar("DebugTabBar"))
    {
        if (ImGui::BeginTabItem("Calls"))
        {
            draw_debug_calls(app);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Components"))
        {
            if (!app.resources.components.empty())
            {
                draw_debug_components(app);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Fields"))
        {
            if (!app.resources.fields.empty())
            {
                draw_debug_fields(app);
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Content"))
        {
            if (!app.case_info.content.empty())
            {
                draw_debug_content(app);
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}

// Draws a "modal" that displays the flash message with a button to clear it.
auto draw_flash_window(app_context_t& app) -> void
{
    static bool first_call = true;

    if (first_call)
    {
        first_call = false;

        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkSize.x * 0.4f, main_viewport->WorkSize.y * 0.4f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(main_viewport->WorkSize.x * 0.2f, main_viewport->WorkSize.y * 0.2f), ImGuiCond_FirstUseEver);
    }

    ImGui::Begin("Alert");
    ImGui::TextWrapped(app.flash.c_str());
    if (ImGui::Button("OK"))
    {
        app.flash.clear();
    }
    ImGui::End();
}

///////////////////////////////////////////////////////////////////////////////
#pragma endregion

#pragma region Main loop functions:
///////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////
#pragma endregion

} // anonymous namespace
} // application namespace

int main(int, char**)
{
    namespace dx = dx_api_explorer;
    auto app_context = std::make_unique<dx::app_context_t>();
    auto& app = *app_context;
    dx::read_config(app); // Todo: this seems to not require the dx:: prefix, why?

    int sdl_init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER);
    assert(sdl_init_result == 0);
    dx::scope_exit sdl_quit(&SDL_Quit);
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED);
    dx::unique_ptr_t<SDL_Window, &SDL_DestroyWindow> window_uptr
    (
        SDL_CreateWindow("DX API Explorer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags)
    );
    SDL_Window* window = window_uptr.get();
    assert(window);

    dx::unique_ptr_t<SDL_Renderer, &SDL_DestroyRenderer> renderer_uptr
    (
        SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED)
    );
    SDL_Renderer* renderer = renderer_uptr.get();
    assert(renderer);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    dx::scope_exit destroy_imgui_context([]() { ImGui::DestroyContext(); });
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigDebugIsDebuggerPresent = true;
    ImGui::StyleColorsLight();

    // If we didn't get a font size from our (potentially non-existant) config file, try
    // to select a reasonable default based on screen resolution as a proxy for DPI.
    // Todo: this mechanism recommends overly large fonts for big normal DPI monitors.
    if (app.font_index < 0)
    {
        int w, h;
        int err = SDL_GetRendererOutputSize(renderer, &w, &h);
        if (err != 0)
        {
            SDL_Log("Could not determine renderer output size: %s", SDL_GetError());
        }
        else
        {
            int calculated_index  = int(w * 1.0f / dx::hidpi_pixel_width_threshold);
            if (calculated_index < dx::font_sizes.size())
            {
                app.font_index = calculated_index;
            }

            SDL_Log("Renderer is %d pixels wide, using default font size of %f.", w, dx::font_sizes[app.font_index].first);
        }
        
    }

    for (const auto& font_size : dx::font_sizes)
    {
        io.Fonts->AddFontFromFileTTF(dx::font_file_name, font_size.first);
    }
    io.FontDefault = io.Fonts->Fonts[app.font_index];

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    dx::scope_exit shutdown_imgui_renderer([]()
    {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
    });

    std::thread network_thread(dx::network_thread_main_loop, std::ref(app));
    dx::app_thread_main_loop(app, window, renderer);
    network_thread.join();

    dx::write_config(app);
    return 0;
}