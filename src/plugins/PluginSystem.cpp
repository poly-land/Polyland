#include "PluginSystem.hpp"

#include <dlfcn.h>
#include "../Compositor.hpp"

CPluginSystem::CPluginSystem() {
    g_pFunctionHookSystem = std::make_unique<CHookSystem>();
}

CPlugin* CPluginSystem::loadPlugin(const std::string& path) {

    if (getPluginByPath(path)) {
        Debug::log(ERR, " [PluginSystem] Cannot load a plugin twice!");
        return nullptr;
    }

    auto* const PLUGIN = m_vLoadedPlugins.emplace_back(std::make_unique<CPlugin>()).get();

    PLUGIN->path = path;

    HANDLE MODULE = dlopen(path.c_str(), RTLD_LAZY);

    if (!MODULE) {
        Debug::log(ERR, " [PluginSystem] Plugin %s could not be loaded.", path.c_str());
        m_vLoadedPlugins.pop_back();
        return nullptr;
    }

    PLUGIN->m_pHandle = MODULE;

    PPLUGIN_API_VERSION_FUNC apiVerFunc = (PPLUGIN_API_VERSION_FUNC)dlsym(MODULE, PLUGIN_API_VERSION_FUNC_STR);
    PPLUGIN_INIT_FUNC        initFunc   = (PPLUGIN_INIT_FUNC)dlsym(MODULE, PLUGIN_INIT_FUNC_STR);

    if (!apiVerFunc || !initFunc) {
        Debug::log(ERR, " [PluginSystem] Plugin %s could not be loaded. (No apiver/init func)", path.c_str());
        dlclose(MODULE);
        m_vLoadedPlugins.pop_back();
        return nullptr;
    }

    const std::string PLUGINAPIVER = apiVerFunc();

    if (PLUGINAPIVER != HYPRLAND_API_VERSION) {
        Debug::log(ERR, " [PluginSystem] Plugin %s could not be loaded. (API version mismatch)", path.c_str());
        dlclose(MODULE);
        m_vLoadedPlugins.pop_back();
        return nullptr;
    }

    PLUGIN_DESCRIPTION_INFO PLUGINDATA;

    try {
        if (!setjmp(m_jbPluginFaultJumpBuf))
            PLUGINDATA = initFunc(MODULE);
        else {
            // this module crashed.
            throw std::exception();
        }
    } catch (std::exception& e) {
        Debug::log(ERR, " [PluginSystem] Plugin %s (Handle %lx) crashed in init. Unloading.", path.c_str(), MODULE);
        unloadPlugin(PLUGIN, true); // Plugin could've already hooked/done something
        return nullptr;
    }

    PLUGIN->author      = PLUGINDATA.author;
    PLUGIN->description = PLUGINDATA.description;
    PLUGIN->version     = PLUGINDATA.version;
    PLUGIN->name        = PLUGINDATA.name;

    Debug::log(LOG, " [PluginSystem] Plugin %s loaded. Handle: %lx, path: \"%s\", author: \"%s\", description: \"%s\", version: \"%s\"", PLUGINDATA.name.c_str(), MODULE,
               path.c_str(), PLUGINDATA.author.c_str(), PLUGINDATA.description.c_str(), PLUGINDATA.version.c_str());

    return PLUGIN;
}

void CPluginSystem::unloadPlugin(const CPlugin* plugin, bool eject) {
    if (!plugin)
        return;

    if (!eject) {
        PPLUGIN_EXIT_FUNC exitFunc = (PPLUGIN_EXIT_FUNC)dlsym(plugin->m_pHandle, PLUGIN_EXIT_FUNC_STR);
        if (exitFunc)
            exitFunc();
    }

    for (auto& [k, v] : plugin->registeredCallbacks)
        g_pHookSystem->unhook(v);

    for (auto& l : plugin->registeredLayouts)
        g_pLayoutManager->removeLayout(l);

    g_pFunctionHookSystem->removeAllHooksFrom(plugin->m_pHandle);

    for (auto& d : plugin->registeredDecorations)
        HyprlandAPI::removeWindowDecoration(plugin->m_pHandle, d);

    dlclose(plugin->m_pHandle);

    Debug::log(LOG, " [PluginSystem] Plugin %s unloaded.", plugin->name.c_str());

    std::erase_if(m_vLoadedPlugins, [&](const auto& other) { return other->m_pHandle == plugin->m_pHandle; });
}

CPlugin* CPluginSystem::getPluginByPath(const std::string& path) {
    for (auto& p : m_vLoadedPlugins) {
        if (p->path == path)
            return p.get();
    }

    return nullptr;
}

CPlugin* CPluginSystem::getPluginByHandle(HANDLE handle) {
    for (auto& p : m_vLoadedPlugins) {
        if (p->m_pHandle == handle)
            return p.get();
    }

    return nullptr;
}

std::vector<CPlugin*> CPluginSystem::getAllPlugins() {
    std::vector<CPlugin*> results(m_vLoadedPlugins.size());
    for (size_t i = 0; i < m_vLoadedPlugins.size(); ++i)
        results[i] = m_vLoadedPlugins[i].get();
    return results;
}