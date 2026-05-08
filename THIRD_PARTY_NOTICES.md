# Third-Party Notices

AudioPlaybackConnector2 includes or depends on the following third-party components.
Their licenses are reproduced below or linked to their canonical sources.

---

## Microsoft Windows App SDK

**Version:** 2.0.1 (with transitive dependencies listed below)  
**License:** MIT  
**Source:** https://github.com/microsoft/WindowsAppSDK  

> Copyright (c) Microsoft Corporation. All rights reserved.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

### Windows App SDK Transitive Dependencies

The following packages are distributed as part of the Windows App SDK and are
licensed under the same MIT license as the main Windows App SDK package:

| Package | Version |
|---------|---------|
| Microsoft.WindowsAppSDK.Base | 2.0.3 |
| Microsoft.WindowsAppSDK.Foundation | 2.0.20 |
| Microsoft.WindowsAppSDK.Runtime | 2.0.1 |
| Microsoft.WindowsAppSDK.WinUI | 2.0.12 |
| Microsoft.WindowsAppSDK.InteractiveExperiences | 2.0.12 |
| Microsoft.WindowsAppSDK.Widgets | 2.0.4 |
| Microsoft.WindowsAppSDK.DWrite | 2.0.26041403 |
| Microsoft.WindowsAppSDK.AI | 2.0.185 |
| Microsoft.WindowsAppSDK.ML | 2.0.325-experimental |
| Microsoft.Windows.AI.MachineLearning | 2.0.325-experimental |

---

## C++/WinRT (microsoft/cppwinrt)

**Version:** 2.0.250303.1  
**License:** MIT  
**Source:** https://github.com/microsoft/cppwinrt  

> Copyright (c) Microsoft Corporation. All rights reserved.
>
> (Same MIT license text as above)

---

## Windows Implementation Libraries (WIL)

**Version:** 1.0.260126.7  
**License:** MIT  
**Source:** https://github.com/microsoft/wil  

> Copyright (c) Microsoft Corporation. All rights reserved.
>
> (Same MIT license text as above)

---

## Microsoft Fluent UI System Icons

**Usage:** Derived toast status icon assets under `AudioPlaybackConnector2 (Package)/Images/Toast*.scale-*.png`  
**License:** MIT  
**Source:** https://github.com/microsoft/fluentui-system-icons  

> Copyright (c) Microsoft Corporation.
>
> (Same MIT license text as above)

---

## Microsoft Edge WebView2

**Version:** 1.0.3719.77  
**License:** Microsoft Edge WebView2 SDK License  
**Source:** https://github.com/MicrosoftEdge/WebView2Feedback  
**License text:** https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.3719.77/License

---

## Windows SDK Build Tools

**Package:** Microsoft.Windows.SDK.BuildTools 10.0.28000.1839  
**License:** Microsoft Software License  
**Source:** https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/

---

## Windows SDK Build Tools (MSIX)

**Package:** Microsoft.Windows.SDK.BuildTools.MSIX 1.7.251221100  
**License:** Microsoft Software License  
**Source:** https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/

---

## Windows System Libraries

The following Windows system libraries are linked at build time and are part of the
Windows operating system. They are redistributed under the terms of the Windows SDK
license agreement.

- `shell32.lib` — Windows Shell API
- `gdiplus.lib` — GDI+ graphics
- `gdi32.lib` — GDI API
- `comctl32.lib` — Common Controls
- `shlwapi.lib` — Shell Lightweight Utility API
- `dwmapi.lib` — Desktop Window Manager API
