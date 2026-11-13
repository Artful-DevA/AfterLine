// SPDX-License-Identifier: MIT
//
// How applications are identified and named, the same on every platform.
#include "audio/PipeWireGraph.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace afterline::audio {

namespace {
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
}  // namespace

std::string NodeInfo::label() const
{
    if (!description.empty())
        return description;
    if (!nick.empty())
        return nick;
    return name;
}

std::string appKey(const NodeInfo &n)
{
    if (!n.appId.empty())
        return "app:" + n.appId;
    if (!n.appBinary.empty()) {
        std::string b = lower(n.appBinary);
        // Browsers and Electron apps run audio in helper processes whose
        // binary names differ from the main one.
        for (const char *suffix : {"-bin", "-wrapped", ".bin", ".exe"}) {
            const size_t len = std::strlen(suffix);
            if (b.size() > len && b.compare(b.size() - len, len, suffix) == 0)
                b.erase(b.size() - len);
        }
        if (b == "chromium-browser")
            b = "chromium";
        if (b == "ms-teams" || b == "msteams")
            b = "teams";
        if (b == "google-chrome" || b == "google-chrome-stable")
            b = "chrome";
        return "bin:" + b;
    }
    if (!n.appName.empty())
        return "name:" + lower(n.appName);
    return {};
}

std::string appDisplayName(const NodeInfo &n)
{
    const std::string key = appKey(n);
    struct Known { const char *needle; const char *name; };
    static const Known known[] = {
        {"firefox", "Firefox"}, {"librewolf", "LibreWolf"}, {"chrome", "Google Chrome"},
        {"chromium", "Chromium"}, {"brave", "Brave"}, {"msedge", "Microsoft Edge"},
        {"vivaldi", "Vivaldi"}, {"opera", "Opera"}, {"zoom", "Zoom"}, {"discord", "Discord"},
        {"teams", "Microsoft Teams"}, {"slack", "Slack"}, {"skype", "Skype"},
        {"signal", "Signal"}, {"telegram", "Telegram"}, {"webex", "Webex"},
        {"jitsi", "Jitsi Meet"}, {"element", "Element"}, {"mumble", "Mumble"},
        {"obs", "OBS Studio"}, {"spotify", "Spotify"}, {"vlc", "VLC"}, {"mpv", "mpv"},
    };
    const std::string lk = lower(key);
    for (const auto &k : known)
        if (lk.find(k.needle) != std::string::npos)
            return k.name;
    if (!n.appName.empty())
        return n.appName;
    if (!n.appBinary.empty())
        return n.appBinary;
    return n.label();
}

std::string appIconName(const NodeInfo &n)
{
    if (!n.appIcon.empty())
        return n.appIcon;
    if (!n.appId.empty())
        return n.appId;
    const std::string key = lower(appKey(n));
    struct Known { const char *needle; const char *icon; };
    static const Known known[] = {
        {"firefox", "firefox"}, {"chrome", "google-chrome"}, {"chromium", "chromium"},
        {"brave", "brave-browser"}, {"msedge", "microsoft-edge"}, {"zoom", "Zoom"},
        {"discord", "discord"}, {"teams", "teams-for-linux"}, {"slack", "slack"},
        {"spotify", "spotify"}, {"vlc", "vlc"}, {"signal", "signal-desktop"},
        {"telegram", "telegram"},
    };
    for (const auto &k : known)
        if (key.find(k.needle) != std::string::npos)
            return k.icon;
    return n.appBinary.empty() ? std::string("audio-x-generic") : lower(n.appBinary);
}

}  // namespace afterline::audio
