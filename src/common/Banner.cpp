#include "Banner.h"
#include "GitRevision.h"
#include "StringFormat.h"

void Trinity::Banner::Show(char const* applicationName, void(*log)(char const* text), void(*logExtraInfo)())
{
    log(Trinity::StringFormat("{} ({})", GitRevision::GetFullVersion(), applicationName).c_str());
    log(R"(<Ctrl-C> to stop.)" "\n");

    // Warspire ASCII logo
    log(R"( __      __               _________      .__                )");
    log(R"(/  \    /  \_____ _______/   _____/_____ |__|______   ____  )");
    log(R"(\   \/\/   /\__  \\_  __ \_____  \\____ \|  \_  __ \_/ __ \ )");
    log(R"( \        /  / __ \|  | \/        \  |_> >  ||  | \/\  ___/ )");
    log(R"(  \__/\  /  (____  /__| /_______  /   __/|__||__|    \___  >)");
    log(R"(       \/        \/             \/|__|                   \/ )");
    log(R"(                 https://warspire.fpr.net/                )" "\n");

    if (logExtraInfo)
        logExtraInfo();
}
