# SPDX-License-Identifier: MIT
# Download the official EfficientSAM-Ti split ONNX models. No existing files are overwritten.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$modelDir = Join-Path $root 'models'
$temp = Join-Path ([IO.Path]::GetTempPath()) ('aibgr-selection-' + [guid]::NewGuid())
$files = @(
    @{ Name='efficientsam_ti_encoder.onnx'; Blob='6458f72477ae216a1bd68db41ffa14802c8d54f1'; SHA='84ED466FFCC5C1F8D08409BC34A23BB364AB2C15E402CB12D4335A42BE0E0951' },
    @{ Name='efficientsam_ti_decoder.onnx'; Blob='f9310202c916fe5a4ec9a6897edae855caf023f4'; SHA='A62F8FA5EA080447C0689418D69E58F1E83E0B7ADF9C142E2BD9BCC8045C0B11' }
)
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    foreach ($file in $files) {
        $dest = Join-Path $modelDir $file.Name
        if (Test-Path -LiteralPath $dest) {
            if ((Get-FileHash -Algorithm SHA256 -LiteralPath $dest).Hash -ne $file.SHA) {
                throw "Existing $dest has a different hash; it was not changed."
            }
            Write-Host "Verified $($file.Name)"
            continue
        }
        $download = Join-Path $temp $file.Name
        $url = "https://api.github.com/repos/yformer/EfficientSAM/git/blobs/$($file.Blob)"
        Write-Host "Downloading official $($file.Name)..."
        Invoke-WebRequest -Uri $url -Headers @{Accept='application/vnd.github.raw+json'; 'User-Agent'='AiBgRemover'} -OutFile $download -UseBasicParsing
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $download).Hash -ne $file.SHA) { throw "Checksum mismatch for $($file.Name); not installing." }
        New-Item -ItemType Directory -Force -Path $modelDir | Out-Null
        Copy-Item -LiteralPath $download -Destination $dest
    }
    $runtime = Join-Path $root 'runtime\onnxruntime.dll'
    if (-not (Test-Path -LiteralPath $runtime)) { Write-Warning 'ONNX Runtime is missing. Run tools\setup-ai.ps1 first.' }
    Write-Host "EfficientSAM models ready in $modelDir. Restart Resolve after installing the plugin."
} finally {
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
