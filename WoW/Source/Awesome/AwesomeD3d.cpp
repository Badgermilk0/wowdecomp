#include "WoW/Source/Awesome/AwesomeD3d.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"

IDirect3DDevice9* AwesomeD3dDevice() {
    CGxDevice* device = g_theGxDevicePtr;

    if (!device || !device->m_context) {
        return nullptr;
    }

    if (device->m_api == GxApi_D3d9) {
        return static_cast<CGxDeviceD3d*>(device)->m_d3dDevice;
    }

    if (device->m_api == GxApi_D3d9Ex) {
        return static_cast<CGxDeviceD3d9Ex*>(device)->m_d3dDevice;
    }

    return nullptr;
}
