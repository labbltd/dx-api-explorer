import { app_context_t, app_status_t, Datasource } from "./dx_api_app_types";
import { json_indent } from "./dx_api_constants";
import { to_bool, to_enum, to_lower } from "./dx_api_helper_procs";
import { action_t, assignment_t, component_map_t, component_t, component_type_strings, component_type_t, content_map_t, field_map_t, field_t, paragraph_t } from "./dx_api_model_types";

// Maps component type to string.
function to_c_str(type: component_type_t): string {
    return component_type_strings[type];
}

// Returns the component type which corresponds to the provided string.
function to_component_type(str: string): component_type_t {
    return to_enum(str, component_type_strings);
}

// Produces a debug string for the component type and name.
function to_string(type: component_type_t, name: string, ref_type?: component_type_t): string {
    return `${to_c_str(type)}: ${name}${ref_type ? `[${to_c_str(ref_type)}]` : ''}`;
}

// Returns true if the provided field should be rendered with an editable interface.
export function is_editable(component: component_t, field: field_t): boolean {
    if (component.is_readonly) return false;
    if (component.is_disabled) return false;
    if (field.is_special) return false;
    if (field.is_class_key) return false;

    return true;
}

// Creates a key from a class id and a name such as: "The-Class-ID.TheName"
function make_key(class_id: string, name: string): string {
    return `${class_id}.${name}`;
}

// Takes a DX API label property like "@L Blah", "@FL .BlahBlah", or "Blah Blah Blah"
// and returns correct response:
//  "@L Blah"          : "Blah".
//  "@FL .BlahBlah"    : The value of the label property for field .BlahBlah.
//  "Blah Blah Blah"   : "Blah Blah Blah"
function resolve_label(raw_label: string, fields: field_map_t, class_id: string): string {
    console.assert(!!class_id);
    let result = raw_label;

    if (raw_label) {
        if (raw_label[0] == '@') {
            if (raw_label[1] == 'L') {
                // "@L Blah"
                //  0123456
                //     ^
                //     Start here.
                result = raw_label.substr(3);
            }
            else if (raw_label[1] == 'F' && raw_label[2] == 'L') {
                // "@FL .Blah"
                //  012345678
                //       ^
                //       Start here.
                const field_id = raw_label.substr(5);
                const field_key = make_key(class_id, field_id);
                result = fields.get(field_key)?.label || result;
            }
        }
    }

    return result;
};

// Returns the value of the provided name in the content map. In strict mode, throws an exception if the name can't be
// found or the content doesn't match.
function get_content(content: content_map_t, class_id: string, name: string, strict_mode = true): string {
    let result = '';
    if (content.has("classID")) {
        if (content.get("classID") == class_id) {
            if (content.has(name)) {
                result = content.get(name) || result;
            }
            else if (strict_mode) {
                const e = `Could not resolve name: ${name}\nName not found in content.`;
                throw new Error(e);
            }
        }
        else if (strict_mode) {
            throw new Error(`Could not resolve name: ${name}\ncontent['classID'] = ${content.get("classID")}\nclass_id = ${class_id}`);
        }
    }
    else if (strict_mode) {
        throw new Error(`Could not resolve name: ${name}\ncontent does not contain 'classID'`);
    }
    return result;
}

// Takes a DX API name property like "@P .Blah" or "Blah Blah Blah" and returns correct response:
//  "@P .Blah" : "Blah" if dereferencing isn't required, otherwise the value of the content for "Blah".
//  "Blah Blah": "Blah Blah"
function resolve_name(raw_name: string, content: content_map_t, class_id: string, dereference_property_name = true): string {
    console.assert(!!class_id);
    let result = raw_name;

    if (raw_name.length >= 2 && raw_name.substring(0, 2) == "@P") {
        // "@P .Blah"
        //  01234567
        //      ^
        //      Start here.
        const name = raw_name.substring(4);
        if (dereference_property_name) {
            result = get_content(content, class_id, name);
        }
        else {
            result = name;
        }
    }

    return result;
}

// Recursively validates that component and all of its children are in a valid
// state for submission. Only applies to field components.
export function validate_component_r(component: component_t, components: component_map_t, fields: field_map_t): boolean {
    let is_valid = true;

    switch (component.type) {
        case component_type_t.component_type_text_input:
        case component_type_t.component_type_text_area:
        case component_type_t.component_type_date:
        case component_type_t.component_type_dropdown:
        case component_type_t.component_type_integer:
            {
                if (component.is_required) {
                    const field = fields.get(component.key);
                    if (!field?.data) {
                        is_valid = false;
                    }
                }
            } break;
    }

    // Process children.
    if (component.children.length > 0) {
        for (const child of component.children) {
            if (!is_valid) break;
            is_valid = validate_component_r(child, components, fields);
        }
    }

    return is_valid;
}

function resolve_datasource(datasource: Datasource, fields: Map<string, field_t>, class_id: string,): component_t[] {
    if (!datasource) return [];
    if (typeof datasource === 'string') {
        // @ASSOCIATED .Geslacht
        // 01234567890123
        if (datasource.substring(0, 11) == "@ASSOCIATED") {
            const source = JSON.parse(fields.get(class_id + datasource.substring(12))!.json).datasource
            return resolve_datasource(source as Datasource, fields, class_id);
        }
    } else if ("records" in datasource) {
        return datasource.records!.map(r => {
            const option = new component_t();
            option.label = r.value;
            option.key = r.key;
            return option;
        });
    }
    // need to handle other scenarios
    return [];
}

function resolve_instructions(instructions: string, paragraphs: Map<string, paragraph_t>): string {
    // @PARAGRAPH AccountDetails
    // 0123456789012...

    if (instructions.substring(0, 10) === '@PARAGRAPH') {
        return paragraphs.get(instructions.substring(11))!.content;
    }
    return instructions;
}


// Recursively makes component and its children from DX API JSON response data.
function make_component_r(component_json: any, app: app_context_t, parent_class_id: string = ""): component_t {
    const new_component = new component_t();
    new_component.json = JSON.stringify(component_json, null, json_indent);
    new_component.type = to_component_type(component_json["type"]);

    switch (new_component.type) {
        case component_type_t.component_type_unknown:
            {
                new_component.class_id = parent_class_id;
                // ...always first.

                new_component.name = component_json["type"];

                // Always last:
                new_component.debug_string = to_string(new_component.type, new_component.name);
            } break;
        case component_type_t.component_type_reference:
            {
                new_component.class_id = parent_class_id;
                // ...always first.

                const config_json = component_json["config"];
                new_component.name = resolve_name(config_json["name"], app.case_info.content, new_component.class_id);
                new_component.ref_type = to_component_type(config_json["type"]);

                // References might specify a context. If that context exists, we use it if we support it. If it exists
                // and we don't support it, we mark this reference as broken.
                if (config_json["context"]) {
                    const context = config_json["context"];

                    if (context.substr(0, 6) == "@CLASS") {
                        // @CLASS The-Class-Name
                        // 0123456789...
                        //        ^
                        //        Start here.
                        new_component.class_id = context.substr(7);
                    } else if (context.substring(0, 2) === '@P') {
                        // @P .Property-Name
                        // 012345...
                        new_component.class_id = app.case_info.content.get(context.substring(4)).classID || parent_class_id;
                    } else if (app.case_info.content.has(context.substring(1))) {
                        // .Property-Name
                        new_component.class_id = app.case_info.content.get(context.substring(1)).classID;
                    } else {
                        new_component.is_broken = true;
                        new_component.broken_string = `Unsupported context: ${context}`;
                    }
                }

                // Always last:
                new_component.debug_string = to_string(new_component.type, new_component.name, new_component.ref_type);
            } break;
        case component_type_t.component_type_region:
        case component_type_t.component_type_group:
        case component_type_t.component_type_flow_container:
            {
                new_component.class_id = parent_class_id;
                // ...always first.

                new_component.name = resolve_name(component_json["name"] || component_json["config"].id || component_json["config"].name, app.case_info.content, new_component.class_id);
                if (component_json["config"]?.instructions) {
                    new_component.instructions = resolve_instructions(component_json["config"].instructions, app.resources.paragraphs);
                }
                // Always last:
                new_component.debug_string = to_string(new_component.type, new_component.name);
            } break;
        case component_type_t.component_type_view:
            {
                new_component.class_id = component_json["classID"];
                // ...always first.

                new_component.name = resolve_name(component_json["name"], app.case_info.content, new_component.class_id);

                // Views usually, but not always, specify a template in the config.
                const config_json = component_json["config"];
                if (config_json["template"]) new_component.ref_type = to_component_type(config_json["template"]);
                
                // optional instructions
                if (component_json["config"]?.instructions) {
                    new_component.instructions = resolve_instructions(component_json["config"].instructions, app.resources.paragraphs);
                }
                // Always last:
                new_component.debug_string = to_string(new_component.type, new_component.name, new_component.ref_type);
            } break;
        case component_type_t.component_type_dropdown:
        case component_type_t.component_type_radio:
        case component_type_t.component_type_text_area:
        case component_type_t.component_type_text_input:
        case component_type_t.component_type_date:
        case component_type_t.component_type_integer:
        case component_type_t.component_type_checkbox:
        case component_type_t.component_type_email:
        case component_type_t.component_type_phone:
        case component_type_t.component_type_attachment:
        case component_type_t.component_type_url:
            {
                new_component.class_id = parent_class_id;
                // ...always first.

                const config_json = component_json["config"];
                new_component.name = resolve_name(config_json["value"], app.case_info.content, new_component.class_id, false);
                new_component.label = resolve_label(config_json["label"] || config_json["caption"], app.resources.fields, new_component.class_id);

                // Check for optional attributes.
                if (config_json["disabled"]) new_component.is_disabled = to_bool(config_json["disabled"]);
                if (config_json["readOnly"]) new_component.is_readonly = to_bool(config_json["readOnly"]);
                if (config_json["required"]) new_component.is_required = to_bool(config_json["required"]);

                if (config_json["datasource"]) new_component.children = resolve_datasource(config_json["datasource"], app.resources.fields, parent_class_id);

                // Always last:
                new_component.debug_string = to_string(new_component.type, new_component.label);
            } break;
    }

    // Validate the component and finalize it.
    if (!new_component.name
        || !new_component.class_id
        || new_component.type == component_type_t.component_type_unspecified) {
        throw new Error(`Failed to make component from JSON:\n${new_component.json}`);
    }
    else {
        new_component.key = make_key(new_component.class_id, new_component.name);
    }

    // Process children:
    if (component_json["children"]) {
        for (const child of component_json["children"]) {
            //component_t new_child_component = make_component_r(child, app, parent_class_id);
            const new_child_component = make_component_r(child, app, new_component.class_id);
            new_component.children.push(new_child_component);
        }
    }

    return new_component;
};

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e350
export function parse_dx_response(app: app_context_t, response_body: string): void {
    const j = JSON.parse(response_body);
    const info = j["data"]["caseInfo"];

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
        for (const assignment of (info["assignments"] || [])) {
            const new_assignment = new assignment_t();
            new_assignment.id = assignment["ID"];
            new_assignment.name = assignment["name"];
            new_assignment.can_perform = to_bool(assignment["canPerform"]);

            for (const action of assignment["actions"]) {
                const new_action = new action_t();
                new_action.id = action["ID"];
                new_action.name = action["name"];
                new_action.type = action["type"];

                new_assignment.actions.set(new_action.id, new_action);
            }

            app.case_info.assignments.set(new_assignment.id, new_assignment);
        }
        if (app.case_info.assignments.size === 1) {
            app.status = app_status_t.open_assignment;
            app.open_assignment_id = app.case_info.assignments.keys().next().value!
            if (app.case_info.assignments.get(app.open_assignment_id)!.actions.size === 1) {
                app.status = app_status_t.open_action;
                app.open_action_id = app.case_info.assignments.get(app.open_assignment_id)!.actions.keys().next().value!;
            }
        }

        app.case_info.content.clear();
        for (const content of Object.keys(info["content"])) {
            const k = content;
            const content_value = info["content"][k];

            app.case_info.content.set(k, content_value);
        }
    } // End of case info parsing.

    // UI resources.
    if (j["uiResources"]) {
        const ui_resources_json = j["uiResources"] || {};
        const resources_json = ui_resources_json["resources"] || {};
        const fields_json = resources_json["fields"] || {};
        const views_json = resources_json["views"] || {};
        const paragraphs_json = resources_json["paragraphs"] || {};

        // Fields:
        {
            app.resources.fields.clear();
            for (const field_array of Object.keys(fields_json)) {
                for (const value of fields_json[field_array]) {

                    const new_field = new field_t();
                    new_field.id = field_array;
                    new_field.json = JSON.stringify(value, null, json_indent);
                    new_field.type = value["type"];

                    // Sometimes we get an "Unknown" type which is completely malformed,
                    // in which case we just skip it entirely.
                    if (to_lower(new_field.type) == "unknown") continue;

                    new_field.class_id = value["classID"];

                    if (value["label"]) new_field.label = value["label"];
                    if (value["isSpecial"]) new_field.is_special = to_bool(value["isSpecial"]);
                    if (value["isClassKey"]) new_field.is_class_key = to_bool(value["isClassKey"]);

                    new_field.data = get_content(app.case_info.content, new_field.class_id, new_field.id, false);

                    const new_field_key = make_key(new_field.class_id, new_field.id);
                    app.resources.fields.set(new_field_key, new_field);
                }
            }
        }

        // Paragraphs
        {
            app.resources.paragraphs.clear();
            for (const paragraph_array of Object.keys(paragraphs_json)) {
                const p = new paragraph_t();
                p.classID = paragraphs_json[paragraph_array][0].classID;
                p.content = paragraphs_json[paragraph_array][0].content;
                p.name = paragraphs_json[paragraph_array][0].name;
                app.resources.paragraphs.set(paragraph_array, p)
            }
        }

        // Views (components):
        {
            app.resources.components.clear();
            for (const view_array of Object.keys(views_json)) {
                for (const value of views_json[view_array]) {
                    const new_component = make_component_r(value, app);
                    app.resources.components.set(new_component.key, new_component);
                }
            }
        }

        // Root:
        {
            const config_json = ui_resources_json["root"]["config"];
            const context = config_json["context"];

            if (context == "caseInfo.content") {
                const name = config_json["name"];
                const type = config_json["type"];

                if (type == "view") {
                    const class_id = app.case_info.content.get("classID") || '';
                    app.root_component_key = make_key(class_id, name);
                }
                else {
                    throw new Error(`Root component uses unsupported type: ${type}`);
                }
            }
            else {
                throw new Error(`Root component uses unsupported context: ${context}`);
            }
        }

        // ActionButtons
        {
            app.action_buttons = ui_resources_json["actionButtons"];

        }
    } // End of UI resources parsing.
}
