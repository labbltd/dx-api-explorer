# DX API Explorer


https://github.com/user-attachments/assets/39a573e8-dd27-4138-9664-4566b90eab43


This app helps you understand [Pega's Constellation DX API](https://docs.pega.com/bundle/dx-api/page/platform/dx-api/dx-api-version-2-con.html) by showing it in action. It's a desktop application that let's you work through a [case](https://docs.pega.com/bundle/platform/page/platform/case-management/case-management-overview.html), providing detailed developer information so you know *exactly* what the DX API is doing every step of the way.

## Key points

- Custom front-end to [Pega Infinity](https://www.pega.com/infinity) applications that use [Constellation UI](https://docs.pega.com/bundle/platform/page/platform/user-experience/constellation-architecture.html).
- Can work through a full [case lifecycle](https://docs.pega.com/bundle/platform/page/platform/case-management/case-life-cycle-elements.html), from creation to resolution.
- Complete request and response information for each API call.
- Inspectors for components, fields, and contents.
- Saves/loads configuration in `dx_api_explorer_config.json` — keep multiple versions on hand and swap them in to connect to different Pega instances.
- Startup and shutdown are nearly instantaneous, and uses almost zero CPU.
- Ships with all dependencies included, except for the [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) (make sure you have this installed).
- Implemented in a straightforward funcs-and-structs style that readily translates to different languages and paradigms.
- No Windows-specific dependencies, should build on OSX and Linux with just a little bit of work.

## Usage

### Purpose
This is a developer tool, and a personal side project. It is not intended for production use, and has not been properly tested/hardened/etc — therefore no support is offered. It *is* intended to help you understand the DX API in particular, and [Pega's Center-out® architecture](https://www.pega.com/technology/center-out) in general.

### Installation

Just unzip it and go. You only need to ensure that you have a compatible Infinity application with an appropriately configured [OAuth 2.0 client registration](https://docs.pega.com/bundle/platform/page/platform/security/set-up-oauth-2-client-registration.html). It should work on any recent version of Infinity, but I have only tested it on 24.

### OAuth 2.0 client registration

You need to set this up in your Pega Infinity instance. It specifies how an external application can connect to Infinity. To get going quickly, here's the tl;dr—
- Important stuff:
	- Type of client: `Confidential`
	- Supported grant types: `Password credentials`
		- Identity mapping: `pyDefaultIdentityMappingForPasswordGrant`
		- Enable refresh token: `true`
	- **Click "View & Download" before save!**
- Other things to double check (but likely do not matter):
	- Refresh token: `Issue once and keep until expiry`
	- Access token lifetime (in seconds): `3600`
	- Refresh token lifetime (in seconds): `86400`
	- JWT generation profile: `pyDefaultUserInfoMapping`

### Connection

Upon startup, the application will present a form for connection information. If this is your first time running the application, there will be no config file and the form will be blank. Fill it out and and click `Login` to connect.

* Server: e.g. `https://my-pega-instance.example.com`.
* DX API Path: for default app, usually `/prweb/api/application/v2`.
	* To connect to a specific application, use `/prweb/app/{app-alias}/api/application/v2`
* Token Endpoint: usually `/prweb/PRRestService/oauth2/v1/token`.
* Client ID and Client Secret: obtained from the OAuth 2.0 client registration.
* User ID and Password: for [a supported user](https://docs.pega.com/bundle/dx-api/page/platform/dx-api/security-settings-v2.html).

Note that you will see information about the connection call in the debug window. This window will continuously update itself with each call. This shows you how to chain together DX API calls to get work done.

### Working through a case

Once connected, you will see a `Create` menu in the main window. This will allow you to refresh the list of available case types; having done that, you can then use it to create cases.

An open case shows buttons for its open assignments. Click a button to open an assignment. In turn, an open assignment shows buttons for opening the available actions.

An open action shows a form built out dynamically as specified by the DX API. Fill the form out and submit it to complete the assignment and close it out, updating the case information. Keep working in this way to progress through a complete case lifecycle.

### Debug window
The additional tabs in the debug window are very handy for programmatically understanding the structure, fields, and content.

In particular, the `Structure` tab is helpful for understanding how to interpret the `uiMetadata` portion of the DX API Response. Click on a component to select it and its JSON. For a visible component, this will also change the color of the info marker (the `(?)` icon) for that component in the main window. You can also click on the info marker to select the component.

### Customizing
The view menu lets you toggle additional windows. You can move and resize the windows, and the app will remember these settings. You can also return to the default layout at any time. And there's a toggle to enable an [XRay visualization akin to that provided by Constellation](https://docs.pega.com/bundle/platform/page/platform/user-experience/debugging-UI.html).

You can adjust the font size to taste. The app will attempt to select something reasonable on first run, but the current method is very crude and you will likely find that an alternative size is more suitable.

## Miscellaneous

### Current level of DX API support

A minimal set of components and field attributes are supported:

- Components:
	- Reference (class context only)
	- Region
	- View
	- TextArea
	- TextInput
	- DefaultForm

- Field attributes (**boolean only**):
	- Readonly
	- Required
	- Disabled

### Licensing and libraries

This app's code is released into the public domain. The included libraries all ship with unencumbered licenses. These libraries are:

- [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [JSON for Modern C++](https://github.com/nlohmann/json)
- [cpp-httplib](https://github.com/yhirose/cpp-httplib)
- [OpenSSL](https://github.com/openssl/openssl)


### Contributing

Bugs are likely — issues and pull requests are welcome. As this is a side project and time is short, code which is not in harmony with the general style of the codebase will have to be rejected. Contributing to this project means assenting to said contributions being released into the public domain.

Thanks to [Daniël Wedema](https://github.com/danielwedema) for the idea to incorporate the X-Ray feature.