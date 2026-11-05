# SPDX-License-Identifier: MIT
# Optional BRIA model. The model's license is separate from this script/plugin.
param(
    [switch]$AcceptBriaTerms,
    [string]$ModelFile = ''
)
$ErrorActionPreference = 'Stop'
if (-not $AcceptBriaTerms) {
    throw 'Read BRIA model card and license first: https://huggingface.co/briaai/RMBG-1.4 . For non-commercial use (or with a separate commercial agreement), rerun with -AcceptBriaTerms.'
}
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dest = Join-Path $root 'models\bria_rmbg14_quantized.onnx'
$sha = 'A6648479275DFD0EDE0F3A8ABC20AA5C437B394681B05E5AF6D268250AAF40F3'
if (Test-Path -LiteralPath $dest) {
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $dest).Hash -ne $sha) { throw "Existing $dest has a different hash; it was not changed." }
    Write-Host "Verified existing model: $dest"
    return
}
$temp = Join-Path ([IO.Path]::GetTempPath()) ('aibgr-bria-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $download = Join-Path $temp 'model.onnx'
    if ($ModelFile) {
        Copy-Item -LiteralPath $ModelFile -Destination $download
    } else {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $url = 'https://huggingface.co/briaai/RMBG-1.4/resolve/36193ca99011a46a75e1862696b5a5a30ca25527/onnx/model_quantized.onnx'
        Write-Host 'Downloading the official quantized BRIA model (about 44 MB)...'
        try { Invoke-WebRequest -Uri $url -OutFile $download -UseBasicParsing }
        catch { throw 'Download failed. Visit the official model page, follow its access/license conditions, download onnx/model_quantized.onnx, then rerun with -AcceptBriaTerms -ModelFile "path\to\model_quantized.onnx".' }
    }
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $download).Hash -ne $sha) { throw 'BRIA model checksum mismatch; not installing it.' }
    New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
    Copy-Item -LiteralPath $download -Destination $dest
    Write-Host "BRIA model ready: $dest"
    Write-Host 'Use Retry AI Load or restart Resolve after the model and plugin are installed.'
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
