#ifndef TRINO_PARITY_ICU_UCONFIG_LOCAL_H
#define TRINO_PARITY_ICU_UCONFIG_LOCAL_H

// Keep C entry points, C++ namespaces, and the data symbol distinct from both
// DuckDB's bundled ICU and any system ICU, including another ICU 76 build.
#define U_DISABLE_RENAMING 0
#define U_DISABLE_VERSION_SUFFIX 0
#define U_LIB_SUFFIX_C_NAME _trino_parity
#define U_STATIC_IMPLEMENTATION
// This global C++ helper is outside ICU's public symbol-renaming table. Hidden
// visibility alone cannot prevent collisions when two ICUs share a static link.
#define UCaseMap UCaseMap_trino_parity_76

// Preserve the reduced feature configuration of the former DuckDB snapshot.
#define UCONFIG_NO_BREAK_ITERATION 1
#define UCONFIG_NO_IDNA 1
#define UCONFIG_NO_CONVERSION 1
#define UCONFIG_NO_TRANSLITERATION 1
#define UCONFIG_NO_REGULAR_EXPRESSIONS 1
#define UCONFIG_NO_SERVICE 1

// Only compiled-in data is used. No ICU_DATA lookup or dynamic plugin loading.
#define UCONFIG_NO_FILE_IO 1
#define U_ENABLE_DYLOAD 0
#define UCONFIG_ENABLE_PLUGINS 0

#endif
