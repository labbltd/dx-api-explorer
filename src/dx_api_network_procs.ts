import { PegaAuth } from "@pega/auth";
import { app_context_t, app_status_t } from "./dx_api_app_types";
import { json_indent } from "./dx_api_constants";
import { parse_dx_response } from "./dx_api_model_procs";
import { case_type_t } from "./dx_api_model_types";
import { http_method_strings, http_method_t, net_call_t, net_call_type_t } from "./dx_api_network_types";

// Helper to convert httplib headers into a human-friendly string.
export function to_string(headers: Headers): string {
    let result = '';

    headers.forEach((value, key) => {
        result += `${key}: ${value.substring(0, 25) + (value.length > 25 ? '...' : '')}\n`;
    })

    return result;
}

// Returns an httplib client with the most common initialization and an endpoint built from the provided arg strings.
function init_call_and_create_client(call: net_call_t, method: http_method_t, ...args: string[]): Promise<Response> {
    call.endpoint = call.dx_api_path;
    call.method = http_method_strings[method];
    for (const sv of args) {
        call.endpoint += sv;
    }
    call.request_headers.set('authorization', `Bearer ${call.access_token}`);
    call.request_headers.set('accept', `application/json`);
    return fetch(call.server + call.endpoint, {
        method: http_method_strings[method],
        headers: call.request_headers,
        body: call.request_body
    });
}

// Maps an httplib result into the output params of a dx call struct.
async function set_call_output(call: net_call_t, result: Response): Promise<void> {

    if (!result.ok) {
        call.error_message = result.statusText;
        return;
    }

    call.etag = result.headers.get("eTag")!;
    call.response_headers = to_string(result.headers);
    call.response_body = await result.text();
    call.succeeded = true;
}

// Should be called from the network thread. Executes the specified network call and stores the response.
async function handle_request(call: net_call_t): Promise<void> {
    switch (call.type) {
        case net_call_type_t.login:
            {
                const request_headers = {
                    "Accept": "application/json",
                    "Content-Type": "application/x-www-form-urlencoded",
                    "Authorization": `Basic ${btoa(`${call.client_id}:${call.client_secret}`)}`
                }

                call.request_body = `grant_type=password&username=${call.user_id}&password=${call.password}`;

                const result = await fetch(call.server + call.endpoint, {
                    method: 'POST',
                    headers: request_headers,
                    body: call.request_body
                });
                if (!result.ok) {
                    call.error_message = result.statusText;
                    return;
                }

                call.response_headers = to_string(result.headers);
                call.response_body = await result.text();
                call.succeeded = true;
            } break;
        case net_call_type_t.refresh_case_types:
            {
                const client = init_call_and_create_client(call, http_method_t.http_method_get, "/casetypes");
                const result = await client;
                await set_call_output(call, result);
            } break;
        case net_call_type_t.create_case:
            {
                const request_body_json = { "caseTypeID": call.work_type_id };
                call.request_body = JSON.stringify(request_body_json, null, json_indent);
                const client = init_call_and_create_client(call, http_method_t.http_method_post, "/cases?viewType=page&pageName=pyEmbedAssignment");
                const result = await client;
                await set_call_output(call, result);
            } break;
        case net_call_type_t.open_assignment:
            {
                const client = init_call_and_create_client(call, http_method_t.http_method_get, "/assignments/", call.id1);
                const result = await client;
                await set_call_output(call, result);
            } break;
        case net_call_type_t.open_assignment_action:
            {
                const client = init_call_and_create_client(call, http_method_t.http_method_get, "/assignments/", call.id1, "/actions/", call.id2);
                const result = await client;
                await set_call_output(call, result);
            } break;
        case net_call_type_t.submit_assignment_action:
            {
                const request_headers = new Headers();
                request_headers.set("if-match", call.etag);
                request_headers.set("x-origin-channel", 'Web');
                call.request_headers = request_headers;
                const client = init_call_and_create_client(call, http_method_t.http_method_patch, "/assignments/", call.id1, "/actions/", call.id2);
                const result = await client;
                await set_call_output(call, result);
            } break;
    }
}

// Should be called from the main thread. Processes a response from a network call and maps the response into
// the application context.
function handle_response(call: net_call_t, app: app_context_t): void {
    app.flash = "";
    app.endpoint = call.method + " - " + app.server + call.endpoint;
    app.request_headers = to_string(call.request_headers);
    app.response_headers = call.response_headers;
    app.request_body = "";
    app.response_body = "";

    // Format JSON bodies if applicable.
    // Not super efficient, but not happening in a tight inner loop so how much does it matter?
    app.request_body = call.request_body;
    try {
        if (call.response_body) {
            // Parsing the result twice (here and below) makes me twitch. But in practice,
            // there is no perceptible performance impact, and this is for debugging anyway.
            // We could disable this (and all debugging facilities) for a release build if
            // the need ever arose.
            app.response_body = JSON.stringify(JSON.parse(call.response_body), null, json_indent);
        }
    }
    catch (e) {
        console.error(e);
        app.response_body = call.response_body;
    }

    if (!call.succeeded) {
        app.flash = call.error_message;
        return;
    }

    switch (call.type) {
        case net_call_type_t.login:
            {
                app.access_token = call.access_token;
                try {
                    const j = JSON.parse(call.response_body);
                    if (j["access_token"]) {
                        app.access_token = j["access_token"];
                        app.status = app_status_t.logged_in;
                    }
                    else {
                        throw new Error("No access token received.");
                    }
                }
                catch (ex) {
                    console.error(ex);
                    app.flash = app.flash + "Failed to login: " + ex;
                }
            } break;
        case net_call_type_t.refresh_case_types:
            {
                app.case_types = [];
                try {
                    const j = JSON.parse(call.response_body);
                    if (j["caseTypes"]) {
                        for (const itemKey of Object.keys(j["caseTypes"])) {
                            const item = j["caseTypes"][itemKey]
                            const case_type_json = item;
                            const case_type = new case_type_t();
                            case_type.id = case_type_json["ID"];
                            case_type.name = case_type_json["name"];
                            app.case_types.push(case_type);
                        }
                    }
                    else {
                        app.flash = "Not constellation compatible and/or no case types defined.";
                    }
                }
                catch (e) {
                    console.error(e);
                    app.flash = `Failed to refresh cases: ${e}`;
                }
            } break;
        case net_call_type_t.create_case:
            {
                try {
                    app.status = app_status_t.open_case;
                    app.etag = call.etag;
                    parse_dx_response(app, call.response_body);
                }
                catch (e) {
                    console.error(e);
                    app.flash = `Failed to create case: ${e}`;
                }
            } break;
        case net_call_type_t.open_assignment:
            {
                try {
                    app.etag = call.etag;
                    app.open_assignment_id = call.id1;
                    app.status = app_status_t.open_assignment;
                    parse_dx_response(app, call.response_body);
                }
                catch (e) {
                    console.error(e);
                    app.flash = `Failed to open assignment: ${e}`;
                }
            } break;
        case net_call_type_t.open_assignment_action:
            {
                try {
                    app.etag = call.etag;
                    app.open_assignment_id = call.id1;
                    app.open_action_id = call.id2;
                    app.status = app_status_t.open_action;
                    parse_dx_response(app, call.response_body);
                }
                catch (e) {
                    console.error(e);
                    app.flash = `Failed to open assignment action: ${e}`;
                }
            } break;
        case net_call_type_t.submit_assignment_action:
            {
                try {
                    app.open_assignment_id = call.id1;
                    app.status = app_status_t.open_case;
                    parse_dx_response(app, call.response_body);
                }
                catch (e) {
                    console.error(e);
                    app.flash = `Failed to submit assignment action: ${e}`;
                }
            } break;
    }
}

// // Returns a net call struct with the most common initialization.
function make_net_call(app: app_context_t, type: net_call_type_t): net_call_t {
    const call = new net_call_t();
    call.type = type;
    call.server = app.server;
    call.access_token = app.access_token;
    call.dx_api_path = app.dx_api_path;

    return call;
}

// Pushes a network call to login to the Pega instance.
export async function login(app: app_context_t): Promise<void> {
    const call = make_net_call(app, net_call_type_t.login);
    call.client_id = app.oauth2.client_id;
    call.client_secret = app.oauth2.client_secret;
    call.user_id = app.oauth2.user_id;
    call.password = app.oauth2.password;
    call.endpoint = app.oauth2.token_endpoint;

    // lock(app.dx_request_mutex);
    // app.dx_request_queue.push(call);
    if (!sessionStorage.getItem('token')) {
        const auth = new PegaAuth({
            grantType: app.oauth2.grant_type,
            clientId: app.oauth2.client_id,
            clientSecret: app.oauth2.client_secret,
            authService: app.oauth2.auth_service,
            userIdentifier: app.oauth2.user_id,
            password: btoa(app.oauth2.password),
            appAlias: app.oauth2.app_alias,
            authorizeUri: app.oauth2.authorization_endpoint,
            tokenUri: app.oauth2.token_endpoint,
            redirectUri: app.oauth2.redirect_uri
        });
        sessionStorage.setItem('token', JSON.stringify(await auth.login()));
    }
    const token = JSON.parse(sessionStorage.getItem('token')!);
    call.access_token = token.access_token;
    call.response_body = JSON.stringify(token);
    call.succeeded = true;
    // await handle_request(call)
    handle_response(call, app);
}

// Pushes a network call to refresh the case types defined in the Pega app.
export async function refresh_case_types(app: app_context_t): Promise<void> {
    const call = make_net_call(app, net_call_type_t.refresh_case_types);
    // std::scoped_lock lock(app.dx_request_mutex);
    app.dx_request_queue.push(call);

    await handle_request(call)
    handle_response(call, app);
}

// Pushes a network call to create a new case of the specified type.
export async function create_case(app: app_context_t, work_type_id: string): Promise<void> {
    const call = make_net_call(app, net_call_type_t.create_case);
    call.work_type_id = work_type_id;

    // std::scoped_lock lock(app.dx_request_mutex);
    app.dx_request_queue.push(call);

    await handle_request(call)
    handle_response(call, app);
}

// Pushes a network call to open the specified assignment.
export async function open_assignment(app: app_context_t, assignment_id: string): Promise<void> {
    // const call = make_net_call(app, net_call_type_t.open_assignment);
    // call.id1 = assignment_id;
    // std::scoped_lock lock(app.dx_request_mutex);
    // app.dx_request_queue.push(call);

    // await handle_request(call)
    // handle_response(call, app);
    app.open_assignment_id = assignment_id;
    app.status = app_status_t.open_assignment;
}

// Pushes a network call to open the specified action.
export async function open_assignment_action(app: app_context_t, action_id: string): Promise<void> {
    // const call = make_net_call(app, net_call_type_t.open_assignment_action);
    // call.id1 = app.open_assignment_id;
    // call.id2 = action_id;
    // std::scoped_lock lock(app.dx_request_mutex);
    // app.dx_request_queue.push(call);

    // await handle_request(call)
    // handle_response(call, app);

    app.open_action_id = action_id;
    app.status = app_status_t.open_action;
}

// Pushes a network call to submit the currently open assignment action. Assumes that
// validation has already passed.
export async function submit_open_assignment_action(app: app_context_t): Promise<void> {
    const call = make_net_call(app, net_call_type_t.submit_assignment_action);

    const content: { [key: string]: string } = {};
    let have_content = false;
    for (const field_pair of app.resources.fields.keys()) {
        const field = app.resources.fields.get(field_pair)!;
        if (field.is_special || field.is_class_key) continue;
        if (field.is_dirty) {
            content[field.id] = field.data;
            have_content = true;
        }
    }

    const body: { [key: string]: typeof content } = {};
    body["content"] = content;

    call.id1 = app.open_assignment_id;
    call.id2 = app.open_action_id;
    call.etag = app.etag;

    if (have_content) {
        call.request_body = JSON.stringify(body, null, json_indent);
    }

    // std::scoped_lock lock(app.dx_request_mutex);
    app.dx_request_queue.push(call);

    await handle_request(call)
    handle_response(call, app);
}