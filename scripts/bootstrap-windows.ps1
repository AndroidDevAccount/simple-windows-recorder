# SPDX-License-Identifier: AGPL-3.0-or-later
$ErrorActionPreference = 'Stop'

winget source update --disable-interactivity
winget install --id Kitware.CMake --exact --source winget `
    --accept-package-agreements --accept-source-agreements --silent --disable-interactivity
winget install --id Ninja-build.Ninja --exact --source winget `
    --accept-package-agreements --accept-source-agreements --silent --disable-interactivity
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --source winget `
    --accept-package-agreements --accept-source-agreements --silent --disable-interactivity `
    --override '--wait --quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'

Write-Host 'Toolchain installation complete. Open a new terminal before building.'
