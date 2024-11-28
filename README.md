# DX API Explorer

This app helps you understand [Pega's Constellation DX API](https://docs.pega.com/bundle/dx-api/page/platform/dx-api/dx-api-version-2-con.html) by showing it in action. It's a desktop application that let's you work through a [case](https://docs.pega.com/bundle/platform/page/platform/case-management/case-management-overview.html), providing detailed developer information so you know *exactly* what the DX API is doing every step of the way.

## Key points:

* Custom front-end to [Pega Infinity](https://www.pega.com/infinity) applications that use [Constellation UI](https://docs.pega.com/bundle/platform/page/platform/user-experience/constellation-architecture.html).
* Can work through a full [case lifecycle](https://docs.pega.com/bundle/platform/page/platform/case-management/case-life-cycle-elements.html), from creation to resolution.
* Complete request and response information for each API call.
* Inspectors for components, fields, and contents.
* Saves/loads configuration in `dx_api_explorer_config.json` — keep multiple versions on hand and swap them in to connect to different Pega instances.
* Startup and shutdown are nearly instantaneous, and uses almost zero CPU.
* Ships with all dependencies included, except for the [Microsoft Visual C++ redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) (make sure you have this installed).
* Implemented in a single `main.cpp` file in a straightforward funcs-and-structs style that readily translates to different languages and paradigms.
* No Windows-specific dependencies, should build on OSX and Linux with just a little bit of work.

## Installation

Just unzip it and go. You only need to ensure that you have a compatible Infinity application and a with an appropriately configured [OAuth 2.0 client registration](https://docs.pega.com/bundle/platform/page/platform/security/set-up-oauth-2-client-registration.html). It should work on any recent version of Infinity, but I have only tested it on 24.

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

## Usage

Upon startup, the application will present a form for connection information. If this is your first time running the application, there will be no config file and the form will be blank. Fill it out and submit it to connect.

* Server: e.g. `https://my-pega-instance.example.com`.
* DX API Path: usually `/prweb/api/application/v2`.
* Token Endpoint: usually `/prweb/PRRestService/oauth2/v1/token`.
* Client ID and Client Secret: obtained from the OAuth 2.0 client registration.


## Application URL
- Default application: `/{server}/prweb/api/application/v2`
- Specific application: `/{server}/prweb/app/{alias}/api/application/v2`

## Current level of support:
- Components:
	- Reference (class context only)
	- Region
	- View
	- TextArea
	- TextInput
	- DefaultForm
- Basic boolean field attributes:
	- Readonly
	- Required
	- Disabled
