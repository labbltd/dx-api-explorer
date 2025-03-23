// This script file is retrieved by framework components looking to implement a
// redirect uri route for an authorization code grant (with or without PKCE) flow.
// It attempts to pass back the received code, state and error using 3 techniques
// 1) Try to call directly into a callback function (won't work across servers)
// 2) Try to call directly to postMessage (won't work across domains)
// 3) Awaits a message from the host page and when received immediately postMessages back with the code
// This script MUST NOT invoke the authDone function directly, but rather just make it available as an
//  exported function.  An alternate source file (authDone.js) duplicates the same authDone function
//  and directly invokes it (and is used by the authDone.html unauthenticated endpoint used by Web Embed).
// IMPORTANT: Any mods to authDone function must be made within authDone.js as well.
export const authDone = () => {
    // For enabling logging via debugger (do not let it be a const...then can't change in debugger)
    // eslint-disable-next-line prefer-const
    let bDebug = false;
    const queryString = window.location.search;
    const urlParams = new URLSearchParams(queryString);
    const code = urlParams.get('code');
    const state = urlParams.get('state');
    const error = urlParams.get('error');
    const errorDesc = urlParams.get('error_description');
    // Silent authentication will work with just regular console.log
    // eslint-disable-next-line no-console
    let fnLog = console.log;
    try {
        // Check if in a popup window.  If so use main window console.
        if (window.opener.console.log) {
            fnLog = window.opener.console.log;
        }
        // eslint-disable-next-line no-empty
    }
    catch (e0) { }
    const doLog = arg => {
        if (bDebug) {
            fnLog(arg);
        }
    };
    // doLog('Testing do Log');
    let bSuccess = false;
    const getEmbedOriginFromState = () => {
        let embedOrigin = null;
        try {
            // Expect state to contain the embedding page's origin followed by random state separated by '.'
            if (state) {
                embedOrigin = window.atob(state.split('.')[0]);
            }
            // eslint-disable-next-line no-empty
        }
        catch (e) { }
        if (!embedOrigin) {
            embedOrigin = window.location.origin;
        }
        return embedOrigin;
    };
    if (code || error) {
        try {
            window.opener.authCodeCallback(code, state, error, errorDesc);
            bSuccess = true;
        }
        catch (e) {
            doLog('auth.html: Failed to directly access authCodeCallback.');
        }
        // Post messages require a targetDomain...trying to pass this via state
        const embedOrigin = getEmbedOriginFromState();
        if (!bSuccess) {
            try {
                window.opener.postMessage({ type: 'PegaAuth', code, state, error, errorDesc }, embedOrigin);
                bSuccess = true;
            }
            catch (e) {
                doLog('auth.html: Failed to directly post message to opener');
            }
        }
        if (!bSuccess) {
            window.addEventListener('message', event => {
                doLog('authDone.js: received PegaAuth message');
                if (event.data && event.data.type && event.data.type === 'PegaAuth') {
                    doLog('authDone.js: posting message back');
                    event.source.postMessage({ type: 'PegaAuth', code, state, error, errorDesc }, embedOrigin);
                }
            });
        }
    }
};
//# sourceMappingURL=authCodeDone.js.map