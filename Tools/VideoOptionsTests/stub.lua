-- A stand-in for the game client under the Video options code (run by video_options_tests.py with lupa's Lua 5.1, the
-- client's dialect): frames and regions, the slider's value rules (CSimpleSlider::SetValue, snapping to the step and
-- to both ends), a CVar store, the dropdown menu functions, and a layout pass that turns anchors into rectangles.
-- Not part of the client; nothing under Tools/ is embedded.

H = { errors = {}, cvars = {}, popups = {}, restarts = 0, sounds = 0 }

local function record(where, err)
	H.errors[#H.errors + 1] = tostring(where) .. ": " .. tostring(err)
end
H.record = record

-- [[ CVars ]] --

-- H.defineCVar(name, default, info): info = { min, max, float, format } for an AwesomeCVar (clamps on set as
-- AwesomeCVar::Apply does), nil for an engine CVar (taken as set)
function H.defineCVar(name, default, info)
	H.cvars[string.lower(name)] = { name = name, value = default, default = default, info = info }
end

local function cvar(name)
	local entry = H.cvars[string.lower(name)]

	if not entry then
		error("Couldn't find CVar named '" .. tostring(name) .. "'", 3)
	end

	return entry
end

function GetCVar(name)
	local entry = H.cvars[string.lower(name)]
	return entry and entry.value
end

function GetCVarDefault(name)
	return cvar(name).default
end

function GetCVarBool(name)
	return GetCVar(name) == "1" and 1 or nil
end

function SetCVar(name, value, event)
	local entry = cvar(name)
	local text = tostring(value)
	local info = entry.info

	if info then
		local number = tonumber(text) or 0

		if not info.float then
			number = number >= 0 and math.floor(number) or math.ceil(number)
		end

		local clamped = math.min(math.max(number, info.min), info.max)

		if clamped ~= number then
			text = info.float and string.format(info.format or "%.2f", clamped) or string.format("%d", clamped)
		end
	end

	entry.value = text
	entry.sets = (entry.sets or 0) + 1
end

-- the stock GetCVarMin / GetCVarMax (GameUI_Scripts.cpp): only extShadowQuality and farclip have a range
function GetCVarMin(name)
	cvar(name)
	local lower = string.lower(name)

	if lower == "extshadowquality" then return 0 end
	if lower == "farclip" then return 177 end
	return nil
end

function GetCVarMax(name)
	cvar(name)
	local lower = string.lower(name)

	if lower == "extshadowquality" then return 5 end
	if lower == "farclip" then return 1277 end
	return nil
end

-- [[ Frames ]] --

H.named = {}
H.frames = {}

local Methods = {}
H.Methods = Methods

local function newMethods(base)
	local methods = {}
	Methods[#Methods + 1] = methods

	if base then
		setmetatable(methods, { __index = base })
	end

	return methods
end

local Region = newMethods()
local FontString = newMethods(Region)
local Texture = newMethods(Region)
local Frame = newMethods(Region)
local Button = newMethods(Frame)
local CheckButton = newMethods(Button)
local Slider = newMethods(Frame)

H.types = { frame = Frame, button = Button, checkbutton = CheckButton, slider = Slider, scrollframe = Frame,
	editbox = Frame, statusbar = Frame, gametooltip = Frame, fontstring = FontString, texture = Texture }
local kindNames = { frame = "Frame", button = "Button", checkbutton = "CheckButton", slider = "Slider",
	scrollframe = "ScrollFrame", editbox = "EditBox", statusbar = "StatusBar", gametooltip = "GameTooltip",
	fontstring = "FontString", texture = "Texture" }

function H.newObject(kind, name, parent)
	kind = kindNames[string.lower(kind)] or kind
	local methods = H.types[string.lower(kind)] or Frame
	local object = setmetatable({}, { __index = methods })
	object.__kind = kind
	object.__name = name
	object.__parent = parent
	object.__points = {}
	object.__shown = true
	object.__scripts = {}
	object.__events = {}
	object.__enabled = true
	object.__children = {}

	if parent then
		table.insert(parent.__children, object)
	end

	if name then
		_G[name] = object
		H.named[name] = object
	end

	H.frames[#H.frames + 1] = object
	return object
end

function Region:GetName() return self.__name end
function Region:GetParent() return self.__parent end
function Region:GetObjectType() return self.__kind end
function Region:IsObjectType(kind) return self.__kind == kind end
function Region:SetPoint(point, relativeTo, relativePoint, x, y)
	if type(relativeTo) == "number" then
		relativeTo, relativePoint, x, y = nil, nil, relativeTo, relativePoint
	end
	if type(relativeTo) == "string" then
		relativeTo = _G[relativeTo]
	end
	if type(relativePoint) == "number" then
		relativePoint, x, y = nil, relativePoint, x
	end
	point = string.upper(point)
	self.__points[point] = { relativeTo or self.__parent, string.upper(relativePoint or point), x or 0, y or 0 }
end
function Region:ClearAllPoints() self.__points = {} end
function Region:SetAllPoints(relative)
	relative = relative or self.__parent
	self.__points = { TOPLEFT = { relative, "TOPLEFT", 0, 0 }, BOTTOMRIGHT = { relative, "BOTTOMRIGHT", 0, 0 } }
end
function Region:SetWidth(w) self.__w = w end
function Region:SetHeight(h) self.__h = h end
function Region:SetSize(w, h) self.__w, self.__h = w, h end
function Region:GetWidth() local l, b, w = H.rect(self) return w or self.__w or 0 end
function Region:GetHeight() local l, b, w, h = H.rect(self) return h or self.__h or 0 end
function Region:Show()
	local wasVisible = self:IsVisible()
	self.__shown = true
	if not wasVisible and self:IsVisible() then H.fireVisibility(self, "OnShow") end
end
function Region:Hide()
	local wasVisible = self:IsVisible()
	self.__shown = false
	if wasVisible then H.fireVisibility(self, "OnHide") end
end
function Region:SetShown(shown) if shown then self:Show() else self:Hide() end end
function Region:IsShown() return self.__shown and 1 or nil end
function Region:IsVisible()
	local object = self
	while object do
		if not object.__shown then return nil end
		object = object.__parent
	end
	return 1
end
function Region:SetAlpha() end
function Region:SetVertexColor(r, g, b) self.__color = { r, g, b } end
function Region:SetDrawLayer() end

function FontString:SetText(text) self.__text = text ~= nil and tostring(text) or nil end
function FontString:GetText() return self.__text end
function FontString:SetFormattedText(fmt, ...) self.__text = string.format(fmt, ...) end
function FontString:SetFontObject(font) self.__font = font end
function FontString:GetFontObject() return H.fontObject end
function FontString:SetTextColor(r, g, b) self.__color = { r, g, b } end
function FontString:GetTextColor() return 1, 0.82, 0 end
function FontString:SetJustifyH() end
function FontString:SetJustifyV() end
function FontString:SetNonSpaceWrap() end
function FontString:SetWordWrap() end
function FontString:GetStringWidth() return H.textWidth(self) end

function Texture:SetTexture(path) self.__texture = path end
function Texture:SetTexCoord() end
function Texture:SetBlendMode() end

function Frame:CreateFontString(name, layer, inherits)
	local fontString = H.newObject("FontString", name and name:gsub("%$parent", self.__name or "") or nil, self)
	fontString.__font = inherits
	return fontString
end
function Frame:CreateTexture(name)
	return H.newObject("Texture", name and name:gsub("%$parent", self.__name or "") or nil, self)
end
function Frame:SetScript(script, fn) self.__scripts[script] = fn end
function Frame:GetScript(script) return self.__scripts[script] end
function Frame:HookScript(script, fn)
	local previous = self.__scripts[script]
	if previous then
		self.__scripts[script] = function (...) previous(...) return fn(...) end
	else
		self.__scripts[script] = fn
	end
end
function Frame:RegisterEvent(event) self.__events[event] = true end
function Frame:UnregisterEvent(event) self.__events[event] = nil end
function Frame:UnregisterAllEvents() self.__events = {} end
function Frame:EnableMouse() end
function Frame:EnableMouseWheel() end
function Frame:SetFrameLevel(level) self.__level = level end
function Frame:GetFrameLevel() return self.__level or 1 end
function Frame:SetFrameStrata() end
function Frame:SetBackdrop() end
function Frame:SetBackdropColor() end
function Frame:SetBackdropBorderColor() end
function Frame:SetToplevel() end
function Frame:SetID(id) self.__id = id end
function Frame:GetID() return self.__id or 0 end
function Frame:Enable() self.__enabled = true end
function Frame:Disable() self.__enabled = false end
function Frame:IsEnabled() return self.__enabled and 1 or nil end
function Frame:SetHitRectInsets() end
function Frame:GetChildren() return unpack(self.__children) end
function Frame:SetHyperlinksEnabled() end

function Button:SetText(text) self.__text = text ~= nil and tostring(text) or nil end
function Button:GetText() return self.__text end
function Button:SetNormalFontObject() end
function Button:SetHighlightFontObject() end
function Button:SetDisabledFontObject() end
function Button:LockHighlight() self.__locked = true end
function Button:UnlockHighlight() self.__locked = false end
function Button:SetNormalTexture(texture) self.__normal = texture end
function Button:SetPushedTexture() end
function Button:SetHighlightTexture() end
function Button:SetDisabledTexture() end
function Button:GetHighlightTexture()
	self.__highlight = self.__highlight or H.newObject("Texture", nil, self)
	return self.__highlight
end
function Button:GetNormalTexture()
	self.__normalTexture = self.__normalTexture or H.newObject("Texture", nil, self)
	return self.__normalTexture
end
function Button:GetFontString() return nil end
function Button:RegisterForClicks() end
function Button:Click(button)
	if self.__kind == "CheckButton" then
		self.__checked = not self.__checked
	end
	H.run(self, "OnClick", button or "LeftButton", false)
end

function CheckButton:SetChecked(checked) self.__checked = checked and checked ~= 0 and true or false end
function CheckButton:GetChecked() return self.__checked and 1 or nil end

-- CSimpleSlider::SetValue (Engine/Source/Frame/CSimpleSlider.cpp): clamp, snap to the step from the minimum, then
-- anything within a step of either end goes to that end; OnValueChanged fires only for a new value. Single floats.
local function float(x)
	return H.toFloat(x)
end

function Slider:SetMinMaxValues(minValue, maxValue)
	self.__min, self.__max, self.__rangeSet = float(minValue), float(maxValue), true
	if self.__valueSet then
		local value = self.__value
		self.__valueSet = false
		self.__value = nil
		Slider.SetValue(self, value) -- the C++ method, not a Lua override on the frame
	end
end
function Slider:GetMinMaxValues() return self.__min or 0, self.__max or 0 end
function Slider:SetValueStep(step)
	step = math.max(float(step), 1.1920929e-07)
	if math.abs(step - (self.__step or 0)) < 0.00000023841858 then return end
	self.__step = step
	if self.__valueSet then
		local value = self.__value
		self.__value = nil
		self.__valueSet = false
		Slider.SetValue(self, value) -- the C++ method, not a Lua override on the frame
	end
end
function Slider:GetValueStep() return self.__step or 0 end
function Slider:SetValue(value)
	if not self.__rangeSet then return end
	local base, maxValue, step = self.__min, self.__max, self.__step or 0
	value = float(math.min(math.max(value, base), maxValue))
	if step ~= 0 then
		local offset = value - base
		local half = step * 0.5
		local steps = offset > 0 and math.floor((offset + half) / step) or math.ceil((offset - half) / step)
		value = float(steps * step + base)
	end
	if maxValue < value + step then value = maxValue end
	if value - step < base then value = base end
	if self.__valueSet and math.abs(value - self.__value) < 0.00000023841858 then return end
	self.__value = value
	self.__valueSet = true
	H.run(self, "OnValueChanged", value)
end
function Slider:GetValue() return self.__value or 0 end
function Slider:SetOrientation() end
function Slider:SetThumbTexture() end
function Slider:GetThumbTexture() return H.newObject("Texture", nil, self) end

function H.fireVisibility(frame, script)
	if frame.__scripts and frame.__scripts[script] then
		H.run(frame, script)
	end
	for _, child in ipairs(frame.__children) do
		if child.__shown then
			H.fireVisibility(child, script)
		end
	end
end

function H.run(frame, script, ...)
	local fn = frame.__scripts and frame.__scripts[script]
	if not fn then return end
	local ok, err = pcall(fn, frame, ...)
	if not ok then
		record((frame.__name or "?") .. ":" .. script, err)
	end
end

function H.fire(event, ...)
	for _, frame in ipairs(H.frames) do
		if frame.__events and frame.__events[event] then
			H.run(frame, "OnEvent", event, ...)
		end
	end
end

function CreateFrame(kind, name, parent, templates)
	if type(parent) == "string" then parent = _G[parent] end
	local frame = H.newObject(kind, name, parent)
	if templates then
		for template in string.gmatch(templates, "[^,%s]+") do
			H.applyTemplate(frame, template)
		end
	end
	H.loaded(frame)
	return frame
end

-- set by the Python side: instantiate a virtual XML template into a frame
H.applyTemplate = function (frame, template) end
H.loaded = function (frame) end

-- [[ The rest of the client API the options code calls ]] --

tinsert, tremove, wipe = table.insert, table.remove, function (t) for k in pairs(t) do t[k] = nil end return t end
strfind, strsub, strlen, strlower, strupper, strmatch, gsub, format = string.find, string.sub, string.len, string.lower,
	string.upper, string.match, string.gsub, string.format
floor, ceil, abs, min, max = math.floor, math.ceil, math.abs, math.min, math.max
function ScrollFrame_OnLoad() end
function IsMacClient() return nil end
function GetGamma() return 1 end
function SetGamma() end
function ScrollFrame_OnScrollRangeChanged() end

function getglobal(name) return _G[name] end
function setglobal(name, value) _G[name] = value end
function securecall(fn, ...)
	if type(fn) == "string" then fn = _G[fn] end
	local results = { pcall(fn, ...) }
	if not results[1] then record("securecall", results[2]) return end
	return unpack(results, 2)
end
function issecure() return 1 end
function hooksecurefunc(a, b, c)
	local tableRef, name, hook = _G, a, b
	if type(a) == "table" then tableRef, name, hook = a, b, c end
	local original = tableRef[name]
	tableRef[name] = function (...) local results = { original(...) } hook(...) return unpack(results) end
end
function geterrorhandler() return function (err) record("errorhandler", err) end end
function PlaySound() H.sounds = H.sounds + 1 end
function StaticPopup_Show(which) H.popups[#H.popups + 1] = which end
function UpdateMicroButtons() end
function ShowUIPanel(frame) frame:Show() end
function HideUIPanel(frame) frame:Hide() end
function RestartGx() H.restarts = H.restarts + 1 end
function GameTooltip_Hide() GameTooltip:Hide() end
function FauxScrollFrame_Update() end
function FauxScrollFrame_GetOffset() return 0 end
function FauxScrollFrame_OnVerticalScroll() end

-- Resolution and multisampling: one display mode list as GetScreenResolutions gives it
H.resolutions = { "1920x1080", "2560x1440", "3840x2160" }
H.currentResolution = 3
function GetScreenResolutions() return unpack(H.resolutions) end
function GetCurrentResolution() return H.currentResolution end
function SetScreenResolution(index) H.currentResolution = index end
function GetRefreshRates() return 60, 144, 240 end
function GetMultisampleFormats() return 24, 24, 1, 24, 24, 4, 24, 24, 8 end
H.multisample = 1
function GetCurrentMultisampleFormat() return H.multisample end
function SetMultisampleFormat(index) H.multisample = index end
function GetVideoCaps() return 1, 1, 1, 1, 1, 16, 1 end
function RestoreVideoResolutionDefaults() H.restoredResolution = true end
function RestoreVideoEffectsDefaults() H.restoredEffects = true end
function RestoreVideoStereoDefaults() end
function IsStereoVideoAvailable() return nil end
function IsPlayerResolutionAvailable() return 1 end
H.terrainMip = 1
function GetTerrainMip() return H.terrainMip end
function SetTerrainMip(value) H.terrainMip = value end

-- the client's own: VideoOptions_Scripts.cpp and AwesomeDisplay.cpp
H.api = "D3D11"
function GetGraphicsAPIs() return "D3D9", "D3D9Ex", "D3D11" end
function GetCurrentGraphicsAPI() return H.api end
function GetMonitorCount() return 2 end
function GetMonitorName(index)
	if index == 1 then return "1: 3840x2160 (primary)" end
	if index == 2 then return "2: 2560x1440" end
end

GRAY_FONT_COLOR = { r = 0.5, g = 0.5, b = 0.5 }
NORMAL_FONT_COLOR = { r = 1, g = 0.82, b = 0 }
HIGHLIGHT_FONT_COLOR = { r = 1, g = 1, b = 1 }
H.fontObject = setmetatable({}, { __index = { GetTextColor = function () return 1, 0.82, 0 end } })

UIParent = H.newObject("Frame", "UIParent", nil)
UIParent.__w, UIParent.__h = 1024, 768
UIParent.__points = { TOPLEFT = { nil, "TOPLEFT", 0, 0 } }
UIErrorsFrame = H.newObject("Frame", "UIErrorsFrame", UIParent)
GameTooltip = H.newObject("GameTooltip", "GameTooltip", UIParent)
GameTooltip.__shown = false
function GameTooltip:SetOwner() self.__lines = {} self.__shown = true end
function GameTooltip:SetText(text) self.__lines = { text } end
function GameTooltip:AddLine(text) self.__lines = self.__lines or {} table.insert(self.__lines, text) end

-- [[ UIDropDownMenu ]] --
-- The menu itself is not drawn: each Initialize collects the entries on the dropdown, H.pick clicks one.

UIDROPDOWNMENU_MAXBUTTONS = 32
UIDROPDOWNMENU_MAXLEVELS = 2
local dropDownInit

function UIDropDownMenu_CreateInfo() return {} end
function UIDropDownMenu_Initialize(frame, init)
	frame.__entries = {}
	frame.initialize = init
	local previous = dropDownInit
	dropDownInit = frame
	init(frame)
	dropDownInit = previous
end
function UIDropDownMenu_AddButton(info)
	local copy = {}
	for key, value in pairs(info) do copy[key] = value end
	table.insert(dropDownInit.__entries, copy)
end
function UIDropDownMenu_SetWidth(frame, width) frame.__w = width + 50 end
function UIDropDownMenu_SetAnchor() end
function UIDropDownMenu_SetText(frame, text) _G[frame:GetName() .. "Text"]:SetText(text) end
function UIDropDownMenu_GetText(frame) return _G[frame:GetName() .. "Text"]:GetText() end
local function refresh(frame)
	for index, entry in ipairs(frame.__entries or {}) do
		if (frame.selectedValue ~= nil and entry.value == frame.selectedValue) or (frame.selectedID and index == frame.selectedID) then
			UIDropDownMenu_SetText(frame, entry.text)
		end
	end
end
function UIDropDownMenu_SetSelectedValue(frame, value) frame.selectedValue, frame.selectedID = value, nil refresh(frame) end
function UIDropDownMenu_GetSelectedValue(frame) return frame.selectedValue end
function UIDropDownMenu_SetSelectedID(frame, id) frame.selectedID, frame.selectedValue = id, nil refresh(frame) end
function UIDropDownMenu_GetSelectedID(frame) return frame.selectedID end
function UIDropDownMenu_EnableDropDown(frame) frame.__enabled = true end
function UIDropDownMenu_DisableDropDown(frame) frame.__enabled = false end

-- click entry `value` of a dropdown's menu
function H.pick(frame, value)
	UIDropDownMenu_Initialize(frame, frame.initialize)
	for index, entry in ipairs(frame.__entries) do
		if entry.value == value or entry.text == value then
			local button = { value = entry.value, arg1 = entry.arg1, arg2 = entry.arg2 }
			function button:GetID() return index end
			function button:GetText() return entry.text end
			local ok, err = pcall(entry.func, button, entry.arg1, entry.arg2)
			if not ok then record("pick " .. frame:GetName(), err) end
			return true
		end
	end
	record("pick " .. frame:GetName(), "no entry " .. tostring(value))
end

-- [[ Layout ]] --
-- Rectangles in UI units, y up from UIParent's bottom: left, bottom, width, height (nil when not placed)

local factors = {
	TOPLEFT = { 0, 1 }, TOP = { 0.5, 1 }, TOPRIGHT = { 1, 1 }, LEFT = { 0, 0.5 }, CENTER = { 0.5, 0.5 },
	RIGHT = { 1, 0.5 }, BOTTOMLEFT = { 0, 0 }, BOTTOM = { 0.5, 0 }, BOTTOMRIGHT = { 1, 0 },
}

function H.textWidth(fontString)
	local text = fontString.__text or ""
	text = text:gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", "")
	local font = tostring(fontString.__font or "")
	local perChar = font:find("Large") and 9 or (font:find("ExtraSmall") and 5 or (font:find("Small") and 5.6 or 6.5))
	return #text * perChar
end

local resolving = {}

function H.rect(region)
	if region == UIParent then return 0, 0, UIParent.__w, UIParent.__h end
	if resolving[region] then return nil end
	resolving[region] = true

	local width, height = region.__w, region.__h

	if region.__kind == "FontString" and not width then
		width = H.textWidth(region)
		height = height or 12
	end

	local left, right, top, bottom, centerX, centerY

	for point, anchor in pairs(region.__points) do
		local relative = anchor[1] or region.__parent or UIParent
		local rl, rb, rw, rh = H.rect(relative)

		if rl then
			local f = factors[anchor[2]]
			if not f or not factors[point] then
				error("unknown anchor point " .. tostring(point) .. " / " .. tostring(anchor[2]) .. " on " .. tostring(region.__name))
			end
			local x = rl + rw * f[1] + anchor[3]
			local y = rb + rh * f[2] + anchor[4]
			local p = factors[point]

			if p[1] == 0 then left = x elseif p[1] == 1 then right = x else centerX = x end
			if p[2] == 0 then bottom = y elseif p[2] == 1 then top = y else centerY = y end
		end
	end

	resolving[region] = nil

	if left and right then width = right - left end
	if top and bottom then height = top - bottom end
	if not width or not height then return nil end

	left = left or (right and right - width) or (centerX and centerX - width / 2)
	bottom = bottom or (top and top - height) or (centerY and centerY - height / 2)

	if not left or not bottom then return nil end

	return left, bottom, width, height
end
