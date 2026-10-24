-- The client's own Video options: the Lighting, Water and Sky, and Advanced panels for the graphics this client adds,
-- and the stock Resolution and Effects panels widened to the client's limits. Built into the client
-- (Awesome/AwesomeEmbeddedUi.cpp) and run as FrameXML right after the FrameXML files, so the stock panel code
-- (VideoOptionsPanels.lua, OptionsPanelTemplates.lua), its templates and its frames exist already. Every control here
-- is a stock options control: nothing changes until Okay or Apply, Cancel puts it back, Defaults resets it to the
-- CVar's default. The fine tuning of each feature stays in the built-in settings window (/awesome).

local OPTIONS_FARCLIP_MIN = 177;
-- the stock slider's 110-yard grid, which its quality presets sit on (507, 727, 1057, 1277), up to about 5000 yards;
-- the client takes up to 10000
local OPTIONS_FARCLIP_STEP = 110;
local OPTIONS_FARCLIP_MAX = OPTIONS_FARCLIP_MIN + 44 * OPTIONS_FARCLIP_STEP;
-- pixel-exact at 2160p (768 / 2160); the client goes down to 0.1
local OPTIONS_UISCALE_MIN = 0.36;

local SLIDER_WIDTH = 112;
local SHADOW_MAP_SIZES = { 1024, 2048, 4096, 8192 };

local LIGHTING_QUALITY_CUSTOM = 6;
local LIGHTING_QUALITY_EPSILON = 0.000001;

local function VideoOptions_IsD3D11 ()
	return GetCurrentGraphicsAPI and GetCurrentGraphicsAPI() == "D3D11";
end


-- [[ Controls ]] --

-- A control is enabled while the renderer has what it needs and every checkbox it requires is checked and enabled
local function VideoOptionsControl_IsAvailable (control)
	if ( control.needsD3D11 and not VideoOptions_IsD3D11() ) then
		return false;
	end

	if ( control.requires ) then
		for _, required in ipairs(control.requires) do
			if ( not required:GetChecked() or not VideoOptionsControl_IsAvailable(required) ) then
				return false;
			end
		end
	end

	return true;
end

local function VideoOptionsControl_SetEnabled (control, enabled)
	if ( control.type == CONTROLTYPE_DROPDOWN ) then
		if ( enabled ) then
			UIDropDownMenu_EnableDropDown(control);
		else
			UIDropDownMenu_DisableDropDown(control);
		end
	elseif ( control.type == CONTROLTYPE_SLIDER ) then
		if ( enabled ) then
			BlizzardOptionsPanel_Slider_Enable(control);
		else
			BlizzardOptionsPanel_Slider_Disable(control);
		end
	elseif ( enabled ) then
		BlizzardOptionsPanel_CheckButton_Enable(control);
	else
		BlizzardOptionsPanel_CheckButton_Disable(control);
	end
end

local function VideoOptionsPanel_UpdateStates (panel)
	for _, control in next, panel.controls do
		if ( control.needsD3D11 or control.requires ) then
			VideoOptionsControl_SetEnabled(control, VideoOptionsControl_IsAvailable(control));
		end
	end

	if ( panel.UpdateStates ) then
		panel:UpdateStates();
	end
end

local function VideoOptionsPanel_ControlChanged (panel)
	if ( panel:IsVisible() ) then
		VideoOptionsFrameApply:Enable();
		VideoOptionsPanel_UpdateStates(panel);

		if ( panel.ControlChanged ) then
			panel:ControlChanged();
		end
	end
end

local function VideoOptionsSlider_OnValueChanged (self, value)
	self.newValue = value;

	if ( self.formatValue ) then
		_G[self:GetName().."Text"]:SetFormattedText(VIDEO_OPTIONS_VALUE, _G[self.labelText], self.formatValue(value));
	end

	VideoOptionsPanel_ControlChanged(self:GetParent());
end

-- option: the stock panels' options entry (text, minValue, maxValue, valueStep, tooltipRequirement, ...), read at
-- PLAYER_ENTERING_WORLD; plus lowText / highText for the ends and formatValue to show the value in the label. A slider
-- without a CVar is keyed by option.label and brings its own GetCurrentValue and SetValue.
local function VideoOptionsPanel_CreateSlider (panel, key, cvar, option, width)
	local slider = CreateFrame("Slider", panel:GetName()..key, panel, "OptionsSliderTemplate");
	local name = slider:GetName();

	slider:SetWidth(width or SLIDER_WIDTH);
	slider:SetHeight(17);
	_G[name.."Text"]:SetFontObject("GameFontNormalSmall");

	if ( option.lowText ) then
		_G[name.."Low"]:SetText(option.lowText);
	end

	if ( option.highText ) then
		_G[name.."High"]:SetText(option.highText);
	end

	slider.type = CONTROLTYPE_SLIDER;
	slider.cvar = cvar;
	slider.label = option.label;
	slider.labelText = option.text;
	slider.formatValue = option.formatValue;
	slider.SetDisplayValue = slider.SetValue;
	slider.SetValue = function (self, value)
		self:SetDisplayValue(value);
		BlizzardOptionsPanel_SetCVarSafe(self.cvar, value);
	end
	slider:SetScript("OnValueChanged", VideoOptionsSlider_OnValueChanged);

	panel.options[cvar or option.label] = option;
	BlizzardOptionsPanel_RegisterControl(slider, panel);

	return slider;
end

local function VideoOptionsPanel_CreateCheckButton (panel, key, cvar, option)
	local checkButton = CreateFrame("CheckButton", panel:GetName()..key, panel, "VideoOptionsSmallCheckButtonTemplate");

	checkButton.type = CONTROLTYPE_CHECKBOX;
	checkButton.cvar = cvar;
	checkButton.setFunc = function () VideoOptionsPanel_ControlChanged(panel); end;

	panel.options[cvar] = option;
	BlizzardOptionsPanel_RegisterControl(checkButton, panel);

	return checkButton;
end

-- The selected entry: the CVar's value, matched to an entry by number or (for names) case-insensitively
local function VideoOptionsDropDown_GetCurrent (dropDown)
	local value = BlizzardOptionsPanel_GetCVarSafe(dropDown.cvar);

	for _, entry in ipairs(dropDown.getEntries()) do
		if ( entry.value == value or (type(value) == "string" and type(entry.value) == "string" and strlower(entry.value) == strlower(value)) ) then
			return entry.value;
		end
	end

	return value;
end

local function VideoOptionsDropDown_OnClick (button, dropDown)
	local value = button.value;

	UIDropDownMenu_SetSelectedValue(dropDown, value);

	if ( dropDown.value == value ) then
		dropDown.newValue = nil;
	else
		dropDown.newValue = value;
	end

	VideoOptionsPanel_ControlChanged(dropDown:GetParent());
end

local function VideoOptionsDropDown_Initialize (dropDown)
	local info = UIDropDownMenu_CreateInfo();
	local selected = UIDropDownMenu_GetSelectedValue(dropDown);

	for _, entry in ipairs(dropDown.getEntries()) do
		info.text = entry.text;
		info.value = entry.value;
		info.func = VideoOptionsDropDown_OnClick;
		info.arg1 = dropDown;
		info.checked = (entry.value == selected) and 1 or nil;

		if ( info.checked ) then
			UIDropDownMenu_SetText(dropDown, entry.text);
		end

		UIDropDownMenu_AddButton(info);
	end
end

local function VideoOptionsDropDown_SetSelected (dropDown, value)
	dropDown.selectedValue = value;
	UIDropDownMenu_Initialize(dropDown, VideoOptionsDropDown_Initialize);
	UIDropDownMenu_SetSelectedValue(dropDown, value);
end

local function VideoOptionsDropDown_OnEnter (self)
	if ( self.tooltipText ) then
		GameTooltip:SetOwner(self, "ANCHOR_RIGHT");
		GameTooltip:SetText(self.tooltipText, nil, nil, nil, nil, 1);

		if ( self.tooltipRequirement ) then
			GameTooltip:AddLine(self.tooltipRequirement, 1.0, 1.0, 1.0, 1.0);
		end

		GameTooltip:Show();
	end
end

-- A dropdown for a CVar, in the stock Resolution panel's style: label above, the entries from getEntries()
-- ({ value, text } in order). Dropdowns stay out of panel.options, whose label text would replace the selection.
local function VideoOptionsPanel_CreateDropDown (panel, key, cvar, labelText, width, getEntries)
	local dropDown = CreateFrame("Frame", panel:GetName()..key, panel, "UIDropDownMenuTemplate");
	local label = dropDown:CreateFontString(dropDown:GetName().."Label", "BACKGROUND", "GameFontNormalSmall");

	label:SetPoint("BOTTOMLEFT", dropDown, "TOPLEFT", 21, 0);
	label:SetText(_G[labelText]);

	dropDown.type = CONTROLTYPE_DROPDOWN;
	dropDown.cvar = cvar;
	dropDown.getEntries = getEntries;
	dropDown.tooltipText = _G["OPTION_TOOLTIP_"..labelText];
	dropDown.defaultValue = BlizzardOptionsPanel_GetCVarDefaultSafe(cvar);
	dropDown.value = VideoOptionsDropDown_GetCurrent(dropDown);

	dropDown:EnableMouse(true);
	dropDown:SetScript("OnEnter", VideoOptionsDropDown_OnEnter);
	dropDown:SetScript("OnLeave", GameTooltip_Hide);

	UIDropDownMenu_SetWidth(dropDown, width);
	VideoOptionsDropDown_SetSelected(dropDown, dropDown.value);

	dropDown.SetValue = function (self, value)
		BlizzardOptionsPanel_SetCVarSafe(self.cvar, value);
	end
	dropDown.GetValue = function (self)
		return VideoOptionsDropDown_GetCurrent(self);
	end
	dropDown.RefreshValue = function (self)
		local value = VideoOptionsDropDown_GetCurrent(self);
		VideoOptionsDropDown_SetSelected(self, value);
		self.value = value;
		self.newValue = value;
	end

	BlizzardOptionsPanel_RegisterControl(dropDown, panel);

	return dropDown;
end

local function VideoOptionsPanel_CreateBox (panel, key, width, height, title)
	local box = CreateFrame("Frame", panel:GetName()..key, panel, "OptionsBoxTemplate");

	box:SetWidth(width);
	box:SetHeight(height);
	box:SetBackdropBorderColor(0.4, 0.4, 0.4);
	box:SetBackdropColor(0.15, 0.15, 0.15);
	_G[box:GetName().."Title"]:SetText(title or "");

	return box;
end

local function VideoOptionsPanel_OnEvent (self, event, ...)
	BlizzardOptionsPanel_OnEvent(self, event, ...);

	if ( event == "PLAYER_ENTERING_WORLD" ) then
		-- show the value of the sliders that name it, now that their label and value are set
		for _, control in next, self.controls do
			if ( control.formatValue ) then
				VideoOptionsSlider_OnValueChanged(control, control:GetValue());
				control.newValue = nil;
			end
		end
	end
end

local function VideoOptionsPanel_Refresh (self)
	BlizzardOptionsPanel_Refresh(self);

	if ( self.needsD3D11Text ) then
		local subText = _G[self:GetName().."SubText"];

		if ( VideoOptions_IsD3D11() ) then
			subText:SetText(self.subText);
		else
			subText:SetText(VIDEO_OPTIONS_NEEDS_D3D11);
		end
	end

	VideoOptionsPanel_UpdateStates(self);

	if ( self.ControlChanged ) then
		self:ControlChanged();
	end
end

local function VideoOptionsPanel_Create (name, title, subText)
	local panel = CreateFrame("Frame", name, VideoOptionsFramePanelContainer);
	panel:Hide();

	local titleText = panel:CreateFontString(name.."Title", "ARTWORK", "GameFontNormalLarge");
	titleText:SetJustifyH("LEFT");
	titleText:SetJustifyV("TOP");
	titleText:SetPoint("TOPLEFT", 16, -16);
	titleText:SetText(title);

	local subTextString = panel:CreateFontString(name.."SubText", "ARTWORK", "GameFontHighlightSmall");
	subTextString:SetNonSpaceWrap(true);
	subTextString:SetJustifyH("LEFT");
	subTextString:SetJustifyV("TOP");
	subTextString:SetHeight(32);
	subTextString:SetPoint("TOPLEFT", titleText, "BOTTOMLEFT", 0, -8);
	subTextString:SetPoint("RIGHT", -32, 0);
	subTextString:SetText(subText);

	panel.name = title;
	panel.subText = subText;
	panel.options = {};

	-- a requirement can change on another panel (HDR for dithering)
	panel:SetScript("OnShow", VideoOptionsPanel_UpdateStates);

	return panel;
end

-- After the controls exist: the stock panel registration (a category of VideoOptionsFrame, okay/cancel/default)
local function VideoOptionsPanel_Register (panel)
	VideoOptionsPanel_OnLoad(panel, nil, nil, nil, VideoOptionsPanel_Refresh);
	panel:SetScript("OnEvent", VideoOptionsPanel_OnEvent);
end

local function FormatYards (value)
	return format(VIDEO_OPTIONS_YARDS, value);
end

local function FormatSeconds (value)
	if ( value < 0.05 ) then
		return VIDEO_OPTIONS_OFF;
	end

	return format(VIDEO_OPTIONS_SECONDS, value);
end

local function FormatFrameRate (value)
	if ( value < 1 ) then
		return VIDEO_OPTIONS_NO_LIMIT;
	end

	return format("%d", value);
end

local function FormatPixels (value)
	return format(VIDEO_OPTIONS_PIXELS, value);
end

local function FormatBias (value)
	return format("%+.2f", value);
end


-- [[ Resolution Panel ]] --

ResolutionPanelOptions.uiscale.minValue = OPTIONS_UISCALE_MIN;
ResolutionPanelOptions.uiScaleDpi = { text = "UI_SCALE_DPI" };

do
	local panel = VideoOptionsResolutionPanel;
	local checkButton = CreateFrame("CheckButton", "VideoOptionsResolutionPanelUIScaleDpi", panel, "VideoOptionsSmallCheckButtonTemplate");

	checkButton:SetPoint("TOPLEFT", VideoOptionsResolutionPanelDisableResize, "BOTTOMLEFT", -16, -4);
	checkButton.type = CONTROLTYPE_CHECKBOX;
	checkButton.cvar = "uiScaleDpi";
	BlizzardOptionsPanel_RegisterControl(checkButton, panel);

	-- the stock Defaults resets only the UI scale controls of this panel itself; the rest is RestoreVideoResolutionDefaults
	local stockDefault = panel.default;
	panel.default = function (self)
		stockDefault(self);

		if ( checkButton.defaultValue ) then
			checkButton:SetValue(checkButton.defaultValue);
		end
	end
end


-- [[ Effects Panel ]] --

-- the client's limits (the HD client's Wow.exe raised them); every stock preset value stays on its slider's grid
EffectsPanelOptions.environmentDetail.maxValue = 3.0;
EffectsPanelOptions.groundEffectDensity.maxValue = 256;
EffectsPanelOptions.groundEffectDist.maxValue = 600;

-- View distance: GetCVarMax still names the stock 1277, and the stock handler then sets a step of a tenth of the range.
-- Once it has run, widen the slider on the stock grid and show the real value again.
VideoOptionsEffectsPanel:HookScript("OnEvent", function (self, event)
	if ( event ~= "PLAYER_ENTERING_WORLD" or self.farclipWidened ) then
		return;
	end

	self.farclipWidened = true;

	local slider = VideoOptionsEffectsPanelViewDistance;
	local value = BlizzardOptionsPanel_GetCVarSafe(slider.cvar);

	slider:SetMinMaxValues(OPTIONS_FARCLIP_MIN, OPTIONS_FARCLIP_MAX);
	slider:SetValueStep(OPTIONS_FARCLIP_STEP);
	slider:SetDisplayValue(value);
	slider.value = value;
	slider.newValue = nil;

	VideoOptionsEffectsPanel_UpdateVideoQuality();
end);


-- [[ Lighting Panel ]] --

do
	local panel = VideoOptionsPanel_Create("VideoOptionsLightingPanel", LIGHTING_LABEL, LIGHTING_SUBTEXT);
	panel.needsD3D11Text = true;

	-- quality presets, as the Effects panel's: a box with the level's name and description over a slider
	local qualityBox = VideoOptionsPanel_CreateBox(panel, "Quality", 380, 88);
	qualityBox:SetPoint("TOPLEFT", 16, -72);

	local qualityLabel = qualityBox:CreateFontString(qualityBox:GetName().."Label", "ARTWORK", "GameFontNormalLeft");
	qualityLabel:SetPoint("TOPLEFT", 16, -8);
	qualityLabel:SetPoint("RIGHT", -16, 0);

	local qualitySubText = qualityBox:CreateFontString(qualityBox:GetName().."SubText", "ARTWORK", "GameFontHighlightExtraSmallLeftTop");
	qualitySubText:SetWidth(350);
	qualitySubText:SetHeight(42);
	qualitySubText:SetPoint("TOPLEFT", qualityLabel, "BOTTOMLEFT", 0, -2);

	local qualitySlider = CreateFrame("Slider", "VideoOptionsLightingPanelQualitySlider", panel, "OptionsSliderTemplate");
	qualitySlider:SetWidth(348);
	qualitySlider:SetHeight(17);
	qualitySlider:SetPoint("BOTTOMLEFT", qualityBox, "BOTTOMLEFT", 16, 16);
	qualitySlider:SetMinMaxValues(1, LIGHTING_QUALITY_CUSTOM);
	qualitySlider:SetValueStep(1);
	qualitySlider:SetValue(LIGHTING_QUALITY_CUSTOM);
	_G["VideoOptionsLightingPanelQualitySliderText"]:SetText("");
	_G["VideoOptionsLightingPanelQualitySliderLow"]:SetText(LIGHTING_QUALITY_LABEL1);
	_G["VideoOptionsLightingPanelQualitySliderHigh"]:SetPoint("TOPRIGHT", qualitySlider, "BOTTOMRIGHT", -65, 3);
	_G["VideoOptionsLightingPanelQualitySliderHigh"]:SetText(LIGHTING_QUALITY_LABEL5);

	local qualityCustom = qualitySlider:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall");
	qualityCustom:SetPoint("TOPRIGHT", qualitySlider, "BOTTOMRIGHT", 4, 3);
	qualityCustom:SetText(LIGHTING_QUALITY_LABEL6);

	-- the switches
	local switches = VideoOptionsPanel_CreateBox(panel, "Switches", 380, 76);
	switches:SetPoint("BOTTOMLEFT", 16, 8);

	local hdr = VideoOptionsPanel_CreateCheckButton(panel, "HDR", "hdr", { text = "HDR_RENDERING" });
	hdr:SetPoint("TOPLEFT", switches, "TOPLEFT", 12, -8);
	local perPixel = VideoOptionsPanel_CreateCheckButton(panel, "PerPixel", "lightPerPixel", { text = "PER_PIXEL_LIGHTING" });
	perPixel:SetPoint("TOPLEFT", switches, "TOPLEFT", 126, -8);
	local dynamic = VideoOptionsPanel_CreateCheckButton(panel, "Dynamic", "lightDynamic", { text = "DYNAMIC_LIGHTS" });
	dynamic:SetPoint("TOPLEFT", switches, "TOPLEFT", 262, -8);
	local specular = VideoOptionsPanel_CreateCheckButton(panel, "Specular", "lightSpecular", { text = "SPECULAR_HIGHLIGHTS" });
	specular:SetPoint("TOPLEFT", perPixel, "BOTTOMLEFT", 0, -8);
	local stableShadows = VideoOptionsPanel_CreateCheckButton(panel, "StableShadows", "shadowStable", { text = "STABLE_SHADOWS" });
	stableShadows:SetPoint("TOPLEFT", hdr, "BOTTOMLEFT", 0, -8);

	-- the sliders, three columns as on the Effects panel
	local exposure = VideoOptionsPanel_CreateSlider(panel, "Exposure", "hdrExposure",
		{ text = "HDR_EXPOSURE", minValue = 0.25, maxValue = 4, valueStep = 0.05 });
	exposure:SetPoint("TOPLEFT", qualityBox, "BOTTOMLEFT", 2, -20);
	local bloom = VideoOptionsPanel_CreateSlider(panel, "Bloom", "bloom",
		{ text = "BLOOM_INTENSITY", minValue = 0, maxValue = 2, valueStep = 0.05 });
	bloom:SetPoint("TOPLEFT", exposure, "BOTTOMLEFT", 0, -24);
	local sharpen = VideoOptionsPanel_CreateSlider(panel, "Sharpen", "sharpen",
		{ text = "WORLD_SHARPENING", minValue = 0, maxValue = 1, valueStep = 0.05 });
	sharpen:SetPoint("TOPLEFT", bloom, "BOTTOMLEFT", 0, -24);
	local ssao = VideoOptionsPanel_CreateSlider(panel, "AmbientOcclusion", "ssao",
		{ text = "AMBIENT_OCCLUSION", minValue = 0, maxValue = 2, valueStep = 0.05 });
	ssao:SetPoint("TOPLEFT", sharpen, "BOTTOMLEFT", 0, -24);

	local skyAmbient = VideoOptionsPanel_CreateSlider(panel, "SkyAmbient", "lightSkyAmbient",
		{ text = "SKY_AMBIENT_LIGHT", minValue = 0, maxValue = 1, valueStep = 0.05 });
	skyAmbient:SetPoint("TOPLEFT", exposure, "TOPRIGHT", 20, 0);
	local heightFog = VideoOptionsPanel_CreateSlider(panel, "HeightFog", "fogHeight",
		{ text = "HEIGHT_FOG", minValue = 0, maxValue = 0.02, valueStep = 0.0005 });
	heightFog:SetPoint("TOPLEFT", skyAmbient, "BOTTOMLEFT", 0, -24);
	local scattering = VideoOptionsPanel_CreateSlider(panel, "Scattering", "fogScattering",
		{ text = "FOG_SCATTERING", minValue = 0, maxValue = 1, valueStep = 0.05 });
	scattering:SetPoint("TOPLEFT", heightFog, "BOTTOMLEFT", 0, -24);
	local fire = VideoOptionsPanel_CreateSlider(panel, "Fire", "lightFire",
		{ text = "FIRE_LIGHTS", minValue = 0, maxValue = 4, valueStep = 0.1 });
	fire:SetPoint("TOPLEFT", scattering, "BOTTOMLEFT", 0, -24);

	local lightDistance = VideoOptionsPanel_CreateSlider(panel, "LightDistance", "lightDynamicDistance",
		{ text = "DYNAMIC_LIGHT_DISTANCE", minValue = 20, maxValue = 500, valueStep = 10, formatValue = FormatYards });
	lightDistance:SetPoint("TOPLEFT", skyAmbient, "TOPRIGHT", 20, 0);

	-- shadowMapSize as the four sizes in order; its 0 (stock: 1024, or 2048 from Shadow Quality 2) stays until moved
	local function ShadowMapIndex (size)
		for index, mapSize in ipairs(SHADOW_MAP_SIZES) do
			if ( size <= mapSize ) then
				return index - 1;
			end
		end

		return #SHADOW_MAP_SIZES - 1;
	end

	local shadowSize = VideoOptionsPanel_CreateSlider(panel, "ShadowResolution", nil,
		{ label = "shadowMapSize", text = "SHADOW_RESOLUTION", minValue = 0, maxValue = #SHADOW_MAP_SIZES - 1, valueStep = 1,
		  default = ShadowMapIndex(4096), lowText = tostring(SHADOW_MAP_SIZES[1]), highText = tostring(SHADOW_MAP_SIZES[#SHADOW_MAP_SIZES]),
		  formatValue = function (value) return SHADOW_MAP_SIZES[floor(value + 0.5) + 1] or ""; end });
	shadowSize.GetCurrentValue = function (self)
		return ShadowMapIndex(BlizzardOptionsPanel_GetCVarSafe("shadowMapSize") or 0);
	end
	shadowSize.SetValue = function (self, value)
		self:SetDisplayValue(value);

		if ( self:GetCurrentValue() ~= value ) then
			SetCVar("shadowMapSize", SHADOW_MAP_SIZES[value + 1], "SHADOW_RESOLUTION");
		end
	end
	shadowSize:SetPoint("TOPLEFT", lightDistance, "BOTTOMLEFT", 0, -24);

	local shadowSoftness = VideoOptionsPanel_CreateSlider(panel, "ShadowSoftness", "shadowSoftness",
		{ text = "SHADOW_SOFTNESS", minValue = 0.25, maxValue = 4, valueStep = 0.05 });
	shadowSoftness:SetPoint("TOPLEFT", shadowSize, "BOTTOMLEFT", 0, -24);

	-- what needs the D3D11 device; the shadow settings work on every renderer
	for _, control in ipairs({ hdr, perPixel, dynamic, specular, exposure, bloom, sharpen, ssao, skyAmbient, heightFog,
							   scattering, fire, lightDistance }) do
		control.needsD3D11 = true;
	end

	exposure.requires = { hdr };
	bloom.requires = { hdr };
	sharpen.requires = { hdr };
	fire.requires = { dynamic };
	lightDistance.requires = { dynamic };

	-- Off, Low, Medium, High (the /lighting on preset), Ultra
	local levels = {
		[1] = { [hdr] = false, [perPixel] = false, [dynamic] = false, [specular] = false,
				[bloom] = 0, [sharpen] = 0, [ssao] = 0, [skyAmbient] = 0, [heightFog] = 0, [scattering] = 0, [fire] = 0 },
		[2] = { [hdr] = true, [perPixel] = true, [dynamic] = false, [specular] = false,
				[bloom] = 0.1, [sharpen] = 0.3, [ssao] = 0, [skyAmbient] = 0.3, [heightFog] = 0, [scattering] = 0, [fire] = 0 },
		[3] = { [hdr] = true, [perPixel] = true, [dynamic] = true, [specular] = true,
				[bloom] = 0.2, [sharpen] = 0.4, [ssao] = 0.5, [skyAmbient] = 0.4, [heightFog] = 0, [scattering] = 0.2, [fire] = 0.5 },
		[4] = { [hdr] = true, [perPixel] = true, [dynamic] = true, [specular] = true,
				[bloom] = 0.3, [sharpen] = 0.5, [ssao] = 0.8, [skyAmbient] = 0.5, [heightFog] = 0.003, [scattering] = 0.4, [fire] = 1 },
		[5] = { [hdr] = true, [perPixel] = true, [dynamic] = true, [specular] = true,
				[bloom] = 0.4, [sharpen] = 0.5, [ssao] = 1, [skyAmbient] = 0.6, [heightFog] = 0.004, [scattering] = 0.5, [fire] = 1.5 },
	};

	local function GetQuality ()
		for quality, controls in ipairs(levels) do
			local match = true;

			for control, value in next, controls do
				if ( control.type == CONTROLTYPE_CHECKBOX ) then
					if ( (control:GetChecked() and true or false) ~= value ) then
						match = false;
						break;
					end
				elseif ( not (abs(control:GetValue() - value) <= LIGHTING_QUALITY_EPSILON) ) then
					match = false;
					break;
				end
			end

			if ( match ) then
				return quality;
			end
		end

		return LIGHTING_QUALITY_CUSTOM;
	end

	local function ShowQuality (quality)
		qualityLabel:SetFormattedText(LIGHTING_QUALITY_S, _G["LIGHTING_QUALITY_LABEL"..quality]);
		qualitySubText:SetText(_G["LIGHTING_QUALITY_SUBTEXT"..quality]);
		panel.settingQuality = true;
		qualitySlider:SetValue(quality);
		panel.settingQuality = nil;
	end

	local function SetQuality (quality)
		if ( levels[quality] ) then
			panel.settingQuality = true;

			for control, value in next, levels[quality] do
				if ( control.type == CONTROLTYPE_CHECKBOX ) then
					control:SetChecked(value);
					BlizzardOptionsPanel_CheckButton_SetNewValue(control);
				else
					control:SetDisplayValue(value);
				end
			end

			panel.settingQuality = nil;
			VideoOptionsPanel_UpdateStates(panel);
			VideoOptionsFrameApply:Enable();
		end

		ShowQuality(quality);
	end

	qualitySlider:SetScript("OnValueChanged", function (self, value)
		if ( not panel.settingQuality and panel:IsVisible() ) then
			SetQuality(value);
		end
	end);

	panel.ControlChanged = function (self)
		if ( not self.settingQuality ) then
			ShowQuality(GetQuality());
		end
	end

	panel.UpdateStates = function (self)
		if ( VideoOptions_IsD3D11() ) then
			BlizzardOptionsPanel_Slider_Enable(qualitySlider);
		else
			BlizzardOptionsPanel_Slider_Disable(qualitySlider);
		end
	end

	VideoOptionsPanel_Register(panel);
end


-- [[ Water and Sky Panel ]] --

do
	local panel = VideoOptionsPanel_Create("VideoOptionsWaterPanel", WATER_SKY_LABEL, WATER_SKY_SUBTEXT);
	panel.needsD3D11Text = true;

	local waterBox = VideoOptionsPanel_CreateBox(panel, "Water", 380, 128, VIDEO_OPTIONS_SECTION_WATER);
	waterBox:SetPoint("TOPLEFT", 16, -88);

	local modern = VideoOptionsPanel_CreateCheckButton(panel, "Modern", "waterModern", { text = "MODERN_WATER" });
	modern:SetPoint("TOPLEFT", waterBox, "TOPLEFT", 12, -8);
	local reflections = VideoOptionsPanel_CreateCheckButton(panel, "Reflections", "waterReflection", { text = "WATER_REFLECTIONS" });
	reflections:SetPoint("TOPLEFT", waterBox, "TOPLEFT", 262, -8);
	local ripples = VideoOptionsPanel_CreateCheckButton(panel, "Ripples", "waterRipples", { text = "WATER_RIPPLES" });
	ripples:SetPoint("TOPLEFT", waterBox, "TOPLEFT", 126, -8);
	local waves = VideoOptionsPanel_CreateCheckButton(panel, "Waves", "waterTessellation", { text = "WATER_WAVES" });
	waves:SetPoint("TOPLEFT", ripples, "BOTTOMLEFT", 0, -4);
	local liquids = VideoOptionsPanel_CreateCheckButton(panel, "Liquids", "liquidModern", { text = "LAVA_AND_SLIME" });
	liquids:SetPoint("TOPLEFT", modern, "BOTTOMLEFT", 0, -4);

	local clarity = VideoOptionsPanel_CreateSlider(panel, "Clarity", "waterClarity",
		{ text = "WATER_CLARITY", minValue = 0.1, maxValue = 4, valueStep = 0.05 });
	clarity:SetPoint("TOPLEFT", waterBox, "TOPLEFT", 14, -86);
	local waveHeight = VideoOptionsPanel_CreateSlider(panel, "WaveHeight", "waterWaveHeight",
		{ text = "WATER_WAVE_HEIGHT", minValue = 0, maxValue = 2, valueStep = 0.05 });
	waveHeight:SetPoint("TOPLEFT", clarity, "TOPRIGHT", 12, 0);
	local splashes = VideoOptionsPanel_CreateSlider(panel, "Splashes", "waterSplashes",
		{ text = "WATER_SPLASHES", minValue = 0, maxValue = 2, valueStep = 0.05 });
	splashes:SetPoint("TOPLEFT", waveHeight, "TOPRIGHT", 12, 0);

	local skyBox = VideoOptionsPanel_CreateBox(panel, "Sky", 380, 140, VIDEO_OPTIONS_SECTION_SKY);
	skyBox:SetPoint("TOPLEFT", waterBox, "BOTTOMLEFT", 0, -26);

	local smoothSky = VideoOptionsPanel_CreateCheckButton(panel, "SmoothSky", "skySmooth", { text = "SMOOTH_SKY" });
	smoothSky:SetPoint("TOPLEFT", skyBox, "TOPLEFT", 12, -8);

	local transitions = VideoOptionsPanel_CreateSlider(panel, "SkyTransitions", "skyTransition",
		{ text = "SKY_TRANSITIONS", minValue = 0, maxValue = 10, valueStep = 0.5, formatValue = FormatSeconds });
	transitions:SetPoint("TOPLEFT", skyBox, "TOPLEFT", 14, -56);
	local dither = VideoOptionsPanel_CreateSlider(panel, "SkyDither", "skyDither",
		{ text = "SKY_DITHERING", minValue = 0, maxValue = 2, valueStep = 0.25 });
	dither:SetPoint("TOPLEFT", transitions, "TOPRIGHT", 40, 0);

	local lightning = VideoOptionsPanel_CreateSlider(panel, "Lightning", "weatherLightning",
		{ text = "LIGHTNING_FLASHES", minValue = 0, maxValue = 2, valueStep = 1, lowText = VIDEO_OPTIONS_OFF, highText = LIGHTNING_HEAVY_RAIN });
	lightning:SetPoint("TOPLEFT", transitions, "BOTTOMLEFT", 0, -24);
	local lightningBrightness = VideoOptionsPanel_CreateSlider(panel, "LightningBrightness", "weatherLightningBrightness",
		{ text = "LIGHTNING_BRIGHTNESS", minValue = 0, maxValue = 1.5, valueStep = 0.05 });
	lightningBrightness:SetPoint("TOPLEFT", lightning, "TOPRIGHT", 40, 0);

	-- the water is D3D11 work; the smooth sky, light transitions and lightning work on every renderer
	for _, control in ipairs({ modern, reflections, ripples, waves, liquids, clarity, waveHeight, splashes, dither }) do
		control.needsD3D11 = true;
	end

	reflections.requires = { modern };
	ripples.requires = { modern };
	liquids.requires = { modern };
	clarity.requires = { modern };
	waves.requires = { ripples };
	splashes.requires = { ripples };
	waveHeight.requires = { waves };
	dither.requires = { VideoOptionsLightingPanelHDR };

	VideoOptionsPanel_Register(panel);
end


-- [[ Advanced Panel ]] --

do
	local panel = VideoOptionsPanel_Create("VideoOptionsAdvancedPanel", ADVANCED_VIDEO_LABEL, ADVANCED_VIDEO_SUBTEXT);

	local apiNames = { D3D9 = GRAPHICS_API_D3D9, D3D9Ex = GRAPHICS_API_D3D9EX, D3D11 = GRAPHICS_API_D3D11 };

	local api = VideoOptionsPanel_CreateDropDown(panel, "GraphicsAPI", "gxApi", "GRAPHICS_API", 110, function ()
		local entries = {};

		for _, name in ipairs({ GetGraphicsAPIs() }) do
			tinsert(entries, { value = name, text = apiNames[name] or name });
		end

		return entries;
	end);
	api:SetPoint("TOPLEFT", 0, -92);
	api.gameRestart = true;
	api.tooltipRequirement = OPTION_RESTART_REQUIREMENT;

	-- the renderer this session runs, which a command-line switch can make differ from the choice above
	local running = panel:CreateFontString(panel:GetName().."GraphicsAPIRunning", "ARTWORK", "GameFontHighlightSmall");
	running:SetPoint("TOPLEFT", api, "BOTTOMLEFT", 21, 2);

	local monitor = VideoOptionsPanel_CreateDropDown(panel, "Monitor", "gxMonitor", "DISPLAY_MONITOR", 160, function ()
		local entries = { { value = 0, text = DISPLAY_MONITOR_LAST } };

		if ( GetMonitorCount and GetMonitorName ) then
			for index = 1, GetMonitorCount() do
				tinsert(entries, { value = index, text = GetMonitorName(index) or tostring(index) });
			end
		end

		return entries;
	end);
	monitor:SetPoint("LEFT", api, "RIGHT", 0, 0);

	local upscaling = VideoOptionsPanel_CreateDropDown(panel, "UIUpscaling", "uiUpscale", "UI_UPSCALING", 110, function ()
		return {
			{ value = 0, text = UI_UPSCALING_OFF },
			{ value = 1, text = UI_UPSCALING_BICUBIC },
			{ value = 2, text = UI_UPSCALING_EDGE },
		};
	end);
	upscaling:SetPoint("TOPLEFT", api, "BOTTOMLEFT", 0, -40);

	local portraits = VideoOptionsPanel_CreateCheckButton(panel, "LivePortraits", "portraitLive", { text = "LIVE_PORTRAITS" });
	portraits:SetPoint("TOPLEFT", monitor, "BOTTOMLEFT", 16, -42);

	local maxFps = VideoOptionsPanel_CreateSlider(panel, "MaxFPS", "maxFPS",
		{ text = "MAX_FPS_FOREGROUND", minValue = 0, maxValue = 360, valueStep = 5, lowText = VIDEO_OPTIONS_NO_LIMIT, highText = "360",
		  formatValue = FormatFrameRate }, 160);
	maxFps:SetPoint("TOPLEFT", upscaling, "BOTTOMLEFT", 18, -30);
	local maxFpsBk = VideoOptionsPanel_CreateSlider(panel, "MaxFPSBk", "maxFPSBk",
		{ text = "MAX_FPS_BACKGROUND", minValue = 0, maxValue = 240, valueStep = 5, lowText = VIDEO_OPTIONS_NO_LIMIT, highText = "240",
		  formatValue = FormatFrameRate }, 160);
	maxFpsBk:SetPoint("TOPLEFT", maxFps, "BOTTOMLEFT", 0, -24);

	local lodBias = VideoOptionsPanel_CreateSlider(panel, "TextureLodBias", "texLodBias",
		{ text = "TEXTURE_LOD_BIAS", minValue = -1, maxValue = 1, valueStep = 0.05, lowText = TEXTURE_LOD_BIAS_SHARP,
		  highText = TEXTURE_LOD_BIAS_SOFT, formatValue = FormatBias }, 160);
	lodBias:SetPoint("TOPLEFT", maxFps, "TOPRIGHT", 40, 0);
	lodBias.needsD3D11 = true;
	local fontSize = VideoOptionsPanel_CreateSlider(panel, "FontResolution", "fontMaxPixelSize",
		{ text = "FONT_RESOLUTION", minValue = 32, maxValue = 256, valueStep = 16, lowText = "32", highText = "256",
		  formatValue = FormatPixels }, 160);
	fontSize:SetPoint("TOPLEFT", lodBias, "BOTTOMLEFT", 0, -24);

	panel.UpdateStates = function (self)
		local current = GetCurrentGraphicsAPI and GetCurrentGraphicsAPI();
		running:SetFormattedText(GRAPHICS_API_CURRENT, current and (apiNames[current] or current) or UNKNOWN);
	end

	VideoOptionsPanel_Register(panel);
end
