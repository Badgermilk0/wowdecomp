#ifndef WOW_SOURCE_AWESOME_AWESOMED3D_H
#define WOW_SOURCE_AWESOME_AWESOMED3D_H

#include <windows.h>
#include <d3d9.h>

// the Direct3D 9 device behind g_theGxDevicePtr (D3D9 or D3D9Ex); null on OpenGL or while the device has no context
IDirect3DDevice9* AwesomeD3dDevice();

#endif
