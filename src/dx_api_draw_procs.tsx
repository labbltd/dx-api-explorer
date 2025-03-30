import { ChangeEvent, JSX, useRef, useState } from "react";
import Brand from "./brand";
import { app_context_t, app_status_t } from "./dx_api_app_types";
import { is_editable, parse_dx_response, validate_component_r } from "./dx_api_model_procs";
import { component_map_t, component_t, component_type_t } from "./dx_api_model_types";
import { create_case, login, open_assignment, open_assignment_action, refresh_case_types, submit_open_assignment_action, to_string } from "./dx_api_network_procs";

function CollapsibleCard(props: { title: string, children: JSX.Element, open?: boolean }) {
    const { title, children, open = false } = props;
    const [collapsed, setCollapsed] = useState(!open);

    return <div className="card">
        <header className="card-header is-clickable" onClick={() => setCollapsed(!collapsed)}>
            <p className="card-header-title">{title}</p>
            <button className="card-header-icon" aria-label="more options">
                <span className="icon">
                    {
                        collapsed ?
                            <i className="fas fa fa-angle-down" aria-hidden="true"></i> :
                            <i className="fas fa fa-angle-up" aria-hidden="true"></i>
                    }
                </span>
            </button>
        </header>
        {!collapsed && <div className="card-content">
            {children}
        </div>}
    </div>
}

// Recursively draws debug component information.
function Draw_component_debug_r(props: { component: component_t, component_map: component_map_t, selectedComponent: component_t | undefined, setSelected: (component: component_t | undefined) => void, id?: string }) {
    const { component, component_map, selectedComponent, setSelected, id = '0' } = props;
    if (!component) {
        return null;
    }
    return <fieldset style={{ padding: '0 10px', border: component.json && JSON.parse(component.json).config?.context ? '1px dashed red' : undefined }}>
        <legend>{component.json && JSON.parse(component.json).config?.context && JSON.parse(component.json).config?.context}</legend>
        <div style={{ outline: selectedComponent === component ? '1px dashed blue' : 'none' }}>
            <div className="is-clickable" onClick={() => setSelected(selectedComponent === component ? undefined : component)}>{component.json && JSON.parse(component.json).config?.visibility && JSON.parse(component.json).config?.visibility !== true ? `(${JSON.parse(component.json).config?.visibility})` : null} {component.debug_string}</div>
            {component.is_broken && "(!)" + component.broken_string}
            {!component.is_broken && component.type == component_type_t.component_type_reference &&
                <Draw_component_debug_r component={component_map.get(component.key)!} component_map={component_map} selectedComponent={selectedComponent} setSelected={setSelected} id={`${id}.${0}`} />
            }
            {component.children?.length > 0 &&
                component.children.map((child, idx) => <Draw_component_debug_r key={`${id}.${idx}`} component={child} component_map={component_map} selectedComponent={selectedComponent} setSelected={setSelected} id={`${id}.${idx}`} />)
            }
        </div>
    </fieldset>
}

function to_input_type(type: component_type_t) {
    switch (type) {
        case component_type_t.component_type_integer: return 'number';
        case component_type_t.component_type_date: return 'date';
        case component_type_t.component_type_text_input: return 'text';
        default: return 'text';
    }
}

function update_field(e: ChangeEvent, component: component_t, app: app_context_t, setApp: Function) {
    const newValue = (e.target as HTMLInputElement)!.value;

    app.case_info.content.set(component.key.replace(component.class_id + '.', ''), newValue)

    const field = app.resources.fields.get(component.key)!;
    field.data = newValue;
    field.is_dirty = true;
    setApp({ ...app })
}

function is_visible(app: app_context_t, component: component_t) {
    const visibility = JSON.parse(component.json)?.config?.visibility;
    if (typeof visibility === 'boolean') {
        return visibility;
    }
    if (typeof visibility === 'string') {
        if (visibility.startsWith('@W')) {
            const when = app.case_info.content.get('summary_of_when_conditions__');
            return when[visibility.replace('@W ', '')];
        }
        if (visibility.startsWith('@E')) {
            let expression = visibility.replace('@E ', '');
            while (expression.indexOf('\.') > -1) {
                const startIdx = expression.indexOf('\.');
                const endIdx = expression.indexOf(' ', startIdx);
                const name = expression.substring(startIdx + 1, endIdx);
                const value = app.case_info.content.get(name) || '';
                expression = expression.replace(`.${name}`, `'${value}'`);
            }
            return eval(expression);
        }
    }
    return true;
}

// Recursively draws components.
function Draw_component_r(props: { component: component_t, app: app_context_t, setApp: Function, id: number }) {
    const { component, app, setApp, id } = props;
    if (!component || !is_visible(app, component)) {
        return null;
    }
    return <div>
        {
            component.type === component_type_t.component_type_reference &&
            <Draw_component_r component={app.resources.components.get(component.key)!}
                app={app}
                setApp={setApp}
                id={id} />
        }
        {
            (
                component.type === component_type_t.component_type_text_area ||
                component.type === component_type_t.component_type_text_input ||
                component.type === component_type_t.component_type_integer ||
                component.type === component_type_t.component_type_checkbox ||
                component.type === component_type_t.component_type_date ||
                component.type === component_type_t.component_type_dropdown ||
                component.type === component_type_t.component_type_radio ||
                component.type === component_type_t.component_type_url
            ) &&
            (is_editable(component, app.resources.fields.get(component.key)!) ?
                <div className="field">
                    <label className="label">{component.label}</label>
                    <div className="control">
                        {
                            component.type === component_type_t.component_type_text_area &&
                            <textarea className="textarea"
                                onChange={e => update_field(e, component, app, setApp)}
                                value={app.resources.fields.get(component.key)!.data}>
                            </textarea>
                        }
                        {
                            (
                                component.type === component_type_t.component_type_text_input ||
                                component.type === component_type_t.component_type_date ||
                                component.type === component_type_t.component_type_integer
                            ) && <input className="input" type={to_input_type(component.type)}
                                value={app.resources.fields.get(component.key)?.data}
                                onChange={e => update_field(e, component, app, setApp)} />
                        }
                        {
                            component.type === component_type_t.component_type_dropdown &&
                            <div className="select">
                                <select onChange={e => update_field(e, component, app, setApp)} defaultValue={app.resources.fields.get(component.key)?.data}>
                                    {!component.is_required && <option value=""></option>}
                                    {component.options?.map(child => <option key={child.key} value={child.key}>
                                        {child.label}
                                    </option>)}
                                </select>
                            </div>
                        }
                        {
                            component.type === component_type_t.component_type_radio &&
                            <div className="radios">
                                {component.options?.map(child => <label key={child.key} className="radio">
                                    <input type="radio" name={component.name}
                                        onChange={e => update_field(e, component, app, setApp)} />
                                    {child.label}
                                </label>)}
                            </div>
                        }
                    </div>
                </div>
                : <>
                    <dt>{component.label}</dt>
                    <dd>{app.resources.fields.get(component.key)?.data}</dd>
                </>)
        }
        {
            component.instructions && <div className="block"><div dangerouslySetInnerHTML={{ __html: component.instructions }}></div></div>
        }
        {
            component.children.map((child, idx) => <Draw_component_r
                key={idx}
                component={child}
                app={app}
                setApp={setApp}
                id={id} />
            )
        }
    </div>;
}

// Draws the main menu.
export function Draw_main_menu() {
    return <nav className="navbar">
        <div className="navbar-brand">
            <a className="navbar-item" href="https://www.labb.ltd/">
                <Brand />
            </a>
        </div>
    </nav>
}

function Field(props: { field: string, app: app_context_t, setApp: Function, type?: string }) {
    const { field, app, setApp, type = 'text' } = props;
    return <div className="field">
        <label className="label">{field.split('_').map(w => w.charAt(0).toUpperCase() + w.slice(1)).join(' ')}</label>
        <div className="control">
            <input className="input" type={type} name={field}
                value={(app as any)[field]}
                onChange={e => setApp({ ...app, [field]: e.target.value })}
            />
        </div>
    </div>
}

function OAuthField(props: { field: string, app: app_context_t, setApp: Function, type?: string, options?: string[] }) {
    const { field, app, setApp, type = 'text', options = [] } = props;
    return <div className="field">
        <label className="label">{field.split('_').map(w => w.charAt(0).toUpperCase() + w.slice(1)).join(' ')}</label>
        <div className="control">
            {(type === 'text' || type === 'password') && <input className="input" type={type} name={field}
                value={(app.oauth2 as any)[field]}
                onChange={e => setApp({ ...app, oauth2: { ...app.oauth2, [field]: e.target.value } })}
            />}
            {type === 'dropdown' && <div className="select">
                <select value={(app.oauth2 as any)[field]} name={field}
                    onChange={e => setApp({ ...app, oauth2: { ...app.oauth2, [field]: e.target.value } })}
                >
                    <option value="">Select...</option>
                    {options.map(o => <option key={o}>{o}</option>)}
                </select>
            </div>}
        </div>
    </div>
}

function Draw_message(props: { title: string, children: JSX.Element }) {
    return <article className="message">
        <div className="message-header">
            <p>{props.title}</p>
        </div>
        <div className="message-body">
            {props.children}
        </div>
    </article>
}

// Draws the login form.
function Draw_login_form(props: { app: app_context_t, setApp: Function }) {
    const { app, setApp } = props;
    return <div className="content">
        <div className="container is-max-tablet">
            <Draw_message title="Infinity">
                <p>Log in to an infinity server to explore the available case types</p>
            </Draw_message>
            <Field field="server" app={app} setApp={setApp} />
            <Field field="dx_api_path" app={app} setApp={setApp} />
            <OAuthField field="grant_type" app={app} setApp={setApp} type="dropdown" options={['authCode', 'customBearer', 'clientCreds', 'passwordCreds', 'none']} />
            <OAuthField field="client_id" app={app} setApp={setApp} />
            <OAuthField field="client_secret" app={app} setApp={setApp} />
            {app.oauth2.grant_type === 'authCode' && <OAuthField field="authorization_endpoint" app={app} setApp={setApp} />}
            <OAuthField field="token_endpoint" app={app} setApp={setApp} />
            <OAuthField field="user_id" app={app} setApp={setApp} />
            {(app.oauth2.grant_type === 'authCode' || app.oauth2.grant_type === 'clientCreds') && <OAuthField field="password" app={app} setApp={setApp} type="password" />}
            {app.oauth2.grant_type === 'authCode' && <OAuthField field="auth_service" app={app} setApp={setApp} />}
            {app.oauth2.grant_type === 'authCode' && <OAuthField field="no_pkce" app={app} setApp={setApp} />}
            {app.oauth2.grant_type === 'authCode' && <OAuthField field="redirect_uri" app={app} setApp={setApp} />}
            <OAuthField field="app_alias" app={app} setApp={setApp} />

            <div className="field">
                <div className="control">
                    <button type="button" className="button" onClick={async () => {
                        await login(app);
                        await refresh_case_types(app);
                        setApp({ ...app })
                    }}>Login</button>
                </div>
            </div>
        </div>
    </div>
}

// Draws the currently open case.
export function Draw_open_case(props: { app: app_context_t, setApp: Function, open: boolean }) {
    const { app, setApp, open } = props;
    const work = app.case_info;

    return <CollapsibleCard title={`Case: ${work.business_id}`} open={open}>
        <div className="content columns">
            <table className="table column">
                <thead>
                    <tr><th>Field</th><th>Value</th></tr>
                </thead>
                <tbody>
                    <tr><td>Case ID</td><td>{work.business_id}</td></tr>
                    <tr><td>Name</td><td>{work.name}</td></tr>
                    <tr><td>Status</td><td>{work.status}</td></tr>
                    <tr><td>Owner</td><td>{work.owner}</td></tr>
                    <tr><td>Created on</td><td>{work.create_time}</td></tr>
                    <tr><td>Created by</td><td>{work.created_by}</td></tr>
                    <tr><td>Updated on</td><td>{work.last_update_time}</td></tr>
                    <tr><td>Updated by</td><td>{work.last_updated_by}</td></tr>
                </tbody>
            </table>
            {work.assignments.size > 0 && <div className="column">
                <h2>Assignments</h2>
                {[...work.assignments.keys()].map(assignment_key => {
                    const assignment = work.assignments.get(assignment_key)!;

                    if (assignment?.can_perform) {
                        return <button key={assignment.name} type="button" className="button" onClick={async () => {
                            await open_assignment(app, assignment.id);
                            setApp({ ...app });
                        }}>{assignment.name}</button>
                    }
                    else {
                        return <button key={assignment.name} type="button" disabled>assignment.name</button>
                    }
                })}
            </div>
            }
        </div>
    </CollapsibleCard>
}

// Draws the currently open assignment.
function Draw_open_assignment(props: { app: app_context_t, setApp: Function, open: boolean }) {
    const { app, setApp, open } = props;
    console.assert(!!app.open_assignment_id);
    const assignment = app.case_info.assignments.get(app.open_assignment_id);

    return <CollapsibleCard title={`Assignment: ${assignment?.name}`} open={open}>
        <div className="content columns">
            <table className="table column">
                <thead>
                    <tr><th>Field</th><th>Value</th></tr>
                </thead>
                <tbody>
                    <tr><td>Name</td><td>{assignment?.name}</td></tr>
                    <tr><td>ID</td><td>{assignment?.id}</td></tr>
                    <tr><td>Can Perform</td><td>{assignment?.can_perform}</td></tr>
                </tbody>
            </table>
            <div className="column">
                <h2>Actions</h2>
                {assignment && [...assignment.actions.keys()].map(actionId => {
                    const action = assignment.actions.get(actionId)
                    return action && <button key={action.name} type="button" className="button" onClick={async () => {
                        await open_assignment_action(app, action.id);
                        setApp({ ...app });
                    }}>{action.name}</button>
                })}
            </div>
        </div>
    </CollapsibleCard>
}

// Draws the currently open assignment action.
function Draw_open_assignment_action(props: { app: app_context_t, setApp: Function, open: boolean }) {
    const { app, setApp, open } = props;
    console.assert(!!app.open_assignment_id);
    console.assert(!!app.open_action_id);
    const action = app.case_info.assignments.get(app.open_assignment_id)!.actions.get(app.open_action_id);
    const component_id = 0;
    const root_component = app.resources.components.get(app.root_component_key)!;

    return <CollapsibleCard title={`Action: ${action?.name}`} open={open}>
        <div className="content">
            <div className="container is-max-tablet">
                <div className="block">
                    <Draw_component_r
                        component={root_component}
                        app={app}
                        setApp={setApp}
                        id={component_id}
                    />
                </div>
                <div className="level">
                    <div className="level-left">
                        {app.action_buttons.secondary.map(button => <button type="button" className="button" key={button.jsAction} onClick={async () => {
                            if (button.jsAction === 'finishAssignment') {
                                const are_components_valid = validate_component_r(root_component, app.resources.components, app.resources.fields);
                                if (are_components_valid) {
                                    await submit_open_assignment_action(app);
                                    setApp({ ...app });
                                }
                                else {
                                    app.flash = "Validation failed. Did you fill out all required fields?";
                                    setApp({ ...app });
                                }
                            }
                            setApp({ ...app });
                        }}>{button.name}</button>)}
                    </div>
                    <div className="level-right">
                        {app.action_buttons.main.map(button => <button type="button" className="button" key={button.jsAction} onClick={async () => {
                            if (button.jsAction === 'finishAssignment') {
                                const are_components_valid = validate_component_r(root_component, app.resources.components, app.resources.fields);
                                if (are_components_valid) {
                                    await submit_open_assignment_action(app);
                                    setApp({ ...app });
                                }
                                else {
                                    app.flash = "Validation failed. Did you fill out all required fields?";
                                    setApp({ ...app });
                                }
                            }
                            setApp({ ...app });
                        }}>{button.name}</button>)}
                    </div>
                </div>
            </div>
        </div>
    </CollapsibleCard>
}

// Draws the main user interface.
export function Draw_main_window(props: { app: app_context_t, setApp: Function }) {
    const { app, setApp } = props;
    return <>
        {
            // Show open work object.
            (
                app.status == app_status_t.open_case ||
                app.status == app_status_t.open_assignment ||
                app.status == app_status_t.open_action
            ) &&
            <Draw_open_case app={app} setApp={setApp} open={app.status == app_status_t.open_case} />
        }
        {
            // Show open assignment.
            (
                app.status == app_status_t.open_assignment ||
                app.status == app_status_t.open_action
            ) &&
            <Draw_open_assignment app={app} setApp={setApp} open={app.status == app_status_t.open_assignment} />
        }
        {
            // Show open action.
            app.status == app_status_t.open_action &&
            <Draw_open_assignment_action app={app} setApp={setApp} open={app.status == app_status_t.open_action} />
        }
    </>
}

// Draws information about network calls and responses.
function Draw_debug_calls(props: { app: app_context_t }) {
    const { app } = props;
    return <>{app.dx_request_queue.map((request, idx) => <CollapsibleCard key={idx} title={request.method + ' ' + request.endpoint}>
        <div className="content columns">
            <div className="column">
                Request
                {request.request_headers && <pre>{to_string(request.request_headers)}</pre>}
                {request.request_body && <pre>{request.request_body}</pre>}
            </div>
            <div className="column">
                Response
                {request.response_headers && <pre>{request.response_headers}</pre>}
                {request.response_body && <pre>{JSON.stringify(JSON.parse(request.response_body), null, 2)}</pre>}
            </div>
        </div>
    </CollapsibleCard>)}</>
}

// Draws a tree view of components in use starting with root.
function Draw_debug_components(props: { app: app_context_t }) {
    const [selected, setSelected] = useState<component_t | undefined>();
    const { app } = props;
    return <div className="columns">
        <pre className="column">
            <Draw_component_debug_r
                component={app.resources.components.get(app.root_component_key)!}
                component_map={app.resources.components}
                selectedComponent={selected}
                setSelected={setSelected} />
        </pre>
        {selected && <pre className="column">{selected?.json}</pre>}
    </div>

}

// Draws fields currently in use.
function Draw_debug_fields(props: { app: app_context_t }) {
    const [selected, setSelected] = useState<string>();
    const { app } = props;
    return <div className="columns">
        <table className="table column is-hoverable">
            <thead>
                <tr><th>Key</th><th>Label</th><th>Type</th><th>Display As</th></tr>
            </thead>
            <tbody>
                {Array.from(app.resources.fields.keys()).sort().map(f => <tr key={f} onClick={() => setSelected(f)} className="is-clickable">
                    <td>{f}</td>
                    <td>{JSON.parse(app.resources.fields.get(f)!.json).label}</td>
                    <td>{JSON.parse(app.resources.fields.get(f)!.json).type}</td>
                    <td>{JSON.parse(app.resources.fields.get(f)!.json).displayAs}</td>
                </tr>)}
            </tbody>
        </table>
        {selected && <pre className="column">{selected && app.resources.fields.get(selected)?.json}</pre>}
    </div>

}

function Draw_debug_content_object(props: { parentKey: string, value: Content }) {
    const { parentKey, value } = props;

    return Object.keys(value).map(childKey => {
        const item = value[childKey];
        const fullKey = `${parentKey}.${childKey}`;
        return (!item || typeof item !== 'object') ? <tr key={fullKey} data-key={fullKey}>
            <td>{fullKey}</td>
            <td>{item}</td>
        </tr> : <Draw_debug_content_object key={fullKey} parentKey={fullKey} value={item} />
    });
}

interface Content {
    [key: string]: string | Content
}
// Draws content currently in use.
function Draw_debug_content(props: { app: app_context_t }) {
    const { app } = props;
    return <table className="table">
        <thead><tr><th>Key</th><th>Value</th></tr></thead>
        <tbody>
            <Draw_debug_content_object parentKey={''} value={Object.fromEntries(app.case_info.content)} />
        </tbody>
    </table>
}

export function Draw_clipboard(props: { app: app_context_t, setApp: Function, setTab: Function }) {
    const clipboardRef = useRef<HTMLTextAreaElement>(null);
    const { app, setApp, setTab } = props;
    return <div className="container is-max-tablet">
        <Draw_message title="Clipboard">
            <p>Copy/Paste the response from any of the Constellation's DX API responses here to analyze its content</p>
        </Draw_message>
        <div className="block">
            <textarea className="textarea" rows={20} ref={clipboardRef}></textarea>
        </div>
        <button type="button" className="button" onClick={() => {
            try {
                parse_dx_response(app, clipboardRef.current?.value!);
                app.dx_request_queue = [];
                setTab('Main');
            } catch (e) {
                app.flash = (e as any).toString();
            }
            setApp({ ...app });
        }}>Analyze</button>
    </div>
}

function to_date_string(value: number) {
    const date = new Date(+(value + '000'));
    return date.toLocaleTimeString()
}

export function Draw_infinity(props: { app: app_context_t, setApp: Function, setTab: Function }) {
    const { app, setApp, setTab } = props;
    if (!app.access_token) {
        return <Draw_login_form app={app} setApp={setApp} />
    }
    const operator = JSON.parse(window.atob(app.access_token.split('\.')[1]));
    return <div className="content columns">
        <div className="column">
            <h2>Operator</h2>
            <table className="table">
                <thead><tr><th>Key</th><th>Value</th></tr></thead>
                <tbody>
                    {Object.keys(operator).map(key => <tr key={key}><td>{key}</td><td>{typeof operator[key] === 'number' ? to_date_string(operator[key]) : operator[key]}</td></tr>)}
                </tbody>
            </table>
            <button type="button" className="button" onClick={() => {
                sessionStorage.removeItem('token');
                setApp(new app_context_t());
            }}>Log out</button>
        </div>
        <div className="column">
            <h2>Available case types</h2>
            <div className="fixed-grid has-1-cols">
                <div className="grid">
                    {app.case_types.map(type => <button type="button" className="button cell" key={type.id} onClick={async () => {
                        await create_case(app, type.id);
                        setTab('Main');
                        setApp({ ...app });
                    }}>{type.name}</button>)}
                </div>
            </div>
        </div>
    </div>
}

// Draws the debug user interface.
export function Draw_tabbed_window(props: { app: app_context_t, setApp: Function }) {
    const { app, setApp } = props;
    const [activeTab, setActiveTab] = useState<string>('Infinity');
    const tabs = [];
    tabs.push({ title: 'Infinity', page: <Draw_infinity app={app} setApp={setApp} setTab={setActiveTab} key="Infinity" /> });
    tabs.push({ title: 'Clipboard', page: <Draw_clipboard app={app} setApp={setApp} setTab={setActiveTab} key="Clipboard" /> });
    if ([app_status_t.open_case, app_status_t.open_assignment, app_status_t.open_action].includes(app.status)) {
        tabs.push({ title: 'Main', page: <Draw_main_window app={app} setApp={setApp} key="Main" /> });
    }
    if (app.dx_request_queue.length > 0) {
        tabs.push({ title: 'Calls', page: <Draw_debug_calls app={app} key="Calls" /> });
    }
    if (Array.from(app.resources.components.keys()).length > 0) {
        tabs.push({ title: 'Structure', page: <Draw_debug_components app={app} key="Structure" /> });
    }
    if (Array.from(app.resources.fields.keys()).length > 0) {
        tabs.push({ title: 'Fields', page: <Draw_debug_fields app={app} key="Fields" /> });
    }
    if (Array.from(app.case_info.content.keys()).length > 0) {
        tabs.push({ title: 'Content', page: <Draw_debug_content app={app} key="Content" /> });
    }

    return <div className="card">
        <div className="card-content">
            <div className="tabs is-centered">
                <ul>
                    {tabs.map(tab => <li key={tab.title}
                        className={activeTab === tab.title ? 'is-active' : ''}
                        onClick={() => setActiveTab(tab.title)}>
                        <a>{tab.title}</a>
                    </li>)}
                </ul>
            </div>
            <div key={app.status}>
                {tabs.find(tab => tab.title === activeTab)?.page}
            </div>
        </div>
    </div>
}
