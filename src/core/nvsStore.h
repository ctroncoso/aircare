// nvsStore.h — thin, typed wrapper around Preferences (NVS).
// Removes the repeated prefs.begin/put/end + Serial boilerplate that was
// duplicated across configHelper.h and scheduleHelper.h.
#pragma once

#include <Preferences.h>
#include <Arduino.h>

namespace nvs
{
    // Open a namespace for writing, invoke fn(prefs), then close.
    // fn should perform the puts. Returns true if the namespace opened.
    bool withWrite(const char *ns, void (*fn)(Preferences &prefs));

    // Open a namespace for reading, invoke fn(prefs), then close.
    // fn should perform the gets. Returns true if the namespace opened.
    bool withRead(const char *ns, void (*fn)(Preferences &prefs));

    // Convenience typed accessors (each opens/closes the namespace itself).
    bool getBool(const char *ns, const char *key, bool def);
    int  getInt(const char *ns, const char *key, int def);
    String getString(const char *ns, const char *key, const char *def);
    void putBool(const char *ns, const char *key, bool v);
    void putInt(const char *ns, const char *key, int v);
    void putString(const char *ns, const char *key, const String &v);

    // Raw byte-blob accessors (each opens/closes the namespace itself).
    void putBytes(const char *ns, const char *key, const void *buf, size_t len);
    void getBytes(const char *ns, const char *key, void *buf, size_t len);
}
