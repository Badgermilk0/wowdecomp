"""Offline test of the client's own Video options panels (WoW/Source/Awesome/Interface/FrameXML).

Runs the stock options FrameXML (the XML and Lua the game loads) and then the built-in panels under Lua 5.1 (lupa,
the client's dialect) with a stand-in client (stub.lua), drives the options as a player would (show, move sliders,
pick dropdown entries, Okay, Cancel, Defaults) and checks the CVars that come out. Also lays the panels out from their
anchors, checks that no control overlaps another or leaves the panel, and draws each panel to a PNG to look at.

The stock files are Blizzard's and are not in this repository. Extract them from the client's MPQs into one folder,
each from the newest archive that has it (the HD client: GlobalStrings.lua and OptionsFrameTemplates.xml from
patch-enUS-3, VideoOptionsFrame.* and VideoOptionsPanels.* from patch-enUS-2, the rest from patch-enUS):
GlobalStrings.lua, OptionsFrameTemplates.xml/.lua, OptionsPanelTemplates.xml/.lua, VideoOptionsFrame.xml/.lua,
VideoOptionsPanels.xml/.lua, GraphicsQualityLevels.lua.

    python video_options_tests.py <stock FrameXML folder> [--png <folder>]

Needs lupa (pip install lupa); --png needs Pillow. Exit code 0 when every check passes.
"""

import argparse
import os
import re
import struct
import sys
import xml.etree.ElementTree as ElementTree

from lupa import lua51

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
BUILT_IN = os.path.join(ROOT, 'WoW', 'Source', 'Awesome', 'Interface', 'FrameXML')

# the parameters FrameXML gives each script handler after self
SCRIPT_PARAMS = {
    'OnClick': 'button, down', 'OnValueChanged': 'value', 'OnEvent': 'event, ...', 'OnUpdate': 'elapsed',
    'OnMouseDown': 'button', 'OnMouseUp': 'button', 'OnEnter': 'motion', 'OnLeave': 'motion',
    'OnVerticalScroll': 'offset', 'OnMouseWheel': 'delta', 'OnDragStart': 'button', 'OnSizeChanged': 'width, height',
}

FRAME_TYPES = {'Frame', 'Button', 'CheckButton', 'Slider', 'ScrollFrame', 'EditBox', 'StatusBar', 'GameTooltip'}
REGION_TEXTURES = {'NormalTexture', 'PushedTexture', 'HighlightTexture', 'DisabledTexture', 'CheckedTexture',
                   'DisabledCheckedTexture', 'ThumbTexture'}


def strip_ns(tag):
    return tag.split('}', 1)[-1]


class FrameXml:
    """Just enough of the FrameXML loader for the options files: templates, frames, layers, anchors, scripts."""

    def __init__(self, lua, folder):
        self.lua = lua
        self.folder = folder
        self.g = lua.globals()
        self.H = self.g.H
        self.templates = {}
        self.unknown_templates = set()
        self.lua_loadstring = self.g.loadstring
        self.H.applyTemplate = self.apply_template_by_name
        self.H.loaded = self.fire_onload

    def loadstring(self, text, name):
        result = self.lua_loadstring(text, name)
        return result if isinstance(result, tuple) else (result, None)

    # -- files

    def run_lua_file(self, path, *args):
        with open(path, 'rb') as handle:
            data = handle.read()
        if data[:3] == b'\xef\xbb\xbf':
            data = data[3:]
        chunk, err = self.loadstring(data, '@' + os.path.basename(path))
        if chunk is None:
            raise RuntimeError('%s: %s' % (path, err))
        ok_fn = self.lua.eval('function (f, ...) local ok, e = pcall(f, ...) if not ok then return e end end')
        err = ok_fn(chunk, *args)
        if err is not None:
            raise RuntimeError('%s: %s' % (path, err))

    def load_xml(self, name):
        path = os.path.join(self.folder, name)
        root = ElementTree.parse(path).getroot()
        for element in root:
            tag = strip_ns(element.tag)
            if tag == 'Script':
                if element.get('file'):
                    self.run_lua_file(os.path.join(self.folder, element.get('file')))
                elif element.text:
                    self.run_lua_text(element.text, name)
            elif tag == 'Include':
                self.load_xml(element.get('file'))
            elif tag in FRAME_TYPES or tag in ('FontString', 'Texture', 'Font'):
                if element.get('virtual') == 'true':
                    self.templates[element.get('name')] = element
                elif tag in FRAME_TYPES:
                    self.create_from_xml(element, None)

    def run_lua_text(self, text, where):
        chunk, err = self.loadstring(text, where)
        if chunk is None:
            raise RuntimeError('%s: %s' % (where, err))
        chunk()

    # -- frames

    def resolve_name(self, name, parent):
        if not name:
            return None
        parent_name = parent['__name'] if parent is not None else None
        return name.replace('$parent', parent_name or '')

    def create_from_xml(self, element, parent):
        tag = strip_ns(element.tag)
        if element.get('parent'):
            parent = self.g[element.get('parent')]
        if parent is None:
            parent = self.g.UIParent
        name = self.resolve_name(element.get('name'), parent)
        frame = self.H.newObject(tag, name, parent)
        for template in (element.get('inherits') or '').split(','):
            template = template.strip()
            if template:
                self.apply_template_by_name(frame, template)
        self.apply_element(frame, element)
        self.fire_onload(frame)
        return frame

    def apply_template_by_name(self, frame, template):
        element = self.templates.get(template)
        if element is None:
            self.generic_template(frame, template)
            return
        for parent_template in (element.get('inherits') or '').split(','):
            parent_template = parent_template.strip()
            if parent_template:
                self.apply_template_by_name(frame, parent_template)
        self.apply_element(frame, element)

    def generic_template(self, frame, template):
        """Templates from files not loaded here: the regions code reaches by name."""
        self.unknown_templates.add(template)
        name = frame['__name']
        if not name:
            return
        new = self.H.newObject
        if 'DropDownMenu' in template:
            for part in ('Left', 'Middle', 'Right'):
                new('Texture', name + part, frame)
            text = new('FontString', name + 'Text', frame)
            text['__font'] = 'GameFontHighlightSmall'
            button = new('Button', name + 'Button', frame)
            for part in ('NormalTexture', 'DisabledTexture', 'PushedTexture', 'HighlightTexture'):
                new('Texture', name + 'Button' + part, button)
            frame['__h'] = 32
        elif 'Button' in template:
            text = new('FontString', name + 'Text', frame)
            text['__font'] = 'GameFontNormal'
            for part in ('Left', 'Middle', 'Right'):
                new('Texture', name + part, frame)
        elif 'Scroll' in template:
            bar = new('Slider', name + 'ScrollBar', frame)
            new('Button', name + 'ScrollBarScrollUpButton', bar)
            new('Button', name + 'ScrollBarScrollDownButton', bar)
            frame['__w'] = frame['__w'] or 16

    def apply_element(self, frame, element):
        if element.get('hidden') == 'true':
            frame['__shown'] = False
        elif element.get('hidden') == 'false':
            frame['__shown'] = True
        if element.get('id'):
            frame['__id'] = int(element.get('id'))
        if element.get('text') and frame['__kind'] in ('Button', 'CheckButton'):
            text = element.get('text')
            frame['__text'] = self.g[text] if self.g[text] is not None else text
        if element.get('setAllPoints') == 'true':
            frame.SetAllPoints(frame)
        for child in element:
            tag = strip_ns(child.tag)
            if tag == 'Size':
                self.apply_size(frame, child)
            elif tag == 'Anchors':
                self.apply_anchors(frame, child)
            elif tag == 'Layers':
                for layer in child:
                    for region in layer:
                        self.create_region(frame, region)
            elif tag == 'Frames':
                for sub in child:
                    if strip_ns(sub.tag) in FRAME_TYPES:
                        self.create_from_xml(sub, frame)
            elif tag == 'Scripts':
                for script in child:
                    self.set_script(frame, script)
            elif tag in REGION_TEXTURES:
                region = self.H.newObject('Texture', self.resolve_name(child.get('name'), frame), frame)
                if tag == 'HighlightTexture':
                    frame['__highlight'] = region
            elif tag in ('ButtonText', 'FontString'):
                self.create_region(frame, child)
            elif tag == 'ScrollChild':
                for sub in child:
                    if strip_ns(sub.tag) in FRAME_TYPES:
                        self.create_from_xml(sub, frame)

    def create_region(self, frame, element):
        tag = strip_ns(element.tag)
        kind = 'FontString' if tag in ('FontString', 'ButtonText') else 'Texture'
        name = self.resolve_name(element.get('name'), frame)
        existing = self.g[name] if name else None
        region = existing if existing is not None else self.H.newObject(kind, name, frame)
        if element.get('inherits'):
            region['__font'] = element.get('inherits')
        if element.get('text'):
            text = element.get('text')
            region['__text'] = self.g[text] if self.g[text] is not None else text
        if element.get('hidden') == 'true':
            region['__shown'] = False
        for child in element:
            tag = strip_ns(child.tag)
            if tag == 'Size':
                self.apply_size(region, child)
            elif tag == 'Anchors':
                self.apply_anchors(region, child)

    @staticmethod
    def dimension(element):
        if element.get('x') is not None or element.get('y') is not None:
            return element.get('x'), element.get('y')
        for child in element:
            if strip_ns(child.tag) == 'AbsDimension':
                return child.get('x'), child.get('y')
        return None, None

    def apply_size(self, region, element):
        x, y = self.dimension(element)
        if x is not None:
            region['__w'] = float(x)
        if y is not None:
            region['__h'] = float(y)

    def apply_anchors(self, region, element):
        for anchor in element:
            point = anchor.get('point')
            relative = anchor.get('relativeTo')
            parent = region['__parent']
            if relative:
                relative = self.g[self.resolve_name(relative, parent)]
            relative_point = anchor.get('relativePoint') or point
            x = y = 0
            for child in anchor:
                if strip_ns(child.tag) == 'Offset':
                    ox, oy = self.dimension(child)
                    x, y = float(ox or 0), float(oy or 0)
            region.SetPoint(region, point, relative or parent, relative_point, x, y)

    def set_script(self, frame, element):
        handler = strip_ns(element.tag)
        if element.get('function'):
            fn = self.g[element.get('function')]
        else:
            params = SCRIPT_PARAMS.get(handler, '...')
            source = 'return function (self, %s)\n%s\nend' % (params, element.text or '')
            chunk, err = self.loadstring(source, '%s:%s' % (frame['__name'], handler))
            if chunk is None:
                raise RuntimeError('%s %s: %s' % (frame['__name'], handler, err))
            fn = chunk()
        frame['__scripts'][handler] = fn

    def fire_onload(self, frame):
        if frame['__scripts']['OnLoad'] is not None:
            self.H.run(frame, 'OnLoad')


# -- checks

class Checks:
    def __init__(self):
        self.failures = []
        self.count = 0

    def check(self, condition, message):
        self.count += 1
        if not condition:
            self.failures.append(message)
            print('FAIL', message)

    def equal(self, actual, expected, message):
        self.check(actual == expected, '%s: %r, expected %r' % (message, actual, expected))

    def close(self, actual, expected, message, tolerance=1e-4):
        ok = actual is not None and abs(float(actual) - float(expected)) <= tolerance
        self.check(ok, '%s: %r, expected %r' % (message, actual, expected))


def parse_cvars(lua):
    """Every CVar the options touch, with the defaults and clamps the C++ registers."""
    H = lua.globals().H
    source = os.path.join(ROOT, 'WoW', 'Source')
    enums = {'SAMPLING_ENGINE': 0, 'SAMPLING_BOX': 1, 'SAMPLING_TENT': 2, 'UPSCALE_ENGINE': 0, 'UPSCALE_BICUBIC': 1,
             'UPSCALE_EDGE': 2, 'LIGHTNING_MAX_DISTANCE': 3000}
    awesome = re.compile(r'AwesomeCVar::(Int|Float)\("(\w+)",\s*([^,]+),\s*([^,]+),\s*([^,)]+)(?:,\s*[^,)]+)?(?:,\s*"([^"]+)")?\)')
    engine = re.compile(r'CVar::Register\("(\w+)",\s*"[^"]*",\s*[^,]+,\s*"([^"]*)"')

    def number(text):
        text = text.strip().rstrip('f')
        if text in enums:
            return enums[text]
        return float(text)

    for folder, _, files in os.walk(source):
        for file in files:
            if not file.endswith('.cpp'):
                continue
            with open(os.path.join(folder, file), encoding='utf-8', errors='replace') as handle:
                text = handle.read()
            for match in engine.finditer(text):
                H.defineCVar(match.group(1), match.group(2))
            for match in awesome.finditer(text):
                kind, name, init, low, high, fmt = match.groups()
                try:
                    init, low, high = number(init), number(low), number(high)
                except ValueError:
                    continue
                is_float = kind == 'Float'
                default = ('%g' % init) if is_float else '%d' % int(init)
                info = lua.table_from({'min': low, 'max': high, 'float': is_float, 'format': fmt or '%.2f'})
                H.defineCVar(name, default, info)

    # engine CVars registered with a computed default, which the pattern above does not read
    computed = {'gxApi': 'D3D9', 'weatherDensity': '2', 'BaseMip': '0', 'textureFilteringMode': '1',
                'componentTextureLevel': '8', 'ffxGlow': '1', 'ffxDeath': '1', 'projectedTextures': '0',
                'particleDensity': '1', 'gxMonitor': '0', 'gxRefresh': '60', 'gamma': '1', 'desktopGamma': '1',
                'windowResizeLock': '0', 'gxCursor': '1', 'gxTripleBuffer': '0', 'gxStereoEnabled': '0',
                'gxStereoConvergence': '1', 'gxStereoSeparation': '25', 'gxFixLag': '1', 'gxVSync': '1',
                'gxWindow': '1', 'gxMaximize': '1', 'useUiScale': '0', 'uiScale': '1.0'}
    for name, default in computed.items():
        if H.cvars[name.lower()] is None:
            H.defineCVar(name, default)


def snapshot(lua, names):
    get = lua.globals().GetCVar
    return {name: get(name) for name in names}


OUR_CVARS = ['hdr', 'hdrExposure', 'bloom', 'sharpen', 'ssao', 'lightPerPixel', 'lightDynamic', 'lightSpecular',
             'lightSkyAmbient', 'fogHeight', 'fogScattering', 'lightFire', 'lightDynamicDistance', 'shadowMapSize',
             'shadowSoftness', 'shadowStable', 'waterModern', 'waterReflection', 'waterRipples', 'waterTessellation',
             'waterClarity', 'waterWaveHeight', 'waterSplashes', 'skySmooth', 'skyTransition', 'skyDither',
             'weatherLightning', 'weatherLightningBrightness', 'gxApi', 'gxMonitor', 'uiUpscale', 'portraitLive',
             'maxFPS', 'maxFPSBk', 'texLodBias', 'fontMaxPixelSize', 'uiScaleDpi']
STOCK_CVARS = ['farclip', 'environmentDetail', 'groundEffectDensity', 'groundEffectDist', 'particleDensity',
               'extShadowQuality', 'useUiScale', 'uiScale', 'gxVSync', 'gxWindow']


def numeric(value):
    try:
        return round(float(value), 4)
    except (TypeError, ValueError):
        return value


def run(stock, png_folder):
    lua = lua51.LuaRuntime(unpack_returned_tuples=True)
    g = lua.globals()
    with open(os.path.join(HERE, 'stub.lua'), 'rb') as handle:
        lua.execute(handle.read())
    H = g.H
    H.toFloat = lambda x: struct.unpack('<f', struct.pack('<f', float(x)))[0]
    parse_cvars(lua)

    c = Checks()
    loader = FrameXml(lua, stock)

    # the stock FrameXML, in FrameXML.toc's order
    loader.run_lua_file(os.path.join(stock, 'GlobalStrings.lua'))
    for name in ('OptionsFrameTemplates.xml', 'OptionsPanelTemplates.xml', 'VideoOptionsFrame.xml', 'VideoOptionsPanels.xml'):
        loader.load_xml(name)
    c.equal(list(H.errors.values()), [], 'stock FrameXML loads without errors')

    # the built-in additions, as AwesomeLoadBuiltInFrameXML runs them: FrameXML.toc's Lua files with (folder, table)
    ns = lua.table()
    with open(os.path.join(BUILT_IN, 'FrameXML.toc'), encoding='utf-8') as handle:
        entries = [line.strip() for line in handle if line.strip() and not line.strip().startswith('#')]
    for entry in entries:
        loader.run_lua_file(os.path.join(BUILT_IN, entry), 'FrameXML', ns)
    c.equal(list(H.errors.values()), [], 'built-in panels load without errors')

    names = [g.VideoOptionsFrame.categoryList[i].name for i in range(1, len(g.VideoOptionsFrame.categoryList) + 1)]
    c.equal(names, ['Resolution', 'Effects', 'Lighting', 'Water and Sky', 'Advanced'], 'Video options categories')

    # the user's earlier settings: Ultra effects with a long view distance, the defaults elsewhere
    for cvar, value in (('farclip', '2927'), ('environmentDetail', '1.5'), ('groundEffectDensity', '64'),
                        ('groundEffectDist', '140'), ('particleDensity', '1'), ('extShadowQuality', '5'),
                        ('BaseMip', '0'), ('textureFilteringMode', '5'), ('weatherDensity', '3'),
                        ('componentTextureLevel', '9'), ('specular', '1'), ('ffxGlow', '1'), ('ffxDeath', '1'),
                        ('projectedTextures', '1'), ('shadowMapSize', '0'), ('gxApi', 'd3d11')):
        g.SetCVar(cvar, value)
    H.terrainMip = 1

    g.H.fire('PLAYER_ENTERING_WORLD')
    c.equal(list(H.errors.values()), [], 'PLAYER_ENTERING_WORLD without errors')

    # labels and tooltips came from the strings
    for panel_name in ('VideoOptionsLightingPanel', 'VideoOptionsWaterPanel', 'VideoOptionsAdvancedPanel'):
        panel = g[panel_name]
        for i in range(1, len(panel.controls) + 1):
            control = panel.controls[i]
            if control.type == g.CONTROLTYPE_DROPDOWN:
                label = g[control.GetName(control) + 'Label']
                c.check(label['__text'] and not label['__text'].isupper(), '%s has a label' % control.GetName(control))
                c.check(control.tooltipText is not None, '%s has a tooltip' % control.GetName(control))
                continue
            text = g[control.GetName(control) + 'Text']
            c.check(text is not None and text['__text'] and not re.fullmatch(r'[A-Z_]+', text['__text']),
                    '%s has a label (%r)' % (control.GetName(control), text and text['__text']))
            c.check(control.tooltipText is not None, '%s has a tooltip' % control.GetName(control))

    frame = g.VideoOptionsFrame
    effects = g.VideoOptionsEffectsPanel
    lighting = g.VideoOptionsLightingPanel
    water = g.VideoOptionsWaterPanel
    advanced = g.VideoOptionsAdvancedPanel

    def show(panel):
        if not frame.IsShown(frame):
            frame.Show(frame)
        g.OptionsList_DisplayPanel(panel)
        g.H.run(panel, 'OnShow')

    def okay():
        g.VideoOptionsFrameOkay_OnClick(g.VideoOptionsFrameOkay, 'LeftButton')

    def apply():
        g.VideoOptionsFrameOkay_OnClick(g.VideoOptionsFrameApply, 'LeftButton', True)

    def cancel():
        g.VideoOptionsFrameCancel_OnClick(g.VideoOptionsFrameCancel, 'LeftButton')

    watched = OUR_CVARS + STOCK_CVARS + ['BaseMip', 'textureFilteringMode', 'weatherDensity']

    # 1. open and close with Okay: nothing changes
    before = snapshot(lua, watched)
    frame.Show(frame)
    c.equal(list(H.errors.values()), [], 'opening the Video options without errors')
    view = g.VideoOptionsEffectsPanelViewDistance
    c.equal(tuple(view.GetMinMaxValues(view)), (177.0, 5017.0), 'view distance range')
    c.close(view.GetValue(view), 2927, 'view distance shows the saved 2927')
    c.equal(tuple(g.VideoOptionsEffectsPanelEnvironmentDetail.GetMinMaxValues(g.VideoOptionsEffectsPanelEnvironmentDetail)),
            (0.5, 3.0), 'environment detail range')
    c.equal(tuple(g.VideoOptionsEffectsPanelClutterDensity.GetMinMaxValues(g.VideoOptionsEffectsPanelClutterDensity)),
            (16.0, 256.0), 'ground clutter density range')
    c.equal(tuple(g.VideoOptionsEffectsPanelClutterRadius.GetMinMaxValues(g.VideoOptionsEffectsPanelClutterRadius)),
            (70.0, 600.0), 'ground clutter radius range')
    c.equal(effects.videoQuality, 6, 'a 2927-yard view distance reads as Custom quality')
    uiscale = g.VideoOptionsResolutionPanelUIScaleSlider
    c.close(uiscale.GetMinMaxValues(uiscale)[0], 0.36, 'UI scale slider goes down to 0.36')
    c.equal(g.VideoOptionsAdvancedPanelGraphicsAPIText['__text'], 'Direct3D 11', 'a lowercase gxApi still selects Direct3D 11')
    okay()
    after = snapshot(lua, watched)
    changed = {k: (before[k], after[k]) for k in watched if numeric(before[k]) != numeric(after[k])}
    c.equal(changed, {}, 'Okay without changes keeps every setting')
    c.equal(list(H.popups.values()), [], 'no restart popup without changes')

    # 2. the stock presets still read as presets on the widened Effects sliders
    g.SetCVar('farclip', '1277')
    show(effects)
    c.equal(effects.videoQuality, 5, 'stock Ultra (view distance 1277) still reads as Ultra')
    g.VideoOptionsEffectsPanelQualitySlider.SetValue(g.VideoOptionsEffectsPanelQualitySlider, 3)
    okay()
    c.close(g.GetCVar('farclip'), 727, 'choosing Fair sets the stock 727 yards')
    c.close(g.GetCVar('groundEffectDist'), 120, 'choosing Fair sets the stock grass radius')

    # 3. Lighting quality presets
    show(lighting)
    c.equal(g.VideoOptionsLightingPanelQualityLabel['__text'], 'Lighting Quality: Off', 'defaults read as Off')
    quality = g.VideoOptionsLightingPanelQualitySlider
    quality.SetValue(quality, 4)
    c.equal(g.VideoOptionsLightingPanelQualityLabel['__text'], 'Lighting Quality: High', 'the slider shows High')
    c.check(g.VideoOptionsFrameApply.IsEnabled(g.VideoOptionsFrameApply), 'a preset enables Apply')
    c.equal(g.GetCVar('hdr'), '0', 'a preset changes nothing before Okay')
    apply()
    high = {'hdr': 1, 'bloom': 0.3, 'ssao': 0.8, 'lightPerPixel': 1, 'lightDynamic': 1, 'lightSpecular': 1,
            'lightSkyAmbient': 0.5, 'fogHeight': 0.003, 'fogScattering': 0.4, 'lightFire': 1, 'sharpen': 0.5}
    for cvar, value in high.items():
        c.close(g.GetCVar(cvar), value, 'High sets %s as /lighting on does' % cvar)
    show(lighting)
    c.equal(g.VideoOptionsLightingPanelQualityLabel['__text'], 'Lighting Quality: High', 'High reads back as High')

    # a control moved off the preset makes it Custom; Cancel puts it back
    bloom = g.VideoOptionsLightingPanelBloom
    bloom.SetDisplayValue(bloom, 1.25)
    c.equal(g.VideoOptionsLightingPanelQualityLabel['__text'], 'Lighting Quality: Custom', 'moving Bloom makes it Custom')
    cancel()
    c.close(g.GetCVar('bloom'), 0.3, 'Cancel keeps bloom')
    show(lighting)
    c.equal(g.VideoOptionsLightingPanelQualityLabel['__text'], 'Lighting Quality: High', 'after Cancel it reads High again')

    # 4. requirements: HDR off disables exposure, bloom and sharpening; dynamic lights off disables firelight
    hdr = g.VideoOptionsLightingPanelHDR
    hdr.Click(hdr)
    c.check(not g.VideoOptionsLightingPanelExposure.IsEnabled(g.VideoOptionsLightingPanelExposure), 'exposure needs HDR')
    c.check(not bloom.IsEnabled(bloom), 'bloom needs HDR')
    c.check(g.VideoOptionsLightingPanelSkyAmbient.IsEnabled(g.VideoOptionsLightingPanelSkyAmbient), 'sky lighting does not')
    show(water)
    c.check(not g.VideoOptionsWaterPanelSkyDither.IsEnabled(g.VideoOptionsWaterPanelSkyDither),
            'dithering follows the unsaved HDR choice on the Lighting panel')
    cancel()
    show(lighting)
    c.check(bloom.IsEnabled(bloom), 'after Cancel bloom is enabled again')

    show(water)
    modern = g.VideoOptionsWaterPanelModern
    ripples = g.VideoOptionsWaterPanelRipples
    modern.Click(modern)
    for control in ('Reflections', 'Ripples', 'Waves', 'Clarity', 'WaveHeight', 'Splashes'):
        widget = g['VideoOptionsWaterPanel' + control]
        c.check(not widget.IsEnabled(widget), '%s needs Modern Water' % control)
    c.check(g.VideoOptionsWaterPanelSkyTransitions.IsEnabled(g.VideoOptionsWaterPanelSkyTransitions),
            'light transitions do not need Modern Water')
    modern.Click(modern)
    ripples.Click(ripples)
    c.check(not g.VideoOptionsWaterPanelWaves.IsEnabled(g.VideoOptionsWaterPanelWaves), 'waves need ripples')
    c.check(not g.VideoOptionsWaterPanelWaveHeight.IsEnabled(g.VideoOptionsWaterPanelWaveHeight),
            'wave height needs ripples through waves')
    c.check(g.VideoOptionsWaterPanelReflections.IsEnabled(g.VideoOptionsWaterPanelReflections), 'reflections do not need ripples')
    okay()
    c.equal(g.GetCVar('waterRipples'), '0', 'Okay saves ripples off')
    c.equal(g.GetCVar('waterModern'), '1', 'Modern Water stays on')

    # 5. shadow resolution: the stock 0 stays until moved, then a size
    show(lighting)
    shadow = g.VideoOptionsLightingPanelShadowResolution
    c.equal(g.VideoOptionsLightingPanelShadowResolutionText['__text'], 'Shadow Resolution: 1024', 'stock 0 shows as 1024')
    okay()
    c.equal(g.GetCVar('shadowMapSize'), '0', 'Okay leaves the stock 0 alone')
    show(lighting)
    shadow.SetDisplayValue(shadow, 3)
    c.equal(g.VideoOptionsLightingPanelShadowResolutionText['__text'], 'Shadow Resolution: 8192', 'label shows 8192')
    okay()
    c.equal(g.GetCVar('shadowMapSize'), '8192', 'shadow resolution 8192')

    # 6. Advanced: renderer, monitor, upscaling, frame rate
    while len(H.popups):
        H.popups[len(H.popups)] = None
    show(advanced)
    c.equal(g.VideoOptionsAdvancedPanelGraphicsAPIRunning['__text'], 'Running: Direct3D 11', 'running renderer shown')
    g.H.pick(g.VideoOptionsAdvancedPanelGraphicsAPI, 'D3D9')
    g.H.pick(g.VideoOptionsAdvancedPanelMonitor, 2)
    g.H.pick(g.VideoOptionsAdvancedPanelUIUpscaling, 1)
    fps = g.VideoOptionsAdvancedPanelMaxFPS
    fps.SetDisplayValue(fps, 0)
    c.equal(g.VideoOptionsAdvancedPanelMaxFPSText['__text'], 'Max Foreground FPS: No Limit', 'FPS 0 reads No Limit')
    fps.SetDisplayValue(fps, 144)
    c.equal(g.VideoOptionsAdvancedPanelMaxFPSText['__text'], 'Max Foreground FPS: 145',
            '144 snaps to the 5-FPS grid and shows it')
    okay()
    c.equal(g.GetCVar('gxApi'), 'D3D9', 'graphics API saved')
    c.equal(g.GetCVar('gxMonitor'), '2', 'monitor saved')
    c.equal(g.GetCVar('uiUpscale'), '1', 'upscaling saved')
    c.close(g.GetCVar('maxFPS'), 145, 'max FPS saved')
    c.equal(list(H.popups.values()), ['CLIENT_RESTART_ALERT'], 'a renderer change asks for a restart')
    c.equal(H.restarts, 0, 'and does not restart the device')

    # 7. Resolution panel: the DPI checkbox
    show(g.VideoOptionsResolutionPanel)
    dpi = g.VideoOptionsResolutionPanelUIScaleDpi
    c.equal(g.VideoOptionsResolutionPanelUIScaleDpiText['__text'], 'Scale with Display', 'DPI checkbox label')
    dpi.Click(dpi)
    okay()
    c.equal(g.GetCVar('uiScaleDpi'), '1', 'DPI scaling saved')

    # 8. Defaults: every panel back to the CVars' defaults
    show(lighting)
    g.VideoOptionsFrame_SetAllToDefaults()
    okay()
    for cvar in OUR_CVARS:
        if cvar == 'gxApi':
            continue
        c.equal(numeric(g.GetCVar(cvar)), numeric(g.GetCVarDefault(cvar)), 'Defaults resets %s' % cvar)

    # 9. on D3D9 the D3D11 settings are shown but disabled, with the reason
    H.api = 'D3D9'
    show(lighting)
    c.equal(g.VideoOptionsLightingPanelSubText['__text'], g.VIDEO_OPTIONS_NEEDS_D3D11, 'the Lighting panel says why')
    c.check(not hdr.IsEnabled(hdr), 'HDR disabled on D3D9')
    c.check(not quality.IsEnabled(quality), 'the preset slider too')
    c.check(shadow.IsEnabled(shadow), 'shadow resolution works on D3D9')
    c.check(g.VideoOptionsLightingPanelStableShadows.IsEnabled(g.VideoOptionsLightingPanelStableShadows), 'stable shadows too')
    show(water)
    c.check(not modern.IsEnabled(modern), 'Modern Water disabled on D3D9')
    c.check(g.VideoOptionsWaterPanelSmoothSky.IsEnabled(g.VideoOptionsWaterPanelSmoothSky), 'Smooth Sky works on D3D9')
    c.check(g.VideoOptionsWaterPanelLightning.IsEnabled(g.VideoOptionsWaterPanelLightning), 'lightning works on D3D9')
    show(advanced)
    c.check(not g.VideoOptionsAdvancedPanelTextureLodBias.IsEnabled(g.VideoOptionsAdvancedPanelTextureLodBias),
            'texture sharpness disabled on D3D9')
    cancel()
    H.api = 'D3D11'

    c.equal(list(H.errors.values()), [], 'no Lua errors in any scenario')

    # 10. layout: inside the panel, nothing overlapping
    layout(lua, c, png_folder)

    print('%d checks, %d failed' % (c.count, len(c.failures)))
    if loader.unknown_templates:
        print('templates stood in for:', ', '.join(sorted(loader.unknown_templates)))
    return not c.failures


def footprint(lua, region):
    """What a control takes up on screen: its own rectangle plus its label, value ends and check text."""
    g = lua.globals()
    rect = g.H.rect(region)
    if rect is None or rect[0] is None:
        return []
    left, bottom, width, height = rect
    kind = region['__kind']
    name = region['__name']
    boxes = []
    if kind == 'Slider':
        text = g[name + 'Text']
        label_width = g.H.textWidth(text) if text is not None else 0
        center = left + width / 2
        boxes.append((min(left, center - label_width / 2), bottom - 12, max(width, label_width), height + 12 + 12))
    elif kind == 'CheckButton':
        text = g[name + 'Text']
        text_width = g.H.textWidth(text) if text is not None else 0
        boxes.append((left + 3, bottom + 3, width - 6 + 2 + text_width, height - 6))
    elif region.type == g.CONTROLTYPE_DROPDOWN:
        boxes.append((left + 16, bottom + 4, width - 32, height - 8 + 12))
    else:
        boxes.append(rect)
    return boxes


def layout(lua, c, png_folder):
    g = lua.globals()
    container = g.VideoOptionsFramePanelContainer
    panels = ['VideoOptionsResolutionPanel', 'VideoOptionsEffectsPanel', 'VideoOptionsLightingPanel',
              'VideoOptionsWaterPanel', 'VideoOptionsAdvancedPanel']
    images = []
    for panel_name in panels:
        panel = g[panel_name]
        g.OptionsList_DisplayPanel(panel)
        cl, cb, cw, ch = g.H.rect(container)
        controls = []
        for i in range(1, len(panel.controls) + 1):
            controls.append(panel.controls[i])
        extra = [panel_name + 'QualitySlider'] if panel_name == 'VideoOptionsLightingPanel' else []
        for name in extra:
            controls.append(g[name])
        footprints = []
        for control in controls:
            boxes = footprint(lua, control)
            c.check(boxes, '%s is placed' % control['__name'])
            for box in boxes:
                footprints.append((control['__name'], box))
                inside = box[0] >= cl - 0.5 and box[1] >= cb - 0.5 and box[0] + box[2] <= cl + cw + 0.5 and \
                    box[1] + box[3] <= cb + ch + 0.5
                if panel_name not in ('VideoOptionsResolutionPanel', 'VideoOptionsEffectsPanel') or 'Dpi' in control['__name']:
                    c.check(inside, '%s stays inside the panel (%s)' % (control['__name'], [round(v) for v in box]))
        if panel_name in ('VideoOptionsLightingPanel', 'VideoOptionsWaterPanel', 'VideoOptionsAdvancedPanel',
                          'VideoOptionsResolutionPanel'):
            for i in range(len(footprints)):
                for j in range(i + 1, len(footprints)):
                    (na, a), (nb, b) = footprints[i], footprints[j]
                    if 'Dpi' not in na + nb and panel_name == 'VideoOptionsResolutionPanel':
                        continue
                    overlap = a[0] < b[0] + b[2] - 1 and b[0] < a[0] + a[2] - 1 and a[1] < b[1] + b[3] - 1 and \
                        b[1] < a[1] + a[3] - 1
                    c.check(not overlap, '%s does not overlap %s' % (na, nb))
        images.append((panel_name, (cl, cb, cw, ch), footprints, panel))

    if png_folder:
        draw(lua, images, png_folder)


def draw(lua, images, folder):
    from PIL import Image, ImageDraw
    g = lua.globals()
    scale = 2
    os.makedirs(folder, exist_ok=True)
    for panel_name, (cl, cb, cw, ch), footprints, panel in images:
        image = Image.new('RGB', (int(cw * scale) + 20, int(ch * scale) + 20), (24, 24, 24))
        pen = ImageDraw.Draw(image)

        def box(rect, color, width=1):
            left, bottom, w, h = rect
            x0 = (left - cl) * scale + 10
            y0 = (cb + ch - (bottom + h)) * scale + 10
            pen.rectangle([x0, y0, x0 + w * scale, y0 + h * scale], outline=color, width=width)
            return x0, y0

        box((cl, cb, cw, ch), (90, 90, 90), 2)
        # every placed region of the panel: boxes grey, text where it sits
        stack = [panel]
        while stack:
            region = stack.pop()
            for i in range(1, len(region['__children']) + 1):
                stack.append(region['__children'][i])
            if region == panel or not region.IsShown(region):
                continue
            rect = g.H.rect(region)
            if rect is None or rect[0] is None:
                continue
            if region['__kind'] == 'FontString' and region['__text']:
                x0, y0 = box(rect, (60, 60, 60))
                pen.text((x0 + 1, y0), region['__text'][:60], fill=(230, 200, 60))
            elif region['__kind'] in ('Slider', 'CheckButton', 'Frame', 'Button'):
                box(rect, (120, 160, 220) if region['__kind'] != 'Frame' else (80, 80, 80))
        for name, rect in footprints:
            box(rect, (220, 90, 90))
        image.save(os.path.join(folder, panel_name + '.png'))
        print('wrote', os.path.join(folder, panel_name + '.png'))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('stock', help='folder with the stock FrameXML files')
    parser.add_argument('--png', help='draw each panel into this folder')
    args = parser.parse_args()
    sys.exit(0 if run(args.stock, args.png) else 1)


if __name__ == '__main__':
    main()
