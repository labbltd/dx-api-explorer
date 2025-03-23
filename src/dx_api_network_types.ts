// https://developer.mozilla.org/en-US/docs/Web/HTTP/Methods
// Using a plain enum so it can index into an array without casting.
export enum http_method_t {
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
export const http_method_strings =
    [
        "Unspecified",
        "Unknown",
        "GET",
        "HEAD",
        "OPTIONS",
        "TRACE",
        "PUT",
        "DELETE",
        "POST",
        "PATCH",
        "CONNECT"
    ];

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/constellacion-dx-api-endponts.html
export enum net_call_type_t {
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
export class net_call_t {
    // Input:
    type = net_call_type_t.none;
    client_id!: string;
    client_secret!: string;
    dx_api_path!: string;
    id1!: string; // pzInsKey such as: "MYORG-MYCO-WORK-MYCASE C-123" or "ASSIGN-WORKLIST MYORG-MYCO-WORK-MYCASE C-123!MY_FLOW"
    id2!: string; // pyID such as: "MyFlowAction"
    password!: string;
    server!: string;
    user_id!: string;
    work_type_id!: string;

    // Input/Output:
    access_token!: string;
    endpoint!: string;

    // Output:
    succeeded = false; // Important that this is always initialized to false!
    method!: string;
    error_message!: string;
    etag!: string;
    request_headers = new Headers();
    request_body!: string;
    response_headers!: string;
    response_body!: string;
};
// using net_call_queue_t = std::queue<net_call_t>;
