import { useState } from "react";
import { app_context_t } from "./dx_api_app_types";
import { Draw_main_menu, Draw_tabbed_window } from "./dx_api_draw_procs";

export default function Explorer() {
    const [app, setApp] = useState<app_context_t>(new app_context_t());
    (window as any).fillOAuth = () => {
        app.server = 'http://localhost:3333';
        app.oauth2 = {
            grant_type: 'authCode',
            client_id: '12345',
            client_secret: '54321',
            token_endpoint: 'http://localhost:3333/prweb/PRRestService/oauth2/v1/token',
            authorization_endpoint: 'http://localhost:3333/prweb/PRRestService/oauth2/v1/authorize',
            redirect_uri: `${window.location.origin + window.location.pathname}authDone.html`,
            user_id: 'daniel.wedema@labb.ltd',
            auth_service: 'pega',
            password: 'password',
            no_pkce: '',
            app_alias: ''
        }
        setApp({ ...app });
    }
    return <>
        <Draw_main_menu />
        <div className="cell">
            <Draw_tabbed_window app={app} setApp={setApp} />
        </div>
        {app.flash && <div className="notification is-primary">
            <button className="delete" onClick={() => { app.flash = ''; setApp({ ...app }) }}></button>
            {app.flash}
        </div>}
        {/* <pre>{JSON.stringify(app, null, 2)}</pre> */}
    </>
}