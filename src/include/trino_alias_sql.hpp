#pragma once

namespace duckdb {

// Registers the trino_<name> macros and trino_meta() table macro on the
// DatabaseInstance the extension is loaded into. Implementation in
// alias_macros_loader.cpp; macro literals in macro_definitions.cpp.
//
// (The historical name of this header survives — the actual SQL embed it
//  used to declare is gone now, replaced by native DefaultMacro arrays.)
void RegisterAliasMacros(class ExtensionLoader &loader);

} // namespace duckdb
