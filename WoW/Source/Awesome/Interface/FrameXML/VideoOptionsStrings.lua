-- Strings for the client's own Video options panels (VideoOptionsPanelsWaystone.lua), as GlobalStrings.lua holds the
-- stock ones: the stock panel code looks a control's label up as _G[text] and its tooltip as
-- _G["OPTION_TOOLTIP_" .. text]. English only; every locale shows these.

-- [[ Panels ]] --

LIGHTING_LABEL = "Lighting";
LIGHTING_SUBTEXT = "These options light the world in a modern way without new art: high dynamic range, bloom, ambient occlusion, per-pixel and dynamic lights, and shadows.";
WATER_SKY_LABEL = "Water and Sky";
WATER_SKY_SUBTEXT = "These options control how water, the sky and lightning are drawn.";
ADVANCED_VIDEO_LABEL = "Advanced";
ADVANCED_VIDEO_SUBTEXT = "These options choose the renderer and the monitor, cap the frame rate, and set how sharply textures, text and portraits are drawn.";

VIDEO_OPTIONS_NEEDS_D3D11 = "|cffff8040These settings need the Direct3D 11 renderer: choose it under Advanced, Graphics API, and restart the game.|r";
VIDEO_OPTIONS_SECTION_WATER = "Water";
VIDEO_OPTIONS_SECTION_SKY = "Sky and Weather";
VIDEO_OPTIONS_SECTION_SHADOWS = "Shadows";
VIDEO_OPTIONS_VALUE = "%s: %s";
VIDEO_OPTIONS_NO_LIMIT = "No Limit";
VIDEO_OPTIONS_YARDS = "%d yd";
VIDEO_OPTIONS_SECONDS = "%.1f s";
VIDEO_OPTIONS_PIXELS = "%d px";
VIDEO_OPTIONS_OFF = "Off";

-- [[ Lighting quality presets ]] --

LIGHTING_QUALITY_S = "Lighting Quality: %s";
LIGHTING_QUALITY_LABEL1 = "Off";
LIGHTING_QUALITY_LABEL2 = "Low";
LIGHTING_QUALITY_LABEL3 = "Medium";
LIGHTING_QUALITY_LABEL4 = "High";
LIGHTING_QUALITY_LABEL5 = "Ultra";
LIGHTING_QUALITY_LABEL6 = "Custom";
LIGHTING_QUALITY_SUBTEXT1 = "The world is lit as in the original game.";
LIGHTING_QUALITY_SUBTEXT2 = "High dynamic range and per-pixel lighting with light sharpening. Little cost on any Direct3D 11 card.";
LIGHTING_QUALITY_SUBTEXT3 = "Adds sun shadows, shade under leaves, dynamic lights, specular highlights, soft ambient occlusion and a little bloom.";
LIGHTING_QUALITY_SUBTEXT4 = "Adds firelight, valley mist and sun-tinted fog, with stronger ambient occlusion, shade and bloom.";
LIGHTING_QUALITY_SUBTEXT5 = "The strongest settings, for high-end systems.";
LIGHTING_QUALITY_SUBTEXT6 = "Your own combination of the settings below.";

-- [[ Lighting ]] --

HDR_RENDERING = "HDR Rendering";
OPTION_TOOLTIP_HDR_RENDERING = "Renders the world in high dynamic range and tonemaps it. Needed for exposure, bloom and sharpening.";
PER_PIXEL_LIGHTING = "Per-Pixel Lighting";
OPTION_TOOLTIP_PER_PIXEL_LIGHTING = "Models, buildings and terrain are lit per pixel with their original formulas instead of per vertex.";
DYNAMIC_LIGHTS = "Dynamic Lights";
OPTION_TOOLTIP_DYNAMIC_LIGHTS = "The lights placed in models and buildings (lamps, torches, crystals) light the world around them.";
SPECULAR_HIGHLIGHTS = "Specular Highlights";
OPTION_TOOLTIP_SPECULAR_HIGHLIGHTS = "Shiny highlights on buildings and models that have shiny materials. Buildings also need Specular Lighting (Effects) and a restart.";
HDR_EXPOSURE = "Exposure";
OPTION_TOOLTIP_HDR_EXPOSURE = "Brightness of the world before tonemapping.";
BLOOM_INTENSITY = "Bloom";
OPTION_TOOLTIP_BLOOM_INTENSITY = "Glow around bright light.";
WORLD_SHARPENING = "Sharpening";
OPTION_TOOLTIP_WORLD_SHARPENING = "Contrast-adaptive sharpening of the world, never the interface: brings back detail softened by distance and texture filtering, without halos.";
AMBIENT_OCCLUSION = "Ambient Occlusion";
OPTION_TOOLTIP_AMBIENT_OCCLUSION = "Darkens creases and the places where objects meet the ground.";
SKY_AMBIENT_LIGHT = "Sky Lighting";
OPTION_TOOLTIP_SKY_AMBIENT_LIGHT = "Tints the ambient light by the sky above and the ground below.";
HEIGHT_FOG = "Valley Mist";
OPTION_TOOLTIP_HEIGHT_FOG = "Mist that gathers low in valleys and over water.";
FOG_SCATTERING = "Sun Scattering";
OPTION_TOOLTIP_FOG_SCATTERING = "Tints the fog toward the sun's colour when looking toward the sun.";
FIRE_LIGHTS = "Firelight";
OPTION_TOOLTIP_FIRE_LIGHTS = "Bonfires, braziers and torches light their surroundings. Needs Dynamic Lights.";
DYNAMIC_LIGHT_DISTANCE = "Light Distance";
OPTION_TOOLTIP_DYNAMIC_LIGHT_DISTANCE = "How far away dynamic lights are still drawn. Needs Dynamic Lights.";
SHADOW_RESOLUTION = "Shadow Resolution";
OPTION_TOOLTIP_SHADOW_RESOLUTION = "Size of every shadow map. Larger maps give sharper, more detailed shadows and use more video memory (8192 needs about 2.4 GB).";
SHADOW_SOFTNESS = "Shadow Softness";
OPTION_TOOLTIP_SHADOW_SOFTNESS = "Widens the shadow edge filter. Raise it if large shadow maps look too sharp.";
STABLE_SHADOWS = "Stable Shadows";
OPTION_TOOLTIP_STABLE_SHADOWS = "Shadow edges no longer crawl and flicker while you move.";
SUN_SHADOWS = "Sun Shadows";
OPTION_TOOLTIP_SUN_SHADOWS = "Shadows take away the sunlight itself, not a little of all light: shade is deep where the sun is strong, and sunlight through leaves falls in bright, soft-edged patches. Needs Shadow Quality at its highest (Effects).";
CANOPY_SHADE = "Canopy Shade";
OPTION_TOOLTIP_CANOPY_SHADE = "The sky's light dims under leaves and roofs, so forests are shaded and clearings bright. Needs Sun Shadows.";
SUN_SHAFTS = "Sun Shafts";
OPTION_TOOLTIP_SUN_SHAFTS = "Sunlight lights the air where it gets through, so it falls in shafts through gaps in the leaves. Needs Direct3D 11 and Shadow Quality at its highest (Effects).";

-- [[ Water and sky ]] --

MODERN_WATER = "Modern Water";
OPTION_TOOLTIP_MODERN_WATER = "Water shaded per pixel: you see into it, it fades to its own colour with depth, reflects the world and the sky, the sun glints on it and foam gathers at the shore.";
WATER_REFLECTIONS = "Reflections";
OPTION_TOOLTIP_WATER_REFLECTIONS = "Water reflects the world on screen. Off, it reflects only the sky.";
WATER_RIPPLES = "Interactive Ripples";
OPTION_TOOLTIP_WATER_RIPPLES = "Characters and creatures disturb the water: wakes, rings, splashes and raindrops.";
WATER_WAVES = "Real Waves";
LAVA_AND_SLIME = "Lava and Slime";
OPTION_TOOLTIP_LAVA_AND_SLIME = "Magma and slime are shaded too: lava as dark crust over glowing melt that cools at the shore and flares where it is stirred, slime as a thick glossy liquid. Both move heavily under you, bubble, and throw globs (and embers) when you splash in.";
OPTION_TOOLTIP_WATER_WAVES = "Near you the ripples move the water surface itself, so wakes and splashes are actual waves. Needs Interactive Ripples.";
WATER_CLARITY = "Water Murkiness";
OPTION_TOOLTIP_WATER_CLARITY = "How quickly the water's own colour hides the bottom.";
WATER_WAVE_HEIGHT = "Wave Height";
OPTION_TOOLTIP_WATER_WAVE_HEIGHT = "How far the ripples move the surface up and down. Needs Real Waves.";
WATER_SPLASHES = "Splashes";
OPTION_TOOLTIP_WATER_SPLASHES = "Droplets and spray when jumping into water, wading and swimming. Needs Interactive Ripples.";
SMOOTH_SKY = "Smooth Sky";
OPTION_TOOLTIP_SMOOTH_SKY = "The sky's colour bands blend smoothly instead of showing stripes near the horizon.";
SKY_TRANSITIONS = "Light Transitions";
OPTION_TOOLTIP_SKY_TRANSITIONS = "How long the sky, fog and light take to settle when the zone's lighting changes. Off switches at once, as in the original game.";
SKY_DITHERING = "Gradient Dithering";
OPTION_TOOLTIP_SKY_DITHERING = "A faint noise that hides banding in smooth gradients such as the sky and fog. Needs HDR Rendering (Lighting).";
LIGHTNING_FLASHES = "Lightning";
OPTION_TOOLTIP_LIGHTNING_FLASHES = "Lightning and thunder in storms. In the middle: thunderstorms only. At the right: heavy rain too.";
LIGHTNING_HEAVY_RAIN = "Heavy Rain";
LIGHTNING_BRIGHTNESS = "Lightning Brightness";
OPTION_TOOLTIP_LIGHTNING_BRIGHTNESS = "How brightly a lightning strike lights the sky and the world.";

-- [[ Advanced ]] --

GRAPHICS_API = "Graphics API";
OPTION_TOOLTIP_GRAPHICS_API = "The renderer. Direct3D 11 is needed for the Lighting and Water options. Takes effect after a restart.";
GRAPHICS_API_CURRENT = "Running: %s";
GRAPHICS_API_D3D9 = "Direct3D 9";
GRAPHICS_API_D3D9EX = "Direct3D 9Ex";
GRAPHICS_API_D3D11 = "Direct3D 11";
DISPLAY_MONITOR = "Monitor";
OPTION_TOOLTIP_DISPLAY_MONITOR = "The monitor for full screen and new windows. Full screen always uses the monitor's own resolution.";
DISPLAY_MONITOR_LAST = "Last Used";
UI_UPSCALING = "Interface Upscaling";
OPTION_TOOLTIP_UI_UPSCALING = "Sharpens interface art drawn larger than it is (most of it at 1440p and 4K).";
UI_UPSCALING_OFF = "Off";
UI_UPSCALING_BICUBIC = "Bicubic";
UI_UPSCALING_EDGE = "Edge-Adaptive";
MAX_FPS_FOREGROUND = "Max Foreground FPS";
OPTION_TOOLTIP_MAX_FPS_FOREGROUND = "Highest frame rate while the game is in front. All the way left: no limit.";
MAX_FPS_BACKGROUND = "Max Background FPS";
OPTION_TOOLTIP_MAX_FPS_BACKGROUND = "Highest frame rate while another window is in front. All the way left: no limit.";
TEXTURE_LOD_BIAS = "Texture Sharpness";
OPTION_TOOLTIP_TEXTURE_LOD_BIAS = "Picks sharper texture detail in the distance and at grazing angles, at some risk of shimmer on fine patterns. Direct3D 11 only.";
TEXTURE_LOD_BIAS_SHARP = "Sharp";
TEXTURE_LOD_BIAS_SOFT = "Soft";
FONT_RESOLUTION = "Text Resolution";
OPTION_TOOLTIP_FONT_RESOLUTION = "Largest pixel size text is rendered at before it is scaled. Raise it if very large text looks soft.";
LIVE_PORTRAITS = "Animated Portraits";
OPTION_TOOLTIP_LIVE_PORTRAITS = "Unit portraits show the live 3D model playing its idle animation instead of a still picture.";
UI_SCALE_DPI = "Scale with Display";
OPTION_TOOLTIP_UI_SCALE_DPI = "The interface keeps the same size on screen on every monitor and resolution, following Windows' display scaling. UI Scale then means 'as at 1080p and 100%'.";
