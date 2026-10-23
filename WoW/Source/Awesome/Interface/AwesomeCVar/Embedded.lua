-- Waystone: AwesomeCVar is built into the client (Awesome/AwesomeEmbeddedUi.cpp) and runs as FrameXML, before the
-- AddOns. Its settings, which an AddOn keeps in SavedVariables (AwesomeCVarDB), are client CVars kept in Config.wtf;
-- this table reads and writes them. Loaded first, as the addon's own files take AwesomeCVarDB as they find it.
local _, ACVar = ...

ACVar.ADDON_VERSION = "3.0-waystone (built in)"

-- the DB field, its CVar, and whether it is a boolean
local FIELDS = {
    showGameMenuButton = { "awesomeGameMenuButton", true },
    showChatMessages = { "awesomeChatMessages", true },
    showVersionAlerts = { "awesomeVersionAlerts", true },
    frameWidth = { "awesomeFrameWidth", false },
    frameHeight = { "awesomeFrameHeight", false },
}

local MINIMAP_FIELDS = {
    hide = { "awesomeMinimapHide", true },
    minimapPos = { "awesomeMinimapPos", false },
}

local function cvarTable(fields, children)
    return setmetatable({}, {
        __index = function(_, key)
            if children and children[key] then
                return children[key]
            end

            local field = fields[key]
            local value = field and GetCVar(field[1])

            if value == nil or value == "" then
                return nil
            end

            if field[2] then
                return value ~= "0"
            end

            return tonumber(value)
        end,
        __newindex = function(_, key, value)
            local field = fields[key]

            -- the nested minimap table is fixed; anything else the addon does not keep is dropped
            if not field then
                return
            end

            if value == nil then
                SetCVar(field[1], "")
            elseif type(value) == "boolean" then
                SetCVar(field[1], value and "1" or "0")
            else
                SetCVar(field[1], tostring(value))
            end
        end,
    })
end

AwesomeCVarDB = cvarTable(FIELDS, { minimap = cvarTable(MINIMAP_FIELDS) })
