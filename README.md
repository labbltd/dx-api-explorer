# README

This is a work in progress...

## Client Registration Setup
- Important stuff:
	- Type of client: Confidential
	- Supported grant types: Password credentials
		- Identity mapping: pyDefaultIdentityMappingForPasswordGrant
		- Enable refresh token: true
	- CLICK VIEW & DOWNLOAD BEFORE SAVE!
- Refresh token: Issue once and keep until expiry
- Access token lifetime (in seconds): 3600
- Refresh token lifetime (in seconds): 86400
- JWT generation profile: pyDefaultUserInfoMapping


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