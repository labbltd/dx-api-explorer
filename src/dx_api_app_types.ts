import { case_info_t, case_type_t, resources_t } from "./dx_api_model_types";
import { net_call_t } from "./dx_api_network_types";

// Used to indicate what information is available for display.
export enum app_status_t {
    logged_out,
    logged_in,
    open_case,
    open_assignment,
    open_action
};

export interface action_button {
    jsAction: string;
    name: string;
    actionID: string;
}

export type Datasource = string | PromptList | AssociatedList;

export interface PromptList {
    tableType?: 'PromptList';
    records?: { key: string, value: string }[];
}

export interface AssociatedList {
    name?: string;
    parameters: { [key: string]: string }
}

// Application state.
export class app_context_t {
    // Display data. //////////////////
    status = app_status_t.logged_out;
    show_debug_window = true;
    show_demo_window = false;
    font_index = -1;

    // General data. //////////////////
    access_token!: string;
    flash!: string; // Messages (usually errors) that should be highlighted to the user: string.
    endpoint!: string;
    request_headers!: string;
    request_body!: string;
    response_headers!: string;
    response_body!: string;
    server = '';
    dx_api_path = '/prweb/api/application/v2';
    oauth2 = {
        authorization_endpoint: '',
        token_endpoint: '',
        user_id: '',
        password: '',
        client_id: '',
        client_secret: '',
        grant_type: '',
        auth_service: 'pega',
        app_alias: '',
        no_pkce: '',
        redirect_uri: '/dist/authDone.html'
    }
    component_debug_json = "Click a component to display its JSON.\nThe format is:\n  Type: Name [Info]\n\nInfo varies by component:\n- Reference [Target Type]\n- View [Template]";
    field_debug_json = "Click a field to display its JSON.";

    // DX API response data. //////////
    case_types = new Array<case_type_t>();
    case_info = new case_info_t();
    resources = new resources_t();
    open_assignment_id!: string;
    open_action_id!: string;
    root_component_key!: string;
    etag!: string; // https://docs.pega.com/bundle/dx-api/page/platform/dx-api/building-constellation-dx-api-request.html

    // Threading data. ////////////////
    dx_request_queue = new Array<net_call_t>(); //net_call_queue_t;
    dx_request_mutex: any; //std::mutex
    dx_response_queue: any; //net_call_queue_t
    dx_response_mutex: any; //std::mutex
    shutdown_requested = false;
    action_buttons!: { main: action_button[], secondary: action_button[] };

    constructor() { this.shutdown_requested = false; }
};
