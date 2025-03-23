import { component_type_t } from "./dx_api_model_types";

// Return lowercase string. "Good enough is good enough."
export function to_lower(str: string): string {
    return str.toLowerCase();
}

// This converts a string to an enum type identifier. It assumes that:
//  - The first enum is 0 and corresponds to "unspecified."
//  - The second enum is 1 and corresponds to "unknown."
//  - The provided enum strings correspond 1:1 to the count of the provided enum type.
//
// Usage looks like:
//  enum_type_t result = to_enum<enum_type_t>(str, enum_type_strings);
//
// You shouldn't have to specifiy the enum count because that should be inferred
// from the enum strings parameter.

export function to_enum(str: string, enum_strings: Array<string>): component_type_t {
    const value = Object.values(component_type_t)[enum_strings.map(s => s.toLowerCase()).indexOf(str.toLowerCase())];
    return component_type_t[value as keyof typeof component_type_t] || component_type_t.component_type_unknown;
}

// Robustly extracts a boolean value from the provided JSON.
export function to_bool(j: any): boolean {
    if (typeof j === 'boolean') return j;

    if (typeof j === 'string') {
        const str = j.toLowerCase();
        if (str == "true") return true;
    }

    return false;
}
