// nvsStore.cpp — Preferences wrapper implementation.
#include "nvsStore.h"

namespace nvs
{
    bool withWrite(const char *ns, void (*fn)(Preferences &prefs))
    {
        Preferences prefs;
        if (!prefs.begin(ns, false))
        {
            Serial.printf("[NVS] Failed to open '%s' for write\n", ns);
            return false;
        }
        fn(prefs);
        prefs.end();
        return true;
    }

    bool withRead(const char *ns, void (*fn)(Preferences &prefs))
    {
        Preferences prefs;
        if (!prefs.begin(ns, true))
        {
            Serial.printf("[NVS] Failed to open '%s' for read\n", ns);
            return false;
        }
        fn(prefs);
        prefs.end();
        return true;
    }

    bool getBool(const char *ns, const char *key, bool def)
    {
        return getInt(ns, key, def ? 1 : 0) != 0;
    }

    int getInt(const char *ns, const char *key, int def)
    {
        int out = def;
        Preferences prefs;
        if (prefs.begin(ns, true))
        {
            out = prefs.getInt(key, def);
            prefs.end();
        }
        else
        {
            Serial.printf("[NVS] Failed to open '%s' for read\n", ns);
        }
        return out;
    }

    String getString(const char *ns, const char *key, const char *def)
    {
        String out = String(def);
        Preferences prefs;
        if (prefs.begin(ns, true))
        {
            out = prefs.getString(key, def);
            prefs.end();
        }
        else
        {
            Serial.printf("[NVS] Failed to open '%s' for read\n", ns);
        }
        return out;
    }

    void putBool(const char *ns, const char *key, bool v)
    {
        putInt(ns, key, v ? 1 : 0);
    }

    void putInt(const char *ns, const char *key, int v)
    {
        Preferences prefs;
        if (prefs.begin(ns, false))
        {
            prefs.putInt(key, v);
            prefs.end();
        }
        else
        {
            Serial.printf("[NVS] Failed to open '%s' for write\n", ns);
        }
    }

    void putString(const char *ns, const char *key, const String &v)
    {
        Preferences prefs;
        if (prefs.begin(ns, false))
        {
            prefs.putString(key, v);
            prefs.end();
        }
        else
        {
            Serial.printf("[NVS] Failed to open '%s' for write\n", ns);
        }
    }

    void putBytes(const char *ns, const char *key, const void *buf, size_t len)
    {
        Preferences prefs;
        if (prefs.begin(ns, false))
        {
            prefs.putBytes(key, buf, len);
            prefs.end();
        }
        else
        {
            Serial.printf("[NVS] Failed to open '%s' for write\n", ns);
        }
    }

    void getBytes(const char *ns, const char *key, void *buf, size_t len)
    {
        Preferences prefs;
        if (prefs.begin(ns, true))
        {
            prefs.getBytes(key, buf, len);
            prefs.end();
        }
        else
        {
            Serial.printf("[NVS] Failed to open '%s' for read\n", ns);
        }
    }
}
