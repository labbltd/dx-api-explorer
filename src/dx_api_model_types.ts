// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e1783
export class field_t {
    id!: string;
    class_id!: string;
    label!: string;
    type!: string;
    data!: string;
    json!: any;

    is_special = false;
    is_class_key = false;
    is_dirty = false;
};

export type field_map_t = Map<string, field_t>;

// Types of components: https://docs.pega.com/bundle/constellation-sdk/page/constellation-sdks/sdks/using-dx-component-builder.html#d15866e82
export enum component_type_t {
    component_type_unspecified,
    component_type_unknown,
    // ...always first.

    // Design system extensions:
    // ...nothing yet!

    // Infrastructure:
    component_type_reference,
    component_type_region,
    component_type_view,
    component_type_group,
    component_type_flow_container,

    // Fields:
    component_type_text_area,
    component_type_text_input,
    component_type_integer,
    component_type_email,
    component_type_phone,
    component_type_checkbox,
    component_type_date,
    component_type_dropdown,
    component_type_radio,
    component_type_url,

    // Templates:
    component_type_default_form,

    // Widgets:
    component_type_attachment,

    // Always last:
    component_type_count,
};

// Component strings as we'll see them in DX API responses, should be in same order as enum.
export const component_type_strings =
    [
        "Unspecified",
        "Unknown",
        "Reference",
        "Region",
        "View",
        "Group",
        "FlowContainer",
        "TextArea",
        "TextInput",
        "Integer",
        "Email",
        "Phone",
        "Checkbox",
        "Date",
        "Dropdown",
        "RadioButtons",
        "URL",
        "DefaultForm",
        "Attachment",
    ];

// Component megastruct:
export class component_t {
    type = component_type_t.component_type_unspecified;
    name!: string;
    class_id!: string;
    key!: string; // Identifies this rule, or the referenced rule in the case of references/fields:string.
    // ...required for all components!

    label!: string;
    json!: string;
    debug_string!: string;
    broken_string!: string;

    is_readonly = false;
    is_required = false;
    is_disabled = false;
    is_broken = false;
    is_selected = false;

    ref_type = component_type_t.component_type_unspecified; // Referenced component / type of template.
    children = new Array<component_t>();
    instructions!: string;
    options?: component_t[];
};

export type component_map_t = Map<string, component_t>;

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e2455
export class action_t {
    id!: string;
    name!: string;
    type!: string;
};


// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e1053
export class assignment_t {
    id!: string;
    name!: string;
    can_perform = false;
    actions = new Map<string, action_t>();
};

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e800
export type content_map_t = Map<string, string>;

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/endpoint-get-casetypes.html
export class case_type_t {
    id!: string;
    name!: string;
};

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e350
export class case_info_t {
    type = new case_type_t();
    id!: string;
    business_id!: string;
    create_time!: string;
    created_by!: string;
    last_update_time!: string;
    last_updated_by!: string;
    name!: string;
    owner!: string;
    status!: string;
    assignments = new Map<string, assignment_t>();
    content = new Map<string, any>();
};

export class paragraph_t {
    content!: string;
    name!: string;
    classID!: string;
}

// https://docs.pega.com/bundle/dx-api/page/platform/dx-api/understand-dx-api-response.html#d33668e1783
export class resources_t {
    paragraphs = new Map<string, paragraph_t>();
    fields = new Map<string, field_t>();
    components = new Map<string, component_t>();
};
