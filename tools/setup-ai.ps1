# SPDX-License-Identifier: MIT
# Download an independently supplied U2NetP model and CPU ONNX Runtime into this checkout.
# No model weights or runtime binaries are committed to Git. Does not overwrite existing assets.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$modelPath = Join-Path $root 'models\u2netp.onnx'
$runtimeDir = Join-Path $root 'runtime'
$runtimePath = Join-Path $runtimeDir 'onnxruntime.dll'
$modelHash = '309C8469258DDA742793DCE0EBEA8E6DD393174F89934733ECC8B14C76F4DDD8'
$runtimeHash = '0B38DF9AF21834E41E73D602D90DB5CB06DBD1CA618948B8F1D66D607AC9F3CD'
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ('aibgr-ai-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    if (Test-Path -LiteralPath $modelPath) {
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $modelPath).Hash -ne $modelHash) {
            throw "Existing $modelPath does not match the expected U2NetP model. It was not modified."
        }
        Write-Host 'Verified existing U2NetP model.'
    } else {
        $modelTemp = Join-Path $temp 'u2netp.onnx'
        Write-Host 'Downloading U2NetP model (about 5 MB)...'
        Invoke-WebRequest -Uri 'https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx' -OutFile $modelTemp -UseBasicParsing
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $modelTemp).Hash -ne $modelHash) { throw 'Model checksum mismatch; not installing it.' }
        New-Item -ItemType Directory -Force -Path (Split-Path $modelPath) | Out-Null
        Copy-Item -LiteralPath $modelTemp -Destination $modelPath
    }
    if (Test-Path -LiteralPath $runtimePath) {
        Write-Host "Existing runtime left unchanged: $runtimePath"
        Write-Host 'The plugin requires a compatible ONNX Runtime (1.16 or newer); see log.txt if loading fails.'
    } else {
        $archive = Join-Path $temp 'onnxruntime.zip'
        Write-Host 'Downloading CPU ONNX Runtime 1.23.2 (about 78 MB)...'
        Invoke-WebRequest -Uri 'https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-win-x64-1.23.2.zip' -OutFile $archive -UseBasicParsing
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash -ne $runtimeHash) { throw 'Runtime archive checksum mismatch; not installing it.' }
        $extracted = Join-Path $temp 'extract'
        Expand-Archive -LiteralPath $archive -DestinationPath $extracted
        $dll = Get-ChildItem -LiteralPath $extracted -Recurse -File -Filter 'onnxruntime.dll' | Select-Object -First 1
        if (-not $dll) { throw 'Runtime archive does not contain onnxruntime.dll.' }
        New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
        # Keep the archive's license and notices with the DLL, without overwriting existing files.
        foreach ($entry in @(@('LICENSE', 'ORT-LICENSE.txt'), @('ThirdPartyNotices.txt', 'ORT-ThirdPartyNotices.txt'))) {
            $notice = Get-ChildItem -LiteralPath $extracted -Recurse -File -Filter $entry[0] | Select-Object -First 1
            $dest = Join-Path $runtimeDir $entry[1]
            if ($notice -and -not (Test-Path -LiteralPath $dest)) {
                Copy-Item -LiteralPath $notice.FullName -Destination $dest
            } elseif (-not $notice) {
                Write-Warning "The archive has no $($entry[0]); check its upstream release notices before redistribution."
            }
        }
        Copy-Item -LiteralPath $dll.FullName -Destination $runtimePath
        $provider = Join-Path $dll.Directory.FullName 'onnxruntime_providers_shared.dll'
        $providerDest = Join-Path $runtimeDir 'onnxruntime_providers_shared.dll'
        if ((Test-Path -LiteralPath $provider) -and -not (Test-Path -LiteralPath $providerDest)) {
            Copy-Item -LiteralPath $provider -Destination $providerDest
        }
    }
    Write-Host "AI files ready in $root"
    Write-Host 'Rebuild/reinstall the plugin, close Resolve fully, and restart it to discover the model.'
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
