// events.cpp — event bus implementation.
#include "events.h"

namespace events
{
    static EventHandler handlers[MAX_HANDLERS] = {nullptr};
    static int count = 0;

    bool subscribe(EventHandler h)
    {
        if (count >= MAX_HANDLERS || h == nullptr)
            return false;
        handlers[count++] = h;
        return true;
    }

    void emit(Evt evt, void *ctx)
    {
        for (int i = 0; i < count; i++)
        {
            if (handlers[i])
                handlers[i](evt, ctx);
        }
    }

    int handlerCount()
    {
        return count;
    }
}